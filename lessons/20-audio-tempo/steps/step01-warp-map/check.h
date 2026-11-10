#pragma once
// Chapter 20, Step 1 - the self-check (main). Included at the end of the starter and of the solution.

#include "minidaw.h"

#include <dawlab/Check.h>

#include <cstdio>
#include <random>

namespace step01_check {

using namespace minidaw;

inline bool near(double a, double b, double eps = 1e-9) {
    return std::fabs(a - b) <= eps * std::max(1.0, std::max(std::fabs(a), std::fabs(b)));
}

inline TempoMap mapOf(double rate, std::vector<TempoPoint> points) {
    TempoMap map;
    map.set(rate, std::move(points));
    return map;
}

// A random tempo map: 1 to 12 points, 50 to 240 BPM, with ramps or without.
inline TempoMap randomMap(std::mt19937& rng, double rate, bool ramps) {
    auto pick = [&rng](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };
    std::vector<TempoPoint> points;
    std::int64_t tick = 0;
    const int n = pick(1, 12);
    for (int i = 0; i < n; ++i) {
        points.push_back({tick, static_cast<double>(pick(500, 2400)) / 10.0, ramps && pick(0, 2) != 0});
        tick += pick(200, 12000);
    }
    return mapOf(rate, points);
}

inline void checkLoop() {
    // A loop of 2 s at 48 kHz that is one bar long, put on bar 2.
    const std::vector<WarpMarker> markers = markersForLoop(96000, 4.0, 3840.0);
    DAWLAB_CHECK(markers.size() == 2);
    DAWLAB_CHECK(markersForLoop(0, 4.0, 0.0).empty() && markersForLoop(96000, 0.0, 0.0).empty() && markersForLoop(-5, 4.0, 0.0).empty());
    WarpMap warp;
    DAWLAB_CHECK((!warp.set({{0.0, 0.0}}) && !warp.set({{0.0, 0.0}, {0.0, 960.0}}) && !warp.set({{0.0, 960.0}, {100.0, 960.0}})));
    if (markers.size() != 2 || !warp.set(markers))
        return;
    DAWLAB_CHECK(markers[0].source == 0.0 && markers[0].tick == 3840.0 && markers[1].source == 96000.0 && markers[1].tick == 7680.0);

    // Both ways, and on beyond the markers.
    DAWLAB_CHECK(near(warp.sourceAt(5760.0), 48000.0) && near(warp.tickAt(24000.0), 4800.0));
    DAWLAB_CHECK(near(warp.sourceAt(1920.0), -48000.0) && near(warp.sourceAt(9600.0), 144000.0));
    DAWLAB_CHECK(near(warp.tickAt(-48000.0), 1920.0) && near(warp.tickAt(144000.0), 9600.0));

    // At the tempo it was made for it plays as it is; at half the tempo at half the speed.
    const TempoMap at120 = mapOf(48000.0, {{0, 120.0, false}});
    const TempoMap at60 = mapOf(48000.0, {{0, 60.0, false}});
    const TempoMap slower = mapOf(48000.0, {{0, 120.0, false}, {3840, 60.0, false}}); // 60 from bar 2
    DAWLAB_CHECK(near(speedAt(warp, at120, 5000.0), 1.0) && near(speedAt(warp, at60, 5000.0), 0.5));
    DAWLAB_CHECK(near(speedAt(warp, slower, 100.0), 1.0) && near(speedAt(warp, slower, 5000.0), 0.5));
    // The loop's third beat: 3 s into the song at 120; with 60 from bar 2 the bar starts at 2 s and a beat is 1 s.
    DAWLAB_CHECK(near(songSecondsOf(warp, at120, 48000.0), 3.0) && near(songSecondsOf(warp, slower, 48000.0), 4.0));
    DAWLAB_CHECK(near(sourceAtSample(warp, at120, 3.0 * 48000.0), 48000.0) && near(sourceAtSample(warp, slower, 4.0 * 48000.0), 48000.0));
    std::printf("  a loop of one bar on bar 2: its third beat at %.1f s at 120 BPM, at %.1f s with 60 BPM from bar 2 (speed %.2f)\n",
                songSecondsOf(warp, at120, 48000.0), songSecondsOf(warp, slower, 48000.0), speedAt(warp, slower, 5000.0));
}

inline void checkBothWays() {
    std::mt19937 rng(20);
    int notBack = 0, notRising = 0;
    constexpr int kMaps = 200;
    for (int c = 0; c < kMaps; ++c) {
        std::vector<WarpMarker> markers;
        double source = std::uniform_real_distribution<double>(0.0, 1000.0)(rng), tick = std::uniform_real_distribution<double>(-5000.0, 5000.0)(rng);
        const int n = std::uniform_int_distribution<int>(2, 20)(rng);
        for (int i = 0; i < n; ++i) {
            markers.push_back({source, tick});
            source += std::uniform_real_distribution<double>(1.0, 50000.0)(rng);
            tick += std::uniform_real_distribution<double>(1.0, 4000.0)(rng);
        }
        WarpMap warp;
        if (!warp.set(markers)) {
            ++notBack;
            continue;
        }
        double before = -1e300;
        for (int k = 0; k < 50; ++k) {
            const double t = markers.front().tick - 2000.0 + (markers.back().tick - markers.front().tick + 4000.0) * k / 49.0;
            const double s = warp.sourceAt(t);
            notBack += std::fabs(warp.tickAt(s) - t) < 1e-6 ? 0 : 1;
            notRising += s > before ? 0 : 1;
            before = s;
        }
        for (const WarpMarker& m : markers) // a marker is on its own line
            notBack += near(warp.sourceAt(m.tick), m.source) && near(warp.tickAt(m.source), m.tick) ? 0 : 1;
    }
    std::printf("  %d random warp maps: %d ticks not back from their source, %d steps backwards\n", kMaps, notBack, notRising);
    DAWLAB_CHECK(notBack == 0 && notRising == 0);
}

// How far a sample of the take is at most from where it was recorded (seconds), when it plays over `map`.
inline double worstOff(const WarpMap& warp, const TempoMap& map, std::int64_t start, std::int64_t length) {
    double worst = 0.0;
    constexpr int kProbes = 4000;
    for (int k = 0; k <= kProbes; ++k) {
        const double n = static_cast<double>(length) * k / kProbes; // a sample of the take
        worst = std::max(worst, std::fabs(songSecondsOf(warp, map, n) - (static_cast<double>(start) + n) / map.sampleRate()));
    }
    return worst;
}

inline void checkTakeOverJumps() {
    DAWLAB_CHECK(markersForTake(mapOf(48000.0, {{0, 120.0, false}}), 0, 0, 0.001).empty());
    DAWLAB_CHECK(markersForTake(mapOf(48000.0, {{0, 120.0, false}}), 0, 48000, 0.0).empty());
    DAWLAB_CHECK(markersForTake(mapOf(48000.0, {{0, 120.0, false}}), -1, 48000, 0.001).empty());

    // 120, then 90 from bar 2, then 150 from bar 3; a take from 1 s to 7 s: both tempo points are under it.
    const TempoMap map = mapOf(48000.0, {{0, 120.0, false}, {3840, 90.0, false}, {7680, 150.0, false}});
    const std::vector<WarpMarker> m = markersForTake(map, 48000, 6 * 48000, 0.001);
    DAWLAB_CHECK(m.size() == 4);
    if (m.size() == 4) {
        // 1 s is beat 3 of bar 1; bar 2 at 2 s; bar 3 at 2 + 4 x 60/90 s.
        DAWLAB_CHECK(m[0].source == 0.0 && near(m[0].tick, 1920.0) && near(m[1].source, 48000.0) && near(m[1].tick, 3840.0));
        DAWLAB_CHECK(near(m[2].source, (1.0 + 4.0 * 60.0 / 90.0) * 48000.0) && near(m[2].tick, 7680.0) && m[3].source == 6.0 * 48000.0);
    }

    std::mt19937 rng(21);
    int wrongCount = 0, off = 0, wrongSpeed = 0, refused = 0;
    double worst = 0.0;
    constexpr int kTakes = 200;
    for (int c = 0; c < kTakes; ++c) {
        const double rate = c % 2 == 0 ? 48000.0 : 44100.0;
        const TempoMap map2 = randomMap(rng, rate, false);
        const std::int64_t start = std::uniform_int_distribution<std::int64_t>(0, 400000)(rng);
        const std::int64_t length = std::uniform_int_distribution<std::int64_t>(1, 3000000)(rng);
        const std::vector<WarpMarker> markers = markersForTake(map2, start, length, 0.001);
        WarpMap warp;
        if (!warp.set(markers)) {
            ++refused;
            continue;
        }
        const double startTick = map2.tickAt(static_cast<double>(start) / rate), endTick = map2.tickAt(static_cast<double>(start + length) / rate);
        size_t under = 0;
        for (const TempoPoint& p : map2.points())
            under += static_cast<double>(p.tick) > startTick && static_cast<double>(p.tick) < endTick ? 1 : 0;
        // The first and the last marker are the take's first sample and the one after its last, exactly.
        wrongCount += markers.size() == under + 2 && markers.front().source == 0.0 && markers.back().source == static_cast<double>(length) ? 0 : 1;
        const double w = worstOff(warp, map2, start, length);
        worst = std::max(worst, w);
        off += w < 1e-9 ? 0 : 1;
        // And the other way: the song's sample `start + n` needs the take's sample n.
        for (int k = 0; k <= 20; ++k) {
            const double n = static_cast<double>(length) * k / 20.0;
            off += std::fabs(sourceAtSample(warp, map2, static_cast<double>(start) + n) - n) < 1e-4 ? 0 : 1;
        }
        // Over the map it was recorded on, the speed is 1 everywhere.
        for (int k = 1; k < 20; ++k)
            wrongSpeed += near(speedAt(warp, map2, startTick + (endTick - startTick) * k / 20.0), 1.0, 1e-7) ? 0 : 1;
    }
    std::printf("  %d takes over maps of jumps: %d refused, %d with a marker too many or too few, %d not where they were recorded\n"
                "    (worst %.1e s), %d places with a speed other than 1\n",
                kTakes, refused, wrongCount, off, worst, wrongSpeed);
    DAWLAB_CHECK(refused == 0 && wrongCount == 0 && off == 0 && wrongSpeed == 0);
}

// The pieces minidaw.h's formula gives a ramp from tick a to b inside segment `i` of the map.
inline int piecesOf(const TempoMap& map, size_t i, double a, double b, double tolerance) {
    const double k = map.slopeOf(i);
    if (k == 0.0)
        return 1;
    const TempoPoint& p = map.points()[i];
    const double slow = std::min(p.bpm + k * (a - static_cast<double>(p.tick)), p.bpm + k * (b - static_cast<double>(p.tick)));
    return std::max(1, static_cast<int>(std::ceil((b - a) * std::sqrt(60.0 / kTicksPerQuarter * std::fabs(k) / (slow * slow) / (8.0 * tolerance)))));
}

inline void checkTakeOverRamps() {
    // 60 to 120 BPM over four bars, a take of the whole ramp (16 ln 2 s).
    const TempoMap ramp = mapOf(48000.0, {{0, 60.0, true}, {15360, 120.0, false}});
    const std::int64_t whole = static_cast<std::int64_t>(ramp.secondsAt(15360.0) * 48000.0);
    size_t before = 0;
    bool fine = true;
    for (const double tolerance : {0.01, 0.001, 0.0001, 0.00001}) {
        const std::vector<WarpMarker> markers = markersForTake(ramp, 0, whole, tolerance);
        WarpMap warp;
        if (!warp.set(markers)) {
            fine = false;
            break;
        }
        const double worst = worstOff(warp, ramp, 0, whole);
        fine = fine && worst <= tolerance && markers.size() > before && markers.size() <= static_cast<size_t>(piecesOf(ramp, 0, 0.0, 15360.0, tolerance)) + 1;
        before = markers.size();
        std::printf("  a take of a ramp from 60 to 120 BPM over 4 bars within %5.2f ms: %3zu markers, the worst sample %.4f ms off\n", 1000.0 * tolerance,
                    markers.size(), 1000.0 * worst);
    }
    DAWLAB_CHECK(fine);

    std::mt19937 rng(22);
    int refused = 0, off = 0, tooMany = 0;
    size_t total = 0;
    constexpr int kTakes = 150;
    constexpr double kTolerance = 0.0005;
    for (int c = 0; c < kTakes; ++c) {
        const TempoMap map = randomMap(rng, 48000.0, true);
        const std::int64_t start = std::uniform_int_distribution<std::int64_t>(0, 400000)(rng);
        const std::int64_t length = std::uniform_int_distribution<std::int64_t>(1, 3000000)(rng);
        const std::vector<WarpMarker> markers = markersForTake(map, start, length, kTolerance);
        WarpMap warp;
        if (!warp.set(markers)) {
            ++refused;
            continue;
        }
        off += worstOff(warp, map, start, length) <= kTolerance ? 0 : 1;
        // No more markers than the formula's pieces.
        const double startTick = map.tickAt(static_cast<double>(start) / 48000.0), endTick = map.tickAt(static_cast<double>(start + length) / 48000.0);
        std::vector<double> edges{startTick};
        for (const TempoPoint& p : map.points())
            if (static_cast<double>(p.tick) > startTick && static_cast<double>(p.tick) < endTick)
                edges.push_back(static_cast<double>(p.tick));
        edges.push_back(endTick);
        size_t most = 1;
        for (size_t e = 0; e + 1 < edges.size(); ++e)
            most += static_cast<size_t>(piecesOf(map, map.segmentOfTick(edges[e]), edges[e], edges[e + 1], kTolerance));
        tooMany += markers.size() <= most ? 0 : 1;
        total += markers.size();
    }
    std::printf("  %d takes over maps of jumps and ramps within %.1f ms: %d refused, %d off by more, %d with more markers than needed (%zu markers in all)\n",
                kTakes, 1000.0 * kTolerance, refused, off, tooMany, total);
    DAWLAB_CHECK(refused == 0 && off == 0 && tooMany == 0);
}

inline void checkNewMap() {
    // The map changes after the take was made. Every tempo twice as fast: every sample of the take is heard at
    // half its time. And wherever the new tempo is r times the old one, the take plays r times as fast.
    std::mt19937 rng(23);
    int notHalf = 0, wrongSpeed = 0, refused = 0;
    constexpr int kTakes = 100;
    for (int c = 0; c < kTakes; ++c) {
        const TempoMap old = randomMap(rng, 48000.0, false);
        std::vector<TempoPoint> twice = old.points();
        for (TempoPoint& p : twice)
            p.bpm *= 2.0;
        const TempoMap fast = mapOf(48000.0, twice);
        const TempoMap other = randomMap(rng, 48000.0, false);
        const std::int64_t start = std::uniform_int_distribution<std::int64_t>(0, 400000)(rng);
        const std::int64_t length = std::uniform_int_distribution<std::int64_t>(1000, 3000000)(rng);
        WarpMap warp;
        if (!warp.set(markersForTake(old, start, length, 0.001))) {
            ++refused;
            continue;
        }
        for (int k = 0; k <= 20; ++k) {
            const double n = static_cast<double>(length) * k / 20.0;
            notHalf += near(songSecondsOf(warp, fast, n), 0.5 * songSecondsOf(warp, old, n), 1e-9) ? 0 : 1;
            const double tick = warp.tickAt(n) + 0.5; // not on a tempo point, where the tempo jumps
            if (tick >= 0.0 && k < 20)
                wrongSpeed += near(speedAt(warp, other, tick), other.bpmAt(tick) / old.bpmAt(tick), 1e-7) ? 0 : 1;
        }
    }
    std::printf("  %d takes, then a new map: %d refused, %d samples not at half their time at twice the tempo,\n"
                "    %d places where the speed is not new tempo / old tempo\n",
                kTakes, refused, notHalf, wrongSpeed);
    DAWLAB_CHECK(refused == 0 && notHalf == 0 && wrongSpeed == 0);
}

} // namespace step01_check

int main() {
    using namespace step01_check;
    std::printf("Step 1: a warp map\n");
    checkLoop();
    checkBothWays();
    checkTakeOverJumps();
    checkTakeOverRamps();
    checkNewMap();
    return dawlab::test::summary("l20_step01");
}
