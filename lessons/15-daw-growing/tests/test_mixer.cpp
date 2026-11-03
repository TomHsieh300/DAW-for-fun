// test_mixer: fader, pan law, mute / solo logic, sends and bus / master gain (Steps 4 and 6).
//
// Every track plays a constant ("DC") clip, so each gain stage shows up directly in the
// output sample value and can be compared with the formula:
//   out_L = dc * 10^(fader/20) * panL(pan, law)  (+ bus returns)

#include "TestHelpers.h"

using namespace l14test;

namespace {

constexpr int kLen = 4800; // 100 ms

// Sample value of the master output (channel ch) at the end of a short render.
float renderValue(Engine& e, int ch) {
    AudioBuffer out = render(e, kLen);
    return out.getSample(ch, kLen - 1);
}

void setupTwoDcTracks(Engine& e) {
    e.prepare(kSampleRate, kBlockSize);
    e.setTrackClips(0, {Clip::fromBuffer(makeConstant(0.5f, kLen), 0, "dc 0.5")});
    e.setTrackClips(1, {Clip::fromBuffer(makeConstant(0.25f, kLen), 0, "dc 0.25")});
}

void testPanLawFormulas() {
    float l = 0, r = 0;
    dsp::Panner::computeGains(0.0f, dsp::PanLaw::ConstantPower, l, r);
    DAWLAB_CHECK_NEAR(l, 0.70711, 1e-4);
    DAWLAB_CHECK_NEAR(r, 0.70711, 1e-4);
    DAWLAB_CHECK_NEAR(l * l + r * r, 1.0, 1e-5); // constant power at the centre
    dsp::Panner::computeGains(0.0f, dsp::PanLaw::Linear, l, r);
    DAWLAB_CHECK_NEAR(l, 0.5, 1e-5);
    dsp::Panner::computeGains(0.0f, dsp::PanLaw::Minus4_5dB, l, r);
    DAWLAB_CHECK_NEAR(gainToDb(l), -4.515, 0.02);
    dsp::Panner::computeGains(-1.0f, dsp::PanLaw::ConstantPower, l, r);
    DAWLAB_CHECK_NEAR(l, 1.0, 1e-5);
    DAWLAB_CHECK_NEAR(r, 0.0, 1e-5);
    // constant power everywhere on the arc: L^2 + R^2 = 1
    for (float p = -1.0f; p <= 1.0f; p += 0.25f) {
        dsp::Panner::computeGains(p, dsp::PanLaw::ConstantPower, l, r);
        DAWLAB_CHECK_NEAR(l * l + r * r, 1.0, 1e-4);
    }
}

void testFaderAndPan() {
    Engine e;
    setupTwoDcTracks(e);
    e.getMixer().getTrack(1).setMute(true);

    // centre, ConstantPower: 0.5 * 0.7071
    DAWLAB_CHECK_NEAR(renderValue(e, 0), 0.5 * 0.70711, 1e-4);
    DAWLAB_CHECK_NEAR(renderValue(e, 1), 0.5 * 0.70711, 1e-4);

    // fader -6.02 dB halves the level
    e.getMixer().getTrack(0).setGainDb(-6.0206f);
    DAWLAB_CHECK_NEAR(renderValue(e, 0), 0.25 * 0.70711, 1e-4);

    // hard left: everything in L, nothing in R
    e.getMixer().getTrack(0).setGainDb(0.0f);
    e.getMixer().getTrack(0).setPan(-1.0f);
    DAWLAB_CHECK_NEAR(renderValue(e, 0), 0.5, 1e-4);
    DAWLAB_CHECK_NEAR(renderValue(e, 1), 0.0, 1e-6);

    // other pan laws at the centre
    e.getMixer().getTrack(0).setPan(0.0f);
    e.getMixer().getTrack(0).setPanLaw(dsp::PanLaw::Linear);
    DAWLAB_CHECK_NEAR(renderValue(e, 0), 0.25, 1e-4);
    e.getMixer().getTrack(0).setPanLaw(dsp::PanLaw::Minus4_5dB);
    DAWLAB_CHECK_NEAR(gainToDb(renderValue(e, 0) / 0.5f), -4.515, 0.02);

    // fader at -inf is silence, the fader is clamped at +12 dB
    e.getMixer().getTrack(0).setPanLaw(dsp::PanLaw::ConstantPower);
    e.getMixer().getTrack(0).setGainDb(-200.0f);
    DAWLAB_CHECK_NEAR(renderValue(e, 0), 0.0, 1e-7);
    e.getMixer().getTrack(0).setGainDb(40.0f);
    DAWLAB_CHECK_NEAR(e.getMixer().getTrack(0).getGainDb(), 12.0, 1e-6);

    // master fader acts on the sum
    e.getMixer().getTrack(0).setGainDb(0.0f);
    e.getMixer().getMaster().setGainDb(-6.0206f);
    DAWLAB_CHECK_NEAR(renderValue(e, 0), 0.5 * 0.70711 * 0.5, 1e-4); // master pan = balance, unity at centre

    // master balance hard right: left side silent, right side untouched
    e.getMixer().getMaster().setGainDb(0.0f);
    e.getMixer().getMaster().setPan(1.0f);
    DAWLAB_CHECK_NEAR(renderValue(e, 0), 0.0, 1e-6);
    DAWLAB_CHECK_NEAR(renderValue(e, 1), 0.5 * 0.70711, 1e-4);
}

void testMuteSolo() {
    Engine e;
    setupTwoDcTracks(e);
    Mixer& m = e.getMixer();
    const float g = 0.70711f; // the tracks' centre pan (the master is a unity balance control)

    DAWLAB_CHECK_NEAR(renderValue(e, 0), (0.5 + 0.25) * g, 1e-4); // both tracks

    m.getTrack(0).setMute(true);
    DAWLAB_CHECK(!m.isTrackAudible(0));
    DAWLAB_CHECK_NEAR(renderValue(e, 0), 0.25 * g, 1e-4);

    m.getTrack(0).setMute(false);
    m.getTrack(1).setSolo(true); // solo track 2 -> track 1 silent
    DAWLAB_CHECK(!m.isTrackAudible(0));
    DAWLAB_CHECK(m.isTrackAudible(1));
    DAWLAB_CHECK_NEAR(renderValue(e, 0), 0.25 * g, 1e-4);

    m.getTrack(0).setSolo(true); // both soloed -> both heard
    DAWLAB_CHECK_NEAR(renderValue(e, 0), 0.75 * g, 1e-4);

    m.getTrack(1).setMute(true); // mute wins over solo
    DAWLAB_CHECK_NEAR(renderValue(e, 0), 0.5 * g, 1e-4);

    m.getTrack(0).setSolo(false); // track 2 still soloed but muted -> silence
    DAWLAB_CHECK(m.isAnyTrackSoloed());
    DAWLAB_CHECK_NEAR(renderValue(e, 0), 0.0, 1e-6);

    m.getTrack(1).setSolo(false);
    m.getTrack(1).setMute(false);
    m.getMaster().setMute(true);
    DAWLAB_CHECK_NEAR(renderValue(e, 0), 0.0, 1e-6);
}

void testSends() {
    Engine e;
    setupTwoDcTracks(e);
    Mixer& m = e.getMixer();
    float gl = 0, gr = 0;
    ChannelStrip::computeStripGains(0.0f, 0.0f, dsp::PanLaw::ConstantPower, PanMode::PanLaw, gl, gr); // 0.7071
    DAWLAB_CHECK_NEAR(gl, 0.70711, 1e-4);
    DAWLAB_CHECK_NEAR(gr, 0.70711, 1e-4);

    // track 1: post-fader send to bus A at -6.02 dB (taken after fader and pan)
    m.getTrack(0).setSendLevelDb(0, -6.0206f);
    // track 2: fader closed, but a pre-fader send at 0 dB to bus A still carries it
    m.getTrack(1).setGainDb(kMinusInfinityDb);
    m.getTrack(1).setSendLevelDb(0, 0.0f);
    m.getTrack(1).setSendPreFader(0, true);
    DAWLAB_CHECK(m.getTrack(1).isSendPreFader(0));

    const double direct = 0.5 * gl;                    // track 1 straight to the master
    const double busIn = 0.5 * gl * 0.5 + 0.25 * 1.0;  // post send + pre send (no pan yet)
    const double busOut = busIn;                       // bus A: fader 0 dB, balance centre = x1
    const double expected = direct + busOut;           // master: unity
    DAWLAB_CHECK_NEAR(renderValue(e, 0), expected, 1e-4);

    // the bus meters see exactly busOut
    DAWLAB_CHECK_NEAR(m.getBus(0).getMaxPeakLinear(), busOut, 1e-4);

    // muting bus A removes the return
    m.getBus(0).setMute(true);
    DAWLAB_CHECK_NEAR(renderValue(e, 0), direct, 1e-4);
    m.getBus(0).setMute(false);

    // muting a track also silences its pre-fader send (mute sits before the send tap)
    m.getTrack(1).setMute(true);
    const double busOut2 = 0.5 * gl * 0.5;
    DAWLAB_CHECK_NEAR(renderValue(e, 0), direct + busOut2, 1e-4);
    m.getTrack(1).setMute(false);

    // two sends from one track to two buses add up
    m.getTrack(1).setSendLevelDb(0, kMinusInfinityDb);
    m.getTrack(0).setSendLevelDb(0, -6.0206f);
    m.getTrack(0).setSendLevelDb(1, -6.0206f);
    const double twoBuses = direct + 2.0 * (0.5 * gl * 0.5);
    DAWLAB_CHECK_NEAR(renderValue(e, 0), twoBuses, 1e-4);
}

void testSoloKeepsReverbBus() {
    // Buses are solo-safe: soloing a track keeps its reverb return, drops the others.
    Engine e;
    setupTwoDcTracks(e);
    Mixer& m = e.getMixer();
    m.getTrack(0).setSendLevelDb(0, 0.0f);
    m.getTrack(1).setSendLevelDb(0, 0.0f);
    m.getTrack(0).setSolo(true);
    const double g = 0.70711;
    const double expected = 0.5 * g + 0.5 * g; // direct + post-fader bus return (bus/master unity)
    DAWLAB_CHECK_NEAR(renderValue(e, 0), expected, 1e-4);
}

void testFaderRampIsSmooth() {
    // Moving a fader while playing ramps over 50 ms: no step larger than the ramp slope.
    Engine e;
    e.prepare(kSampleRate, kBlockSize);
    e.setTrackClips(0, {Clip::fromBuffer(makeConstant(1.0f, 48000), 0)});
    e.applyPendingCommandsOffline();
    e.getTransport().play();
    AudioBuffer out(2, 48000);
    for (int pos = 0; pos < 48000; pos += kBlockSize) {
        if (pos == 9 * kBlockSize)
            e.getMixer().getTrack(0).setGainDb(-20.0f);
        const int len = std::min(kBlockSize, 48000 - pos);
        e.processBlock(out.toBlock().getSubBlock(pos, len));
    }
    float maxStep = 0.0f;
    for (int i = 1; i < 48000; ++i)
        maxStep = std::max(maxStep, std::abs(out.getSample(0, i) - out.getSample(0, i - 1)));
    // total change 0.7071 * (1 - 0.1) = 0.636 spread over 2400 samples -> 0.000265 per sample
    DAWLAB_CHECK(maxStep < 0.001f);
    DAWLAB_CHECK_NEAR(out.getSample(0, 47999), 0.70711 * 0.1, 1e-4);
}

} // namespace

int main() {
    testPanLawFormulas();
    testFaderAndPan();
    testMuteSolo();
    testSends();
    testSoloKeepsReverbBus();
    testFaderRampIsSmooth();
    return dawlab::test::summary("l14_test_mixer");
}
