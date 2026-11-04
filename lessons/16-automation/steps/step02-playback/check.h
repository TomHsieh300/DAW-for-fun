#pragma once
// check.h - the self-check of Step 2 (the curve on the audio thread). Defines main(); included at
// the END of starter/step02.cpp and solution/step02.cpp, so both run exactly the same checks.
//
// Hand-made cases first (a ramp, a jump inside a block, a loop, a seek, stopped, the knob, a swap),
// then random sessions against a reference written as plainly as possible: one sample at a time.

#include <dawlab/Common.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

// Every allocation in this program is counted: render() must not make any.
namespace step02_check {
inline long long allocations = 0, frees = 0;
inline void* countedAlloc(std::size_t size) {
    ++allocations;
    if (void* p = std::malloc(size > 0 ? size : 1))
        return p;
    throw std::bad_alloc();
}
inline void countedFree(void* p) noexcept {
    frees += p != nullptr ? 1 : 0;
    std::free(p);
}
} // namespace step02_check
void* operator new(std::size_t size) { return step02_check::countedAlloc(size); }
void* operator new[](std::size_t size) { return step02_check::countedAlloc(size); }
void operator delete(void* p) noexcept { step02_check::countedFree(p); }
void operator delete[](void* p) noexcept { step02_check::countedFree(p); }
void operator delete(void* p, std::size_t) noexcept { step02_check::countedFree(p); }
void operator delete[](void* p, std::size_t) noexcept { step02_check::countedFree(p); }

namespace step02_check {

using namespace minidaw;

// A deterministic random generator (the same sequence on every compiler).
struct Lcg {
    std::uint32_t state = 216u;
    std::uint32_t next() noexcept {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    }
    int below(int n) noexcept { return n <= 0 ? 0 : static_cast<int>(next() % static_cast<std::uint32_t>(n)); }
    float unit() noexcept { return static_cast<float>(next() % 10001u) / 10000.0f; }
};

// One block of the transport through the lane, the way the engine will do it.
inline void playBlock(MiniTransport& transport, AutomationLane& lane, float* out, int n) {
    TimelineSegment segments[MiniTransport::kMaxSegments];
    const int count = transport.advance(n, segments);
    lane.render(segments, count, transport.position(), out, n);
}

inline AutomationCurve* rampCurve(std::int64_t from, std::int64_t to) {
    auto* c = new AutomationCurve();
    c->add({from, 0.0f, Shape::Linear});
    c->add({to, 1.0f, Shape::Linear});
    return c;
}

inline float biggestStep(const float* v, int n) {
    float biggest = 0.0f;
    for (int i = 1; i < n; ++i)
        biggest = std::max(biggest, std::abs(v[i] - v[i - 1]));
    return biggest;
}

// The whole step, one sample at a time and without a segment, a cursor or a search that could be
// wrong: the position moves by one and wraps at the loop end; the target is valueAt(); the limit is
// applied to one value. Slow on purpose - it is what the fast code must agree with.
struct Reference {
    std::int64_t position = 0, loopStart = 0, loopEnd = 0;
    bool playing = false, started = false;
    float value = 0.0f, maxStep = 1.0f / 64.0f;

