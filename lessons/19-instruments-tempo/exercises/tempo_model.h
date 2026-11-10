#pragma once
// Exercises 8 and 9 - Exercises 4 and 5, solved, on the engine's TempoPoint: a tempo map from beats, a tempo from
// taps, and the fewest tempo points within a tolerance. (Do Exercises 4 and 5 first: this file gives them away.)
//
// All lists here are jumps (no ramps) of any length; the engine's TempoMap holds 256 points.

#include <dawlab/engine/TempoMap.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace tempomodel {

using dawlab::engine::TempoPoint;

constexpr double kSlack = 1e-9;

// A list a map could be made of, but for its length: the first at tick 0, ticks rising, 20 .. 999 BPM, no ramps.
inline bool isJumpList(const std::vector<TempoPoint>& points) {
    if (points.empty() || points.front().tick != 0)
        return false;
    for (size_t i = 0; i < points.size(); ++i)
        if (!(points[i].bpm >= 20.0 && points[i].bpm <= 999.0) || points[i].rampToNext || (i > 0 && points[i].tick <= points[i - 1].tick))
            return false;
    return true;
}

// When each point plays, in seconds.
inline std::vector<double> pointTimes(const std::vector<TempoPoint>& points) {
    std::vector<double> times(points.size(), 0.0);
    for (size_t i = 1; i < points.size(); ++i)
        times[i] = times[i - 1] + static_cast<double>(points[i].tick - points[i - 1].tick) * 60.0 / (960.0 * points[i - 1].bpm);
    return times;
}

// When a tick (>= 0) plays; `times` are the list's pointTimes().
inline double secondsAt(const std::vector<TempoPoint>& points, const std::vector<double>& times, double tick) {
    size_t lo = 0, hi = points.size();
    while (hi - lo > 1) {
        const size_t mid = (lo + hi) / 2;
        (static_cast<double>(points[mid].tick) <= tick ? lo : hi) = mid;
    }
    return times[lo] + (tick - static_cast<double>(points[lo].tick)) * 60.0 / (960.0 * points[lo].bpm);
}

// ---- Exercise 4 -------------------------------------------------------------------------------------------------
inline bool mapFromBeats(const std::vector<double>& seconds, std::vector<TempoPoint>& out, int maxPoints = 256) {
    if (seconds.size() < 2)
        return false;
    std::vector<TempoPoint> points;
    for (size_t k = 0; k + 1 < seconds.size(); ++k) {
        const double bpm = 60.0 / (seconds[k + 1] - seconds[k]);
        if (!(bpm >= 20.0 && bpm <= 999.0))
            return false;
        if (!points.empty() && std::fabs(points.back().bpm - bpm) <= 1e-9)
            continue;
        if (static_cast<int>(points.size()) == maxPoints)
            return false;
        points.push_back({static_cast<std::int64_t>(k) * 960, bpm, false});
    }
    out = std::move(points);
    return true;
}

inline double tapTempo(const std::vector<double>& seconds) {
    if (seconds.size() < 2)
        return 0.0;
    std::vector<double> intervals;
    for (size_t k = 0; k + 1 < seconds.size(); ++k)
        intervals.push_back(seconds[k + 1] - seconds[k]);
    std::sort(intervals.begin(), intervals.end());
    const size_t n = intervals.size();
    const double median = n % 2 == 1 ? intervals[n / 2] : 0.5 * (intervals[n / 2 - 1] + intervals[n / 2]);
    return median > 0.0 ? 60.0 / median : 0.0;
}

// ---- Exercise 5 -------------------------------------------------------------------------------------------------
inline bool thinTempo(const std::vector<TempoPoint>& points, double toleranceSeconds, std::vector<TempoPoint>& out) {
    if (!(toleranceSeconds >= 0.0) || !isJumpList(points))
        return false;
    const size_t n = points.size();
    const std::vector<double> times = pointTimes(points);
    const auto perTick = [&](size_t i, size_t j) { return (times[j] - times[i]) / static_cast<double>(points[j].tick - points[i].tick); };
    const auto fits = [&](size_t i, size_t j) {
        const double slope = perTick(i, j);
        for (size_t p = i + 1; p < j; ++p)
            if (std::fabs(times[i] + static_cast<double>(points[p].tick - points[i].tick) * slope - times[p]) > toleranceSeconds + kSlack)
                return false;
        return true;
    };
    std::vector<int> fewest(n, 0);
    std::vector<size_t> from(n, 0);
    for (size_t j = 1; j < n; ++j) {
        fewest[j] = fewest[j - 1] + 1;
        from[j] = j - 1;
        for (size_t i = 0; i + 1 < j; ++i)
            if (fewest[i] + 1 < fewest[j] && fits(i, j)) {
                fewest[j] = fewest[i] + 1;
                from[j] = i;
            }
    }
    std::vector<TempoPoint> result{points.back()};
    for (size_t j = n - 1; j > 0; j = from[j])
        result.push_back({points[from[j]].tick, 60.0 / (960.0 * perTick(from[j], j)), false});
    std::reverse(result.begin(), result.end());
    out = std::move(result);
    return true;
}

} // namespace tempomodel
