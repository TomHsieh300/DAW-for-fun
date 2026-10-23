// Step 10 (solution): counting allocations per thread, and rendering with any block sizes.
//
// Build and run:  l14_step10_solution   (ctest, label "solution")
// Your version:   starter/step10.cpp -> l14_step10_starter
//
// C++ lets a program replace the global operator new / delete. These replacements allocate with
// malloc / free as usual and, when the CALLING thread has counting switched on
// (audit::CountScope), count the call. So any allocation inside processBlock() - by the engine
// or by any DSP class it calls, also hidden ones in std::vector or std::function - is caught,
// while the UI thread allocates freely at the same time.

#include "../minidaw.h"

#include <cstdlib>
#include <new>

namespace {
void* countedAlloc(std::size_t n) {
    if (minidaw::audit::countingOnThisThread())
        minidaw::audit::counters().allocations.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(n ? n : 1))
        return p;
    throw std::bad_alloc();
}
void countedFree(void* p) noexcept {
    if (p != nullptr && minidaw::audit::countingOnThisThread())
        minidaw::audit::counters().frees.fetch_add(1, std::memory_order_relaxed);
    std::free(p);
}
} // namespace

void* operator new(std::size_t n) { return countedAlloc(n); }
void* operator new[](std::size_t n) { return countedAlloc(n); }
void operator delete(void* p) noexcept { countedFree(p); }
void operator delete[](void* p) noexcept { countedFree(p); }
void operator delete(void* p, std::size_t) noexcept { countedFree(p); }
void operator delete[](void* p, std::size_t) noexcept { countedFree(p); }

namespace minidaw::audit {

AudioBuffer renderWithBlockSizes(Engine& engine, int total, const std::vector<int>& blockSizes) {
    engine.applyPendingCommandsOffline();
    engine.getTransport().setPosition(0);
    engine.getTransport().play();
    AudioBuffer out(kNumChannels, total);
    std::size_t next = 0;
    for (int pos = 0; pos < total;) {
        const int wanted = blockSizes.empty() ? 512 : std::max(1, blockSizes[next++ % blockSizes.size()]);
        const int n = std::min(wanted, total - pos);
        engine.processBlock(out.toBlock().getSubBlock(pos, n));
        engine.collectGarbage();
        pos += n;
    }
    return out;
}

} // namespace minidaw::audit

#include "../check.h"
