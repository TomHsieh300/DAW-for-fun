#pragma once
// check.h - the self-check of Step 8 (plugin delay compensation). Defines main(); included at
// the END of starter/step08.cpp and solution/step08.cpp.
//
// Tests only what Step 8 adds: the delay line, and where impulses arrive when tracks, buses and
// the master have latent inserts (PureDelayEffect = a pure N-sample delay that reports N).

#include <dawlab/Common.h>

#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

namespace step08_check {

using namespace dawlab;
using namespace minidaw;

constexpr double kSampleRate = 48000.0;
constexpr float g = 0.70710678f;

inline Clip impulseClip(int64_t at, float amp = 0.25f) {
    auto b = std::make_shared<AudioBuffer>(makeImpulse(1, 1, 0, amp));
    return Clip::fromBuffer(b, at);
}

inline std::unique_ptr<InsertEffect> delay(int samples) {
    auto fx = createEffect("PureDelay");
    fx->setParameter(0, static_cast<float>(samples));
    return fx;
}

struct Arrival {
    int at;
    float value;
};

// Plays `total` samples and lists every non-zero output sample (left channel).
inline std::vector<Arrival> arrivals(Engine& engine, int total) {
    engine.getTransport().play();
    AudioBuffer out(kNumChannels, total);
    for (int pos = 0; pos < total; pos += 256)
        engine.processBlock(out.toBlock().getSubBlock(pos, std::min(256, total - pos)));
    engine.collectGarbage();
    std::vector<Arrival> result;
    for (int i = 0; i < total; ++i)
        if (std::abs(out.getSample(0, i)) > 1e-6f)
            result.push_back({i, out.getSample(0, i)});
    return result;
}

inline void print(const char* what, const std::vector<Arrival>& a) {
    std::printf("  %-44s:", what);
    for (const Arrival& x : a)
        std::printf(" %.4f at %d", x.value, x.at);
    std::printf("%s\n", a.empty() ? " (silence)" : "");
}

inline int run() {
    std::printf("Step 8: plugin delay compensation (build-along)\n");

    // ---- 1) the delay line on its own --------------------------------------------------------------
    {
        LatencyCompensator c;
        c.prepare(16, 2);
        AudioBuffer ramp(2, 40);
        for (int i = 0; i < 40; ++i) {
            ramp.setSample(0, i, static_cast<float>(i + 1));
            ramp.setSample(1, i, -static_cast<float>(i + 1));
        }
        AudioBuffer x = ramp;
        c.process(x.toBlock().getSubBlock(0, 10)); // delay 0: unchanged, but the ring is written
        bool passThrough = true;
        for (int i = 0; i < 10; ++i)
            passThrough = passThrough && x.getSample(0, i) == ramp.getSample(0, i);
        c.setDelaySamples(3);
        for (int start = 10; start < 40; start += 2) // blocks of 2: the ring wraps several times
            c.process(x.toBlock().getSubBlock(start, 2));
        bool delayed = true;
        for (int i = 10; i < 40; ++i)
            delayed = delayed && x.getSample(0, i) == ramp.getSample(0, i - 3) && x.getSample(1, i) == ramp.getSample(1, i - 3);
        std::printf("  delay line: delay 0 passes through %s; delay 3 -> y[n] = x[n-3] on both channels %s; "
                    "first sample after 0 -> 3 is x[7] = %.0f (expected 8)\n",
                    passThrough ? "yes" : "NO", delayed ? "yes" : "NO", x.getSample(0, 10));
        DAWLAB_CHECK(passThrough);
        DAWLAB_CHECK(delayed);
        DAWLAB_CHECK(c.setDelaySamples(99) == 16); // clamped to the maximum
    }

    // ---- 2) two tracks, an impulse at 1000 on both, track 1 has a 100-sample delay ----------------
    {
        Engine engine;
        engine.prepare(kSampleRate, 512);
        engine.setTrackClips(0, {impulseClip(1000)});
        engine.setTrackClips(1, {impulseClip(1000)});
        engine.addInsert(ChannelRef::track(0), -1, delay(100));
        const auto a = arrivals(engine, 3000);
        print("PDC on : track 1 delayed by 100", a);
        DAWLAB_CHECK(a.size() == 1 && a[0].at == 1100);
        DAWLAB_CHECK(a.size() == 1 && std::abs(a[0].value - 2 * 0.25f * g) < 1e-5f);
        DAWLAB_CHECK(engine.getMixer().getTrack(0).getCompensator().getDelaySamples() == 0);
        DAWLAB_CHECK(engine.getMixer().getTrack(1).getCompensator().getDelaySamples() == 100);
        DAWLAB_CHECK(engine.getLatencySamples() == 100);
    }
    {
        Engine engine;
        engine.prepare(kSampleRate, 512);
        engine.getMixer().setLatencyCompensationEnabled(false);
        engine.setTrackClips(0, {impulseClip(1000)});
        engine.setTrackClips(1, {impulseClip(1000)});
        engine.addInsert(ChannelRef::track(0), -1, delay(100));
        const auto a = arrivals(engine, 3000);
        print("PDC off: the flam", a);
        DAWLAB_CHECK(a.size() == 2 && a[0].at == 1000 && a[1].at == 1100);
        DAWLAB_CHECK(engine.getLatencySamples() == 0);
    }

    // ---- 3) tracks, buses and master: every path lands at 1000 + 37 + 250 + 64 ----------------------
    {
        Engine engine;
        engine.prepare(kSampleRate, 512);
        engine.setTrackClips(0, {impulseClip(1000)});
        engine.setTrackClips(1, {impulseClip(1000)});
        engine.addInsert(ChannelRef::track(0), -1, delay(37));
        engine.addInsert(ChannelRef::bus(0), -1, delay(250));
        engine.addInsert(ChannelRef::master(), -1, delay(64));
        engine.getMixer().getTrack(0).setSendLevelDb(0, 0.0f); // track 1 -> bus A (250)
        engine.getMixer().getTrack(1).setSendLevelDb(1, 0.0f); // track 2 -> bus B (0)
        const auto a = arrivals(engine, 3000);
        print("tracks 37/0, buses 250/0, master 64", a);
        std::printf("  %-44s: %d (expected 351), track sum waits %d (expected 250)\n", "total latency",
                    engine.getLatencySamples(), engine.getMixer().getTrackSumCompensationSamples());
        DAWLAB_CHECK(a.size() == 1 && a[0].at == 1351);
        DAWLAB_CHECK(a.size() == 1 && std::abs(a[0].value - 4 * 0.25f * g) < 1e-5f); // 4 aligned paths
        DAWLAB_CHECK(engine.getLatencySamples() == 37 + 250 + 64);
        DAWLAB_CHECK(engine.getMixer().getTrackSumCompensationSamples() == 250);
    }

    // ---- 4) beyond the limit: C = 128, track 1 needs 200 -------------------------------------------
    {
        Engine engine;
        engine.prepare(kSampleRate, 512, 128);
        engine.setTrackClips(0, {impulseClip(1000)});
        engine.setTrackClips(1, {impulseClip(1000)});
        engine.addInsert(ChannelRef::track(0), -1, delay(200));
        const auto a = arrivals(engine, 3000);
        print("limit C = 128, track 1 delayed by 200", a);
        std::printf("  %-44s: %s, reported latency %d (expected yes, 128)\n", "overflow", engine.hasPdcOverflow() ? "yes" : "no",
                    engine.getLatencySamples());
        DAWLAB_CHECK(engine.hasPdcOverflow());
        DAWLAB_CHECK(engine.getLatencySamples() == 128);
        DAWLAB_CHECK(a.size() == 2 && a[0].at == 1128 && a[1].at == 1200); // track 2 aligned, track 1 late
    }

    // ---- 5) bypass removes the latency, PDC follows in the next block -------------------------------
    {
        Engine engine;
        engine.prepare(kSampleRate, 512);
        engine.setTrackClips(0, {impulseClip(1000)});
        engine.setTrackClips(1, {impulseClip(1000)});
        InsertEffect* d = engine.addInsert(ChannelRef::track(0), -1, delay(100));
        AudioBuffer out(kNumChannels, 256);
        engine.processBlock(out.toBlock()); // stopped: applies the commands
        DAWLAB_CHECK(engine.getLatencySamples() == 100);
        if (d)
            d->setBypassed(true);
        const auto a = arrivals(engine, 3000);
        DAWLAB_CHECK(engine.getLatencySamples() == 0);
        DAWLAB_CHECK(a.size() == 1 && a[0].at == 1000);
        d = engine.getMixer().getTrack(0).getInserts().getInsert(0);
        if (d)
            d->setParameter(0, 30.0f); // a parameter that changes the latency (like a lookahead)
        if (d)
            d->setBypassed(false);
        engine.getTransport().setPosition(0);
        const auto b = arrivals(engine, 3000);
        print("delay changed to 30 by its parameter", b);
        DAWLAB_CHECK(engine.getLatencySamples() == 30);
        DAWLAB_CHECK(b.size() == 1 && b[0].at == 1030);
    }

    return dawlab::test::summary("l14_step08");
}

} // namespace step08_check

int main() { return step08_check::run(); }
