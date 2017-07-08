#include "config.hpp"
#include "run_pool.hpp"
#include "worker_thread_service.hpp"
#include "goblin_name_generator.hpp"
#include "goblin_impl.hpp"

#include <boost/variant.hpp>
#include <boost/signals2.hpp>
#include <iostream>
#include <set>
#include <thread>
#include <array>

#include <boost/msm/front/state_machine_def.hpp>
#include <boost/msm/back/state_machine.hpp>
#include <boost/msm/front/functor_row.hpp>
#include <boost/msm/front/common_states.hpp>

#include "use_unique_future.hpp"

template<class ...> using void_t = void;

template<class, class = void>
struct is_callable_with_error_code : std::false_type {
};

template<class F>
struct is_callable_with_error_code
        <F,
                void_t<
                        decltype(std::declval<F>()(std::declval<asio::error_code>()))
                >
        >
        : std::true_type {
};

template<class Implementation>
struct impl_proxy {
    impl_proxy(std::shared_ptr<Implementation> impl)
            : impl_(impl) {
    }

    impl_proxy(const impl_proxy &) = delete;

    impl_proxy &operator=(const impl_proxy &) = delete;

    ~impl_proxy() {
        if (started_) {
            impl_->stop();
        }
    }

    void start() {
        impl_->start();
        started_ = true;
    }

    auto get_impl_ptr() -> Implementation * {
        return impl_.get();
    }

    std::shared_ptr<Implementation> impl_;
    bool started_ = false;
};

struct goblin_service : asio::detail::service_base<goblin_service> {
    using impl_class = goblin_impl;

    using implementation_proxy = impl_proxy<impl_class>;

    /* specify the relationship between handle and implementation here */
    using implementation_type = std::shared_ptr<impl_class>;

    goblin_service(asio::io_service &owner) : asio::detail::service_base<goblin_service>(owner) {}

    implementation_type construct() {

        /*
         * care - a goblin impl uses asio objects and therefore it's helpful to control it with a shared
         *        pointer. However, the 'handle' class - goblin has unique ownership semantics.
         *        It is convenient to separate the lifetime of the goblin from the lifetime of the
         *        implementation. The death of a goblin handle can signal to the impl that it should start
         *        an orderly shutdown.
         */

        auto shared_impl = std::make_shared<impl_class>(get_worker_executor(), name_generator_());
        auto proxy = std::make_shared<implementation_proxy>(shared_impl);
        // use the lifetime of the proxy to refer to the implementation
        auto result = implementation_type {proxy, proxy->get_impl_ptr()};
        auto lock = cache_lock(cache_mutex_);
        goblin_cache_.insert(result);
        lock.unlock();
        proxy->start();
        return result;
    };

    template<class Handler>
    auto make_async_completion_handler(Handler &&handler) {
        auto &executor = this->get_io_service();
        auto work = asio::io_service::work(executor);

        return [&executor, work, handler = std::forward<Handler>(handler)](auto &&... args) mutable {
            executor.post([handler, args...]() mutable {
                handler(args...);
            });
        };
    }

    template<class WaitHandler>
    auto async_spawn(implementation_type &impl, WaitHandler &&handler) {

        asio::detail::async_result_init<
                WaitHandler, void(boost::system::error_code)> init(
                std::forward<WaitHandler>(handler));

        auto async_handler = make_async_completion_handler(std::move(init.handler));
        impl->process_events(EventAddBirthHandler{async_handler},
                             GoblinBorn{*impl});

        return init.result.get();
    }

    template<class WaitHandler>
    auto on_birth(implementation_type &impl, WaitHandler &&handler) {

        asio::detail::async_result_init<
                WaitHandler, void(boost::system::error_code)> init(
                std::forward<WaitHandler>(handler));

        auto async_handler = make_async_completion_handler(std::move(init.handler));
        impl->process_event(EventAddBirthHandler{async_handler});

        //  service_impl_.async_wait(impl, init.handler);

        return init.result.get();
    }

    /** cause a handler run when the goblin dies.
     * The handler will be called exactly once.
     * @tparam Handler
     * @param impl
     * @param handler
     * @return
     */

