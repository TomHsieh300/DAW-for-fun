#pragma once
// Chapter 19, Step 3 - the self-check (main). Included at the end of the starter and of the solution.

#include "minidaw.h"

#include <dawlab/Check.h>

#include <cstdio>
#include <random>

namespace step03_check {

using namespace minidaw;

// A synth (no latency), a sampler that reads ahead, a synth with a limiter and a filter, a sampler that streams
// 4096 samples ahead - armed.
inline std::vector<LatencyTrack> session() {
    return {{0, 0, false}, {256, 0, false}, {64, 32, false}, {4096, 0, true}};
}

inline void checkPlan() {
    const std::vector<LatencyTrack> tracks = session();
    const Plan whole = planLatency(tracks, false);
    DAWLAB_CHECK(whole.total == 4096 && whole.compensation == std::vector<int>({4096, 3840, 4000, 0}));
    const Plan low = planLatency(tracks, true);
    DAWLAB_CHECK(low.total == 256 && low.compensation == std::vector<int>({256, 0, 160, 0}));
    std::printf("  latencies 0, 256, 64+32 and 4096 (armed): total %d, waits %d %d %d %d; with low-latency monitoring: total %d, "
                "waits %d %d %d %d\n",
                whole.total, whole.compensation[0], whole.compensation[1], whole.compensation[2], whole.compensation[3], low.total,
                low.compensation[0], low.compensation[1], low.compensation[2], low.compensation[3]);
    DAWLAB_CHECK(planLatency({}, false).total == 0 && planLatency({{100, 0, true}}, true).total == 0);
}

inline void checkLive() {
    // Blocks of 512, the keyboard scheduled 2 blocks late (chapter 18, Step 5), an output latency of 512.
    std::vector<LatencyTrack> tracks = session();
    const Plan whole = planLatency(tracks, false), low = planLatency(tracks, true);
    const int sampler = heardLive(tracks, whole, 3, 1024, 512), samplerLow = heardLive(tracks, low, 3, 1024, 512);
    DAWLAB_CHECK(sampler == 5632 && samplerLow == 5632);
    // Arm the synth instead: without low-latency monitoring it waits for the sampler on another track.
    tracks[3].armed = false;
    tracks[0].armed = true;
    const Plan whole2 = planLatency(tracks, false), low2 = planLatency(tracks, true);
    const int synth = heardLive(tracks, whole2, 0, 1024, 512), synthLow = heardLive(tracks, low2, 0, 1024, 512);
    DAWLAB_CHECK(synth == 5632 && synthLow == 1536);
    std::printf("  a key to the ear on the sampler: %d samples (%.0f ms) either way; on the synth %d (%.0f ms), with low-latency "
                "monitoring %d (%.0f ms)\n",
                sampler, sampler / 48.0, synth, synth / 48.0, synthLow, synthLow / 48.0);
}

inline void checkRecording() {
    // The musician plays exactly on the beat at sample 96000, as they hear it. The engine was rendering
    // 96000 + total + output latency then. Where the note goes must not depend on the armed instrument's latency.
    for (const int armedLatency : {0, 256, 4096, 20000}) {
        std::vector<LatencyTrack> tracks = session();
        tracks[3].instrumentLatency = armedLatency;
        const Plan low = planLatency(tracks, true), whole = planLatency(tracks, false);
        DAWLAB_CHECK(placeRecorded(96000 + low.total + 512, low, 512) == 96000);
        DAWLAB_CHECK(placeRecorded(96000 + whole.total + 512, whole, 512) == 96000);
    }
    std::vector<LatencyTrack> tracks = session();
    const Plan low = planLatency(tracks, true);
    std::printf("  a note played on the beat it heard: at 96000 whatever the armed sampler's latency (low-latency total %d)\n", low.total);
}

// Plays notes at sample `at` on the tracks `on`, in random blocks; returns the output.
inline std::vector<float> render(const std::vector<LatencyTrack>& tracks, bool low, std::int64_t at, const std::vector<int>& on,
                                 std::int64_t total, unsigned seed) {
    Mix mix;
    mix.setTracks(tracks, low);
    std::vector<float> out(static_cast<size_t>(total));
    std::mt19937 rng(seed);
    for (std::int64_t s = 0; s < total;) {
        const int n = static_cast<int>(std::min<std::int64_t>(seed == 0 ? 512 : std::uniform_int_distribution<int>(1, 700)(rng), total - s));
        std::vector<std::vector<BlockEvent>> events(tracks.size());
        if (at >= s && at < s + n)
            for (const int t : on)
                events[static_cast<size_t>(t)].push_back({static_cast<int>(at - s), true});
        std::fill(out.begin() + s, out.begin() + s + n, 7.0f); // render() clears its block itself
        mix.render(events, out.data() + s, n);
        s += n;
    }
    return out;
}

inline std::vector<std::pair<std::int64_t, float>> impulses(const std::vector<float>& x) {
    std::vector<std::pair<std::int64_t, float>> found;
    for (size_t i = 0; i < x.size(); ++i)
        if (x[i] != 0.0f)
            found.push_back({static_cast<std::int64_t>(i), x[i]});
    return found;
}

inline void checkMix() {
    const std::vector<LatencyTrack> tracks = session();
    // A chord on all four tracks at 10000: together at 10000 + 4096, in any blocks.
    for (const unsigned seed : {0u, 1u, 2u}) {
        const auto found = impulses(render(tracks, false, 10000, {0, 1, 2, 3}, 20000, seed));
        DAWLAB_CHECK(found.size() == 1 && found[0].first == 14096 && found[0].second == 4.0f);
    }
    // With low-latency monitoring: the three others together at 10000 + 256, the armed sampler on its own after its
    // own 4096 - the others no longer wait for it.
    const auto low = impulses(render(tracks, true, 10000, {0, 1, 2, 3}, 20000, 3));
    DAWLAB_CHECK(low.size() == 2 && low[0].first == 10256 && low[0].second == 3.0f && low[1].first == 14096 && low[1].second == 1.0f);
    std::printf("  a chord on the four tracks at 10000: one impulse of 4 at %lld; with low-latency monitoring 3 at %lld and the armed "
                "one at %lld\n",
                static_cast<long long>(14096), static_cast<long long>(low.empty() ? -1 : low[0].first),
                static_cast<long long>(low.size() < 2 ? -1 : low[1].first));
    // Random sessions: every song note heard at its sample + total.
    std::mt19937 rng(7);
    int wrong = 0;
    for (int run = 0; run < 100; ++run) {
        std::vector<LatencyTrack> t(static_cast<size_t>(std::uniform_int_distribution<int>(1, 8)(rng)));
        for (LatencyTrack& x : t)
            x = {std::uniform_int_distribution<int>(0, 3000)(rng), std::uniform_int_distribution<int>(0, 500)(rng), false};
        std::vector<int> all;
        for (int k = 0; k < static_cast<int>(t.size()); ++k)
            all.push_back(k);
        const Plan plan = planLatency(t, false);
        const auto found = impulses(render(t, false, 1234, all, 6000, static_cast<unsigned>(run + 10)));
        wrong += found.size() == 1 && found[0].first == 1234 + plan.total && found[0].second == static_cast<float>(t.size()) ? 0 : 1;
    }
    std::printf("  100 random sessions in random blocks: %d where the tracks did not arrive together at the note + total\n", wrong);
    DAWLAB_CHECK(wrong == 0);
}

} // namespace step03_check

int main() {
    using namespace step03_check;
    std::printf("Step 3: instrument latency\n");
    checkPlan();
    checkLive();
    checkRecording();
    checkMix();
    return dawlab::test::summary("l19_step03");
}
