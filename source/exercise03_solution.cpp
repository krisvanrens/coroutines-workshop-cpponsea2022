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
  void return_void() const noexcept {
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

  [[nodiscard]] decltype(auto) get_result() const& noexcept requires std::movable<T> {
    return *promise_->result_;
  }

  [[nodiscard]] decltype(auto) get_result() const&& noexcept requires std::movable<T> {
    return *std::move(promise_->result_);
  }

private:
  promise_ptr<promise_type> promise_;

  task(promise_type* promise)
    : promise_{promise} {
  }
};

template<>
struct [[nodiscard]] task<void> {
  struct promise_type : detail::task_promise_storage<void> {
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
