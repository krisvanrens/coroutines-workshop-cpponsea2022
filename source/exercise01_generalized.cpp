#include <coroutine>
#include <exception>
#include <future>
#include <iostream>

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

    std::suspend_never initial_suspend() const noexcept {
      return {};
    }

    std::suspend_never final_suspend() const noexcept {
      return {};
    }

    void return_value(const T &value) noexcept(std::is_nothrow_copy_constructible_v<T>) {
      this->set_value(value);
    }

    void return_value(T &&value) noexcept(std::is_nothrow_copy_constructible_v<T>) {
      this->set_value(std::move(value));
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

    std::suspend_never initial_suspend() const noexcept {
      return {};
    }

    std::suspend_never final_suspend() const noexcept {
      return {};
    }

    void return_void() noexcept {
      this->set_value();
    }
  };
};

std::future<int> foo() {
  // std::promise<int> p;
  // auto f = p.get_future();
  // try {
  //   int i = 42;
  //   p.set_value(i);
  // }
  // catch(...) {
  //   p.set_exception(std::current_exception());
  // }
  // return f;

  co_return 42;
}

int main() {
  std::cout << foo().get() << "\n";
}

