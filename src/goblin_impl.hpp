#include "config.hpp"
#include "goblin_state.hpp"
#include <boost/variant.hpp>


/* Implementations of goblins are active objects. They are controlled by shared pointers.
 * */
struct goblin_impl : std::enable_shared_from_this<goblin_impl> {

    GoblinState goblin_state_;
    bool running_ = false;

    goblin_impl(asio::io_service& executor, std::string name) : executor_(executor), name_(name) {}

    void start() {
        auto lock = get_lock();
        goblin_state_.start();
        running_ = true;
    }

    void stop() {
        auto lock = get_lock();
        goblin_state_.stop();
        running_ = false;
    }

    auto name_copy() const {
        auto lock = lock_type(mutex_);
        return name_;
    }

    bool is_dead() {
        auto lock = lock_type(mutex_);
        return (not running_) or goblin_state_.is_flag_active<PositivelyDead>();
    }

    auto get_weak_ptr() {
        return std::weak_ptr<goblin_impl>(shared_from_this());
    }

    auto get_weak_ptr() const {
        return std::weak_ptr<goblin_impl const>(shared_from_this());
    }

    using mutex_type = std::mutex;
    using lock_type = std::unique_lock<mutex_type>;
    auto get_lock() const -> lock_type { return lock_type(mutex_); }

    auto get_executor() const -> asio::io_service& { return executor_; }

    template<class Message>
    void process_event(Message&& message)
    {
        auto lock = get_lock();
        goblin_state_.process_event(message);
    }

    template<class...Messages>
    void process_events(Messages&&...msgs)
    {
        auto lock = get_lock();
        using expand = int[];
        void(expand{
                (goblin_state_.process_event(msgs), 0)...
        });
    }



    asio::io_service& executor_;
    mutable mutex_type mutex_;
    std::string name_;
};

