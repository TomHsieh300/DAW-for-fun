// test_warp: audio clips that follow the tempo, in the real engine (chapter 20, Step 5).
//
//   1. Markers for a loop and for a take, over the engine's tempo map.
//   2. renderFollowClip(): Off and Start are placed; Varispeed moves the pitch with the speed; Stretch keeps it;
//      a range of the source, two channels, the halves of a cut take, a render that is refused or cancelled.
//   3. FollowRenderer: a request comes back once; a request that a newer one overtook does not come back at all.
//   4. FollowTrack through the engine: placeholders at once, renders when they are ready, clicks on their beats
//      under a new map; a new gain renders nothing.
//   5. Many maps in a row: what plays in the end is the render of the last one.
//   6. An audio thread that plays while the UI thread changes the map and hands renders over: nothing is
//      allocated on the audio thread (this file replaces operator new), and ThreadSanitizer sees no race.

#include "TestHelpers.h"
#include "TestTracks.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <random>
#include <thread>

// ---- allocation counting (as in test_tempo.cpp) ---------------------------------------------------------------
namespace {
thread_local bool tCounting = false;
std::atomic<long> gAllocs{0};
void* countedAlloc(std::size_t n) {
    if (tCounting)
        gAllocs.fetch_add(1, std::memory_order_relaxed);
    void* p = std::malloc(n == 0 ? 1 : n);
    if (p == nullptr)
        throw std::bad_alloc();
    return p;
}
void countedFree(void* p) noexcept {
    if (p != nullptr && tCounting)
        gAllocs.fetch_add(1, std::memory_order_relaxed);
    std::free(p);
}
} // namespace

void* operator new(std::size_t n) { return countedAlloc(n); }
void* operator new[](std::size_t n) { return countedAlloc(n); }
void operator delete(void* p) noexcept { countedFree(p); }
void operator delete[](void* p) noexcept { countedFree(p); }
void operator delete(void* p, std::size_t) noexcept { countedFree(p); }
void operator delete[](void* p, std::size_t) noexcept { countedFree(p); }

using namespace l15test;
using namespace dawlab::engine::test;

