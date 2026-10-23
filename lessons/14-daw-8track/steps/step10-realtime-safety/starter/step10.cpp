// Step 10 (starter): counting allocations per thread, and rendering with any block sizes.
//
// Build and run:  l14_step10_starter   (prints [FAIL] until both TODOs are done)
// Reference:      ../solution/step10.cpp, docs/step-10-realtime-safety.md ("Your turn")
//
// The replaced global operator new / delete below already allocate and free; they just do not
// count yet. audit::counters(), audit::countingOnThisThread() and audit::CountScope are in
// ../minidaw.h.

#include "../minidaw.h"

#include <cstdlib>
#include <new>

namespace {
void* countedAlloc(std::size_t n) {
    // TODO 1a: if minidaw::audit::countingOnThisThread() is true, increment
    //          minidaw::audit::counters().allocations.
    if (void* p = std::malloc(n ? n : 1))
        return p;
    throw std::bad_alloc();
}
void countedFree(void* p) noexcept {
    // TODO 1b: if p is not null and counting is on for this thread, increment counters().frees.
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
    // TODO 2: engine.applyPendingCommandsOffline(); seek to 0; play; then fill a stereo buffer of
    //         `total` samples with processBlock() calls whose sizes cycle through blockSizes
    //         (the last block is cut at `total`); collectGarbage() after each block. Return it.
    (void)engine;
    (void)total;
    (void)blockSizes;
    return {};
}

} // namespace minidaw::audit

#include "../check.h"
