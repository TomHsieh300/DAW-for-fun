// test_timeline: transport maths, sample-accurate clip placement, clip gain / fades, loop,
// seek and stop (Steps 2 and 3), and a loop changed while another thread plays.

#include "TestHelpers.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <random>
#include <thread>

#include "TestTracks.h"

using namespace l15test;
using namespace dawlab::engine::test;

namespace {

constexpr float kCentre = 0.70710678f; // a track's centre pan gain (ConstantPower)

void testTransportMaths() {
    Transport t;
    t.prepare(48000.0);
    t.setTempo(120.0);
    DAWLAB_CHECK_NEAR(t.getSamplesPerBeat(), 24000.0, 1e-9);
    DAWLAB_CHECK_NEAR(t.getSamplesPerBar(), 96000.0, 1e-9);

    // bar 3, beat 2, tick 480 = (2*4 + 1 + 0.5) beats = 9.5 beats = 228000 samples
    DAWLAB_CHECK(t.barsBeatsToSamples({3, 2, 480}) == 228000);
    const BarsBeats p = t.samplesToBarsBeats(228000);
    DAWLAB_CHECK(p.bar == 3 && p.beat == 2 && p.tick == 480);
    const BarsBeats q = t.samplesToBarsBeats(96000 * 4); // exactly bar 5
    DAWLAB_CHECK(q.bar == 5 && q.beat == 1 && q.tick == 0);
    const BarsBeats z = t.samplesToBarsBeats(0);
    DAWLAB_CHECK(z.bar == 1 && z.beat == 1 && z.tick == 0);

    // 7/8 at 90 BPM: one beat = 48000 * 60 / 90 = 32000 samples, one bar = 224000
    t.setTempo(90.0);
    t.setTimeSignature(7, 8);
    DAWLAB_CHECK_NEAR(t.getSamplesPerBeat(), 32000.0, 1e-9);
    DAWLAB_CHECK_NEAR(t.getSamplesPerBar(), 224000.0, 1e-9);
    DAWLAB_CHECK(t.barsBeatsToSamples({2, 7, 0}) == 224000 + 6 * 32000);

    // tempo is clamped
    t.setTempo(5.0);
    DAWLAB_CHECK_NEAR(t.getTempo(), Transport::kMinTempo, 1e-9);

    // a pending seek is what getPosition() reports, applied by advance()
    t.setTempo(120.0);
    t.setTimeSignature(4, 4);
    t.setPosition(1000);
    DAWLAB_CHECK(t.getPosition() == 1000);
    TimelineSegment seg[Transport::kMaxSegmentsPerBlock];
    int covered = 0;
    DAWLAB_CHECK(t.advance(256, seg, covered) == 0); // stopped: no segments, position unchanged
    DAWLAB_CHECK(covered == 256);
    DAWLAB_CHECK(t.getPosition() == 1000);
    t.play();
    DAWLAB_CHECK(t.advance(256, seg, covered) == 1);
    DAWLAB_CHECK(seg[0].timelineStart == 1000 && seg[0].numSamples == 256 && seg[0].blockOffset == 0);
    DAWLAB_CHECK(t.getPosition() == 1256);

    // loop end inside a block: split in two segments
    t.setLoopRange(1000, 1300);
    t.setLoopEnabled(true);
    DAWLAB_CHECK(t.advance(100, seg, covered) == 2);
    DAWLAB_CHECK(seg[0].timelineStart == 1256 && seg[0].numSamples == 44);
    DAWLAB_CHECK(seg[1].timelineStart == 1000 && seg[1].numSamples == 56 && seg[1].blockOffset == 44);
    DAWLAB_CHECK(t.getPosition() == 1056);

    // a 10-sample loop in a 512 block needs more than 8 segments: advance covers part of it
    t.setLoopRange(0, 10);
    t.setPosition(0);
    const int n = t.advance(512, seg, covered);
    DAWLAB_CHECK(n == Transport::kMaxSegmentsPerBlock);
    DAWLAB_CHECK(covered == 80);
    DAWLAB_CHECK(t.getPosition() == 0);
}

void testClipPlacement() {
    // The engines here are on the heap: an Engine holds all its track slots (some 190 KB), and a Debug build of
    // MSVC gives each block below its own room on the stack, so a few of them overflowed Windows' 1 MB stack.

    // An impulse clip at timeline sample 12345 must appear at exactly output sample 12345,
    // whatever the block size.
    for (int blockSize : {512, 64, 1, 1000}) {
        const auto engine = std::make_unique<Engine>();
        Engine& e = *engine;
        addTestTracks(e);
        e.prepare(kSampleRate, 512);
        Clip c = Clip::fromBuffer(makeImpulseClip(1, 0), 12345, "click");
        e.setTrackClips(testId(0), {c});
        AudioBuffer out = render(e, 20000, blockSize);
        DAWLAB_CHECK(argMaxAbs(out, 0) == 12345);
        DAWLAB_CHECK(countAbove(out, 0, 1e-6f) == 1);
        DAWLAB_CHECK_NEAR(out.getSample(0, 12345), kCentre, 1e-5);
    }

    // trimming: sourceOffset / length select a window of the source
    {
        const auto engine = std::make_unique<Engine>();
        Engine& e = *engine;
        addTestTracks(e);
        e.prepare(kSampleRate, 512);
        auto ramp = std::make_shared<AudioBuffer>(1, 1000);
        for (int i = 0; i < 1000; ++i)
            ramp->setSample(0, i, static_cast<float>(i) * 1e-3f);
        Clip c;
        c.source = ramp;
        c.start = 2000;
        c.sourceOffset = 100; // play source samples 100..399
        c.length = 300;
        c.gainDb = -6.0206f; // x0.5
        e.setTrackClips(testId(0), {c});
        AudioBuffer out = render(e, 3000);
        DAWLAB_CHECK_NEAR(out.getSample(0, 1999), 0.0, 1e-9);
        DAWLAB_CHECK_NEAR(out.getSample(0, 2000), 0.100 * 0.5 * kCentre, 1e-5);
        DAWLAB_CHECK_NEAR(out.getSample(0, 2299), 0.399 * 0.5 * kCentre, 1e-5);
        DAWLAB_CHECK_NEAR(out.getSample(0, 2300), 0.0, 1e-9);
    }

    // a stereo clip on a stereo track keeps its channels (balance: unity at the centre); a clip
    // that is longer than its source pads silence. (A mono track would fold it to (L+R)/2:
    // test_track_format.)
    {
        const auto engine = std::make_unique<Engine>();
        Engine& e = *engine;
        addTestTracks(e);
        e.prepare(kSampleRate, 512);
        e.getMixer().getTrack(0).setFormat(TrackFormat::Stereo);
        auto st = std::make_shared<AudioBuffer>(2, 100);
        for (int i = 0; i < 100; ++i) {
            st->setSample(0, i, 0.2f);
            st->setSample(1, i, -0.4f);
        }
        Clip c = Clip::fromBuffer(st, 0);
        c.length = 200;
        e.setTrackClips(testId(0), {c});
        AudioBuffer out = render(e, 300);
        DAWLAB_CHECK_NEAR(out.getSample(0, 50), 0.2, 1e-5);
        DAWLAB_CHECK_NEAR(out.getSample(1, 50), -0.4, 1e-5);
        DAWLAB_CHECK_NEAR(out.getSample(0, 150), 0.0, 1e-9);
    }

    // overlapping clips on one track add
    {
        const auto engine = std::make_unique<Engine>();
        Engine& e = *engine;
        addTestTracks(e);
        e.prepare(kSampleRate, 512);
        e.setTrackClips(testId(0), {Clip::fromBuffer(makeConstant(0.1f, 1000), 0), Clip::fromBuffer(makeConstant(0.2f, 1000), 500)});
        AudioBuffer out = render(e, 2000);
        DAWLAB_CHECK_NEAR(out.getSample(0, 250), 0.1 * kCentre, 1e-5);
        DAWLAB_CHECK_NEAR(out.getSample(0, 750), 0.3 * kCentre, 1e-5);
        DAWLAB_CHECK_NEAR(out.getSample(0, 1250), 0.2 * kCentre, 1e-5);
    }
}

void testFades() {
    Clip c = Clip::fromBuffer(makeConstant(1.0f, 10), 0);
    c.fadeInSamples = 4;
    c.fadeOutSamples = 4;
    // fade-in: (i+1)/(F+1) = 0.2, 0.4, 0.6, 0.8 then 1
    DAWLAB_CHECK_NEAR(clipFadeAt(c, 0), 0.2, 1e-6);
    DAWLAB_CHECK_NEAR(clipFadeAt(c, 3), 0.8, 1e-6);
    DAWLAB_CHECK_NEAR(clipFadeAt(c, 4), 1.0, 1e-6);
    DAWLAB_CHECK_NEAR(clipFadeAt(c, 9), 0.2, 1e-6);
    // linear fade-out + fade-in of the same length sum to 1 at every overlap position
    Clip out = c, in = c;
    out.fadeInSamples = 0;
    in.fadeOutSamples = 0;
    for (int k = 0; k < 4; ++k)
        DAWLAB_CHECK_NEAR(clipFadeAt(out, 6 + k) + clipFadeAt(in, k), 1.0, 1e-6);
    // equal power: squares sum to 1
    out.fadeShape = in.fadeShape = FadeShape::EqualPower;
    for (int k = 0; k < 4; ++k) {
        const double a = clipFadeAt(out, 6 + k), b = clipFadeAt(in, k);
        DAWLAB_CHECK_NEAR(a * a + b * b, 1.0, 1e-5);
    }
}

// renderClip() itself, a few samples at a time: a fade-in only and a fade-out only clip get their fade
// (with the clip gain, x0.5) wherever a block starts inside it.
void testFadesWhenRendered() {
    Clip in = Clip::fromBuffer(makeConstant(1.0f, 10), 100);
    in.gainDb = -6.0206f;
    in.fadeInSamples = 4;
    Clip out = in;
    out.fadeInSamples = 0;
    out.fadeOutSamples = 4;
    const float inGain[10] = {0.2f, 0.4f, 0.6f, 0.8f, 1, 1, 1, 1, 1, 1};
    const float outGain[10] = {1, 1, 1, 1, 1, 1, 0.8f, 0.6f, 0.4f, 0.2f};
    AudioBuffer a(1, 10), b(1, 10);
    for (int from = 0; from < 10; from += 3) {
        const int len = std::min(3, 10 - from);
        renderClip(in, 100 + from, a.toBlock().getSubBlock(from, len));
        renderClip(out, 100 + from, b.toBlock().getSubBlock(from, len));
    }
    for (int i = 0; i < 10; ++i) {
        DAWLAB_CHECK_NEAR(a.getSample(0, i), 0.5 * inGain[i], 1e-5);
        DAWLAB_CHECK_NEAR(b.getSample(0, i), 0.5 * outGain[i], 1e-5);
    }
}

// A clip that is longer than its source plays silence after the source's last sample - not the
// sample after it (in a stereo buffer that is where the next channel starts) - and a source with
// no channels plays nothing at all.
void testClipPastItsSource() {
    auto st = std::make_shared<AudioBuffer>(2, 4);
    for (int i = 0; i < 4; ++i) {
        st->setSample(0, i, 0.2f);
        st->setSample(1, i, -0.4f);
    }
    Clip c = Clip::fromBuffer(st, 0);
    c.length = 6;
    AudioBuffer dest(2, 6);
    renderClip(c, 0, dest.toBlock());
    DAWLAB_CHECK_NEAR(dest.getSample(0, 3), 0.2, 1e-6);
    DAWLAB_CHECK_NEAR(dest.getSample(1, 3), -0.4, 1e-6);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 4; i < 6; ++i)
            DAWLAB_CHECK(dest.getSample(ch, i) == 0.0f);

