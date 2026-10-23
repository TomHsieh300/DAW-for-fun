// Exercise 2 (solution): the hand calculations of Exercise 2, checked against the engine.
//
// (a) 44.1 kHz, 96 BPM, 3/4: samples per beat / bar, bar 5 beat 2 in samples and seconds,
//     and which bar:beat:tick sample 1 000 000 is.
// (b) a dotted-eighth delay and a quarter-note LFO at 96 BPM.
// (c) PDC for tracks with 0 / 64 / 240 / 1024 samples of latency, buses with 0 / 512 and a
//     master with 240: every compensator and the total latency.
// (d) fader -6 dB with pan +0.5 (constant power), and the same on a Balance strip.
// (e) memory used by the PDC delay lines with the default maximum of 16384 samples.
//
// The worked derivations are in solutions/README.md; this program only proves the numbers.

#include <dawlab/Common.h>
#include <dawlab/engine/All.h>

#include <cstdio>
#include <memory>

using namespace dawlab;
using namespace dawlab::engine;

int main() {
    // ---- (a) ----
    Transport t;
    t.prepare(44100.0);
    t.setTempo(96.0);
    t.setTimeSignature(3, 4);
    std::printf("(a) samples per beat %.1f, per bar %.1f\n", t.getSamplesPerBeat(), t.getSamplesPerBar());
    DAWLAB_CHECK_NEAR(t.getSamplesPerBeat(), 27562.5, 1e-9);
    DAWLAB_CHECK_NEAR(t.getSamplesPerBar(), 82687.5, 1e-9);
    const int64_t p = t.barsBeatsToSamples({5, 2, 0}); // 13 beats = 358312.5 -> rounds to 358313
    std::printf("    bar 5 beat 2 = sample %lld = %.4f s\n", static_cast<long long>(p), t.toSeconds(p));
    DAWLAB_CHECK(p == 358313);
    DAWLAB_CHECK_NEAR(13.0 * 60.0 / 96.0, 8.125, 1e-12);
    const BarsBeats q = t.samplesToBarsBeats(1000000);
    std::printf("    sample 1000000 = %d:%d:%03d\n", q.bar, q.beat, q.tick);
    DAWLAB_CHECK(q.bar == 13 && q.beat == 1 && q.tick == 269);

    // ---- (b) ----
    const float dottedEighth = DelayEffect::noteToMs(96.0, 0.75);
    std::printf("(b) dotted eighth at 96 BPM = %.3f ms = %.3f samples at 44.1 kHz; quarter-note LFO = %.3f Hz\n",
                static_cast<double>(dottedEighth), dottedEighth * 44.1, 96.0 / 60.0);
    DAWLAB_CHECK_NEAR(dottedEighth, 468.75, 1e-3);
    DAWLAB_CHECK_NEAR(dottedEighth * 44.1, 20671.875, 0.05);

    // ---- (c) ----
    Engine e;
    e.prepare(48000.0, 512);
    const int trackLat[4] = {0, 64, 240, 1024};
    for (int i = 0; i < 4; ++i)
        if (trackLat[i] > 0)
            e.addInsert(ChannelRef::track(i), -1, std::make_unique<PureDelayEffect>(trackLat[i]));
    e.addInsert(ChannelRef::bus(1), -1, std::make_unique<PureDelayEffect>(512));
    e.addInsert(ChannelRef::master(), -1, std::make_unique<PureDelayEffect>(240));
    e.applyPendingCommandsOffline();
    const Mixer& m = e.getMixer();
    std::printf("(c) track compensation: %d %d %d %d | bus: %d %d | track sum: %d | total %d samples = %.2f ms\n",
                m.getTrack(0).getCompensator().getDelaySamples(), m.getTrack(1).getCompensator().getDelaySamples(),
                m.getTrack(2).getCompensator().getDelaySamples(), m.getTrack(3).getCompensator().getDelaySamples(),
                m.getBus(0).getCompensator().getDelaySamples(), m.getBus(1).getCompensator().getDelaySamples(),
                m.getTrackSumCompensationSamples(), e.getLatencySamples(), samplesToMs(e.getLatencySamples(), 48000.0));
    DAWLAB_CHECK(m.getTrack(0).getCompensator().getDelaySamples() == 1024);
    DAWLAB_CHECK(m.getTrack(1).getCompensator().getDelaySamples() == 960);
    DAWLAB_CHECK(m.getTrack(2).getCompensator().getDelaySamples() == 784);
    DAWLAB_CHECK(m.getTrack(3).getCompensator().getDelaySamples() == 0);
    DAWLAB_CHECK(m.getTrack(4).getCompensator().getDelaySamples() == 1024); // empty tracks are padded too
    DAWLAB_CHECK(m.getBus(0).getCompensator().getDelaySamples() == 512);
    DAWLAB_CHECK(m.getBus(1).getCompensator().getDelaySamples() == 0);
    DAWLAB_CHECK(m.getTrackSumCompensationSamples() == 512);
    DAWLAB_CHECK(e.getLatencySamples() == 1776);
    DAWLAB_CHECK_NEAR(samplesToMs(1776, 48000.0), 37.0, 1e-9);

    // ---- (d) ----
    float l = 0, r = 0;
    ChannelStrip::computeStripGains(-6.0f, 0.5f, dsp::PanLaw::ConstantPower, PanMode::PanLaw, l, r);
    std::printf("(d) pan law : L %.5f (%.2f dB)  R %.5f (%.2f dB)\n", static_cast<double>(l),
                static_cast<double>(gainToDb(l)), static_cast<double>(r), static_cast<double>(gainToDb(r)));
    DAWLAB_CHECK_NEAR(l, 0.191797, 1e-5);
    DAWLAB_CHECK_NEAR(r, 0.463039, 1e-5);
    DAWLAB_CHECK_NEAR(gainToDb(l), -14.34, 0.01);
    DAWLAB_CHECK_NEAR(gainToDb(r), -6.69, 0.01);
    ChannelStrip::computeStripGains(-6.0f, 0.5f, dsp::PanLaw::ConstantPower, PanMode::Balance, l, r);
    std::printf("    balance : L %.5f (%.2f dB)  R %.5f (%.2f dB)\n", static_cast<double>(l),
                static_cast<double>(gainToDb(l)), static_cast<double>(r), static_cast<double>(gainToDb(r)));
    DAWLAB_CHECK_NEAR(gainToDb(l), -12.02, 0.01);
    DAWLAB_CHECK_NEAR(gainToDb(r), -6.0, 0.01);

    // ---- (e) ----
    const long long perStrip = (static_cast<long long>(kDefaultMaxPdcSamples) + 1) * kNumChannels * 4;
    const long long total = perStrip * (kNumTracks + kNumBuses + 1 + 1); // 11 strips + the track-sum line
    std::printf("(e) PDC memory: %lld bytes per delay line, %lld bytes in total (%.2f MiB)\n", perStrip, total,
                static_cast<double>(total) / (1024.0 * 1024.0));
    DAWLAB_CHECK(perStrip == 131080);
    DAWLAB_CHECK(total == 1572960);
    return dawlab::test::summary("l14_ex2");
}
