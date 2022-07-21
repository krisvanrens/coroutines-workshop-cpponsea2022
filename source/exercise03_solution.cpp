#include <concepts>
#include <coroutine>
#include <iostream>
#include <memory>
#include <optional>
#include <utility>

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

template<typename T>
struct [[nodiscard]] task {
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

  [[nodiscard]] const T& get_result() const& noexcept {
    return *promise_->value_;
  }

  [[nodiscard]] T&& get_result() const&& noexcept {
    return *std::move(promise_->value_);
  }

private:
  promise_ptr<promise_type> promise_;

  task(promise_type* p)
    : promise_(p) {
  }
};

template<>
struct [[nodiscard]] task<void> {
  struct promise_type {
    task get_return_object() noexcept {
      return this;
    }

    static std::suspend_never initial_suspend() noexcept {
      return {};
    }

    static std::suspend_always final_suspend() noexcept {
      return {};
    }

    void return_void() {
    }

    [[noreturn]] static void unhandled_exception() {
      throw;
    }
  };

  void get_result() const noexcept {
  }

private:
  promise_ptr<promise_type> promise_;

  task(promise_type* p)
    : promise_(p) {
  }
};

task<int> func() {
  co_return 42;
}

task<void> coro() {
  std::cout << func().get_result() << "\n";
  co_return;
}

int main() {
  auto c1 = func();
  auto c2 = coro();
}
