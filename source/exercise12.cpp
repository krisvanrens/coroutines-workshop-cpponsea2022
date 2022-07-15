// - Implement `zip()` generator for two input ranges
//   - should return pairs of consecutive elements from each range
//   - in case of a different `size()`, generation stops at the end of the shorter range
// - Implement interface providing the best possible performance

#include <coroutine>
#include <exception>
#include <memory>
#include <utility>


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
public:
  using value_type = std::remove_reference_t<T>;
  using reference = std::conditional_t<std::is_reference_v<T>, T, const value_type&>;
  using pointer = const value_type*;

  struct promise_type {
    pointer value;

    static std::suspend_always initial_suspend() noexcept { return {}; }
    static std::suspend_always final_suspend() noexcept { return {}; }
    static void return_void() noexcept {}

    generator<T> get_return_object() noexcept { return this; }
    std::suspend_always yield_value(reference v) noexcept
    {
      value = std::addressof(v);
      return {};
    }
    void unhandled_exception() { throw; }
    
    // disallow co_await in generator coroutines
    void await_transform() = delete;
  };

  class iterator {
    std::coroutine_handle<promise_type> handle_;
    friend generator;
    explicit iterator(promise_type& promise) noexcept:
      handle_(std::coroutine_handle<promise_type>::from_promise(promise))
    {}
  public:
    using value_type = generator::value_type;
    using difference_type = std::ptrdiff_t;
    
    iterator(iterator&& other) noexcept: handle_(std::exchange(other.handle_, {})) {}
    iterator& operator=(iterator&& other) noexcept
    {
      handle_ = std::exchange(other.handle_, {});
      return *this;
    }

    iterator& operator++() { handle_.resume(); return *this; }
    void operator++(int) { ++*this; }

    [[nodiscard]] reference operator*() const noexcept { return *handle_.promise().value; }
    [[nodiscard]] pointer operator->() const noexcept { return std::addressof(operator*()); }

    [[nodiscard]] bool operator==(std::default_sentinel_t) const noexcept { return handle_.done(); }
  };

  [[nodiscard]] iterator begin()
  {
    auto handle = std::coroutine_handle<promise_type>::from_promise(*promise_);
    handle.resume();
    return iterator(*promise_);
  }
  [[nodiscard]] std::default_sentinel_t end() const noexcept { return std::default_sentinel; }

private:
  promise_ptr<promise_type> promise_;
  generator(promise_type* promise): promise_(promise) {}
};

template<typename T>
inline constexpr bool std::ranges::enable_view<generator<T>> = true;


// ********* EXAMPLE *********

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <ranges>
#include <utility>
#include <vector>

generator<std::uint64_t> iota(std::uint64_t start = 0)
{
  while(true)
    co_yield start++;
}

generator<std::uint64_t> fibonacci()
{
  std::uint64_t a = 0, b = 1;
  while(true)	{
    co_yield b;
    a = std::exchange(b, a + b);
  }
}

int main()
{
  auto g = iota();
  std::array<int, 1'000'000> r1;
  std::ranges::copy_n(std::ranges::begin(g), r1.size(), std::ranges::begin(r1));

  auto fib = fibonacci();
  std::vector<int> r2;
  r2.reserve(r1.size());
  std::ranges::copy_n(std::ranges::begin(fib), r1.size(), std::back_inserter(r2));

  auto z1 = zip(r1, r2);
  for(auto& [v1, v2] : z1 | std::views::take(20))
    std::cout << "[" << v1 << ", " << v2 << "] ";
  std::cout << '\n';

  auto z2 = zip(iota(), fibonacci());
  for(auto& [v1, v2] : z2 | std::views::take(20))
    std::cout << "[" << v1 << ", " << v2 << "] ";
  std::cout << '\n';
}