    template<class WaitHandler>
    auto wait_death(implementation_type &impl, WaitHandler &&handler) {

        asio::detail::async_result_init<
                WaitHandler, void(boost::system::error_code)> init(
                std::forward<WaitHandler>(handler));

        auto async_handler = make_async_completion_handler(std::move(init.handler));
        impl->process_event(EventAddDeathHandler{async_handler});

        //  service_impl_.async_wait(impl, init.handler);

        return init.result.get();
    }

    auto name_copy(implementation_type const &impl) {
        return impl->name_copy();
    }

    auto is_dead(implementation_type const &impl) const {
        return impl->is_dead();
    }

    auto be_born(implementation_type &impl) {
        // let's implement this as a background job
        impl->process_event(GoblinBorn{*impl});
    }

    auto die(implementation_type &impl) {
        impl->process_event(GoblinDies{*impl});
    }


private:

    auto get_worker_executor() const -> asio::io_service & {
        return worker_service_.get_worker_executor();
    }

    using cache_mutex = std::mutex;
    using cache_lock = std::unique_lock<cache_mutex>;
    using goblin_cache = std::set<std::weak_ptr<goblin_impl>, std::owner_less<std::weak_ptr<goblin_impl>>>;

    void shutdown_service() override {

    }

    worker_thread_service &worker_service_ = asio::use_service<worker_thread_service>(get_io_service());
    cache_mutex cache_mutex_;
    goblin_cache goblin_cache_;
    goblin_name_generator name_generator_{};

};

/** This is a goblin.
 * A goblin lives in an io_service.
 * A goblin has an automatically generated name
 * A goblin will do nothing until it is told to start_killing
 * Then it will kill people at random until it is killed.
 * It will report to any interested listeners that it has killed someone or it has died.
 */
template<class Outer>
struct goblin_interface {
    template<class Handler>
    auto async_spawn(Handler &&handler) {
        auto self = outer_self();
        return self->get_service().async_spawn(self->get_implementation(),
                                               std::forward<Handler>(handler));
    }

    auto name() const -> std::string {
        auto self = outer_self();
        return self->get_service().name_copy(self->get_implementation());
    }

    bool is_dead() const {
        auto self = outer_self();
        return self->get_service().is_dead(self->get_implementation());
    }

    void be_born() {
        auto self = outer_self();
        self->get_service().be_born(self->get_implementation());
    }

    void die() {
        auto self = outer_self();
        self->get_service().die(self->get_implementation());
    }

private:
    Outer *outer_self() { return static_cast<Outer *>(this); }

    const Outer *outer_self() const { return static_cast<const Outer *>(this); }
};

struct goblin_ref : goblin_interface<goblin_ref> {
    using service_type = goblin_service;
    using implementation_type = goblin_service::implementation_type;

    goblin_ref(service_type &service, implementation_type impl)
            : service_(std::addressof(service)),
              impl_(std::move(impl)) {}

    auto get_implementation() -> implementation_type & {
        return impl_;
    }

    auto get_implementation() const -> implementation_type const & {
        return impl_;
    }

    auto get_service() const -> service_type & {
        return *service_;
    }

    auto get_executor() const -> asio::io_service & {
        return get_service().get_io_service();
    }


private:
    service_type *service_;
    implementation_type impl_;

};

struct goblin : goblin_interface<goblin> {
    using service_type = goblin_service;
    using implementation_type = goblin_service::implementation_type;

    goblin(asio::io_service &owner) :
            service_(std::addressof(asio::use_service<service_type>(owner))),
            impl_(get_service().construct()) {}

    template<class WaitHandler>
    goblin(asio::io_service &owner, WaitHandler &&handler) :
            service_(std::addressof(asio::use_service<service_type>(owner))),
            impl_(get_service().construct()) {

        get_service().on_birth(get_implementation(), std::forward<WaitHandler>(handler));
        be_born();
    }

    operator goblin_ref() const {
        return goblin_ref(get_service(), get_implementation().get()->shared_from_this());
    }

    auto ref() const {
        return goblin_ref(*this);
    }

    // allow goblins to be privately, - don't store copies in client code
private:
    goblin(goblin const &r)
            : service_(r.service_), impl_(r.get_implementation()->shared_from_this()) {}

    goblin &operator=(goblin const &) = delete;

public:
    goblin(goblin &&) = default;

    goblin &operator=(goblin &&) = default;

    ~goblin() = default;

