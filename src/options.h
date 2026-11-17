#ifndef SIPHON_OPTIONS_H_
#define SIPHON_OPTIONS_H_

// Header guard; see ring_buffer.h for how it works.

#include <ctime>
#include <optional>
#include <span>
#include <string>
#include <string_view>

/// @brief Everything that belongs to the program.
namespace siphon {

/// @brief The usage text that `siphon --help` prints.
inline constexpr const char* kUsage =
    "usage: siphon [options] [output.wav]\n"
    "\n"
    "Records the sound your Mac plays to a WAV file, until you press\n"
    "Ctrl+C. You still hear the sound as normal while it records.\n"
    "\n"
    "  output.wav        Where to save. Default: siphon-<date>-<time>.wav\n"
    "  -t, --seconds N   Stop by itself after N seconds.\n"
    "  -f, --force       Replace the output file if it already exists.\n"
    "  -h, --help        Show this text.\n";

/// @brief What the command line asks for.
struct Options {
  /// @brief Where to save. Empty means "make a name from the date and
  /// time" (see DefaultOutputPath()).
  std::string output_path;
  /// @brief Stop after this many seconds. Empty means "until Ctrl+C".
  std::optional<double> max_seconds;
  /// @brief True if an existing output file may be replaced.
  bool force = false;
  /// @brief True if the user asked for the usage text.
  bool show_help = false;
};

/// @brief Reads the command-line arguments.
/// @param args The arguments, without the program's own name (argv[0]).
/// @param error Set to the reason on failure.
/// @return The options, or nothing if the arguments are wrong.
std::optional<Options> ParseOptions(std::span<const std::string_view> args,
                                    std::string* error);

/// @brief Makes a file name from a date and time, for example
/// "siphon-20261117-143005.wav". Names made this way sort by time.
/// @param local_time The date and time to use, usually the local time now.
/// @return The file name.
std::string DefaultOutputPath(const std::tm& local_time);

}  // namespace siphon

#endif  // SIPHON_OPTIONS_H_
