// test_pdc: plugin delay compensation across tracks, buses and the master (Step 8).
//
// Impulses placed at the same timeline sample on different tracks must arrive at the master
// at the same output sample, whatever latency their inserts introduce.

#include "TestHelpers.h"

using namespace l14test;

namespace {

constexpr int kAt = 1000; // timeline position of every impulse
constexpr int kLen = 8000;

void placeImpulse(Engine& e, int track, float amp = 0.25f) {
    e.setTrackClips(track, {Clip::fromBuffer(makeImpulseClip(1, 0, amp), kAt)});
}

void testTwoTracks() {
    Engine e;
    e.prepare(kSampleRate, kBlockSize);
    placeImpulse(e, 0);
    placeImpulse(e, 1);
    e.addInsert(ChannelRef::track(0), -1, std::make_unique<PureDelayEffect>(100));
    e.applyPendingCommandsOffline();
    DAWLAB_CHECK(e.getMixer().getMaxTrackLatencySamples() == 100);
    DAWLAB_CHECK(e.getMixer().getTrack(1).getCompensator().getDelaySamples() == 100);
    DAWLAB_CHECK(e.getMixer().getTrack(0).getCompensator().getDelaySamples() == 0);
    DAWLAB_CHECK(e.getLatencySamples() == 100);

    // PDC on, no output compensation: one combined impulse 100 samples late
    AudioBuffer out = render(e, kLen);
    DAWLAB_CHECK(countAbove(out, 0, 1e-6f) == 1);
    DAWLAB_CHECK(argMaxAbs(out, 0) == kAt + 100);
    DAWLAB_CHECK_NEAR(out.getSample(0, kAt + 100), 2 * 0.25 * 0.70711, 1e-5);

    // with output compensation (what a bounce does): back at kAt
    AudioBuffer comp = render(e, kLen, kBlockSize, true);
    DAWLAB_CHECK(argMaxAbs(comp, 0) == kAt);
    DAWLAB_CHECK(countAbove(comp, 0, 1e-6f) == 1);

    // PDC off: two separate impulses (the "flam" you hear without PDC)
    e.getMixer().setLatencyCompensationEnabled(false);
    AudioBuffer off = render(e, kLen);
    DAWLAB_CHECK(countAbove(off, 0, 1e-6f) == 2);
    DAWLAB_CHECK_NEAR(off.getSample(0, kAt), 0.25 * 0.70711, 1e-5);
    DAWLAB_CHECK_NEAR(off.getSample(0, kAt + 100), 0.25 * 0.70711, 1e-5);
}

void testBusesAndMaster() {
    // track 0: 37-sample insert, dry + send to bus A (bus A has a 250-sample insert)
    // track 1: 0 latency, send to bus B (no insert)
    // master : 64-sample insert
    Engine e;
    e.prepare(kSampleRate, kBlockSize);
    placeImpulse(e, 0, 0.1f);
    placeImpulse(e, 1, 0.1f);
    e.addInsert(ChannelRef::track(0), -1, std::make_unique<PureDelayEffect>(37));
    e.addInsert(ChannelRef::bus(0), -1, std::make_unique<PureDelayEffect>(250));
    e.addInsert(ChannelRef::master(), -1, std::make_unique<PureDelayEffect>(64));
    Mixer& m = e.getMixer();
    m.getTrack(0).setSendLevelDb(0, 0.0f);
    m.getTrack(1).setSendLevelDb(1, 0.0f);
    e.applyPendingCommandsOffline();

    DAWLAB_CHECK(m.getMaxTrackLatencySamples() == 37);
    DAWLAB_CHECK(m.getMaxBusLatencySamples() == 250);
    DAWLAB_CHECK(m.getTrackSumCompensationSamples() == 250);
    DAWLAB_CHECK(m.getBus(1).getCompensator().getDelaySamples() == 250);
    DAWLAB_CHECK(m.getBus(0).getCompensator().getDelaySamples() == 0);
    DAWLAB_CHECK(e.getLatencySamples() == 37 + 250 + 64);

    // 4 paths (2 dry + 2 bus returns) all land on the same sample
    AudioBuffer out = render(e, kLen);
    DAWLAB_CHECK(countAbove(out, 0, 1e-6f) == 1);
    DAWLAB_CHECK(argMaxAbs(out, 0) == kAt + 351);
    DAWLAB_CHECK_NEAR(out.getSample(0, kAt + 351), 4 * 0.1 * 0.70711, 1e-5);

    AudioBuffer comp = render(e, kLen, kBlockSize, true);
    DAWLAB_CHECK(argMaxAbs(comp, 0) == kAt);
}

void testRealEffectsAndLatencyChange() {
    // A real lookahead limiter (chapter 05) on track 0 against a dry track 1.
    Engine e;
    e.prepare(kSampleRate, kBlockSize);
    placeImpulse(e, 0, 0.1f); // well below the ceiling: the limiter only delays it
    placeImpulse(e, 1, 0.1f);
    InsertEffect* lim = e.addInsert(ChannelRef::track(0), -1, std::make_unique<LimiterEffect>());
    e.applyPendingCommandsOffline();
    const int lat = e.getLatencySamples();
    DAWLAB_CHECK(lat > 0);
    std::printf("  limiter latency at 5 ms lookahead: %d samples (theory 240)\n", lat);
    AudioBuffer out = render(e, kLen);
    DAWLAB_CHECK(argMaxAbs(out, 0) == kAt + lat);
    DAWLAB_CHECK(countAbove(out, 0, 1e-3f) == 1); // the two impulses coincide

    // change the lookahead: the latency and the compensation follow at the next block
    lim->setParameter(LimiterEffect::Lookahead, 2.0f);
    e.applyPendingCommandsOffline();
    const int lat2 = e.getLatencySamples();
    std::printf("  limiter latency at 2 ms lookahead: %d samples (theory 96)\n", lat2);
    DAWLAB_CHECK(lat2 < lat && lat2 > 0);
    AudioBuffer out2 = render(e, kLen);
    DAWLAB_CHECK(argMaxAbs(out2, 0) == kAt + lat2);
    DAWLAB_CHECK(countAbove(out2, 0, 1e-3f) == 1);

    // bypassing removes the latency (and the compensation) entirely
    lim->setBypassed(true);
    e.applyPendingCommandsOffline();
    DAWLAB_CHECK(e.getLatencySamples() == 0);
    AudioBuffer out3 = render(e, kLen);
    DAWLAB_CHECK(argMaxAbs(out3, 0) == kAt);
}

void testDeviceChange() {
    // A device switches from 48 kHz to 96 kHz: Engine::prepare() prepares the existing inserts
    // again, so a 5 ms lookahead becomes 480 samples and PDC follows.
    Engine e;
    e.prepare(48000.0, 512);
    e.addInsert(ChannelRef::track(0), -1, std::make_unique<LimiterEffect>());
    e.applyPendingCommandsOffline();
    DAWLAB_CHECK(e.getLatencySamples() == 240);
    e.prepare(96000.0, 256);
    DAWLAB_CHECK(e.getNumInserts(ChannelRef::track(0)) == 1);
    DAWLAB_CHECK(e.getLatencySamples() == 480);
    DAWLAB_CHECK(e.getMixer().getTrack(1).getCompensator().getDelaySamples() == 480);
}

void testPdcLimit() {
    // Track 0 carries 3 x 8191 = 24573 samples of latency, more than one compensator can hold
    // (kDefaultMaxPdcSamples = 16384). Track 1 is dry.
    const int big = 3 * PureDelayEffect::kMaxDelaySamples;
    const int kLong = 40000;
    {
        Engine e;
        e.prepare(kSampleRate, kBlockSize); // C = 16384
        placeImpulse(e, 0);
        placeImpulse(e, 1);
        for (int i = 0; i < 3; ++i)
            e.addInsert(ChannelRef::track(0), -1, std::make_unique<PureDelayEffect>(PureDelayEffect::kMaxDelaySamples));
        e.applyPendingCommandsOffline();
        const Mixer& m = e.getMixer();
        std::printf("  PDC limit %d: track latency %d -> target %d, overflow %s, reported latency %d\n",
                    m.getMaxPdcSamples(), m.getTrack(0).getInsertLatencySamples(), m.getMaxTrackLatencySamples(),
                    m.hasPdcOverflow() ? "yes" : "no", e.getLatencySamples());
        DAWLAB_CHECK(m.getTrack(0).getInsertLatencySamples() == big);
        DAWLAB_CHECK(m.hasPdcOverflow() && e.hasPdcOverflow());
        DAWLAB_CHECK(m.getMaxTrackLatencySamples() == kDefaultMaxPdcSamples);
        DAWLAB_CHECK(m.getTrack(1).getCompensator().getDelaySamples() == kDefaultMaxPdcSamples);
        DAWLAB_CHECK(e.getLatencySamples() == kDefaultMaxPdcSamples); // the latency actually applied
        // The bounce keeps the aligned (dry) path exactly in place; the overflowing path is late
        // by 24573 - 16384 = 8189 samples, but it is still in the file.
        AudioBuffer comp = render(e, kLong, kBlockSize, true);
        DAWLAB_CHECK(countAbove(comp, 0, 1e-6f) == 2);
        DAWLAB_CHECK_NEAR(comp.getSample(0, kAt), 0.25 * 0.70711, 1e-5);
        DAWLAB_CHECK_NEAR(comp.getSample(0, kAt + big - kDefaultMaxPdcSamples), 0.25 * 0.70711, 1e-5);
    }
    {
        // The cure: prepare the engine with a larger PDC limit.
        Engine e;
        e.prepare(kSampleRate, kBlockSize, 32768);
        placeImpulse(e, 0);
        placeImpulse(e, 1);
        for (int i = 0; i < 3; ++i)
            e.addInsert(ChannelRef::track(0), -1, std::make_unique<PureDelayEffect>(PureDelayEffect::kMaxDelaySamples));
        e.applyPendingCommandsOffline();
        DAWLAB_CHECK(!e.hasPdcOverflow());
        DAWLAB_CHECK(e.getLatencySamples() == big);
        AudioBuffer comp = render(e, kLong, kBlockSize, true);
        DAWLAB_CHECK(countAbove(comp, 0, 1e-6f) == 1);
        DAWLAB_CHECK(argMaxAbs(comp, 0) == kAt);
    }
}

} // namespace

int main() {
    testTwoTracks();
    testBusesAndMaster();
    testRealEffectsAndLatencyChange();
    testDeviceChange();
    testPdcLimit();
    return dawlab::test::summary("l14_test_pdc");
}
