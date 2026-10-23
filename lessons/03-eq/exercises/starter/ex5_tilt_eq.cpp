// Exercise 5 (starter code): Tilt EQ
//
// Goal: build a tilt EQ from one low shelf + one high shelf (both at the pivot frequency, Q = 0.707):
//   - setTiltDb(t): low-shelf gain = -t/2, high-shelf gain = +t/2 -> 0 dB at the pivot, ∓t/2 at the two ends.
//   - getMagnitudeDb: add the dB of the two shelves.
//   - Bonus: smooth the tilt parameter with SmoothedValue to avoid clicks.
// When run, the self-check prints FAIL until you complete the TODOs.

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
        updateCoefficients();
        reset();
    }

    void reset() noexcept {
        low_.reset();
        high_.reset();
    }

    void setPivotHz(float hz) noexcept {
        pivotHz_ = hz;
        updateCoefficients();
    }
    void setTiltDb(float tiltDb) noexcept {
        tiltDb_ = std::clamp(tiltDb, -12.0f, 12.0f);
        updateCoefficients();
    }

    double getMagnitudeDb(double freq) const noexcept {
        (void)freq;
        return 0.0; // TODO: add the magnitudeDb of low_ and high_
    }

    void process(AudioBlock block) noexcept {
        low_.process(block);
        high_.process(block);
    }

    float processSample(int ch, float x) noexcept { return high_.processSample(ch, low_.processSample(ch, x)); }

private:
    void updateCoefficients() noexcept {
        // TODO: low_ = makeLowShelf(sr, pivot, kShelfQ, -tilt/2); high_ = makeHighShelf(sr, pivot, kShelfQ, +tilt/2)
    }

    double sampleRate_ = 48000.0;
    float pivotHz_ = 1000.0f;
    float tiltDb_ = 0.0f;
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

    std::printf("  pivot 1 kHz, tilt +6 dB:\n");
    for (double f : {30.0, 250.0, 1000.0, 4000.0, 16000.0}) {
        tilt.reset();
        const double m = measureGainDb([&](float x) { return tilt.processSample(0, x); }, f, sr);
        std::printf("    %7.0f    %+6.2f     %+6.2f\n", f, tilt.getMagnitudeDb(f), m);
        DAWLAB_CHECK_NEAR(m, tilt.getMagnitudeDb(f), 0.05);
    }
    DAWLAB_CHECK_NEAR(tilt.getMagnitudeDb(1000.0), 0.0, 0.01);
    DAWLAB_CHECK_NEAR(tilt.getMagnitudeDb(30.0), -3.0, 0.1);
    DAWLAB_CHECK_NEAR(tilt.getMagnitudeDb(16000.0), 3.0, 0.1);
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
    dark.prepare(sr, 512, 1);
    dark.setTiltDb(-6.0f);
    for (double f : {100.0, 1000.0, 5000.0})
        DAWLAB_CHECK_NEAR(dark.getMagnitudeDb(f), -tilt.getMagnitudeDb(f), 1e-6);

    // TODO (bonus):
    //   1. Change setTiltDb() so it only sets the target of a SmoothedValue (20 ms ramp), and have process() call
    //      skip(32) every 32 samples and recompute the coefficients from the current value (the same trick as ParametricEq).
    //   2. Listening test: process the pluck sequence (makePluckSequence(sr, 4.0)) in blocks of 512 samples,
    //      with tilt +6 dB for the first 2 seconds and switching to -6 dB at 2 seconds (it must not click),
    //      writeWav(outputPath("l03_ex5_tilt.wav"), wet, sr).

    return dawlab::test::summary("l03_ex5_starter");
}
