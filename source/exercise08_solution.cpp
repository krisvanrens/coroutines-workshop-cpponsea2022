// - Implement `async<Func>` awaiter that
//   - keeps the result in our `storage<T>` class
//   - runs any invocable in a detached thread
//   - captures the exception from the invocable (if any) and rethrows on resume
//   - returns a result of the invocable (if any)

#include <chrono>
#include <concepts>
#include <coroutine>
#include <exception>
#include <iostream>
#include <memory>
#include <syncstream>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>

template<typename... Args>
void check_and_rethrow(const std::variant<Args...>& result) {
  if (std::holds_alternative<std::exception_ptr>(result)) {
    std::rethrow_exception(std::get<std::exception_ptr>(std::move(result)));
  }
}

template<typename T>
class storage_base {
protected:
  using value_type = std::remove_reference_t<T>; // In case of T&&.
                                                 //
  std::variant<std::monostate, std::exception_ptr, value_type> result_;

public:
  template<std::convertible_to<value_type> U>
  void set_value(U&& value) noexcept(std::is_nothrow_constructible_v<value_type, decltype(std::forward<U>(value))>) {
    result_.template emplace<value_type>(std::forward<U>(value));
  }

  [[nodiscard]] const value_type& get() const& {
    check_and_rethrow(this->result_);
    return std::get<value_type>(this->result_);
  }

  [[nodiscard]] value_type&& get() && {
    check_and_rethrow(this->result_);
    return std::get<value_type>(std::move(this->result_));
  }
};

template<typename T>
class storage_base<T&> {
protected:
  std::variant<std::monostate, std::exception_ptr, T*> result_;

public:
  void set_value(T& value) noexcept {
    result_ = std::addressof(value);
  }

  [[nodiscard]] const T& get() const {
    check_and_rethrow(this->result_);
    return *std::get<T*>(this->result_);
  }
};

template<>
class storage_base<void> {
protected:
  std::variant<std::monostate, std::exception_ptr> result_;

public:
  void get() const {
    check_and_rethrow(this->result_);
  }
};

template<typename T>
class storage : public storage_base<T> {
public:
  void set_exception(std::exception_ptr ptr) noexcept {
    this->result_ = std::move(ptr);
  }
};

namespace detail {

template<typename T, template<typename...> typename Type>
inline constexpr bool is_specialization_of = false;

template<typename... Params, template<typename...> typename Type>
inline constexpr bool is_specialization_of<Type<Params...>, Type> = true;

} // namespace detail

template<typename T, template<typename...> typename Type>
concept specialization_of = detail::is_specialization_of<T, Type>;

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
Handle func_arg(Ret (T::*)(Handle) const&);

template<typename Ret, typename T, typename Handle>
Handle func_arg(Ret (T::*)(Handle) const&&);

template<typename T>
concept suspend_return_type = std::is_void_v<T> || std::is_same_v<T, bool> || specialization_of<T, std::coroutine_handle>;

} // namespace detail

template<typename T>
concept awaiter = requires(T&& t, decltype(detail::func_arg(&std::remove_reference_t<T>::await_suspend)) arg) {
  { std::forward<T>(t).await_ready() } -> std::convertible_to<bool>;
  { arg } -> std::convertible_to<std::coroutine_handle<>>; // TODO: Why does GCC not inherit from `std::coroutine_handle<>`?
  { std::forward<T>(t).await_suspend(arg) } -> detail::suspend_return_type;
  std::forward<T>(t).await_resume();
};

template<typename T, typename Value>
concept awaiter_of = awaiter<T> && requires(T&& t) {
  { std::forward<T>(t).await_resume() } -> std::same_as<Value>;
};

struct coro_deleter {
  template<typename Promise>
  void operator()(Promise* promise) const noexcept {
    if (auto handle = std::coroutine_handle<Promise>::from_promise(*promise); handle) {
      handle.destroy();
    }
  }
};

template<typename T>
using promise_ptr = std::unique_ptr<T, coro_deleter>;

namespace detail {

template<typename T>
struct task_promise_storage_base : storage<T> {
  template<std::convertible_to<T> U>
  void return_value(U&& value) noexcept(std::is_nothrow_constructible_v<T, decltype(std::forward<U>(value))>) {
    this->set_value(std::forward<U>(value));
  }
};

template<>
struct task_promise_storage_base<void> : storage<void> {
  void return_void() noexcept {
  }
};

template<typename T>
struct task_promise_storage : task_promise_storage_base<T> {
  void unhandled_exception() noexcept(noexcept(this->set_exception(std::current_exception()))) {
    this->set_exception(std::current_exception());
  }
};

} // namespace detail

