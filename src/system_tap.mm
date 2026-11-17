// SystemTap's implementation. This is an Objective-C++ file (.mm): C++ that
// may also use Objective-C. It needs Objective-C for one class,
// CATapDescription, and for the NSDictionary that describes the aggregate
// device. The rest is Core Audio's plain C API.
//
// It is compiled with ARC (automatic reference counting, -fobjc-arc in
// CMakeLists.txt). ARC frees Objective-C objects for us, much like
// std::shared_ptr frees C++ objects when the last owner goes away. Core
// Audio's C objects (the tap, the device) are not Objective-C objects, so
// ARC doesn't touch them, and Impl's destructor frees them by hand.

#include "system_tap.h"

#import <CoreAudio/AudioHardwareTapping.h>
#import <CoreAudio/CATapDescription.h>
#import <CoreAudio/CoreAudio.h>
#import <Foundation/Foundation.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "ring_buffer.h"

namespace siphon {
namespace {

/// @brief Floats in the scratch buffer that OnInput() uses to interleave
/// audio. Core Audio blocks are usually 512 frames or fewer, so this holds
/// several blocks of stereo. A bigger block is handled in parts.
constexpr std::size_t kScratchSamples = 8192;

/// @brief Turns a Core Audio error code into text.
///
/// Many Core Audio codes are four letters packed into 32 bits ('who?' is
/// kAudioHardwareBadObjectError, for example). Those print as letters,
/// which you can search for in the SDK headers. Other codes print as a
/// number.
/// @param status The error code.
/// @return Text such as "'who?'" or "-50".
std::string StatusText(OSStatus status) {
  const auto code = static_cast<std::uint32_t>(status);
  char letters[4];
  for (int i = 0; i < 4; ++i) {
    letters[i] = static_cast<char>(code >> (24 - 8 * i));
  }
  const bool printable = std::all_of(std::begin(letters), std::end(letters),
                                     [](char c) { return c >= 32 && c < 127; });
  if (printable) {
    return "'" + std::string(letters, 4) + "'";
  }
  return std::to_string(status);
}

/// @brief Reads one property of a Core Audio object.
///
/// Every Core Audio object (the system, a device, a tap) has properties,
/// each named by a four-letter "selector". This reads the global one.
/// @tparam T The property's type. The compiler deduces it from `out`, so a
/// call is just `GetProperty(id, selector, &value)`.
/// @param object The object to ask.
/// @param selector Which property.
/// @param out Where to put the value.
/// @return noErr (0) on success, or an error code.
template <typename T>
OSStatus GetProperty(AudioObjectID object, AudioObjectPropertySelector selector,
                     T* out) {
  const AudioObjectPropertyAddress address = {
      selector, kAudioObjectPropertyScopeGlobal,
      kAudioObjectPropertyElementMain};
  UInt32 size = sizeof(T);
  return AudioObjectGetPropertyData(object, &address, 0, nullptr, &size, out);
}

/// @brief Reads a string property, such as a device's name or UID.
/// @param object The object to ask.
/// @param selector Which property.
/// @return The string, or nil if the property can't be read.
NSString* GetStringProperty(AudioObjectID object,
                            AudioObjectPropertySelector selector) {
  CFStringRef value = nullptr;
  if (GetProperty(object, selector, &value) != noErr || value == nullptr) {
    return nil;
  }
  // Core Audio hands us a CFStringRef that we own (the "Copy" rule of Core
  // Foundation). __bridge_transfer turns it into an NSString and hands that
  // ownership to ARC, which frees it when the last reference goes away.
  return (__bridge_transfer NSString*)value;
}

}  // namespace

/// @brief The Core Audio side of SystemTap. See system_tap.h.
struct SystemTap::Impl {
  /// @brief Stops capturing and frees the Core Audio objects, newest first.
  ///
  /// Start() fills these in one at a time, and it may fail halfway. So each
  /// one is only freed if it was made. This one destructor then cleans up
  /// after a full start and after a failed one.
  ~Impl() {
    if (started) {
      // AudioDeviceStop() waits for a running IO proc call to finish. After
      // it returns, OnInput() never runs again, so `ring` and this Impl are
      // safe to destroy.
      AudioDeviceStop(aggregate_id, io_proc_id);
    }
    if (io_proc_id != nullptr) {
      AudioDeviceDestroyIOProcID(aggregate_id, io_proc_id);
    }
    if (aggregate_id != kAudioObjectUnknown) {
      AudioHardwareDestroyAggregateDevice(aggregate_id);
    }
    if (tap_id != kAudioObjectUnknown) {
      AudioHardwareDestroyProcessTap(tap_id);
    }
  }

