#include "options.h"

#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <ctime>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace siphon {
namespace {

/// @brief Reads a number of seconds, such as "30" or "2.5".
/// @return The number, or nothing if the text is not a positive number.
std::optional<double> ParseSeconds(std::string_view text) {
  // strtod needs a null-terminated string, and a string_view may not be
  // one, so copy it into a std::string first.
  const std::string copy(text);
  char* end = nullptr;
  const double value = std::strtod(copy.c_str(), &end);
  // `end` points at the first character strtod couldn't read. Anything left
  // over ("10s", "1x") makes the whole argument wrong. isfinite() rejects
  // "inf" and "nan", which strtod accepts.
  if (copy.empty() || *end != '\0' || !std::isfinite(value) || value <= 0) {
    return std::nullopt;
  }
  return value;
}

}  // namespace

std::optional<Options> ParseOptions(std::span<const std::string_view> args,
                                    std::string* error) {
  Options options;
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string_view arg = args[i];
    if (arg == "-h" || arg == "--help") {
      options.show_help = true;
    } else if (arg == "-f" || arg == "--force") {
      options.force = true;
    } else if (arg == "-t" || arg == "--seconds") {
      if (i + 1 == args.size()) {
        *error = std::string(arg) + " needs a number of seconds";
        return std::nullopt;
      }
      // ++i takes the next argument as this option's value, so the loop
      // doesn't read it again as an argument of its own.
      const std::string_view value = args[++i];
      options.max_seconds = ParseSeconds(value);
      if (!options.max_seconds) {
        *error = "not a positive number of seconds: " + std::string(value);
        return std::nullopt;
      }
    } else if (arg.starts_with('-') && arg != "-") {
      *error = "unknown option: " + std::string(arg);
      return std::nullopt;
    } else if (options.output_path.empty()) {
      options.output_path = arg;
    } else {
      *error = "only one output file, please (got " + options.output_path +
               " and " + std::string(arg) + ")";
      return std::nullopt;
    }
  }
  return options;
}

std::string DefaultOutputPath(const std::tm& local_time) {
  char name[64];
  // %Y%m%d-%H%M%S gives 20261117-143005: year, month, day, then hour,
  // minute, second, each with leading zeros.
  std::strftime(name, sizeof(name), "siphon-%Y%m%d-%H%M%S.wav", &local_time);
  return name;
}

}  // namespace siphon