template<typename T>
requires std::movable<T> || std::is_void_v<T>
struct [[nodiscard]] task {
  struct promise_type : detail::task_promise_storage<T> {
    std::coroutine_handle<> continuation_ = std::noop_coroutine();

    static std::suspend_always initial_suspend() noexcept {
      return {};
    }

    static auto final_suspend() noexcept {
      struct final_awaiter : std::suspend_always {
        std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> handle) noexcept {
          return handle.promise().continuation_;
        }
      };

      return final_awaiter{};
    }

    task get_return_object() noexcept {
      return this;
    }
  };

  awaiter_of<void> auto operator co_await() const noexcept requires std::is_void_v<T> {
    return awaiter(*promise_);
  }

  awaiter_of<const T&> auto operator co_await() const& noexcept {
    return awaiter(*promise_);
  }

  awaiter_of<T&&> auto operator co_await() const&& noexcept {
    struct rvalue_awaiter : awaiter {
      T&& await_resume() const {
        return std::move(this->promise_).get();
      }
    };

    return rvalue_awaiter({*promise_});
  }

  void start() const {
    std::coroutine_handle<promise_type>::from_promise(*promise_).resume();
  }

  [[nodiscard]] decltype(auto) get() const& {
    return promise_->get();
  }

  [[nodiscard]] decltype(auto) get() const&& {
    return std::move(promise_)->get();
  }

private:
  struct awaiter {
    promise_type& promise_;

    bool await_ready() const noexcept {
      return std::coroutine_handle<promise_type>::from_promise(promise_).done();
    }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> continuation) const noexcept {
      promise_.continuation_ = continuation;
      return std::coroutine_handle<promise_type>::from_promise(promise_);
    }

    decltype(auto) await_resume() const {
      return promise_.get();
    }
  };

  task(promise_type* promise)
    : promise_{promise} {
  }

  promise_ptr<promise_type> promise_;
};

template<std::invocable Func>
class [[nodiscard]] async final {
public:
  template<typename Func_>
  requires std::same_as<std::remove_cvref_t<Func_>, Func>
  explicit async(Func_&& func)
    : func_{std::forward<Func_>(func)} {
  }

  // Only allow awaiting of rvalues to prevent multiple awaits.
  decltype(auto) operator co_await() & = delete;

  decltype(auto) operator co_await() && {
    struct awaiter {
      async& awaitable;

      bool await_ready() const noexcept {
        return false;
      }

      void await_suspend(std::coroutine_handle<> handle) const noexcept {
        const auto work = [&, handle] {
          try {
            if constexpr (std::is_void_v<result_type>) {
              awaitable.func_();
            } else {
              awaitable.result_.set_value(awaitable.func_());
            }
          } catch (...) {
            awaitable.result_.set_exception(std::current_exception());
          }

          handle.resume();
        };

        std::jthread{work}.detach();
      }

      decltype(auto) await_resume() const {
        return std::move(awaitable.result_).get();
      }
    };

    return awaiter{*this};
  }

private:
  using result_type = std::invoke_result_t<Func>;

  Func                 func_;
  storage<result_type> result_;
};

template<typename Func>
async(Func) -> async<Func>;

task<int> func1() {
  const int result = co_await async([] { return 42; });
  co_await async([&] { std::osyncstream(std::cout) << "Result: " << result << '\n'; });
  co_return result + 23;
}

task<void> func2() {
  const auto result = co_await func1();
  std::osyncstream(std::cout) << "Result of func1: " << result << '\n';
}

task<int> func3() {
  const int result = co_await async([] {
    std::osyncstream(std::cout) << "About to throw an exception\n";
    throw std::runtime_error("Some error");
    std::osyncstream(std::cout) << "This will never be printed\n";
    return 42;
  });

  std::osyncstream(std::cout) << "I will never tell you that the result is: " << result << '\n';

  co_return result;
}

task<void> example() {
  co_await func2();
  co_await func3();
}

template<typename T>
void test(task<T> task) {
  try {
    task.start();

    using namespace std::chrono_literals;
    std::this_thread::sleep_for(200ms);

    if constexpr (std::is_void_v<T>) {
      task.get();
    } else {
      std::cout << "Result: " << task.get() << '\n';
    }
  } catch (const std::exception& ex) {
    std::cout << "Exception caught: " << ex.what() << "\n";
  }
}

int main() {
  try {
    test(example());
    test(func3());
  } catch (const std::exception& ex) {
    std::cout << "Unhandled exception: " << ex.what() << "\n";
  }
}
