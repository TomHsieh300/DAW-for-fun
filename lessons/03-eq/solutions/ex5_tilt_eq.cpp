// Exercise 5 (solution): Tilt EQ
//
// A tilt EQ has only two knobs: the pivot (the pivot frequency) and the tilt (in dB). The curve acts like a seesaw: 0 dB at the pivot,
// gradually approaching -tilt/2 toward the low-frequency end and +tilt/2 toward the high-frequency end (tilt > 0 = brighter; tilt < 0 = darker).
// Two RBJ shelves are enough:
//   low shelf  @ pivot, gain = -tilt/2
//   high shelf @ pivot, gain = +tilt/2
// Why is the pivot exactly 0 dB? The f0 of an RBJ shelf is the "midpoint frequency": |H(f0)| = A = 10^(gainDb/40),
// which is gainDb / 2 (proved in README section 2.6). At the pivot the low shelf contributes -tilt/4 and the high shelf +tilt/4, which sum to 0.
// Both use Q = 0.707 (S = 1): the steepest without overshoot, so the whole curve is monotonic with no hump.

#include <dawlab/Common.h>
#include <dawlab/dsp/Biquad.h>
#include <dawlab/dsp/SmoothedValue.h>

#include <cstdio>

using namespace dawlab;
using namespace dawlab::dsp;

namespace {

class TiltEq {
public:
    static constexpr double kShelfQ = 0.70710678118654752;

    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        sampleRate_ = sampleRate;
        low_.prepare(sampleRate, maxBlockSize, numChannels);
        high_.prepare(sampleRate, maxBlockSize, numChannels);
        tiltSmoother_.reset(sampleRate, 0.02);
        tiltSmoother_.setCurrentAndTargetValue(tiltDb_);
        updateCoefficients(tiltDb_);
        reset();
    }

    void reset() noexcept {
        low_.reset();
        high_.reset();
    }

    void setPivotHz(float hz) noexcept {
        pivotHz_ = hz;
        updateCoefficients(tiltSmoother_.getCurrentValue());
    }
    // tilt: positive = more highs, fewer lows (brighter); negative = the opposite
    void setTiltDb(float tiltDb) noexcept {
        tiltDb_ = std::clamp(tiltDb, -12.0f, 12.0f);
        tiltSmoother_.setTargetValue(tiltDb_);
    }

    double getMagnitudeDb(double freq) const noexcept {
        return low_.getCoefficients().magnitudeDb(freq, sampleRate_) + high_.getCoefficients().magnitudeDb(freq, sampleRate_);
    }

    // Parameter smoothing: tilt ramps linearly to the target over 20 ms, and the coefficients are recomputed every 32 samples (the same trick as ParametricEq)
    void process(AudioBlock block) noexcept {
        const int n = block.getNumSamples();
        for (int start = 0; start < n; start += kUpdateInterval) {
            const int len = std::min(kUpdateInterval, n - start);
            if (tiltSmoother_.isSmoothing()) {
                tiltSmoother_.skip(len);
                updateCoefficients(tiltSmoother_.getCurrentValue());
            }
            AudioBlock sub = block.getSubBlock(start, len);
            low_.process(sub);
            high_.process(sub);
        }
    }

    float processSample(int ch, float x) noexcept { return high_.processSample(ch, low_.processSample(ch, x)); }

private:
    static constexpr int kUpdateInterval = 32;

    void updateCoefficients(float tiltDb) noexcept {
        low_.setCoefficients(BiquadCoefficients::makeLowShelf(sampleRate_, pivotHz_, kShelfQ, -0.5 * tiltDb));
        high_.setCoefficients(BiquadCoefficients::makeHighShelf(sampleRate_, pivotHz_, kShelfQ, 0.5 * tiltDb));
    }

    double sampleRate_ = 48000.0;
    float pivotHz_ = 1000.0f;
    float tiltDb_ = 0.0f;
    SmoothedValue tiltSmoother_;
    Biquad low_, high_;
};

} // namespace

