#include "sample_math.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace siphon {

std::int16_t FloatToPcm16(float sample) {
  const float clipped = std::clamp(sample, -1.0f, 1.0f);
  // Scale by 32767, not 32768, so +1.0 becomes 32767 and fits. -1.0 then
  // becomes -32767, and -32768 is never used; one step out of 65536 doesn't
  // matter. std::lround rounds to the nearest whole number. A plain cast
  // would always round toward zero, which adds a small distortion.
  return static_cast<std::int16_t>(std::lround(clipped * 32767.0f));
}

void ConvertToPcm16(std::span<const float> in, std::span<std::int16_t> out) {
  assert(out.size() >= in.size());
  std::transform(in.begin(), in.end(), out.begin(), FloatToPcm16);
}

float PeakDb(std::span<const float> samples) {
  float peak = 0.0f;
  for (const float sample : samples) {
    peak = std::max(peak, std::abs(sample));
  }
  if (peak <= 0.0f) {
    return kSilenceDb;  // log10(0) would be minus infinity.
  }
  return std::max(20.0f * std::log10(peak), kSilenceDb);
}

}  // namespace siphon
