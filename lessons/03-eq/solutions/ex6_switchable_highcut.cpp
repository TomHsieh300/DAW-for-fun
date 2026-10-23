// Exercise 6 (solution): a high cut (lowpass) switchable between 12 / 24 dB/oct, with no click when switching
//
// 12 dB/oct = one 2nd-order Butterworth section (Q = 1/√2).
// 24 dB/oct = 4th-order Butterworth = two 2nd-order sections in cascade, with Q of 0.5412 and 1.3066 (butterworthQ(4, k) from Chapter 02).
//   Why not two sections with Q = 0.707? That would give -6 dB at fc, and the corner would not be the "maximally flat" Butterworth shape.
//   The poles of a 4th-order Butterworth sit on the left half of the s-plane circle, one pair every 45°; splitting them into two conjugate pairs yields those two Q values;
//   neither section is -3 dB at fc on its own (the Q=0.5412 one is -5.33, the Q=1.3066 one is +2.32), but multiplied together (dB added) they give exactly -3.01 dB.
// Theoretical value (the digital Butterworth after the bilinear transform):
//   |H(f)|² = 1 / (1 + (tan(π f / fs) / tan(π fc / fs))^(2N))
//   The analog (f/fc)^(2N) is replaced by tan -- this is frequency warping, and far from fc the digital version is steeper than 6N dB/oct.
// How to switch the slope without a click: run both chains at the same time and crossfade with a SmoothedValue over 20 ms.
//   Why does switching directly click? At fc the phase of the 12 dB/oct chain is -90° and that of the 24 dB/oct chain is -180°, so switching instantly = the waveform jumps by a big step.

#include <dawlab/Common.h>
#include <dawlab/dsp/Biquad.h>
#include <dawlab/dsp/SmoothedValue.h>

#include <cstdio>

using namespace dawlab;
using namespace dawlab::dsp;

namespace {

class SwitchableHighCut {
public:
    enum class Slope { Db12, Db24 };

    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        sampleRate_ = sampleRate;
        numChannels_ = numChannels;
        lp12_.prepare(sampleRate, maxBlockSize, numChannels);
        lp24a_.prepare(sampleRate, maxBlockSize, numChannels);
        lp24b_.prepare(sampleRate, maxBlockSize, numChannels);
        mix_.reset(sampleRate, crossfadeSeconds_);
        mix_.setCurrentAndTargetValue(slope_ == Slope::Db24 ? 1.0f : 0.0f);
        updateCoefficients();
        reset();
    }

    void reset() noexcept {
        lp12_.reset();
        lp24a_.reset();
        lp24b_.reset();
    }

    void setCutoff(float hz) noexcept {
        cutoffHz_ = std::clamp(hz, 20.0f, static_cast<float>(0.45 * sampleRate_));
        updateCoefficients();
    }
    void setSlope(Slope slope) noexcept {
        slope_ = slope;
        mix_.setTargetValue(slope == Slope::Db24 ? 1.0f : 0.0f); // crossfade to the other chain
    }
    // Call before prepare; 0 = switch directly (clicks; used as the control)
    void setCrossfadeSeconds(double seconds) noexcept { crossfadeSeconds_ = std::max(0.0, seconds); }
    Slope getSlope() const noexcept { return slope_; }

    // Theoretical response of the target slope
    double getMagnitudeDb(double freq) const noexcept {
        if (slope_ == Slope::Db12)
            return lp12_.getCoefficients().magnitudeDb(freq, sampleRate_);
        return lp24a_.getCoefficients().magnitudeDb(freq, sampleRate_) +
               lp24b_.getCoefficients().magnitudeDb(freq, sampleRate_);
    }

    // Theoretical digital Butterworth value: -10 log10(1 + (tan(πf/fs) / tan(πfc/fs))^(2N))
    static double butterworthDb(double freq, double cutoff, double sampleRate, int order) noexcept {
        const double ratio = std::tan(kPi * freq / sampleRate) / std::tan(kPi * cutoff / sampleRate);
        return -10.0 * std::log10(1.0 + std::pow(ratio, 2.0 * order));
    }

