#ifndef SIPHON_RING_BUFFER_H_
#define SIPHON_RING_BUFFER_H_

// A header guard. The first time a .cpp file includes this header,
// SIPHON_RING_BUFFER_H_ is not defined yet, so the contents are compiled and
// the macro gets defined. If the same .cpp file includes the header again
// (often indirectly, through another header), the #ifndef is false and the
// contents are skipped, which avoids "redefinition" errors.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

/// @brief Everything that belongs to the program.
namespace siphon {

/// @brief A queue of samples from one writer thread to one reader thread,
/// with no locks.
///
/// Core Audio calls our capture code on its real-time audio thread. That
/// thread must never wait: if it blocks on a lock, a memory allocation or a
/// disk write, the audio system misses its deadline and you hear a glitch.
/// So the audio thread only copies samples into this buffer, and the main
/// thread takes them out and writes them to disk.
///
/// The buffer is a fixed array used in a circle. Two counters say how many
/// samples were ever written and read. Their difference is how many samples
/// wait in the buffer. Each counter has one owner: only the writer changes
/// `written_`, and only the reader changes `read_`. Each side reads the
/// other's counter, and atomics with acquire/release order make sure the
/// samples are in memory before the counter that announces them.
///
/// Exactly one thread may call Write(), and exactly one (other) thread may
/// call Read(). More than one writer or reader is a data race.
class RingBuffer {
 public:
  /// @brief Makes an empty buffer.
  /// @param min_capacity The fewest samples the buffer must hold. The real
  /// capacity is the next power of two, so a position in the array is
  /// `counter & mask_` instead of a slower `counter % capacity`.
  explicit RingBuffer(std::size_t min_capacity);

  /// @brief Not copyable: the two threads must share one buffer.
  RingBuffer(const RingBuffer&) = delete;
  /// @brief Not copy-assignable, for the same reason.
  RingBuffer& operator=(const RingBuffer&) = delete;

  /// @brief Adds samples. Writer thread only.
  ///
  /// All or nothing: if the samples don't all fit, none are added and they
  /// are counted in dropped(). A part block would cut an audio frame in two
  /// and swap the left and right channels from then on.
  /// @param samples The samples to add.
  /// @return True if they were added, false if they were dropped.
  bool Write(std::span<const float> samples);

  /// @brief Takes out the oldest samples. Reader thread only.
  /// @param out Where to put them. At most `out.size()` samples are taken.
  /// @return How many samples were put in `out`. 0 if the buffer is empty.
  std::size_t Read(std::span<float> out);

  /// @brief How many samples the buffer can hold.
  std::size_t capacity() const { return samples_.size(); }

  /// @brief How many samples Write() has thrown away because the buffer was
  /// full. Safe to call from any thread.
  std::uint64_t dropped() const {
    return dropped_.load(std::memory_order_relaxed);
  }

 private:
  /// @brief The samples. Its size is a power of two.
  std::vector<float> samples_;
  /// @brief capacity() - 1. `counter & mask_` is the counter's position in
  /// `samples_`.
  std::size_t mask_;
  // alignas(64) puts each counter on its own 64-byte cache line. The CPU
  // moves memory between cores one cache line at a time. If both counters
  // shared a line, every write by one thread would force the other core to
  // reload it, even though each thread changes only its own counter. This
  // slowdown is called "false sharing".
  /// @brief How many samples were ever written. Only Write() changes it.
  alignas(64) std::atomic<std::size_t> written_{0};
  /// @brief How many samples were ever read. Only Read() changes it.
  alignas(64) std::atomic<std::size_t> read_{0};
  /// @brief See dropped().
  alignas(64) std::atomic<std::uint64_t> dropped_{0};
};

}  // namespace siphon

#endif  // SIPHON_RING_BUFFER_H_
