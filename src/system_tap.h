#ifndef SIPHON_SYSTEM_TAP_H_
#define SIPHON_SYSTEM_TAP_H_

// Header guard; see ring_buffer.h for how it works.
//
// This header is plain C++, so main.cpp (a C++ file) can include it. The
// implementation, system_tap.mm, is Objective-C++: C++ that may also use
// Objective-C classes. It needs one Objective-C class, CATapDescription.
// Keeping every Objective-C and Core Audio type out of this header keeps
// the rest of the program plain C++.

#include <cstdint>
#include <memory>
#include <string>

#include "ring_buffer.h"

/// @brief Everything that belongs to the program.
namespace siphon {

/// @brief Captures everything the Mac plays, while it still plays as
/// normal. macOS 14.2 or later.
///
/// How it works:
/// 1. A "process tap" (AudioHardwareCreateProcessTap) taps the mixed
///    output of every app.
/// 2. A tap can't be read directly. It is put into a private "aggregate
///    device": a virtual audio device made of other devices and taps.
///    The tap shows up as the aggregate's input, like a microphone.
/// 3. An "IO proc", a function Core Audio calls on its real-time audio
///    thread about every 10 ms, receives each block of input and copies
///    it into a RingBuffer.
///
/// The first time it runs, macOS asks for permission to record system
/// audio. If permission is denied, the tap still runs but gives silence.
///
/// RAII ("resource acquisition is initialization"): Start() makes the
/// tap, the device and the IO proc. The destructor stops and destroys them,
/// in the reverse order. Nothing leaks, even on an early return.
class SystemTap {
 public:
  /// @brief Starts capturing.
  ///
  /// A static "factory" function rather than a constructor, because the
  /// setup can fail. A constructor can only report failure by throwing.
  /// @param ring Where the audio thread puts the samples. Not owned: it
  /// must outlive the returned tap, because the audio thread writes to it
  /// until the tap is destroyed.
  /// @param error Set to the reason on failure.
  /// @return The running tap, or null on failure.
  static std::unique_ptr<SystemTap> Start(RingBuffer* ring,
                                          std::string* error);

  /// @brief Stops capturing and frees every Core Audio object. When the
  /// destructor returns, the audio thread no longer touches the ring.
  ~SystemTap();

  /// @brief Not copyable: a copy would destroy the same device twice.
  SystemTap(const SystemTap&) = delete;
  /// @brief Not copy-assignable, for the same reason.
  SystemTap& operator=(const SystemTap&) = delete;

  /// @brief Frames per second of the captured audio, such as 48000.
  std::uint32_t sample_rate() const { return sample_rate_; }

  /// @brief Samples per frame. The samples in the ring are interleaved:
  /// for stereo, left, right, left, right, and so on.
  std::uint16_t channels() const { return channels_; }

  /// @brief The name of the output device being tapped, such as "MacBook
  /// Pro Speakers" or "AirPods".
  const std::string& device_name() const { return device_name_; }

 private:
  /// @brief The parts that need Core Audio types, defined in system_tap.mm.
  ///
  /// This is the "pimpl" (pointer to implementation) pattern. This header
  /// only declares that Impl exists. Code that includes the header doesn't
  /// need Impl's members, so it doesn't need the Objective-C headers.
  struct Impl;

  /// @brief Use Start() instead.
  /// @param impl The set-up implementation. Moved in: the tap owns it.
  explicit SystemTap(std::unique_ptr<Impl> impl);

  /// @brief Owns the Core Audio objects. A unique_ptr of a type that is
  /// only declared works, as long as the destructor is defined where Impl
  /// is complete, in system_tap.mm. That is why ~SystemTap() is declared
  /// here and not left to the compiler.
  std::unique_ptr<Impl> impl_;
  /// @brief See sample_rate().
  std::uint32_t sample_rate_ = 0;
  /// @brief See channels().
  std::uint16_t channels_ = 0;
  /// @brief See device_name().
  std::string device_name_;
};

}  // namespace siphon

#endif  // SIPHON_SYSTEM_TAP_H_
