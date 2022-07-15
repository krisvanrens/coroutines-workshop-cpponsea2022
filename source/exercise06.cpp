// - Provide `is_ready()` member function in promise
//   that will return `true` if the coroutine is suspended
//   at its final suspend point
// - Associate an awaiter type with a `task<T>` that
//   - suspends if the coroutine is not ready
//   - on resume
//     - does nothing for `task<void>` case
//     - otherwise, returns a result stored in `std::optional`
//   - no action is being done on suspend
//     - we will work on it later

#include <concepts>
#include <coroutine>
#include <memory>
#include <optional>

struct coro_deleter {
  template<typename Promise>
  void operator()(Promise* promise) const noexcept
  {
    auto handle = std::coroutine_handle<Promise>::from_promise(*promise);
    if(handle)
      handle.destroy();
  }
};
template<typename T>
using promise_ptr = std::unique_ptr<T, coro_deleter>;


// ********* TASK *********

namespace detail {

template<typename T>
struct task_promise_storage {
  std::optional<T> result;

  template<std::convertible_to<T> U>
  void return_value(U&& value)
    noexcept(std::is_nothrow_constructible_v<T, decltype(std::forward<U>(value))>)
  {
    result = std::forward<U>(value);
  }
};

template<>
struct task_promise_storage<void> {
  void return_void() noexcept {}
};

} // namespace detail

template<typename T>
  requires std::movable<T> || std::is_void_v<T>
struct [[nodiscard]] task {
  struct promise_type : detail::task_promise_storage<T> {
    static std::suspend_never initial_suspend() noexcept { return {}; }
    static std::suspend_always final_suspend() noexcept { return {}; }
    [[noreturn]] static void unhandled_exception() { throw; }
    task get_return_object() noexcept { return this; }
  };

  [[nodiscard]] decltype(auto) get_result() const & noexcept requires std::movable<T> { return *promise_->result; }
  [[nodiscard]] decltype(auto) get_result() const && noexcept requires std::movable<T> { return *std::move(promise_->result); }
private:
  task(promise_type* p) : promise_(p) {}
  promise_ptr<promise_type> promise_;
};


// ********* EXAMPLE *********

#include <iostream>

task<int> foo()
{
  co_return 42;
}

task<int> bar()
{
  const int res = co_await foo();
  std::cout << "Result of foo: " << res << "\n";
  co_return res + 23;
}

task<void> baz()
{
  const auto res = co_await bar();
  std::cout << "Result of bar: " << res << "\n";
}

task<void> run()
{
  co_await baz();
}

int main()
{
  const auto task = run();
}

