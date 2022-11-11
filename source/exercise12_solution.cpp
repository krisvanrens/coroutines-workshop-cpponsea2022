#include <algorithm>
#include <array>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <iterator>
#include <memory>
#include <ranges>
#include <tuple>
#include <utility>
#include <vector>

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

    // disallow co_await in generator coroutines
    void await_transform() = delete;
  };

  class iterator {
    std::coroutine_handle<promise_type> handle_;

    friend generator;

    explicit iterator(promise_type& promise) noexcept
      : handle_(std::coroutine_handle<promise_type>::from_promise(promise)) {
    }

  public:
    using value_type      = generator::value_type;
    using difference_type = std::ptrdiff_t;

    iterator(iterator&& other) noexcept
      : handle_(std::exchange(other.handle_, {})) {
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
    return iterator(*promise_);
  }

  [[nodiscard]] std::default_sentinel_t end() const noexcept {
    return std::default_sentinel;
  }

private:
  promise_ptr<promise_type> promise_;
  generator(promise_type* promise)
    : promise_(promise) {
  }
};

template<typename T>
inline constexpr bool std::ranges::enable_view<generator<T>> = true;

generator<std::uint64_t> iota(std::uint64_t start = 0) {
  while (true)
    co_yield start++;
}

generator<std::uint64_t> fibonacci() {
  std::uint64_t a = 0, b = 1;
  while (true) {
    co_yield b;
    a = std::exchange(b, a + b);
  }
}

// The "universal approach" taking an arbitrary number of input ranges.
//
// This approach did not work as I couldn't get operator| to work, also, it only accepted
//  rvalues to be taken as all the tuples are created in place.
#if 0
template<typename... Ts>
class zip_iterator {
  using indices = std::index_sequence_for<Ts...>;

public:
  using iterator_category = std::forward_iterator_tag;
  using difference_type   = std::ptrdiff_t;
  using iterator_type     = std::tuple<Ts...>;
  using value_type        = std::tuple<typename std::iterator_traits<Ts>::value_type...>;
  using pointer           = value_type*;
  using reference         = value_type&;

  constexpr zip_iterator(std::tuple<Ts...>&& begin_iterator_tuple, std::tuple<Ts...>&& end_iterator_tuple)
    : iterator_tuple_{std::forward<std::tuple<Ts...>>(begin_iterator_tuple)},
      end_iterator_tuple_{std::forward<std::tuple<Ts...>>(end_iterator_tuple)},
      value_tuple_{make(indices())} {
  }

  constexpr zip_iterator(std::tuple<Ts...>&& end_iterator_tuple)
    : iterator_tuple_{std::forward<std::tuple<Ts...>>(end_iterator_tuple)} {
  }

  [[nodiscard]] constexpr auto operator*() noexcept {
    return value_tuple_;
  }

  [[nodiscard]] constexpr auto operator->() noexcept {
    return &value_tuple_;
  }

  constexpr zip_iterator& operator++() noexcept {
    next(indices());
    value_tuple_ = make(indices());
    return *this;
  }

  constexpr zip_iterator operator++(int) noexcept {
    auto tmp{*this};
    ++(*this);
    return tmp;
  }

  [[nodiscard]] constexpr bool operator==(const zip_iterator& other) const noexcept {
    return iterator_tuple_ == other.iterator_tuple_;
  }

  [[nodiscard]] constexpr bool operator!=(const zip_iterator& other) const noexcept {
    return iterator_tuple_ != other.iterator_tuple_;
  }

private:
  iterator_type iterator_tuple_;
  iterator_type end_iterator_tuple_;
  value_type value_tuple_;

  template<std::size_t... Indexes>
  [[nodiscard]] constexpr auto make(std::index_sequence<Indexes...>) noexcept {
    return std::make_tuple(
      (std::get<Indexes>(iterator_tuple_) == std::get<Indexes>(end_iterator_tuple_) ?
       typename std::iterator_traits<decltype(std::prev(std::get<Indexes>(iterator_tuple_)))>::value_type{} :
       *std::get<Indexes>(iterator_tuple_))...
    );
  }

  template<std::size_t... Indexes>
  constexpr void next(std::index_sequence<Indexes...>) noexcept {
    ((
      std::get<Indexes>(iterator_tuple_) == std::get<Indexes>(end_iterator_tuple_) ?
      std::get<Indexes>(iterator_tuple_) : ++std::get<Indexes>(iterator_tuple_)
    ), ...);
  }
};

