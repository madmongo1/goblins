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

namespace msm = boost::msm;
namespace msmf = boost::msm::front;
namespace msmb = boost::msm::back;


struct goblin_impl_proxy {
    goblin_impl_proxy(std::shared_ptr<goblin_impl> impl)
            : impl_(impl) {
    }

    goblin_impl_proxy(const goblin_impl_proxy &) = delete;

    goblin_impl_proxy &operator=(const goblin_impl_proxy &) = delete;

    ~goblin_impl_proxy() {
        if (started_) {
            impl_->stop();
        }
    }

    void start() {
        impl_->start();
        started_ = true;
    }

    auto get_impl_ptr() -> goblin_impl * {
        return impl_.get();
    }

    std::shared_ptr<goblin_impl> impl_;
    bool started_ = false;
};

struct goblin_service : asio::detail::service_base<goblin_service> {
    using impl_class = goblin_impl;

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
        auto proxy = std::make_shared<goblin_impl_proxy>(shared_impl);
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
            executor.post([handler, args...]() mutable { handler(args...); });
        };
    }

    template<class Handler>
    auto on_birth(implementation_type &impl, Handler &&handler) {
        auto async_handler = make_async_completion_handler(std::forward<Handler>(handler));

        auto async_handler2 = async_handler;
        auto callback = std::function<void()>(async_handler2);
        impl->process_event(EventAddBirthHandler{callback});
        add_birth_handler(*impl, async_handler);
    }

private:

    template<class State, class Handler>
    void add_birth_handler(impl_class &impl, State &state, Handler &&handler) {
        // do nothing
    }

    template<class Handler>
    void add_birth_handler(impl_class &impl, impl_class::unborn &state, Handler &&handler) {
        state.birth_handlers_.emplace_back(std::forward<Handler>(handler));
    }

    template<class Handler>
    void add_birth_handler(impl_class &impl, impl_class::killing_folk &state, Handler &&handler) {
        handler();
    }

    template<class Handler>
    void add_birth_handler(impl_class &impl, impl_class::dead &state, Handler &&handler) {
        // do nothing - handler does not get called.
    }


    template<class Handler>
    void add_birth_handler(impl_class &impl, Handler &&handler) {
        auto lock = impl.get_lock();

        boost::apply_visitor([this, &impl, &handler](auto &state) {
                                 this->add_birth_handler(impl, state, std::forward<Handler>(handler));
                             },
                             impl.get_state());
    }

public:

    auto name_copy(implementation_type const &impl) {
        return impl->name_copy();
    }

    auto is_dead(implementation_type const &impl) const {
        return impl->is_dead();
    }

    auto be_born(implementation_type &impl) {
        // let's implement this as a background job
        auto weak_impl = impl->get_weak_ptr();
        auto my_work = asio::io_service::work(get_io_service());
        worker_service_.get_worker_executor().post([this, my_work, weak_impl]() {
            if (auto impl = weak_impl.lock()) {
                impl->process_event(GoblinBorn{*impl});
                this->handle_be_born(*impl);
            }
        });
    }


private:


    void handle_be_born(impl_class &impl, impl_class::unborn &state) {
        auto lock = impl.get_lock();
        auto handlers = state.take_birth_handlers();
        impl.set_state(impl_class::killing_folk());
        lock.unlock();
        impl_class::unborn::fire_birth_handlers(handlers);
    }

    template<class InvalidState>
    void handle_be_born(impl_class &impl, InvalidState &state) {
        std::cerr << "invalid state to be born in" << std::endl;
    }

    void handle_be_born(impl_class &impl) {
        boost::apply_visitor([this, &impl](auto &state) { this->handle_be_born(impl, state); }, impl.get_state());
    }

    auto get_worker_executor() const -> asio::io_service & {
        return worker_service_.get_worker_executor();
    }

private:
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
struct goblin {
    using service_type = goblin_service;
    using implementation_type = goblin_service::implementation_type;

    goblin(asio::io_service &owner) :
            service_(std::addressof(asio::use_service<service_type>(owner))),
            impl_(get_service().construct()) {}

    // allow goblins to be privately, - don't store copies in client code
private:
    goblin(goblin const &r)
            : service_(r.service_)
            , impl_(r.get_implementation()->shared_from_this()) {}

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
        get_service().on_birth(get_implementation(),
                               [self = *this, handler = std::forward<Handler>(handler)]() mutable {
                                   handler(self);
                               });
    }

    auto name() const -> std::string {
        return get_service().name_copy(get_implementation());
    }

    bool is_dead() const {
        return get_service().is_dead(get_implementation());
    }

    void be_born() {
        get_service().be_born(get_implementation());
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


int main() {

    asio::io_service executor;
    run_pool pool_of_life(executor, "pool of life");

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
/*
        goblins.on_death([&](auto& gob){
            if (all_dead()) {
                pool_of_life.stop();
            }
        });
*/
    }
    all_goblins([](auto &gob) {
        gob.on_birth([](auto &gob) {
            std::cout << gob.name() << " lives!" << std::endl;
        });
    });

    all_goblins([](auto &gob) {
        gob.be_born();
    });


    pool_of_life.join();

}
