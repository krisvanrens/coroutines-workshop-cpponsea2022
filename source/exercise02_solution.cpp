#include <concepts>
#include <coroutine>
#include <exception>
#include <future>
#include <iostream>
#include <memory>
#include <optional>
#include <utility>

template<typename T, typename... Args>
struct std::coroutine_traits<std::future<T>, Args...> {
  struct promise_type {
    std::promise<T> promise;

    std::future<T> get_return_object() noexcept {
      return promise.get_future();
    }

    std::suspend_never initial_suspend() noexcept {
      return {};
    }

    std::suspend_never final_suspend() noexcept {
      return {};
    }

    void return_value(T value) {
      promise.set_value(value);
    }

    void unhandled_exception() {
      promise.set_exception(std::current_exception());
    }
  };
};

std::future<int> func1() {
  co_return 42;
}

struct coro_deleter {
  template<typename Promise>
  void operator()(Promise* promise) {
    if (auto handle = std::coroutine_handle<Promise>::from_promise(*promise); handle) {
      handle.destroy();
    }
  }
};

template<typename T>
using promise_ptr = std::unique_ptr<T, coro_deleter>;

template<std::movable T>
class [[nodiscard]] task {
public:
  struct promise_type {
    std::optional<T> value_;

    task get_return_object() noexcept {
      return this;
    }

    static std::suspend_never initial_suspend() noexcept {
      return {};
    }

    static std::suspend_always final_suspend() noexcept {
      return {};
    }

    template<std::convertible_to<T> U>
    void return_value(U&& value) noexcept(std::is_nothrow_constructible_v<T, decltype(std::forward<U>(value))>) {
      value_ = std::forward<U>(value);
    }

    [[noreturn]] static void unhandled_exception() {
      throw;
    }
  };

  task(const task&)           = delete;
  task operator=(const task&) = delete;

  [[nodiscard]] const T& get_result() const& noexcept {
    return *promise_->value_;
  }

  [[nodiscard]] T&& get_result() const&& noexcept {
    return *std::move(promise_->value_);
  }

private:
  promise_ptr<promise_type> promise_;

  task(promise_type* promise)
    : promise_{promise} {
  }
};

template<typename T, typename... Args>
struct std::coroutine_traits<task<T>, Args...> {
  using promise_type = task<T>::promise_type;
};

task<int> func2() {
  co_return 42;
}

int main() {
  std::cout << func1().get() << "\n";
  std::cout << func2().get_result() << "\n";
}
