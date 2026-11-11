// test_tempo_thinning: the fewest tempo points within a timing tolerance, as the MIDI import uses them
// (app/Source/TempoThinning.h; Exercises 5 and 9 as a part of the app). No JUCE.
//
//   1. By hand: a tempo that only repeats goes at no cost; a list with nothing to drop comes back bit for bit.
//   2. Random short lists: as few points as the best of every possible choice, and no point off by more than
//      the tolerance.
//   3. fitTempoPoints(): a list that fits is not touched; one that does not gets the smallest of 0.5 ms, 1 ms,
//      2 ms ... that fits, and the engine's TempoMap takes the result.
//   4. Next to "every k-th point", which the import did first.

#include "TempoThinning.h"

#include <dawlab/Check.h>

#include <cmath>
#include <cstdio>
#include <random>

using namespace dawlab::app::tempothin;
using dawlab::engine::TempoMap;
using dawlab::engine::TempoPoint;

namespace {

// When each point plays, counted here once more, independently of the header.
std::vector<double> timesOf(const std::vector<TempoPoint>& points) {
    std::vector<double> times(points.size(), 0.0);
    for (size_t i = 1; i < points.size(); ++i)
        times[i] = times[i - 1] + static_cast<double>(points[i].tick - points[i - 1].tick) * 60.0 / (960.0 * points[i - 1].bpm);
    return times;
}

// When a tick plays with `points` (jumps).
double secondsAt(const std::vector<TempoPoint>& points, std::int64_t tick) {
    double seconds = 0.0;
    size_t i = 0;
    for (; i + 1 < points.size() && points[i + 1].tick <= tick; ++i)
        seconds += static_cast<double>(points[i + 1].tick - points[i].tick) * 60.0 / (960.0 * points[i].bpm);
    return seconds + static_cast<double>(tick - points[i].tick) * 60.0 / (960.0 * points[i].bpm);
}

// How far a point of `original` moves at most (seconds) when `now` is the map.
double worstPoint(const std::vector<TempoPoint>& original, const std::vector<TempoPoint>& now) {
    const std::vector<double> times = timesOf(original);
    double worst = 0.0;
    for (size_t i = 0; i < original.size(); ++i)
        worst = std::max(worst, std::fabs(secondsAt(now, original[i].tick) - times[i]));
    return worst;
}

bool same(const std::vector<TempoPoint>& a, const std::vector<TempoPoint>& b) {
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].tick != b[i].tick || a[i].bpm != b[i].bpm || a[i].rampToNext != b[i].rampToNext)
            return false;
    return true;
}

// A band: a point on every beat, a tempo that wanders, every beat up to 1 % off.
std::vector<TempoPoint> band(int beats, unsigned seed) {
    std::mt19937 rng(seed);
    std::vector<TempoPoint> points;
    double bpm = 120.0;
    for (int k = 0; k < beats; ++k) {
        if (std::uniform_int_distribution<int>(0, 5)(rng) == 0)
            bpm = std::clamp(bpm * std::uniform_real_distribution<double>(0.9, 1.1)(rng), 60.0, 240.0);
        points.push_back({static_cast<std::int64_t>(k) * 960, bpm * (1.0 + std::uniform_real_distribution<double>(-0.01, 0.01)(rng)), false});
    }
    return points;
}

// The fewest points of any choice that keeps the first and the last and moves no point by more than the tolerance.
int fewestOfAll(const std::vector<TempoPoint>& points, double tolerance) {
    const size_t n = points.size();
    const std::vector<double> times = timesOf(points);
    int best = static_cast<int>(n);
    for (unsigned mask = 0; mask < (1u << n); ++mask) {
        if ((mask & 1u) == 0 || (mask & (1u << (n - 1))) == 0)
            continue;
        bool fits = true;
        size_t i = 0;
        int kept = 1;
        for (size_t j = 1; j < n && fits; ++j) {
            if ((mask & (1u << j)) == 0)
                continue;
            const double perTick = (times[j] - times[i]) / static_cast<double>(points[j].tick - points[i].tick);
            for (size_t p = i + 1; p < j; ++p)
                fits = fits && std::fabs(times[i] + static_cast<double>(points[p].tick - points[i].tick) * perTick - times[p]) <= tolerance + 1e-9;
            i = j;
            ++kept;
        }
        if (fits)
            best = std::min(best, kept);
    }
    return best;
}

