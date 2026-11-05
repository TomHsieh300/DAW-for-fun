#pragma once
// check.h - the self-check of Step 2 (where the take belongs). Defines main(); included at the END of
// starter/step02.cpp and solution/step02.cpp, so both run exactly the same checks.
//
// The heart of it is a simulated loop-back: a click track goes through the mix latency and the
// output latency, comes back through a cable and the input latency, and is recorded. Every click has
// to land exactly on its own position, for every latency, block size and start position. Ends with a
// real audio thread next to the UI thread; build it with -fsanitize=thread as well.

#include <dawlab/Common.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <thread>
#include <vector>

namespace step02_check {

using namespace minidaw;

using ull = unsigned long long;

constexpr int kRingFrames = 1 << 16;

// A delay of a fixed number of samples.
class Delay {
public:
    explicit Delay(int samples) : line_(static_cast<std::size_t>(samples), 0.0f) {}
    float process(float in) {
        if (line_.empty())
            return in;
        line_.push_back(in);
        const float out = line_.front();
        line_.pop_front();
        return out;
    }

private:
    std::deque<float> line_;
};

// The song, the device and the cable. Real latencies here may differ from what the device reports.
class Loopback {
public:
    Loopback(int realInput, int realOutput, int realMix, std::vector<std::int64_t> clicks)
        : toInput_(realInput), toOutput_(realOutput), mix_(realMix), clicks_(std::move(clicks)) {}

