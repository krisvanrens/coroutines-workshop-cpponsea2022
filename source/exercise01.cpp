// - Compiler will guide you :-)
// - Return object of `std::suspend_never` type from suspend points

#include <exception>
#include <future>
#include <iostream>

std::future<int> foo()
{
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

int main()
{
  std::cout << foo().get() << "\n";
}