    Clip none = Clip::fromBuffer(std::make_shared<AudioBuffer>(0, 100), 0);
    AudioBuffer silent(2, 100);
    renderClip(none, 0, silent.toBlock());
    DAWLAB_CHECK(peakAbs(silent) == 0.0f);
}

// renderOffline(): the default length (the song's end, from startSample on), a tail rendered with
// the transport stopped, and the latency dropped from the front of a bounce.
void testBounceLengths() {
    {   // numSamples 0: from startSample up to the end of the last clip
        const auto engine = std::make_unique<Engine>();
        Engine& e = *engine;
        addTestTracks(e);
        e.prepare(kSampleRate, 512);
        e.setTrackClips(testId(0), {Clip::fromBuffer(makeConstant(0.1f, 1000), 0)});
        RenderOptions o;
        o.startSample = 200;
        o.compensateLatency = false;
        const AudioBuffer out = renderOffline(e, o);
        DAWLAB_CHECK(out.getNumSamples() == 800);
        DAWLAB_CHECK_NEAR(out.getSample(0, 0), 0.1 * kCentre, 1e-5);
        DAWLAB_CHECK_NEAR(out.getSample(0, 799), 0.1 * kCentre, 1e-5);
    }
    {   // a 10 ms tail in blocks of 300: the clip goes on past the song's end, but the tail is silent
        const auto engine = std::make_unique<Engine>();
        Engine& e = *engine;
        addTestTracks(e);
        e.prepare(kSampleRate, 512);
        e.setTrackClips(testId(0), {Clip::fromBuffer(makeConstant(0.1f, 3000), 0)});
        RenderOptions o;
        o.numSamples = 1000;
        o.tailSeconds = 0.01;
        o.blockSize = 300;
        o.compensateLatency = false;
        const AudioBuffer out = renderOffline(e, o);
        DAWLAB_CHECK(out.getNumSamples() == 1480);
        DAWLAB_CHECK_NEAR(out.getSample(0, 999), 0.1 * kCentre, 1e-5);
        int loudInTail = 0;
        for (int i = 1000; i < out.getNumSamples(); ++i)
            loudInTail += out.getSample(0, i) != 0.0f ? 1 : 0;
        DAWLAB_CHECK(loudInTail == 0);
    }
    {   // 100 samples of insert latency: the bounce is still numSamples long, and the impulse is back in place
        const auto engine = std::make_unique<Engine>();
        Engine& e = *engine;
        addTestTracks(e);
        e.prepare(kSampleRate, 512);
        e.setTrackClips(testId(0), {Clip::fromBuffer(makeImpulseClip(1, 0, 0.5f), 1000)});
        e.addInsert(testTrack(0), -1, std::make_unique<PureDelayEffect>(100));
        e.applyPendingCommandsOffline();
        const AudioBuffer out = render(e, 3000, kBlockSize, true);
        DAWLAB_CHECK(out.getNumSamples() == 3000);
        for (int ch = 0; ch < 2; ++ch) {
            DAWLAB_CHECK(argMaxAbs(out, ch) == 1000);
            DAWLAB_CHECK_NEAR(out.getSample(ch, 1000), 0.5 * kCentre, 1e-5);
        }
    }
}

