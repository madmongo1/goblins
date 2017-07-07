#include <boost/asio.hpp>
#include <boost/variant.hpp>
#include <boost/signals2.hpp>
#include <iostream>
#include <set>
#include <thread>
#include <array>


namespace asio {
    using namespace boost::asio;
    using error_code = boost::system::error_code;
}

struct run_pool {
    run_pool(asio::io_service &executor)
            : executor_(executor) {}

    ~run_pool() {
        stop();
    }

    void add_thread() {
        threads_.emplace_back([&, work = asio::io_service::work(executor_)] { this->run(); });
    }

    void stop() {
        executor_.stop();
        join_threads();
    }

    void join() {
        run();
        join_threads();
    }

private:

    void join_threads() {
        for (auto &&t : threads_) {
            if (t.joinable()) t.join();
        }
        threads_.clear();
    }

    void run() {
        while (!executor_.stopped()) {
            try {
                executor_.run();
            }
            catch (std::exception const &e) {
                std::cerr << e.what() << std::endl;
            }
        }
    }

    asio::io_service &executor_;
    std::vector<std::thread> threads_;
};


struct worker_thread_service : asio::detail::service_base<worker_thread_service> {

    worker_thread_service(asio::io_service &owner)
            : asio::detail::service_base<worker_thread_service>(owner) {
        worker_pool_.add_thread();
    }

    auto get_worker_executor() -> asio::io_service&
    {
        return worker_executor_;
    }

    void shutdown_service() override
    {
        worker_pool_.stop();
    }

    asio::io_service worker_executor_;
    run_pool worker_pool_ {worker_executor_};

};

class goblin_name_generator {
    struct impl {
        auto generate() {
            auto result = names[pos];
            if (iteration) {
                result += " " + std::to_string(iteration);
            }
            if (++pos >= names.size()) {
                iteration++;
                pos = 0;
            }
            return result;
        }

        int iteration = 0;
        int pos = 0;
        std::array<std::string, 3> names = {"yarr!", "gnurgghhh!", "fgumschak!"};
    };

    static impl &get_impl() {
        static impl _{};
        return _;
    }

public:
    std::string operator()() const {
        return get_impl().generate();
    }
};

/* Implementations of goblins are active objects. They are controlled by shared pointers.
 * */
struct goblin_impl : std::enable_shared_from_this<goblin_impl> {

    goblin_impl(std::string name) : name_(name) {}

    using birth_handler_function = std::function<void()>;

    struct is_dead_traits {
        static constexpr bool is_dead() { return true; }
    };

    struct is_not_dead_traits {
        static constexpr bool is_dead() { return true; }
    };

    struct unborn : is_not_dead_traits {
        void add_birth_handler(birth_handler_function f) {
            birth_handlers_.push_back(std::move(f));
        }

        auto take_birth_handlers() {
            auto copy = std::move(birth_handlers_);
            birth_handlers_.clear();
            return copy;
        }

        static auto fire_birth_handlers(std::vector<birth_handler_function>& handlers)
        {
            for (auto &handler : handlers) {
                handler();
            }
        }

        std::vector<birth_handler_function> birth_handlers_;
    };

    struct killing_folk : is_not_dead_traits {
        void enter() {

        }

        void add_birth_handler(birth_handler_function f) {
            // already alive - notify immediately
            f();
        }
    };

    struct dead : is_dead_traits {
        void add_birth_handler(birth_handler_function f) {
            // will never notify - dump it
        }
    };

    using state_type = boost::variant<unborn, killing_folk, dead>;

    void start() {

    }

    void stop() {

    }

    template<class State>
    void handle_be_born(State const& state)
    {
        std::cerr << "logic error - invalid state";
    }
    template<class State>
    void handle_be_born(State& state)
    {
        std::cerr << "logic error - invalid state";
    }

    void add_birth_handler(std::function<void()> handler) {
        auto lock = lock_type(mutex_);
        boost::apply_visitor([&handler](auto &state) {
                                 state.add_birth_handler(std::move(handler));
                             },
                             state_);

    }

    auto name_copy() const {
        auto lock = lock_type(mutex_);
        return name_;
    }

    bool is_dead() const {
        auto lock = lock_type(mutex_);
        return boost::apply_visitor([](auto const &state) {
                                        return state.is_dead();
                                    },
                                    state_);
    }

    auto get_weak_ptr()  {
        return std::weak_ptr<goblin_impl>(shared_from_this());
    }
    auto get_weak_ptr() const {
        return std::weak_ptr<goblin_impl const>(shared_from_this());
    }

    template<class NewState>
    auto set_state(NewState&& ns) -> NewState&
    {
        state_ = std::forward<NewState>(ns);
        return boost::get<NewState>(state_);
    }

    auto get_lock() const { return lock_type(mutex_); }

    auto get_state() -> state_type& {
        return state_;
    }



    using mutex_type = std::mutex;
    using lock_type = std::unique_lock<mutex_type>;

    mutable mutex_type mutex_;
    std::string name_;
    state_type state_ = unborn();
};

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

        auto shared_impl = std::make_shared<impl_class>(name_generator_());
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
        impl->add_birth_handler(make_async_completion_handler(std::forward<Handler>(handler)));
    }

    auto name_copy(implementation_type const &impl) {
        return impl->name_copy();
    }

    auto is_dead(implementation_type const &impl) const {
        return impl->is_dead();
    }

    auto be_born(implementation_type& impl) {
        // let's implement this as a background job
        auto weak_impl = impl->get_weak_ptr();
        worker_service_.get_worker_executor().post([this, weak_impl]() {
            if (auto impl = weak_impl.lock()) {
                this->handle_be_born(*impl);
            }
        });
    }


private:

    void handle_be_born(impl_class& impl, impl_class::unborn& state)
    {
        auto lock = impl.get_lock();
        auto handlers = state.take_birth_handlers();
        impl.set_state(impl_class::killing_folk());
        lock.unlock();
        impl_class::unborn::fire_birth_handlers(handlers);
    }

    template<class InvalidState>
    void handle_be_born(impl_class& impl, InvalidState& state)
    {
        std::cerr << "invalid state to be born in" << std::endl;
    }

    void handle_be_born(impl_class& impl)
    {
        boost::apply_visitor([this, &impl](auto& state){ this->handle_be_born(impl, state); }, impl.get_state());
    }




private:
    using cache_mutex = std::mutex;
    using cache_lock = std::unique_lock<cache_mutex>;
    using goblin_cache = std::set<std::weak_ptr<goblin_impl>, std::owner_less<std::weak_ptr<goblin_impl>>>;

    void shutdown_service() override {

    }

    worker_thread_service& worker_service_ = asio::use_service<worker_thread_service>(get_io_service());
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
    goblin(goblin const &) = default;

    goblin &operator=(goblin const &) = default;

public:
    goblin(goblin &&) = default;

    goblin &operator=(goblin &&) = default;

    ~goblin() = default;

    // boilerplate


    auto get_service() const -> service_type & {
        return *service_;
    }

    auto get_executor() const -> asio::io_service & {
        return get_service().get_io_service();
    }

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
    std::vector<goblin> goblins;

    asio::io_service executor;
    run_pool pool_of_life(executor);

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
        gob.on_birth([&](auto &gob) {
            std::cout << gob.name() << " lives!" << std::endl;
        });
    });

    all_goblins([](auto &gob) {
        gob.be_born();
    });


    pool_of_life.join();

}
