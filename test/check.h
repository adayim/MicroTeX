// A few lines of test harness, so the tests need nothing but the library.
//
//   TEST(name) { CHECK_EQ(f(x), expected); }
//
// Every TEST in the executable runs (main.cpp); a failed check reports its
// file, line and both values, and the test goes on to its next check.

#ifndef MICROTEX_TEST_CHECK_H
#define MICROTEX_TEST_CHECK_H

#include <exception>
#include <sstream>
#include <string>
#include <vector>

namespace check {

using Strings = std::vector<std::string>;

struct Case {
  const char* name;
  void (*fn)();
};

inline std::vector<Case>& cases() {
  static std::vector<Case> all;
  return all;
}

inline int& failures() {
  static int n = 0;
  return n;
}

struct Add {
  Add(const char* name, void (*fn)()) { cases().push_back({name, fn}); }
};

template <class T>
std::string show(const T& x) {
  std::ostringstream s;
  s << x;
  return s.str();
}

inline std::string show(const std::string& x) { return '"' + x + '"'; }

template <class T>
std::string show(const std::vector<T>& v) {
  std::string out = "{";
  for (std::size_t i = 0; i < v.size(); i++) out += (i ? ", " : "") + show(v[i]);
  return out + "}";
}

void fail(const char* file, int line, const std::string& what);

inline bool contains(const std::string& s, const std::string& part) {
  return s.find(part) != std::string::npos;
}

// Every string contains `part`, and there is at least one.
inline bool allContain(const Strings& all, const std::string& part) {
  if (all.empty()) return false;
  for (const auto& s : all) {
    if (!contains(s, part)) return false;
  }
  return true;
}

inline bool anyContains(const Strings& all, const std::string& part) {
  for (const auto& s : all) {
    if (contains(s, part)) return true;
  }
  return false;
}

}  // namespace check

#define TEST(name)                                     \
  static void name();                                  \
  static const check::Add add_##name(#name, name);     \
  static void name()

#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) check::fail(__FILE__, __LINE__, "CHECK(" #cond ")");   \
  } while (0)

#define CHECK_EQ(actual, expected)                                          \
  do {                                                                      \
    const auto& a_ = (actual);                                              \
    const auto& e_ = (expected);                                            \
    if (!(a_ == e_)) {                                                      \
      check::fail(__FILE__, __LINE__,                                       \
                  #actual "\n  got:      " + check::show(a_) +              \
                    "\n  expected: " + check::show(e_));                    \
    }                                                                       \
  } while (0)

// `expr` throws, with `part` in its message.
#define CHECK_THROWS(expr, part)                                               \
  do {                                                                         \
    std::string what_;                                                         \
    bool threw_ = false;                                                       \
    try {                                                                      \
      (void)(expr);                                                            \
    } catch (const std::exception& e) {                                        \
      threw_ = true;                                                           \
      what_ = e.what();                                                        \
    }                                                                          \
    if (!threw_) {                                                             \
      check::fail(__FILE__, __LINE__, #expr "\n  did not throw");              \
    } else if (!check::contains(what_, part)) {                                \
      check::fail(__FILE__, __LINE__,                                          \
                  #expr "\n  threw: " + what_ + "\n  expected: " + (part));    \
    }                                                                          \
  } while (0)

#endif
