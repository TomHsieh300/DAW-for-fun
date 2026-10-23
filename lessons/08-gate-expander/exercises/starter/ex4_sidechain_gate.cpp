// Exercise 4 starter: sidechain -- use a kick track to control a pad track (ducking and a trance gate)
//
// What to do:
//   (a) Complete Ducker::nextGain(): detect the envelope of the sidechain, and when it is above the threshold
//       pull the main signal down by duckDb; the gain reduction is smoothed in the dB domain with attack / release
//       (hint: the Branching mode of AttackReleaseFilter).
//   (b) Complete the trance gate in main(): use Gate::process(main, sidechain) so the pad only passes when a kick occurs.
// The test material and the self-check are already written; once the TODOs are filled in, running l08_ex4_starter
// should print PASS.
// Hint: the solution is in solutions/ex4_sidechain_gate.cpp.

#include <dawlab/Common.h>
#include <dawlab/dsp/EnvelopeFollower.h>
#include <dawlab/dsp/Gate.h>

#include <cstdio>

using namespace dawlab;
using dawlab::dsp::AttackReleaseFilter;
using dawlab::dsp::DetectorMode;
using dawlab::dsp::EnvelopeFollower;
using dawlab::dsp::Gate;

namespace {

constexpr int kBlockSize = 512;
constexpr double kSampleRate = 48000.0;
constexpr double kKickPeriodSec = 0.5;

class Ducker {
public:
    void prepare(double sampleRate, int /*maxBlockSize*/, int /*numChannels*/) {
        detector_.prepare(sampleRate);
        detector_.setMode(DetectorMode::Peak);
        detector_.setAttackMs(1.0f);
        detector_.setReleaseMs(detectorReleaseMs_);
        smoother_.prepare(sampleRate);
        smoother_.setAttackMs(attackMs_);
        smoother_.setReleaseMs(releaseMs_);
        reset();
    }
    void reset() noexcept {
        detector_.reset();
        smoother_.reset();
    }
    void setThresholdDb(float db) noexcept { thresholdDb_ = db; }
    void setDuckDb(float db) noexcept { duckDb_ = std::abs(db); }
    void setAttackMs(float ms) noexcept {
        attackMs_ = ms;
        smoother_.setAttackMs(ms);
    }
    void setReleaseMs(float ms) noexcept {
        releaseMs_ = ms;
        smoother_.setReleaseMs(ms);
    }

    // Feed in a detection sample from the sidechain; returns the linear gain to multiply onto main
    float nextGain(float sidechainSample) noexcept {
        // TODO(a):
        //   1. envDb = gainToDb(detector_.processSample(sidechainSample))
        //   2. targetGr = envDb >= thresholdDb_ ? duckDb_ : 0
        //   3. gr = smoother_.process(targetGr)   <- smoothing in the dB domain
        //   4. return dbToGain(-gr)
        (void)sidechainSample;
        return 1.0f;
    }