int main() {
    constexpr double sr = 48000.0;
    std::printf("=== Chapter 03 Exercise 5: Tilt EQ ===\n\n");

    TiltEq tilt;
    tilt.prepare(sr, 512, 1);
    tilt.setPivotHz(1000.0f);
    tilt.setTiltDb(6.0f);
    tilt.prepare(sr, 512, 1); // prepare again: the smoother snaps to the target, so the measurement does not have to wait for the ramp

    std::printf("  pivot 1 kHz, tilt +6 dB:\n");
    std::printf("    freq(Hz)   theory(dB)   measured(dB)\n");
    for (double f : {30.0, 60.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0}) {
        tilt.reset();
        const double m = measureGainDb([&](float x) { return tilt.processSample(0, x); }, f, sr);
        std::printf("    %7.0f    %+6.2f     %+6.2f\n", f, tilt.getMagnitudeDb(f), m);
        DAWLAB_CHECK_NEAR(m, tilt.getMagnitudeDb(f), 0.05);
    }
    DAWLAB_CHECK_NEAR(tilt.getMagnitudeDb(1000.0), 0.0, 0.01);   // the pivot is exactly 0 dB (the midpoints of the two shelves cancel)
    DAWLAB_CHECK_NEAR(tilt.getMagnitudeDb(30.0), -3.0, 0.1);     // low end -> -tilt/2
    DAWLAB_CHECK_NEAR(tilt.getMagnitudeDb(16000.0), 3.0, 0.1);   // high end -> +tilt/2
    DAWLAB_CHECK_NEAR(tilt.getMagnitudeDb(2000.0), -tilt.getMagnitudeDb(500.0), 0.1); // antisymmetric about the pivot (1 octave)
    DAWLAB_CHECK_NEAR(tilt.getMagnitudeDb(4000.0), -tilt.getMagnitudeDb(250.0), 0.15); // 2 octaves (a little warping at the high end)
    // Monotonic: it must rise at every step from low to high (no hump)
    bool monotonic = true;
    double prev = tilt.getMagnitudeDb(20.0);
    for (double f = 25.0; f <= 20000.0; f *= 1.05) {
        const double db = tilt.getMagnitudeDb(f);
        if (db < prev - 1e-6)
            monotonic = false;
        prev = db;
    }
    DAWLAB_CHECK(monotonic);

    // A negative tilt is the mirror image of a positive tilt: H(-t) = 1 / H(+t)
    TiltEq dark;
    dark.setTiltDb(-6.0f);
    dark.prepare(sr, 512, 1);
    for (double f : {100.0, 1000.0, 5000.0})
        DAWLAB_CHECK_NEAR(dark.getMagnitudeDb(f), -tilt.getMagnitudeDb(f), 1e-6);

    // Listening test: apply +6 dB (bright) and -6 dB (dark) to the pluck sequence, process in blocks, and change the tilt midway (smooth, no click)
    AudioBuffer dry = makePluckSequence(sr, 4.0);
    AudioBuffer wet = dry;
    TiltEq live;
    live.prepare(sr, 512, wet.getNumChannels());
    live.setTiltDb(6.0f);
    live.prepare(sr, 512, wet.getNumChannels());
    AudioBlock whole(wet);
    for (int start = 0; start < wet.getNumSamples(); start += 512) {
        if (start <= secondsToSamples(2.0, sr) && secondsToSamples(2.0, sr) < start + 512) // the block that contains that time point
            live.setTiltDb(-6.0f); // at 2 seconds, switch from "bright" to "dark"
        live.process(whole.getSubBlock(start, std::min(512, wet.getNumSamples() - start)));
    }
    DAWLAB_CHECK(!hasNanOrInf(wet));
    writeWav(outputPath("l03_ex5_tilt.wav"), wet, sr);
    std::printf("\n  pluck sequence: tilt +6 dB for the first 2 seconds, -6 dB for the last 2 seconds -> dawlab_output/l03_ex5_tilt.wav\n");

    return dawlab::test::summary("l03_ex5");
}