  /// @brief Receives one block of captured audio. Runs on Core Audio's
  /// real-time thread, so it must not lock, allocate, or do I/O.
  /// @param input The block. Each AudioBuffer in the list holds either all
  /// channels interleaved, or one channel ("non-interleaved").
  void OnInput(const AudioBufferList* input) {
    // The aggregate may list other inputs before the tap's buffers. The
    // tap's buffers come last, so read only the last `tap_buffers`.
    if (input->mNumberBuffers < tap_buffers) {
      return;
    }
    const UInt32 first = input->mNumberBuffers - tap_buffers;
    const AudioBuffer& front = input->mBuffers[first];

    if (tap_buffers == 1) {
      // Already interleaved: copy the floats as they are.
      const auto* data = static_cast<const float*>(front.mData);
      ring->Write({data, front.mDataByteSize / sizeof(float)});
      return;
    }

    // One buffer per channel. Interleave them (L R L R ...) into `scratch`
    // before they go into the ring, a few frames at a time.
    const std::size_t frames = front.mDataByteSize / sizeof(float);
    const std::size_t frames_per_part = scratch.size() / tap_buffers;
    for (std::size_t start = 0; start < frames; start += frames_per_part) {
      const std::size_t count = std::min(frames_per_part, frames - start);
      for (std::size_t frame = 0; frame < count; ++frame) {
        for (UInt32 channel = 0; channel < tap_buffers; ++channel) {
          const auto* data =
              static_cast<const float*>(input->mBuffers[first + channel].mData);
          scratch[frame * tap_buffers + channel] = data[start + frame];
        }
      }
      ring->Write({scratch.data(), count * tap_buffers});
    }
  }

