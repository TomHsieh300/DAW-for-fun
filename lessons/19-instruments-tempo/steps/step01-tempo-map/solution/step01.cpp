// Chapter 19, Step 1 (solution): a tempo map.
//
// Build and run:  l19_step01_solution   (ctest, label "solution")
// Your version:   starter/step01.cpp -> l19_step01_starter

#include "../minidaw.h"

namespace minidaw {

namespace {

constexpr double kSecondsPerTickAt1Bpm = 60.0 / kTicksPerQuarter; // a tick at 1 BPM

// The slope of segment i in BPM per tick: 0 unless it ramps to a next point.
double slopeOf(const std::vector<TempoPoint>& p, size_t i) noexcept {
    if (!p[i].rampToNext || i + 1 >= p.size())
        return 0.0;
    return (p[i + 1].bpm - p[i].bpm) / static_cast<double>(p[i + 1].tick - p[i].tick);
}

// The seconds from the start of segment i to `ticks` ticks into it.
double secondsInto(const std::vector<TempoPoint>& p, size_t i, double ticks) noexcept {
    const double k = slopeOf(p, i);
    if (k == 0.0)
        return ticks * kSecondsPerTickAt1Bpm / p[i].bpm;
    return kSecondsPerTickAt1Bpm * std::log1p(k * ticks / p[i].bpm) / k; // ln(B(t) / B0) / k, accurate for small k t
}

} // namespace

void TempoMap::prepare() {
    starts_.assign(points_.size(), 0.0);
    for (size_t i = 1; i < points_.size(); ++i)
        starts_[i] = starts_[i - 1] + secondsInto(points_, i - 1, static_cast<double>(points_[i].tick - points_[i - 1].tick));
}

double TempoMap::bpmAt(double tick) const noexcept {
    const size_t i = segmentOfTick(tick);
    if (tick < 0.0)
        return points_[0].bpm;
    return points_[i].bpm + slopeOf(points_, i) * (tick - static_cast<double>(points_[i].tick));
}

double TempoMap::secondsAt(double tick) const noexcept {
    if (tick < 0.0)
        return tick * kSecondsPerTickAt1Bpm / points_[0].bpm; // the first tempo holds before 0
    const size_t i = segmentOfTick(tick);
    return starts_[i] + secondsInto(points_, i, tick - static_cast<double>(points_[i].tick));
}

double TempoMap::tickAt(double seconds) const noexcept {
    if (seconds < 0.0)
        return seconds * points_[0].bpm / kSecondsPerTickAt1Bpm;
    const size_t i = segmentOfSeconds(seconds);
    const double s = seconds - starts_[i];
    const double k = slopeOf(points_, i);
    const double into = k == 0.0 ? s * points_[i].bpm / kSecondsPerTickAt1Bpm
                                 : points_[i].bpm * std::expm1(k * s / kSecondsPerTickAt1Bpm) / k; // (B(t) - B0) / k
    return static_cast<double>(points_[i].tick) + into;
}

std::int64_t TempoMap::sampleOfTick(std::int64_t tick) const noexcept {
    return std::llround(secondsAt(static_cast<double>(tick)) * sampleRate_);
}

std::int64_t TempoMap::firstTickAtOrAfter(std::int64_t sample) const noexcept {
    // tickAt() gives the exact (fractional) tick of the sample; ceil() is the first whole tick at or after it. Its
    // sample rounds to `sample` or later - but rounding may also pull the tick before up to `sample`: step back,
    // and forward in the rare case that floating point put ceil() a tick short.
    std::int64_t t = static_cast<std::int64_t>(std::ceil(tickAt(static_cast<double>(sample) / sampleRate_)));
    while (sampleOfTick(t) < sample)
        ++t;
    while (sampleOfTick(t - 1) >= sample)
        --t;
    return t;
}

} // namespace minidaw

#include "../check.h"
