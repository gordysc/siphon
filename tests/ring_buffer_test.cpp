// RingBuffer tests: wrap-around, all-or-nothing writes, and a real
// two-thread run that checks no sample is lost or reordered.

#include "ring_buffer.h"

#include <array>
#include <cstddef>
#include <thread>
#include <vector>

#include "check.h"

namespace siphon {
namespace {

/// @brief The capacity rounds up to a power of two.
void TestCapacityRoundsUp() {
  CHECK(RingBuffer(5).capacity() == 8);
  CHECK(RingBuffer(8).capacity() == 8);
  CHECK(RingBuffer(0).capacity() == 1);
}

/// @brief Samples come out in the order they went in.
void TestReadsInOrder() {
  RingBuffer ring(8);
  const std::array<float, 3> in = {1, 2, 3};
  CHECK(ring.Write(in));
  std::array<float, 8> out{};
  CHECK(ring.Read(out) == 3);
  CHECK(out[0] == 1 && out[1] == 2 && out[2] == 3);
  CHECK(ring.Read(out) == 0);  // Now empty.
}

/// @brief A write that runs past the end of the array continues at the
/// start, and a read gets it back in one piece.
void TestWrapsAround() {
  RingBuffer ring(4);
  std::array<float, 4> out{};
  const std::array<float, 3> first = {1, 2, 3};
  CHECK(ring.Write(first));
  CHECK(ring.Read(std::span(out).first(3)) == 3);
  // The next write starts at slot 3 and wraps: slots 3, 0, 1.
  const std::array<float, 3> second = {4, 5, 6};
  CHECK(ring.Write(second));
  CHECK(ring.Read(out) == 3);
  CHECK(out[0] == 4 && out[1] == 5 && out[2] == 6);
}

/// @brief A write that doesn't fit adds nothing and counts as dropped.
void TestFullWriteDropsWholeBlock() {
  RingBuffer ring(4);
  const std::array<float, 3> block = {1, 2, 3};
  CHECK(ring.Write(block));
  CHECK(!ring.Write(block));  // Only 1 free slot left.
  CHECK(ring.dropped() == 3);
  std::array<float, 4> out{};
  CHECK(ring.Read(out) == 3);  // The first block only; nothing half-added.
}

/// @brief A read stops at the size of `out`, and the rest waits.
void TestShortReadLeavesTheRest() {
  RingBuffer ring(8);
  const std::array<float, 5> in = {1, 2, 3, 4, 5};
  CHECK(ring.Write(in));
  std::array<float, 2> out{};
  CHECK(ring.Read(out) == 2);
  CHECK(ring.Read(out) == 2);
  CHECK(out[0] == 3 && out[1] == 4);
}

/// @brief One thread writes 1, 2, 3, ... while another reads. The reader
/// must see every number, in order. This is the real use: the audio thread
/// and the main thread.
void TestTwoThreads() {
  constexpr int kTotal = 1'000'000;
  constexpr int kBlock = 64;
  RingBuffer ring(1024);

  // The lambda captures `ring` by reference ([&]). That is safe because
  // join() below waits for the thread to finish before `ring` goes away.
  std::thread writer([&] {
    std::array<float, kBlock> block{};
    int next = 0;
    while (next < kTotal) {
      for (int i = 0; i < kBlock; ++i) {
        block[i] = static_cast<float>(next + i);
      }
      // A full ring drops the block, so try again until it fits. The real
      // audio thread never retries; this test only checks the ordering.
      while (!ring.Write(block)) {
        std::this_thread::yield();
      }
      next += kBlock;
    }
  });

  std::vector<float> out(256);
  int expected = 0;
  bool in_order = true;
  while (expected < kTotal) {
    const std::size_t count = ring.Read(out);
    for (std::size_t i = 0; i < count; ++i) {
      in_order = in_order && out[i] == static_cast<float>(expected);
      ++expected;
    }
  }
  writer.join();
  CHECK(in_order);
  CHECK(expected == kTotal);
}

}  // namespace
}  // namespace siphon

/// @brief Runs every test in this file.
/// @return 0 if every check passed, otherwise 1.
int main() {
  using namespace siphon;
  const std::array tests = {
      testing::Test{"capacity rounds up", TestCapacityRoundsUp},
      testing::Test{"reads in order", TestReadsInOrder},
      testing::Test{"wraps around", TestWrapsAround},
      testing::Test{"full write drops whole block",
                    TestFullWriteDropsWholeBlock},
      testing::Test{"short read leaves the rest", TestShortReadLeavesTheRest},
      testing::Test{"two threads", TestTwoThreads},
  };
  return testing::RunTests(tests);
}
