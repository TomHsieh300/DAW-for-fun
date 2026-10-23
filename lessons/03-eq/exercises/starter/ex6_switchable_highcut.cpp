// Exercise 6 (starter code): a high cut (lowpass) switchable between 12 / 24 dB/oct, with no click when switching
//
// Goals:
//   1. 12 dB/oct = one Butterworth lowpass (Q = 1/√2).
//      24 dB/oct = two lowpass sections in cascade, with Q from butterworthQ(4, 0) and butterworthQ(4, 1) (Biquad.h from Chapter 02).
//   2. Both slopes must be -3 dB at fc; at other frequencies they must match the digital Butterworth theory
//      |H|² = 1 / (1 + (tan(πf/fs) / tan(πfc/fs))^(2N)).
//   3. When switching the slope, use a SmoothedValue to crossfade; the sine wave at fc must not show a large jump.
// When run, the self-check prints FAIL until you complete the TODOs.

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
        // TODO: do not switch directly; instead set the target of mix_ to 0 (12) or 1 (24) to crossfade
        mix_.setCurrentAndTargetValue(slope == Slope::Db24 ? 1.0f : 0.0f);
    }
    void setCrossfadeSeconds(double seconds) noexcept { crossfadeSeconds_ = std::max(0.0, seconds); }
    Slope getSlope() const noexcept { return slope_; }

    double getMagnitudeDb(double freq) const noexcept {
        (void)freq;
        return 0.0; // TODO: return the dB of lp12 or of lp24a + lp24b, depending on the current slope
    }

    // TODO: theoretical digital Butterworth value: -10 log10(1 + (tan(πf/fs) / tan(πfc/fs))^(2N))
    static double butterworthDb(double freq, double cutoff, double sampleRate, int order) noexcept {
        (void)freq;
        (void)cutoff;
        (void)sampleRate;
        (void)order;
        return 0.0;
    }

    float processSample(int ch, float x) noexcept {
        if (ch == 0)
            mixCurrent_ = mix_.getNextValue();
        // TODO: y12 = lp12_(x); y24 = lp24b_(lp24a_(x)); return y12 + mix * (y24 - y12)
        return lp12_.processSample(ch, x);
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
        // TODO: lp24a_ / lp24b_ use butterworthQ(4, 0) and butterworthQ(4, 1)
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
        hc.prepare(sr, 512, 1);
        std::printf("  %d dB/oct (Butterworth, order %d), fc = %.0f Hz\n", 6 * order, order, fc);
        for (double f : {500.0, 1000.0, 2000.0, 4000.0}) {
            hc.reset();
            const double theory = SwitchableHighCut::butterworthDb(f, fc, sr, order);
            const double m = measureGainDb([&](float x) { return hc.processSample(0, x); }, f, sr);
            std::printf("    %7.0f    theory %+7.2f    measured %+7.2f\n", f, theory, m);
            DAWLAB_CHECK_NEAR(m, theory, 0.1);
            DAWLAB_CHECK_NEAR(hc.getMagnitudeDb(f), theory, 1e-6);
        }
        DAWLAB_CHECK_NEAR(hc.getMagnitudeDb(fc), -3.0103, 0.01);
    }
    DAWLAB_CHECK_NEAR(SwitchableHighCut::butterworthDb(2000.0, fc, sr, 2), -12.38, 0.05);
    DAWLAB_CHECK_NEAR(SwitchableHighCut::butterworthDb(2000.0, fc, sr, 4), -24.25, 0.05);

    const float sineSlope = static_cast<float>(0.5 * kTwoPi * 1000.0 / sr);
    const float jumpHard = maxJumpWhenSwitching(0.0, sr);
    const float jumpSoft = maxJumpWhenSwitching(0.02, sr);
    std::printf("\n  Maximum jump when switching: direct %.4f / crossfade %.4f (the sine itself %.4f)\n", jumpHard, jumpSoft, sineSlope);
    DAWLAB_CHECK(jumpHard > 3.0f * sineSlope);
    DAWLAB_CHECK(jumpSoft < 1.2f * sineSlope);

    // TODO (part 4, listening test): run the drum loop (makeDrumLoop(sr, 2)) through a 2 kHz high cut,
    //   processing in blocks of 512 samples: 12 dB/oct for the first 2 seconds, and call setSlope(Db24) at the start of the block that contains the 2-second mark,
    //   and finally writeWav(outputPath("l03_ex6_highcut_switch.wav"), wet, sr). You should not hear a click at the switch.

    return dawlab::test::summary("l03_ex6_starter");
}
