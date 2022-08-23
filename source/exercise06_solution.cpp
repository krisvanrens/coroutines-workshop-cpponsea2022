#include <concepts>
#include <coroutine>
#include <iostream>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

struct coro_deleter {
  template<typename Promise>
  void operator()(Promise* promise) const noexcept {
    auto handle = std::coroutine_handle<Promise>::from_promise(*promise);
    if (handle)
      handle.destroy();
  }
};

template<typename T>
using promise_ptr = std::unique_ptr<T, coro_deleter>;

namespace detail {

template<typename T>
struct task_promise_storage {
  std::optional<T> result_;

  template<std::convertible_to<T> U>
  void return_value(U&& value) noexcept(std::is_nothrow_constructible_v<T, decltype(std::forward<U>(value))>) {
    result_ = std::forward<U>(value);
  }
};

template<>
struct task_promise_storage<void> {
  void return_void() noexcept {
  }
};

} // namespace detail

template<typename T>
requires std::movable<T> || std::is_void_v<T>
struct [[nodiscard]] task {
  struct promise_type : detail::task_promise_storage<T> {
    task get_return_object() noexcept {
      return this;
    }

    static std::suspend_never initial_suspend() noexcept {
      return {};
    }

    static std::suspend_always final_suspend() noexcept {
      return {};
    }

    [[noreturn]] static void unhandled_exception() {
      throw;
    }
  };

  constexpr auto operator co_await() {
    return awaiter{*promise_};
  }

  [[nodiscard]] decltype(auto) get_result() const& noexcept requires std::movable<T> {
    return *promise_->result_;
  }

  [[nodiscard]] decltype(auto) get_result() const&& noexcept requires std::movable<T> {
    return *std::move(promise_->result_);
  }

private:
  struct awaiter {
    promise_type& promise_;

    bool await_ready() const noexcept {
      return std::coroutine_handle<promise_type>::from_promise(promise_).done();
    }

    void await_suspend(std::coroutine_handle<>) const noexcept {
    }

    void await_resume() const requires std::is_void_v<T> {
    }

    decltype(auto) await_resume() const {
      return static_cast<const T&>(*promise_.result_);
    }
  };

  task(promise_type* promise)
    : promise_{promise} {
  }

  promise_ptr<promise_type> promise_;
};

task<int> func1() {
  co_return 42;
}

task<int> func2() {
  const int result = co_await func1();
  std::cout << "Result of func1: " << result << "\n";
  co_return result + 23;
}

task<void> func3() {
  const auto result = co_await func2();
  std::cout << "Result of func2: " << result << "\n";
}

task<void> run() {
  co_await func3();
}

int main() {
  const auto task = run();
}
