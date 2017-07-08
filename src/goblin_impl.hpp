#include "config.hpp"
#include "goblin_state.hpp"
#include <boost/variant.hpp>


/* Implementations of goblins are active objects. They are controlled by shared pointers.
 * */
struct goblin_impl : std::enable_shared_from_this<goblin_impl> {

    GoblinState goblin_state_;

    goblin_impl(asio::io_service& executor, std::string name) : executor_(executor), name_(name) {}

    using birth_handler_function = std::function<void()>;

    struct is_dead_traits {
        static constexpr bool is_dead() { return true; }
    };

    struct is_not_dead_traits {
        static constexpr bool is_dead() { return true; }
    };

    struct unborn : is_not_dead_traits {

        auto take_birth_handlers() {
            auto copy = std::move(birth_handlers_);
            birth_handlers_.clear();
            return copy;
        }

        static auto fire_birth_handlers(std::vector<birth_handler_function> &handlers) {
            for (auto &handler : handlers) {
                handler();
            }
        }

        std::vector<birth_handler_function> birth_handlers_;
    };

    struct killing_folk : is_not_dead_traits {
    };

    struct dead : is_dead_traits {
    };

    using state_type = boost::variant<unborn, killing_folk, dead>;

    void start() {
        auto lock = get_lock();
        std::cout << "starting " << name_ << std::endl;
        goblin_state_.start();
    }

    void stop() {
        auto lock = get_lock();
        std::cout << "stopping " << name_ << std::endl;
        goblin_state_.stop();
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

    auto get_weak_ptr() {
        return std::weak_ptr<goblin_impl>(shared_from_this());
    }

    auto get_weak_ptr() const {
        return std::weak_ptr<goblin_impl const>(shared_from_this());
    }

    template<class NewState>
    auto set_state(NewState &&ns) -> NewState & {
        state_ = std::forward<NewState>(ns);
        return boost::get<NewState>(state_);
    }

    using mutex_type = std::mutex;
    using lock_type = std::unique_lock<mutex_type>;
    auto get_lock() const -> lock_type { return lock_type(mutex_); }

    auto get_state() -> state_type & {
        return state_;
    }

    auto get_executor() const -> asio::io_service& { return executor_; }

    template<class Message>
    void process_event(Message const& message)
    {
        auto lock = get_lock();
        goblin_state_.process_event(message);
    }


    asio::io_service& executor_;
    mutable mutex_type mutex_;
    std::string name_;
    state_type state_ = unborn();
};

