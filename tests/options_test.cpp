// Command-line parsing tests.

#include "options.h"

#include <array>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "check.h"

namespace siphon {
namespace {

/// @brief Parses `args` and throws away the error text.
/// @param args The arguments. An initializer_list lets a test write
/// `Parse({"-t", "5"})`. The string literals live for the whole program,
/// so the string_views made from them stay valid.
std::optional<Options> Parse(std::initializer_list<std::string_view> args) {
  const std::vector<std::string_view> list(args);
  std::string error;
  return ParseOptions(list, &error);
}

/// @brief No arguments: default name, no time limit.
void TestNoArguments() {
  const auto options = Parse({});
  CHECK(options.has_value());
  CHECK(options->output_path.empty());
  CHECK(!options->max_seconds.has_value());
  CHECK(!options->force && !options->show_help);
}

/// @brief A path, a time limit and --force, in any order.
void TestAllOptions() {
  const auto options = Parse({"-t", "2.5", "out.wav", "--force"});
  CHECK(options.has_value());
  CHECK(options->output_path == "out.wav");
  CHECK(options->max_seconds == 2.5);
  CHECK(options->force);
}

/// @brief Long and short names do the same thing.
void TestLongNames() {
  const auto options = Parse({"--seconds", "10", "--help"});
  CHECK(options.has_value());
  CHECK(options->max_seconds == 10.0);
  CHECK(options->show_help);
}

/// @brief Bad input gives no options.
void TestBadInput() {
  CHECK(!Parse({"-t"}).has_value());           // No number.
  CHECK(!Parse({"-t", "abc"}).has_value());    // Not a number.
  CHECK(!Parse({"-t", "10s"}).has_value());    // Junk after the number.
  CHECK(!Parse({"-t", "0"}).has_value());      // Not positive.
  CHECK(!Parse({"-t", "-3"}).has_value());     // Not positive.
  CHECK(!Parse({"-t", "inf"}).has_value());    // Not finite.
  CHECK(!Parse({"--loud"}).has_value());       // Unknown option.
  CHECK(!Parse({"a.wav", "b.wav"}).has_value());  // Two outputs.
}

/// @brief The error text names the problem.
void TestErrorText() {
  const std::vector<std::string_view> args = {"--loud"};
  std::string error;
  CHECK(!ParseOptions(args, &error).has_value());
  CHECK(error == "unknown option: --loud");
}

/// @brief The default name holds the date and time, with leading zeros.
void TestDefaultOutputPath() {
  std::tm time{};
  time.tm_year = 2026 - 1900;  // tm_year counts from 1900.
  time.tm_mon = 0;             // tm_mon counts from 0, so 0 is January.
  time.tm_mday = 5;
  time.tm_hour = 9;
  time.tm_min = 3;
  time.tm_sec = 7;
  CHECK(DefaultOutputPath(time) == "siphon-20260105-090307.wav");
}

}  // namespace
}  // namespace siphon

/// @brief Runs every test in this file.
/// @return 0 if every check passed, otherwise 1.
int main() {
  using namespace siphon;
  const std::array tests = {
      testing::Test{"no arguments", TestNoArguments},
      testing::Test{"all options", TestAllOptions},
      testing::Test{"long names", TestLongNames},
      testing::Test{"bad input", TestBadInput},
      testing::Test{"error text", TestErrorText},
      testing::Test{"default output path", TestDefaultOutputPath},
  };
  return testing::RunTests(tests);
}
