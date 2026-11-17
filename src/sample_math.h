#ifndef SIPHON_SAMPLE_MATH_H_
#define SIPHON_SAMPLE_MATH_H_

// Header guard; see ring_buffer.h for how it works.

#include <cstdint>
#include <span>

/// @brief Everything that belongs to the program.
namespace siphon {

/// @brief The quietest level PeakDb() reports, in decibels.
///
/// A 16-bit sample can't hold anything quieter than about -96 dB, so the
/// file can't either.
inline constexpr float kSilenceDb = -96.0f;

/// @brief Turns one float sample into a 16-bit PCM sample.
///
/// Core Audio gives samples as floats from -1.0 to 1.0. A WAV file with
/// 16-bit samples stores whole numbers from -32768 to 32767. Values outside
/// -1.0 to 1.0 are clipped (cut off at the limit). Without the clip, a
/// too-loud sample would wrap around to the other end of the range and play
/// as a loud click.
/// @param sample The float sample.
/// @return The 16-bit sample.
std::int16_t FloatToPcm16(float sample);

/// @brief Calls FloatToPcm16() on each sample.
/// @param in The float samples.
/// @param out Where to put the 16-bit samples. Must be at least as long as
/// `in`.
void ConvertToPcm16(std::span<const float> in, std::span<std::int16_t> out);

/// @brief The loudest sample, in decibels relative to full scale (dBFS).
///
/// 0 dB is the loudest sample a file can hold (1.0). Every halving of the
/// level takes off about 6 dB. Typical music peaks between -12 and 0 dB.
/// @param samples The samples to look at.
/// @return The peak level, from kSilenceDb up to 0 (or above 0 if a
/// sample is above 1.0). kSilenceDb for silence or no samples.
float PeakDb(std::span<const float> samples);

}  // namespace siphon

#endif  // SIPHON_SAMPLE_MATH_H_
