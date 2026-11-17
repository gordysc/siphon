#ifndef MINTED_TESTS_CHECK_H_
#define MINTED_TESTS_CHECK_H_

// Header guard; see src/ring_buffer.h for how it works.

#include <cstdio>
#include <functional>
#include <span>

/// @brief A tiny test harness, so the tests need no extra library.
///
/// Each test is a plain function. CHECK() records a failure and carries on,
/// so one run reports every broken check. RunTests() returns the exit code
/// that CTest reads: 0 for success, 1 for any failure.
namespace siphon::testing {

/// @brief How many CHECK()s have failed so far in this program.
///
/// An `inline` variable (C++17) in a header is one shared variable, however
/// many .cpp files include the header.
inline int failures = 0;

/// @brief One named test.
struct Test {
  /// @brief Printed in the report. A string literal lives for the whole
  /// program, so a stored `const char*` to one is safe.
  const char* name;
  /// @brief The test body.
  std::function<void()> run;
};

/// @brief Runs every test and prints a summary.
/// @param tests The tests to run.
/// @return 0 if every check passed, otherwise 1.
inline int RunTests(std::span<const Test> tests) {
  for (const Test& test : tests) {
    const int before = failures;
    test.run();
    std::printf("[%s] %s\n", failures == before ? "PASS" : "FAIL", test.name);
  }
  std::printf("%d check(s) failed\n", failures);
  return failures == 0 ? 0 : 1;
}

}  // namespace siphon::testing

/// @brief Checks that `condition` is true, and reports where if it isn't.
///
/// This has to be a macro, not a function. Only the preprocessor can turn
/// the condition's source text into a string (`#condition`) and fill in the
/// file name and line number where CHECK() was written. The `do { } while
/// (false)` wrapper makes the macro act like one statement, so it works
/// safely inside an `if` without braces.
#define CHECK(condition)                                           \
  do {                                                             \
    if (!(condition)) {                                            \
      std::printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
                  #condition);                                     \
      ++::siphon::testing::failures;                               \
    }                                                              \
  } while (false)

#endif  // MINTED_TESTS_CHECK_H_
