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
  // ...
};

generator<int> simple() {
  // co_await std::suspend_never{}; // should not compile
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
