#pragma once
// check.h - the self-check of Step 5 (input monitoring). Defines main(); included at the END of
// starter/step05.cpp and solution/step05.cpp, so both run exactly the same checks.
//
// The heart of it: a microphone near a speaker, simulated. What the monitor plays comes back through
// the room to the input, a little louder or a little softer. Ends with real threads; build it with
// -fsanitize=thread as well.

#include <dawlab/Common.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <random>
#include <thread>
#include <vector>

namespace step05_check {

using namespace minidaw;

constexpr int kRate = 48000;
constexpr int kRampFrames = kRate * 5 / 1000;
constexpr int kWindow = kRate / 20;

class Delay {
public:
    explicit Delay(std::int64_t samples) : line_(static_cast<std::size_t>(samples), 0.0f) {}
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

// One mono input to one mono output, in one call. The output starts from `mix`.
float* monoBlock(Monitor& monitor, const std::vector<float>& input, std::vector<float>& output,
                 const std::vector<TimelineSegment>& segments) {
    const float* inputs[] = {input.data()};
    float* outputs[] = {output.data()};
    monitor.process(inputs, 1, outputs, 1, segments.data(), static_cast<int>(segments.size()), static_cast<int>(input.size()));
    return output.data();
}

void checkLatency() {
    const Latencies l{240, 480, 752, 11};
    DAWLAB_CHECK(monitorLatency(l, MonitorPath::Direct) == 720);
    DAWLAB_CHECK(monitorLatency(l, MonitorPath::ThroughInserts) == 1472);
    DAWLAB_CHECK(monitorLatency(Latencies{0, 0, 0, 0}, MonitorPath::ThroughInserts) == 0);
}

void checkWants() {
    const TimeWindow window{1000, 2000};
    DAWLAB_CHECK(!wantsInput(MonitorMode::Off, true, 1500, window));
    DAWLAB_CHECK(!wantsInput(MonitorMode::Off, false, 1500, window));
    DAWLAB_CHECK(wantsInput(MonitorMode::Always, true, 0, window));
    DAWLAB_CHECK(wantsInput(MonitorMode::Always, false, 0, window));
    DAWLAB_CHECK(wantsInput(MonitorMode::Auto, false, 0, window));    // stopped: the musician hears themselves
    DAWLAB_CHECK(!wantsInput(MonitorMode::Auto, true, 999, window));  // before the punch: what is on the track
    DAWLAB_CHECK(wantsInput(MonitorMode::Auto, true, 1000, window));  // exact
    DAWLAB_CHECK(wantsInput(MonitorMode::Auto, true, 1999, window));
    DAWLAB_CHECK(!wantsInput(MonitorMode::Auto, true, 2000, window)); // exact
    DAWLAB_CHECK(wantsInput(MonitorMode::Auto, true, 0, TimeWindow{}));
}

// Feeds a whole window with this peak, one frame in the middle of silence.
void window(FeedbackGuard& guard, float peak) {
    for (int i = 0; i < kWindow; ++i)
        guard.listen(i == kWindow / 2 ? peak : 0.0f);
}

void checkGuard() {
    FeedbackGuard guard;
    guard.prepare(kRate);
    DAWLAB_CHECK(!guard.tripped());

    // Growing: six windows in a row, and not five.
    float peak = 0.1f;
    for (int i = 0; i < 5; ++i, peak *= 1.25f)
        window(guard, peak);
    DAWLAB_CHECK(!guard.tripped());
    window(guard, peak);
    DAWLAB_CHECK(guard.tripped());

    // It stays tripped through silence, until it is reset.
    for (int i = 0; i < 100; ++i)
        window(guard, 0.0f);
    DAWLAB_CHECK(guard.tripped());
    guard.reset();
    DAWLAB_CHECK(!guard.tripped());

    // Five, a pause of one window (not growing), and five again: not six in a row.
    guard.reset();
    peak = 0.1f;
    for (int i = 0; i < 5; ++i, peak *= 1.25f)
        window(guard, peak);
    window(guard, peak * 0.5f);
    peak = 0.12f;
    for (int i = 0; i < 5; ++i, peak *= 1.25f)
        window(guard, peak);
    DAWLAB_CHECK(!guard.tripped());

    // Too soft to count, however fast it grows.
    guard.reset();
    peak = 0.0001f;
    for (int i = 0; i < 10; ++i, peak *= 1.5f)
        window(guard, peak);
    DAWLAB_CHECK(!guard.tripped());

    // Growing too slowly to count.
    guard.reset();
    peak = 0.1f;
    for (int i = 0; i < 12; ++i, peak *= 1.15f)
        window(guard, peak);
    DAWLAB_CHECK(!guard.tripped());

    // Pinned: five windows, and not four.
    guard.reset();
    for (int i = 0; i < 4; ++i)
        window(guard, 0.96f);
    DAWLAB_CHECK(!guard.tripped());
    window(guard, 0.96f);
    DAWLAB_CHECK(guard.tripped());

    // Just below the top, for as long as it likes: a loud musician.
    guard.reset();
    for (int i = 0; i < 400; ++i)
        window(guard, 0.94f);
    DAWLAB_CHECK(!guard.tripped());

    // Hits that reach the top, with a window between them.
    guard.reset();
    for (int i = 0; i < 400; ++i)
        window(guard, i % 3 == 2 ? 0.2f : 1.0f);
    DAWLAB_CHECK(!guard.tripped());

    // The windows follow the sample rate.
    FeedbackGuard fast;
    fast.prepare(96000);
    for (int i = 0; i < 5 * kWindow; ++i)
        fast.listen(1.0f); // 5 windows at 48 kHz, 2.5 at 96 kHz
    DAWLAB_CHECK(!fast.tripped());
    for (int i = 0; i < 5 * kWindow; ++i)
        fast.listen(1.0f);
    DAWLAB_CHECK(fast.tripped());

    // A window is exactly 50 ms: the fifth one ends on this frame, and not before.
    guard.reset();
    for (int i = 0; i < 5 * kWindow - 1; ++i)
        guard.listen(1.0f);
    DAWLAB_CHECK(!guard.tripped());
    guard.listen(1.0f);
    DAWLAB_CHECK(guard.tripped());

    // The peak of a window is its highest frame, wherever it is.
    guard.reset();
    for (int w = 0; w < 5; ++w) {
        guard.listen(1.0f);
        for (int i = 1; i < kWindow; ++i)
            guard.listen(0.0f);
    }
    DAWLAB_CHECK(guard.tripped());
}

void checkAdds() {
    Monitor monitor;
    monitor.prepare(kRate);
    monitor.setMode(MonitorMode::Always);
    monitor.setLevel(0.5f);
    constexpr int n = 2000;
    std::vector<float> in(n, 0.8f), left(n, 0.25f), right(n, 0.25f);

    // Into the mix, and from mono to stereo.
    {
        const float* inputs[] = {in.data()};
        float* outputs[] = {left.data(), right.data()};
        monitor.process(inputs, 1, outputs, 2, nullptr, 0, n);
    }
    DAWLAB_CHECK(std::abs(left[0] - (0.25f + 0.8f / kRampFrames)) < 1e-5f); // the ramp's first step
    DAWLAB_CHECK(std::abs(left[n - 1] - 0.65f) < 1e-5f);
    DAWLAB_CHECK(std::abs(right[n - 1] - 0.65f) < 1e-5f);
    DAWLAB_CHECK(std::abs(monitor.gain() - 0.5f) < 1e-6f);

    // Every level change is a ramp too: no step is bigger than one ramp step.
    {
        Monitor m;
        m.prepare(kRate);
        m.setMode(MonitorMode::Always);
        m.setLevel(1.0f);
        std::vector<float> one(n, 1.0f), out(n, 0.0f);
        monoBlock(m, one, out, {});
        std::fill(out.begin(), out.end(), 0.0f);
        m.setLevel(0.1f);
        monoBlock(m, one, out, {});
        float largest = 0.0f;
        for (int i = 1; i < n; ++i)
            largest = std::max(largest, std::abs(out[static_cast<std::size_t>(i)] - out[static_cast<std::size_t>(i - 1)]));
        DAWLAB_CHECK(largest < 1.01f / kRampFrames);
        DAWLAB_CHECK(std::abs(out[n - 1] - 0.1f) < 1e-5f);
        DAWLAB_CHECK(std::abs(out[0] - (1.0f - 1.0f / kRampFrames)) < 1e-4f);
    }

    // Off: fades out, and then adds nothing at all.
    {
        Monitor m;
        m.prepare(kRate);
        m.setMode(MonitorMode::Always);
        m.setLevel(1.0f);
        std::vector<float> one(n, 1.0f), out(n, 0.0f);
        monoBlock(m, one, out, {});
        m.setMode(MonitorMode::Off);
        std::fill(out.begin(), out.end(), 0.0f);
        monoBlock(m, one, out, {});
        DAWLAB_CHECK(out[0] > 0.9f);
        DAWLAB_CHECK(out[kRampFrames] == 0.0f);
        DAWLAB_CHECK(out[n - 1] == 0.0f);
    }

    // Stereo to stereo, each to its own, and more inputs than outputs.
    {
        Monitor m;
        m.prepare(kRate);
        m.setMode(MonitorMode::Always);
        std::vector<float> a(n, 0.2f), b(n, 0.4f), c(n, 0.6f), outA(n, 0.0f), outB(n, 0.0f);
        const float* inputs[] = {a.data(), b.data(), c.data()};
        float* outputs[] = {outA.data(), outB.data()};
        m.process(inputs, 3, outputs, 2, nullptr, 0, n);
        DAWLAB_CHECK(std::abs(outA[n - 1] - 0.2f) < 1e-5f);
        DAWLAB_CHECK(std::abs(outB[n - 1] - 0.4f) < 1e-5f);
    }

    // A device without this input.
    {
        Monitor m;
        m.prepare(kRate);
        m.setMode(MonitorMode::Always);
        std::vector<float> a(n, 0.2f), outA(n, 0.0f), outB(n, 0.0f);
        const float* inputs[] = {a.data(), nullptr};
        float* outputs[] = {outA.data(), outB.data()};
        m.process(inputs, 2, outputs, 2, nullptr, 0, n);
        DAWLAB_CHECK(std::abs(outA[n - 1] - 0.2f) < 1e-5f);
        DAWLAB_CHECK(outB[n - 1] == 0.0f);
        m.process(nullptr, 0, outputs, 2, nullptr, 0, n);
        DAWLAB_CHECK(std::abs(outA[n - 1] - 0.2f) < 1e-5f);
    }
}

// Auto, to the frame, also inside a block, and in a loop with a wrap inside a block.
void checkAuto() {
    constexpr int n = 4000;
    RecordRange punch;
    punch.punchIn = 1100;
    punch.punchOut = 3100;

    Monitor monitor;
    monitor.prepare(kRate);
    monitor.setMode(MonitorMode::Auto);
    monitor.setRange(punch);
    std::vector<float> one(n, 1.0f), out(n, 0.0f);
    monoBlock(monitor, one, out, {{0, 0, n}});
    DAWLAB_CHECK(out[1099] == 0.0f);
    DAWLAB_CHECK(out[1100] > 0.0f);
    DAWLAB_CHECK(std::abs(out[1100 + kRampFrames] - 1.0f) < 1e-3f);
    DAWLAB_CHECK(std::abs(out[3099] - 1.0f) < 1e-3f);
    DAWLAB_CHECK(out[3100] < out[3099]);
    DAWLAB_CHECK(out[3100 + kRampFrames] == 0.0f);

    // A wrap: back into the window at once, inside the block. A segment starts at its blockOffset.
    RecordRange loop;
    loop.loopStart = 1000;
    loop.loopEnd = 2000;
    Monitor looping;
    looping.prepare(kRate);
    looping.setMode(MonitorMode::Auto);
    looping.setRange(loop);
    std::vector<float> first(1000, 1.0f), firstOut(1000, 0.0f);
    monoBlock(looping, first, firstOut, {{500, 0, 1000}});
    DAWLAB_CHECK(firstOut[499] == 0.0f); // the run-up
    DAWLAB_CHECK(firstOut[500] > 0.0f);  // position 1000
    std::fill(firstOut.begin(), firstOut.end(), 0.0f);
    const TimelineSegment parts[] = {{1500, 0, 500}, {1000, 500, 500}};
    const float* inputs[] = {first.data()};
    float* outputs[] = {firstOut.data()};
    looping.process(inputs, 1, outputs, 1, parts, 2, 1000);
    DAWLAB_CHECK(std::abs(firstOut[499] - 1.0f) < 1e-3f);
    DAWLAB_CHECK(std::abs(firstOut[500] - 1.0f) < 1e-3f); // no dip at the wrap
    DAWLAB_CHECK(std::abs(firstOut[999] - 1.0f) < 1e-3f);

    // The position of a frame is counted from its segment, not from the block.
    RecordRange inner = loop;
    inner.punchIn = 1100;
    inner.punchOut = 1900;
    Monitor offsets;
    offsets.prepare(kRate);
    offsets.setMode(MonitorMode::Auto);
    offsets.setRange(inner);
    std::vector<float> offsetIn(600, 1.0f), offsetOut(600, 0.0f);
    const TimelineSegment offsetParts[] = {{1990, 0, 10}, {1000, 10, 590}};
    const float* offsetInputs[] = {offsetIn.data()};
    float* offsetOutputs[] = {offsetOut.data()};
    offsets.process(offsetInputs, 1, offsetOutputs, 1, offsetParts, 2, 600);
    DAWLAB_CHECK(offsetOut[109] == 0.0f); // position 1099
    DAWLAB_CHECK(offsetOut[110] > 0.0f);  // position 1100

    // Stopped: the input, whatever the window says.
    Monitor stopped;
    stopped.prepare(kRate);
    stopped.setMode(MonitorMode::Auto);
    stopped.setRange(punch);
    std::vector<float> stoppedOut(n, 0.0f);
    monoBlock(stopped, one, stoppedOut, {});
    DAWLAB_CHECK(std::abs(stoppedOut[n - 1] - 1.0f) < 1e-3f);

    // A transport past the loop's end plays straight on: there is no loop to be inside.
    Monitor past;
    past.prepare(kRate);
    past.setMode(MonitorMode::Auto);
    past.setRange(loop);
    std::vector<float> pastOut(n, 0.0f);
    monoBlock(past, one, pastOut, {{5000, 0, n}});
    DAWLAB_CHECK(std::abs(pastOut[n - 1] - 1.0f) < 1e-3f);
}

// The blocks do not matter: the same input gives the same output, in any pieces.
void checkBlocks() {
    constexpr int n = 30000;
    std::mt19937 random(5u);
    std::uniform_real_distribution<float> noise(-0.5f, 0.5f);
    std::vector<float> in(n);
    for (auto& s : in)
        s = noise(random);

    const auto play = [&](int block) {
        Monitor m;
        m.prepare(kRate);
        m.setMode(MonitorMode::Auto);
        RecordRange punch;
        punch.punchIn = 7001;
        punch.punchOut = 19003;
        m.setRange(punch);
        std::vector<float> out(n, 0.0f);
        for (int done = 0; done < n; done += block) {
            const int count = std::min(block, n - done);
            const float* inputs[] = {in.data() + done};
            float* outputs[] = {out.data() + done};
            const TimelineSegment segment{done, 0, count};
            m.process(inputs, 1, outputs, 1, &segment, 1, count);
        }
        return out;
    };
    const auto whole = play(n);
    DAWLAB_CHECK(whole == play(64));
    DAWLAB_CHECK(whole == play(97));
    DAWLAB_CHECK(whole == play(4096));
}

// What the musician hears is measured here, not worked out.
void checkHeardLate() {
    const Latencies l{240, 480, 752, 0};
    const auto heardAfter = [&](MonitorPath path) {
        Delay fromMic(l.input), mixDelay(path == MonitorPath::ThroughInserts ? l.mix : 0), toSpeaker(l.output);
        Monitor monitor;
        monitor.prepare(kRate);
        monitor.setMode(MonitorMode::Always);
        // Let the ramp finish first.
        constexpr int kStart = 2 * kRampFrames;
        constexpr int kBlock = 64;
        std::int64_t heard = -1;
        for (std::int64_t frame = 0; frame < 4000 && heard < 0; frame += kBlock) {
            std::vector<float> in(kBlock), out(kBlock, 0.0f);
            for (int i = 0; i < kBlock; ++i)
                in[static_cast<std::size_t>(i)] = fromMic.process(frame + i == kStart ? 1.0f : 0.0f);
            monoBlock(monitor, in, out, {});
            for (int i = 0; i < kBlock; ++i) {
                const float speaker = toSpeaker.process(mixDelay.process(out[static_cast<std::size_t>(i)]));
                if (speaker != 0.0f && heard < 0)
                    heard = frame + i - kStart;
            }
        }
        return heard;
    };
    const std::int64_t direct = heardAfter(MonitorPath::Direct);
    const std::int64_t through = heardAfter(MonitorPath::ThroughInserts);
    DAWLAB_CHECK(direct == monitorLatency(l, MonitorPath::Direct));
    DAWLAB_CHECK(through == monitorLatency(l, MonitorPath::ThroughInserts));
    std::printf("  heard after: %.1f ms directly, %.1f ms through the inserts (one 752-sample plugin anywhere in the mix)\n",
                1000.0 * static_cast<double>(direct) / kRate, 1000.0 * static_cast<double>(through) / kRate);
}

// A microphone and a speaker in one room: what is played comes back after `roundTrip` frames, `loopGain`
// times as loud, and the converter clips. A soft noise keeps it going. Also returns what was recorded.
struct Room {
    float loopGain = 0.0f;
    int roundTrip = 1000;
    int block = 256;
    int frames = kRate * 3;
};
struct RoomResult {
    int trippedAt = -1;
    float peakAfterMute = 0.0f;
    float peak = 0.0f;
    std::vector<float> input, recorded;
};

RoomResult runRoom(const Room& room, MonitorMode mode) {
    RoomResult result;
    Monitor monitor;
    monitor.prepare(kRate);
    monitor.setMode(mode);
    Recorder recorder;
    Latencies device{0, 0, 0, 0};
    recorder.start(device, RecordRange{}, 1, 1 << 16);
    Take take(1);
    Delay air(room.roundTrip);
    std::mt19937 random(11u);
    std::uniform_real_distribution<float> noise(-0.01f, 0.01f);

    std::vector<float> in(static_cast<std::size_t>(room.block)), out(static_cast<std::size_t>(room.block));
    std::vector<float> back(static_cast<std::size_t>(room.block), 0.0f);
    std::int64_t position = 0;
    for (int done = 0; done < room.frames; done += room.block, position += room.block) {
        for (int i = 0; i < room.block; ++i)
            in[static_cast<std::size_t>(i)] =
                std::clamp(noise(random) + room.loopGain * back[static_cast<std::size_t>(i)], -1.0f, 1.0f);
        result.input.insert(result.input.end(), in.begin(), in.end());

        const float* inputs[] = {in.data()};
        const TimelineSegment segment{position, 0, room.block};
        recorder.process(inputs, 1, &segment, 1, 0);
        take.collect(recorder.ring());

        std::fill(out.begin(), out.end(), 0.0f);
        monoBlock(monitor, in, out, {segment});
        for (int i = 0; i < room.block; ++i) {
            const float s = out[static_cast<std::size_t>(i)];
            result.peak = std::max(result.peak, std::abs(s));
            if (result.trippedAt >= 0 && done - result.trippedAt > kRampFrames + room.block)
                result.peakAfterMute = std::max(result.peakAfterMute, std::abs(s));
            back[static_cast<std::size_t>(i)] = air.process(s);
        }
        if (result.trippedAt < 0 && monitor.feedbackDetected())
            result.trippedAt = done + room.block;
    }
    if (!take.channels().empty())
        result.recorded = take.channels()[0];
    return result;
}

void checkFeedback() {
    Room howling;
    howling.loopGain = 1.5f;
    const RoomResult bad = runRoom(howling, MonitorMode::Always);
    DAWLAB_CHECK(bad.trippedAt >= 0);
    DAWLAB_CHECK(bad.trippedAt < kRate);
    DAWLAB_CHECK(bad.peakAfterMute < 0.02f); // only the room's noise, and nothing playing it
    std::printf("  a loop gain of 1.5: detected after %d ms, and silent after that\n", bad.trippedAt * 1000 / kRate);

    // Barely more than 1 takes long to grow: it ends up pinned at the top.
    Room slow;
    slow.loopGain = 1.05f;
    slow.frames = kRate * 6;
    const RoomResult crawling = runRoom(slow, MonitorMode::Always);
    DAWLAB_CHECK(crawling.trippedAt >= 0);
    DAWLAB_CHECK(crawling.peakAfterMute < 0.02f);
    std::printf("  a loop gain of 1.05: detected after %d ms\n", crawling.trippedAt * 1000 / kRate);

    // Any block size, any round trip.
    for (const int block : {64, 480, 1024}) {
        for (const int roundTrip : {1024, 3000}) {
            Room room;
            room.loopGain = 1.5f;
            room.block = block;
            room.roundTrip = roundTrip;
            DAWLAB_CHECK(runRoom(room, MonitorMode::Always).trippedAt >= 0);
        }
    }

    // It comes back softer: an echo, and no alarm.
    Room stable;
    stable.loopGain = 0.5f;
    DAWLAB_CHECK(runRoom(stable, MonitorMode::Always).trippedAt < 0);

    // Not monitored: no sound in the room, so a loud input is not feedback.
    const RoomResult off = runRoom(howling, MonitorMode::Off);
    DAWLAB_CHECK(off.trippedAt < 0);
    DAWLAB_CHECK(off.peak == 0.0f);

    // The recording does not notice: all of it is the input, including the howl and what followed it.
    DAWLAB_CHECK(bad.recorded == bad.input);
    DAWLAB_CHECK(off.recorded == off.input);
}

// A musician: notes with fast attacks, up to loud, some of them long. No feedback, no alarm.
void checkMusician() {
    std::mt19937 random(7u);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    int trips = 0, notes = 0;
    for (int round = 0; round < 20; ++round) {
        Monitor monitor;
        monitor.prepare(kRate);
        monitor.setMode(MonitorMode::Always);
        constexpr int kBlock = 256;
        std::vector<float> in(kBlock), out(kBlock);
        double phase = 0.0;
        for (int note = 0; note < 40; ++note, ++notes) {
            const float loudness = 0.05f + 0.88f * unit(random);
            const int length = static_cast<int>(kRate * (0.05f + 1.5f * unit(random)));
            const int attack = static_cast<int>(kRate * (0.001f + 0.08f * unit(random)));
            const double step = 2.0 * 3.14159265358979 * (80.0 + 900.0 * unit(random)) / kRate;
            for (int done = 0; done < length; done += kBlock) {
                for (int i = 0; i < kBlock; ++i) {
                    const int at = done + i;
                    const float envelope = at < attack ? static_cast<float>(at) / static_cast<float>(attack)
                                                       : std::exp(-3.0f * static_cast<float>(at - attack) / static_cast<float>(length));
                    in[static_cast<std::size_t>(i)] = loudness * envelope * static_cast<float>(std::sin(phase));
                    phase += step;
                }
                std::fill(out.begin(), out.end(), 0.0f);
                monoBlock(monitor, in, out, {});
            }
        }
        trips += monitor.feedbackDetected() ? 1 : 0;
    }
    std::printf("  a musician, %d notes: %d false alarms (expected 0)\n", notes, trips);
    DAWLAB_CHECK(trips == 0);
}

// A loud input is only feedback if it is being played.
void checkOnlyWhenHeard() {
    constexpr int n = kWindow * 8;
    std::vector<float> loud(n, 1.0f), out(n, 0.0f);
    Monitor off;
    off.prepare(kRate);
    off.setMode(MonitorMode::Off);
    monoBlock(off, loud, out, {});
    DAWLAB_CHECK(!off.feedbackDetected());

    // Negative samples count as much as positive ones.
    Monitor negative;
    negative.prepare(kRate);
    negative.setMode(MonitorMode::Always);
    std::vector<float> below(n, -1.0f);
    monoBlock(negative, below, out, {});
    DAWLAB_CHECK(negative.feedbackDetected());
}

void checkReset() {
    Monitor monitor;
    monitor.prepare(kRate);
    monitor.setMode(MonitorMode::Always);
    constexpr int n = kWindow * 6;
    std::vector<float> loud(n, 1.0f), out(n, 0.0f);
    monoBlock(monitor, loud, out, {});
    DAWLAB_CHECK(monitor.feedbackDetected());
    DAWLAB_CHECK(monitor.gain() == 0.0f);

    // The UI asks; the audio thread does it, with its next block.
    monitor.resetFeedback();
    DAWLAB_CHECK(monitor.feedbackDetected());
    std::vector<float> soft(n, 0.2f);
    std::fill(out.begin(), out.end(), 0.0f);
    monoBlock(monitor, soft, out, {});
    DAWLAB_CHECK(!monitor.feedbackDetected());
    DAWLAB_CHECK(std::abs(out[n - 1] - 0.2f) < 1e-3f);
    DAWLAB_CHECK(out[0] < 0.01f); // and it comes back with a ramp
}

// A real audio thread that records and monitors, and the UI changing everything at the same time.
// The test's own counters are relaxed: they must not synchronize anything for the code under test.
void checkThreads() {
    constexpr int kBlock = 128;
    Monitor monitor;
    monitor.prepare(kRate);
    Recorder recorder;
    std::atomic<bool> quit{false};
    std::atomic<std::int64_t> transport{0}, allowed{0};
    const auto valueAt = [](std::int64_t position) { return static_cast<float>(position % 1000 + 1) / 1000.0f; };

    std::thread audio([&] {
        float input[kBlock], output[kBlock];
        const float* inputs[] = {input};
        float* outputs[] = {output};
        std::int64_t position = 0;
        while (!quit.load(std::memory_order_relaxed)) {
            if (position >= allowed.load(std::memory_order_relaxed)) {
                std::this_thread::yield();
                continue;
            }
            for (int i = 0; i < kBlock; ++i) {
                input[i] = valueAt(position + i);
                output[i] = 0.0f;
            }
            const TimelineSegment segment{position, 0, kBlock};
            recorder.process(inputs, 1, &segment, 1, 0);
            monitor.process(inputs, 1, outputs, 1, &segment, 1, kBlock);
            position += kBlock;
            transport.store(position, std::memory_order_relaxed);
        }
    });

    const auto begin = std::chrono::steady_clock::now();
    const auto running = [&] { return std::chrono::steady_clock::now() - begin < std::chrono::seconds(30); };
    const auto keepGoing = [&] {
        allowed.store(transport.load(std::memory_order_relaxed) + (1 << 12), std::memory_order_relaxed);
        std::this_thread::yield();
    };

    std::uint64_t frames = 0, wrong = 0;
    int takes = 0;
    for (int round = 0; round < 60 && running(); ++round) {
        if (!recorder.start(Latencies{}, RecordRange{}, 1, 1 << 15)) {
            ++wrong;
            break;
        }
        Take take(1);
        const std::int64_t until = transport.load(std::memory_order_relaxed) + 8000;
        int change = 0;
        while (transport.load(std::memory_order_relaxed) < until && running()) {
            take.collect(recorder.ring());
            monitor.setMode(static_cast<MonitorMode>(change % 3));
            monitor.setLevel(static_cast<float>(change % 5) * 0.25f);
            RecordRange range;
            range.punchIn = change * 7;
            range.punchOut = change * 7 + 900;
            range.loopStart = change;
            range.loopEnd = change * 3;
            monitor.setRange(range);
            if (change % 7 == 0)
                monitor.resetFeedback();
            (void)monitor.feedbackDetected();
            ++change;
            keepGoing();
        }
        recorder.stop();
        while (!recorder.isIdle() && running())
            keepGoing();
        take.collect(recorder.ring());
        if (!recorder.wasStarted())
            continue;
        ++takes;
        const auto& samples = take.channels()[0];
        const std::int64_t stamp = recorder.stampedPosition();
        wrong += samples.size() == static_cast<std::size_t>(recorder.streamFrames()) ? 0 : 1;
        for (std::size_t i = 0; i < samples.size(); ++i)
            wrong += samples[i] == valueAt(stamp + static_cast<std::int64_t>(i)) ? 0 : 1;
        frames += samples.size();
    }
    quit.store(true, std::memory_order_relaxed);
    audio.join();

    std::printf("  two threads: %d takes, %llu frames recorded while the monitor kept changing, %llu wrong (expected 0)\n",
                takes, static_cast<unsigned long long>(frames), static_cast<unsigned long long>(wrong));
    DAWLAB_CHECK(takes > 0);
    DAWLAB_CHECK(wrong == 0);
}

} // namespace step05_check

int main() {
    using namespace step05_check;
    std::printf("Step 5: input monitoring\n");
    checkLatency();
    checkWants();
    checkGuard();
    checkAdds();
    if (dawlab::test::counters().failures > 0)
        return dawlab::test::summary("l17_step05"); // the rest needs a monitor that works
    checkAuto();
    checkBlocks();
    checkHeardLate();
    checkFeedback();
    checkMusician();
    checkOnlyWhenHeard();
    checkReset();
    checkThreads();
    return dawlab::test::summary("l17_step05");
}