namespace {

constexpr std::int64_t kBeat = 960, kBar = 3840;
constexpr std::int64_t kBeatAt120 = 24000; // samples of a beat at 120 BPM and 48 kHz

TempoMap mapOf(std::vector<TempoPoint> points) {
    TempoMap m;
    m.set(points);
    return m;
}

std::shared_ptr<const AudioBuffer> sineBuffer(std::vector<double> hzOfChannel, int samples, double amplitude) {
    auto b = std::make_shared<AudioBuffer>(static_cast<int>(hzOfChannel.size()), samples);
    for (size_t ch = 0; ch < hzOfChannel.size(); ++ch)
        for (int i = 0; i < samples; ++i)
            b->setSample(static_cast<int>(ch), i, static_cast<float>(amplitude * std::sin(kTwoPi * hzOfChannel[ch] * i / kSampleRate)));
    return b;
}

// A click on every beat of 120 BPM.
std::shared_ptr<const AudioBuffer> clicksBuffer(int beats, float amplitude) {
    auto b = std::make_shared<AudioBuffer>(1, beats * static_cast<int>(kBeatAt120));
    for (int k = 0; k < beats; ++k)
        b->setSample(0, k * static_cast<int>(kBeatAt120), amplitude);
    return b;
}

// A tone measured in stretches of 4096 samples with a free phase (as Step 4's check): what is not the tone in dB
// below it, and the pitch in cents from how the phase walks.
struct Tone {
    double noiseDb = -200.0, cents = 0.0, rms = 0.0;
};

Tone measureTone(const AudioBuffer& buffer, int ch, int from, int to, double hz) {
    constexpr int kStretch = 4096;
    const double w = kTwoPi * hz / kSampleRate;
    const float* y = buffer.getReadPointer(ch);
    double signal = 0.0, noise = 0.0, turned = 0.0, before = 0.0, squares = 0.0;
    int stretches = 0;
    for (int s0 = from; s0 + kStretch <= std::min(to, buffer.getNumSamples()); s0 += kStretch) {
        double ss = 0.0, cc = 0.0, sc = 0.0, ys = 0.0, yc = 0.0;
        for (int i = s0; i < s0 + kStretch; ++i) {
            const double sn = std::sin(w * i), cs = std::cos(w * i);
            ss += sn * sn, cc += cs * cs, sc += sn * cs, ys += y[i] * sn, yc += y[i] * cs;
            squares += static_cast<double>(y[i]) * y[i];
        }
        const double det = ss * cc - sc * sc;
        if (std::fabs(det) < 1e-12)
            continue;
        const double a = (ys * cc - yc * sc) / det, b = (yc * ss - ys * sc) / det;
        for (int i = s0; i < s0 + kStretch; ++i) {
            const double fit = a * std::sin(w * i) + b * std::cos(w * i);
            signal += fit * fit;
            noise += (y[i] - fit) * (y[i] - fit);
        }
        const double phase = std::atan2(b, a);
        if (stretches > 0) {
            double step = phase - before;
            while (step > kPi)
                step -= kTwoPi;
            while (step < -kPi)
                step += kTwoPi;
            turned += step;
        }
        before = phase;
        ++stretches;
    }
    Tone tone;
    if (signal > 0.0 && stretches > 1) {
        tone.noiseDb = 10.0 * std::log10(signal / std::max(noise, 1e-300));
        tone.cents = 1200.0 * std::log2(1.0 + turned / (kTwoPi * (stretches - 1) * kStretch) * kSampleRate / hz);
        tone.rms = std::sqrt(squares / (static_cast<double>(stretches) * kStretch));
    }
    return tone;
}

// A loop of `beats` beats made at 120 BPM, on `startTick`, playing all of `source`.
FollowClip loopClip(std::shared_ptr<const AudioBuffer> source, FollowMode mode, double startTick) {
    FollowClip c;
    const std::int64_t length = source->getNumSamples();
    c.clip = Clip::fromBuffer(std::move(source), 0, "loop");
    c.mode = mode;
    c.markers = markersForLoop(0, length, static_cast<double>(length) / kBeatAt120, startTick);
    return c;
}

// ---- 1 -------------------------------------------------------------------------------------------------------

void testMarkers() {
    DAWLAB_CHECK(markersForLoop(0, 0, 4.0, 0.0).empty() && markersForLoop(0, 100, 0.0, 0.0).empty());
    const std::vector<WarpMarker> loop = markersForLoop(1000, 96000, 4.0, 3840.0);
    DAWLAB_CHECK(loop.size() == 2 && areWarpMarkers(loop));
    if (loop.size() == 2)
        DAWLAB_CHECK(loop[0].source == 1000.0 && loop[0].tick == 3840.0 && loop[1].source == 97000.0 && loop[1].tick == 7680.0);
    DAWLAB_CHECK((!areWarpMarkers({}) && !areWarpMarkers({{0.0, 0.0}}) && !areWarpMarkers({{0.0, 0.0}, {0.0, 10.0}}) && !areWarpMarkers({{0.0, 5.0}, {9.0, 5.0}})));
    WarpMap warp;
    DAWLAB_CHECK((!warp.set({{0.0, 0.0}}) && warp.set(loop)));
    DAWLAB_CHECK(std::fabs(warp.sourceAt(5760.0) - 49000.0) < 1e-6 && std::fabs(warp.tickAt(25000.0) - 4800.0) < 1e-6);

    // A take from 1 s to 7 s over 120, 90 from bar 2, 150 from bar 3; its audio starts at sample 500 of its file.
    const TempoMap jumps = mapOf({{0, 120.0, false}, {kBar, 90.0, false}, {2 * kBar, 150.0, false}});
    const TempoMap constant = mapOf({{0, 120.0, false}});
    DAWLAB_CHECK(markersForTake(constant, kSampleRate, -1, 0, 100, 0.001).empty() && markersForTake(constant, kSampleRate, 0, 0, 0, 0.001).empty() &&
                 markersForTake(constant, kSampleRate, 0, 0, 100, 0.0).empty() && markersForTake(constant, 0.0, 0, 0, 100, 0.001).empty());
    const std::vector<WarpMarker> take = markersForTake(jumps, kSampleRate, 48000, 500, 6 * 48000);
    DAWLAB_CHECK(take.size() == 4 && areWarpMarkers(take));
    if (take.size() == 4) {
        DAWLAB_CHECK(take[0].source == 500.0 && std::fabs(take[0].tick - 1920.0) < 1e-6 && std::fabs(take[1].source - 48500.0) < 1e-6 && take[1].tick == 3840.0);
        DAWLAB_CHECK(std::fabs(take[2].source - (500.0 + (1.0 + 4.0 * 60.0 / 90.0) * 48000.0)) < 1e-6 && take[3].source == 500.0 + 6 * 48000.0);
    }

    // Over random maps with ramps: with its markers and the same map a take plays where it was recorded.
    std::mt19937 rng(60);
    double worst = 0.0;
    size_t markers = 0;
    int refused = 0;
    for (int c = 0; c < 150; ++c) {
        std::vector<TempoPoint> points;
        std::int64_t tick = 0;
        for (int i = 0, n = std::uniform_int_distribution<int>(1, 10)(rng); i < n; ++i) {
            points.push_back({tick, std::uniform_int_distribution<int>(500, 2400)(rng) / 10.0, std::uniform_int_distribution<int>(0, 2)(rng) != 0});
            tick += std::uniform_int_distribution<int>(200, 12000)(rng);
        }
        const TempoMap map = mapOf(points);
        const std::int64_t start = std::uniform_int_distribution<std::int64_t>(0, 400000)(rng), length = std::uniform_int_distribution<std::int64_t>(1, 2000000)(rng);
        const std::int64_t offset = std::uniform_int_distribution<std::int64_t>(0, 5000)(rng);
        WarpMap w;
        const std::vector<WarpMarker> m = markersForTake(map, kSampleRate, start, offset, length, 0.0005);
        if (!w.set(m)) {
            ++refused;
            continue;
        }
        markers += m.size();
        for (int k = 0; k <= 400; ++k) {
            const double n = static_cast<double>(length) * k / 400.0;
            worst = std::max(worst, std::fabs(map.secondsAt(w.tickAt(static_cast<double>(offset) + n)) - (static_cast<double>(start) + n) / kSampleRate));
        }
    }
    std::printf("  150 takes over random maps: %d refused, the worst sample %.3f ms from where it was recorded (%zu markers)\n", refused, worst * 1e3, markers);
    DAWLAB_CHECK(refused == 0 && worst <= 0.0005);
}

// ---- 2 -------------------------------------------------------------------------------------------------------

void testRender() {
    const TempoMap at120 = mapOf({{0, 120.0, false}}), at60 = mapOf({{0, 60.0, false}}), at240 = mapOf({{0, 240.0, false}});
    const auto tone = sineBuffer({440.0}, 96000, 0.25); // one bar at 120 BPM

    // Off: the clip. Start: at its tick, as it is.
    FollowClip plain;
    plain.clip = Clip::fromBuffer(tone, 96000, "plain");
    plain.clip.gainDb = -3.0f;
    Clip out;
    DAWLAB_CHECK(renderFollowClip(plain, at60, kSampleRate, out) && out.start == 96000 && out.length == 96000 && out.source == tone && out.gainDb == -3.0f);
    plain.mode = FollowMode::Start;
    anchorStart(plain, at120, kSampleRate);
    DAWLAB_CHECK(std::fabs(plain.startTick - 3840.0) < 1e-6);
    DAWLAB_CHECK(renderFollowClip(plain, at120, kSampleRate, out) && out.start == 96000);
    DAWLAB_CHECK(renderFollowClip(plain, at60, kSampleRate, out) && out.start == 192000 && out.length == 96000 && out.source == tone);
    DAWLAB_CHECK((songRangeOf(plain, at60, kSampleRate) == std::make_pair<std::int64_t, std::int64_t>(192000, 288000)));

    // Varispeed: a loop on bar 2, at half and at twice its tempo.
    const FollowClip tape = loopClip(tone, FollowMode::Varispeed, 3840.0);
    DAWLAB_CHECK(tape.isWarped());
    Clip slow, fast, same;
    DAWLAB_CHECK(renderFollowClip(tape, at60, kSampleRate, slow) && renderFollowClip(tape, at240, kSampleRate, fast) && renderFollowClip(tape, at120, kSampleRate, same));
    DAWLAB_CHECK(slow.start == 192000 && slow.length == 192000 && slow.sourceOffset == 0 && slow.source && slow.source != tone && slow.source->getNumSamples() == 192000);
    DAWLAB_CHECK(fast.start == 48000 && fast.length == 48000 && same.start == 96000 && same.length == 96000);
    if (slow.source && fast.source && same.source && slow.source->getNumSamples() == 192000 && fast.source->getNumSamples() == 48000) {
        const Tone low = measureTone(*slow.source, 0, 4096, 188000, 220.0), high = measureTone(*fast.source, 0, 4096, 44000, 880.0);
        std::printf("  varispeed, a 440 Hz loop at half and at twice its tempo: 220 Hz (the rest %.0f dB below) and 880 Hz (%.0f dB)\n", low.noiseDb, high.noiseDb);
        DAWLAB_CHECK(low.noiseDb > 80.0 && high.noiseDb > 80.0 && std::fabs(low.rms - 0.25 / std::sqrt(2.0)) < 0.001);
        DAWLAB_CHECK(maxAbsDiff(*same.source, *tone) < 1e-5f); // at its own tempo it is the loop
    }

    // Stretch: as long, and still 440 Hz.
    const FollowClip stretched = loopClip(tone, FollowMode::Stretch, 3840.0);
    Clip slowKept, fastKept;
    DAWLAB_CHECK(renderFollowClip(stretched, at60, kSampleRate, slowKept) && renderFollowClip(stretched, at240, kSampleRate, fastKept));
    DAWLAB_CHECK(slowKept.start == 192000 && slowKept.length == 192000 && fastKept.start == 48000 && fastKept.length == 48000);
    if (slowKept.source && fastKept.source && slowKept.source->getNumSamples() == 192000 && fastKept.source->getNumSamples() == 48000) {
        const Tone a = measureTone(*slowKept.source, 0, 4096, 188000, 440.0), b = measureTone(*fastKept.source, 0, 4096, 44000, 440.0);
        std::printf("  stretch, the same loop: 440 Hz at both tempos (%+.2f and %+.2f cents, the rest %.0f and %.0f dB below)\n", a.cents, b.cents, a.noiseDb, b.noiseDb);
        DAWLAB_CHECK(a.noiseDb > 28.0 && b.noiseDb > 28.0 && std::fabs(a.cents) < 2.0 && std::fabs(b.cents) < 2.0);
        DAWLAB_CHECK(std::fabs(a.rms - 0.25 / std::sqrt(2.0)) < 0.005 && std::fabs(b.rms - 0.25 / std::sqrt(2.0)) < 0.005);
    }

    // Two channels stay two channels; in a stretch both slide together.
    const auto stereo = sineBuffer({440.0, 660.0}, 96000, 0.25);
    Clip st, sv;
    DAWLAB_CHECK(renderFollowClip(loopClip(stereo, FollowMode::Stretch, 0.0), at60, kSampleRate, st) && renderFollowClip(loopClip(stereo, FollowMode::Varispeed, 0.0), at60, kSampleRate, sv));
    DAWLAB_CHECK(st.source && sv.source && st.source->getNumChannels() == 2 && sv.source->getNumChannels() == 2);
    if (st.source && sv.source && st.source->getNumChannels() == 2 && sv.source->getNumChannels() == 2) {
        DAWLAB_CHECK(measureTone(*sv.source, 0, 4096, 188000, 220.0).noiseDb > 80.0 && measureTone(*sv.source, 1, 4096, 188000, 330.0).noiseDb > 80.0);
        const Tone left = measureTone(*st.source, 0, 4096, 188000, 440.0), right = measureTone(*st.source, 1, 4096, 188000, 660.0);
        DAWLAB_CHECK(std::fabs(left.cents) < 3.0 && std::fabs(right.cents) < 3.0 && left.rms > 0.1 && right.rms > 0.1);
    }

    // A range of the source: the loop's second and third beat only.
    FollowClip part = tape;
    part.clip.sourceOffset = 24000;
    part.clip.length = 48000;
    Clip partOut;
    DAWLAB_CHECK(renderFollowClip(part, at60, kSampleRate, partOut) && partOut.start == 240000 && partOut.length == 96000);

    // A take cut in two: the halves meet at any tempo, and together they are the uncut take, sample for sample.
    FollowClip whole;
    whole.clip = Clip::fromBuffer(sineBuffer({330.0}, 192000, 0.25), 0, "take");
    whole.mode = FollowMode::Varispeed;
    whole.markers = markersForTake(at120, kSampleRate, 0, 0, 192000);
    FollowClip a = whole, b = whole;
    a.clip.length = 70000;
    b.clip.sourceOffset = 70000;
    b.clip.length = 122000;
    const TempoMap bends = mapOf({{0, 100.0, true}, {kBar, 150.0, false}, {2 * kBar, 80.0, false}});
    Clip wholeOut, aOut, bOut;
    DAWLAB_CHECK(renderFollowClip(whole, bends, kSampleRate, wholeOut) && renderFollowClip(a, bends, kSampleRate, aOut) && renderFollowClip(b, bends, kSampleRate, bOut));
    DAWLAB_CHECK(aOut.start == wholeOut.start && aOut.end() == bOut.start && bOut.end() == wholeOut.end());
    float seam = 1.0f;
    if (aOut.source && bOut.source && wholeOut.source && aOut.end() == bOut.start && bOut.end() == wholeOut.end()) {
        seam = 0.0f;
        for (std::int64_t i = 0; i < wholeOut.length; ++i) {
            const float cut = i < aOut.length ? aOut.source->getSample(0, static_cast<int>(i)) : bOut.source->getSample(0, static_cast<int>(i - aOut.length));
            seam = std::max(seam, std::fabs(cut - wholeOut.source->getSample(0, static_cast<int>(i))));
        }
    }
    DAWLAB_CHECK(seam == 0.0f);

    // Refused: longer than the engine will render (a clip of 2^27 samples at an eighth of its tempo), and a
    // render that is cancelled. `out` is not touched.
    FollowClip huge = tape;
    huge.clip.length = std::int64_t{1} << 27;
    huge.markers = markersForLoop(0, huge.clip.length, static_cast<double>(huge.clip.length) / kBeatAt120, 0.0);
    Clip untouched;
    untouched.start = 77;
    DAWLAB_CHECK((!renderFollowClip(huge, mapOf({{0, 20.0, false}}), kSampleRate, untouched) && untouched.start == 77 && !untouched.source));
    std::atomic<bool> cancel{true};
    DAWLAB_CHECK(!renderFollowClip(tape, at60, kSampleRate, untouched, &cancel) && !renderFollowClip(stretched, at60, kSampleRate, untouched, &cancel) && untouched.start == 77);
    cancel = false;
    DAWLAB_CHECK(renderFollowClip(tape, at60, kSampleRate, untouched, &cancel) && untouched.start == 192000);

    // A warped mode without markers, or without audio, plays as it is.
    FollowClip lost = tape;
    lost.markers.clear();
    lost.clip.start = 1234;
    DAWLAB_CHECK(!lost.isWarped() && renderFollowClip(lost, at60, kSampleRate, out) && out.start == 1234 && out.source == tone);
    FollowClip empty = tape;
    empty.clip.source.reset();
    empty.clip.start = 55;
    DAWLAB_CHECK(renderFollowClip(empty, at60, kSampleRate, out) && out.start == 55 && !out.source);

    // A placeholder: the last render, where the clip starts now; with no render yet, the clip's own audio.
    const Clip held = placeholderFor(tape, &slow, at240, kSampleRate), bare = placeholderFor(tape, nullptr, at240, kSampleRate);
    DAWLAB_CHECK(held.start == 48000 && held.source == slow.source && held.length == slow.length && bare.start == 48000 && bare.source == tone);
}

// ---- 3 -------------------------------------------------------------------------------------------------------

void testRenderer() {
    const TempoMap at60 = mapOf({{0, 60.0, false}}), at240 = mapOf({{0, 240.0, false}});
    const FollowClip tape = loopClip(sineBuffer({440.0}, 96000, 0.25), FollowMode::Varispeed, 0.0);
    FollowRenderer renderer;
    DAWLAB_CHECK(renderer.isIdle() && renderer.collect().empty());
    const FollowRenderer::Key key = renderer.newKey(), other = renderer.newKey();
    DAWLAB_CHECK(key != other);

    // One request: one result, once.
    const std::uint64_t first = renderer.request(key, tape, at60, kSampleRate);
    renderer.waitUntilIdle();
    std::vector<FollowRenderer::Result> done = renderer.collect();
    DAWLAB_CHECK(done.size() == 1 && renderer.collect().empty());
    if (done.size() == 1)
        DAWLAB_CHECK(done[0].key == key && done[0].generation == first && done[0].clip.length == 192000 && done[0].clip.source);

    // Two requests of one key: only the newer one comes back, whether the older was finished, cancelled or still waiting.
    const std::uint64_t older = renderer.request(key, tape, at60, kSampleRate);
    const std::uint64_t newer = renderer.request(key, tape, at240, kSampleRate);
    const std::uint64_t others = renderer.request(other, tape, at60, kSampleRate);
    renderer.waitUntilIdle();
    done = renderer.collect();
    DAWLAB_CHECK(newer > older && done.size() == 2);
    for (const FollowRenderer::Result& r : done)
        DAWLAB_CHECK((r.key == key && r.generation == newer && r.clip.length == 48000) || (r.key == other && r.generation == others && r.clip.length == 192000));

    // A key that is forgotten gives nothing, also if its render was already done.
    renderer.request(key, tape, at60, kSampleRate);
    renderer.waitUntilIdle();
    renderer.forget(key);
    DAWLAB_CHECK(renderer.collect().empty());
    renderer.request(other, tape, at60, kSampleRate);
    renderer.forget(other);
    renderer.waitUntilIdle();
    DAWLAB_CHECK(renderer.collect().empty() && renderer.isIdle());

    // A renderer that is destroyed while it works stops, and does not hang (a long stretch is waiting here).
    {
        FollowRenderer busy;
        const FollowClip longOne = loopClip(sineBuffer({440.0}, 20 * 48000, 0.25), FollowMode::Stretch, 0.0);
        for (int i = 0; i < 4; ++i)
            busy.request(busy.newKey(), longOne, at60, kSampleRate);
    }
    DAWLAB_CHECK(true);
}

// ---- 4 -------------------------------------------------------------------------------------------------------

// The samples where the mono track's output has a peak above `threshold`, one per click.
std::vector<int> peaksOf(const AudioBuffer& out, float threshold) {
    std::vector<int> peaks;
    const auto at = [&out](int i) { return i >= 0 && i < out.getNumSamples() ? std::fabs(out.getSample(0, i)) : 0.0f; };
    for (int i = 0; i < out.getNumSamples(); ++i) {
        const float v = at(i);
        if (v > threshold && v >= at(i - 1) && v > at(i + 1) && (peaks.empty() || i - peaks.back() > 200))
            peaks.push_back(i);
    }
    return peaks;
}

void testThroughTheEngine() {
    Engine e;
    e.prepare(kSampleRate, kBlockSize);
    addTestTracks(e);
    const TempoMap at120 = mapOf({{0, 120.0, false}});
    const TempoMap bends = mapOf({{0, 100.0, false}, {kBar, 150.0, false}, {2 * kBar, 75.0, false}});
    const auto send = [&](const TempoMap& map, FollowTrack& track) {
        e.getTransport().setTempoMap(map);
        DAWLAB_CHECK(e.setTrackClips(testId(0), track.playable()));
        e.applyPendingCommandsOffline();
        e.collectGarbage();
    };

    // The mix's own delay: where a click at sample 0 comes out.
    FollowRenderer renderer;
    FollowTrack track(renderer);
    const auto clicks = clicksBuffer(12, 0.5f);
    FollowClip take;
    take.clip = Clip::fromBuffer(clicks, 0, "clicks");
    track.update({take}, at120, kSampleRate);
    DAWLAB_CHECK(track.isUpToDate() && track.getNumClips() == 1 && renderer.isIdle()); // Off: nothing to render
    send(at120, track);
    const std::vector<int> plainPeaks = peaksOf(render(e, 13 * kBeatAt120), 0.1f);
    DAWLAB_CHECK(plainPeaks.size() == 12);
    const int delay = plainPeaks.empty() ? 0 : plainPeaks[0];

    // The take follows by varispeed, with the markers of the map it was recorded over: it plays as it was.
    take.mode = FollowMode::Varispeed;
    take.markers = markersForTake(at120, kSampleRate, 0, 0, clicks->getNumSamples());
    track.update({take}, at120, kSampleRate);
    DAWLAB_CHECK(!track.isUpToDate());
    const std::vector<Clip> waiting = track.playable();
    DAWLAB_CHECK(waiting.size() == 1 && waiting[0].source == clicks && waiting[0].start == 0); // no render yet: its own audio
    renderer.waitUntilIdle();
    DAWLAB_CHECK(track.take(renderer.collect()) && track.isUpToDate() && !track.take(renderer.collect()));
    send(at120, track);
    DAWLAB_CHECK(peaksOf(render(e, 13 * kBeatAt120), 0.1f) == plainPeaks);
    const Clip firstRender = track.playable()[0];

    // A new map. At once: the last render where the take starts now. Then: the render, and every click on its beat.
    track.update({take}, bends, kSampleRate);
    const std::vector<Clip> held = track.playable();
    DAWLAB_CHECK(!track.isUpToDate() && held.size() == 1 && held[0].source == firstRender.source && held[0].start == 0 && held[0].length == firstRender.length);
    renderer.waitUntilIdle();
    DAWLAB_CHECK(track.take(renderer.collect()) && track.isUpToDate());
    send(bends, track);
    const std::int64_t end = std::llround(bends.secondsAt(12.0 * kBeat) * kSampleRate);
    DAWLAB_CHECK(track.playable()[0].length == end && track.playable()[0].source != firstRender.source);
    const std::vector<int> peaks = peaksOf(render(e, end + kBeatAt120), 0.1f);
    int offBeat = 0;
    for (size_t k = 0; k < peaks.size() && k < 12; ++k)
        offBeat += std::llabs(peaks[k] - delay - std::llround(bends.secondsAt(static_cast<double>(k) * kBeat) * kSampleRate)) <= 1 ? 0 : 1;
    std::printf("  12 clicks recorded at 120 BPM, through the engine over 100, 150 and 75 BPM: %zu heard, %d off their beats (the last at %.3f s)\n", peaks.size(),
                offBeat, peaks.empty() ? 0.0 : (peaks.back() - delay) / kSampleRate);
    DAWLAB_CHECK(peaks.size() == 12 && offBeat == 0);

    // A new gain, a new name: no render, and the engine's clip has them.
    const Clip before = track.playable()[0];
    take.clip.gainDb = -6.0f;
    take.clip.name = "quieter";
    take.clip.fadeInSamples = 100;
    track.update({take}, bends, kSampleRate);
    DAWLAB_CHECK(track.isUpToDate() && renderer.isIdle() && renderer.collect().empty());
    DAWLAB_CHECK(track.playable()[0].source == before.source && track.playable()[0].gainDb == -6.0f && track.playable()[0].name == "quieter" &&
                 track.playable()[0].fadeInSamples == 100 && track.playable()[0].start == before.start && track.playable()[0].length == before.length);

    // Back to a clip that does not follow, and a second clip: placed at once.
    FollowClip second;
    second.clip = Clip::fromBuffer(clicks, 5000, "second");
    second.mode = FollowMode::Start;
    anchorStart(second, bends, kSampleRate);
    take.mode = FollowMode::Off;
    track.update({take, second}, at120, kSampleRate);
    DAWLAB_CHECK(track.isUpToDate() && track.getNumClips() == 2 && track.playable()[0].source == clicks && track.playable()[0].start == 0);
    DAWLAB_CHECK(track.playable()[1].start == sampleOfTick(at120, kSampleRate, second.startTick) && track.getClip(1).mode == FollowMode::Start);
    track.update({}, at120, kSampleRate);
    DAWLAB_CHECK(track.getNumClips() == 0 && track.playable().empty() && track.isUpToDate());
    send(at120, track);
}

// ---- 5 -------------------------------------------------------------------------------------------------------

void testManyMaps() {
    // The user drags a tempo point: a map every few milliseconds, each asking for a new render of a stretch.
    FollowRenderer renderer;
    FollowTrack track(renderer);
    const FollowClip loop = loopClip(sineBuffer({440.0}, 4 * 96000, 0.25), FollowMode::Stretch, 0.0);
    std::mt19937 rng(61);
    int wrong = 0, taken = 0;
    double bpm = 120.0;
    for (int i = 0; i < 120; ++i) {
        bpm = 60.0 + std::uniform_int_distribution<int>(0, 1200)(rng) / 10.0;
        track.update({loop}, mapOf({{0, bpm, false}}), kSampleRate);
        if (i % 7 == 3)
            std::this_thread::sleep_for(std::chrono::milliseconds(std::uniform_int_distribution<int>(1, 30)(rng)));
        if (track.take(renderer.collect()))
            ++taken;
        // Whatever is up to date is the render of THIS map.
        if (track.isUpToDate())
            wrong += std::llabs(track.playable()[0].length - std::llround(4 * 96000 * 120.0 / bpm)) <= 1 ? 0 : 1;
    }
    renderer.waitUntilIdle();
    const bool last = track.take(renderer.collect());
    const std::int64_t want = std::llround(4 * 96000 * 120.0 / bpm);
    std::printf("  120 maps in a row: %d renders taken on the way, the render at the end is the last map's (%lld samples, wanted %lld)\n", taken + (last ? 1 : 0),
                static_cast<long long>(track.playable()[0].length), static_cast<long long>(want));
    DAWLAB_CHECK(track.isUpToDate() && wrong == 0 && std::llabs(track.playable()[0].length - want) <= 1 && renderer.collect().empty());
}

// ---- 6 -------------------------------------------------------------------------------------------------------

void testThreads() {
    Engine e;
    e.prepare(kSampleRate, kBlockSize);
    addTestTracks(e);
    FollowRenderer renderer;
    FollowTrack track(renderer);
    FollowClip loop = loopClip(sineBuffer({440.0}, 2 * 96000, 0.1), FollowMode::Varispeed, 0.0);
    FollowClip stretched = loopClip(sineBuffer({330.0}, 24000, 0.1), FollowMode::Stretch, 2.0 * kBar);
    track.update({loop, stretched}, mapOf({{0, 120.0, false}}), kSampleRate);
    e.setTrackClips(testId(0), track.playable());
    e.getTransport().setLoopRange(0, 8 * 48000);
    e.getTransport().setLoopEnabled(true);
    e.getTransport().play();
    e.applyPendingCommandsOffline();

    std::atomic<bool> stop{false};
    std::atomic<long> audioAllocs{0};
    std::atomic<int> blocks{0};
    std::thread audio([&] {
        AudioBuffer block(kNumChannels, kBlockSize);
        while (!stop.load(std::memory_order_relaxed)) {
            const long before = gAllocs.load();
            tCounting = true;
            e.processBlock(block.toBlock());
            tCounting = false;
            audioAllocs.fetch_add(gAllocs.load() - before, std::memory_order_relaxed);
            blocks.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::yield();
        }
    });

    std::mt19937 rng(62);
    int handed = 0;
    for (int i = 0; i < 300; ++i) {
        const TempoMap map = mapOf({{0, 70.0 + std::uniform_int_distribution<int>(0, 100)(rng), i % 3 == 0}, {4 * kBar, 90.0 + (i % 50), false}});
        if (i % 20 == 0 || i % 20 == 1) { // two maps close together, then time for the render
            e.getTransport().setTempoMap(map);
            track.update({loop, stretched}, map, kSampleRate);
            e.setTrackClips(testId(0), track.playable()); // the placeholders
        }
        if (track.take(renderer.collect())) {
            e.setTrackClips(testId(0), track.playable()); // the renders
            ++handed;
        }
        e.collectGarbage();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    renderer.waitUntilIdle();
    if (track.take(renderer.collect())) {
        e.setTrackClips(testId(0), track.playable());
        ++handed;
    }
    while (blocks.load() < 50) // on a slow machine: let the audio thread have played something
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    stop = true;
    audio.join();
    e.collectGarbage();
    std::printf("  an audio thread next to a UI thread that changes the map and a worker that renders: %d renders handed over in %d blocks,\n"
                "    %ld allocations on the audio thread\n",
                handed, blocks.load(), audioAllocs.load());
    DAWLAB_CHECK(handed > 0 && track.isUpToDate() && audioAllocs.load() == 0);
}

} // namespace

int main() {
    std::printf("l20_test_warp: audio clips that follow the tempo\n");
    testMarkers();
    testRender();
    testRenderer();
    testThroughTheEngine();
    testManyMaps();
    testThreads();
    return dawlab::test::summary("l20_test_warp");
}
