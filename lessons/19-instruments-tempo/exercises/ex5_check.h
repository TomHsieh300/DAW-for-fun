#pragma once
// Exercise 5 - the self-check (main). Included at the end of the starter and of the solution.

#include "ex5_thin_tempo.h"

#include <dawlab/Check.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>

namespace ex5::check {

using minidaw::TempoMap;

// How far a tick of the song moves at most (seconds): at every point of the old list, between them and long
// after the end, asked of Step 1's map.
inline double worstTick(const std::vector<TempoPoint>& before, const std::vector<TempoPoint>& after) {
    TempoMap a, b;
    if (!a.set(48000.0, before) || !b.set(48000.0, after))
        return 1e9;
    double worst = 0.0;
    const auto look = [&](double tick) { worst = std::max(worst, std::fabs(a.secondsAt(tick) - b.secondsAt(tick))); };
    for (size_t i = 0; i < before.size(); ++i) {
        look(static_cast<double>(before[i].tick));
        if (i + 1 < before.size())
            look(0.5 * static_cast<double>(before[i].tick + before[i + 1].tick));
    }
    look(static_cast<double>(before.back().tick) + 5.0e6); // more than an hour later
    return worst;
}

// What the header asks of a list, but for "the fewest".
inline bool sound(const std::vector<TempoPoint>& before, const std::vector<TempoPoint>& after, double tolerance) {
    if (after.empty() || after.size() > before.size() || after.front().tick != 0)
        return false;
    if (after.back().tick != before.back().tick || after.back().bpm != before.back().bpm)
        return false; // the last point stays as it is
    TempoMap a, b;
    if (!a.set(48000.0, before) || !b.set(48000.0, after))
        return false;
    for (const TempoPoint& p : after) {
        if (p.rampToNext)
            return false;
        const bool wasThere = std::any_of(before.begin(), before.end(), [&](const TempoPoint& q) { return q.tick == p.tick; });
        const double tick = static_cast<double>(p.tick);
        if (!wasThere || std::fabs(a.secondsAt(tick) - b.secondsAt(tick)) > 1e-9)
            return false; // a point that stays does not move
    }
    return worstTick(before, after) <= tolerance + 2.0 * kSlack;
}

// The fewest points, by trying every choice of the points that stay (for short lists).
inline int fewestByTrying(const std::vector<TempoPoint>& points, double tolerance) {
    const size_t n = points.size();
    if (n <= 2)
        return static_cast<int>(n);
    const std::vector<double> times = pointTimes(points);
    int fewest = static_cast<int>(n);
    for (unsigned mask = 0; mask < (1u << (n - 2)); ++mask) {
        std::vector<size_t> stays{0};
        for (size_t i = 1; i + 1 < n; ++i)
            if ((mask >> (i - 1)) & 1u)
                stays.push_back(i);
        stays.push_back(n - 1);
        if (static_cast<int>(stays.size()) >= fewest)
            continue;
        std::vector<TempoPoint> thinned;
        for (size_t s = 0; s + 1 < stays.size(); ++s) {
            const size_t i = stays[s], j = stays[s + 1];
            thinned.push_back({points[i].tick, 60.0 * static_cast<double>(points[j].tick - points[i].tick) / (960.0 * (times[j] - times[i])), false});
        }
        thinned.push_back(points.back());
        if (worstTick(points, thinned) <= tolerance + kSlack)
            fewest = static_cast<int>(stays.size());
    }
    return fewest;
}

inline void checkByHand() {
    std::vector<TempoPoint> out;
    // One point, two points: nothing to do.
    DAWLAB_CHECK((thinTempo({{0, 97.5, false}}, 0.0, out) && out.size() == 1 && out[0].bpm == 97.5));
    DAWLAB_CHECK((thinTempo({{0, 100.0, false}, {960, 140.0, false}}, 10.0, out) && out.size() == 2 && out[0].bpm == 100.0 && out[1].bpm == 140.0));
    // The same tempo four times: with no tolerance at all, the first and the last.
    const std::vector<TempoPoint> same{{0, 120.0, false}, {960, 120.0, false}, {1920, 120.0, false}, {3840, 120.0, false}};
    DAWLAB_CHECK((thinTempo(same, 0.0, out) && out.size() == 2 && out[0].tick == 0 && out[1].tick == 3840));
    DAWLAB_CHECK(sound(same, out, 0.0));

    // Beats 0.50, 0.52, 0.48 and 0.50 s long, then 90 BPM. The points play at 0, 0.5, 1.02, 1.5 and 2.0 s.
    const std::vector<TempoPoint> band{{0, 120.0, false}, {960, 60.0 / 0.52, false}, {1920, 125.0, false}, {2880, 120.0, false}, {3840, 90.0, false}};
    // Within 25 ms: one tempo, 120, for the four beats (the third point is 20 ms late), and the last point.
    DAWLAB_CHECK((thinTempo(band, 0.025, out) && out.size() == 2));
    if (out.size() == 2) {
        DAWLAB_CHECK_NEAR(out[0].bpm, 120.0, 1e-9);
        DAWLAB_CHECK((out[1].tick == 3840 && out[1].bpm == 90.0));
        DAWLAB_CHECK_NEAR(worstTick(band, out), 0.02, 1e-9);
    }
    // Within 15 ms: two runs of two beats, 0.51 and 0.49 s a beat - every point 10 ms off or on its time.
    DAWLAB_CHECK((thinTempo(band, 0.015, out) && out.size() == 3));
    if (out.size() == 3) {
        DAWLAB_CHECK((out[0].tick == 0 && out[1].tick == 1920 && out[2].tick == 3840));
        DAWLAB_CHECK_NEAR(out[0].bpm, 60.0 / 0.51, 1e-9);
        DAWLAB_CHECK_NEAR(out[1].bpm, 60.0 / 0.49, 1e-9);
        std::printf("  beats of 0.50, 0.52, 0.48, 0.50 s within 15 ms: %.2f BPM, %.2f from beat 3; the worst tick is %.1f ms off\n", out[0].bpm,
                    out[1].bpm, 1000.0 * worstTick(band, out));
    }
    DAWLAB_CHECK(sound(band, out, 0.015));
    // Within nothing: all five.
    DAWLAB_CHECK((thinTempo(band, 0.0, out) && out.size() == 5 && worstTick(band, out) < 1e-9));

    // Refused, and `out` stays as it was.
    const std::vector<TempoPoint> before = out;
    DAWLAB_CHECK((!thinTempo({}, 0.01, out) && !thinTempo(band, -0.001, out) && !thinTempo(band, std::nan(""), out)));
    DAWLAB_CHECK((!thinTempo({{0, 120.0, true}, {960, 90.0, false}}, 0.01, out)));                        // a ramp
    DAWLAB_CHECK((!thinTempo({{0, 120.0, false}, {960, 90.0, false}, {1920, 100.0, true}}, 0.01, out))); // also on the last
    DAWLAB_CHECK((!thinTempo({{960, 120.0, false}}, 0.01, out) && !thinTempo({{0, 120.0, false}, {0, 90.0, false}}, 0.01, out)));
    DAWLAB_CHECK((!thinTempo({{0, 120.0, false}, {960, 1000.0, false}}, 0.01, out) && out.size() == before.size()));
}

// A band: a tempo that wanders, and beats that are each a little off.
inline std::vector<TempoPoint> randomBand(std::mt19937& rng, int beats, double jitter) {
    std::vector<TempoPoint> points;
    double bpm = std::uniform_real_distribution<double>(70.0, 180.0)(rng);
    for (int k = 0; k < beats; ++k) {
        if (std::uniform_int_distribution<int>(0, 5)(rng) == 0)
            bpm = std::clamp(bpm * std::uniform_real_distribution<double>(0.9, 1.1)(rng), 40.0, 400.0);
        points.push_back({static_cast<std::int64_t>(k) * 960, bpm * (1.0 + std::uniform_real_distribution<double>(-jitter, jitter)(rng)), false});
    }
    return points;
}

inline void checkFewest() {
    std::mt19937 rng(5);
    int refused = 0, unsound = 0, notFewest = 0, thinned = 0;
    const int runs = 400;
    const double tolerances[5] = {0.0, 0.002, 0.005, 0.01, 0.02};
    for (int run = 0; run < runs; ++run) {
        const std::vector<TempoPoint> points = randomBand(rng, std::uniform_int_distribution<int>(1, 12)(rng), 0.03);
        const double tolerance = tolerances[run % 5];
        std::vector<TempoPoint> out;
        if (!thinTempo(points, tolerance, out)) {
            ++refused;
            continue;
        }
        unsound += sound(points, out, tolerance) ? 0 : 1;
        notFewest += static_cast<int>(out.size()) == fewestByTrying(points, tolerance) ? 0 : 1;
        thinned += out.size() < points.size() ? 1 : 0;
    }
    std::printf("  %d random lists of up to 12 points against every choice: %d refused, %d that move a tick too far, %d not the fewest (%d got "
                "shorter)\n",
                runs, refused, unsound, notFewest, thinned);
    DAWLAB_CHECK(refused == 0 && unsound == 0 && notFewest == 0);
    DAWLAB_CHECK(thinned > runs / 4); // the lists are such that there is something to thin
}

inline void checkBand() {
    // 250 beats with each beat up to 1 % off: a point on every beat.
    std::mt19937 rng(55);
    const std::vector<TempoPoint> band = randomBand(rng, 250, 0.01);
    std::vector<TempoPoint> out;
    size_t last = band.size();
    bool fine = true;
    for (double tolerance : {0.0, 0.001, 0.005, 0.02}) {
        fine = thinTempo(band, tolerance, out) && sound(band, out, tolerance) && out.size() <= last && fine;
        if (out.empty())
            break;
        last = out.size();
        // What chapter 19's import does to get under the same number: every k-th point with its own tempo (here
        // with the last point too, so that the two are compared within the song, not an hour after it).
        const size_t every = (band.size() + out.size() - 1) / out.size();
        std::vector<TempoPoint> kth;
        for (size_t i = 0; i + 1 < band.size(); i += every)
            kth.push_back(band[i]);
        kth.push_back(band.back());
        std::printf("  250 beats within %4.1f ms: %3zu points (worst tick %.2f ms off); every %zu. point instead: %.0f ms off\n", 1000.0 * tolerance,
                    out.size(), 1000.0 * worstTick(band, out), every, 1000.0 * worstTick(band, kth));
    }
    DAWLAB_CHECK(fine);
    DAWLAB_CHECK(last < 60); // within 20 ms a band like this needs a point where the tempo changes, and few more
}

} // namespace ex5::check

int main() {
    using namespace ex5::check;
    std::printf("Exercise 5 - thin a tempo list\n");
    checkByHand();
    checkFewest();
    checkBand();
    return dawlab::test::summary("l19_ex5");
}