void testByHand() {
    std::vector<TempoPoint> out;
    // Refused: no list, a negative tolerance, a first point not at 0, ticks that go back, a tempo out of range, a ramp.
    DAWLAB_CHECK(!thinTempo({}, 0.0, out));
    DAWLAB_CHECK(!thinTempo({{0, 120.0, false}}, -0.001, out));
    DAWLAB_CHECK(!thinTempo({{960, 120.0, false}}, 0.0, out));
    DAWLAB_CHECK((!thinTempo({{0, 120.0, false}, {960, 100.0, false}, {960, 90.0, false}}, 0.0, out)));
    DAWLAB_CHECK((!thinTempo({{0, 120.0, false}, {960, 1000.0, false}}, 0.0, out)));
    DAWLAB_CHECK((!thinTempo({{0, 120.0, true}, {960, 100.0, false}}, 0.0, out)));
    DAWLAB_CHECK(out.empty());

    // One point, and a list with nothing to drop: bit for bit what went in.
    const std::vector<TempoPoint> one{{0, 97.5, false}};
    DAWLAB_CHECK(thinTempo(one, 0.0, out) && same(out, one));
    const std::vector<TempoPoint> steps{{0, 120.0, false}, {240, 111.3, false}, {480, 103.7, false}, {720, 91.9, false}, {960, 60.0, false}};
    DAWLAB_CHECK(thinTempo(steps, 0.0, out) && same(out, steps));

    // A tempo that only repeats goes, at no cost. The last point always stays.
    std::vector<TempoPoint> steady;
    for (int k = 0; k < 1000; ++k)
        steady.push_back({static_cast<std::int64_t>(k) * 240, k < 600 ? 96.0 : 132.0, false});
    DAWLAB_CHECK(thinTempo(steady, 0.0, out) && out.size() == 3);
    if (out.size() == 3) {
        DAWLAB_CHECK(out[0].tick == 0 && std::fabs(out[0].bpm - 96.0) < 1e-9);
        DAWLAB_CHECK(out[1].tick == 600 * 240 && std::fabs(out[1].bpm - 132.0) < 1e-9 && out[2].tick == 999 * 240 && out[2].bpm == 132.0);
        DAWLAB_CHECK(worstPoint(steady, out) < 1e-9);
    }

    // One beat 10 ms late between two that are on time: it stays within 5 ms, and goes within 10.
    const std::vector<TempoPoint> late{{0, 60.0 / 0.51, false}, {960, 60.0 / 0.49, false}, {1920, 120.0, false}};
    DAWLAB_CHECK(thinTempo(late, 0.005, out) && same(out, late));
    DAWLAB_CHECK(thinTempo(late, 0.0101, out) && out.size() == 2);
    if (out.size() == 2)
        DAWLAB_CHECK(std::fabs(out[0].bpm - 120.0) < 1e-9 && out[1].tick == 1920 && out[1].bpm == 120.0);
}

void testFewest() {
    std::mt19937 rng(19);
    int agreed = 0, within = 0, dropped = 0;
    constexpr int kLists = 400;
    for (int c = 0; c < kLists; ++c) {
        const int n = std::uniform_int_distribution<int>(2, 11)(rng);
        std::vector<TempoPoint> points;
        std::int64_t tick = 0;
        for (int i = 0; i < n; ++i) {
            points.push_back({tick, 120.0 * std::uniform_real_distribution<double>(0.97, 1.03)(rng), false});
            tick += 240 * std::uniform_int_distribution<int>(1, 8)(rng);
        }
        const double tolerance = std::uniform_int_distribution<int>(0, 4)(rng) == 0 ? 0.0 : std::uniform_real_distribution<double>(0.0, 0.01)(rng);
        std::vector<TempoPoint> out;
        if (!thinTempo(points, tolerance, out) || out.empty())
            continue;
        agreed += static_cast<int>(out.size()) == fewestOfAll(points, tolerance) ? 1 : 0;
        within += out.front().tick == 0 && out.back().tick == points.back().tick && out.back().bpm == points.back().bpm && isJumpList(out) &&
                              worstPoint(points, out) <= tolerance + 2e-9
                          ? 1
                          : 0;
        dropped += static_cast<int>(points.size() - out.size());
    }
    DAWLAB_CHECK(agreed == kLists);
    DAWLAB_CHECK(within == kLists);
    DAWLAB_CHECK(dropped > kLists);
    std::printf("  %d of %d random lists: as few points as the best of every choice (%d points dropped)\n", agreed, kLists, dropped);
}

