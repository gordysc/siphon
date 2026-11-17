// Sample conversion and level tests.

#include "sample_math.h"

#include <array>
#include <cmath>
#include <cstdint>

#include "check.h"

namespace siphon {
namespace {

/// @brief Full scale, zero and halfway convert to the expected integers.
void TestFloatToPcm16Range() {
  CHECK(FloatToPcm16(0.0f) == 0);
  CHECK(FloatToPcm16(1.0f) == 32767);
  CHECK(FloatToPcm16(-1.0f) == -32767);
  CHECK(FloatToPcm16(0.5f) == 16384);  // 16383.5 rounds away from zero.
}

/// @brief Samples past full scale clip instead of wrapping around.
void TestFloatToPcm16Clips() {
  CHECK(FloatToPcm16(1.5f) == 32767);
  CHECK(FloatToPcm16(-7.0f) == -32767);
}

/// @brief ConvertToPcm16() converts each sample.
void TestConvertToPcm16() {
  const std::array<float, 3> in = {0.0f, 1.0f, -1.0f};
  std::array<std::int16_t, 3> out{};
  ConvertToPcm16(in, out);
  CHECK(out[0] == 0 && out[1] == 32767 && out[2] == -32767);
}

/// @brief Full scale is 0 dB, half is about -6 dB, silence is kSilenceDb.
void TestPeakDb() {
  const std::array<float, 3> full = {0.1f, -1.0f, 0.2f};
  CHECK(std::abs(PeakDb(full)) < 0.01f);
  const std::array<float, 1> half = {0.5f};
  CHECK(std::abs(PeakDb(half) - -6.02f) < 0.01f);
  const std::array<float, 2> silence = {0.0f, 0.0f};
  CHECK(PeakDb(silence) == kSilenceDb);
  CHECK(PeakDb({}) == kSilenceDb);
}

}  // namespace
}  // namespace siphon

/// @brief Runs every test in this file.
/// @return 0 if every check passed, otherwise 1.
int main() {
  using namespace siphon;
  const std::array tests = {
      testing::Test{"FloatToPcm16 range", TestFloatToPcm16Range},
      testing::Test{"FloatToPcm16 clips", TestFloatToPcm16Clips},
      testing::Test{"ConvertToPcm16", TestConvertToPcm16},
      testing::Test{"PeakDb", TestPeakDb},
  };
  return testing::RunTests(tests);
}
