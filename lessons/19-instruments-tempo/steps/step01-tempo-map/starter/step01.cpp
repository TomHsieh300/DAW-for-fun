// Chapter 19, Step 1 (starter): a tempo map.
//
// Fill in the TODOs. Until then the self-check prints FAIL (expected). The solution is
// solution/step01.cpp. What the functions must do is in ../minidaw.h.

#include "../minidaw.h"

namespace minidaw {

void TempoMap::prepare() {
    // TODO: starts_[i] = starts_[i - 1] + the seconds of segment i - 1 (constant, or the ramp's integral).
    starts_.assign(points_.size(), 0.0);
}

double TempoMap::bpmAt(double tick) const noexcept {
    // TODO: the point's tempo, or on a ramp the line to the next point's. Before 0: the first point's.
    (void)tick;
    return points_[0].bpm;
}

double TempoMap::secondsAt(double tick) const noexcept {
    // TODO: segmentOfTick(), then starts_[i] plus the part of the segment: ticks x 60 / (960 x B) on a jump,
    //       60/960 x ln(B(t) / B0) / k on a ramp (std::log1p keeps the digits when k is tiny).
    return tick * 60.0 / (kTicksPerQuarter * points_[0].bpm);
}

double TempoMap::tickAt(double seconds) const noexcept {
    // TODO: segmentOfSeconds(), then the inverse of the segment's formula (std::expm1 on a ramp).
    return seconds * points_[0].bpm * kTicksPerQuarter / 60.0;
}

std::int64_t TempoMap::sampleOfTick(std::int64_t tick) const noexcept {
    // TODO: round(secondsAt(tick) x rate).
    return std::llround(static_cast<double>(tick) * sampleRate_ * 60.0 / (kTicksPerQuarter * points_[0].bpm));
}

std::int64_t TempoMap::firstTickAtOrAfter(std::int64_t sample) const noexcept {
    // TODO: ceil(tickAt(sample / rate)), then make sure: forward while its sample is before `sample`, back while
    //       the tick before still rounds to `sample` or later.
    return static_cast<std::int64_t>(std::ceil(tickAt(static_cast<double>(sample) / sampleRate_)));
}

} // namespace minidaw

#include "../check.h"