// A clip whose value encodes the timeline position: v(t) = t * 1e-5. The output (divided by
// the pan gain) then tells which timeline sample was played at every output sample.
std::shared_ptr<const AudioBuffer> makeTimelineRamp(int n) {
    auto b = std::make_shared<AudioBuffer>(1, n);
    for (int i = 0; i < n; ++i)
        b->setSample(0, i, static_cast<float>(i) * 1e-5f);
    return b;
}

int decodePosition(const AudioBuffer& out, int i) {
    return static_cast<int>(std::lround(out.getSample(0, i) / kCentre / 1e-5f));
}

void testLoopPlayback() {
    for (int loopLen : {2000, 7}) { // a normal loop, and one far shorter than a block
        Engine e;
        addTestTracks(e);
        e.prepare(kSampleRate, 512);
        e.setTrackClips(testId(0), {Clip::fromBuffer(makeTimelineRamp(10000), 0)});
        e.applyPendingCommandsOffline();
        Transport& t = e.getTransport();
        t.setLoopRange(1000, 1000 + loopLen);
        t.setLoopEnabled(true);
        t.setPosition(0);
        t.play();
        AudioBuffer out(2, 10000);
        std::mt19937 rng(7);
        std::uniform_int_distribution<int> sizeDist(1, 700);
        int pos = 0;
        while (pos < 10000) { // random block sizes, also > maxBlockSize
            const int len = std::min(sizeDist(rng), 10000 - pos);
            e.processBlock(out.toBlock().getSubBlock(pos, len));
            pos += len;
        }
        int errors = 0;
        for (int i = 0; i < 10000; ++i) {
            const int expected = i < 1000 + loopLen ? i : 1000 + (i - 1000) % loopLen;
            if (decodePosition(out, i) != expected)
                ++errors;
        }
        DAWLAB_CHECK(errors == 0);
    }
}

