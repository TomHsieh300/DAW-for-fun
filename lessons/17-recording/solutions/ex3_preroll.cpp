// Exercise 3 (solution): write only what a take can use.
//
// Build and run:  l17_ex3_solution   (ctest, label "solution")
// Your version:   exercises/starter/ex3_preroll.cpp -> l17_ex3_starter

#include "../exercises/ex3_preroll.h"

namespace minidaw {

Preroll prerollFor(std::int64_t stampedPosition, const Latencies& latencies, const RecordRange& range, std::int64_t preroll) noexcept {
    const TimeWindow window = visibleWindow(range, stampedPosition);
    if (window.to <= window.from)
        return {0, stampedPosition};
    const std::int64_t first = window.from - (stampedPosition - latencies.total()); // pass 0's first frame
    std::int64_t skip = std::max<std::int64_t>(first - std::max<std::int64_t>(preroll, 0), 0);
    if (range.loopsFrom(stampedPosition)) // the moved position must still loop
        skip = std::min(skip, range.loopEnd - 1 - stampedPosition);
    return {skip, stampedPosition + skip};
}

void StreamGate::arm(std::int64_t skip) noexcept {
    remaining_ = std::max<std::int64_t>(skip, 0);
    dropped_ = 0;
}

int StreamGate::drop(int n) noexcept {
    const int d = static_cast<int>(std::min<std::int64_t>(remaining_, std::max(n, 0)));
    remaining_ -= d;
    dropped_ += d;
    return d;
}

} // namespace minidaw

#include "../exercises/ex3_check.h"
