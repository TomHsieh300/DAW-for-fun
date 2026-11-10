#pragma once
// Chapter 19, Step 1: a tempo map.
//
// Chapter 18 kept one tempo: a tick lasted samplesPerTick, and sampleOfTick() was one multiplication. A song's
// tempo changes - a jump into a chorus, an accelerando into the last bar - and the notes have to follow, since they
// are in ticks. A TEMPO MAP is a list of points, each a tick and a tempo:
//
//   jump   the tempo holds from its point until the next one
//   ramp   the tempo goes in a straight line, in BPM per tick, from its point to the next one (an accelerando or a
//          ritardando). The last point always holds.
//
// The map turns ticks into time. Time is in SECONDS here, not samples: the same song at 44.1 and at 48 kHz has
// the same map, and a sample is round(seconds x sampleRate).
//
//   secondsAt(t)  the time of tick t: the sum of the segments before t, plus the part of t's own segment.
//                 A tick at tempo B lasts 60 / (B x 960) s. Constant: (t - a) x that. Ramp from B0 at tick a to B1
//                 at tick c: B(t) = B0 + k (t - a), k = (B1 - B0) / (c - a), and the time is the integral
//                     60/960 x ln(B(t) / B0) / k
//                 (as k goes to 0 this becomes the constant formula - use that one when B1 == B0).
//                 Where each point starts is the exact sum of the segments before it, unrounded (Exercise 4 of
//                 chapter 18 showed what rounding them costs).
//   tickAt(s)     the inverse, exact: the tick (with its fraction) that plays at s seconds. Ramp: the formula
//                 above solved for t, B(t) = B0 x exp(k x s' x 960/60), with s' the seconds into the segment.
//   sampleOfTick(t) = round(secondsAt(t) x rate), and firstTickAtOrAfter(sample) its exact inverse, as in
//                 chapter 18's Step 1: the ticks of a block [s0, s1) are [first(s0), first(s1)).
//
// Before tick 0 the first point's tempo holds (a count-in, a negative tick). Everything is a binary search, then
// a few operations: the audio thread calls these for every block.

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

    // ---- provided -----------------------------------------------------------------------------------------
    // False (the map stays as it was) unless: at least one point, the first at tick 0, ticks strictly increasing,
    // every bpm in [kMinBpm, kMaxBpm]. Then prepare().
    bool set(double sampleRate, std::vector<TempoPoint> points) {
        if (!(sampleRate > 0.0) || points.empty() || points.front().tick != 0)
            return false;
        for (size_t i = 0; i < points.size(); ++i)
            if (!(points[i].bpm >= kMinBpm && points[i].bpm <= kMaxBpm) || (i > 0 && points[i].tick <= points[i - 1].tick))
                return false;
        sampleRate_ = sampleRate;
        points_ = std::move(points);
        prepare();
        return true;
    }
    const std::vector<TempoPoint>& points() const noexcept { return points_; }
    double sampleRate() const noexcept { return sampleRate_; }
    // Where each point starts, in seconds (filled by prepare()).
    const std::vector<double>& starts() const noexcept { return starts_; }
    // The index of the point whose segment holds `tick` (0 for a tick before 0).
    size_t segmentOfTick(double tick) const noexcept {
        size_t lo = 0, hi = points_.size();
        while (hi - lo > 1) {
            const size_t mid = (lo + hi) / 2;
            if (static_cast<double>(points_[mid].tick) <= tick)
                lo = mid;
            else
                hi = mid;
        }
        return lo;
    }
    // The index of the point whose segment holds `seconds` (0 before 0).
    size_t segmentOfSeconds(double seconds) const noexcept {
        const auto after = std::upper_bound(starts_.begin(), starts_.end(), seconds);
        return after == starts_.begin() ? 0 : static_cast<size_t>(after - starts_.begin()) - 1;
    }

    // ---- YOUR TURN (starter/step01.cpp) -----------------------------------------------------------------------
    // The tempo at a tick: the point's, or on a ramp the line between it and the next.
    double bpmAt(double tick) const noexcept;
    double secondsAt(double tick) const noexcept;
    double tickAt(double seconds) const noexcept;
    std::int64_t sampleOfTick(std::int64_t tick) const noexcept;
    std::int64_t firstTickAtOrAfter(std::int64_t sample) const noexcept;

private:
    // Fills starts_: 0 for the first point, then where the one before it ends - its whole segment's seconds.
    void prepare();

    double sampleRate_ = 48000.0;
    std::vector<TempoPoint> points_{TempoPoint{}};
    std::vector<double> starts_{0.0};
};

} // namespace minidaw
