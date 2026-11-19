#include <cstring>

#include "testing.hpp"

int main(int argc, char** argv) {
  const char* filter = argc > 1 ? argv[1] : nullptr;
  int ran = 0;
  for (const auto& t : testing::registry()) {
    if (filter && !std::strstr(t.name, filter)) continue;
    const int before = testing::failures();
    t.fn();
    ++ran;
    std::printf("[%s] %s\n", testing::failures() == before ? " ok " : "FAIL", t.name);
  }
  std::printf("\n%d tests, %d checks, %d failed\n", ran, testing::checks(), testing::failures());
  return testing::failures() ? 1 : 0;
}
