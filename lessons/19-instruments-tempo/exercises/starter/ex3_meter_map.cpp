// Exercise 3 (starter): a meter map.
//
// Fill in the TODOs. Until then the self-check prints FAIL (expected). The solution is
// solutions/ex3_meter_map.cpp. What the functions must do is in ../ex3_meter_map.h.

#include "../ex3_meter_map.h"

namespace ex3 {

void MeterMap::prepare() {
    // TODO: starts_[i] = starts_[i - 1] + (bars of change i - 1) x its ticksPerBar.
    starts_.assign(changes_.size(), 0);
}

std::int64_t MeterMap::tickOfBar(std::int64_t bar) const noexcept {
    // TODO: the last change at or before the bar (std::upper_bound; the first for a bar before 0), then its start
    //       plus the bars since it.
    return bar * ticksPerBar(changes_[0]);
}

Position MeterMap::positionOf(std::int64_t tick) const noexcept {
    // TODO: the change whose ticks hold the tick (starts_), the bars into it - rounded DOWN, also for a tick
    //       before 0 - then the beat and the ticks into the beat.
    (void)tick;
    return {};
}

std::int64_t MeterMap::tickOf(const Position& position) const noexcept {
    // TODO: tickOfBar() + beats x the ticks per beat of the bar's meter + the ticks.
    (void)position;
    return 0;
}

} // namespace ex3

#include "../ex3_check.h"
