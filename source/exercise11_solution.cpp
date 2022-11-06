#include <concepts>
#include <coroutine>
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
  using value_type = std::remove_reference_t<T>;
  using reference  = std::conditional_t<std::is_reference_v<T>, T, const value_type&>;
  using pointer    = const value_type*;

  struct promise_type {
    pointer value;

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

    std::suspend_always yield_value(reference v) noexcept {
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
    // Required for ranges (names are predetermined).
    using value_type      = generator::value_type;
    using difference_type = std::ptrdiff_t;

    iterator(iterator&& other) noexcept
      : handle_{std::exchange(other.handle_, {})} {
    }

    iterator& operator=(iterator&& other) noexcept {
      handle_ = std::exchange(other.handle_, {});
      return *this;
    }

    iterator& operator++() {
      handle_.resume();
      return *this;
    }

    void operator++(int) {
      ++*this;
    }

    [[nodiscard]] reference operator*() const noexcept {
      return *handle_.promise().value;
    }

    [[nodiscard]] pointer operator->() const noexcept {
      return std::addressof(operator*());
    }

    [[nodiscard]] bool operator==(std::default_sentinel_t) const noexcept {
      return handle_.done();
    }
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

template<typename T>
inline constexpr bool std::ranges::enable_view<generator<T>> = true;

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
