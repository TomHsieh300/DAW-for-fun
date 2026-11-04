#pragma once
// check.h - the self-check of Step 3 (automation and the user). Defines main(); included at the END
// of starter/step03.cpp and solution/step03.cpp, so both run exactly the same checks.
//
// Who wins in every mode, what is captured, thin() and punch() by hand and against plain references
// on random data, and a whole recording that is played back.

#include <dawlab/Common.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

// Every allocation in this program is counted: render() must not make any.
namespace step03_check {
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
} // namespace step03_check
void* operator new(std::size_t size) { return step03_check::countedAlloc(size); }
void* operator new[](std::size_t size) { return step03_check::countedAlloc(size); }
void operator delete(void* p) noexcept { step03_check::countedFree(p); }
void operator delete[](void* p) noexcept { step03_check::countedFree(p); }
void operator delete(void* p, std::size_t) noexcept { step03_check::countedFree(p); }
void operator delete[](void* p, std::size_t) noexcept { step03_check::countedFree(p); }

namespace step03_check {

using namespace minidaw;

// A deterministic random generator (the same sequence on every compiler).
struct Lcg {
    std::uint32_t state = 316u;
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

inline AutomationCurve* constantCurve(float value) {
    auto* c = new AutomationCurve();
    c->add({0, value, Shape::Linear});
    return c;
}

// The value of a polyline at a position, written plainly.
inline double onPolyline(const std::vector<AutomationPoint>& p, std::int64_t position) {
    for (size_t i = 0; i + 1 < p.size(); ++i)
        if (position >= p[i].position && position <= p[i + 1].position) {
            const double t = static_cast<double>(position - p[i].position) / static_cast<double>(p[i + 1].position - p[i].position);
            return p[i].value + (static_cast<double>(p[i + 1].value) - p[i].value) * t;
        }
    return -1000.0; // not on it at all
}

inline std::int64_t positionOf(const AutomationCurve& c, int i) {
    return i >= 0 && i < c.size() ? c.points()[static_cast<size_t>(i)].position : -1;
}
inline float valueOf(const AutomationCurve& c, int i) {
    return i >= 0 && i < c.size() ? c.points()[static_cast<size_t>(i)].value : -1.0f;
}

inline int run() {
    std::printf("Step 3: automation and the user (build-along)\n");

    // ---- who wins: the curve is at 0.25, the hand at 0.75 ----------------------------------------
    {
        const char* names[] = {"Read", "Touch", "Latch", "Write"};
        const AutomationMode modes[] = {AutomationMode::Read, AutomationMode::Touch, AutomationMode::Latch,
                                        AutomationMode::Write};
        const float C = 0.25f, H = 0.75f; // the curve, the hand
        const float expected[4][6] = {
            {C, C, C, C, H, C}, // Read
            {C, H, C, C, H, C}, // Touch
            {C, H, H, C, H, C}, // Latch: from the touch until stop
            {H, H, H, C, H, H}, // Write: from play until stop
        };
        std::printf("           untouched touched released stopped  stopped  playing\n"
                    "                                               +touch   again\n");
        for (int m = 0; m < 4; ++m) {
            AutomationLane lane;
            lane.exchangeCurve(constantCurve(C));
            lane.setKnobValue(H);
            lane.setMode(modes[m]);
            MiniTransport transport;
            transport.play();
            float out[128] = {};
            float heard[6] = {};
            playBlock(transport, lane, out, 128);
            heard[0] = out[127];
            lane.touch();
            playBlock(transport, lane, out, 128);
            heard[1] = out[127];
            lane.release();
            playBlock(transport, lane, out, 128);
            heard[2] = out[127];
            transport.stop();
            playBlock(transport, lane, out, 128);
            heard[3] = out[127];
            lane.touch();
            playBlock(transport, lane, out, 128);
            heard[4] = out[127];
            lane.release();
            transport.play();
            playBlock(transport, lane, out, 128);
            heard[5] = out[127];
            bool right = true;
            std::printf("  %-7s", names[m]);
            for (int s = 0; s < 6; ++s) {
                std::printf("  %.2f   ", static_cast<double>(heard[s]));
                right = right && heard[s] == expected[m][s];
            }
            std::printf("\n");
            DAWLAB_CHECK(right);
        }
    }

    // ---- a click between two blocks still latches; leaving Latch mode ends the latch -------------
    {
        AutomationLane lane;
        lane.exchangeCurve(constantCurve(0.25f));
        lane.setKnobValue(0.75f);
        lane.setMode(AutomationMode::Latch);
        MiniTransport transport;
        transport.play();
        float out[128] = {};
        playBlock(transport, lane, out, 128);
        lane.touch();
        lane.release(); // the audio thread never sees touching == true
        playBlock(transport, lane, out, 128);
        const float afterClick = out[127];
        lane.setMode(AutomationMode::Read);
        playBlock(transport, lane, out, 128);
        const float afterRead = out[127];
        lane.setMode(AutomationMode::Latch);
        playBlock(transport, lane, out, 128);
        std::printf("  Latch, a click between blocks: %.2f (expected 0.75). To Read and back to Latch: %.2f, %.2f (expected 0.25, 0.25)\n",
                    static_cast<double>(afterClick), static_cast<double>(afterRead), static_cast<double>(out[127]));
        DAWLAB_CHECK(afterClick == 0.75f);
        DAWLAB_CHECK(afterRead == 0.25f && out[127] == 0.25f);

        // In Touch mode the same click is over before it counts: the curve plays on, nothing is recorded.
        AutomationLane touchLane;
        touchLane.exchangeCurve(constantCurve(0.25f));
        touchLane.setKnobValue(0.75f);
        touchLane.setMode(AutomationMode::Touch);
        playBlock(transport, touchLane, out, 128);
        touchLane.touch();
        touchLane.release();
        playBlock(transport, touchLane, out, 128);
        std::printf("  Touch, a click between blocks: %.2f, %d events (expected 0.25, 0)\n", static_cast<double>(out[127]),
                    touchLane.getCaptureQueue().size());
        DAWLAB_CHECK(out[127] == 0.25f && touchLane.getCaptureQueue().size() == 0);
    }

    // ---- what the audio thread captures ----------------------------------------------------------
    {
        AutomationLane lane;
        lane.exchangeCurve(constantCurve(0.25f));
        lane.setKnobValue(0.5f);
        lane.setMode(AutomationMode::Touch);
        MiniTransport transport;
        transport.setLoop(0, 1000);
        transport.play();
        CaptureQueue& queue = lane.getCaptureQueue();
        float out[512] = {};
        playBlock(transport, lane, out, 512); // 0 .. 512, untouched: nothing
        const int untouched = queue.size();
        lane.touch();
        playBlock(transport, lane, out, 512); // 512 .. 1000, then the wrap: 0 .. 24
        lane.setKnobValue(0.75f);
        playBlock(transport, lane, out, 512); // 24 .. 536
        lane.release();
        playBlock(transport, lane, out, 512); // the end marker at 1048 % 1000
        CaptureEvent e[5];
        int got = 0;
        while (got < 5 && queue.pop(e[got]))
            ++got;
        std::printf("  captured: %d untouched; touched %d events: %lld+%d, %lld+%d, %lld+%d, end marker %d\n"
                    "  (expected 0; 4: 512+488, 0+24, 24+512, end marker 0)\n",
                    untouched, got, static_cast<long long>(e[0].position), e[0].numSamples,
                    static_cast<long long>(e[1].position), e[1].numSamples, static_cast<long long>(e[2].position),
                    e[2].numSamples, e[3].numSamples);
        DAWLAB_CHECK(untouched == 0 && got == 4);
        DAWLAB_CHECK(e[0].position == 512 && e[0].numSamples == 488 && e[0].value == 0.5f);
        DAWLAB_CHECK(e[1].position == 0 && e[1].numSamples == 24 && e[2].position == 24 && e[2].numSamples == 512 &&
                     e[2].value == 0.75f && e[3].numSamples == 0);
    }

    // ---- nothing is captured when stopped, or in Read ----------------------------------------------
    {
        AutomationLane reading, stopped;
        reading.setKnobValue(0.5f);
        stopped.setKnobValue(0.5f);
        stopped.setMode(AutomationMode::Write);
        MiniTransport playing, standing;
        playing.play();
        float out[128] = {};
        reading.touch();
        stopped.touch();
        playBlock(playing, reading, out, 128);
        playBlock(standing, stopped, out, 128);
        std::printf("  a hand in Read, a hand while stopped in Write: %d, %d events (expected 0, 0)\n",
                    reading.getCaptureQueue().size(), stopped.getCaptureQueue().size());
        DAWLAB_CHECK(reading.getCaptureQueue().size() == 0 && stopped.getCaptureQueue().size() == 0);
        stopped.release();
        playBlock(standing, stopped, out, 128);
        DAWLAB_CHECK(stopped.getCaptureQueue().size() == 0); // not even an end marker

        // A stop ends the recording at once, also while the hand is still on the fader.
        AutomationLane held;
        held.setKnobValue(0.5f);
        held.setMode(AutomationMode::Touch);
        held.touch();
        MiniTransport t;
        t.play();
        playBlock(t, held, out, 128);
        t.stop();
        playBlock(t, held, out, 128);
        CaptureEvent event, lastEvent;
        int events = 0;
        while (held.getCaptureQueue().pop(event)) {
            lastEvent = event;
            ++events;
        }
        std::printf("  stop with the hand still on the fader: %d events, the last with %d samples (expected 2, 0)\n", events,
                    lastEvent.numSamples);
        DAWLAB_CHECK(events == 2 && lastEvent.numSamples == 0);
    }

    // ---- thin() by hand ----------------------------------------------------------------------------
    {
        std::vector<AutomationPoint> line, corner;
        for (int i = 0; i <= 100; ++i) {
            line.push_back({i * 512, static_cast<float>(i) / 100.0f, Shape::Linear});
            corner.push_back({i * 512, i <= 40 ? 0.1f : 0.1f + static_cast<float>(i - 40) * 0.01f, Shape::Linear});
        }
        const auto thinLine = thin(line, 0.002f);
        const auto thinCorner = thin(corner, 0.002f);
        const std::vector<AutomationPoint> two = {{0, 0.5f, Shape::Linear}, {10, 0.75f, Shape::Linear}};
        const bool cornerKept = thinCorner.size() == 3 && thinCorner[1].position == 40 * 512;
        std::printf("  thin: a straight line of 101 points: %d left (expected 2); a corner: %d left (expected 3, at %d)\n",
                    static_cast<int>(thinLine.size()), static_cast<int>(thinCorner.size()), 40 * 512);
        DAWLAB_CHECK(thinLine.size() == 2 && thinLine.back().position == 100 * 512);
        DAWLAB_CHECK(cornerKept);
        DAWLAB_CHECK(thin(two, 0.002f).size() == 2);
    }

    // ---- thin() on random recordings: every dropped point is within the tolerance ------------------
    {
        Lcg rng;
        const float tolerance = 0.01f;
        int outside = 0, notSubset = 0, endsLost = 0;
        long long before = 0, after = 0;
        for (int round = 0; round < 200; ++round) {
            std::vector<AutomationPoint> input;
            float value = rng.unit();
            std::int64_t position = rng.below(1000);
            const int count = 3 + rng.below(400);
            for (int i = 0; i < count; ++i) {
                input.push_back({position, value, Shape::Linear});
                position += 1 + rng.below(512);
                value = AutomationCurve::clamp01(value + (rng.unit() - 0.5f) * 0.03f);
            }
            const auto kept = thin(input, tolerance);
            before += count;
            after += static_cast<long long>(kept.size());
            endsLost += !kept.empty() && kept.front().position == input.front().position &&
                                kept.back().position == input.back().position
                            ? 0
                            : 1;
            size_t k = 0;
            for (const AutomationPoint& p : input) {
                outside += std::abs(p.value - onPolyline(kept, p.position)) <= tolerance + 1e-6 ? 0 : 1;
                if (k < kept.size() && kept[k].position == p.position && kept[k].value == p.value)
                    ++k;
            }
            notSubset += k == kept.size() ? 0 : 1;
        }
        std::printf("  thin, 200 random recordings: %lld points -> %lld; %d further than the tolerance, %d rounds with\n"
                    "  invented points, %d with lost ends (expected fewer than a third, 0, 0, 0)\n",
                    before, after, outside, notSubset, endsLost);
        DAWLAB_CHECK(outside == 0 && notSubset == 0 && endsLost == 0);
        DAWLAB_CHECK(after * 3 < before);
    }

    // ---- punch() by hand -----------------------------------------------------------------------------
    {
        AutomationCurve ramp;
        ramp.add({0, 0.0f, Shape::Linear});
        ramp.add({48000, 1.0f, Shape::Linear});
        const std::vector<AutomationPoint> pass = {{12000, 0.875f, Shape::Linear}, {24000, 0.875f, Shape::Linear}};
        const AutomationCurve result = punch(ramp, pass);
        std::printf("  punch 0.875 into a ramp over 12000 .. 24000: %d points; values at 11999, 12000, 23999, 24000, 36000:\n"
                    "  %.4f %.4f %.4f %.4f %.4f (expected 6; 0.2500 0.8750 0.8750 0.5000 0.7500)\n",
                    result.size(), static_cast<double>(result.valueAt(11999)), static_cast<double>(result.valueAt(12000)),
                    static_cast<double>(result.valueAt(23999)), static_cast<double>(result.valueAt(24000)),
                    static_cast<double>(result.valueAt(36000)));
        DAWLAB_CHECK(result.size() == 6 && positionOf(result, 1) == 12000 && positionOf(result, 2) == 12000 &&
                     valueOf(result, 1) == 0.25f && valueOf(result, 2) == 0.875f && valueOf(result, 3) == 0.875f &&
                     valueOf(result, 4) == 0.5f);
        DAWLAB_CHECK(result.valueAt(36000) == 0.75f && std::abs(result.valueAt(11999) - 0.2499792f) < 1e-6f);

        // Old points inside are gone; a Hold going out stays a Hold; a jump at the start keeps its first value.
        AutomationCurve steps;
        steps.add({0, 0.1f, Shape::Linear});
        steps.add({1000, 0.3f, Shape::Linear});
        steps.add({1000, 0.6f, Shape::Linear}); // a jump right where the pass begins
        steps.add({1500, 0.9f, Shape::Linear}); // inside the pass
        steps.add({2000, 0.4f, Shape::Hold});   // inside, its Hold goes on past the pass
        steps.add({4000, 0.8f, Shape::Linear});
        const AutomationCurve s = punch(steps, {{1000, 0.5f, Shape::Linear}, {3000, 0.5f, Shape::Linear}});
        std::printf("  over a jump and a Hold: %d points; at 999, 3000, 3999: %.3f %.3f %.3f (expected 6; 0.300 0.400 0.400)\n",
                    s.size(), static_cast<double>(s.valueAt(999)), static_cast<double>(s.valueAt(3000)),
                    static_cast<double>(s.valueAt(3999)));
        DAWLAB_CHECK(s.size() == 6 && valueOf(s, 1) == 0.3f && std::abs(s.valueAt(999) - 0.3f) < 1e-3f);
        DAWLAB_CHECK(s.valueAt(3000) == 0.4f && s.valueAt(3999) == 0.4f && s.valueAt(4000) == 0.8f);

        AutomationCurve empty(0.6f);
        const AutomationCurve first = punch(empty, pass);
        std::printf("  into an empty curve with default 0.6: at 0, 12000, 24000: %.3f %.3f %.3f (expected 0.600 0.875 0.600)\n",
                    static_cast<double>(first.valueAt(0)), static_cast<double>(first.valueAt(12000)),
                    static_cast<double>(first.valueAt(24000)));
        DAWLAB_CHECK(first.valueAt(0) == 0.6f && first.valueAt(12000) == 0.875f && first.valueAt(24000) == 0.6f);
        DAWLAB_CHECK(first.defaultValue() == 0.6f);
    }

    // ---- punch() on random curves: outside the pass nothing changes, inside it is the pass ------------
    {
        Lcg rng;
        int outsideWrong = 0, insideWrong = 0, unsorted = 0;
        long long values = 0;
        for (int round = 0; round < 300; ++round) {
            AutomationCurve old(rng.unit());
            for (int k = rng.below(12); k > 0; --k)
                old.add({static_cast<std::int64_t>(rng.below(40)) * 100, rng.unit(),
                         rng.below(3) == 0 ? Shape::Hold : Shape::Linear});
            std::vector<AutomationPoint> pass;
            std::int64_t position = 1 + rng.below(3000);
            const std::int64_t from = position;
            for (int k = 2 + rng.below(6); k > 0; --k) {
                pass.push_back({position, rng.unit(), Shape::Linear});
                position += 1 + rng.below(300);
            }
            const std::int64_t to = pass.back().position;
            const AutomationCurve result = punch(old, pass);
            const auto& points = result.points();
            for (size_t i = 1; i < points.size(); ++i)
                unsorted += points[i - 1].position <= points[i].position ? 0 : 1;
            for (std::int64_t x = -50; x < 4300; x += 17, ++values) {
                if (x < from)
                    outsideWrong += std::abs(result.valueAt(x) - old.valueAt(x)) < 3e-6f ? 0 : 1;
                else if (x >= to)
                    outsideWrong += std::abs(result.valueAt(x) - old.valueAt(x)) < 3e-6f ? 0 : 1;
                else
                    insideWrong += std::abs(result.valueAt(x) - onPolyline(pass, x)) < 1e-6 ? 0 : 1;
            }
        }
        std::printf("  punch, 300 random curves, %lld values: %d changed outside the pass, %d wrong inside, %d unsorted\n"
                    "  (expected 0, 0, 0)\n",
                    values, outsideWrong, insideWrong, unsorted);
        DAWLAB_CHECK(outsideWrong == 0 && insideWrong == 0 && unsorted == 0);
    }

    // ---- a whole recording: touch, move, let go, play it back ----------------------------------------
    {
        AutomationCurve master(0.0f); // the UI's copy
        master.add({0, 0.25f, Shape::Linear});
        AutomationLane lane;
        lane.exchangeCurve(new AutomationCurve(master));
        lane.setKnobValue(0.25f);
        lane.setMode(AutomationMode::Touch);
        MiniTransport transport;
        transport.play();
        Recorder recorder;
        const int blocks = 280, blockSize = 512, touchAt = 50, releaseAt = 200;
        std::vector<float> first(static_cast<size_t>(blocks * blockSize));
        std::int64_t touchedFrom = 0, touchedTo = 0;
        int publishes = 0, recordingChecks = 0;
        for (int b = 0; b < blocks; ++b) {
            if (b == touchAt) {
                lane.touch();
                touchedFrom = transport.position();
            }
            if (b >= touchAt && b < releaseAt) { // the hand: a slow wave around 0.5
                const double seconds = static_cast<double>(b - touchAt) * blockSize / 48000.0;
                lane.setKnobValue(0.25f + 0.25f * static_cast<float>(1.0 - std::cos(6.283185 * seconds * 0.7)));
            }
            if (b == releaseAt) {
                lane.release();
                touchedTo = transport.position();
            }
            playBlock(transport, lane, first.data() + b * blockSize, blockSize);
            if (b % 3 == 2) // the UI's timer
                if (recorder.collect(lane.getCaptureQueue(), master)) {
                    delete lane.exchangeCurve(new AutomationCurve(master));
                    ++publishes;
                }
            recordingChecks += b > touchAt + 3 && b < releaseAt && !recorder.isRecording() ? 1 : 0;
        }
        std::printf("  recorded 150 blocks of a moving hand: %d publish, %d points in the curve (expected 1, fewer than 50)\n",
                    publishes, master.size());
        DAWLAB_CHECK(publishes == 1 && recordingChecks == 0);
        DAWLAB_CHECK(master.size() >= 6 && master.size() < 50);

        // Playing it back in Read, from the start, without a hand.
        lane.setMode(AutomationMode::Read);
        lane.setKnobValue(0.0f); // the hand is gone
        transport.seek(0);
        std::vector<float> second(first.size());
        for (int b = 0; b < blocks; ++b)
            playBlock(transport, lane, second.data() + b * blockSize, blockSize);
        double worst = 0.0;
        for (size_t i = 64; i < first.size(); ++i) { // past the seek's short ramp
            const auto x = static_cast<std::int64_t>(i);
            if (x < touchedFrom + 64 || (x >= touchedTo && x < touchedTo + 64))
                continue;
            worst = std::max(worst, static_cast<double>(std::abs(first[i] - second[i])));
        }
        const float beforeTouch = master.valueAt(touchedFrom - 1), afterRelease = master.valueAt(touchedTo + 100);
        std::printf("  played back in Read: at most %.4f from what was heard (expected less than 0.02);\n"
                    "  before the touch %.2f, after the release %.2f (expected 0.25, 0.25)\n",
                    worst, static_cast<double>(beforeTouch), static_cast<double>(afterRelease));
        DAWLAB_CHECK(worst < 0.02);
        DAWLAB_CHECK(beforeTouch == 0.25f && afterRelease == 0.25f);
    }

    // ---- a loop makes two passes; a seek ends a pass ----------------------------------------------------
    {
        AutomationCurve master;
        master.add({0, 0.25f, Shape::Linear});
        AutomationLane lane;
        lane.setKnobValue(0.75f);
        lane.setMode(AutomationMode::Write);
        MiniTransport transport;
        transport.seek(2000);
        transport.setLoop(0, 3000);
        transport.play();
        float out[512];
        for (int b = 0; b < 4; ++b) // 2000 .. 3000, then 0 .. 1048
            playBlock(transport, lane, out, 512);
        Recorder recorder;
        const bool firstCollect = recorder.collect(lane.getCaptureQueue(), master); // the first pass ended at the wrap
        const float inFirstPass = master.valueAt(2500);
        const float notYet = master.valueAt(500);
        transport.seek(10000); // a seek ends the second pass
        playBlock(transport, lane, out, 512);
        recorder.collect(lane.getCaptureQueue(), master);
        const float secondPass = master.valueAt(500);
        const float gap = master.valueAt(1500);
        transport.stop();
        playBlock(transport, lane, out, 512);
        const bool lastCollect = recorder.collect(lane.getCaptureQueue(), master);
        std::printf("  Write over a loop and a seek: after the wrap 2500: %.2f, 500: %.2f (expected 0.75, 0.25);\n"
                    "  after the seek 500: %.2f, 1500: %.2f (expected 0.75, 0.25); after stop 10100: %.2f, recording %s\n",
                    static_cast<double>(inFirstPass), static_cast<double>(notYet), static_cast<double>(secondPass),
                    static_cast<double>(gap), static_cast<double>(master.valueAt(10100)),
                    recorder.isRecording() ? "yes" : "no");
        DAWLAB_CHECK(firstCollect && inFirstPass == 0.75f && notYet == 0.25f);
        DAWLAB_CHECK(secondPass == 0.75f && gap == 0.25f);
        DAWLAB_CHECK(lastCollect && master.valueAt(10100) == 0.75f && !recorder.isRecording());
    }

    // ---- a queue nobody empties: events are lost, nothing else ------------------------------------------
    {
        AutomationCurve master;
        master.add({0, 0.25f, Shape::Linear});
        AutomationLane lane;
        lane.setKnobValue(0.75f);
        lane.setMode(AutomationMode::Write);
        MiniTransport transport;
        transport.play();
        float out[64];
        const int full = CaptureQueue::kCapacity;
        for (int b = 0; b < full + 100; ++b)
            playBlock(transport, lane, out, 64);
        const long long dropped = lane.getCaptureQueue().getDropped();
        Recorder recorder;
        recorder.collect(lane.getCaptureQueue(), master);
        transport.stop(); // the end marker
        playBlock(transport, lane, out, 64);
        recorder.collect(lane.getCaptureQueue(), master);
        const std::int64_t resumed = (static_cast<std::int64_t>(full) + 100) * 64;
        std::printf("  the UI hangs for %d blocks: %lld events lost (expected 100); the kept part at 1000: %.2f; the lost part: %.2f\n"
                    "  (expected 0.75, 0.25), recording %s (expected no)\n",
                    full + 100, dropped, static_cast<double>(master.valueAt(1000)),
                    static_cast<double>(master.valueAt(resumed - 100)), recorder.isRecording() ? "yes" : "no");
        DAWLAB_CHECK(dropped == 100);
        DAWLAB_CHECK(master.valueAt(1000) == 0.75f && master.valueAt(resumed - 100) == 0.25f && !recorder.isRecording());

        // The end marker itself must not get lost when the queue is full at that moment.
        AutomationLane busy;
        busy.setKnobValue(0.75f);
        busy.setMode(AutomationMode::Write);
        MiniTransport t2;
        t2.play();
        for (int b = 0; b < full; ++b)
            playBlock(t2, busy, out, 64);
        t2.stop();
        playBlock(t2, busy, out, 64); // the marker does not fit
        CaptureEvent e;
        for (int k = 0; k < full; ++k)
            busy.getCaptureQueue().pop(e);
        const int emptied = busy.getCaptureQueue().size();
        t2.play();
        playBlock(t2, busy, out, 64); // tried again, and the next recording starts in the same block
        const bool markerCame = busy.getCaptureQueue().pop(e) && e.numSamples == 0;
        const bool thenEvent = busy.getCaptureQueue().pop(e) && e.numSamples == 64;
        std::printf("  an end marker that does not fit comes one block later, before the next event: %s (expected yes)\n",
                    markerCame && thenEvent ? "yes" : "no");
        DAWLAB_CHECK(emptied == 0 && markerCame && thenEvent);
    }

    // ---- nothing on the audio path allocates ------------------------------------------------------------
    {
        AutomationLane lane;
        lane.exchangeCurve(constantCurve(0.25f));
        lane.setMode(AutomationMode::Latch);
        MiniTransport transport;
        transport.setLoop(100, 700);
        transport.play();
        float out[512];
        CaptureEvent e;
        const long long before = allocations;
        for (int i = 0; i < 2000; ++i) {
            if (i % 50 == 10)
                lane.touch();
            if (i % 50 == 30)
                lane.release();
            if (i % 200 == 199)
                transport.stop();
            if (i % 200 == 0)
                transport.play();
            playBlock(transport, lane, out, 512);
            while (lane.getCaptureQueue().pop(e)) {
            }
        }
        const long long made = allocations - before;
        std::printf("  2000 blocks with touches, stops and a loop: %lld allocations (expected 0)\n", made);
        DAWLAB_CHECK(made == 0);
    }

    return dawlab::test::summary("l16_step03");
}

} // namespace step03_check

int main() { return step03_check::run(); }
