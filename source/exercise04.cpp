// Implement the `sleep_for` awaiter type that will suspend a current
// thread for a specified duration

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

#include <chrono>
#include <iostream>

template<typename T, template<typename...> typename Type>
inline constexpr bool is_specialization_of = false;

template<typename... Params, template<typename...> typename Type>
inline constexpr bool is_specialization_of<Type<Params...>, Type> = true;

template<typename T, template<typename...> typename Type>
concept specialization_of = is_specialization_of<T, Type>;

template<specialization_of<std::chrono::duration> D>
struct sleep_for {
  D duration;
};

task<void> foo()
{
  using namespace std::chrono_literals;

  std::cout << "about to sleep\n";
  co_await sleep_for(1s);
  std::cout << "about to return\n";
}

int main()
{
  auto task = foo();
}

