// Exercise 5 (starter code): multiplicative (exponential) ramp -- fader automation that is a straight line in dB
//
//   linear ramp (dsp::SmoothedValue): g[n] = g[0] + n * step         (gain is a straight line in time)
//   multiplicative ramp             : g[n] = g[0] * r^n,  r = (target / current)^(1/N)
//                                       -> dB[n] = dB[0] + n * 20*log10(r)  (dB is a straight line in time)
//
// You cannot reach 0 by multiplying, nor multiply anything up from 0: replace 0 with a floor kFloorGain (-100 dB),
// let the ramp run between the floor and the target, and snap the last sample to the exact target value.
// You are done when every self-check passes after you complete the TODOs. The starter code prints FAIL when it runs; that is expected.

#include <dawlab/Common.h>
#include <dawlab/dsp/SmoothedValue.h>

#include <cmath>
#include <cstdio>
#include <vector>

using namespace dawlab;

class MultiplicativeRamp {
public:
    static constexpr double kFloorGain = 1.0e-5; // -100 dB: the stand-in for 0

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
        value = std::max(0.0f, value);
        if (value == target_)
            return;
        target_ = value;
        if (rampSamples_ == 0) {
            setCurrentAndTargetValue(value);
            return;
        }
        // TODO 1: start from the current value current_ (replace 0 with kFloorGain), aim for value (also replace 0 with kFloorGain),
        //         compute multiplier_ = (to / from)^(1 / rampSamples_), and set current_ to from.
        multiplier_ = 1.0;
        countdown_ = rampSamples_;
    }

    float getNextValue() noexcept {
        if (countdown_ <= 0)
            return target_;
        if (--countdown_ == 0) {
            current_ = target_; // the last step snaps to the exact target (an exact 0 when muting)
            return target_;
        }
        // TODO 2: multiply current_ by multiplier_ (the state is a double; think about why)
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

template <typename Ramp>
std::vector<float> runRamp(Ramp& r, float from, float to) {
    r.setCurrentAndTargetValue(from);
    r.setTargetValue(to);
    std::vector<float> v(static_cast<size_t>(kRamp));
    for (auto& x : v)
        x = r.getNextValue();
    return v;
}

double maxDbStep(const std::vector<float>& v, float from) {
    double prev = gainToDb(static_cast<double>(from)), m = 0.0;
    for (size_t i = 0; i + 1 < v.size(); ++i) {
        const double d = gainToDb(static_cast<double>(v[i]));
        m = std::max(m, std::abs(d - prev));
        prev = d;
    }
    return m;
}

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

    const std::vector<float> m = runRamp(mul, 1.0f, 0.001f);
    const std::vector<float> l = runRamp(lin, 1.0f, 0.001f);
    const double stepDb = 20.0 * std::log10(mul.getMultiplier());
    std::printf("multiplier r = %.6f (%.4f dB per sample)\n", mul.getMultiplier(), stepDb);
    DAWLAB_CHECK_NEAR(stepDb, -60.0 / kRamp, 1e-6);

    const double mHalf = gainToDb(static_cast<double>(m[kRamp / 2 - 1]));
    const double lHalf = gainToDb(static_cast<double>(l[kRamp / 2 - 1]));
    std::printf("Halfway (2400 samples): multiplicative %.2f dB, linear %.2f dB\n", mHalf, lHalf);
    DAWLAB_CHECK_NEAR(mHalf, -30.0, 0.01);
    DAWLAB_CHECK_NEAR(lHalf, -6.01, 0.01);

    double maxDev = 0.0;
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
    std::printf("Max |delta dB| between adjacent samples: multiplicative %.4f dB, linear %.3f dB\n", mStep, lStep);
    DAWLAB_CHECK_NEAR(mStep, 0.0125, 1e-4);
    DAWLAB_CHECK(lStep > 1.0);
    DAWLAB_CHECK(m.back() == 0.001f);
    DAWLAB_CHECK(!mul.isSmoothing());

    const std::vector<float> mute = runRamp(mul, 1.0f, 0.0f);
    const double muteHalf = gainToDb(static_cast<double>(mute[kRamp / 2 - 1]));
    std::printf("mute: halfway %.2f dB, last sample = %g\n", muteHalf, static_cast<double>(mute.back()));
    DAWLAB_CHECK_NEAR(muteHalf, -50.0, 0.01);
    DAWLAB_CHECK(mute[kRamp - 2] > 0.0f && mute[kRamp - 2] < 1.1e-5f);
    DAWLAB_CHECK(mute.back() == 0.0f);

    const std::vector<float> un = runRamp(mul, 0.0f, 1.0f);
    std::printf("unmute: first sample %.3e, last sample %.6f\n", static_cast<double>(un.front()),
                static_cast<double>(un.back()));
    DAWLAB_CHECK(un.front() > 0.0f && un.front() < 2.0e-5f);
    DAWLAB_CHECK(un.back() == 1.0f);

    mul.setCurrentAndTargetValue(1.0f);
    mul.setTargetValue(0.001f);
    float prev = 1.0f;
    for (int i = 0; i < kRamp / 4; ++i)
        prev = mul.getNextValue();
    mul.setTargetValue(1.0f);
    const float next = mul.getNextValue();
    const double jumpDb = gainToDb(static_cast<double>(next)) - gainToDb(static_cast<double>(prev));
    std::printf("Switching back to 0 dB at %.2f dB: the next sample advances %+.4f dB\n", gainToDb(static_cast<double>(prev)), jumpDb);
    DAWLAB_CHECK_NEAR(gainToDb(static_cast<double>(prev)), -15.0, 0.01);
    DAWLAB_CHECK(jumpDb > 0.0 && jumpDb < 0.01);

    return dawlab::test::summary("l01_ex5_starter");
}