    // Per sample: call channels 0, 1, … in order for every frame; the crossfade ramp advances on channel 0
    float processSample(int ch, float x) noexcept {
        if (ch == 0)
            mixCurrent_ = mix_.getNextValue();
        const float y12 = lp12_.processSample(ch, x);
        const float y24 = lp24b_.processSample(ch, lp24a_.processSample(ch, x));
        return y12 + mixCurrent_ * (y24 - y12);
    }

    void process(AudioBlock block) noexcept {
        const int nch = std::min(block.getNumChannels(), numChannels_);
        for (int i = 0; i < block.getNumSamples(); ++i)
            for (int ch = 0; ch < nch; ++ch)
                block.getChannelPointer(ch)[i] = processSample(ch, block.getChannelPointer(ch)[i]);
    }

private:
    void updateCoefficients() noexcept {
        lp12_.setCoefficients(BiquadCoefficients::makeLowpass(sampleRate_, cutoffHz_, BiquadCoefficients::kButterworthQ));
        lp24a_.setCoefficients(BiquadCoefficients::makeLowpass(sampleRate_, cutoffHz_, butterworthQ(4, 0))); // 0.5412
        lp24b_.setCoefficients(BiquadCoefficients::makeLowpass(sampleRate_, cutoffHz_, butterworthQ(4, 1))); // 1.3066
    }

    double sampleRate_ = 48000.0;
    int numChannels_ = 0;
    float cutoffHz_ = 1000.0f;
    Slope slope_ = Slope::Db12;
    double crossfadeSeconds_ = 0.02;
    SmoothedValue mix_;
    float mixCurrent_ = 0.0f;
    Biquad lp12_, lp24a_, lp24b_;
};

// Click test for switching the slope: a sine wave at fc, switched midway, measuring the largest sample-to-sample jump
float maxJumpWhenSwitching(double crossfadeSeconds, double sr) {
    SwitchableHighCut hc;
    hc.setCrossfadeSeconds(crossfadeSeconds);
    hc.prepare(sr, 512, 1);
    hc.setCutoff(1000.0f);
    AudioBuffer y = makeSine(1000.0, sr, 1.0, 0.5f, 1);
    AudioBlock whole(y);
    for (int start = 0; start < y.getNumSamples(); start += 512) {
        // Switch at the start of block 48 (0.512 s): the phase of the 1 kHz sine is exactly 0° at that point,
        // and the output difference between the 12 and 24 dB/oct chains is y24 - y12 = 0.5·cos(θ + 45°), about 0.35 at θ = 0°, so a direct switch is sure to show a big jump
        if (start == 48 * 512)
            hc.setSlope(SwitchableHighCut::Slope::Db24);
        hc.process(whole.getSubBlock(start, std::min(512, y.getNumSamples() - start)));
    }
    float maxJump = 0.0f;
    const float* d = y.getReadPointer(0);
    for (int i = secondsToSamples(0.3, sr); i < y.getNumSamples(); ++i)
        maxJump = std::max(maxJump, std::abs(d[i] - d[i - 1]));
    return maxJump;
}

} // namespace

