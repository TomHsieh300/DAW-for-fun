#pragma once
// Exercise 3 - the self-check (main). Included at the end of the starter and of the solution.

#include "ex3_preroll.h"

#include <dawlab/Check.h>

#include <cstdio>
#include <random>

namespace minidaw::ex3_check {

// The takes of the shortened stream are the takes of the whole one, each `skip` frames earlier in it.
inline bool sameTakes(std::int64_t stamped, const Latencies& l, const RecordRange& r, std::int64_t n, std::int64_t minFrames,
                      const Preroll& p) {
    const std::vector<Placement> whole = takesOf(stamped, l, r, n, minFrames);
    const std::vector<Placement> cut = takesOf(p.stampedPosition, l, r, std::max<std::int64_t>(n - p.skip, 0), minFrames);
    if (whole.size() != cut.size())
        return false;
    for (size_t i = 0; i < whole.size(); ++i)
        if (cut[i].timelineStart != whole[i].timelineStart || cut[i].numFrames != whole[i].numFrames ||
            cut[i].skipFrames != whole[i].skipFrames - p.skip || cut[i].skipFrames < 0)
            return false;
    return true;
}

inline void checkExamples() {
    const Latencies l{480, 480, 240, 0}; // 1200 in all
    RecordRange punch;
    punch.punchIn = 480000; // bar 41 at 120 BPM, 48 kHz
    punch.punchOut = 528000;

    // Started at 0: everything up to the punch (and its preroll) is never a take.
    const Preroll p = prerollFor(0, l, punch, 48000);
    std::printf("  a punch at 10 s, recorded from 0: %lld frames (%.2f s) never written, the take at frame %lld of the file\n",
                static_cast<long long>(p.skip), p.skip / 48000.0, static_cast<long long>(takesOf(p.stampedPosition, l, punch, 600000).front().skipFrames));
    DAWLAB_CHECK(p.skip == 480000 + 1200 - 48000);
    DAWLAB_CHECK(p.stampedPosition == p.skip);
    DAWLAB_CHECK(sameTakes(0, l, punch, 700000, 0, p));
    DAWLAB_CHECK(takesOf(p.stampedPosition, l, punch, 700000 - p.skip).front().skipFrames == 48000); // the preroll is there

    // Started inside the preroll, or after the punch-in: nothing to drop.
    DAWLAB_CHECK(prerollFor(450000, l, punch, 48000).skip == 0);
    DAWLAB_CHECK(prerollFor(500000, l, punch, 48000).skip == 0);
    // No punch, no loop: only what lies before the timeline's zero can go.
    DAWLAB_CHECK(prerollFor(0, l, RecordRange{}, 0).skip == 1200);
    DAWLAB_CHECK(prerollFor(0, l, RecordRange{}, 48000).skip == 0);
    // A negative trim (the driver says too much) makes L smaller, never the skip negative.
    DAWLAB_CHECK(prerollFor(0, Latencies{0, 0, 0, -5000}, RecordRange{}, 0).skip == 0);

    // A loop from bar 9 to bar 13, started at bar 1: the first pass is far away.
    RecordRange loop;
    loop.loopStart = 384000;
    loop.loopEnd = 576000;
    const Preroll q = prerollFor(0, l, loop, 24000);
    DAWLAB_CHECK(q.skip == 384000 + 1200 - 24000);
    DAWLAB_CHECK(sameTakes(0, l, loop, 384000 + 1200 + 192000 * 3 + 50000, 0, q));
    // A huge preroll may not move the start past the loop's end.
    RecordRange shortLoop;
    shortLoop.loopStart = 0;
    shortLoop.loopEnd = 1000;
    const Preroll s = prerollFor(500, Latencies{0, 0, 0, -40000}, shortLoop, 0);
    DAWLAB_CHECK(s.stampedPosition < shortLoop.loopEnd && s.skip >= 0);
    DAWLAB_CHECK(sameTakes(500, Latencies{0, 0, 0, -40000}, shortLoop, 100000, 0, s));
    // A punch outside the loop: no take at all, nothing to gain.
    RecordRange outside = loop;
    outside.punchIn = 700000;
    outside.punchOut = 710000;
    DAWLAB_CHECK(prerollFor(0, l, outside, 0).skip == 0);
}

inline void checkRandom() {
    std::mt19937_64 rng(1717);
    auto pick = [&rng](std::int64_t lo, std::int64_t hi) { return lo + static_cast<std::int64_t>(rng() % static_cast<std::uint64_t>(hi - lo + 1)); };
    int wrong = 0;
    std::int64_t saved = 0, total = 0;
    for (int c = 0; c < 20000; ++c) {
        const Latencies l{static_cast<int>(pick(0, 4000)), static_cast<int>(pick(0, 4000)), static_cast<int>(pick(0, 3000)),
                          static_cast<int>(pick(-6000, 6000))};
        RecordRange r;
        const int kind = static_cast<int>(rng() % 4);
        if (kind & 1) {
            r.loopStart = pick(0, 400000);
            r.loopEnd = r.loopStart + pick(1, 300000);
        }
        if (kind & 2) {
            r.punchIn = pick(0, 700000);
            r.punchOut = r.punchIn + pick(1, 300000);
        }
        const std::int64_t stamped = pick(-20000, 600000);
        const std::int64_t n = pick(0, 2000000);
        const std::int64_t minFrames = rng() % 2 == 0 ? 0 : pick(0, 5000);
        const std::int64_t preroll = pick(0, 50000);
        const Preroll p = prerollFor(stamped, l, r, preroll);
        if (p.skip < 0 || p.stampedPosition != stamped + p.skip || !sameTakes(stamped, l, r, n, minFrames, p))
            ++wrong;
        // Every take keeps its preroll, or all the frames there were before it.
        const std::vector<Placement> cut = takesOf(p.stampedPosition, l, r, std::max<std::int64_t>(n - p.skip, 0), minFrames);
        const std::vector<Placement> whole = takesOf(stamped, l, r, n, minFrames);
        if (!cut.empty() && !whole.empty() && cut.front().skipFrames < std::min(preroll, whole.front().skipFrames))
            ++wrong;
        saved += std::min(p.skip, n);
        total += n;
    }
    std::printf("  20000 random recordings: %d wrong (expected 0), %.0f %% of the frames not written\n", wrong, 100.0 * saved / total);
    DAWLAB_CHECK(wrong == 0);
    DAWLAB_CHECK(saved > total / 40); // random ranges; a punch far ahead saves far more (above)
}

inline void checkGate() {
    std::mt19937 rng(7);
    for (const std::int64_t skip : {0LL, 1LL, 511LL, 512LL, 100000LL}) {
        StreamGate gate;
        gate.arm(skip);
        std::int64_t position = 0, firstKept = -1, kept = 0;
        while (position < 200000) {
            const int n = 1 + static_cast<int>(rng() % 700);
            const int d = gate.drop(n);
            if (d < 0 || d > n) {
                firstKept = -2;
                break;
            }
            if (d < n && firstKept == -1)
                firstKept = position + d;
            kept += n - d;
            position += n;
        }
        DAWLAB_CHECK(firstKept == skip && gate.dropped() == skip && kept == position - skip);
    }
    StreamGate gate;
    gate.arm(100);
    DAWLAB_CHECK(gate.drop(0) == 0 && gate.drop(60) == 60 && gate.drop(60) == 40 && gate.drop(60) == 0);
    gate.arm(10); // the next recording starts over
    DAWLAB_CHECK(gate.dropped() == 0 && gate.drop(64) == 10);
}

} // namespace minidaw::ex3_check

int main() {
    std::printf("Exercise 3 - write only what a take can use\n");
    minidaw::ex3_check::checkExamples();
    minidaw::ex3_check::checkRandom();
    minidaw::ex3_check::checkGate();
    return dawlab::test::summary("l17_ex3");
}
