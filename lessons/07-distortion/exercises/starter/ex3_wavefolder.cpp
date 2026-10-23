// Exercise 3 (starter): implement a wavefolder
//
// Goals:
//   (a) Write foldTriangle(u, threshold): the part beyond ±threshold is "folded back", possibly many times (a triangle wave of period 4·threshold).
//       With threshold = 1 it must match dawlab::dsp::Distortion::foldback() exactly.
//   (b) Write foldSine(u): y = sin(π u / 2). For |u| <= 1 it behaves like a soft clip, then folds back smoothly (a simplified Buchla-style folder).
//   (c) Wrap (a) in a Wavefolder class (standard interface + setDriveDb + setThreshold) that processes blocks at 4x oversampling.
//
// Run: l07_ex3_starter → it currently FAILs because of the TODOs; once you finish, everything should PASS.

#include <dawlab/Common.h>
#include <dawlab/dsp/Distortion.h>
#include <dawlab/dsp/Oversampler.h>

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

// (a) Triangle folder: maps u onto a triangle wave of period 4T, with y = u for |u| <= T
float foldTriangle(float u, float threshold) {
    // TODO: hint -- first take (u + T) modulo 4T (std::fmod; add the period back for negative values) to get t ∈ [0, 4T),
    //       then y = t - T when t < 2T, otherwise y = 3T - t.
    (void)threshold;
    return u;
}

// (b) Sine folder: y = sin(π u / 2)
float foldSine(float u) {
    // TODO
    return u;
}

// (c) Wavefolder: drive → oversample → foldTriangle → downsample
class Wavefolder {
public:
    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        oversampler_.prepare(sampleRate, maxBlockSize, numChannels);
        numChannels_ = numChannels;
    }
    void reset() noexcept { oversampler_.reset(); }
    void setDriveDb(float db) noexcept { drive_ = dawlab::dbToGain(db); }
    void setThreshold(float t) noexcept { threshold_ = std::max(0.05f, t); }
    int getLatencySamples() const noexcept { return oversampler_.getLatencySamples(); }

    void process(dawlab::AudioBlock block) noexcept {
        // TODO: 1. upsample  2. for every sample y = foldTriangle(x * drive_, threshold_)  3. downsample
        (void)block;
    }

private:
    dawlab::dsp::Oversampler oversampler_{2};
    int numChannels_ = 0;
    float drive_ = 1.0f;
    float threshold_ = 1.0f;
};

template <typename Processor>
void processInBlocks(dawlab::AudioBuffer& buffer, Processor& processor) {
    dawlab::AudioBlock whole(buffer);
    for (int start = 0; start < buffer.getNumSamples(); start += kBlockSize) {
        const int len = std::min(kBlockSize, buffer.getNumSamples() - start);
        processor.process(whole.getSubBlock(start, len));
    }
}

} // namespace

int main() {
    using dawlab::dsp::Distortion;

    // (a) With threshold = 1 it matches Distortion::foldback; a few hand-computed points
    DAWLAB_CHECK_NEAR(foldTriangle(0.5f, 1.0f), 0.5, 1e-6);
    DAWLAB_CHECK_NEAR(foldTriangle(1.5f, 1.0f), 0.5, 1e-6);
    DAWLAB_CHECK_NEAR(foldTriangle(2.0f, 1.0f), 0.0, 1e-6);
    DAWLAB_CHECK_NEAR(foldTriangle(3.0f, 1.0f), -1.0, 1e-6);
    DAWLAB_CHECK_NEAR(foldTriangle(-2.5f, 1.0f), 0.5, 1e-6);
    DAWLAB_CHECK_NEAR(foldTriangle(1.0f, 0.5f), 0.0, 1e-6);  // threshold 0.5: u = 1 folds exactly to 0
    DAWLAB_CHECK_NEAR(foldTriangle(0.75f, 0.5f), 0.25, 1e-6);
    float maxDiff = 0.0f;
    for (int i = -400; i <= 400; ++i) {
        const float u = 0.025f * static_cast<float>(i);
        maxDiff = std::max(maxDiff, std::abs(foldTriangle(u, 1.0f) - Distortion::foldback(u)));
    }
    DAWLAB_CHECK(maxDiff < 1e-5f);

    // (b) sine folder
    DAWLAB_CHECK_NEAR(foldSine(0.0f), 0.0, 1e-6);
    DAWLAB_CHECK_NEAR(foldSine(1.0f), 1.0, 1e-6);
    DAWLAB_CHECK_NEAR(foldSine(2.0f), 0.0, 1e-6);
    DAWLAB_CHECK_NEAR(foldSine(3.0f), -1.0, 1e-6);
    DAWLAB_CHECK_NEAR(foldSine(0.1f), std::sin(0.1 * dawlab::kPi / 2.0), 1e-6);

    // (c) Wavefolder: small signals are transparent; large signals fold back and the output does not exceed the threshold
    {
        Wavefolder wf;
        wf.prepare(kSampleRate, kBlockSize, 1);
        wf.setThreshold(1.0f);
        wf.setDriveDb(0.0f);
        dawlab::AudioBuffer small = dawlab::makeSine(220.0, kSampleRate, 1.0, 0.25f, 1);
        processInBlocks(small, wf);
        const int skip = dawlab::secondsToSamples(0.2, kSampleRate), n = dawlab::secondsToSamples(0.5, kSampleRate);
        const double thdSmall = dawlab::thdPercent(small.getReadPointer(0) + skip, n, 220.0, kSampleRate, 20);
        std::printf("drive 0 dB, 0.25 amplitude: THD = %.4f%% (should be close to 0)\n", thdSmall);
        DAWLAB_CHECK(thdSmall < 0.1);

        wf.reset();
        wf.setDriveDb(18.0f); // 0.25 × 7.94 ≈ 2: folds back once
        dawlab::AudioBuffer big = dawlab::makeSine(220.0, kSampleRate, 1.0, 0.25f, 1);
        processInBlocks(big, wf);
        const double thdBig = dawlab::thdPercent(big.getReadPointer(0) + skip, n, 220.0, kSampleRate, 20);
        const float peak = dawlab::peakAbs(big);
        std::printf("drive 18 dB: THD = %.1f%%, peak = %.3f (<= 1 + a little filter overshoot)\n", thdBig, peak);
        DAWLAB_CHECK(thdBig > 20.0);
        DAWLAB_CHECK(peak < 1.1f);
        DAWLAB_CHECK(!dawlab::hasNanOrInf(big));
        std::printf("latency = %d samples\n", wf.getLatencySamples());
        DAWLAB_CHECK(wf.getLatencySamples() > 0);
    }

    return dawlab::test::summary("l07_ex3");
}
