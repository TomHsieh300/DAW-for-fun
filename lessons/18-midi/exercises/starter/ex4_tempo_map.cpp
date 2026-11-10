// Exercise 4 (starter): a tempo map.
//
// Fill in the TODOs. Until then the self-check prints FAIL (expected). The solution is
// solutions/ex4_tempo_map.cpp. What the functions must do is in ../ex4_tempo_map.h.

#include "../ex4_tempo_map.h"

namespace ex4 {

void TempoMap::prepare() {
    // TODO: starts_[i] = starts_[i - 1] + (ticks of segment i - 1) x its samplesPerTick, unrounded.
    starts_.assign(changes_.size(), 0.0);
}

std::int64_t TempoMap::sampleOfTick(std::int64_t tick) const noexcept {
    // TODO: find the change at or before the tick (std::upper_bound), then its start plus the rest of the way.
    return std::llround(static_cast<double>(tick) * samplesPerTick(0));
}

std::int64_t TempoMap::firstTickAtOrAfter(std::int64_t sample) const noexcept {
    // TODO: find the segment the sample lies in (starts_), then as in Step 1: ceil, and back while the tick
    //       before still rounds up to the sample.
    return static_cast<std::int64_t>(std::ceil(static_cast<double>(sample) / samplesPerTick(0)));
}

} // namespace ex4

#include "../ex4_check.h"
