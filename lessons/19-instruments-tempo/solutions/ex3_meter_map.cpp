// Exercise 3 (solution): a meter map.
//
// Build and run:  l19_ex3_solution   (ctest, label "solution")
// Your version:   exercises/starter/ex3_meter_map.cpp -> l19_ex3_starter

#include "../exercises/ex3_meter_map.h"

namespace ex3 {

namespace {

// Division that rounds down also for a negative tick (C++'s rounds towards zero).
std::int64_t floorDiv(std::int64_t a, std::int64_t b) noexcept {
    return a / b - ((a % b != 0 && (a < 0) != (b < 0)) ? 1 : 0);
}

} // namespace

void MeterMap::prepare() {
    starts_.assign(changes_.size(), 0);
    for (size_t i = 1; i < changes_.size(); ++i)
        starts_[i] = starts_[i - 1] + (changes_[i].bar - changes_[i - 1].bar) * ticksPerBar(changes_[i - 1]);
}

std::int64_t MeterMap::tickOfBar(std::int64_t bar) const noexcept {
    // The last change at or before the bar; the first one for a bar before 0.
    const auto after = std::upper_bound(changes_.begin(), changes_.end(), bar, [](std::int64_t b, const MeterChange& c) { return b < c.bar; });
    const size_t i = after == changes_.begin() ? 0 : static_cast<size_t>(after - changes_.begin()) - 1;
    return starts_[i] + (bar - changes_[i].bar) * ticksPerBar(changes_[i]);
}

Position MeterMap::positionOf(std::int64_t tick) const noexcept {
    const auto after = std::upper_bound(starts_.begin(), starts_.end(), tick);
    const size_t i = after == starts_.begin() ? 0 : static_cast<size_t>(after - starts_.begin()) - 1;
    const MeterChange& c = changes_[i];
    const std::int64_t into = tick - starts_[i]; // negative only before the first change
    const std::int64_t bars = floorDiv(into, ticksPerBar(c));
    const std::int64_t inBar = into - bars * ticksPerBar(c);
    return {c.bar + bars, static_cast<int>(inBar / ticksPerBeat(c)), static_cast<int>(inBar % ticksPerBeat(c))};
}

std::int64_t MeterMap::tickOf(const Position& position) const noexcept {
    const auto after = std::upper_bound(changes_.begin(), changes_.end(), position.bar, [](std::int64_t b, const MeterChange& c) { return b < c.bar; });
    const size_t i = after == changes_.begin() ? 0 : static_cast<size_t>(after - changes_.begin()) - 1;
    return tickOfBar(position.bar) + position.beat * ticksPerBeat(changes_[i]) + position.tick;
}

} // namespace ex3

#include "../exercises/ex3_check.h"
