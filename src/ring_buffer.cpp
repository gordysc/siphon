#include "ring_buffer.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <span>

namespace siphon {

RingBuffer::RingBuffer(std::size_t min_capacity)
    : samples_(std::bit_ceil(std::max<std::size_t>(min_capacity, 1))),
      mask_(samples_.size() - 1) {}

bool RingBuffer::Write(std::span<const float> samples) {
  // Our own counter needs no ordering: no other thread changes it.
  const std::size_t written = written_.load(std::memory_order_relaxed);
  // acquire: pairs with the release in Read(). After this load, the reader
  // has finished with the slots it gave back, so we may write over them.
  const std::size_t read = read_.load(std::memory_order_acquire);
  // Unsigned subtraction stays correct when the counters wrap past the
  // largest size_t, because the result is taken modulo 2^64 as well.
  const std::size_t free_slots = capacity() - (written - read);
  if (samples.size() > free_slots) {
    dropped_.fetch_add(samples.size(), std::memory_order_relaxed);
    return false;
  }

  // The new samples may run past the end of the array. Then the rest goes
  // to the start, so the copy has two parts.
  const std::size_t start = written & mask_;
  const std::size_t first = std::min(samples.size(), capacity() - start);
  std::copy_n(samples.begin(), first, samples_.begin() + start);
  std::copy(samples.begin() + first, samples.end(), samples_.begin());

  // release: the samples copied above become visible to the reader before
  // the new count does. The reader never sees a count for samples that
  // aren't there yet.
  written_.store(written + samples.size(), std::memory_order_release);
  return true;
}

std::size_t RingBuffer::Read(std::span<float> out) {
  const std::size_t read = read_.load(std::memory_order_relaxed);
  // acquire: pairs with the release in Write(), so the samples are visible.
  const std::size_t written = written_.load(std::memory_order_acquire);
  const std::size_t count = std::min(out.size(), written - read);

  const std::size_t start = read & mask_;
  const std::size_t first = std::min(count, capacity() - start);
  std::copy_n(samples_.begin() + start, first, out.begin());
  std::copy_n(samples_.begin(), count - first, out.begin() + first);

  // release: we finish reading the slots before the writer can reuse them.
  read_.store(read + count, std::memory_order_release);
  return count;
}

}  // namespace siphon
