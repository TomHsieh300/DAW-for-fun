#pragma once
// Exercise 3 - The fewest markers for a take.
//
// Step 1's markersForTake() puts a marker at every tempo point under a take and cuts each ramp into EQUAL pieces,
// as many as the chord bound h^2/8 max|f''| asks for at the ramp's slow end. That is never wrong, and it spends
// markers where the ramp is fast and the curve hardly bends. Chapter 19's Exercise 5 found the fewest tempo points
// for a tolerance with a dynamic program; do the same for warp markers.
//
//   fewestMarkers(map, startSample, lengthSamples, toleranceSeconds, gridSamples, out)
//       A take of `lengthSamples` samples was recorded over `map` from song sample `startSample`. The CANDIDATES
//       for a marker are the take's samples 0, gridSamples, 2 gridSamples, ..., the samples of the tempo points
//       under the take (rounded down), and lengthSamples. A candidate marker is exact: (n, map.tickAt((start + n) /
//       rate)).
//       `out` is a list of candidate markers that starts with sample 0 and ends with sample lengthSamples, with
//       which every candidate sample n plays within toleranceSeconds (+ kSlack) of where it was recorded:
//           | map.secondsAt(warp.tickAt(n)) - (start + n) / rate |  <=  tolerance
//       - and of all such lists, one with the fewest markers.
//       Refused (false, `out` untouched): a length or a grid below 1, a start below 0, a tolerance that is
//       negative or not a number.
//
// Between two candidates the error is not checked; with candidates a few milliseconds apart it can only be a
// hair more than at them (the self-check measures how much).
//
// Step 1's warp map and markersForTake() are in ../steps/warp_map.h.

#include "../steps/warp_map.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ex3 {

using namespace minidaw;

constexpr double kSlack = 1e-9; // seconds: room for rounding in "within the tolerance"

// The candidate samples of a take, in order (provided).
inline std::vector<std::int64_t> candidatesOf(const TempoMap& map, std::int64_t startSample, std::int64_t lengthSamples, std::int64_t gridSamples) {
    std::vector<std::int64_t> c;
    for (std::int64_t n = 0; n < lengthSamples; n += gridSamples)
        c.push_back(n);
    const double rate = map.sampleRate();
    for (const TempoPoint& p : map.points()) {
        const std::int64_t n = static_cast<std::int64_t>(std::floor(map.secondsAt(static_cast<double>(p.tick)) * rate)) - startSample;
        if (n > 0 && n < lengthSamples)
            c.push_back(n);
    }
    c.push_back(lengthSamples);
    std::sort(c.begin(), c.end());
    c.erase(std::unique(c.begin(), c.end()), c.end());
    return c;
}

// The exact marker of candidate sample n (provided).
inline WarpMarker exactMarker(const TempoMap& map, std::int64_t startSample, std::int64_t n) {
    return {static_cast<double>(n), map.tickAt(static_cast<double>(startSample + n) / map.sampleRate())};
}

// ---- YOUR TURN (starter/ex3_fewest_markers.cpp) ---------------------------------------------------------------
bool fewestMarkers(const TempoMap& map, std::int64_t startSample, std::int64_t lengthSamples, double toleranceSeconds, std::int64_t gridSamples,
                   std::vector<WarpMarker>& out);

} // namespace ex3
