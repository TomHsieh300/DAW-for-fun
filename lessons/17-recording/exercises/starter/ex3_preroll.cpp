// Exercise 3 (starter): write only what a take can use.
//
// Fill in the TODOs. Until then the self-check prints FAIL (expected). The solution is
// solutions/ex3_preroll.cpp. What the functions must do is in ../ex3_preroll.h.

#include "../ex3_preroll.h"

namespace minidaw {

Preroll prerollFor(std::int64_t stampedPosition, const Latencies& latencies, const RecordRange& range, std::int64_t preroll) noexcept {
    // TODO: visibleWindow(); an empty one -> no skip. first = window.from - (stamped - L);
    //       skip = max(first - preroll, 0), and in a loop at most loopEnd - 1 - stamped.
    (void)latencies;
    (void)range;
    (void)preroll;
    return {0, stampedPosition};
}

void StreamGate::arm(std::int64_t skip) noexcept {
    // TODO: remember how many frames are still to drop; nothing dropped yet.
    (void)skip;
}

int StreamGate::drop(int n) noexcept {
    // TODO: as many of the n frames as are still to drop.
    (void)n;
    return 0;
}

} // namespace minidaw

#include "../ex3_check.h"
