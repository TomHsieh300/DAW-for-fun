#pragma once
// Exercise 4 - the self-check (main). Included at the end of the starter and of the solution.

#include "ex4_tempo_map.h"

#include <dawlab/Check.h>

#include <cstdio>
#include <random>

namespace ex4::check {

// The reference: segment by segment from the start, in long double, every time.
inline std::int64_t reference(const TempoMap& map, std::int64_t tick) {
    const std::vector<TempoChange>& c = map.changes();
    long double start = 0.0L;
    size_t i = 0;
    while (i + 1 < c.size() && c[i + 1].tick <= tick) {
        start += static_cast<long double>(c[i + 1].tick - c[i].tick) * map.samplesPerTick(i);
        ++i;
    }
    return std::llround(static_cast<double>(start + static_cast<long double>(tick - c[i].tick) * map.samplesPerTick(i)));
}

inline bool inverseHolds(const TempoMap& map, std::int64_t sample) {
    const std::int64_t t = map.firstTickAtOrAfter(sample);
    return map.sampleOfTick(t) >= sample && map.sampleOfTick(t - 1) < sample;
}

inline void checkOneTempo() {
    TempoMap map;
    DAWLAB_CHECK(map.set(48000.0, {{0, 120.0}}));
    bool same = true, inverse = true;
    for (std::int64_t t = -2000; t <= 2000; t += 7)
        same = same && map.sampleOfTick(t) == 25 * t;
    for (std::int64_t s = -3000; s <= 3000; ++s)
        inverse = inverse && inverseHolds(map, s);
    DAWLAB_CHECK(same && inverse);
    DAWLAB_CHECK(map.starts().size() == 1 && map.starts()[0] == 0.0);
    DAWLAB_CHECK(map.set(44100.0, {{0, 97.0}}));
    const double spt = 44100.0 * 60.0 / (97.0 * kTicksPerQuarter);
    same = true;
    for (std::int64_t t = 0; t <= 200000; t += 997)
        same = same && map.sampleOfTick(t) == std::llround(static_cast<double>(t) * spt);
    DAWLAB_CHECK(same && map.sampleOfTick(122880) == 3491629);
    std::printf("  one tempo: the same as Step 1 (bar 33 at 97 BPM, 44.1 kHz: sample %lld)\n", static_cast<long long>(map.sampleOfTick(122880)));
}

inline void checkChanges() {
    // 120 BPM for a bar, then 60 for a bar, then 240: 48 000 samples a bar at 120 BPM and 48 kHz.
    TempoMap map;
    DAWLAB_CHECK(map.set(48000.0, {{3840, 60.0}, {0, 120.0}, {7680, 240.0}}));
    DAWLAB_CHECK(map.starts().size() == 3 && map.starts()[1] == 96000.0 && map.starts()[2] == 288000.0);
    DAWLAB_CHECK(map.sampleOfTick(3840) == 96000 && map.sampleOfTick(3841) == 96050 && map.sampleOfTick(7680) == 288000);
    DAWLAB_CHECK(map.sampleOfTick(7681) == 288013 && map.sampleOfTick(-10) == -250); // 288012.5 rounds up
    DAWLAB_CHECK(map.firstTickAtOrAfter(96000) == 3840 && map.firstTickAtOrAfter(96001) == 3841 && map.firstTickAtOrAfter(288000) == 7680);
    DAWLAB_CHECK(map.firstTickAtOrAfter(288013) == 7681 && map.firstTickAtOrAfter(288014) == 7682 && map.firstTickAtOrAfter(-251) == -10);
    std::printf("  120, 60 and 240 BPM a bar each: bars at samples 0, %lld, %lld, %lld\n", static_cast<long long>(map.sampleOfTick(3840)),
                static_cast<long long>(map.sampleOfTick(7680)), static_cast<long long>(map.sampleOfTick(11520)));

    // A later change does not move a note before it; it moves every note after it.
    TempoMap edited;
    DAWLAB_CHECK(edited.set(48000.0, {{0, 120.0}, {3840, 60.0}, {7680, 200.0}}));
    DAWLAB_CHECK(edited.sampleOfTick(7000) == map.sampleOfTick(7000) && edited.sampleOfTick(7680) == map.sampleOfTick(7680));
    DAWLAB_CHECK(edited.sampleOfTick(9600) == 288000 + 1920 * 15 && map.sampleOfTick(9600) == 288000 + std::llround(1920 * 12.5));
}

inline void checkRandomMaps() {
    std::mt19937 rng(4);
    const double rates[] = {44100.0, 48000.0, 96000.0, 22050.0};
    int wrong = 0, notInverse = 0, notMonotone = 0;
    const int maps = 300;
    for (int m = 0; m < maps; ++m) {
        std::vector<TempoChange> changes{{0, std::uniform_real_distribution<double>(20.0, 300.0)(rng)}};
        const int n = std::uniform_int_distribution<int>(0, 40)(rng);
        std::int64_t tick = 0;
        for (int i = 0; i < n; ++i) {
            tick += std::uniform_int_distribution<int>(1, 20000)(rng);
            changes.push_back({tick, std::uniform_real_distribution<double>(20.0, 300.0)(rng)});
        }
        TempoMap map;
        if (!map.set(rates[m % 4], changes)) {
            ++wrong;
            continue;
        }
        for (int k = 0; k < 300; ++k) {
            const std::int64_t t = std::uniform_int_distribution<std::int64_t>(-1000, tick + 30000)(rng);
            wrong += map.sampleOfTick(t) == reference(map, t) ? 0 : 1;
        }
        for (const TempoChange& c : map.changes())
            for (std::int64_t t = c.tick - 3; t < c.tick + 3; ++t)
                notMonotone += map.sampleOfTick(t + 1) >= map.sampleOfTick(t) ? 0 : 1;
        const std::int64_t last = reference(map, tick + 30000);
        for (int k = 0; k < 300; ++k)
            notInverse += inverseHolds(map, std::uniform_int_distribution<std::int64_t>(-5000, last)(rng)) ? 0 : 1;
        for (const double s : map.starts()) // right at and around every change
            for (std::int64_t d = -2; d <= 2; ++d)
                notInverse += inverseHolds(map, std::llround(s) + d) ? 0 : 1;
    }
    std::printf("  %d random maps (1-41 changes, 20-300 BPM): %d ticks wrong, %d samples whose first tick is wrong, %d steps "
                "backwards\n",
                maps, wrong, notInverse, notMonotone);
    DAWLAB_CHECK(wrong == 0 && notInverse == 0 && notMonotone == 0);
}

inline void checkBlocks() {
    // Every event of a song with a ritardando into each bar falls into exactly one block, at an offset inside it.
    std::vector<TempoChange> changes;
    for (int bar = 0; bar < 64; ++bar)
        for (int beat = 0; beat < 4; ++beat)
            changes.push_back({(bar * 4 + beat) * kTicksPerQuarter, 140.0 - 7.5 * beat});
    TempoMap map;
    DAWLAB_CHECK(map.set(44100.0, changes));
    std::vector<std::int64_t> events;
    for (std::int64_t t = 0; t < 64 * 4 * kTicksPerQuarter; t += 120)
        events.push_back(t);
    std::mt19937 rng(9);
    size_t next = 0;
    int found = 0, outside = 0;
    const std::int64_t end = map.sampleOfTick(64 * 4 * kTicksPerQuarter);
    for (std::int64_t s0 = 0; s0 < end;) {
        const int n = std::uniform_int_distribution<int>(1, 700)(rng);
        const std::int64_t from = map.firstTickAtOrAfter(s0), to = map.firstTickAtOrAfter(s0 + n);
        while (next < events.size() && events[next] < from)
            ++next, ++outside; // skipped: in no block
        for (; next < events.size() && events[next] >= from && events[next] < to; ++next) {
            const std::int64_t offset = map.sampleOfTick(events[next]) - s0;
            outside += offset >= 0 && offset < n ? 0 : 1;
            ++found;
        }
        s0 += n;
    }
    std::printf("  a ritardando into every beat, %d events in random blocks: %d found, %d outside their block\n",
                static_cast<int>(events.size()), found, outside);
    DAWLAB_CHECK(found == static_cast<int>(events.size()) && outside == 0);
}

inline void checkNoDrift() {
    // 2000 changes, 1001 ticks apart, between 97 and 131 BPM at 44.1 kHz: no segment is a whole number of samples.
    std::vector<TempoChange> changes;
    for (int i = 0; i < 2000; ++i)
        changes.push_back({i * 1001LL, i % 2 == 0 ? 97.0 : 131.0});
    TempoMap map;
    DAWLAB_CHECK(map.set(44100.0, changes));
    const std::int64_t last = 2000LL * 1001;
    const std::int64_t got = map.sampleOfTick(last), want = reference(map, last);
    double rounded = 0.0; // what rounding every segment start would give
    for (size_t i = 1; i < map.changes().size(); ++i)
        rounded = static_cast<double>(std::llround(rounded + 1001.0 * map.samplesPerTick(i - 1)));
    rounded = static_cast<double>(std::llround(rounded + 1001.0 * map.samplesPerTick(map.changes().size() - 1)));
    std::printf("  2000 changes later: sample %lld, exactly %lld; rounding every change would give %.0f\n", static_cast<long long>(got),
                static_cast<long long>(want), rounded);
    DAWLAB_CHECK(got == want && std::fabs(rounded - static_cast<double>(want)) > 100.0);
}

} // namespace ex4::check

int main() {
    using namespace ex4::check;
    std::printf("Exercise 4 - a tempo map\n");
    checkOneTempo();
    checkChanges();
    checkRandomMaps();
    checkBlocks();
    checkNoDrift();
    return dawlab::test::summary("l18_ex4");
}
