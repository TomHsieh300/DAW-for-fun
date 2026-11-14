#pragma once
// TempoMap: the tempo of a song, as points of jumps and ramps (chapter 19, Steps 1 and 2).
//
//   point   a tick and a tempo in BPM. From it the tempo holds until the next point (a jump there), or - rampToNext -
//           goes in a straight line to the next point's tempo. The last point holds; before tick 0 the first one.
//   time    in SECONDS, so the map does not depend on the sample rate. A ramp's time is the integral of 1 / B(t),
//           60/960 x ln(1 + k (t - a) / B0) / k, and its inverse is closed too (Step 1). Where each point starts is
//           the exact sum of the segments before it.
//
// The map is a fixed-size value - at most kMaxPoints points in a std::array, nothing on the heap - so it can be
// copied on the audio thread (Transport does, when the UI hands it a new one) without allocating.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace dawlab::engine {

struct TempoPoint {
    int64_t tick = 0;
    double bpm = 120.0;
    bool rampToNext = false;
};

class TempoMap {
public:
    static constexpr int kMaxPoints = 256;
    static constexpr int kTicksPerBeat = 960;
    static constexpr double kMinBpm = 20.0, kMaxBpm = 999.0;

    TempoMap() noexcept { setConstant(120.0); }
    static TempoMap constant(double bpm) noexcept {
        TempoMap m;
        m.setConstant(bpm);
        return m;
    }

    // False (the map stays as it was) unless: 1 to kMaxPoints points, the first at tick 0, ticks strictly
    // increasing, every bpm in [kMinBpm, kMaxBpm].
    bool set(const std::vector<TempoPoint>& points) noexcept {
        if (points.empty() || points.size() > static_cast<size_t>(kMaxPoints) || points.front().tick != 0)
            return false;
        for (size_t i = 0; i < points.size(); ++i)
            if (!(points[i].bpm >= kMinBpm && points[i].bpm <= kMaxBpm) || (i > 0 && points[i].tick <= points[i - 1].tick))
                return false;
        count_ = static_cast<int>(points.size());
        std::copy(points.begin(), points.end(), points_.begin());
        starts_[0] = 0.0;
        for (int i = 1; i < count_; ++i)
            starts_[static_cast<size_t>(i)] =
                starts_[static_cast<size_t>(i - 1)] + secondsInto(i - 1, static_cast<double>(point(i).tick - point(i - 1).tick));
        return true;
    }
    // One tempo, written into the map itself: no vector, so the constructor, constant() and this never allocate.
    // set() refuses a NaN, so this does too (std::clamp would pass it on); any other bpm, infinity too, clamps.
    void setConstant(double bpm) noexcept {
        if (std::isnan(bpm))
            return;
        points_[0] = {0, std::clamp(bpm, kMinBpm, kMaxBpm), false};
        starts_[0] = 0.0;
        count_ = 1;
    }

    int size() const noexcept { return count_; }
    const TempoPoint& point(int i) const noexcept { return points_[static_cast<size_t>(i)]; }
    std::vector<TempoPoint> points() const { return {points_.begin(), points_.begin() + count_}; }
    bool isConstant() const noexcept { return count_ == 1; }
    bool operator==(const TempoMap& o) const noexcept {
        if (count_ != o.count_)
            return false;
        for (int i = 0; i < count_; ++i)
            if (point(i).tick != o.point(i).tick || point(i).bpm != o.point(i).bpm || point(i).rampToNext != o.point(i).rampToNext)
                return false;
        return true;
    }
    bool operator!=(const TempoMap& o) const noexcept { return !(*this == o); }

    // ---- ticks and time ---------------------------------------------------------------------------------------
    double bpmAt(double tick) const noexcept {
        if (tick < 0.0)
            return point(0).bpm;
        const int i = segmentOfTick(tick);
        return point(i).bpm + slope(i) * (tick - static_cast<double>(point(i).tick));
    }
    double secondsAt(double tick) const noexcept {
        if (tick < 0.0)
            return tick * kSecondsPerTick / point(0).bpm;
        const int i = segmentOfTick(tick);
        return starts_[static_cast<size_t>(i)] + secondsInto(i, tick - static_cast<double>(point(i).tick));
    }
    double tickAt(double seconds) const noexcept {
        if (seconds < 0.0)
            return seconds * point(0).bpm / kSecondsPerTick;
        const auto after = std::upper_bound(starts_.begin(), starts_.begin() + count_, seconds);
        const int i = after == starts_.begin() ? 0 : static_cast<int>(after - starts_.begin()) - 1;
        const double s = seconds - starts_[static_cast<size_t>(i)];
        const double k = slope(i);
        const double into = k == 0.0 ? s * point(i).bpm / kSecondsPerTick : point(i).bpm * std::expm1(k * s / kSecondsPerTick) / k;
        return static_cast<double>(point(i).tick) + into;
    }

    // ---- ticks and samples ------------------------------------------------------------------------------------
    int64_t sampleOfTick(int64_t tick, double sampleRate) const noexcept {
        return std::llround(secondsAt(static_cast<double>(tick)) * sampleRate);
    }
    // The smallest tick whose sample is at least `sample`: the ticks of a block [s0, s1) are
    // [firstTickAtOrAfter(s0), firstTickAtOrAfter(s1)).
    int64_t firstTickAtOrAfter(int64_t sample, double sampleRate) const noexcept {
        int64_t t = static_cast<int64_t>(std::ceil(tickAt(static_cast<double>(sample) / sampleRate)));
        while (sampleOfTick(t, sampleRate) < sample) // a guard against the last digit: log and exp are inverses on paper
            ++t;
        while (sampleOfTick(t - 1, sampleRate) >= sample)
            --t;
        return t;
    }
    // The exact (fractional) tick of a sample.
    double tickAtSample(double sample, double sampleRate) const noexcept { return tickAt(sample / sampleRate); }

private:
    static constexpr double kSecondsPerTick = 60.0 / kTicksPerBeat; // at 1 BPM

    int segmentOfTick(double tick) const noexcept {
        int lo = 0, hi = count_;
        while (hi - lo > 1) {
            const int mid = (lo + hi) / 2;
            (static_cast<double>(point(mid).tick) <= tick ? lo : hi) = mid;
        }
        return lo;
    }
    double slope(int i) const noexcept {
        if (!point(i).rampToNext || i + 1 >= count_)
            return 0.0;
        return (point(i + 1).bpm - point(i).bpm) / static_cast<double>(point(i + 1).tick - point(i).tick);
    }
    double secondsInto(int i, double ticks) const noexcept {
        const double k = slope(i);
        return k == 0.0 ? ticks * kSecondsPerTick / point(i).bpm : kSecondsPerTick * std::log1p(k * ticks / point(i).bpm) / k;
    }

    std::array<TempoPoint, kMaxPoints> points_{};
    std::array<double, kMaxPoints> starts_{};
    int count_ = 0;
};

} // namespace dawlab::engine
