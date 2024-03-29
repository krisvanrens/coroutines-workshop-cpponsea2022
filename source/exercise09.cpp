// - Implement `synchronized_task<T>`
//   - `promise_type` should inherit from `task_promise_storage`
//   - it should get a synchronization primitive through `start()` member function and release it on
//     the final suspend
//   - `get()` should return a value or rethrow an exception (if set)
// - `task<T>` does not need any custom interface anymore
//   - remove `start()` and `get()`

#include <concepts>
#include <coroutine>
#include <exception>
#include <memory>
#include <variant>
#include <thread>


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
  std::variant<std::monostate, std::exception_ptr, T> result;
public:
  template<std::convertible_to<T> U>
  void set_value(U&& value)
    noexcept(std::is_nothrow_constructible_v<T, decltype(std::forward<U>(value))>)
  {
    result.template emplace<T>(std::forward<U>(value));
  }

  [[nodiscard]] const T& get() const &
  {
    check_and_rethrow(this->result);
    return std::get<T>(this->result);
  }

  [[nodiscard]] T&& get() &&
  {
    check_and_rethrow(this->result);
    return std::get<T>(std::move(this->result));
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

  [[nodiscard]] T& get() const
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
  using value_type = T;
  void set_exception(std::exception_ptr ptr) noexcept
  {
    this->result = std::move(ptr);
  }
};


// ********* UTILITIES *********

namespace detail {

template<typename T>
decltype(auto) get_awaiter(T&& awaitable)
{
  if constexpr(requires { std::forward<T>(awaitable).operator co_await(); })
    return std::forward<T>(awaitable).operator co_await();
  else if constexpr(requires { operator co_await(std::forward<T>(awaitable)); })
    return operator co_await(std::forward<T>(awaitable));
  else
    return std::forward<T>(awaitable);
}

}

// ********* TYPE TRAITS *********

namespace detail {

template<typename T, template<typename...> typename Type>
inline constexpr bool is_specialization_of = false;

template<typename... Params, template<typename...> typename Type>
inline constexpr bool is_specialization_of<Type<Params...>, Type> = true;

} // namespace detail

template<typename T, template<typename...> typename Type>
concept specialization_of = detail::is_specialization_of<T, Type>;

// remove_rvalue_reference
template<typename T>
struct remove_rvalue_reference { using type = T; };

template<typename T>
struct remove_rvalue_reference<T&&> { using type = T; };

template<typename T>
using remove_rvalue_reference_t = typename remove_rvalue_reference<T>::type;


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

template<typename T>
concept awaitable = requires(T&& t) {
  { detail::get_awaiter(std::forward<T>(t)) } -> awaiter;
};

template<typename T, typename Value>
concept awaitable_of = awaitable<T> && requires(T&& t) {
  { detail::get_awaiter(std::forward<T>(t)) } -> awaiter_of<Value>;
};

template<typename T>
concept task_value_type = std::move_constructible<T> || std::is_reference_v<T> || std::is_void_v<T>;


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
  void unhandled_exception()
    noexcept(noexcept(this->set_exception(std::current_exception())))
  { 
    this->set_exception(std::current_exception());
  }
};

template<typename T>
struct task_promise_storage : task_promise_storage_base<T> {
  template<typename U>
  void return_value(U&& value)
    noexcept(noexcept(this->set_value(std::forward<U>(value))))
    requires requires { this->set_value(std::forward<U>(value)); }
  {
    this->set_value(std::forward<U>(value));
  }
};

template<>
struct task_promise_storage<void> : task_promise_storage_base<void> {
  void return_void() noexcept {}
};

} // namespace detail

template<task_value_type T = void, typename Allocator = void>
class [[nodiscard]] task {
public:
  struct promise_type : detail::task_promise_storage<T> {
    std::coroutine_handle<> continuation = std::noop_coroutine();
    static std::suspend_always initial_suspend() noexcept { return {}; }
    static awaiter_of<void> auto final_suspend() noexcept
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

  awaiter_of<T> auto operator co_await() const noexcept
  {
    return awaiter(*promise_);
  }

  awaiter_of<const T&> auto operator co_await() const & noexcept
    requires std::move_constructible<T>
  {
    return awaiter(*promise_);
  }

  awaiter_of<T&&> auto operator co_await() const && noexcept
    requires std::move_constructible<T>
  {
    struct rvalue_awaiter : awaiter {
      T&& await_resume() { return std::move(this->promise).get(); }
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

  promise_ptr<promise_type> promise_;
  task(promise_type* p) : promise_(p) {}
};


// ********* ASYNC *********

template<std::invocable Func>
class async {
public:
  using return_type = std::invoke_result_t<Func>;
  template<typename F>
    requires std::same_as<std::remove_cvref_t<F>, Func>
  explicit async(F&& func): func_{std::forward<F>(func)} {}

  decltype(auto) operator co_await() & = delete; // async should be co_awaited only once (on rvalue)
  decltype(auto) operator co_await() &&
  {
    struct awaiter {
      async& awaitable;
      bool await_ready() const noexcept { return false; }
      void await_suspend(std::coroutine_handle<> handle)
      {
        auto work = [&, handle]() {
          try {
            if constexpr(std::is_void_v<return_type>)
              awaitable.func_();
            else
              awaitable.result_.set_value(awaitable.func_());
          }
          catch(...) {
            awaitable.result_.set_exception(std::current_exception());
          }
          handle.resume();
        };
        std::jthread(work).detach();  // TODO: Fix that (replace with a thread pool)
      }
      decltype(auto) await_resume() { return std::move(awaitable.result_).get(); }
    };
    return awaiter{*this};
  }
private:
  Func func_;
  storage<return_type> result_;
};

template<typename F>
async(F) -> async<F>;


// ********* SYNCHRONIZED TASK *********

namespace detail {

template<typename Sync, task_value_type T>
  requires requires(Sync s) { s.notify_awaitable_completed(); }
class [[nodiscard]] synchronized_task {
  // Implement here....
};


// ********* MAKE SYNCHRONIZED TASK *********

template<awaitable A>
using awaiter_for_t = decltype(detail::get_awaiter(std::declval<A>()));

template<awaitable A>
using await_result_t = decltype(std::declval<awaiter_for_t<A>>().await_resume());

template<typename Sync, awaitable A>
  requires requires(Sync s) { s.notify_awaitable_completed(); }
synchronized_task<Sync, remove_rvalue_reference_t<await_result_t<A>>> make_synchronized_task(A&& awaitable)
{ 
  co_return co_await std::forward<A>(awaitable);
}

} // namespace detail


// ********* SYNC AWAIT *********

template<awaitable A>
[[nodiscard]] decltype(auto) sync_await(A&& awaitable)
{
  struct sync {
    std::binary_semaphore sem{0};
    void notify_awaitable_completed() { sem.release(); }
  };

  auto sync_task = detail::make_synchronized_task<sync>(std::forward<A>(awaitable));
  sync work_done;
  sync_task.start(work_done);
  work_done.sem.acquire();
  return sync_task.get();
}


// ********* EXAMPLE *********

#include <iostream>
#include <syncstream>

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
  co_return 42;
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
    if constexpr(std::is_void_v<T>)
      sync_await(t);
    else
      std::cout << "Result: " << sync_await(t) << '\n';
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

