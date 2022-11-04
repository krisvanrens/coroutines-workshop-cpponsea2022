// - Remove `value()` and `next()` from `generator<T>`
// - Make `generator<T>` an input range
//   - use `std::ptrdiff_t` for a `difference_type` in `iterator`

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

  [[nodiscard]] bool next()
  {
    auto handle = std::coroutine_handle<promise_type>::from_promise(*promise_);
    handle.resume();
    return !handle.done();
  }
  [[nodiscard]] const T& value() const { return *promise_->value; }
private:
  promise_ptr<promise_type> promise_;
  generator(promise_type* promise): promise_(promise) {}
};


// ********* EXAMPLE *********

#include <cstdint>
#include <iostream>
#include <ranges>
#include <stdexcept>
#include <utility>

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

generator<int> broken()
{
  co_yield 1;
  throw std::runtime_error("Some error\n");
  co_yield 2;
}

static_assert(std::input_iterator<generator<int>::iterator>);
static_assert(std::ranges::input_range<generator<int>>);
static_assert(std::ranges::viewable_range<generator<int>>);
static_assert(std::ranges::view<generator<int>>);

int main()
{
  try {
    for(auto i : iota() | std::views::take(10))
      std::cout << i << ' ';
    std::cout << '\n';

    auto gen = fibonacci();
    for(auto i : std::views::counted(gen.begin(), 10))
      std::cout << i << ' ';
    std::cout << '\n';

    for(auto v : broken())
      std::cout << v << ' ';
    std::cout << '\n';
  }
  catch(const std::exception& ex) {
    std::cout << "Unhandled exception: " << ex.what() << "\n";
  }
}

