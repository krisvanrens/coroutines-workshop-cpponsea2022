#include <chrono>
#include <concepts>
#include <coroutine>
#include <future>
#include <iostream>
#include <memory>
#include <optional>
#include <thread>
#include <utility>

template<typename T, template<typename...> typename Type>
inline constexpr bool is_specialization_of = false;

template<typename... Params, template<typename...> typename Type>
inline constexpr bool is_specialization_of<Type<Params...>, Type> = true;

template<typename T, template<typename...> typename Type>
concept specialization_of = is_specialization_of<T, Type>;

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

template<typename R, typename... Args>
struct std::coroutine_traits<std::future<R>, Args...> {
  struct promise_type {
    std::promise<R> p;

    auto get_return_object() {
      return p.get_future();
    }

    auto initial_suspend() {
      return std::suspend_never{};
    }

    auto final_suspend() noexcept {
      return std::suspend_never{};
    }

    void return_void() {
      p.set_value();
    }

    void unhandled_exception() {
      p.set_exception(std::current_exception());
    }
  };
};

template<specialization_of<std::chrono::duration> D>
constexpr auto operator co_await(const D& duration) {
  struct awaiter {
    const D& duration;

    bool await_ready() const noexcept {
      return duration <= D::zero();
    }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> handle) const {
      std::this_thread::sleep_for(duration);
      return handle;
    }

    static void await_resume() noexcept {
    }
  };

  return awaiter{duration};
}

using namespace std::chrono_literals;

task<void> func1() {
  co_await std::suspend_never{};

  std::cout << "Going to sleep..\n";
  co_await 1s;
  std::cout << "..done!\n";

  std::cout << "Going to sleep again..\n";
  auto dur = 1s;
  co_await dur;
  std::cout << "..done!\n";
}

std::future<void> func2() {
  co_await std::suspend_never{};

  std::cout << "You shall not sleep!\n";
  co_await 1s; // Should not compile
}

// - Refactor the previous exercise to use our `std::chrono::duration`-specific awaitable
//   implementation only while awaiting in `task<T>`-based coroutine

int main() {
  auto task = func1();
  auto fut  = func2();
}
