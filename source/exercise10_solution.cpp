#include <coroutine>
#include <exception>
#include <iostream>
#include <memory>

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
class [[nodiscard]] generator {
public:
  using value_t     = std::remove_reference_t<T>;
  using reference_t = std::conditional_t<std::is_reference_v<T>, T, const value_t&>;
  using pointer_t   = const value_t*;

  struct promise_type {
    pointer_t value_{};

    static std::suspend_always initial_suspend() noexcept {
      return {};
    }

    static std::suspend_always final_suspend() noexcept {
      return {};
    }

    std::suspend_always yield_value(const T& value) noexcept {
      value_ = std::addressof(value);
      return {};
    }

    void await_transform() = delete;

    void unhandled_exception() {
      throw;
    }

    generator<T> get_return_object() noexcept {
      return this;
    }
  };

  [[nodiscard]] bool next() const noexcept {
    auto handle = std::coroutine_handle<promise_type>::from_promise(*promise_);
    handle.resume();
    return !handle.done();
  }

  [[nodiscard]] T value() const noexcept {
    return *(promise_->value_);
  }

private:
  promise_ptr<promise_type> promise_;

  generator(promise_type* promise)
    : promise_(promise) {
  }
};

generator<int> simple() {
  // co_await std::suspend_never{}; // Generator is not awaitable.

  co_yield 1;
  co_yield 2;
}

int main() {
  auto g = simple();
  while (g.next()) {
    std::cout << g.value() << ' ';
  }
  std::cout << '\n';
}