template<typename... Ts>
zip_iterator<Ts...> make_zip_iterator(std::tuple<Ts...>&& end_tuple) {
  return zip_iterator<Ts...>{std::forward<std::tuple<Ts...>>(end_tuple)};
}

template<typename... Ts>
zip_iterator<Ts...> make_zip_iterator(std::tuple<Ts...>&& begin_tuple,
                                      std::tuple<Ts...>&& end_tuple) {
  return zip_iterator<Ts...>{std::forward<std::tuple<Ts...>>(begin_tuple),
                             std::forward<std::tuple<Ts...>>(end_tuple)};
}

template<std::ranges::input_range... Ranges>
class zip_range final {
  using indices = std::index_sequence_for<Ranges...>;

public:
  explicit constexpr zip_range(Ranges&&... ranges)
    : ranges_{std::forward<Ranges>(ranges)...},
      size_{range_size(std::get<0>(ranges_))} { // TODO: Validate equal range lengths.
  }

  [[nodiscard]] constexpr auto begin() const noexcept {
    return make_zip_iterator(begin_tuple(indices()), end_tuple(indices()));
  }

  [[nodiscard]] constexpr auto end() const noexcept {
    return make_zip_iterator(end_tuple(indices()));
  }

  [[nodiscard]] constexpr std::size_t size() const noexcept {
    return size_;
  }

  [[nodiscard]] constexpr bool empty() const noexcept {
    return (size_ == 0L);
  }

private:
  std::tuple<Ranges...> ranges_;
  std::size_t size_;

  template<typename T>
  [[nodiscard]] constexpr std::size_t range_size(T&& r) const noexcept {
    return static_cast<std::size_t>(std::distance(std::begin(r), std::end(r)));
  }

  template<std::size_t... Indexes>
  [[nodiscard]] auto begin_tuple(std::index_sequence<Indexes...>) const noexcept {
    return std::make_tuple(std::begin(std::get<Indexes>(ranges_))...);
  }

  template<std::size_t... Indexes>
  [[nodiscard]] auto end_tuple(std::index_sequence<Indexes...>) const noexcept {
    return std::make_tuple(std::end(std::get<Indexes>(ranges_))...);
  }
};

template<std::ranges::input_range... Ranges>
[[nodiscard]] constexpr zip_range<Ranges...> zip(Ranges... ranges) {
  return zip_range<Ranges...>{std::forward<Ranges>(ranges)...};
}
#endif

// The "simple approach" (WIP)..
template<std::ranges::input_range Range1, std::ranges::input_range Range2>
class zip final : public std::ranges::view_interface<zip<Range1, Range2>> {
public:
  constexpr zip(Range1 rng1, Range2 rng2)
    : rng1_begin_{std::begin(rng1)}
    , rng1_end_{std::end(rng1)}
    , rng2_begin_{std::begin(rng2)}
    , rng2_end_{std::end(rng2)} {
  }

  [[nodiscard]] constexpr auto begin() const noexcept {
    return std::pair{rng1_begin_, rng2_begin_};
  }

  [[nodiscard]] constexpr auto end() const noexcept {
    return std::pair{rng1_end_, rng2_end_};
  }

private:
  typename Range1::const_iterator rng1_begin_, rng1_end_;
  typename Range2::const_iterator rng2_begin_, rng2_end_;
};

int main() {
  auto i   = iota();
  auto fib = fibonacci();

  std::array<int, 1'000'000> r1;
  std::ranges::copy_n(std::ranges::begin(i), r1.size(), std::ranges::begin(r1));

  std::vector<int> r2;
  r2.reserve(r1.size());
  std::ranges::copy_n(std::ranges::begin(fib), r1.size(), std::back_inserter(r2));

  auto z1 = zip(r1, r2);
  for (auto& [v1, v2] : z1 | std::views::take(20)) {
    std::cout << "[" << v1 << ", " << v2 << "] ";
  }
  std::cout << '\n';

  auto z2 = zip(iota(), fibonacci());
  for (auto& [v1, v2] : z2 | std::views::take(20)) {
    std::cout << "[" << v1 << ", " << v2 << "] ";
  }
  std::cout << '\n';
}