void testSeekAndStop() {
    Engine e;
    addTestTracks(e);
    e.prepare(kSampleRate, 512);
    e.setTrackClips(testId(0), {Clip::fromBuffer(makeTimelineRamp(48000), 0)});
    e.applyPendingCommandsOffline();
    Transport& t = e.getTransport();
    t.play();
    AudioBuffer block(2, 256);
    e.processBlock(block);
    DAWLAB_CHECK(decodePosition(block, 0) == 0);
    t.setPosition(30000); // seek: applied at the start of the next block
    e.processBlock(block);
    DAWLAB_CHECK(decodePosition(block, 0) == 30000);
    DAWLAB_CHECK(decodePosition(block, 255) == 30255);
    t.stop();
    e.processBlock(block);
    DAWLAB_CHECK(peakAbs(block) == 0.0f); // stopped: no clip audio (no inserts -> silence)
    DAWLAB_CHECK(t.getPosition() == 30256);
    t.setPositionBarsBeats({1, 2, 0}); // beat 2 = 24000
    t.play();
    e.processBlock(block);
    DAWLAB_CHECK(decodePosition(block, 0) == 24000);
}

void testLoopChangeArrivesWhole() {
    // The UI thread switches the loop between [0, 3000) and [1000, 2000) while an audio thread plays blocks of
    // 4000. A wrap may only go from 3000 to 0 or from 2000 to 1000; a loop read half old, half new would wrap
    // somewhere else. Today's way can only pass; the old one (two atomics) failed in every run. 50000 switches,
    // or 10 s on a slow machine.
    Transport t;
    t.prepare(48000.0);
    t.setLoopRange(0, 3000);
    t.setLoopEnabled(true);
    t.play();
    std::atomic<bool> quit{false};
    std::atomic<long> calls{0};
    long legal = 0, torn = 0; // the audio thread's, read after join()
    std::thread audio([&] {
        TimelineSegment seg[Transport::kMaxSegmentsPerBlock];
        while (!quit.load(std::memory_order_relaxed)) {
            int covered = 0;
            const int n = t.advance(4000, seg, covered);
            for (int s = 0; s + 1 < n; ++s) {
                const int64_t end = seg[s].timelineStart + seg[s].numSamples, next = seg[s + 1].timelineStart;
                if (end != next)
                    ++((end == 3000 && next == 0) || (end == 2000 && next == 1000) ? legal : torn);
            }
            calls.fetch_add(1, std::memory_order_release);
        }
    });
    const auto waitForBlocks = [&](long more) {
        const long target = calls.load(std::memory_order_acquire) + more;
        while (calls.load(std::memory_order_acquire) < target)
            std::this_thread::yield();
    };
    std::mt19937 rng(1);
    volatile unsigned sink = 0;
    int switches = 0;
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (; switches < 50000 && std::chrono::steady_clock::now() < until; ++switches) {
        t.setLoopRange(0, 3000);
        t.setPosition(0);
        waitForBlocks(2);
        for (unsigned spin = rng() % 64; spin > 0; --spin) // a different moment each time
            sink = sink + 1;
        t.setLoopRange(1000, 2000);
        waitForBlocks(1);
    }
    quit = true;
    audio.join();
    std::printf("  %d loop changes while another thread plays: %ld wraps where a loop ends, %ld torn\n", switches, legal, torn);
    DAWLAB_CHECK(torn == 0 && legal > 0);
}

} // namespace

int main() {
    testTransportMaths();
    testClipPlacement();
    testFades();
    testFadesWhenRendered();
    testClipPastItsSource();
    testBounceLengths();
    testLoopPlayback();
    testSeekAndStop();
    testLoopChangeArrivesWhole();
    return dawlab::test::summary("l15_test_timeline");
}
