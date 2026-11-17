#include "wav_writer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace siphon {
namespace {

/// @brief Bytes in one 16-bit sample.
constexpr std::uint16_t kBytesPerSample = 2;

/// @brief Writes numbers and names into a byte array, lowest byte first.
///
/// Building the bytes by hand, rather than copying a struct into the file,
/// gives the same file on any computer. A struct could get padding bytes
/// between its fields, and a big-endian computer would store each number
/// with its bytes in the opposite order.
class ByteWriter {
 public:
  /// @brief Starts writing at the first byte of `out`.
  /// @param out The bytes to fill. A std::span is a view (a pointer and a
  /// length): it doesn't own or copy the bytes, so the array must outlive
  /// the writer. In WavHeader() both live in the same function.
  explicit ByteWriter(std::span<std::uint8_t> out) : out_(out) {}

  /// @brief Writes a 4-letter chunk name, such as "RIFF".
  /// @param tag The name. `const char (&)[5]` is a reference to an array of
  /// exactly 5 chars: a string literal of 4 letters plus its '\0' end
  /// marker. A longer or shorter literal doesn't compile. Only the 4
  /// letters are written.
  void Tag(const char (&tag)[5]) {
    for (int i = 0; i < 4; ++i) {
      out_[pos_++] = static_cast<std::uint8_t>(tag[i]);
    }
  }

  /// @brief Writes a 16-bit number, low byte first.
  /// @param value The number. `value >> 8` moves the high byte down. The
  /// cast to uint8_t keeps only the lowest 8 bits.
  void U16(std::uint16_t value) {
    out_[pos_++] = static_cast<std::uint8_t>(value);
    out_[pos_++] = static_cast<std::uint8_t>(value >> 8);
  }

  /// @brief Writes a 32-bit number, low 16 bits first.
  /// @param value The number.
  void U32(std::uint32_t value) {
    U16(static_cast<std::uint16_t>(value));
    U16(static_cast<std::uint16_t>(value >> 16));
  }

 private:
  /// @brief The bytes being filled. Not owned; see the constructor.
  std::span<std::uint8_t> out_;
  /// @brief Where the next byte goes.
  std::size_t pos_ = 0;
};

}  // namespace

std::array<std::uint8_t, kWavHeaderBytes> WavHeader(std::uint32_t sample_rate,
                                                    std::uint16_t channels,
                                                    std::uint32_t data_bytes) {
  const std::uint16_t block_align = channels * kBytesPerSample;
  std::array<std::uint8_t, kWavHeaderBytes> header{};
  ByteWriter out(header);
  out.Tag("RIFF");
  // The RIFF size counts everything after this field: "WAVE" (4), the fmt
  // chunk (8 + 16) and the data chunk (8 + data). That is the file size - 8.
  out.U32(static_cast<std::uint32_t>(kWavHeaderBytes - 8 + data_bytes));
  out.Tag("WAVE");

  out.Tag("fmt ");  // The space is part of the name.
  out.U32(16);      // Size of the fmt chunk's data below.
  out.U16(1);       // Format 1: plain integer PCM.
  out.U16(channels);
  out.U32(sample_rate);
  out.U32(sample_rate * block_align);  // Bytes per second.
  out.U16(block_align);                // Bytes per frame.
  out.U16(kBytesPerSample * 8);        // Bits per sample.

  out.Tag("data");
  out.U32(data_bytes);
  return header;
}

WavWriter::~WavWriter() { Close(nullptr); }

bool WavWriter::Open(const std::filesystem::path& path,
                     std::uint32_t sample_rate, std::uint16_t channels,
                     std::string* error) {
  Close(nullptr);
  file_.open(path, std::ios::binary | std::ios::trunc);
  if (!file_) {
    *error = "could not create " + path.string();
    return false;
  }
  sample_rate_ = sample_rate;
  channels_ = channels;
  data_bytes_ = 0;
  const auto header = WavHeader(sample_rate_, channels_, 0);
  file_.write(reinterpret_cast<const char*>(header.data()), header.size());
  if (!file_) {
    *error = "could not write to " + path.string();
    return false;
  }
  return true;
}

bool WavWriter::Write(std::span<const std::int16_t> samples,
                      std::string* error) {
  const std::uint64_t bytes = samples.size_bytes();
  if (data_bytes_ + bytes > kMaxWavDataBytes) {
    *error = "the file reached the 4 GiB size limit of WAV";
    return false;
  }
  // Writing the int16_t values as raw bytes assumes a little-endian CPU.
  // Every Mac (Intel and Apple silicon) and every common PC is one. A
  // big-endian port would have to swap the bytes of each sample first.
  file_.write(reinterpret_cast<const char*>(samples.data()),
              static_cast<std::streamsize>(bytes));
  if (!file_) {
    *error = "could not write the samples (is the disk full?)";
    return false;
  }
  data_bytes_ += bytes;
  return true;
}

bool WavWriter::Close(std::string* error) {
  if (!file_.is_open()) {
    return true;
  }
  const auto header = WavHeader(sample_rate_, channels_,
                                static_cast<std::uint32_t>(data_bytes_));
  file_.seekp(0);
  file_.write(reinterpret_cast<const char*>(header.data()), header.size());
  file_.close();
  if (!file_) {
    if (error != nullptr) {
      *error = "could not finish the WAV header";
    }
    return false;
  }
  return true;
}

}  // namespace siphon
