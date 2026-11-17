#ifndef SIPHON_WAV_WRITER_H_
#define SIPHON_WAV_WRITER_H_

// Header guard; see ring_buffer.h for how it works.

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>

/// @brief Everything that belongs to the program.
namespace siphon {

/// @brief Size of the header that WavHeader() makes, in bytes.
inline constexpr std::size_t kWavHeaderBytes = 44;

/// @brief The most sample data one WAV file can hold, in bytes.
///
/// The file stores its own size, less 8 bytes, in an unsigned 32-bit field.
/// So the whole file must stay under 4 GiB. At 48,000 frames per second in
/// 16-bit stereo, that is about 6 hours 12 minutes.
inline constexpr std::uint64_t kMaxWavDataBytes =
    0xFFFFFFFFull - (kWavHeaderBytes - 8);

/// @brief Makes the 44-byte header of a 16-bit PCM WAV file.
///
/// A WAV file is a RIFF file: a list of "chunks", each a 4-letter name, a
/// 32-bit size, then the data. This one has three parts:
///
///     "RIFF" <file size - 8> "WAVE"
///     "fmt " <16>  format, channels, sample rate, ...
///     "data" <data size>  the samples
///
/// Every number is little-endian (lowest byte first).
/// @param sample_rate Frames per second, for example 48000.
/// @param channels Samples per frame, for example 2 for stereo.
/// @param data_bytes Size of the sample data that follows the header.
/// @return The header bytes.
std::array<std::uint8_t, kWavHeaderBytes> WavHeader(std::uint32_t sample_rate,
                                                    std::uint16_t channels,
                                                    std::uint32_t data_bytes);

/// @brief Writes 16-bit PCM samples to a WAV file.
///
/// The header holds the size of the data, which is unknown until the
/// recording ends. So Open() writes a header with size 0, Write() adds
/// samples, and Close() goes back to the start and writes the real sizes.
///
/// If the program stops without Close() (a crash, or `kill -9`), the
/// samples are in the file but the header still says 0. Most audio programs
/// then show an empty file.
class WavWriter {
 public:
  /// @brief Makes a writer with no file open.
  WavWriter() = default;

  /// @brief Calls Close() if a file is still open.
  ~WavWriter();

  /// @brief Not copyable: two writers can't own one file.
  WavWriter(const WavWriter&) = delete;
  /// @brief Not copy-assignable, for the same reason.
  WavWriter& operator=(const WavWriter&) = delete;

  /// @brief Creates (or replaces) the file and writes the first header.
  /// @param path Where to write.
  /// @param sample_rate Frames per second.
  /// @param channels Samples per frame.
  /// @param error Set to the reason on failure.
  /// @return True on success.
  bool Open(const std::filesystem::path& path, std::uint32_t sample_rate,
            std::uint16_t channels, std::string* error);

  /// @brief Adds samples to the end of the file.
  /// @param samples Interleaved samples: for stereo, left, right, left,
  /// right, and so on.
  /// @param error Set to the reason on failure.
  /// @return True on success. False if the write fails, or if the samples
  /// would take the file past kMaxWavDataBytes. Nothing is written then.
  bool Write(std::span<const std::int16_t> samples, std::string* error);

  /// @brief Writes the real sizes into the header and closes the file.
  ///
  /// Does nothing if no file is open.
  /// @param error Set to the reason on failure. May be null.
  /// @return True on success.
  bool Close(std::string* error);

  /// @brief How many bytes of samples Write() has added.
  std::uint64_t data_bytes() const { return data_bytes_; }

 private:
  /// @brief The open file. Binary mode, so nothing changes the bytes.
  std::ofstream file_;
  /// @brief See Open().
  std::uint32_t sample_rate_ = 0;
  /// @brief See Open().
  std::uint16_t channels_ = 0;
  /// @brief See data_bytes().
  std::uint64_t data_bytes_ = 0;
};

}  // namespace siphon

#endif  // SIPHON_WAV_WRITER_H_