    // One device block at the given transport position: fills `input` with what came back.
    void block(std::int64_t position, float* input, int n) {
        for (int i = 0; i < n; ++i) {
            const float rendered = isClick(position + i) ? 1.0f : 0.0f;
            input[i] = toInput_.process(toOutput_.process(mix_.process(rendered)));
        }
    }

private:
    bool isClick(std::int64_t position) const {
        for (std::int64_t c : clicks_)
            if (c == position)
                return true;
        return false;
    }
    Delay toInput_, toOutput_, mix_;
    std::vector<std::int64_t> clicks_;
};

// Plays the transport for `blocks` blocks of size n from position `from` on, arming at block `armAt`,
// and returns the finished take.
struct Session {
    Recorder recorder;
    std::vector<float> take;
    Placement placement;
};

inline void record(Session& s, Loopback& loop, const Latencies& reported, int mixLatency, std::int64_t from, int n,
                   int blocks, int armAt) {
    std::vector<float> input(static_cast<std::size_t>(n));
    const float* in[1] = {input.data()};
    Take take(1);
    std::int64_t position = 0;
    // The song plays from 0, so that the delay lines are full when recording begins.
    for (int i = 0; position < from; ++i) {
        const int m = static_cast<int>(std::min<std::int64_t>(n, from - position));
        loop.block(position, input.data(), m);
        s.recorder.process(in, 1, m, position, true, mixLatency);
        position += m;
    }
    for (int i = 0; i < blocks; ++i) {
        if (i == armAt)
            s.recorder.start(reported, 1, kRingFrames);
        loop.block(position, input.data(), n);
        s.recorder.process(in, 1, n, position, true, mixLatency);
        position += n;
        take.collect(s.recorder.ring());
    }
    s.recorder.stop();
    s.recorder.process(in, 1, n, position, true, mixLatency);
    take.collect(s.recorder.ring());
    s.take = take.channels()[0];
    s.placement = place(s.recorder.stampedPosition(), s.recorder.latencies(), static_cast<std::int64_t>(s.take.size()));
}

// The timeline positions where the placed take has a click.
inline std::vector<std::int64_t> clicksOnTimeline(const Session& s) {
    std::vector<std::int64_t> found;
    for (std::int64_t i = 0; i < s.placement.numFrames; ++i)
        if (s.take[static_cast<std::size_t>(s.placement.skipFrames + i)] != 0.0f)
            found.push_back(s.placement.timelineStart + i);
    return found;
}

// ---- place() ------------------------------------------------------------------------------------

inline void checkPlace() {
    const Latencies l{100, 200, 300, 0};
    DAWLAB_CHECK(l.total() == 600);

    Placement p = place(10000, l, 5000);
    DAWLAB_CHECK(p.timelineStart == 9400 && p.skipFrames == 0 && p.numFrames == 5000);

    // Started at the song's beginning: the first frames are from before it.
    p = place(0, l, 5000);
    DAWLAB_CHECK(p.timelineStart == 0 && p.skipFrames == 600 && p.numFrames == 4400);
    p = place(600, l, 5000);
    DAWLAB_CHECK(p.timelineStart == 0 && p.skipFrames == 0 && p.numFrames == 5000);
    p = place(599, l, 5000);
    DAWLAB_CHECK(p.timelineStart == 0 && p.skipFrames == 1 && p.numFrames == 4999);

    // A take that lies entirely before the song leaves nothing.
    p = place(0, l, 200);
    DAWLAB_CHECK(p.timelineStart == 0 && p.skipFrames == 200 && p.numFrames == 0);

    // A negative trim: the driver reports too much.
    p = place(1000, Latencies{100, 200, 300, -50}, 10);
    DAWLAB_CHECK(p.timelineStart == 450);

    // A take of an hour at 192 kHz does not overflow anything.
    p = place(691200000000LL, l, 691200000LL);
    DAWLAB_CHECK(p.timelineStart == 691199999400LL && p.numFrames == 691200000LL);
}

// ---- the loop-back ------------------------------------------------------------------------------

inline void checkClicksLand() {
    const std::vector<std::int64_t> clicks = {30000, 41000, 52345};
    Latencies reported{240, 480, 752, 0};
    Loopback loop(240, 480, 752, clicks);
    Session s;
    record(s, loop, reported, 752, 20000, 512, 80, 0);

    DAWLAB_CHECK(s.recorder.isIdle() && s.recorder.wasStarted());
    DAWLAB_CHECK(s.recorder.stampedPosition() == 20000);
    DAWLAB_CHECK(s.recorder.latencies().mix == 752); // the audio thread filled it in
    const std::vector<std::int64_t> found = clicksOnTimeline(s);
    DAWLAB_CHECK(found == clicks);

    // Without the correction every click would be this late, which is what the step is about.
    std::int64_t uncorrected = -1;
    for (std::size_t i = 0; i < s.take.size() && uncorrected < 0; ++i)
        if (s.take[i] != 0.0f)
            uncorrected = s.recorder.stampedPosition() + static_cast<std::int64_t>(i) - clicks[0];
    std::printf("  without the correction the first click lands %lld samples late (%.1f ms at 48 kHz)\n",
                static_cast<long long>(uncorrected), static_cast<double>(uncorrected) / 48.0);
    DAWLAB_CHECK(uncorrected == 240 + 480 + 752);
}

// Recording from the song's very start: the frames from before it are skipped.
inline void checkFromZero() {
    Latencies reported{100, 100, 56, 0};
    Loopback loop(100, 100, 56, {0, 5000});
    Session s;
    record(s, loop, reported, 56, 0, 256, 40, 0);
    DAWLAB_CHECK(s.placement.timelineStart == 0 && s.placement.skipFrames == 256);
    DAWLAB_CHECK(clicksOnTimeline(s) == (std::vector<std::int64_t>{0, 5000}));
}

// The driver reports too little. Measure the round trip with a click, and trim.
inline void checkMeasuredTrim() {
    const int realInput = 277;
    Latencies reported{240, 480, 0, 0};
    {
        Loopback loop(realInput, 480, 0, {25000});
        Session s;
        record(s, loop, reported, 0, 20000, 256, 60, 0);
        DAWLAB_CHECK(clicksOnTimeline(s) == (std::vector<std::int64_t>{25037})); // 37 samples late

        const std::int64_t measured = measureRoundTrip(s.take.data(), static_cast<std::int64_t>(s.take.size()),
                                                       s.recorder.stampedPosition(), 25000);
        std::printf("  the driver reports %lld samples, the click measures %lld: trim %lld\n",
                    static_cast<long long>(reported.total()), static_cast<long long>(measured),
                    static_cast<long long>(measured - reported.total()));
        DAWLAB_CHECK(measured == realInput + 480);
        reported.trim = static_cast<int>(measured - reported.total());
    }
    Loopback loop(realInput, 480, 0, {25000});
    Session s;
    record(s, loop, reported, 0, 20000, 256, 60, 0);
    DAWLAB_CHECK(clicksOnTimeline(s) == (std::vector<std::int64_t>{25000})); // and now it is right
}

inline void checkMeasure() {
    std::vector<float> take(1000, 0.0f);
    DAWLAB_CHECK(measureRoundTrip(take.data(), 1000, 5000, 5000) == -1); // silence: no click came back

    // A soft noise floor, a click with a slow start and ringing behind it.
    for (std::size_t i = 0; i < take.size(); ++i)
        take[i] = i % 2 == 0 ? 0.001f : -0.001f;
    DAWLAB_CHECK(measureRoundTrip(take.data(), 1000, 5000, 5000) == -1); // only noise
    take[300] = 0.1f;
    take[301] = -0.5f;
    take[302] = 0.8f;
    take[303] = -0.6f;
    take[304] = 0.2f;
    DAWLAB_CHECK(measureRoundTrip(take.data(), 1000, 5000, 5000) == 301); // the first frame at half the peak
    DAWLAB_CHECK(measureRoundTrip(take.data(), 1000, 5000, 5100) == 201); // measured from the click
    DAWLAB_CHECK(measureRoundTrip(take.data(), 1000, 5000, 5400) == -1);  // before it was sent

    // A click may come back upside down: the peak is its absolute value.
    std::vector<float> inverted(1000, 0.0f);
    inverted[399] = 0.3f;
    inverted[400] = -0.9f;
    DAWLAB_CHECK(measureRoundTrip(inverted.data(), 1000, 5000, 5000) == 400);
}

// ---- the recorder's states ----------------------------------------------------------------------

inline void checkStates() {
    Recorder r;
    float samples[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    const float* in[1] = {samples};
    Take take(1);

    DAWLAB_CHECK(r.isIdle());
    r.process(in, 1, 8, 0, true, 0); // nothing happens while idle
    DAWLAB_CHECK(r.isIdle());

    DAWLAB_CHECK(r.start({}, 1, 64));
    DAWLAB_CHECK(r.state() == Recorder::State::Armed);
    DAWLAB_CHECK(!r.start({}, 1, 64)); // only from Idle

    // Armed and stopped: it waits, and stamps the first block that plays.
    r.process(in, 1, 8, 100, false, 7);
    DAWLAB_CHECK(r.state() == Recorder::State::Armed);
    r.process(in, 1, 8, 100, true, 7);
    DAWLAB_CHECK(r.state() == Recorder::State::Recording);
    DAWLAB_CHECK(r.stampedPosition() == 100 && r.wasStarted());

    r.process(in, 1, 8, 108, true, 9);
    DAWLAB_CHECK(r.latencies().mix == 7); // the latency stays as stamped when the mix changes later

    r.stop();
    DAWLAB_CHECK(r.state() == Recorder::State::Stopping);
    DAWLAB_CHECK(!r.isIdle()); // the audio thread may still be inside process()
    r.process(in, 1, 8, 116, true, 9);
    DAWLAB_CHECK(r.isIdle());
    take.collect(r.ring());
    DAWLAB_CHECK(take.numFrames() == 16); // the block after stop() does not belong to the take
    DAWLAB_CHECK(take.numFrames() == 16 && take.channels()[0][8] == 1.0f && take.channels()[0][15] == 8.0f);

    // stop() while armed: nothing was recorded. The audio thread confirms this too.
    DAWLAB_CHECK(r.start({}, 1, 64));
    r.stop();
    DAWLAB_CHECK(r.state() == Recorder::State::Stopping);
    r.process(in, 1, 8, 200, true, 0);
    DAWLAB_CHECK(r.isIdle() && !r.wasStarted());

    // A new take starts empty.
    DAWLAB_CHECK(r.start({}, 1, 64));
    r.process(in, 1, 8, 500, true, 0);
    DAWLAB_CHECK(r.stampedPosition() == 500);
    r.stop();
    r.process(in, 1, 8, 508, true, 0);
    Take second(1);
    second.collect(r.ring());
    DAWLAB_CHECK(second.numFrames() == 8);

    // The transport stops: the take ends, without stop().
    DAWLAB_CHECK(r.start({}, 1, 64));
    r.process(in, 1, 8, 0, true, 0);
    r.process(in, 1, 8, 8, false, 0);
    DAWLAB_CHECK(r.isIdle());
    Take third(1);
    third.collect(r.ring());
    DAWLAB_CHECK(third.numFrames() == 8);

    // The transport jumps: a take is one straight line, so it ends, and the jumped block is not in it.
    DAWLAB_CHECK(r.start({}, 1, 64));
    r.process(in, 1, 8, 0, true, 0);
    r.process(in, 1, 8, 8, true, 0);
    r.process(in, 1, 8, 4000, true, 0);
    DAWLAB_CHECK(r.isIdle());
    Take fourth(1);
    fourth.collect(r.ring());
    DAWLAB_CHECK(fourth.numFrames() == 16);
}

// A block dropped at the very start must not move the take: the stamp is taken before the push.
inline void checkFirstBlockDropped() {
    Recorder r;
    std::vector<float> a(32, 1.0f), b(8, 2.0f);
    const float* inA[1] = {a.data()};
    const float* inB[1] = {b.data()};
    DAWLAB_CHECK(r.start({}, 1, 16));
    r.process(inA, 1, 32, 1000, true, 0); // bigger than the ring: dropped
    r.process(inB, 1, 8, 1032, true, 0);
    r.stop();
    r.process(inB, 1, 8, 1040, true, 0);
    Take take(1);
    take.collect(r.ring());
    DAWLAB_CHECK(r.stampedPosition() == 1000);
    DAWLAB_CHECK(take.numFrames() == 40 && take.channels()[0][31] == 0.0f && take.channels()[0][32] == 2.0f);
}

// ---- random sessions ----------------------------------------------------------------------------

struct Lcg {
    std::uint32_t state = 218u;
    std::uint32_t next() noexcept {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    }
    int below(int n) noexcept { return n <= 0 ? 0 : static_cast<int>(next() % static_cast<std::uint32_t>(n)); }
};

inline void checkRandom() {
    Lcg random;
    int wrong = 0;
    std::uint64_t clicksChecked = 0;
    for (int session = 0; session < 300; ++session) {
        const int input = random.below(2000), output = random.below(2000), mix = random.below(4000);
        const int blockSizes[] = {16, 64, 128, 441, 512, 1024, 4096};
        const int n = blockSizes[random.below(7)];
        const std::int64_t from = random.below(8000);
        const int blocks = 2 + 40000 / n;
        const int arm = random.below(blocks / 2);

        // The clicks the take covers: after the first frame's position, before the last one's.
        std::vector<std::int64_t> clicks;
        std::int64_t c = 200 + random.below(300);
        while (clicks.size() < 8) {
            clicks.push_back(c);
            c += 1 + random.below(6000);
        }
        Loopback loop(input, output, mix, clicks);
        Session s;
        record(s, loop, Latencies{input, output, 0, 0}, mix, from, n, blocks, arm);

        // What the take could have caught: the timeline covered by the placed frames.
        std::vector<std::int64_t> expected;
        for (std::int64_t k : clicks)
            if (k >= s.placement.timelineStart && k < s.placement.timelineStart + s.placement.numFrames)
                expected.push_back(k);
        clicksChecked += expected.size();
        wrong += clicksOnTimeline(s) == expected ? 0 : 1;
    }
    std::printf("  300 random latencies, block sizes and starts, %llu clicks: %d sessions wrong (expected 0)\n",
                static_cast<ull>(clicksChecked), wrong);
    DAWLAB_CHECK(wrong == 0);
    DAWLAB_CHECK(clicksChecked > 300);
}

// ---- two real threads ---------------------------------------------------------------------------

// The audio thread runs all the time. The UI thread starts and stops takes whenever it likes, and
// empties the ring. Whatever the moment, the clicks land on their positions.
//
// A real device delivers audio at the speed of the clock, and the UI thread easily keeps up. Here the
// audio thread stays at most `kAhead` frames ahead of what the UI thread has seen. Without that it can
// outrun collect() for as long as it likes, and the take grows until the memory runs out.
//
// Three kinds of takes, because emptying the ring also synchronizes the two threads and could hide a
// missing release on the state: stopped at once (it may never start), recorded without emptying the
// ring until the end, and emptied all the time. The stamp is always read before the last collect().
// The test's own counters are relaxed for the same reason: they must not synchronize anything.
inline void checkThreads() {
    const int kInput = 123, kOutput = 234, kMix = 345, kBlock = 64, kEvery = 1000;
    const int kTotal = kInput + kOutput + kMix;
    const std::int64_t kAhead = 8192;
    Recorder recorder;
    std::atomic<std::int64_t> transport{0}, seen{0};
    std::atomic<bool> quit{false};

    std::thread audio([&] {
        std::vector<float> input(kBlock);
        const float* in[1] = {input.data()};
        std::int64_t position = 0;
        while (!quit.load(std::memory_order_acquire)) {
            for (int i = 0; i < kBlock; ++i) {
                const std::int64_t arrival = position + i - kTotal; // where this frame was played along with
                input[static_cast<std::size_t>(i)] = arrival > 0 && arrival % kEvery == 0 ? 1.0f : 0.0f;
            }
            recorder.process(in, 1, kBlock, position, true, kMix);
            position += kBlock;
            transport.store(position, std::memory_order_relaxed);
            while (position - seen.load(std::memory_order_relaxed) > kAhead && !quit.load(std::memory_order_acquire))
                std::this_thread::yield();
        }
    });

    const auto giveUp = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    auto running = [&] { return std::chrono::steady_clock::now() < giveUp; };
    int takes = 0, emptyTakes = 0;
    std::uint64_t clicks = 0, wrong = 0;
    auto keepSeen = [&] {
        seen.store(transport.load(std::memory_order_relaxed), std::memory_order_relaxed);
        std::this_thread::yield();
    };
    for (int round = 0; round < 600 && running(); ++round) {
        if (!recorder.start(Latencies{kInput, kOutput, 0, 0}, 1, 1 << 15)) {
            ++wrong;
            break;
        }
        const int kind = round % 3;
        Take take(1);
        const std::int64_t until = transport.load(std::memory_order_relaxed) + kEvery * 3 + round;
        while (kind != 0 && transport.load(std::memory_order_relaxed) < until && running()) {
            if (kind == 2)
                take.collect(recorder.ring());
            keepSeen();
        }
        recorder.stop();
        while (!recorder.isIdle() && running())
            keepSeen();
        const bool started = recorder.wasStarted();
        const std::int64_t stamped = recorder.stampedPosition();
        const Latencies latencies = recorder.latencies();
        take.collect(recorder.ring());
        keepSeen();

        // A take that began has at least one block, and one that did not begin has none.
        wrong += started == (take.numFrames() > 0) ? 0 : 1;
        if (!started) {
            ++emptyTakes;
            continue;
        }
        ++takes;
        const auto& samples = take.channels()[0];
        const Placement p = place(stamped, latencies, static_cast<std::int64_t>(samples.size()));
        for (std::int64_t i = 0; i < p.numFrames; ++i) {
            if (samples[static_cast<std::size_t>(p.skipFrames + i)] != 0.0f) {
                ++clicks;
                wrong += (p.timelineStart + i) % kEvery == 0 ? 0 : 1;
            }
        }
    }
    quit.store(true, std::memory_order_release);
    audio.join();

    std::printf("  two threads: %d takes (%d never started), %llu clicks, %llu in the wrong place (expected 0)\n", takes,
                emptyTakes, static_cast<ull>(clicks), static_cast<ull>(wrong));
    DAWLAB_CHECK(takes > 0);
    DAWLAB_CHECK(clicks > 0);
    DAWLAB_CHECK(wrong == 0);
}

} // namespace step02_check

int main() {
    using namespace step02_check;
    std::printf("Step 2: where the take belongs\n");
    checkPlace();
    checkMeasure();
    checkStates();
    if (dawlab::test::counters().failures > 0)
        return dawlab::test::summary("l17_step02"); // the rest needs a recorder that works
    checkFirstBlockDropped();
    checkClicksLand();
    checkFromZero();
    checkMeasuredTrim();
    checkRandom();
    checkThreads();
    return dawlab::test::summary("l17_step02");
}
