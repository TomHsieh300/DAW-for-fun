#pragma once
// TempoThinning: the fewest tempo points within a timing tolerance (chapter 19; Exercises 5 and 9 as a part of
// the app - this file gives both away). No JUCE: tests/test_tempo_thinning.cpp runs in every build.
//
// A MIDI file may have more tempo events than the engine's map has points (256). Keeping every k-th one, each
// with the tempo it had, loses the tempos between them: the song drifts, by seconds in a long file. Instead:
//
//   thinTempo(points, tolerance, out)
//       The fewest of the points with which no tick of the song plays more than `tolerance` seconds from where
//       it did. A point that stays gets the tempo that brings the next point that stays at exactly its old time,
//       so a point that stays never moves and the errors do not add up; between two neighbours nothing changes,
//       and that tempo is left bit for bit. The first and the last point always stay.
//       The fewest, not just few: fewest[j] = 1 + the smallest fewest[i] over the runs (i, j) that fit.
//   fitTempoPoints(points, maxPoints)
//       thinTempo() with a tolerance of 0 - which only drops tempos that repeat the one before - and, if that is
//       still more than maxPoints points, with the smallest of 0.5 ms, 1 ms, 2 ms ... that fits.
//
// The lists are jumps (a file has no ramps): the first at tick 0, ticks rising, every tempo in the map's range.
// The time this takes grows with the square of the number of points and more: a file with 5000 tempos that all
// differ takes about half a second, one with 20 000 some ten seconds.

#include <dawlab/engine/TempoMap.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace dawlab::app::tempothin {

using engine::TempoMap;
using engine::TempoPoint;

constexpr double kSlack = 1e-9;              // seconds: room for rounding in "within the tolerance"
constexpr double kFirstTolerance = 0.0005;   // seconds: the first one fitTempoPoints() tries after 0

// A list a map could be made of, but for its length - and without ramps.
inline bool isJumpList(const std::vector<TempoPoint>& points) {
    if (points.empty() || points.front().tick != 0)
        return false;
    for (size_t i = 0; i < points.size(); ++i)
        if (!(points[i].bpm >= TempoMap::kMinBpm && points[i].bpm <= TempoMap::kMaxBpm) || points[i].rampToNext ||
            (i > 0 && points[i].tick <= points[i - 1].tick))
            return false;
    return true;
}

// When each point plays, in seconds.
inline std::vector<double> pointTimes(const std::vector<TempoPoint>& points) {
    const double secondsPerTick = 60.0 / static_cast<double>(TempoMap::kTicksPerBeat);
    std::vector<double> times(points.size(), 0.0);
    for (size_t i = 1; i < points.size(); ++i)
        times[i] = times[i - 1] + static_cast<double>(points[i].tick - points[i - 1].tick) * secondsPerTick / points[i - 1].bpm;
    return times;
}

// False (out stays as it was): the tolerance is negative, or the list is not jumps a map could be made of.
inline bool thinTempo(const std::vector<TempoPoint>& points, double toleranceSeconds, std::vector<TempoPoint>& out) {
    if (!(toleranceSeconds >= 0.0) || !isJumpList(points))
        return false;
    const size_t n = points.size();
    const std::vector<double> times = pointTimes(points);
    const auto secondsPerTick = [&](size_t i, size_t j) { return (times[j] - times[i]) / static_cast<double>(points[j].tick - points[i].tick); };
    // With no point between i and j: is every point that goes within the tolerance of its old time?
    const auto fits = [&](size_t i, size_t j) {
        const double slope = secondsPerTick(i, j);
        for (size_t p = i + 1; p < j; ++p)
            if (std::fabs(times[i] + static_cast<double>(points[p].tick - points[i].tick) * slope - times[p]) > toleranceSeconds + kSlack)
                return false;
        return true;
    };

    std::vector<int> fewest(n, 0); // the fewest points before j when j stays
    std::vector<size_t> from(n, 0); // and the point that stays before j then
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
    for (size_t j = n - 1; j > 0; j = from[j]) {
        const size_t i = from[j];
        const double bpm = i + 1 == j ? points[i].bpm : 60.0 / (static_cast<double>(TempoMap::kTicksPerBeat) * secondsPerTick(i, j));
        result.push_back({points[i].tick, std::clamp(bpm, TempoMap::kMinBpm, TempoMap::kMaxBpm), false});
    }
    std::reverse(result.begin(), result.end());
    out = std::move(result);
    return true;
}

struct Fitted {
    bool ok = false;
    std::vector<TempoPoint> points;
    double tolerance = 0.0; // seconds: no tick of the song moves by more than this
};

// Not ok: maxPoints < 2 (a first and a last point always stay), or the list is not jumps a map could be made of.
inline Fitted fitTempoPoints(const std::vector<TempoPoint>& points, int maxPoints = TempoMap::kMaxPoints) {
    Fitted fitted;
    if (maxPoints < 2 || !thinTempo(points, 0.0, fitted.points))
        return fitted;
    while (static_cast<int>(fitted.points.size()) > maxPoints) {
        fitted.tolerance = fitted.tolerance == 0.0 ? kFirstTolerance : 2.0 * fitted.tolerance;
        thinTempo(points, fitted.tolerance, fitted.points);
    }
    fitted.ok = true;
    return fitted;
}

} // namespace dawlab::app::tempothin
