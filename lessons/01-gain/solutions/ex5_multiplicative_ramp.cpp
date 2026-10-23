// Exercise 5 solution: multiplicative (exponential) ramp -- fader automation that is a straight line in dB
//
// The dsp::SmoothedValue of this chapter is a linear ramp: every sample adds the same step, so the gain is a straight line in time.
// But the ear hears dB. When a linear ramp runs from 1.0 to 0.001 (-60 dB), the gain is still 0.5 (-6 dB) halfway through,
// 90% of the time is spent above -20 dB, and only in the last few dozen samples does it drop all at once -- it sounds like "loud the whole time, then suddenly gone".
//
// A multiplicative ramp instead multiplies by the same factor r every sample:
//     r = (target / current)^(1/N)        -> g[n] = current * r^n
//     dB[n] = dB[0] + n * 20*log10(r)     -> a straight line in dB, advancing by the same number of dB every sample
// Example: 100 ms (4800 samples @ 48 kHz) from 0 dB to -60 dB, -0.0125 dB per sample, exactly -30 dB at the halfway point.
// (JUCE's SmoothedValue<float, ValueSmoothingTypes::Multiplicative> does exactly this.)
//
// The only trouble is 0: multiplying by any factor never reaches 0, and nothing can be multiplied up from 0.
// So 0 is treated as a "floor" kFloorGain = -100 dB (= kMinusInfinityDb); the ramp runs between the floor and the target,
// and the last sample "snaps" to the exact target value (an exact 0 when muting).
//
// Run: l01_ex5_solution

#include <dawlab/Common.h>
#include <dawlab/dsp/SmoothedValue.h>

#include <cmath>
#include <cstdio>
#include <vector>

using namespace dawlab;

class MultiplicativeRamp {
public:
    static constexpr double kFloorGain = 1.0e-5; // -100 dB: the stand-in for 0 (dbToGain(kMinusInfinityDb) is also 0)

    void reset(double sampleRate, double rampSeconds) noexcept {
        rampSamples_ = std::max(0, static_cast<int>(std::lround(rampSeconds * sampleRate)));
        setCurrentAndTargetValue(target_);
    }

    void setCurrentAndTargetValue(float value) noexcept {
        target_ = std::max(0.0f, value);
        current_ = target_;
        countdown_ = 0;
    }

    void setTargetValue(float value) noexcept {
        value = std::max(0.0f, value); // only non-negative gains are handled (a gain ratio cannot cross 0)
        if (value == target_)
            return;
        target_ = value;
        if (rampSamples_ == 0) {
            setCurrentAndTargetValue(value);
            return;
        }
        // Start from the current value (so changing the target mid-ramp does not jump); 0 is replaced by the floor
        const double from = std::max(current_, kFloorGain);
        const double to = std::max(static_cast<double>(value), kFloorGain);
        current_ = from;
        multiplier_ = std::pow(to / from, 1.0 / static_cast<double>(rampSamples_));
        countdown_ = rampSamples_;
    }

    float getNextValue() noexcept {
        if (countdown_ <= 0)
            return target_;
        if (--countdown_ == 0) {
            current_ = target_; // snap on the last step: avoids accumulated rounding error and lets a mute stop at an exact 0
            return target_;
        }
        current_ *= multiplier_; // the state is a double: after 4800 successive multiplications the error is ~1e-13, whereas a float would reach ~1e-4
        return static_cast<float>(current_);
    }

    bool isSmoothing() const noexcept { return countdown_ > 0; }
    float getTargetValue() const noexcept { return target_; }
    double getMultiplier() const noexcept { return multiplier_; }

private:
    double current_ = 1.0;
    double multiplier_ = 1.0;
    float target_ = 1.0f;
    int rampSamples_ = 0;
    int countdown_ = 0;
};

namespace {

constexpr double kSr = 48000.0;
constexpr int kRamp = 4800; // 100 ms

// Runs the whole ramp (including the final snapping step) and returns kRamp values
template <typename Ramp>
std::vector<float> runRamp(Ramp& r, float from, float to) {
    r.setCurrentAndTargetValue(from);
    r.setTargetValue(to);
    std::vector<float> v(static_cast<size_t>(kRamp));
    for (auto& x : v)
        x = r.getNextValue();
    return v;
}

// The largest |delta dB| between two adjacent samples (counting from the starting value)
double maxDbStep(const std::vector<float>& v, float from) {
    double prev = gainToDb(static_cast<double>(from)), m = 0.0;
    for (size_t i = 0; i + 1 < v.size(); ++i) { // the last one (the snap) is not counted: when muting it is -100 -> 0
        const double d = gainToDb(static_cast<double>(v[i]));
        m = std::max(m, std::abs(d - prev));
        prev = d;
    }
    return m;
}

// What fraction of the ramp is spent above thresholdDb
double fractionAbove(const std::vector<float>& v, double thresholdDb) {
    int count = 0;
    for (float x : v)
        if (gainToDb(static_cast<double>(x)) > thresholdDb)
            ++count;
    return static_cast<double>(count) / static_cast<double>(v.size());
}

} // namespace