    float next(const AutomationCurve* curve, float knob) {
        const float wanted = curve == nullptr ? knob : curve->valueAt(position);
        if (!started)
            value = wanted;
        else if (wanted - value > maxStep)
            value += maxStep;
        else if (value - wanted > maxStep)
            value -= maxStep;
        else
            value = wanted;
        started = true;
        if (playing) {
            ++position;
            if (loopEnd > loopStart && position == loopEnd)
                position = loopStart;
        }
        return value;
    }
};

inline int run() {
    std::printf("Step 2: the curve on the audio thread (build-along)\n");

    // ---- a slow ramp comes through exactly, one value per sample ---------------------------------
    {
        AutomationLane lane;
        const AutomationCurve* ramp = rampCurve(0, 48000);
        lane.exchangeCurve(ramp);
        MiniTransport transport;
        transport.play();
        std::vector<float> all(48000), perBlock(48000);
        for (int done = 0; done < 48000; done += 512)
            playBlock(transport, lane, all.data() + done, std::min(512, 48000 - done));
        int wrong = 0;
        for (int i = 0; i < 48000; ++i) {
            perBlock[static_cast<size_t>(i)] = ramp->valueAt(i - i % 512);
            wrong += all[static_cast<size_t>(i)] == ramp->valueAt(i) ? 0 : 1;
        }
        const float stepAll = biggestStep(all.data(), 48000), stepBlock = biggestStep(perBlock.data(), 48000);
        std::printf("  a 1 s ramp in blocks of 512: %d of 48000 samples differ from the curve (expected 0)\n"
                    "  biggest step %.6f; one value per block would make %.6f, a staircase you can hear\n",
                    wrong, static_cast<double>(stepAll), static_cast<double>(stepBlock));
        DAWLAB_CHECK(wrong == 0);
        DAWLAB_CHECK(stepAll < 2.2e-5f && stepBlock > 0.01f);
    }

    // ---- the first sample the lane ever renders is not a ramp from nothing -----------------------
    {
        AutomationLane lane;
        auto* c = new AutomationCurve();
        c->add({0, 0.8f, Shape::Linear});
        lane.exchangeCurve(c);
        MiniTransport transport;
        transport.play();
        float out[8] = {};
        playBlock(transport, lane, out, 8);
        std::printf("  first sample of a curve at 0.8: %.4f (expected 0.8000)\n", static_cast<double>(out[0]));
        DAWLAB_CHECK(out[0] == 0.8f && out[7] == 0.8f);
    }

    // ---- a jump lands on its sample, inside a block, as a short ramp ------------------------------
    {
        AutomationLane lane;
        auto* c = new AutomationCurve();
        c->add({0, 0.25f, Shape::Hold});
        c->add({1000, 0.75f, Shape::Linear}); // at sample 1000: the second block's offset 200
        lane.exchangeCurve(c);
        MiniTransport transport;
        transport.play();
        float first[800] = {}, second[800] = {};
        playBlock(transport, lane, first, 800);
        playBlock(transport, lane, second, 800);
        float dc[800];
        std::fill_n(dc, 800, 1.0f);
        applyGains(dc, second, 800);
        std::printf("  jump 0.25 -> 0.75 at sample 1000, block offsets 199, 200, 230, 231: %.4f %.4f %.4f %.4f\n"
                    "  (expected 0.2500 0.2656 0.7344 0.7500: 32 steps of 1/64, starting exactly on the sample)\n",
                    static_cast<double>(dc[199]), static_cast<double>(dc[200]), static_cast<double>(dc[230]),
                    static_cast<double>(dc[231]));
        DAWLAB_CHECK(first[799] == 0.25f && dc[199] == 0.25f && std::abs(dc[200] - 0.265625f) < 1e-6f);
        DAWLAB_CHECK(dc[230] < 0.75f && dc[231] == 0.75f && dc[799] == 0.75f);
    }

    // ---- the block size changes nothing -----------------------------------------------------------
    {
        const int total = 100000;
        auto renderAll = [&](int blockSize) {
            AutomationLane lane;
            auto* c = new AutomationCurve();
            c->add({2000, 0.1f, Shape::Linear});
            c->add({9000, 0.9f, Shape::Hold});
            c->add({15000, 0.2f, Shape::Linear});
            c->add({15000, 0.6f, Shape::Linear});
            c->add({40000, 0.0f, Shape::Linear});
            lane.exchangeCurve(c);
            MiniTransport transport;
            transport.setLoop(10000, 30000);
            transport.play();
            Lcg rng;
            std::vector<float> out(static_cast<size_t>(total));
            for (int done = 0; done < total;) {
                const int n = std::min(blockSize > 0 ? blockSize : 1 + rng.below(512), total - done);
                playBlock(transport, lane, out.data() + done, n);
                done += n;
            }
            return out;
        };
        const std::vector<float> one = renderAll(1);
        int differ = 0;
        for (const int size : {64, 300, 512, 0}) // 0: random sizes
            differ += renderAll(size) == one ? 0 : 1;
        std::printf("  blocks of 64, 300, 512 and of random sizes against blocks of 1: %d differ (expected 0)\n", differ);
        DAWLAB_CHECK(differ == 0);
        DAWLAB_CHECK(one[50] == 0.1f && one[99999] != 0.0f);
    }

    // ---- a loop inside one block ------------------------------------------------------------------
    {
        AutomationLane lane;
        lane.exchangeCurve(rampCurve(1000, 1100));
        MiniTransport transport;
        transport.seek(1000);
        transport.setLoop(1000, 1100);
        transport.play();
        float out[512] = {};
        TimelineSegment segments[MiniTransport::kMaxSegments];
        const int count = transport.advance(512, segments);
        lane.render(segments, count, transport.position(), out, 512);
        const float step = biggestStep(out, 512);
        std::printf("  a loop of 100 samples in a block of 512: %d segments, biggest step %.4f (expected 6, 0.0156)\n",
                    count, static_cast<double>(step));
        DAWLAB_CHECK(count == 6);
        DAWLAB_CHECK(std::abs(out[99] - 0.99f) < 1e-6f && step > 0.015f && step < 0.0157f);
    }

    // ---- a seek, playing and stopped ---------------------------------------------------------------
    {
        AutomationLane lane;
        const AutomationCurve* ramp = rampCurve(0, 48000);
        lane.exchangeCurve(ramp);
        MiniTransport transport;
        transport.play();
        float out[128] = {};
        playBlock(transport, lane, out, 128);
        const float before = out[127];
        transport.seek(36000); // the curve is at 0.75 there
        playBlock(transport, lane, out, 128);
        int reached = -1;
        for (int i = 0; i < 128 && reached < 0; ++i)
            reached = out[i] == ramp->valueAt(36000 + i) ? i : -1;
        std::printf("  seek to 0.75: the first sample moves %.4f (expected 0.0156), the curve is reached at sample %d\n",
                    static_cast<double>(out[0] - before), reached);
        DAWLAB_CHECK(std::abs(out[0] - before - 1.0f / 64.0f) < 1e-6f);
        DAWLAB_CHECK(reached >= 40 && reached <= 64 && out[127] == ramp->valueAt(36127));

        transport.stop();
        transport.seek(12000); // stopped: the value still follows the playhead (0.25)
        playBlock(transport, lane, out, 128);
        std::printf("  stopped, seek to 0.25: the block ends at %.4f (expected 0.2500), position still %lld\n",
                    static_cast<double>(out[127]), static_cast<long long>(transport.position()));
        DAWLAB_CHECK(out[127] == 0.25f && transport.position() == 12000);
    }

    // ---- no curve: the knob. Swapping: the old curve comes back, a shorter one is safe ------------
    {
        AutomationLane lane;
        lane.setKnobValue(0.5f);
        MiniTransport transport;
        transport.play();
        float out[128] = {};
        playBlock(transport, lane, out, 128);
        const bool knobFirst = out[0] == 0.5f && out[127] == 0.5f;

        auto* tenPoints = new AutomationCurve();
        for (int i = 0; i < 10; ++i)
            tenPoints->add({i * 100, 0.5f, Shape::Linear});
        const AutomationCurve* nothing = lane.exchangeCurve(tenPoints);
        transport.seek(950); // the cursor ends on the last point
        playBlock(transport, lane, out, 16);

        auto* onePoint = new AutomationCurve();
        onePoint->add({0, 0.75f, Shape::Linear});
        const AutomationCurve* old = lane.exchangeCurve(onePoint); // the cursor's index is out of range now
        playBlock(transport, lane, out, 128);
        const bool oldBack = old == tenPoints && old->size() == 10 && nothing == nullptr;
        delete old; // the "UI thread": the lane frees nothing itself
        const float onCurve = out[127];

        delete lane.exchangeCurve(nullptr);
        lane.setKnobValue(0.25f);
        playBlock(transport, lane, out, 128);
        std::printf("  knob 0.5: %s. A swap returns the old curve: %s. One point instead of ten: %.4f (expected 0.7500)\n"
                    "  curve removed, knob 0.25: first sample %.4f, block ends at %.4f (expected 0.7344, 0.2500)\n",
                    knobFirst ? "followed" : "NOT followed", oldBack ? "yes" : "no", static_cast<double>(onCurve),
                    static_cast<double>(out[0]), static_cast<double>(out[127]));
        DAWLAB_CHECK(knobFirst);
        DAWLAB_CHECK(oldBack && onCurve == 0.75f);
        DAWLAB_CHECK(std::abs(out[0] - 0.734375f) < 1e-6f && out[127] == 0.25f);
    }

    // ---- the cursor: no search while the block simply continues ------------------------------------
    {
        AutomationLane lane;
        auto* c = new AutomationCurve();
        for (int i = 0; i < 10; ++i)
            c->add({i * 20000, i % 2 == 0 ? 0.0f : 1.0f, Shape::Linear});
        lane.exchangeCurve(c);
        MiniTransport transport;
        transport.play();
        float out[300];
        for (int block = 0; block < 667; ++block)
            playBlock(transport, lane, out, 300);
        const long long playing = lane.getSearches();
        for (int seek = 0; seek < 20; ++seek) {
            transport.seek(seek * 7000);
            playBlock(transport, lane, out, 300);
        }
        const long long afterSeeks = lane.getSearches() - playing;
        std::printf("  667 blocks through 10 points: %lld searches (expected at most 11); 20 seeks: %lld more (at most 20)\n",
                    playing, afterSeeks);
        DAWLAB_CHECK(playing >= 1 && playing <= 11);
        DAWLAB_CHECK(afterSeeks >= 1 && afterSeeks <= 20);

        // A recorded curve is dense: a point every 100 samples, several per block. A block still only
        // has to search when it starts exactly on a point (1 block in 25 here), if the cursor keeps
        // where the LAST sample of the block was, not where the first one was.
        AutomationLane denseLane;
        auto* dense = new AutomationCurve();
        for (int i = 0; i < 5200; ++i)
            dense->add({i * 100, i % 2 == 0 ? 0.25f : 0.75f, Shape::Linear});
        denseLane.exchangeCurve(dense);
        MiniTransport denseTransport;
        denseTransport.play();
        float block[512];
        for (int i = 0; i < 1000; ++i)
            playBlock(denseTransport, denseLane, block, 512);
        std::printf("  1000 blocks through a point every 100 samples: %lld searches (expected 40)\n",
                    denseLane.getSearches());
        DAWLAB_CHECK(denseLane.getSearches() == 40);

        // locate() itself answers like segmentAt(), also for a hint that is far too early.
        CurveCursor cursor;
        cursor.segment = 0;
        const int located = cursor.locate(*c, 150000);
        std::printf("  locate() with a hint far behind the position: %d (expected %d)\n", located, c->segmentAt(150000));
        DAWLAB_CHECK(located == c->segmentAt(150000) && located == 7);
    }

    // ---- nothing on the audio path allocates -----------------------------------------------------
    {
        AutomationLane lane;
        const AutomationCurve* spare = rampCurve(0, 5000);
        lane.exchangeCurve(rampCurve(0, 9000));
        MiniTransport transport;
        transport.setLoop(100, 700);
        transport.play();
        float out[512];
        const long long before = allocations;
        for (int i = 0; i < 2000; ++i) {
            if (i % 100 == 99)
                transport.seek(i);
            if (i == 1000)
                spare = lane.exchangeCurve(spare);
            playBlock(transport, lane, out, 512);
        }
        const long long made = allocations - before;
        delete spare;
        std::printf("  2000 blocks with seeks, a loop and a swap: %lld allocations (expected 0)\n", made);
        DAWLAB_CHECK(made == 0);
    }

    // ---- random sessions against the reference ---------------------------------------------------
    {
        Lcg rng;
        long long values = 0, mismatches = 0;
        std::vector<float> out(512);
        for (int round = 0; round < 200; ++round) {
            AutomationLane lane;
            MiniTransport transport;
            Reference ref;
            lane.setKnobValue(rng.unit());
            for (int block = 0; block < 60; ++block) {
                switch (rng.below(12)) { // what the user does between two blocks
                case 0: {
                    auto* c = new AutomationCurve(rng.unit());
                    for (int k = rng.below(10); k > 0; --k)
                        c->add({static_cast<std::int64_t>(rng.below(40)) * 100, rng.unit(),
                                rng.below(3) == 0 ? Shape::Hold : Shape::Linear});
                    delete lane.exchangeCurve(c);
                    break;
                }
                case 1: delete lane.exchangeCurve(nullptr); break;
                case 2: {
                    const std::int64_t to = rng.below(4400);
                    transport.seek(to);
                    ref.position = to;
                    break;
                }
                case 3: {
                    const std::int64_t start = rng.below(3000);
                    const std::int64_t end = start + 64 + rng.below(1000);
                    transport.setLoop(start, end);
                    ref.loopStart = start;
                    ref.loopEnd = end;
                    break;
                }
                case 4:
                    transport.clearLoop();
                    ref.loopStart = ref.loopEnd = 0;
                    break;
                case 5:
                    transport.stop();
                    ref.playing = false;
                    break;
                case 6: lane.setKnobValue(rng.unit()); break;
                default:
                    transport.play();
                    ref.playing = true;
                    break;
                }
                const int n = 1 + rng.below(512);
                const AutomationCurve* curve = lane.getCurve();
                const float knob = lane.getKnobValue();
                playBlock(transport, lane, out.data(), n);
                for (int i = 0; i < n; ++i, ++values)
                    mismatches += out[static_cast<size_t>(i)] == ref.next(curve, knob) ? 0 : 1;
            }
        }
        std::printf("  200 random sessions (seeks, loops, stops, swaps, knob moves), %lld values: %lld differ from\n"
                    "  the reference (expected 0)\n",
                    values, mismatches);
        DAWLAB_CHECK(values > 100000);
        DAWLAB_CHECK(mismatches == 0);
    }

    return dawlab::test::summary("l16_step02");
}

} // namespace step02_check

int main() { return step02_check::run(); }
