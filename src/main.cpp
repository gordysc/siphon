// siphon: records the sound your Mac plays to a WAV file.
//
//   siphon                    Record until Ctrl+C, to siphon-<date>-<time>.wav
//   siphon song.wav -t 30     Record 30 seconds to song.wav
//
// Two threads share the work. Core Audio's real-time thread copies each
// block of sound into a RingBuffer (see system_tap.mm). This main thread
// wakes up every 50 ms, empties the ring, and writes the samples to disk.

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "options.h"
#include "ring_buffer.h"
#include "sample_math.h"
#include "system_tap.h"
#include "wav_writer.h"

namespace {

/// @brief How long the main thread sleeps between two drains of the ring.
constexpr auto kDrainInterval = std::chrono::milliseconds(50);

/// @brief Ring size in samples: about 10 seconds of 48 kHz stereo. The main
/// thread empties it every 50 ms, so it only fills up if the disk stalls
/// for a long time.
constexpr std::size_t kRingSamples = 1 << 20;

/// @brief Frames the main thread reads from the ring at a time.
constexpr std::size_t kChunkFrames = 4096;

/// @brief Characters in the level meter.
constexpr int kMeterWidth = 30;

/// @brief Set to 1 by HandleInterrupt() when the user presses Ctrl+C.
///
/// A signal handler may run between any two instructions of the main
/// thread. The only kind of variable the C++ standard lets it write
/// safely is a `volatile std::sig_atomic_t`. `volatile` makes the main
/// loop read it from memory each time, so it sees the change.
volatile std::sig_atomic_t g_stop_requested = 0;

/// @brief Runs when the user presses Ctrl+C (the SIGINT signal).
///
/// Without a handler, Ctrl+C ends the program at once, and the WAV header
/// would never get its real size. With it, the main loop sees the flag,
/// stops, and closes the file properly.
/// @param signal The signal number (unused).
void HandleInterrupt(int /*signal*/) { g_stop_requested = 1; }

/// @brief Prints a time as minutes and seconds, such as "02:05".
/// @param seconds The time.
/// @return The text.
std::string FormatTime(double seconds) {
  const auto whole = static_cast<long>(seconds);
  char text[32];
  std::snprintf(text, sizeof(text), "%02ld:%02ld", whole / 60, whole % 60);
  return text;
}

/// @brief Draws the status line: time recorded and a level meter.
///
/// "\r" moves the cursor to the start of the line without a new line, so
/// each update draws over the last one.
/// @param seconds Time recorded so far.
/// @param peak_db Loudest level since the last update, in dB.
void PrintStatus(double seconds, float peak_db) {
  // Map kSilenceDb..0 dB onto 0..kMeterWidth characters.
  const float fraction = 1.0f - peak_db / siphon::kSilenceDb;
  const int filled = std::clamp(static_cast<int>(fraction * kMeterWidth), 0,
                                kMeterWidth);
  const std::string meter =
      std::string(filled, '#') + std::string(kMeterWidth - filled, '.');
  std::printf("\r  %s  [%s] %4.0f dB ", FormatTime(seconds).c_str(),
              meter.c_str(), peak_db);
  // stdout normally waits for a full line before it prints. fflush() makes
  // it print now, even though the line has no '\n'.
  std::fflush(stdout);
}

/// @brief Moves samples from the ring into the WAV file.
/// @param ring The ring to empty.
/// @param writer The open file.
/// @param channels Samples per frame.
/// @param frames_left The most frames to write. Counts down, so a time
/// limit cuts the recording at the exact frame. Empty means no limit.
/// Passed by (non-const) reference, so the caller sees the new count.
/// @param floats Space for samples read from the ring. Its size must be a
/// whole number of frames. The vectors are passed in by reference so they
/// are allocated once in main(), not again on every call.
/// @param pcm Space for the 16-bit samples. At least as big as `floats`.
/// @param peak_db Set to the loudest level in the samples written.
/// @param error Set to the reason on failure.
/// @return How many frames were written, or nothing on a write error.
std::optional<std::uint64_t> Drain(siphon::RingBuffer& ring,
                                   siphon::WavWriter& writer,
                                   std::uint16_t channels,
                                   std::optional<std::uint64_t>& frames_left,
                                   std::vector<float>& floats,
                                   std::vector<std::int16_t>& pcm,
                                   float& peak_db, std::string* error) {
  std::uint64_t frames_written = 0;
  peak_db = siphon::kSilenceDb;
  while (true) {
    // The audio thread writes whole frames, and `floats` holds a whole
    // number of frames, so Read() always returns whole frames too.
    std::size_t count = ring.Read(floats);
    if (count == 0) {
      return frames_written;
    }
    std::size_t frames = count / channels;
    if (frames_left) {
      // *frames_left is the number inside the optional.
      frames = static_cast<std::size_t>(
          std::min<std::uint64_t>(frames, *frames_left));
      *frames_left -= frames;
      count = frames * channels;
    }
    // A std::span is a view of part of a vector, with no copy.
    const std::span<const float> samples(floats.data(), count);
    peak_db = std::max(peak_db, siphon::PeakDb(samples));
    siphon::ConvertToPcm16(samples, pcm);
    if (!writer.Write({pcm.data(), count}, error)) {
      return std::nullopt;
    }
    frames_written += frames;
    if (frames_left && *frames_left == 0) {
      return frames_written;
    }
  }
}

}  // namespace

