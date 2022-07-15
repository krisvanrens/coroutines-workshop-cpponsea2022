// - Implement `async<Func>` awaiter that
//   - keeps the result in our `storage<T>` class
//   - runs any invocable in a detached thread
//   - captures the exception from the invocable (if any) and rethrows on resume
//   - returns a result of the invocable (if any)

#include <concepts>
#include <coroutine>
#include <exception>
#include <memory>
#include <variant>


// ********* STORAGE *********

template<typename... Args>
void check_and_rethrow(const std::variant<Args...>& result)
{
  if(std::holds_alternative<std::exception_ptr>(result))
    std::rethrow_exception(std::get<std::exception_ptr>(std::move(result)));
}

template<typename T>
class storage_base {
protected:
  using value_type = std::remove_reference_t<T>;  // in case of T&&
  std::variant<std::monostate, std::exception_ptr, value_type> result;
public:
  template<std::convertible_to<value_type> U>
  void set_value(U&& value)
    noexcept(std::is_nothrow_constructible_v<value_type, decltype(std::forward<U>(value))>)
  {
    result.template emplace<value_type>(std::forward<U>(value));
  }

  [[nodiscard]] const value_type& get() const &
  {
    check_and_rethrow(this->result);
    return std::get<value_type>(this->result);
  }

  [[nodiscard]] value_type&& get() &&
  {
    check_and_rethrow(this->result);
    return std::get<value_type>(std::move(this->result));
  }
};

template<typename T>
class storage_base<T&> {
protected:
  std::variant<std::monostate, std::exception_ptr, T*> result;
public:
  void set_value(T& value) noexcept
  {
    result = std::addressof(value);
  }

  [[nodiscard]] const T& get() const
  {
    check_and_rethrow(this->result);
    return *std::get<T*>(this->result);
  }
};

template<>
class storage_base<void> {
protected:
  std::variant<std::monostate, std::exception_ptr> result;
public:
  void get() const
  {
    check_and_rethrow(this->result);
  }
};

template<typename T>
class storage : public storage_base<T> {
public:
  void set_exception(std::exception_ptr ptr) noexcept
  {
    this->result = std::move(ptr);
  }
};


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
struct task_promise_storage_base : storage<T> {
  template<std::convertible_to<T> U>
  void return_value(U&& value)
    noexcept(std::is_nothrow_constructible_v<T, decltype(std::forward<U>(value))>)
  {
    this->set_value(std::forward<U>(value));
  }
};

template<>
struct task_promise_storage_base<void> : storage<void> {
  void return_void() noexcept {}
};

template<typename T>
struct task_promise_storage : task_promise_storage_base<T> {
  void unhandled_exception()
    noexcept(noexcept(this->set_exception(std::current_exception())))
  {
    this->set_exception(std::current_exception());
  }
};

} // namespace detail

template<typename T>
  requires std::movable<T> || std::is_void_v<T>
struct [[nodiscard]] task {
  struct promise_type : detail::task_promise_storage<T> {
    std::coroutine_handle<> continuation = std::noop_coroutine();
    static std::suspend_always initial_suspend() noexcept { return {}; }
    static auto final_suspend() noexcept
    { 
      struct final_awaiter : std::suspend_always {
        std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> h) noexcept
        {
          return h.promise().continuation;
        }
      };
      return final_awaiter{};
    }
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
      T&& await_resume() const { return std::move(this->promise).get(); }
    };
    return rvalue_awaiter({*promise_});
  }

  void start() const
  {
    std::coroutine_handle<promise_type>::from_promise(*promise_).resume();
  }

  [[nodiscard]] decltype(auto) get() const & { return promise_->get(); }
  [[nodiscard]] decltype(auto) get() const && { return std::move(promise_)->get(); }
private:
  struct awaiter {
    promise_type& promise;
    bool await_ready() const noexcept
    {
      return std::coroutine_handle<promise_type>::from_promise(promise).done();
    }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> cont) const noexcept
    {
      promise.continuation = cont;
      return std::coroutine_handle<promise_type>::from_promise(promise);
    }
    decltype(auto) await_resume() const { return promise.get(); }
  };

  task(promise_type* p) : promise_(p) {}
  promise_ptr<promise_type> promise_;
};


// ********* EXAMPLE *********

#include <chrono>
#include <iostream>
#include <syncstream>
#include <thread>

task<int> foo()
{
  const int res = co_await async([]{ return 42; });
  co_await async([&]{ std::osyncstream(std::cout) << "Result: " << res << '\n'; });
  co_return res + 23;
}

task<void> bar()
{
  const auto res = co_await foo();
  std::osyncstream(std::cout) << "Result of foo: " << res << '\n';
}

task<int> boo()
{
  const int res = co_await async([]{
    std::osyncstream(std::cout) << "About to throw an exception\n";
    throw std::runtime_error("Some error");
    std::osyncstream(std::cout) << "This will never be printed\n";
    return 42;
  });
  std::osyncstream(std::cout) << "I will never tell you that the result is: " << res << '\n';
  co_return res;
}

task<void> example()
{
  co_await bar();
  co_await boo();
}

template<typename T>
void test(task<T> t)
{
  try {
    t.start();
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(200ms);
    if constexpr(std::is_void_v<T>)
      t.get();
    else
      std::cout << "Result: " << t.get() << '\n';
  }
  catch(const std::exception& ex) {
    std::cout << "Exception caught: " << ex.what() << "\n";
  }
}

int main()
{
  try {
    test(example());
    test(boo());
  }
  catch(const std::exception& ex) {
    std::cout << "Unhandled exception: " << ex.what() << "\n";
  }
}