// What the import did first: every k-th point, each with the tempo it had.
std::vector<TempoPoint> everyKth(const std::vector<TempoPoint>& points, int maxPoints) {
    const size_t every = (points.size() + static_cast<size_t>(maxPoints) - 1) / static_cast<size_t>(maxPoints);
    std::vector<TempoPoint> kept;
    for (size_t i = 0; i < points.size(); i += every)
        kept.push_back(points[i]);
    return kept;
}

void testFit() {
    // Refused: no room for a first and a last point, and a list that is not one.
    DAWLAB_CHECK(!fitTempoPoints({{0, 120.0, false}}, 1).ok && !fitTempoPoints({{0, 120.0, false}}, 0).ok);
    DAWLAB_CHECK(!fitTempoPoints({}, 256).ok && !fitTempoPoints({{0, 5.0, false}}, 256).ok);

    // A list that fits: nothing moves, the tolerance is 0, and what could not go is bit for bit the same.
    const std::vector<TempoPoint> small = band(200, 3);
    Fitted f = fitTempoPoints(small);
    DAWLAB_CHECK(f.ok && f.tolerance == 0.0 && same(f.points, small));
    // Again: the same.
    DAWLAB_CHECK(same(fitTempoPoints(f.points).points, f.points));

    const std::vector<TempoPoint> file = band(700, 9);
    f = fitTempoPoints(file, 699); // one point too many: the first tolerance is enough
    DAWLAB_CHECK(f.ok && f.tolerance == 0.0005 && f.points.size() <= 699);
    for (const int maxPoints : {256, 64, 16, 2}) {
        f = fitTempoPoints(file, maxPoints);
        DAWLAB_CHECK(f.ok && static_cast<int>(f.points.size()) <= maxPoints && f.tolerance >= 0.0005);
        if (!f.ok || f.points.empty())
            continue;
        const double steps = std::log2(f.tolerance / 0.0005);
        DAWLAB_CHECK(std::fabs(steps - std::round(steps)) < 1e-9); // 0.5 ms, 1 ms, 2 ms ...
        const double worst = worstPoint(file, f.points);
        DAWLAB_CHECK(isJumpList(f.points) && worst <= f.tolerance + 2e-9);
        TempoMap map;
        DAWLAB_CHECK(maxPoints > TempoMap::kMaxPoints || map.set(f.points));
        // The tolerance before it did not fit.
        std::vector<TempoPoint> tighter;
        DAWLAB_CHECK(thinTempo(file, f.tolerance > 0.0005 ? 0.5 * f.tolerance : 0.0, tighter) && static_cast<int>(tighter.size()) > maxPoints);
        const std::vector<TempoPoint> kth = everyKth(file, maxPoints);
        const double theirs = worstPoint(file, kth);
        if (maxPoints >= 16)
            DAWLAB_CHECK(theirs > 10.0 * worst);
        std::printf("  700 tempos into %3d points: %3zu points, the worst beat %.2f ms off (within %.1f ms); every k-th: %3zu points, %.0f ms off\n",
                    maxPoints, f.points.size(), 1000.0 * worst, 1000.0 * f.tolerance, kth.size(), 1000.0 * theirs);
    }

    // A tempo at the edge of the range stays in it when it is computed again.
    std::vector<TempoPoint> edge;
    for (int k = 0; k < 600; ++k)
        edge.push_back({static_cast<std::int64_t>(k) * 97, k % 7 == 0 ? 999.0 : (k % 5 == 0 ? 20.0 : 998.9), false});
    f = fitTempoPoints(edge, 100);
    TempoMap map;
    DAWLAB_CHECK(f.ok && f.points.size() <= 100 && map.set(f.points));
    // The same tempo at many ticks is one tempo again - but computed from sums, and a sum is rounded: without a
    // clamp some of these come out a hair above 999 or below 20, and the map would refuse them.
    std::mt19937 rng(5);
    int taken = 0;
    constexpr int kLists = 200;
    for (int c = 0; c < kLists; ++c) {
        std::vector<TempoPoint> flat;
        std::int64_t tick = 0;
        for (int k = 0; k < 40; ++k) {
            flat.push_back({tick, c % 2 == 0 ? TempoMap::kMaxBpm : TempoMap::kMinBpm, false});
            tick += std::uniform_int_distribution<int>(1, 5000)(rng);
        }
        f = fitTempoPoints(flat);
        taken += f.ok && f.points.size() == 2 && map.set(f.points) ? 1 : 0;
    }
    DAWLAB_CHECK(taken == kLists);
}

} // namespace

int main() {
    std::printf("l19_test_tempo_thinning: the fewest tempo points within a timing tolerance\n");
    testByHand();
    testFewest();
    testFit();
    return dawlab::test::summary("l19_test_tempo_thinning");
}