int main() {
    constexpr double sr = 48000.0;
    constexpr double fc = 1000.0;
    std::printf("=== Chapter 03 Exercise 6: high cut switchable between 12 / 24 dB/oct ===\n\n");

    SwitchableHighCut hc;
    hc.prepare(sr, 512, 1);
    hc.setCutoff(static_cast<float>(fc));

    for (int order : {2, 4}) {
        hc.setSlope(order == 2 ? SwitchableHighCut::Slope::Db12 : SwitchableHighCut::Slope::Db24);
        hc.prepare(sr, 512, 1); // let the crossfade snap to its target (the measurement does not have to wait)
        std::printf("  %d dB/oct (Butterworth, order %d), fc = %.0f Hz\n", 6 * order, order, fc);
        std::printf("    freq(Hz)   theory(dB)   measured(dB)   analog Butterworth(dB)\n");
        for (double f : {250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0}) {
            hc.reset();
            const double theory = SwitchableHighCut::butterworthDb(f, fc, sr, order);
            const double m = measureGainDb([&](float x) { return hc.processSample(0, x); }, f, sr);
            const double ideal = -10.0 * std::log10(1.0 + std::pow(f / fc, 2.0 * order)); // analog (no warping)
            std::printf("    %7.0f    %+7.2f    %+7.2f      %+7.2f\n", f, theory, m, ideal);
            DAWLAB_CHECK_NEAR(m, theory, 0.1);
            DAWLAB_CHECK_NEAR(hc.getMagnitudeDb(f), theory, 1e-6);
        }
        DAWLAB_CHECK_NEAR(hc.getMagnitudeDb(fc), -3.0103, 0.01); // both slopes are -3 dB at fc
    }
    // At 2 fc: about -12.4 dB for 12 dB/oct and about -24.3 dB for 24 dB/oct (slightly more than the analog 12.3 / 24.1: warping)
    DAWLAB_CHECK_NEAR(SwitchableHighCut::butterworthDb(2000.0, fc, sr, 2), -12.38, 0.05);
    DAWLAB_CHECK_NEAR(SwitchableHighCut::butterworthDb(2000.0, fc, sr, 4), -24.25, 0.05);
    // The gain at fc of each of the two Q values the 4th order is split into: only their sum is -3 dB
    const double qa = BiquadCoefficients::makeLowpass(sr, fc, butterworthQ(4, 0)).magnitudeDb(fc, sr);
    const double qb = BiquadCoefficients::makeLowpass(sr, fc, butterworthQ(4, 1)).magnitudeDb(fc, sr);
    std::printf("\n  The two sections of the 4th order at fc: Q=0.5412 -> %+.2f dB, Q=1.3066 -> %+.2f dB, sum %+.2f dB\n", qa, qb, qa + qb);
    DAWLAB_CHECK_NEAR(qa + qb, -3.0103, 0.01);

    // ---- Click when switching ----
    const float sineSlope = static_cast<float>(0.5 * kTwoPi * 1000.0 / sr); // the most the sine wave itself changes per sample
    const float jumpHard = maxJumpWhenSwitching(0.0, sr);
    const float jumpSoft = maxJumpWhenSwitching(0.02, sr);
    std::printf("\n  1 kHz sine (= fc), switching 12 -> 24 dB/oct midway: largest sample jump\n");
    std::printf("    direct switch   %.4f (the sine itself changes by at most %.4f)\n", jumpHard, sineSlope);
    std::printf("    20 ms crossfade %.4f\n", jumpSoft);
    DAWLAB_CHECK(jumpHard > 3.0f * sineSlope); // direct switching: -90° jumps to -180°, so the waveform takes a big step
    DAWLAB_CHECK(jumpSoft < 1.2f * sineSlope); // crossfade: about the same as the sine wave's own change

    // Listening test: drum loop, 12 dB/oct for the first half and 24 dB/oct for the second half, fc = 2 kHz
    AudioBuffer wet = makeDrumLoop(sr, 2);
    SwitchableHighCut live;
    live.prepare(sr, 512, wet.getNumChannels());
    live.setCutoff(2000.0f);
    AudioBlock whole(wet);
    for (int start = 0; start < wet.getNumSamples(); start += 512) {
        if (start <= secondsToSamples(2.0, sr) && secondsToSamples(2.0, sr) < start + 512) // the block that contains that time point
            live.setSlope(SwitchableHighCut::Slope::Db24);
        live.process(whole.getSubBlock(start, std::min(512, wet.getNumSamples() - start)));
    }
    DAWLAB_CHECK(!hasNanOrInf(wet));
    writeWav(outputPath("l03_ex6_highcut_switch.wav"), wet, sr);
    std::printf("\n  drum loop high cut 2 kHz: 12 dB/oct for the first 2 seconds, 24 dB/oct for the last 2 seconds -> dawlab_output/l03_ex6_highcut_switch.wav\n");

    return dawlab::test::summary("l03_ex6");
}
