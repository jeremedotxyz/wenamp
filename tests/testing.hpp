#pragma once
// A deliberately tiny, dependency-free test harness.
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace testing {

struct Case {
  const char* name;
  void (*fn)();
};

inline std::vector<Case>& registry() {
  static std::vector<Case> r;
  return r;
}
inline int& failures() {
  static int n = 0;
  return n;
}
inline int& checks() {
  static int n = 0;
  return n;
}

struct Registrar {
  Registrar(const char* n, void (*f)()) { registry().push_back({n, f}); }
};

// Per-run scratch directory for generated audio / playlists.
inline const std::filesystem::path& scratch() {
  static const std::filesystem::path p = [] {
    auto d = std::filesystem::temp_directory_path() / "wenamp-tests";
    std::filesystem::remove_all(d);
    std::filesystem::create_directories(d);
    return d;
  }();
  return p;
}

}  // namespace testing

#define TEST(name)                                          \
  static void test_##name();                                \
  static testing::Registrar reg_##name(#name, test_##name); \
  static void test_##name()

#define CHECK(cond)                                                                \
  do {                                                                             \
    ++testing::checks();                                                           \
    if (!(cond)) {                                                                 \
      ++testing::failures();                                                       \
      std::printf("    FAILED %s:%d  %s\n", __FILE__, __LINE__, #cond);            \
    }                                                                              \
  } while (0)

#define CHECK_NEAR(a, b, eps)                                                                      \
  do {                                                                                             \
    ++testing::checks();                                                                           \
    const double va_ = static_cast<double>(a), vb_ = static_cast<double>(b);                       \
    if (!(std::fabs(va_ - vb_) <= (eps))) {                                                        \
      ++testing::failures();                                                                       \
      std::printf("    FAILED %s:%d  %s ~= %s  (%g vs %g, eps %g)\n", __FILE__, __LINE__, #a, #b, va_, vb_, \
                  static_cast<double>(eps));                                                       \
    }                                                                                              \
  } while (0)

#define CHECK_EQ(a, b)                                                                         \
  do {                                                                                         \
    ++testing::checks();                                                                       \
    if (!((a) == (b))) {                                                                       \
      ++testing::failures();                                                                   \
      std::printf("    FAILED %s:%d  %s == %s\n", __FILE__, __LINE__, #a, #b);                 \
    }                                                                                          \
  } while (0)
