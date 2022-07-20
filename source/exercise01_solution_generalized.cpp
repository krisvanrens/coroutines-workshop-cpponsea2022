#include <concepts>
#include <coroutine>
#include <exception>
#include <future>
#include <iostream>
#include <utility>

template<typename T, typename... Args>
struct std::coroutine_traits<std::future<T>, Args...> {
  struct promise_type : std::promise<T> {
    promise_type() = default;

    std::future<T> get_return_object() noexcept {
      return this->get_future();
    }

    void unhandled_exception() noexcept {
      this->set_exception(std::current_exception());
    }

    static std::suspend_never initial_suspend() noexcept {
      return {};
    }

    static std::suspend_never final_suspend() noexcept {
      return {};
    }

    template<std::convertible_to<T> U>
    void return_value(U&& value) noexcept(std::is_nothrow_constructible_v<T, decltype(std::forward<U>(value))>) {
      this->set_value(std::forward<U>(value));
    }
  };
};

template<typename... Args>
struct std::coroutine_traits<std::future<void>, Args...> {
  struct promise_type : std::promise<void> {
    promise_type() = default;

    std::future<void> get_return_object() noexcept {
      return this->get_future();
    }

    void unhandled_exception() noexcept {
      this->set_exception(std::current_exception());
    }

    static std::suspend_never initial_suspend() noexcept {
      return {};
    }

    static std::suspend_never final_suspend() noexcept {
      return {};
    }

    void return_void() noexcept {
      this->set_value();
    }
  };
};

struct Blah {};

std::future<int> func1() {
  co_return 42;
}

std::future<float> func2() {
  co_return 3.141592f;
}

std::future<Blah> func3() {
  co_return Blah{};
}

std::future<void> func4() {
  co_return;
}

int main() {
  std::cout << func1().get() << "\n";
  std::cout << func2().get() << "\n";
  func3().get();
  func4().get();
}