int main() {
    MultiplicativeRamp mul;
    mul.reset(kSr, 0.1);
    dsp::SmoothedValue lin;
    lin.reset(kSr, 0.1);

    // ---- 1. 0 dB -> -60 dB: the multiplicative ramp is a straight line in dB -----------------------
    const std::vector<float> m = runRamp(mul, 1.0f, 0.001f);
    const std::vector<float> l = runRamp(lin, 1.0f, 0.001f);
    const double stepDb = 20.0 * std::log10(mul.getMultiplier());
    std::printf("multiplier r = %.6f (%.4f dB per sample)\n", mul.getMultiplier(), stepDb);
    DAWLAB_CHECK_NEAR(stepDb, -60.0 / kRamp, 1e-6);

    const double mHalf = gainToDb(static_cast<double>(m[kRamp / 2 - 1])); // sample number 2400
    const double lHalf = gainToDb(static_cast<double>(l[kRamp / 2 - 1]));
    std::printf("Halfway (2400 samples): multiplicative %.2f dB, linear %.2f dB\n", mHalf, lHalf);
    DAWLAB_CHECK_NEAR(mHalf, -30.0, 0.01);
    DAWLAB_CHECK_NEAR(lHalf, -6.01, 0.01);

    double maxDev = 0.0; // each sample's deviation from the "straight line in dB"
    for (int i = 0; i < kRamp; ++i)
        maxDev = std::max(maxDev, std::abs(gainToDb(static_cast<double>(m[static_cast<size_t>(i)]))
                                           - (-60.0 * (i + 1) / kRamp)));
    std::printf("Max deviation from the straight line in dB: %.5f dB\n", maxDev);
    DAWLAB_CHECK(maxDev < 1e-3);

    const double mAbove = fractionAbove(m, -20.0), lAbove = fractionAbove(l, -20.0);
    std::printf("Time spent above -20 dB during the ramp: multiplicative %.1f%%, linear %.1f%%\n", 100.0 * mAbove,
                100.0 * lAbove);
    DAWLAB_CHECK_NEAR(mAbove, 1.0 / 3.0, 0.001);
    DAWLAB_CHECK_NEAR(lAbove, 0.9 / 0.999, 0.001);

    const double mStep = maxDbStep(m, 1.0f), lStep = maxDbStep(l, 1.0f);
    std::printf("Max |delta dB| between adjacent samples: multiplicative %.4f dB, linear %.3f dB (in the last few samples)\n", mStep, lStep);
    DAWLAB_CHECK_NEAR(mStep, 0.0125, 1e-4);
    DAWLAB_CHECK(lStep > 1.0);

    DAWLAB_CHECK(m.back() == 0.001f); // the last step snaps to the exact target
    DAWLAB_CHECK(!mul.isSmoothing());

    // ---- 2. mute: 0 dB -> 0 (runs down to the -100 dB floor, then snaps to an exact 0 at the end) ----
    const std::vector<float> mute = runRamp(mul, 1.0f, 0.0f);
    const double muteHalf = gainToDb(static_cast<double>(mute[kRamp / 2 - 1]));
    std::printf("mute: halfway %.2f dB, second-to-last %.2f dB, last = %g\n", muteHalf,
                gainToDb(static_cast<double>(mute[kRamp - 2])), static_cast<double>(mute.back()));
    DAWLAB_CHECK_NEAR(muteHalf, -50.0, 0.01);
    DAWLAB_CHECK(mute[kRamp - 2] > 0.0f && mute[kRamp - 2] < 1.1e-5f);
    DAWLAB_CHECK(mute.back() == 0.0f);

    // ---- 3. unmute: 0 -> 0 dB (starts from the floor; the first sample is not 0 and does not jump up) ----
    const std::vector<float> un = runRamp(mul, 0.0f, 1.0f);
    std::printf("unmute: first sample %.3e (%.4f dB), last sample %.6f\n", static_cast<double>(un.front()),
                gainToDb(static_cast<double>(un.front())), static_cast<double>(un.back()));
    DAWLAB_CHECK(un.front() > 0.0f && un.front() < 2.0e-5f);
    DAWLAB_CHECK(un.back() == 1.0f);

    // ---- 4. Changing the target mid-ramp: start from the current value, no jump ----------------------
    mul.setCurrentAndTargetValue(1.0f);
    mul.setTargetValue(0.001f);
    float prev = 1.0f;
    for (int i = 0; i < kRamp / 4; ++i) // run a quarter of the way -> -15 dB
        prev = mul.getNextValue();
    mul.setTargetValue(1.0f);
    const float next = mul.getNextValue();
    const double jumpDb = gainToDb(static_cast<double>(next)) - gainToDb(static_cast<double>(prev));
    std::printf("Switching back to 0 dB at %.2f dB: the next sample advances only %+.4f dB\n", gainToDb(static_cast<double>(prev)),
                jumpDb);
    DAWLAB_CHECK_NEAR(gainToDb(static_cast<double>(prev)), -15.0, 0.01);
    DAWLAB_CHECK(jumpDb > 0.0 && jumpDb < 0.01);

    // ---- Listening demo: a 1 kHz sine, a linear fade-out for the first 2 seconds, a multiplicative fade-out for the last 2 seconds (1.5 seconds each) ----
    {
        MultiplicativeRamp fm;
        fm.reset(kSr, 1.5);
        dsp::SmoothedValue fl;
        fl.reset(kSr, 1.5);
        AudioBuffer demo = makeSine(1000.0, kSr, 4.0, 0.5f, 1);
        const int half = demo.getNumSamples() / 2;
        fl.setCurrentAndTargetValue(1.0f);
        fl.setTargetValue(0.0f);
        fm.setCurrentAndTargetValue(1.0f);
        fm.setTargetValue(0.0f);
        float* d = demo.getWritePointer(0);
        for (int i = 0; i < half; ++i)
            d[i] *= fl.getNextValue();
        for (int i = half; i < demo.getNumSamples(); ++i)
            d[i] *= fm.getNextValue();
        writeWav(outputPath("l01_ex5_multiplicative_ramp.wav"), demo, kSr);
        std::printf("Wrote dawlab_output/l01_ex5_multiplicative_ramp.wav (first half: linear fade-out, second half: multiplicative fade-out)\n");
    }

    return dawlab::test::summary("l01_ex5");
}
