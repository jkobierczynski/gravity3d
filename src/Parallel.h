#pragma once
#include <cstddef>
#include <functional>

// A tiny persistent thread pool with a blocking parallel-for. Created once and
// reused across the whole run (force evaluation happens many times per frame, so
// spawning threads per call would dominate). Work is split into chunks and pulled
// dynamically by the workers plus the calling thread, which balances irregular
// loads (leaves of varying occupancy, boxes with varying interaction lists).
//
// Determinism: every output element is produced entirely by one thread in a fixed
// order, so results are identical to the single-threaded path regardless of the
// thread count. Threading here is a pure speed-up, never a change in the numbers.
namespace parallel {

// Number of parallel lanes (workers + caller). Honors $GRAVITY3D_THREADS, else the
// hardware concurrency.
unsigned threadCount();

// Run fn over sub-ranges covering [0, n). fn(begin,end) is called for each chunk.
// For n at or below minChunk (or a single lane) it simply runs inline on the caller.
void forRange(std::size_t n,
              const std::function<void(std::size_t begin, std::size_t end)>& fn,
              std::size_t minChunk = 1);

} // namespace parallel
