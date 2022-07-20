// - Compiler will guide you :-)
// - Return object of `std::suspend_never` type from suspend points

#include <coroutine>
#include <exception>
#include <future>
#include <iostream>

template<typename... Args>
struct std::coroutine_traits<std::future<int>, Args...> {
  struct promise_type : std::promise<int> {
    promise_type() = default;

    std::future<int> get_return_object() noexcept {
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

    void return_value(int value) noexcept {
      this->set_value(value);
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

