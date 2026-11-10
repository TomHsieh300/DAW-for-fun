#pragma once
// Exercise 3 - the self-check (main). Included at the end of the starter and of the solution.

#include "ex3_meter_map.h"

#include <dawlab/Check.h>

#include <cstdio>
#include <random>

namespace ex3::check {

// The reference: bar after bar from the start, every time.
inline std::int64_t referenceTickOfBar(const MeterMap& map, std::int64_t bar) {
    const auto& c = map.changes();
    if (bar < 0)
        return bar * MeterMap::ticksPerBar(c[0]);
    std::int64_t tick = 0;
    size_t i = 0;
    for (std::int64_t b = 0; b < bar; ++b) {
        while (i + 1 < c.size() && c[i + 1].bar <= b)
            ++i;
        tick += MeterMap::ticksPerBar(c[i]);
    }
    return tick;
}

inline void checkOneMeter() {
    MeterMap map;
    DAWLAB_CHECK(map.set({{0, 4, 4}}));
    DAWLAB_CHECK(map.tickOfBar(0) == 0 && map.tickOfBar(3) == 11520 && map.tickOfBar(-1) == -3840);
    DAWLAB_CHECK((map.positionOf(0) == Position{0, 0, 0}) && (map.positionOf(3839) == Position{0, 3, 959}) && (map.positionOf(3840) == Position{1, 0, 0}));
    DAWLAB_CHECK((map.positionOf(-1) == Position{-1, 3, 959}) && (map.positionOf(-3840) == Position{-1, 0, 0}) && (map.positionOf(-3841) == Position{-2, 3, 959}));
    DAWLAB_CHECK(map.tickOf({2, 1, 480}) == 2 * 3840 + 960 + 480 && map.tickOf({-1, 3, 959}) == -1);
    DAWLAB_CHECK(map.set({{0, 6, 8}}));
    DAWLAB_CHECK(map.tickOfBar(1) == 2880 && (map.positionOf(2880 + 480 * 5 + 7) == Position{1, 5, 7}));
    std::printf("  one meter: 4/4 bars of 3840 ticks, 6/8 bars of 2880; a tick before 0 is in bar -1\n");
}

inline void checkChanges() {
    // Two bars of 4/4, one of 2/4, then 6/8, and 7/16 from bar 10.
    MeterMap map;
    DAWLAB_CHECK(map.set({{3, 6, 8}, {0, 4, 4}, {2, 2, 4}, {10, 7, 16}}));
    DAWLAB_CHECK(map.starts().size() == 4 && map.starts()[1] == 7680 && map.starts()[2] == 9600 && map.starts()[3] == 9600 + 7 * 2880);
    DAWLAB_CHECK(map.tickOfBar(2) == 7680 && map.tickOfBar(3) == 9600 && map.tickOfBar(4) == 12480 && map.tickOfBar(11) == 29760 + 1680);
    DAWLAB_CHECK((map.positionOf(7679) == Position{1, 3, 959}) && (map.positionOf(7680) == Position{2, 0, 0}) && (map.positionOf(9599) == Position{2, 1, 959}));
    DAWLAB_CHECK((map.positionOf(9600) == Position{3, 0, 0}) && (map.positionOf(9600 + 480) == Position{3, 1, 0}) && (map.positionOf(29760 + 240 * 6 + 239) == Position{10, 6, 239}));
    DAWLAB_CHECK(map.tickOf({3, 5, 100}) == 9600 + 5 * 480 + 100 && map.tickOf({10, 6, 239}) == 29760 + 1679);
    std::printf("  4/4, 4/4, 2/4, then 6/8, 7/16 from bar 11: bar lines at 0, 3840, 7680, 9600, 12480 ... %lld\n",
                static_cast<long long>(map.tickOfBar(10)));
    // Refused: the map stays.
    DAWLAB_CHECK(!map.set({{1, 4, 4}}) && !map.set({{0, 4, 4}, {0, 3, 4}}) && !map.set({{0, 0, 4}}) && !map.set({{0, 4, 3}}) && !map.set({{0, 4, 64}}) &&
                 !map.set({}));
    DAWLAB_CHECK(map.changes().size() == 4);
}

inline void checkRandomMaps() {
    std::mt19937 rng(3);
    auto pick = [&rng](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };
    int wrongBars = 0, notBack = 0, outOfRange = 0, notOrdered = 0;
    const int maps = 300;
    for (int m = 0; m < maps; ++m) {
        std::vector<MeterChange> changes{{0, pick(1, 12), 1 << pick(1, 4)}};
        std::int64_t bar = 0;
        for (int i = 0, n = pick(0, 20); i < n; ++i) {
            bar += pick(1, 12);
            changes.push_back({bar, pick(1, 12), 1 << pick(1, 4)});
        }
        MeterMap map;
        if (!map.set(changes)) {
            ++wrongBars;
            continue;
        }
        for (std::int64_t b = -2; b <= bar + 5; ++b)
            wrongBars += map.tickOfBar(b) == referenceTickOfBar(map, b) ? 0 : 1;
        const std::int64_t end = referenceTickOfBar(map, bar + 5);
        Position last = map.positionOf(-5001);
        for (int k = 0; k < 400; ++k) {
            const std::int64_t tick = std::uniform_int_distribution<std::int64_t>(-5000, end)(rng);
            const Position p = map.positionOf(tick);
            notBack += map.tickOf(p) == tick ? 0 : 1;
            // In its bar: the bar starts at or before the tick, the next one after it; the beat and tick fit the meter.
            outOfRange += map.tickOfBar(p.bar) <= tick && map.tickOfBar(p.bar + 1) > tick && p.beat >= 0 && p.tick >= 0 ? 0 : 1;
        }
        for (std::int64_t tick = -5000; tick < -4000; ++tick) { // positions grow with ticks
            const Position p = map.positionOf(tick);
            notOrdered += p.bar > last.bar || (p.bar == last.bar && (p.beat > last.beat || (p.beat == last.beat && p.tick > last.tick))) ? 0 : 1;
            last = p;
        }
    }
    std::printf("  %d random maps (1-21 changes, 1/2 to 12/16): %d bar lines wrong, %d positions that do not come back, %d outside their\n"
                "    bar, %d out of order\n",
                maps, wrongBars, notBack, outOfRange, notOrdered);
    DAWLAB_CHECK(wrongBars == 0 && notBack == 0 && outOfRange == 0 && notOrdered == 0);
}

} // namespace ex3::check

int main() {
    using namespace ex3::check;
    std::printf("Exercise 3 - a meter map\n");
    checkOneMeter();
    checkChanges();
    checkRandomMaps();
    return dawlab::test::summary("l19_ex3");
}
