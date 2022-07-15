// - Implement `generator<T>`
//   - `next()` should resume the coroutine and return `false` if coroutine is suspended at its final
//     suspend point

#include <coroutine>
#include <exception>
#include <memory>


// ********* RAII *********

struct coro_deleter {
  template<typename Promise>
  void operator()(Promise* promise) const noexcept
  {
    auto handle = std::coroutine_handle<Promise>::from_promise(*promise);
    if(handle)
      handle.destroy();
  }
};
template<typename T>
using promise_ptr = std::unique_ptr<T, coro_deleter>;


// ********* GENERATOR *********

template<typename T>
class [[nodiscard]] generator {
  // ...
};


// ********* EXAMPLE *********

#include <iostream>

generator<int> simple()
{
  // co_await std::suspend_never{}; // should not compile
  co_yield 1;
  co_yield 2;
}

int main()
{
  auto g = simple();
  while(g.next())
    std::cout << g.value() << ' ';
  std::cout << '\n';
}

