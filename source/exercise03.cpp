// Extend our task to support no return type as well

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

template<typename T>
struct [[nodiscard]] task {
  struct promise_type {
    std::optional<T> result;

    static std::suspend_never initial_suspend() noexcept { return {}; }
    static std::suspend_always final_suspend() noexcept { return {}; }
    [[noreturn]] static void unhandled_exception() { throw; }
    task get_return_object() noexcept { return this; }

    template<std::convertible_to<T> U>
    void return_value(U&& value) noexcept(std::is_nothrow_constructible_v<T, decltype(std::forward<U>(value))>)
    {
      result = std::forward<U>(value);
    }
  };

  [[nodiscard]] const T& get_result() const & noexcept { return *promise_->result; }
  [[nodiscard]] T&& get_result() const && noexcept { return *std::move(promise_->result); }
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

task<void> coro()
{
  std::cout << foo().get_result() << "\n";
  co_return;
}

int main()
{
  auto c = coro();
}

