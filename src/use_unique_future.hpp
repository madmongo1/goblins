//
// Created by Richard Hodges on 08/07/2017.
//

#pragma once

#include <exception> // current_exception, make_exception_ptr
#include <memory> // make_shared, shared_ptr
#include <thread> // thread
#include <utility> // move

#define BOOST_RESULT_OF_USE_DECLTYPE
#define BOOST_THREAD_PROVIDES_FUTURE_CONTINUATION

#include <boost/asio.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/thread/future.hpp>

/// @brief Class used to indicate an asynchronous operation should return
///        a boost::unique_future.
class use_unique_future_t {};

/// @brief A special value, similiar to std::nothrow.
constexpr use_unique_future_t use_unique_future {};

namespace detail {

/// @brief Completion handler to adapt a boost::promise as a completion
///        handler.
template <typename T>
class unique_promise_handler;

/// @brief Completion handler to adapt a void boost::promise as a completion
///        handler.
template <>
class unique_promise_handler<void>
{
public:
  /// @brief Construct from use_unique_future special value.
  explicit unique_promise_handler(use_unique_future_t)
    : promise_(std::make_shared<boost::promise<void> >())
  {}

  void operator()(const boost::system::error_code& error)
  {
    // On error, convert the error code into an exception and set it on
    // the promise.
    if (error)
      promise_->set_exception(
          std::make_exception_ptr(boost::system::system_error(error)));
    // Otherwise, set the value.
    else
      promise_->set_value();
  }

//private:
  std::shared_ptr<boost::promise<void> > promise_;
};

// Ensure any exceptions thrown from the handler are propagated back to the
// caller via the future.
template <typename Function, typename T>
void asio_handler_invoke(
    Function function,
    unique_promise_handler<T>* handler)
{
  // Guarantee the promise lives for the duration of the function call.
  std::shared_ptr<boost::promise<T> > promise(handler->promise_);
  try
  {
    function();
  }
  catch (...)
  {
    promise->set_exception(std::current_exception());
  }
}

} // namespace detail

namespace boost {
namespace asio {

/// @brief Handler type specialization for use_unique_future.
template <typename ReturnType>
struct handler_type<
    use_unique_future_t,
    ReturnType(boost::system::error_code)>
{
  typedef ::detail::unique_promise_handler<void> type;
};

/// @brief Handler traits specialization for unique_promise_handler.
template <typename T>
class async_result< ::detail::unique_promise_handler<T> >
{
public:
  // The initiating function will return a boost::unique_future.
  typedef boost::unique_future<T> type;

  // Constructor creates a new promise for the async operation, and obtains the
  // corresponding future.
  explicit async_result(::detail::unique_promise_handler<T>& handler)
  {
    value_ = handler.promise_->get_future();
  }

  // Obtain the future to be returned from the initiating function.
  type get() { return std::move(value_); }

private:
  type value_;
};

} // namespace asio
} // namespace boost
