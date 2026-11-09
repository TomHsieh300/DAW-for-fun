#pragma once
// Exercise 4 - the self-check (main). Included at the end of the starter and of the solution.

#include "ex4_measure_latency.h"

#include <dawlab/Check.h>

#include <cstdio>
#include <random>
#include <set>
#include <utility>

namespace ex4::check {

constexpr double kRate = 48000.0;
constexpr int kChirps = 8;

// What a take placed on the timeline would hold: noise, and each chirp `lags[k]` late (a missing one
// not at all), `gain` loud, with an echo 600 samples later; optionally inverted, and a door slam.
struct Room {
    explicit Room(std::vector<int> lagsPerChirp) : lags(std::move(lagsPerChirp)) {}
    std::vector<int> lags;
    float gain = 0.3f;
    float noise = 0.001f;
    bool invert = false;
    std::set<int> missing;
    bool door = false;
};

inline std::vector<std::int64_t> chirpPositions() {
    std::vector<std::int64_t> at;
    for (int k = 1; k <= kChirps; ++k)
        at.push_back(static_cast<std::int64_t>(k * kRate));
    return at;
}

inline std::vector<float> record(const Room& room, const std::vector<float>& chirp) {
    std::vector<float> rec(static_cast<size_t>((kChirps + 2) * kRate));
    std::mt19937 rng(42);
    if (room.noise > 0.0f) {
        std::normal_distribution<float> noise(0.0f, room.noise);
        for (float& s : rec)
            s = noise(rng);
    }
    const std::vector<std::int64_t> at = chirpPositions();
    for (int k = 0; k < kChirps; ++k) {
        if (room.missing.count(k) != 0)
            continue;
        for (size_t i = 0; i < chirp.size(); ++i) {
            const float v = (room.invert ? -1.0f : 1.0f) * room.gain / 0.5f * chirp[i];
            rec[static_cast<size_t>(at[static_cast<size_t>(k)] + room.lags[static_cast<size_t>(k)]) + i] += v;
            rec[static_cast<size_t>(at[static_cast<size_t>(k)] + room.lags[static_cast<size_t>(k)] + 600) + i] += 0.3f * v; // the echo
        }
    }
    if (room.door) // a slam, louder than any chirp, 50 ms after the third chirp was played
        for (int i = 0; i < 2000; ++i)
            rec[static_cast<size_t>(at[2] + 2400 + i)] += (i % 2 == 0 ? 1.0f : -1.0f) * 0.8f * std::exp(-i / 300.0f);
    return rec;
}

inline LatencyMeasurement run(const char* what, const Room& room, int early, int late) {
    const std::vector<float> chirp = makeChirp(kRate);
    const LatencyMeasurement m = measureLatency(record(room, chirp), chirpPositions(), chirp, early, late, kRate);
    std::printf("  %-44s heard %d of %d, lag %6d, spread %5d: %s\n", what, m.heardCount, kChirps, m.lag, m.spread,
                m.trustworthy ? "set the trim" : "do not trust it");
    return m;
}

inline void checkAll() {
    const std::vector<int> same(kChirps, 37);
    LatencyMeasurement m = run("through a cable", Room{same}, 2400, 4800);
    DAWLAB_CHECK(m.lags.size() == kChirps && m.heard.size() == kChirps);
    DAWLAB_CHECK(m.heardCount == kChirps && m.lag == 37 && m.spread == 0 && m.trustworthy);

    Room air{std::vector<int>(kChirps, 1234)};
    air.gain = 0.01f; // -40 dB, and the noise is only 14 dB below it
    air.noise = 0.002f;
    m = run("through the air, quiet", air, 2400, 4800);
    DAWLAB_CHECK(m.heardCount == kChirps && m.lag == 1234 && m.trustworthy);

    m = run("the driver says too much", Room{std::vector<int>(kChirps, -300)}, 2400, 4800);
    DAWLAB_CHECK(m.lag == -300 && m.trustworthy);

    Room inverted{same};
    inverted.invert = true;
    m = run("the microphone inverts", inverted, 2400, 4800);
    DAWLAB_CHECK(m.lag == 37 && m.trustworthy);

    Room dropout{same};
    dropout.missing = {5};
    m = run("one chirp lost", dropout, 2400, 4800);
    DAWLAB_CHECK(m.heardCount == kChirps - 1 && !m.heard[5] && m.lag == 37 && m.trustworthy);

    Room door{same};
    door.door = true;
    m = run("a door slams after the third chirp", door, 2400, 4800);
    DAWLAB_CHECK(m.lags.size() == kChirps && m.lags[2] == 37 && m.lag == 37 && m.trustworthy);
    {
        // Step 2's way - the first frame at half the peak - finds the door, not the click.
        const std::vector<float> rec = record(door, makeChirp(kRate));
        const std::int64_t from = 3 * static_cast<std::int64_t>(kRate) - 2400, to = from + 9600;
        float peak = 0.0f;
        for (std::int64_t i = from; i < to; ++i)
            peak = std::max(peak, std::abs(rec[static_cast<size_t>(i)]));
        std::int64_t first = from;
        while (std::abs(rec[static_cast<size_t>(first)]) < 0.5f * peak)
            ++first;
        std::printf("    (the first loud frame says %lld)\n", static_cast<long long>(first - 3 * static_cast<std::int64_t>(kRate)));
        DAWLAB_CHECK(first - 3 * static_cast<std::int64_t>(kRate) > 1000);
    }

    // The laptop of Step 7: every chirp heard, but the latency moves.
    Room laptop{{17236, 17227, 17236, 17236, 17879, 13354, 16776, 17236}};
    laptop.gain = 0.02f;
    m = run("a latency that moves (Step 7's laptop)", laptop, 4800, 21600);
    DAWLAB_CHECK(m.heardCount == kChirps && m.lag == 17236 && m.spread == 17879 - 13354 && !m.trustworthy);

    Room muted{same};
    muted.gain = 0.0f;
    muted.noise = 0.0f;
    m = run("a muted microphone", muted, 2400, 4800);
    DAWLAB_CHECK(m.heardCount == 0 && m.lag == 0 && !m.trustworthy);

    Room few{same};
    few.missing = {0, 2, 3, 5, 6};
    m = run("three chirps of eight", few, 2400, 4800);
    DAWLAB_CHECK(m.heardCount == 3 && m.lag == 37 && !m.trustworthy);

    Room five{same};
    five.missing = {0, 3, 6};
    m = run("five chirps of eight", five, 2400, 4800);
    DAWLAB_CHECK(m.heardCount == 5 && !m.trustworthy);
    Room six{same};
    six.missing = {1, 4};
    m = run("six chirps of eight", six, 2400, 4800);
    DAWLAB_CHECK(m.heardCount == 6 && m.trustworthy);

    // Within 1 ms is fine, beyond it is not.
    m = run("lags 48 samples apart", Room{{37, 85, 37, 37, 60, 37, 37, 37}}, 2400, 4800);
    DAWLAB_CHECK(m.spread == 48 && m.trustworthy);
    m = run("lags 49 samples apart", Room{{37, 86, 37, 37, 60, 37, 37, 37}}, 2400, 4800);
    DAWLAB_CHECK(m.spread == 49 && !m.trustworthy);
}

} // namespace ex4::check

int main() {
    std::printf("Exercise 4 - measure the latency, and know when not to trust it\n");
    ex4::check::checkAll();
    return dawlab::test::summary("l17_ex4");
}