    /** compare two goblins for equality.
     * Two goblins are considered equal if they reference the same internal goblin state.
     * Two goblin states that happen to share a name are not equal.
     * A goblin copy is not equal to its parent if the parent has been moved from.
     * @return
     */
    bool operator==(goblin const &r) const {
        return get_implementation().get() == r.get_implementation().get();
    }

    // boilerplate


    auto get_service() const -> service_type & {
        return *service_;
    }

    auto get_executor() const -> asio::io_service & {
        return get_service().get_io_service();
    }


    /** Request the goblin to call a handler when born.
     * The handler shall be called exactly once, as if by a call to get_executor().post().
     * The handler will be invoked with the signature void(goblin&). The handler may use the
     * goblin reference to perform gobliny actions but should not seek to store or copy it as it
     * maintains a shared reference to the internal goblin state
     * @tparam Handler
     * @param handler
     * @return
     */
    template<class Handler>
    auto on_birth(Handler &&handler) {
        return get_service().on_birth(get_implementation(), std::forward<Handler>(handler));
    }

    /** Request the goblin to call a handler when it dies (or if it's already dead).
     * The handler shall be called exactly once, as if by a call to get_executor().post().
     * The handler will be invoked with the signature void(goblin&). The handler may use the
     * goblin reference to perform gobliny actions but should not seek to store or copy it as it
     * maintains a shared reference to the internal goblin state
     * @tparam Handler
     * @param handler
     * @return
     */

    template<class WaitHandler>
    auto wait_death(WaitHandler &&handler) {
        // If you get an error on the following line it means that your handler does
        // not meet the documented type requirements for a WaitHandler.
        //BOOST_ASIO_WAIT_HANDLER_CHECK(WaitHandler, handler) type_check;

        return get_service().wait_death(get_implementation(),
                                        std::forward<WaitHandler>(handler));
    }


    auto get_implementation() -> implementation_type & {
        return impl_;
    }

    auto get_implementation() const -> implementation_type const & {
        return impl_;
    }

private:
    service_type *service_;
    implementation_type impl_;
};


template<class AsioExecutor>
struct asio_executor {
    asio_executor(AsioExecutor &exec) : executor_(exec) {}

    void close() { executor_.stop(); }

    bool closed() { return executor_.stopped(); }

    template<class Closure>
    void submit(Closure &&closure) {
        executor_.dispatch(std::forward<Closure>(closure));
    }

    bool try_executing_one() {
        auto ran = executor_.poll_one();
        return ran != 0;
    }

private:
    AsioExecutor &executor_;
};

template<class AsioExecutor>
auto make_asio_executor(AsioExecutor &exec) {
    return asio_executor<AsioExecutor>(exec);
}

int main() {

    asio::io_service executor;
    run_pool pool_of_life(executor, "pool of life");
    auto goblin_exec = make_asio_executor(executor);


    std::vector<goblin> goblins;
    auto all_dead = [&]() {
        return std::all_of(goblins.begin(),
                           goblins.end(),
                           [](auto const &g) {
                               return g.is_dead();
                           });
    };

    auto all_goblins = [&](auto f) {
        for (auto &gob : goblins) {
            f(gob);
        }
    };

    for (int i = 0; i < 3; ++i) {
        goblins.emplace_back(executor);
    }

    all_goblins([&](auto &gob) {
        gob.async_spawn(use_unique_future)
                .then(goblin_exec, [name = gob.name()](auto f) {
                    try {
                        f.get();
                        std::cout << std::this_thread::get_id() << " : " << name << " lives!" << std::endl;
                    }
                    catch (std::exception const &e) {
                        std::cout << name << " not alive because: " << e.what() << std::endl;
                    }
                });
    });

    asio::deadline_timer t(executor);
    t.expires_from_now(boost::posix_time::seconds(1));
    t.async_wait(use_unique_future)
            .then(goblin_exec, [&](auto f) {
                try {
                    f.get();
                    all_goblins([](auto &gob) { gob.die(); });
                }
                catch (...) {

                }
            });


    all_goblins([&](auto &gob) {
        gob.wait_death(use_unique_future)
                .then(goblin_exec, [mygoblin = gob.ref()](auto &&f) {
                    try {
                        f.get();
                        std::cout << mygoblin.name() << " died" << std::endl;
                    }
                    catch (...) {
                        std::cout << mygoblin.name() << " was deleted before he could even die!\n";
                    }
                });
    });

    goblins.erase(goblins.begin() + 2);


    pool_of_life.join();

}
