// - Remove `value()` and `next()` from `generator<T>`
// - Make `generator<T>` an input range
//   - use `std::ptrdiff_t` for a `difference_type` in `iterator`

#include <coroutine>
#include <concepts>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <ranges>
#include <stdexcept>
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
class [[nodiscard]] generator {
public:
  using value_t     = std::remove_reference_t<T>;
  using reference_t = std::conditional_t<std::is_reference_v<T>, T, const value_t&>;
  using pointer_t   = const value_t*;

  struct promise_type {
    pointer_t value;

    static std::suspend_always initial_suspend() noexcept {
      return {};
    }

    static std::suspend_always final_suspend() noexcept {
      return {};
    }

    static void return_void() noexcept {
    }

    generator<T> get_return_object() noexcept {
      return this;
    }

    std::suspend_always yield_value(reference_t v) noexcept {
      value = std::addressof(v);
      return {};
    }

    void unhandled_exception() {
      throw;
    }

    // Disallow co_await in generator coroutines.
    void await_transform() = delete;
  };

  class iterator {
    std::coroutine_handle<promise_type> handle_;

    friend generator;

    explicit iterator(promise_type& promise) noexcept
      : handle_{std::coroutine_handle<promise_type>::from_promise(promise)} {
    }

  public:
    // TODO: move operations
    // TODO: prefix operator++
    // TODO: postfix operator++
    // TODO: operator*
    // TODO: operator->
    // TODO: operator==
  };

  [[nodiscard]] iterator begin() {
    auto handle = std::coroutine_handle<promise_type>::from_promise(*promise_);
    handle.resume();
    return iterator{*promise_};
  }

  [[nodiscard]] std::default_sentinel_t end() const noexcept {
    return {};
  }

private:
  promise_ptr<promise_type> promise_;

  generator(promise_type* promise)
    : promise_(promise) {
  }
};

generator<std::uint64_t> iota(std::uint64_t start = 0L) {
  while (true) {
    co_yield start++;
  }
}

generator<std::uint64_t> fibonacci() {
  std::uint64_t a = 0, b = 1;
  while (true) {
    co_yield b;
    a = std::exchange(b, a + b);
  }
}

generator<int> broken() {
  co_yield 1;
  throw std::runtime_error("Some error\n");
  co_yield 2;
}

static_assert(std::input_iterator<generator<int>::iterator>);
static_assert(std::ranges::input_range<generator<int>>);
static_assert(std::ranges::viewable_range<generator<int>>);
static_assert(std::ranges::view<generator<int>>);

int main() {
  try {
    for (auto i : iota() | std::views::take(10)) {
      std::cout << i << ' ';
    }
    std::cout << '\n';

    auto gen = fibonacci();
    for (auto i : std::views::counted(gen.begin(), 10)) {
      std::cout << i << ' ';
    }
    std::cout << '\n';

    for (auto v : broken()) {
      std::cout << v << ' ';
    }
    std::cout << '\n';
  } catch (const std::exception& ex) {
    std::cout << "Unhandled exception: " << ex.what() << "\n";
  }
}