/// @brief Parses the options, records, and saves the file.
/// @param argc How many command-line arguments there are, including the
/// program's own name.
/// @param argv The arguments. argv[0] is the program's name.
/// @return 0 on success, 1 on any error.
int main(int argc, char** argv) {
  // Copy the C-style arguments into a vector of string_views (views of
  // argv's characters, no copies), skipping argv[0]. argv lives until main()
  // returns, so the views stay valid.
  const std::vector<std::string_view> args(argv + 1, argv + argc);
  std::string error;
  const std::optional<siphon::Options> options =
      siphon::ParseOptions(args, &error);
  if (!options) {
    std::fprintf(stderr, "siphon: %s\n\n%s", error.c_str(), siphon::kUsage);
    return 1;
  }
  if (options->show_help) {
    std::printf("%s", siphon::kUsage);
    return 0;
  }

  std::string path = options->output_path;
  if (path.empty()) {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    // localtime_r is the thread-safe version of std::localtime: it fills
    // our `local` instead of a shared static struct.
    localtime_r(&now, &local);
    path = siphon::DefaultOutputPath(local);
  }
  if (std::filesystem::exists(path) && !options->force) {
    std::fprintf(stderr,
                 "siphon: %s already exists (use --force to replace it)\n",
                 path.c_str());
    return 1;
  }

  // The ring must outlive the tap, because the audio thread writes to it
  // until the tap is destroyed. Local variables are destroyed in the
  // reverse order they were made, so declaring `ring` first means it is
  // destroyed last.
  siphon::RingBuffer ring(kRingSamples);
  std::unique_ptr<siphon::SystemTap> tap =
      siphon::SystemTap::Start(&ring, &error);
  if (!tap) {
    std::fprintf(stderr, "siphon: %s\n", error.c_str());
    return 1;
  }
  const std::uint16_t channels = tap->channels();
  const std::uint32_t sample_rate = tap->sample_rate();

  siphon::WavWriter writer;
  if (!writer.Open(path, sample_rate, channels, &error)) {
    std::fprintf(stderr, "siphon: %s\n", error.c_str());
    return 1;
  }

  std::optional<std::uint64_t> frames_left;
  if (options->max_seconds) {
    frames_left = static_cast<std::uint64_t>(*options->max_seconds *
                                             sample_rate + 0.5);
  }

  std::signal(SIGINT, HandleInterrupt);
  std::printf("Recording %s (%u Hz, %u channels) to %s\n",
              tap->device_name().c_str(), sample_rate, channels, path.c_str());
  std::printf("Press Ctrl+C to stop.\n");

  std::vector<float> floats(kChunkFrames * channels);
  std::vector<std::int16_t> pcm(floats.size());
  std::uint64_t total_frames = 0;
  // The loudest level of the whole recording, to spot a silent file.
  float loudest_db = siphon::kSilenceDb;
  bool ok = true;
  while (!g_stop_requested && !(frames_left && *frames_left == 0)) {
    std::this_thread::sleep_for(kDrainInterval);
    float peak_db = siphon::kSilenceDb;
    const auto frames = Drain(ring, writer, channels, frames_left, floats, pcm,
                              peak_db, &error);
    if (!frames) {
      ok = false;
      break;
    }
    total_frames += *frames;
    loudest_db = std::max(loudest_db, peak_db);
    PrintStatus(static_cast<double>(total_frames) / sample_rate, peak_db);
  }

  // Stop the audio thread first, then write out what is left in the ring.
  // reset() destroys the SystemTap now, rather than at the end of main().
  tap.reset();
  if (ok) {
    float peak_db = siphon::kSilenceDb;
    const auto frames = Drain(ring, writer, channels, frames_left, floats, pcm,
                              peak_db, &error);
    ok = frames.has_value();
    total_frames += frames.value_or(0);
  }
  std::printf("\n");

  std::string close_error;
  if (!writer.Close(&close_error) && ok) {
    error = close_error;
    ok = false;
  }
  if (!ok) {
    std::fprintf(stderr, "siphon: %s\n", error.c_str());
    return 1;
  }

  std::printf("Saved %s of audio to %s\n",
              FormatTime(static_cast<double>(total_frames) / sample_rate)
                  .c_str(),
              path.c_str());
  // Without permission, macOS doesn't report an error: the tap just gives
  // silence. Perfect digital silence (every sample exactly 0) is rare when
  // anything plays, so it is a strong hint.
  if (loudest_db <= siphon::kSilenceDb) {
    std::printf(
        "Warning: the recording is silent. If sound was playing, allow\n"
        "your terminal app in System Settings > Privacy & Security >\n"
        "Screen & System Audio Recording, then run siphon again.\n");
  }
  if (ring.dropped() > 0) {
    std::printf("Warning: %llu samples were lost because the disk was slow.\n",
                static_cast<unsigned long long>(ring.dropped()));
  }
  return 0;
}
