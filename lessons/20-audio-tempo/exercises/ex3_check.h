#pragma once
// Exercise 3 - the self-check (main). Included at the end of the starter and of the solution.

#include "ex3_fewest_markers.h"

#include <dawlab/Check.h>

#include <cmath>
#include <cstdio>
#include <random>
#include <tuple>

namespace ex3::check {

inline TempoMap mapOf(std::vector<TempoPoint> points) {
    TempoMap map;
    map.set(48000.0, std::move(points));
    return map;
}

// How far the candidate samples of the take play from where they were recorded with `markers` (seconds), and how
// far any sample does, probed every 37 samples.
struct Off {
    double atCandidates = 0.0, anywhere = 0.0;
};

inline Off offOf(const TempoMap& map, std::int64_t start, std::int64_t length, std::int64_t grid, const std::vector<WarpMarker>& markers) {
    Off off;
    WarpMap warp;
    if (!warp.set(markers))
        return {1e9, 1e9};
    const auto at = [&](std::int64_t n) { return std::fabs(map.secondsAt(warp.tickAt(static_cast<double>(n))) - static_cast<double>(start + n) / map.sampleRate()); };
    for (const std::int64_t n : candidatesOf(map, start, length, grid))
        off.atCandidates = std::max(off.atCandidates, at(n));
    for (std::int64_t n = 0; n <= length; n += 37)
        off.anywhere = std::max(off.anywhere, at(n));
    return off;
}

// A list of exact candidate markers, from the first candidate to the last, in order.
inline bool wellFormed(const TempoMap& map, std::int64_t start, std::int64_t length, std::int64_t grid, const std::vector<WarpMarker>& markers) {
    const std::vector<std::int64_t> c = candidatesOf(map, start, length, grid);
    if (markers.size() < 2 || markers.front().source != 0.0 || markers.back().source != static_cast<double>(length))
        return false;
    size_t k = 0;
    for (const WarpMarker& m : markers) {
        while (k < c.size() && static_cast<double>(c[k]) < m.source)
            ++k;
        if (k == c.size() || static_cast<double>(c[k]) != m.source)
            return false;
        const WarpMarker e = exactMarker(map, start, c[k]);
        if (std::fabs(e.tick - m.tick) > 1e-9)
            return false;
    }
    return true;
}

// The fewest markers of any choice of candidates (every subset that keeps the first and the last).
inline int fewestOfAll(const TempoMap& map, std::int64_t start, std::int64_t length, std::int64_t grid, double tolerance) {
    const std::vector<std::int64_t> c = candidatesOf(map, start, length, grid);
    const size_t n = c.size();
    int best = static_cast<int>(n);
    for (unsigned mask = 0; mask < (1u << n); ++mask) {
        if ((mask & 1u) == 0 || (mask & (1u << (n - 1))) == 0)
            continue;
        std::vector<WarpMarker> m;
        for (size_t k = 0; k < n; ++k)
            if (mask & (1u << k))
                m.push_back(exactMarker(map, start, c[k]));
        if (static_cast<int>(m.size()) < best && offOf(map, start, length, grid, m).atCandidates <= tolerance + kSlack)
            best = static_cast<int>(m.size());
    }
    return best;
}

inline void checkRefused() {
    const TempoMap map = mapOf({{0, 120.0, false}});
    std::vector<WarpMarker> out{{5.0, 5.0}};
    DAWLAB_CHECK(!fewestMarkers(map, 0, 0, 0.001, 480, out) && !fewestMarkers(map, 0, 1000, 0.001, 0, out));
    DAWLAB_CHECK(!fewestMarkers(map, -1, 1000, 0.001, 480, out) && !fewestMarkers(map, 0, 1000, -0.001, 480, out) &&
                 !fewestMarkers(map, 0, 1000, std::nan(""), 480, out));
    DAWLAB_CHECK(out.size() == 1 && out[0].source == 5.0);
}

inline void checkByHand() {
    // One tempo: the time is a straight line of the samples, two markers are exact, whatever the tolerance.
    std::vector<WarpMarker> out;
    const TempoMap steady = mapOf({{0, 97.0, false}});
    DAWLAB_CHECK(fewestMarkers(steady, 12345, 300000, 0.0, 480, out) && out.size() == 2 && wellFormed(steady, 12345, 300000, 480, out));
    // Jumps under the take: a marker at each, no more - and with a tolerance as large as the jump's effect, fewer.
    const TempoMap jumps = mapOf({{0, 120.0, false}, {3840, 90.0, false}, {7680, 150.0, false}});
    DAWLAB_CHECK(fewestMarkers(jumps, 48000, 6 * 48000, 0.0001, 480, out) && out.size() == 4 && wellFormed(jumps, 48000, 6 * 48000, 480, out));
    DAWLAB_CHECK(fewestMarkers(jumps, 48000, 6 * 48000, 1.0, 480, out) && out.size() == 2);
    // A take of one sample: its two ends.
    DAWLAB_CHECK(fewestMarkers(steady, 0, 1, 0.0, 480, out) && out.size() == 2 && out[1].source == 1.0);
}

inline void checkNotGreedy() {
    // Two takes where going as far as a line fits, again and again, needs one marker more than the fewest:
    // a shorter run first makes a longer one possible later.
    std::vector<WarpMarker> out;
    const TempoMap a = mapOf({{0, 61.7, false}, {1526, 166.0, false}, {3782, 86.4, true}, {9195, 108.5, true}, {14956, 227.6, false}});
    DAWLAB_CHECK(fewestMarkers(a, 0, 179388, 0.0084050142666348745, 16308, out) && out.size() == 4 && fewestOfAll(a, 0, 179388, 16308, 0.0084050142666348745) == 4);
    const TempoMap b = mapOf({{0, 147.0, true}, {5121, 192.1, true}, {6841, 99.5, true}, {9927, 86.4, true}});
    DAWLAB_CHECK(fewestMarkers(b, 0, 112680, 0.0076179523997552505, 11268, out) && out.size() == 6 && fewestOfAll(b, 0, 112680, 11268, 0.0076179523997552505) == 6);
}

inline void checkAgainstEveryChoice() {
    std::mt19937 rng(30);
    int agreed = 0, formed = 0, within = 0;
    constexpr int kTakes = 150;
    for (int t = 0; t < kTakes; ++t) {
        std::vector<TempoPoint> points;
        std::int64_t tick = 0;
        for (int i = 0, n = std::uniform_int_distribution<int>(1, 4)(rng); i < n; ++i) {
            points.push_back({tick, std::uniform_int_distribution<int>(600, 1800)(rng) / 10.0, std::uniform_int_distribution<int>(0, 1)(rng) == 1});
            tick += std::uniform_int_distribution<int>(500, 6000)(rng);
        }
        const TempoMap map = mapOf(points);
        const std::int64_t start = std::uniform_int_distribution<std::int64_t>(0, 50000)(rng);
        const std::int64_t grid = std::uniform_int_distribution<std::int64_t>(4000, 30000)(rng);
        const std::int64_t length = grid * std::uniform_int_distribution<std::int64_t>(2, 9)(rng) + std::uniform_int_distribution<std::int64_t>(0, grid - 1)(rng);
        if (candidatesOf(map, start, length, grid).size() > 14)
            continue;
        const double tolerance = std::uniform_int_distribution<int>(0, 3)(rng) == 0 ? 0.0 : std::uniform_real_distribution<double>(0.0, 0.02)(rng);
        std::vector<WarpMarker> out;
        if (!fewestMarkers(map, start, length, tolerance, grid, out))
            continue;
        agreed += static_cast<int>(out.size()) == fewestOfAll(map, start, length, grid, tolerance) ? 1 : 0;
        formed += wellFormed(map, start, length, grid, out) ? 1 : 0;
        within += offOf(map, start, length, grid, out).atCandidates <= tolerance + kSlack ? 1 : 0;
    }
    std::printf("  %d small takes: %d with as few markers as the best of every choice, %d well formed, %d within the tolerance\n", kTakes, agreed, formed, within);
    DAWLAB_CHECK(agreed == kTakes && formed == kTakes && within == kTakes);
}

inline void checkRamps() {
    // A take of a ramp from 60 to 120 BPM over four bars, and one that goes up and down, against Step 1's equal pieces.
    const TempoMap up = mapOf({{0, 60.0, true}, {15360, 120.0, false}});
    const TempoMap wave = mapOf({{0, 70.0, true}, {7680, 160.0, true}, {15360, 80.0, true}, {23040, 140.0, false}});
    bool fine = true;
    for (const auto& [name, map, bars] : {std::make_tuple("60 to 120 BPM", up, 4), std::make_tuple("up and down", wave, 6)}) {
        const std::int64_t length = static_cast<std::int64_t>(map.secondsAt(bars * 3840.0) * 48000.0);
        for (const double tolerance : {0.001, 0.0001}) {
            std::vector<WarpMarker> out;
            const std::vector<WarpMarker> equal = markersForTake(map, 0, length, tolerance);
            constexpr std::int64_t kGrid = 240; // 5 ms
            if (!fewestMarkers(map, 0, length, tolerance, kGrid, out)) {
                fine = false;
                continue;
            }
            const Off off = offOf(map, 0, length, kGrid, out);
            fine = fine && wellFormed(map, 0, length, kGrid, out) && off.atCandidates <= tolerance + kSlack && off.anywhere <= 1.05 * tolerance &&
                   out.size() < equal.size();
            std::printf("  a take of %-13s within %.1f ms: %3zu markers, Step 1's equal pieces %3zu (the worst sample %.4f ms off, %.4f between candidates)\n",
                        name, 1000.0 * tolerance, out.size(), equal.size(), 1000.0 * off.atCandidates, 1000.0 * off.anywhere);
        }
    }
    DAWLAB_CHECK(fine);
}

} // namespace ex3::check

int main() {
    using namespace ex3::check;
    std::printf("Exercise 3 - the fewest markers for a take\n");
    checkRefused();
    checkByHand();
    checkNotGreedy();
    checkAgainstEveryChoice();
    checkRamps();
    return dawlab::test::summary("l20_ex3");
}