  /// @brief Where OnInput() puts samples. Not owned; see SystemTap::Start().
  RingBuffer* ring = nullptr;
  /// @brief Space to interleave samples in. Allocated once in Start(), so
  /// the audio thread never allocates.
  std::vector<float> scratch;
  /// @brief How many AudioBuffers the tap gives per block: 1 if
  /// interleaved, else one per channel.
  UInt32 tap_buffers = 1;
  /// @brief The process tap. kAudioObjectUnknown until it is made.
  AudioObjectID tap_id = kAudioObjectUnknown;
  /// @brief The private aggregate device that contains the tap.
  AudioObjectID aggregate_id = kAudioObjectUnknown;
  /// @brief The registered IO proc. Null until it is made.
  AudioDeviceIOProcID io_proc_id = nullptr;
  /// @brief True once AudioDeviceStart() succeeded.
  bool started = false;
};

SystemTap::SystemTap(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

// Defined here, where Impl is a complete type, so unique_ptr knows how to
// delete it. The default is fine: deleting impl_ runs ~Impl().
SystemTap::~SystemTap() = default;

std::unique_ptr<SystemTap> SystemTap::Start(RingBuffer* ring,
                                            std::string* error) {
  // std::make_unique allocates the Impl and gives it one owner. If any step
  // below fails, the early `return nullptr` destroys `impl`, and ~Impl()
  // frees whatever was made so far. No manual cleanup on each error path.
  auto impl = std::make_unique<Impl>();
  impl->ring = ring;

  // 1. Find the output device the sound plays on now: speakers, headphones.
  AudioObjectID output_device = kAudioObjectUnknown;
  OSStatus status = GetProperty(kAudioObjectSystemObject,
                                kAudioHardwarePropertyDefaultOutputDevice,
                                &output_device);
  NSString* output_uid =
      status == noErr
          ? GetStringProperty(output_device, kAudioDevicePropertyDeviceUID)
          : nil;
  if (output_uid == nil) {
    *error = "could not find the default output device";
    return nullptr;
  }
  NSString* output_name =
      GetStringProperty(output_device, kAudioObjectPropertyName);

  // 2. Make a tap of every process's audio, mixed down to stereo. The list
  // of processes to leave out is empty: siphon itself plays nothing.
  // Unmuted: the sound still plays on the output device as normal.
  // Private: other apps can't see the tap.
  CATapDescription* description =
      [[CATapDescription alloc] initStereoGlobalTapButExcludeProcesses:@[]];
  description.name = @"siphon";
  description.privateTap = YES;
  description.muteBehavior = CATapUnmuted;
  status = AudioHardwareCreateProcessTap(description, &impl->tap_id);
  if (status != noErr) {
    *error = "could not create the audio tap (" + StatusText(status) +
             "). siphon needs macOS 14.2 or later.";
    return nullptr;
  }

  // 3. Read the tap's format: sample rate, channels, sample type.
  AudioStreamBasicDescription format = {};
  status = GetProperty(impl->tap_id, kAudioTapPropertyFormat, &format);
  if (status != noErr) {
    *error = "could not read the tap's format (" + StatusText(status) + ")";
    return nullptr;
  }
  const bool is_float = (format.mFormatFlags & kAudioFormatFlagIsFloat) != 0 &&
                        format.mBitsPerChannel == 32;
  if (format.mFormatID != kAudioFormatLinearPCM || !is_float ||
      format.mChannelsPerFrame == 0) {
    *error = "the tap gives a sample format siphon doesn't read";
    return nullptr;
  }
  const bool non_interleaved =
      (format.mFormatFlags & kAudioFormatFlagIsNonInterleaved) != 0;
  impl->tap_buffers = non_interleaved ? format.mChannelsPerFrame : 1;
  impl->scratch.resize(kScratchSamples);

  // 4. Put the tap in a private aggregate device. The output device is the
  // aggregate's clock: the tap's audio comes from it, at its rate.
  // "drift" turns on drift correction between the tap and that clock.
  // The @{...} and @[...] are Objective-C literals for NSDictionary and
  // NSArray. @kAudio...Key turns the C string macro into an NSString.
  NSDictionary* aggregate = @{
    @kAudioAggregateDeviceNameKey : @"siphon",
    @kAudioAggregateDeviceUIDKey : [NSUUID UUID].UUIDString,
    @kAudioAggregateDeviceMainSubDeviceKey : output_uid,
    @kAudioAggregateDeviceIsPrivateKey : @YES,
    @kAudioAggregateDeviceIsStackedKey : @NO,
    @kAudioAggregateDeviceTapAutoStartKey : @YES,
    @kAudioAggregateDeviceSubDeviceListKey :
        @[ @{@kAudioSubDeviceUIDKey : output_uid} ],
    @kAudioAggregateDeviceTapListKey : @[ @{
      @kAudioSubTapDriftCompensationKey : @YES,
      @kAudioSubTapUIDKey : description.UUID.UUIDString,
    } ],
  };
  // __bridge passes the NSDictionary as a CFDictionaryRef without changing
  // who owns it. ARC still owns it, and it lives until the end of Start().
  status = AudioHardwareCreateAggregateDevice(
      (__bridge CFDictionaryRef)aggregate, &impl->aggregate_id);
  if (status != noErr) {
    *error = "could not create the aggregate device (" + StatusText(status) +
             ")";
    return nullptr;
  }

  // 5. Register the IO proc. It is an Objective-C "block", a closure much
  // like a C++ lambda. It captures `raw`, a plain pointer to the Impl, by
  // value. The Impl outlives every call: ~Impl() stops the device first.
  Impl* raw = impl.get();
  status = AudioDeviceCreateIOProcIDWithBlock(
      &impl->io_proc_id, impl->aggregate_id, nullptr,
      ^(const AudioTimeStamp*, const AudioBufferList* input,
        const AudioTimeStamp*, AudioBufferList*, const AudioTimeStamp*) {
        raw->OnInput(input);
      });
  if (status != noErr) {
    *error = "could not register the audio callback (" + StatusText(status) +
             ")";
    return nullptr;
  }

  // 6. Start. From now on OnInput() runs about every 10 ms.
  status = AudioDeviceStart(impl->aggregate_id, impl->io_proc_id);
  if (status != noErr) {
    *error = "could not start capturing (" + StatusText(status) + ")";
    return nullptr;
  }
  impl->started = true;

  // `new` because the constructor is private, and std::make_unique can't
  // call a private constructor. The unique_ptr owns the object right away.
  std::unique_ptr<SystemTap> tap(new SystemTap(std::move(impl)));
  tap->sample_rate_ = static_cast<std::uint32_t>(format.mSampleRate);
  tap->channels_ = static_cast<std::uint16_t>(format.mChannelsPerFrame);
  tap->device_name_ = output_name != nil ? output_name.UTF8String : "unknown";
  return tap;
}

}  // namespace siphon
