#pragma once
// Chapter 19's tempo map (its Step 1), solved: the steps of chapter 20 start from it.
//
// A list of points, each a tick and a tempo; from a point the tempo holds (a jump at the next point) or goes in a
// straight line to the next point's (a ramp). The map is in SECONDS; a sample is seconds x rate.
//   secondsAt(tick)   the time of a tick: 60 / (960 B) s a tick on a jump, 60/960 x ln(B(t) / B0) / k on a ramp
//   tickAt(seconds)   the exact inverse, with its fraction
// Before tick 0 the first point's tempo holds.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace minidaw {

constexpr int kTicksPerQuarter = 960;

struct TempoPoint {
    std::int64_t tick = 0;
    double bpm = 120.0;
    bool rampToNext = false; // the tempo goes in a line to the next point's (ignored on the last point)
};

class TempoMap {
public:
    static constexpr double kMinBpm = 20.0, kMaxBpm = 999.0;

    // False (the map stays as it was) unless: at least one point, the first at tick 0, ticks strictly increasing,
    // every bpm in [kMinBpm, kMaxBpm].
    bool set(double sampleRate, std::vector<TempoPoint> points) {
        if (!(sampleRate > 0.0) || points.empty() || points.front().tick != 0)
            return false;
        for (size_t i = 0; i < points.size(); ++i)
            if (!(points[i].bpm >= kMinBpm && points[i].bpm <= kMaxBpm) || (i > 0 && points[i].tick <= points[i - 1].tick))
                return false;
        sampleRate_ = sampleRate;
        points_ = std::move(points);
        starts_.assign(points_.size(), 0.0);
        for (size_t i = 1; i < points_.size(); ++i)
            starts_[i] = starts_[i - 1] + secondsInto(i - 1, static_cast<double>(points_[i].tick - points_[i - 1].tick));
        return true;
    }
    const std::vector<TempoPoint>& points() const noexcept { return points_; }
    double sampleRate() const noexcept { return sampleRate_; }

    // The index of the point whose segment holds `tick` (0 for a tick before 0).
    size_t segmentOfTick(double tick) const noexcept {
        size_t lo = 0, hi = points_.size();
        while (hi - lo > 1) {
            const size_t mid = (lo + hi) / 2;
            (static_cast<double>(points_[mid].tick) <= tick ? lo : hi) = mid;
        }
        return lo;
    }
    // The slope of segment i in BPM per tick: 0 unless it ramps to a next point.
    double slopeOf(size_t i) const noexcept {
        if (!points_[i].rampToNext || i + 1 >= points_.size())
            return 0.0;
        return (points_[i + 1].bpm - points_[i].bpm) / static_cast<double>(points_[i + 1].tick - points_[i].tick);
    }
    double bpmAt(double tick) const noexcept {
        if (tick < 0.0)
            return points_[0].bpm;
        const size_t i = segmentOfTick(tick);
        return points_[i].bpm + slopeOf(i) * (tick - static_cast<double>(points_[i].tick));
    }
    double secondsAt(double tick) const noexcept {
        if (tick < 0.0)
            return tick * kSecondsPerTickAt1Bpm / points_[0].bpm;
        const size_t i = segmentOfTick(tick);
        return starts_[i] + secondsInto(i, tick - static_cast<double>(points_[i].tick));
    }
    double tickAt(double seconds) const noexcept {
        if (seconds < 0.0)
            return seconds * points_[0].bpm / kSecondsPerTickAt1Bpm;
        const auto after = std::upper_bound(starts_.begin(), starts_.end(), seconds);
        const size_t i = after == starts_.begin() ? 0 : static_cast<size_t>(after - starts_.begin()) - 1;
        const double s = seconds - starts_[i];
        const double k = slopeOf(i);
        const double into = k == 0.0 ? s * points_[i].bpm / kSecondsPerTickAt1Bpm : points_[i].bpm * std::expm1(k * s / kSecondsPerTickAt1Bpm) / k;
        return static_cast<double>(points_[i].tick) + into;
    }

private:
    static constexpr double kSecondsPerTickAt1Bpm = 60.0 / kTicksPerQuarter;

    // The seconds from the start of segment i to `ticks` ticks into it.
    double secondsInto(size_t i, double ticks) const noexcept {
        const double k = slopeOf(i);
        if (k == 0.0)
            return ticks * kSecondsPerTickAt1Bpm / points_[i].bpm;
        return kSecondsPerTickAt1Bpm * std::log1p(k * ticks / points_[i].bpm) / k;
    }

    double sampleRate_ = 48000.0;
    std::vector<TempoPoint> points_{TempoPoint{}};
    std::vector<double> starts_{0.0};
};

} // namespace minidaw
