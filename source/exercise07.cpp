// - `task<T>` should
//   - always suspend on initialization
//   - provide `start()` member function
// - On suspend `task<T>` awaiter should store a continuation handle in the promise
//   and perform Symmetric Control Transfer
// - Provide a dedicated awaiter for the final suspend
//   - it should always suspend the coroutine
//   - upon suspension it should resume the continuation (if set)

#include <concepts>
#include <coroutine>
#include <memory>
#include <optional>


// ********* TYPE TRAITS *********

namespace detail {

template<typename T, template<typename...> typename Type>
inline constexpr bool is_specialization_of = false;

template<typename... Params, template<typename...> typename Type>
inline constexpr bool is_specialization_of<Type<Params...>, Type> = true;

} // namespace detail

template<typename T, template<typename...> typename Type>
concept specialization_of = detail::is_specialization_of<T, Type>;


// ********* CONCEPTS *********

namespace detail {

template<typename Ret, typename Handle>
Handle func_arg(Ret (*)(Handle));

template<typename Ret, typename T, typename Handle>
Handle func_arg(Ret (T::*)(Handle));

template<typename Ret, typename T, typename Handle>
Handle func_arg(Ret (T::*)(Handle) &);

template<typename Ret, typename T, typename Handle>
Handle func_arg(Ret (T::*)(Handle) &&);

template<typename Ret, typename T, typename Handle>
Handle func_arg(Ret (T::*)(Handle) const);

template<typename Ret, typename T, typename Handle>
Handle func_arg(Ret (T::*)(Handle) const &);

template<typename Ret, typename T, typename Handle>
Handle func_arg(Ret (T::*)(Handle) const &&);

template<typename T>
concept suspend_return_type =
  std::is_void_v<T> ||
  std::is_same_v<T, bool> ||
  specialization_of<T, std::coroutine_handle>;

} // namespace detail

template<typename T>
concept awaiter =
  requires(T&& t, decltype(detail::func_arg(&std::remove_reference_t<T>::await_suspend)) arg) {
    { std::forward<T>(t).await_ready() } -> std::convertible_to<bool>;
    { arg } -> std::convertible_to<std::coroutine_handle<>>; // TODO Why gcc does not inherit from `std::coroutine_handle<>`?
    { std::forward<T>(t).await_suspend(arg) } -> detail::suspend_return_type;
    std::forward<T>(t).await_resume();
  };

template<typename T, typename Value>
concept awaiter_of = awaiter<T> && requires(T&& t) {
  { std::forward<T>(t).await_resume() } -> std::same_as<Value>;
};


// ********* RAII *********

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

  awaiter_of<void> auto operator co_await() const noexcept requires std::is_void_v<T>
  {
    return awaiter(*promise_);
  }

  awaiter_of<const T&> auto operator co_await() const & noexcept
  {
    return awaiter(*promise_);
  }

  awaiter_of<T&&> auto operator co_await() const && noexcept
  {
    struct rvalue_awaiter : awaiter {
      T&& await_resume() const { return *std::move(this->promise.result); }
    };
    return rvalue_awaiter({*promise_});
  }

  [[nodiscard]] decltype(auto) get_result() const & noexcept requires std::movable<T> { return *promise_->result; }
  [[nodiscard]] decltype(auto) get_result() const && noexcept requires std::movable<T> { return *std::move(promise_->result); }
private:
  struct awaiter {
    promise_type& promise;
    bool await_ready() const noexcept
    {
      return std::coroutine_handle<promise_type>::from_promise(promise).done();
    }
    void await_suspend(std::coroutine_handle<>) const noexcept {}
    void await_resume() const requires std::is_void_v<T> {}
    decltype(auto) await_resume() const
    {
      return static_cast<const T&>(*promise.result);
    }
  };

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
  task.start();
}

