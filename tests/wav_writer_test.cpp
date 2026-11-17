// WAV header and writer tests. The writer test makes a real file in the
// system's temporary folder and reads its bytes back.

#include "wav_writer.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "check.h"

namespace siphon {
namespace {

/// @brief Reads a little-endian 32-bit number from `bytes` at `offset`.
std::uint32_t ReadU32(const std::uint8_t* bytes, std::size_t offset) {
  return bytes[offset] | (bytes[offset + 1] << 8) | (bytes[offset + 2] << 16) |
         (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

/// @brief Reads a little-endian 16-bit number from `bytes` at `offset`.
std::uint16_t ReadU16(const std::uint8_t* bytes, std::size_t offset) {
  return static_cast<std::uint16_t>(bytes[offset] | (bytes[offset + 1] << 8));
}

/// @brief True if the 4 bytes at `offset` spell `tag`.
bool HasTag(const std::uint8_t* bytes, std::size_t offset, const char* tag) {
  return std::memcmp(bytes + offset, tag, 4) == 0;
}

/// @brief Each header field is at its standard offset, with the right value.
void TestHeaderFields() {
  const auto header = WavHeader(48000, 2, 1000);
  const std::uint8_t* b = header.data();
  CHECK(HasTag(b, 0, "RIFF"));
  CHECK(ReadU32(b, 4) == 36 + 1000);
  CHECK(HasTag(b, 8, "WAVE"));
  CHECK(HasTag(b, 12, "fmt "));
  CHECK(ReadU32(b, 16) == 16);
  CHECK(ReadU16(b, 20) == 1);           // PCM
  CHECK(ReadU16(b, 22) == 2);           // channels
  CHECK(ReadU32(b, 24) == 48000);       // sample rate
  CHECK(ReadU32(b, 28) == 48000 * 4);   // bytes per second
  CHECK(ReadU16(b, 32) == 4);           // bytes per frame
  CHECK(ReadU16(b, 34) == 16);          // bits per sample
  CHECK(HasTag(b, 36, "data"));
  CHECK(ReadU32(b, 40) == 1000);
}

/// @brief A written file has the samples after the header, and Close()
/// puts the real sizes in the header.
void TestWriteAndClose() {
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() / "siphon_wav_writer_test.wav";
  std::string error;
  {
    WavWriter writer;
    CHECK(writer.Open(path, 44100, 1, &error));
    const std::array<std::int16_t, 3> samples = {1, -2, 300};
    CHECK(writer.Write(samples, &error));
    CHECK(writer.Write(samples, &error));
    CHECK(writer.data_bytes() == 12);
    CHECK(writer.Close(&error));
  }

  std::ifstream file(path, std::ios::binary);
  // istreambuf_iterator reads the file byte by byte; the two iterators mean
  // "from the start to the end". This builds the vector from all the bytes.
  const std::vector<std::uint8_t> bytes(
      (std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  CHECK(bytes.size() == kWavHeaderBytes + 12);
  CHECK(ReadU32(bytes.data(), 4) == 36 + 12);
  CHECK(ReadU32(bytes.data(), 24) == 44100);
  CHECK(ReadU32(bytes.data(), 40) == 12);
  CHECK(static_cast<std::int16_t>(ReadU16(bytes.data(), 44)) == 1);
  CHECK(static_cast<std::int16_t>(ReadU16(bytes.data(), 46)) == -2);
  CHECK(static_cast<std::int16_t>(ReadU16(bytes.data(), 48)) == 300);
  file.close();
  std::filesystem::remove(path);
}

/// @brief The destructor closes the file and fixes the header, so an early
/// return in the program still leaves a valid file.
void TestDestructorCloses() {
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() / "siphon_wav_destructor_test.wav";
  std::string error;
  {
    WavWriter writer;
    CHECK(writer.Open(path, 8000, 2, &error));
    const std::array<std::int16_t, 4> samples = {1, 2, 3, 4};
    CHECK(writer.Write(samples, &error));
  }  // No Close(): the destructor runs here.
  CHECK(std::filesystem::file_size(path) == kWavHeaderBytes + 8);
  std::ifstream file(path, std::ios::binary);
  std::array<std::uint8_t, kWavHeaderBytes> header{};
  file.read(reinterpret_cast<char*>(header.data()), header.size());
  CHECK(ReadU32(header.data(), 40) == 8);
  file.close();
  std::filesystem::remove(path);
}

/// @brief Open() reports a folder that doesn't exist.
void TestOpenFailsOnBadPath() {
  WavWriter writer;
  std::string error;
  CHECK(!writer.Open("/no/such/folder/x.wav", 48000, 2, &error));
  CHECK(!error.empty());
}

}  // namespace
}  // namespace siphon

/// @brief Runs every test in this file.
/// @return 0 if every check passed, otherwise 1.
int main() {
  using namespace siphon;
  const std::array tests = {
      testing::Test{"header fields", TestHeaderFields},
      testing::Test{"write and close", TestWriteAndClose},
      testing::Test{"destructor closes", TestDestructorCloses},
      testing::Test{"open fails on bad path", TestOpenFailsOnBadPath},
  };
  return testing::RunTests(tests);
}
