#pragma once
// Chapter 19, Step 1 - the self-check (main). Included at the end of the starter and of the solution.

#include "minidaw.h"

#include <dawlab/Check.h>

#include <cstdio>
#include <random>

namespace step01_check {

using namespace minidaw;

// The reference: the time of a tick by numbers alone - Simpson's rule over 1/B(t), segment by segment, in long
// double. It knows nothing of logarithms.
inline long double referenceSeconds(const TempoMap& map, double tick) {
    const auto& p = map.points();
    auto bpm = [&p](size_t i, long double t) -> long double {
        if (!p[i].rampToNext || i + 1 >= p.size())
            return p[i].bpm;
        return p[i].bpm + (p[i + 1].bpm - p[i].bpm) * (t - static_cast<long double>(p[i].tick)) / static_cast<long double>(p[i + 1].tick - p[i].tick);
    };
    auto integral = [&](size_t i, long double from, long double to) {
        const int n = 2000; // even
        const long double h = (to - from) / n;
        long double sum = 1.0L / bpm(i, from) + 1.0L / bpm(i, to);
        for (int k = 1; k < n; ++k)
            sum += (k % 2 == 1 ? 4.0L : 2.0L) / bpm(i, from + k * h);
        return sum * h / 3.0L * 60.0L / kTicksPerQuarter;
    };
    if (tick < 0.0)
        return tick * 60.0L / (kTicksPerQuarter * p[0].bpm);
    long double seconds = 0.0L;
    size_t i = 0;
    for (; i + 1 < p.size() && p[i + 1].tick <= tick; ++i)
        seconds += integral(i, static_cast<long double>(p[i].tick), static_cast<long double>(p[i + 1].tick));
    return seconds + integral(i, static_cast<long double>(p[i].tick), tick);
}

inline bool inverseHolds(const TempoMap& map, std::int64_t sample) {
    const std::int64_t t = map.firstTickAtOrAfter(sample);
    return map.sampleOfTick(t) >= sample && map.sampleOfTick(t - 1) < sample;
}

// A random map: 1 to 30 points, jumps and ramps, 40 to 300 BPM.
inline TempoMap randomMap(std::mt19937& rng, double rate) {
    auto pick = [&rng](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };
    std::vector<TempoPoint> points;
    std::int64_t tick = 0;
    const int n = pick(1, 30);
    for (int i = 0; i < n; ++i) {
        points.push_back({tick, static_cast<double>(pick(400, 3000)) / 10.0, pick(0, 1) == 1});
        tick += pick(1, 20000);
    }
    TempoMap map;
    map.set(rate, points);
    return map;
}

inline void checkJumps() {
    TempoMap map;
    DAWLAB_CHECK(map.set(48000.0, {{0, 120.0}}));
    bool same = true;
    for (std::int64_t t = -3000; t <= 3000; t += 7)
        same = same && map.sampleOfTick(t) == 25 * t;
    DAWLAB_CHECK(same && map.bpmAt(5000) == 120.0 && map.secondsAt(960) == 0.5);
    // A bar at 120, a bar at 60, then 240: the bars start at 0, 2, 6 and 7 s.
    DAWLAB_CHECK(map.set(48000.0, {{0, 120.0}, {3840, 60.0}, {7680, 240.0}}));
    DAWLAB_CHECK(map.starts().size() == 3 && map.starts()[1] == 2.0 && map.starts()[2] == 6.0);
    DAWLAB_CHECK(map.secondsAt(11520) == 7.0 && map.secondsAt(5760) == 4.0 && map.bpmAt(7679) == 60.0 && map.bpmAt(7680) == 240.0);
    DAWLAB_CHECK(map.tickAt(4.0) == 5760.0 && map.tickAt(6.5) == 9600.0 && map.tickAt(-0.5) == -960.0 && map.secondsAt(-960) == -0.5);
    DAWLAB_CHECK(map.sampleOfTick(7681) == 288013 && map.firstTickAtOrAfter(288013) == 7681 && map.firstTickAtOrAfter(288014) == 7682);
    std::printf("  120, 60 and 240 BPM a bar each: bars at %g, %g, %g and %g s\n", map.secondsAt(0), map.secondsAt(3840), map.secondsAt(7680),
                map.secondsAt(11520));
    // The same map at another rate: the same seconds, other samples.
    TempoMap cd;
    DAWLAB_CHECK(cd.set(44100.0, map.points()) && cd.secondsAt(11520) == 7.0 && cd.sampleOfTick(11520) == 308700);
}

inline void checkRamps() {
    // From 60 to 120 BPM over 4 bars: 16 ln 2 = 11.09 s, the tempo 90 half way.
    TempoMap map;
    DAWLAB_CHECK(map.set(48000.0, {{0, 60.0, true}, {4 * 3840, 120.0}}));
    const double ramp = map.secondsAt(4 * 3840);
    DAWLAB_CHECK_NEAR(ramp, 16.0 * std::log(2.0), 1e-12);
    DAWLAB_CHECK(map.bpmAt(2 * 3840) == 90.0 && map.bpmAt(4 * 3840) == 120.0 && map.bpmAt(9 * 3840) == 120.0);
    DAWLAB_CHECK(map.bpmAt(-960) == 60.0 && map.secondsAt(-960) == -1.0); // before 0 the first tempo holds, no ramp
    DAWLAB_CHECK_NEAR(map.secondsAt(5 * 3840), ramp + 2.0, 1e-12); // then 120 holds: 2 s a bar
    DAWLAB_CHECK_NEAR(map.tickAt(ramp), 4 * 3840.0, 1e-9);
    DAWLAB_CHECK_NEAR(map.tickAt(map.secondsAt(1234.5)), 1234.5, 1e-9);
    std::printf("  60 to 120 BPM over 4 bars: %.6f s (16 ln 2 = %.6f), 90 BPM half way\n", ramp, 16.0 * std::log(2.0));
    // A ritardando: down from 140 to 70 over 2 bars, then a jump back up.
    DAWLAB_CHECK(map.set(48000.0, {{0, 140.0, true}, {7680, 70.0}, {9600, 140.0}}));
    DAWLAB_CHECK_NEAR(map.secondsAt(7680), 7680.0 * 60.0 / 960.0 * std::log(2.0) / 70.0, 1e-12);
    DAWLAB_CHECK_NEAR(map.secondsAt(9600) - map.secondsAt(7680), 1920.0 / 960.0 * 60.0 / 70.0, 1e-12);
    // A ramp that hardly ramps: 120 to 120.000001 over 100 000 bars. A plain log(B / B0) / k loses the digits.
    DAWLAB_CHECK(map.set(48000.0, {{0, 120.0, true}, {384000000, 120.000001}}));
    const long double want = referenceSeconds(map, 3840.0 * 1000);
    std::printf("  a ramp of 0.000001 BPM over 100 000 bars, at bar 1000: %.12f s (reference %.12Lf)\n", map.secondsAt(3840.0 * 1000), want);
    DAWLAB_CHECK_NEAR(map.secondsAt(3840.0 * 1000), static_cast<double>(want), 1e-9);
    DAWLAB_CHECK_NEAR(map.tickAt(map.secondsAt(3840.0 * 1000)), 3840.0 * 1000, 1e-3); // and back: std::expm1 keeps them too
}

inline void checkRandomMaps() {
    std::mt19937 rng(19);
    const double rates[] = {44100.0, 48000.0, 96000.0};
    int wrong = 0, notInverse = 0, notBack = 0, notMonotone = 0;
    double worst = 0.0;
    const int maps = 150;
    for (int m = 0; m < maps; ++m) {
        const TempoMap map = randomMap(rng, rates[m % 3]);
        const std::int64_t end = map.points().back().tick + 20000;
        for (int k = 0; k < 40; ++k) {
            const double t = std::uniform_real_distribution<double>(-2000.0, static_cast<double>(end))(rng);
            const double err = std::fabs(map.secondsAt(t) - static_cast<double>(referenceSeconds(map, t)));
            worst = std::max(worst, err);
            wrong += err < 1e-9 ? 0 : 1;
            notBack += std::fabs(map.tickAt(map.secondsAt(t)) - t) < 1e-6 ? 0 : 1;
        }
        for (const TempoPoint& p : map.points())
            for (std::int64_t t = p.tick - 3; t < p.tick + 3; ++t)
                notMonotone += map.sampleOfTick(t + 1) >= map.sampleOfTick(t) ? 0 : 1;
        const std::int64_t last = map.sampleOfTick(end);
        for (int k = 0; k < 300; ++k)
            notInverse += inverseHolds(map, std::uniform_int_distribution<std::int64_t>(-5000, last)(rng)) ? 0 : 1;
        for (const double s : map.starts())
            for (std::int64_t d = -2; d <= 2; ++d)
                notInverse += inverseHolds(map, std::llround(s * map.sampleRate()) + d) ? 0 : 1;
    }
    std::printf("  %d random maps of jumps and ramps: %d times off the reference (worst %.1e s), %d not back from seconds,\n"
                "    %d samples whose first tick is wrong, %d steps backwards\n",
                maps, wrong, worst, notBack, notInverse, notMonotone);
    DAWLAB_CHECK(wrong == 0 && notBack == 0 && notInverse == 0 && notMonotone == 0);
}

inline void checkBlocks() {
    // A song that speeds up and slows down every bar: every event in exactly one block, at an offset inside it.
    std::vector<TempoPoint> points;
    for (int bar = 0; bar < 32; ++bar)
        points.push_back({bar * 3840LL, bar % 2 == 0 ? 90.0 : 150.0, bar % 4 != 3});
    TempoMap map;
    DAWLAB_CHECK(map.set(44100.0, points));
    std::vector<std::int64_t> events;
    for (std::int64_t t = 0; t < 32 * 3840; t += 80)
        events.push_back(t);
    std::mt19937 rng(1);
    size_t next = 0;
    int found = 0, outside = 0;
    const std::int64_t end = map.sampleOfTick(32 * 3840);
    for (std::int64_t s0 = 0; s0 < end;) {
        const int n = std::uniform_int_distribution<int>(1, 900)(rng);
        const std::int64_t from = map.firstTickAtOrAfter(s0), to = map.firstTickAtOrAfter(s0 + n);
        while (next < events.size() && events[next] < from)
            ++next, ++outside;
        for (; next < events.size() && events[next] >= from && events[next] < to; ++next) {
            const std::int64_t offset = map.sampleOfTick(events[next]) - s0;
            outside += offset >= 0 && offset < n ? 0 : 1;
            ++found;
        }
        s0 += n;
    }
    std::printf("  a song that ramps up and down every bar, %zu events in random blocks: %d found, %d outside their block\n", events.size(),
                found, outside);
    DAWLAB_CHECK(found == static_cast<int>(events.size()) && outside == 0);
}

} // namespace step01_check

int main() {
    using namespace step01_check;
    std::printf("Step 1: a tempo map\n");
    checkJumps();
    checkRamps();
    checkRandomMaps();
    checkBlocks();
    return dawlab::test::summary("l19_step01");
}
