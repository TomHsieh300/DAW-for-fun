#pragma once
// Exercise 4 - the self-check (main). Included at the end of the starter and of the solution.

#include "ex4_beats_to_map.h"

#include <dawlab/Check.h>

#include <cmath>
#include <cstdio>
#include <random>

namespace ex4::check {

using minidaw::TempoMap;

// How far the beats of the map are from where they were heard, at most (seconds).
inline double worstBeat(const std::vector<TempoPoint>& points, const std::vector<double>& seconds) {
    TempoMap map;
    if (!map.set(48000.0, points))
        return 1e9;
    double worst = 0.0;
    for (size_t k = 0; k < seconds.size(); ++k)
        worst = std::max(worst, std::fabs(map.secondsAt(static_cast<double>(k) * 960.0) - (seconds[k] - seconds[0])));
    return worst;
}

inline void checkBeats() {
    // A steady band: eight beats half a second apart, the first heard at 3.2 s. One point, 120 BPM.
    std::vector<double> steady;
    for (int k = 0; k < 8; ++k)
        steady.push_back(3.2 + 0.5 * k);
    std::vector<TempoPoint> points;
    DAWLAB_CHECK(mapFromBeats(steady, points) && points.size() == 1 && points[0].tick == 0 && std::fabs(points[0].bpm - 120.0) < 1e-9);
    DAWLAB_CHECK(worstBeat(points, steady) < 1e-9);
    // A band that speeds up: 0.6, 0.6, 0.5, 0.5, 0.4 s apart. Three tempos, at beats 1, 3 and 5.
    const std::vector<double> band{10.0, 10.6, 11.2, 11.7, 12.2, 12.6};
    DAWLAB_CHECK(mapFromBeats(band, points) && points.size() == 3);
    if (points.size() == 3) {
        DAWLAB_CHECK(points[0].tick == 0 && points[1].tick == 1920 && points[2].tick == 3840 && !points[0].rampToNext && !points[2].rampToNext);
        DAWLAB_CHECK_NEAR(points[0].bpm, 100.0, 1e-9);
        DAWLAB_CHECK_NEAR(points[1].bpm, 120.0, 1e-9);
        DAWLAB_CHECK_NEAR(points[2].bpm, 150.0, 1e-9);
        std::printf("  beats 0.6, 0.6, 0.5, 0.5, 0.4 s apart: %.0f BPM, %.0f from beat 3, %.0f from beat 5; every beat within %.1e s\n", points[0].bpm,
                    points[1].bpm, points[2].bpm, worstBeat(points, band));
    }
    DAWLAB_CHECK(worstBeat(points, band) < 1e-9);

    // Refused, and `out` stays as it was.
    const std::vector<TempoPoint> before = points;
    DAWLAB_CHECK(!mapFromBeats({}, points) && !mapFromBeats({1.0}, points));
    DAWLAB_CHECK(!mapFromBeats({1.0, 1.5, 1.5, 2.0}, points) && !mapFromBeats({1.0, 2.0, 1.9}, points)); // not one after another
    DAWLAB_CHECK(!mapFromBeats({0.0, 0.05}, points) && !mapFromBeats({0.0, 3.5}, points));                // 1200 BPM, 17 BPM
    DAWLAB_CHECK(!mapFromBeats({0.0, std::nan(""), 1.0}, points));
    DAWLAB_CHECK(mapFromBeats({0.0, 3.0}, points) && mapFromBeats({0.0, 60.0 / 999.0}, points)); // 20 and 999 BPM: the limits
    std::vector<double> wild{0.0}; // 300 beats that all differ: more points than a map holds
    for (int k = 0; k < 300; ++k)
        wild.push_back(wild.back() + 0.4 + 0.001 * k);
    points = before;
    DAWLAB_CHECK(!mapFromBeats(wild, points) && points.size() == before.size());
    wild.resize(257); // 256 intervals: just fits
    DAWLAB_CHECK(mapFromBeats(wild, points) && points.size() == 256 && worstBeat(points, wild) < 1e-9);
}

inline void checkRandomBands() {
    std::mt19937 rng(4);
    int wrong = 0, late = 0, tooMany = 0;
    const int runs = 300;
    for (int run = 0; run < runs; ++run) {
        // A band that holds each tempo for a few beats.
        std::vector<double> seconds{std::uniform_real_distribution<double>(0.0, 100.0)(rng)};
        int tempos = 0;
        while (seconds.size() < 200 && tempos < 40) {
            const double interval = 60.0 / std::uniform_real_distribution<double>(50.0, 220.0)(rng);
            ++tempos;
            for (int i = 0, n = std::uniform_int_distribution<int>(1, 12)(rng); i < n; ++i)
                seconds.push_back(seconds.back() + interval);
        }
        std::vector<TempoPoint> points;
        if (!mapFromBeats(seconds, points)) {
            ++wrong;
            continue;
        }
        late += worstBeat(points, seconds) < 1e-7 ? 0 : 1;
        tooMany += static_cast<int>(points.size()) <= tempos ? 0 : 1; // no point where the tempo goes on
    }
    std::printf("  %d random bands of up to 200 beats: %d refused, %d with a beat off its time, %d with a point where the tempo did not change\n",
                runs, wrong, late, tooMany);
    DAWLAB_CHECK(wrong == 0 && late == 0 && tooMany == 0);
}

inline void checkTaps() {
    DAWLAB_CHECK(tapTempo({}) == 0.0 && tapTempo({1.0}) == 0.0);
    DAWLAB_CHECK_NEAR(tapTempo({0.0, 0.5}), 120.0, 1e-9);
    DAWLAB_CHECK_NEAR(tapTempo({2.0, 2.5, 3.0, 3.5, 4.0}), 120.0, 1e-9);
    // A missed tap (one interval twice as long) and a nervous double tap: the median does not care.
    DAWLAB_CHECK_NEAR(tapTempo({0.0, 0.5, 1.0, 2.0, 2.5, 3.0}), 120.0, 1e-9);
    DAWLAB_CHECK_NEAR(tapTempo({0.0, 0.5, 1.0, 1.05, 1.5, 2.0}), 120.0, 1e-9); // 0.5 0.5 0.05 0.45 0.5: the middle one is 0.5
    // An even number of intervals: the mean of the two in the middle.
    DAWLAB_CHECK_NEAR(tapTempo({0.0, 0.4, 1.0}), 120.0, 1e-9);
    const double mean = 60.0 / ((0.5 + 0.5 + 0.5 + 1.0 + 0.5) / 5.0);
    std::printf("  five taps with one missed: the median says %.1f BPM, the mean would say %.1f\n", tapTempo({0.0, 0.5, 1.0, 2.0, 2.5, 3.0}), mean);
}

} // namespace ex4::check

int main() {
    using namespace ex4::check;
    std::printf("Exercise 4 - a tempo map from beats\n");
    checkBeats();
    checkRandomBands();
    checkTaps();
    return dawlab::test::summary("l19_ex4");
}
