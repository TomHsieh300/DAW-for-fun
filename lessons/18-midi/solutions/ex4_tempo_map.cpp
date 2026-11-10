// Exercise 4 (solution): a tempo map.
//
// Build and run:  l18_ex4_solution   (ctest, label "solution")
// Your version:   exercises/starter/ex4_tempo_map.cpp -> l18_ex4_starter

#include "../exercises/ex4_tempo_map.h"

namespace ex4 {

void TempoMap::prepare() {
    starts_.assign(changes_.size(), 0.0);
    for (size_t i = 1; i < changes_.size(); ++i)
        starts_[i] = starts_[i - 1] + static_cast<double>(changes_[i].tick - changes_[i - 1].tick) * samplesPerTick(i - 1);
}

std::int64_t TempoMap::sampleOfTick(std::int64_t tick) const noexcept {
    // The last change at or before the tick; the first one for a tick before it.
    const auto after = std::upper_bound(changes_.begin(), changes_.end(), tick, [](std::int64_t t, const TempoChange& c) { return t < c.tick; });
    const size_t i = after == changes_.begin() ? 0 : static_cast<size_t>(after - changes_.begin()) - 1;
    return std::llround(starts_[i] + static_cast<double>(tick - changes_[i].tick) * samplesPerTick(i));
}

std::int64_t TempoMap::firstTickAtOrAfter(std::int64_t sample) const noexcept {
    // The change in whose segment the sample lies; then Step 1's ceil() in that segment, and back while the
    // tick before still rounds up to the sample.
    const auto s = static_cast<double>(sample);
    const auto after = std::upper_bound(starts_.begin(), starts_.end(), s);
    const size_t i = after == starts_.begin() ? 0 : static_cast<size_t>(after - starts_.begin()) - 1;
    std::int64_t t = changes_[i].tick + static_cast<std::int64_t>(std::ceil((s - starts_[i]) / samplesPerTick(i)));
    while (sampleOfTick(t - 1) >= sample)
        --t;
    return t;
}

} // namespace ex4

#include "../exercises/ex4_check.h"