    void process(AudioBlock main, const AudioBlock& sidechain) noexcept {
        const int n = std::min(main.getNumSamples(), sidechain.getNumSamples());
        for (int i = 0; i < n; ++i) {
            float det = 0.0f;
            for (int ch = 0; ch < sidechain.getNumChannels(); ++ch)
                det = std::max(det, std::abs(sidechain.getChannelPointer(ch)[i]));
            const float g = nextGain(det);
            for (int ch = 0; ch < main.getNumChannels(); ++ch)
                main.getChannelPointer(ch)[i] *= g;
        }
    }

private:
    EnvelopeFollower detector_;
    AttackReleaseFilter smoother_;
    float thresholdDb_ = -20.0f;
    float duckDb_ = 12.0f;
    float attackMs_ = 5.0f;
    float releaseMs_ = 50.0f;
    float detectorReleaseMs_ = 30.0f;
};

AudioBuffer makePad(double seconds) {
    AudioBuffer b(2, secondsToSamples(seconds, kSampleRate));
    for (int i = 0; i < b.getNumSamples(); ++i) {
        const auto v = static_cast<float>(0.2 * std::sin(kTwoPi * 220.0 * i / kSampleRate) +
                                          0.2 * std::sin(kTwoPi * 330.0 * i / kSampleRate));
        b.setSample(0, i, v);
        b.setSample(1, i, v);
    }
    return b;
}

AudioBuffer makeKicks(double seconds) {
    AudioBuffer b(1, secondsToSamples(seconds, kSampleRate));
    const int len = secondsToSamples(0.3, kSampleRate);
    for (double t0 = 0.0; t0 < seconds; t0 += kKickPeriodSec) {
        const int start = secondsToSamples(t0, kSampleRate);
        double phase = 0.0;
        for (int i = 0; i < len && start + i < b.getNumSamples(); ++i) {
            const double s = i / kSampleRate;
            const double f = 45.0 + 75.0 * std::exp(-s * 30.0);
            phase += kTwoPi * f / kSampleRate;
            b.addSample(0, start + i, static_cast<float>(0.9 * std::sin(phase) * std::exp(-s * 14.0)));
        }
    }
    return b;
}

template <typename Fn>
AudioBuffer runBlocks(const AudioBuffer& main, const AudioBuffer& sidechain, Fn&& processBlock) {
    AudioBuffer out = main;
    AudioBlock whole(out);
    AudioBlock sc(const_cast<AudioBuffer&>(sidechain));
    const int n = out.getNumSamples();
    for (int start = 0; start < n; start += kBlockSize) {
        const int len = std::min(kBlockSize, n - start);
        processBlock(whole.getSubBlock(start, len), sc.getSubBlock(start, len));
    }
    return out;
}

double windowRmsDb(const AudioBuffer& b, double fromMs, double toMs) {
    double sum = 0.0;
    int count = 0;
    for (int k = 1; k <= 4; ++k) {
        const int start = secondsToSamples(k * kKickPeriodSec + fromMs * 0.001, kSampleRate);
        const int n = secondsToSamples((toMs - fromMs) * 0.001, kSampleRate);
        const float r = rms(b.getReadPointer(0) + start, n);
        sum += r > 0.0f ? 20.0 * std::log10(r) : -120.0;
        ++count;
    }
    return sum / count;
}

} // namespace

int main() {
    std::printf("Chapter 08 Exercise 4 (starter): sidechain ducking and trance gate\n");
    const double seconds = 3.0;
    const AudioBuffer pad = makePad(seconds);
    const AudioBuffer kicks = makeKicks(seconds);
    writeWav(outputPath("l08_ex4_pad.wav"), pad, kSampleRate);
    writeWav(outputPath("l08_ex4_kick.wav"), kicks, kSampleRate);

    Ducker ducker;
    ducker.prepare(kSampleRate, kBlockSize, 2);
    ducker.setThresholdDb(-20.0f);
    ducker.setDuckDb(12.0f);
    ducker.setAttackMs(5.0f);
    ducker.setReleaseMs(50.0f);
    const AudioBuffer ducked = runBlocks(pad, kicks, [&](AudioBlock m, AudioBlock s) { ducker.process(m, s); });
    writeWav(outputPath("l08_ex4_ducked.wav"), ducked, kSampleRate);

    const double padRef = windowRmsDb(pad, 15.0, 60.0);
    const double duckDuring = windowRmsDb(ducked, 15.0, 60.0);
    const double duckBetween = windowRmsDb(ducked, 350.0, 490.0);
    std::printf("[a] ducking: pad %.2f dBFS; 15-60 ms after the kick %.2f dBFS (should be 12 dB lower); 350-490 ms %.2f dBFS\n", padRef,
                duckDuring, duckBetween);
    DAWLAB_CHECK_NEAR(padRef - duckDuring, 12.0, 1.0);
    DAWLAB_CHECK_NEAR(duckBetween, padRef, 1.0);

    // TODO(b): create a Gate (threshold -20, hysteresis 6, range -80, attack 1 ms, hold 50 ms, release 50 ms),
    //          and call gate.process(main, sidechain) through runBlocks(pad, kicks, ...) to get gated
    AudioBuffer gated = pad; // <- replace with the gate's output
    writeWav(outputPath("l08_ex4_trance_gate.wav"), gated, kSampleRate);

    const double gateOpen = windowRmsDb(gated, 20.0, 100.0);
    const double gateClosed = windowRmsDb(gated, 350.0, 490.0);
    std::printf("[b] trance gate: 20-100 ms after the kick %.2f dBFS; 350-490 ms %.2f dBFS (should be < -60)\n", gateOpen, gateClosed);
    DAWLAB_CHECK_NEAR(gateOpen, windowRmsDb(pad, 20.0, 100.0), 1.0);
    DAWLAB_CHECK(gateClosed < -60.0);

    return dawlab::test::summary("l08_ex4_starter");
}
