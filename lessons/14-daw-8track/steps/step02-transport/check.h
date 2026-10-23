#pragma once
// check.h - the self-check of Step 2 (transport). Defines main(); included at the END of
// starter/step02.cpp and solution/step02.cpp.
//
// Tests only what Step 2 adds: the musical grid, seeks, segments, and the loop - first on the
// Transport alone, then through the engine with a "ramp tape" whose sample i has the value i,
// so every output sample tells which timeline position was played.

#include <dawlab/Common.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <random>

namespace step02_check {

using namespace dawlab;
using namespace minidaw;

inline bool same(const BarsBeats& a, const BarsBeats& b) { return a.bar == b.bar && a.beat == b.beat && a.tick == b.tick; }

// Renders `total` samples with the block sizes produced by nextBlock() and compares every output
// sample with a reference model of the transport: play from `start`, wrap at the loop end.
template <typename NextBlock>
int64_t countMisplaced(int64_t start, int64_t loopStart, int64_t loopEnd, int total, NextBlock nextBlock) {
    auto ramp = std::make_shared<AudioBuffer>(1, 20000);
    for (int i = 0; i < ramp->getNumSamples(); ++i)
        ramp->setSample(0, i, static_cast<float>(i));
    Engine engine;
    engine.prepare(48000.0, 512);
    engine.setSource(ramp);
    Transport& t = engine.getTransport();
    t.setLoopRange(loopStart, loopEnd);
    t.setLoopEnabled(true);
    t.setPosition(start);
    t.play();
    AudioBuffer out(kNumChannels, total);
    for (int pos = 0; pos < total;) {
        const int n = std::min(nextBlock(), total - pos);
        engine.processBlock(out.toBlock().getSubBlock(pos, n));
        pos += n;
    }
    int64_t expected = start, misplaced = 0;
    for (int i = 0; i < total; ++i) {
        if (out.getSample(0, i) != static_cast<float>(expected) || out.getSample(1, i) != static_cast<float>(expected))
            ++misplaced;
        if (++expected == loopEnd)
            expected = loopStart;
    }
    return misplaced;
}

inline int run() {
    std::printf("Step 2: the transport (build-along)\n");

    // ---- 1) the grid ----------------------------------------------------------------------------
    Transport t;
    t.prepare(48000.0);
    std::printf("  120 BPM, 4/4, 48 kHz: %.0f samples per beat, %.0f per bar\n", t.getSamplesPerBeat(),
                t.getSamplesPerBar());
    std::printf("     bar:beat:tick    samples    back\n");
    const BarsBeats grid[] = {{1, 1, 0}, {1, 2, 0}, {1, 2, 480}, {2, 1, 0}, {3, 2, 480}, {9, 1, 0}};
    const int64_t gridSamples[] = {0, 24000, 36000, 96000, 228000, 768000};
    for (int i = 0; i < 6; ++i) {
        const int64_t s = t.barsBeatsToSamples(grid[i]);
        const BarsBeats back = t.samplesToBarsBeats(s);
        std::printf("     %2d:%d:%03d      %7lld    %d:%d:%03d\n", grid[i].bar, grid[i].beat, grid[i].tick,
                    static_cast<long long>(s), back.bar, back.beat, back.tick);
        DAWLAB_CHECK(s == gridSamples[i]);
        DAWLAB_CHECK(same(back, grid[i]));
    }
    DAWLAB_CHECK(same(t.samplesToBarsBeats(23999), BarsBeats{1, 1, 959})); // one sample before beat 2
    t.setTempo(90.0);
    t.setTimeSignature(7, 8);
    std::printf("  7/8 at 90 BPM: bar 2 = %lld samples (expected 224000)\n",
                static_cast<long long>(t.barsBeatsToSamples({2, 1, 0})));
    DAWLAB_CHECK(t.barsBeatsToSamples({2, 1, 0}) == 224000);
    DAWLAB_CHECK(same(t.samplesToBarsBeats(224000 + 32000), BarsBeats{2, 2, 0}));
    Transport odd; // 96 BPM at 44.1 kHz: a beat is 27562.5 samples - round once, at the end
    odd.prepare(44100.0);
    odd.setTempo(96.0);
    std::printf("  96 BPM at 44.1 kHz: beat 2 = %lld, bar 2 = %lld (expected 27563, 110250)\n",
                static_cast<long long>(odd.barsBeatsToSamples({1, 2, 0})),
                static_cast<long long>(odd.barsBeatsToSamples({2, 1, 0})));
    DAWLAB_CHECK(odd.barsBeatsToSamples({1, 2, 0}) == 27563);
    DAWLAB_CHECK(odd.barsBeatsToSamples({2, 1, 0}) == 110250);
    DAWLAB_CHECK(same(odd.samplesToBarsBeats(27563), BarsBeats{1, 2, 0}));

    // ---- 2) advance(): stopped, playing, seek -------------------------------------------------
    Transport tr;
    tr.prepare(48000.0);
    TimelineSegment seg[Transport::kMaxSegmentsPerBlock];
    int covered = -1;
    tr.setPosition(5000);
    int count = tr.advance(512, seg, covered);
    DAWLAB_CHECK(count == 0);                 // stopped: no segments...
    DAWLAB_CHECK(covered == 512);             // ...but the whole block is "done"
    DAWLAB_CHECK(tr.getPosition() == 5000);   // and the pending seek was applied
    tr.play();
    count = tr.advance(512, seg, covered);
    DAWLAB_CHECK(count == 1 && seg[0].timelineStart == 5000 && seg[0].blockOffset == 0 && seg[0].numSamples == 512);
    DAWLAB_CHECK(tr.getPosition() == 5512);

    // ---- 3) the loop split ------------------------------------------------------------------------
    tr.setLoopRange(1000, 1300);
    tr.setLoopEnabled(true);
    tr.setPosition(1256);
    count = tr.advance(100, seg, covered);
    std::printf("  loop [1000, 1300), position 1256, 100-sample block: %d segments", count);
    for (int i = 0; i < count; ++i)
        std::printf(" {%lld, offset %d, %d}", static_cast<long long>(seg[i].timelineStart), seg[i].blockOffset,
                    seg[i].numSamples);
    std::printf(", position after %lld\n", static_cast<long long>(tr.getPosition()));
    DAWLAB_CHECK(count == 2);
    DAWLAB_CHECK(seg[0].timelineStart == 1256 && seg[0].blockOffset == 0 && seg[0].numSamples == 44);
    DAWLAB_CHECK(seg[1].timelineStart == 1000 && seg[1].blockOffset == 44 && seg[1].numSamples == 56);
    DAWLAB_CHECK(tr.getPosition() == 1056);
    tr.setLoopRange(0, 10); // a 10-sample loop needs more than 8 segments for 512 samples
    tr.setPosition(0);
    count = tr.advance(512, seg, covered);
    std::printf("  10-sample loop, 512-sample block: %d segments cover %d samples (expected 8, 80)\n", count, covered);
    DAWLAB_CHECK(count == Transport::kMaxSegmentsPerBlock && covered == 80);
    tr.setLoopRange(100, 200); // switched on behind the loop end: plays on linearly
    tr.setPosition(300);
    count = tr.advance(512, seg, covered);
    DAWLAB_CHECK(count == 1 && seg[0].timelineStart == 300 && tr.getPosition() == 812);

    // ---- 4) through the engine: every output sample at the right timeline position ----------
    const int64_t m441 = countMisplaced(900, 1000, 1300, 3000, [] { return 441; });
    std::mt19937 rng(7);
    std::uniform_int_distribution<int> size(1, 600);
    const int64_t mTiny = countMisplaced(0, 3, 10, 10000, [&] { return size(rng); });
    std::printf("  engine, loop [1000, 1300) from 900 with 441-sample blocks: %lld misplaced samples (expected 0)\n",
                static_cast<long long>(m441));
    std::printf("  engine, 7-sample loop with random blocks 1..600: %lld misplaced of 10000 (expected 0)\n",
                static_cast<long long>(mTiny));
    DAWLAB_CHECK(m441 == 0);
    DAWLAB_CHECK(mTiny == 0);

    return dawlab::test::summary("l14_step02");
}

} // namespace step02_check

int main() { return step02_check::run(); }
