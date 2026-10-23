#pragma once
// Chapter 11: LoudnessMeter -- ITU-R BS.1770-4 / EBU R128 loudness measurement (LUFS).
//
// Pipeline (the first two steps are done per channel):
//   1. K-weighting: two biquad stages (the chapter 02 Biquad)
//        stage 1: high shelf (+4 dB above roughly 1.68 kHz), modelling the acoustic effect of the head
//        stage 2: highpass (about 38 Hz), removing the very low frequencies we barely hear
//   2. Mean square: for each 400 ms block (gating block), compute z_i = (1/T) Σ y_i[n]^2.
//      Blocks overlap by 75%, i.e. a new block is produced every 100 ms.
//   3. Weight and sum the channels, then convert to LUFS:
//        L = −0.691 + 10·log10( Σ_i G_i · z_i )
//      G_i: L, R, C = 1.0; Ls, Rs = 1.41 (+1.5 dB); LFE is excluded
//   4. momentary   = the last 400 ms (one block)
//      short-term  = the last 3 s (average of thirty 100 ms sub-blocks)
//      integrated  = the average of all blocks after two gates:
//          absolute gate: discard blocks below −70 LUFS
//          relative gate: discard blocks below (mean of the blocks that passed the absolute gate − 10 LU)
//
// In the implementation we accumulate the mean square in 100 ms units (sub-blocks): a 400 ms block is
// the latest 4 sub-blocks and 3 s is the latest 30 sub-blocks, so each sample needs only one
// square-and-accumulate.
//
// The integrated gating needs "the loudness of every block" -- storing them in a vector would allocate
// on the audio thread. Instead we use a histogram: [−70, +10) LUFS is divided into 800 bins of 0.1 LU,
// and each bin records the block count and the sum of mean squares. The relative gate is judged at the
// bin centre, with an error of at most 0.05 LU, and nothing is ever allocated.
//
// All getters read std::atomic (relaxed), so the UI thread can read them directly.

#include <dawlab/AudioBuffer.h>
#include <dawlab/DspMath.h>
#include <dawlab/dsp/Biquad.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

namespace dawlab::dsp {

class LoudnessMeter {
public:
    static constexpr float kAbsoluteGateLufs = -70.0f;
    static constexpr float kRelativeGateLu = -10.0f;
    static constexpr double kBlockSeconds = 0.4;   // gating block length
    static constexpr double kStepSeconds = 0.1;    // 75% overlap -> a new block every 100 ms
    static constexpr int kBlockSteps = 4;          // 400 ms / 100 ms
    static constexpr int kShortTermSteps = 30;     // 3 s / 100 ms
    static constexpr float kLufsOffset = -0.691f;  // compensates the K-weighting gain at 1 kHz (about +0.691 dB)
    static constexpr int kMaxChannels = 8;

    // ---- K-weighting coefficients ----------------------------------------------------------
    // Table 1 / Table 2 of ITU-R BS.1770-4 list coefficients for 48 kHz only (a0 normalized to 1):
    //
    //   stage 1 (pre-filter, high shelf)   b0 =  1.53512485958697  b1 = -2.69169618940638  b2 = 1.19839281085285
    //                                      a1 = -1.69065929318241  a2 =  0.73248077421585
    //   stage 2 (RLB weighting, highpass)  b0 =  1.0               b1 = -2.0               b2 = 1.0
    //                                      a1 = -1.99004745483398  a2 =  0.99007225036621
    //
    // The standard gives no coefficients for other sample rates; they must be redesigned from the
    // "analog prototype":
    //
    //   stage 1: H1(s) = (Vh·s² + Vb·s/Q + 1) / (s² + s/Q + 1)   Vh = 10^(G/20), Vb = Vh^0.49967
    //            f0 = 1681.974 Hz, G = 3.9998 dB, Q = 0.70718
    //            gain is 1 as s → 0 (DC) and Vh ≈ +4 dB as s → ∞ (high frequencies): a high shelf
    //   stage 2: H2(s) = s² / (s² + s/Q + 1)                     f0 = 38.135 Hz, Q = 0.50033
    //
    //   s is normalized to ω0 = 2π f0. The bilinear transform uses s = (1/K)·(1 − z⁻¹)/(1 + z⁻¹) with
    //   K = tan(π f0 / fs) (this K maps f0 exactly onto f0, so frequency warping does not move it).
    //   Substituting into H1 and multiplying numerator and denominator by K²(1 + z⁻¹)²:
    //     numerator   = Vh(1 − z⁻¹)² + Vb(K/Q)(1 − z⁻¹)(1 + z⁻¹) + K²(1 + z⁻¹)²
    //     denominator =   (1 − z⁻¹)² +    (K/Q)(1 − z⁻¹)(1 + z⁻¹) + K²(1 + z⁻¹)²
    //   Expanding into polynomials in z⁻¹ gives the expressions in makeKWeightingStage1(), such as
    //   b0 = (Vh + Vb·K/Q + K²)/a0.
    //
    // These prototype parameters were back-derived from the 48 kHz standard coefficients (libebur128 uses
    // the same set); plugging in 48 kHz reproduces the table above (error < 1e-10, checked by the
    // loudness_verify example), and plugging in 44.1 / 96 kHz gives the same response curve.
    // Note that stage 1 is not the RBJ cookbook shelf: RBJ defines the corner at "half the gain", so the
    // two parameter sets are not interchangeable.
    static constexpr double kStage1Hz = 1681.974450955533;
    static constexpr double kStage1Q = 0.7071752369554196;
    static constexpr double kStage1GainDb = 3.999843853973347;
    static constexpr double kStage1VbExponent = 0.4996667741545416;
    static constexpr double kStage2Hz = 38.13547087602444;
    static constexpr double kStage2Q = 0.5003270373238773;

    // The standard 48 kHz coefficient tables (b0, b1, b2, a1, a2), for verification and teaching
    static constexpr double kStage1Table48k[5] = {1.53512485958697, -2.69169618940638, 1.19839281085285,
                                                  -1.69065929318241, 0.73248077421585};
    static constexpr double kStage2Table48k[5] = {1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621};

    static BiquadCoefficients makeKWeightingStage1(double sampleRate) noexcept {
        const double K = std::tan(kPi * kStage1Hz / sampleRate); // frequency mapping of the bilinear transform
        const double Vh = std::pow(10.0, kStage1GainDb / 20.0);
        const double Vb = std::pow(Vh, kStage1VbExponent);
        const double a0 = 1.0 + K / kStage1Q + K * K;
        BiquadCoefficients c;
        c.b0 = (Vh + Vb * K / kStage1Q + K * K) / a0;
        c.b1 = 2.0 * (K * K - Vh) / a0;
        c.b2 = (Vh - Vb * K / kStage1Q + K * K) / a0;
        c.a1 = 2.0 * (K * K - 1.0) / a0;
        c.a2 = (1.0 - K / kStage1Q + K * K) / a0;
        return c;
    }

    // The numerator of the standard's Table 2 is fixed at 1, -2, 1 (not divided by a0), so the high-frequency
    // gain is a0 ≈ +0.043 dB rather than exactly 0 dB; we follow the standard, and that little bit of gain
    // is already included in the -0.691 offset.
    static BiquadCoefficients makeKWeightingStage2(double sampleRate) noexcept {
        const double K = std::tan(kPi * kStage2Hz / sampleRate);
        const double a0 = 1.0 + K / kStage2Q + K * K;
        BiquadCoefficients c;
        c.b0 = 1.0;
        c.b1 = -2.0;
        c.b2 = 1.0;
        c.a1 = 2.0 * (K * K - 1.0) / a0;
        c.a2 = (1.0 - K / kStage2Q + K * K) / a0;
        return c;
    }

    // mean square (already weighted and summed) -> LUFS
    static float powerToLufs(double weightedMeanSquare) noexcept {
        if (weightedMeanSquare <= 1e-15) // about -150 dB, treated as silence
            return kMinusInfinityDb;
        return kLufsOffset + static_cast<float>(10.0 * std::log10(weightedMeanSquare));
    }

    static double lufsToPower(float lufs) noexcept { return std::pow(10.0, (lufs - kLufsOffset) / 10.0); }

    // ---- Standard interface ---------------------------------------------------------------
    void prepare(double sampleRate, int numChannels) {
        assert(sampleRate > 0.0 && numChannels > 0 && numChannels <= kMaxChannels);
        sampleRate_ = sampleRate;
        numChannels_ = std::min(numChannels, kMaxChannels);
        stepSamples_ = std::max(1, static_cast<int>(std::lround(kStepSeconds * sampleRate)));

        stage1_.prepare(sampleRate, 1, numChannels_);
        stage2_.prepare(sampleRate, 1, numChannels_);
        stage1_.setCoefficients(makeKWeightingStage1(sampleRate));
        stage2_.setCoefficients(makeKWeightingStage2(sampleRate));

        histogramCount_.assign(kHistogramBins, 0u);
        histogramPower_.assign(kHistogramBins, 0.0);
        reset();
    }

    void reset() noexcept {
        stage1_.reset();
        stage2_.reset();
        stepAccumulators_.fill(0.0);
        stepRing_.fill(0.0);
        stepSampleCount_ = 0;
        ringPos_ = 0;
        stepsSeen_ = 0;
        std::fill(histogramCount_.begin(), histogramCount_.end(), 0u);
        std::fill(histogramPower_.begin(), histogramPower_.end(), 0.0);
        momentary_.store(kMinusInfinityDb, std::memory_order_relaxed);
        shortTerm_.store(kMinusInfinityDb, std::memory_order_relaxed);
        integrated_.store(kMinusInfinityDb, std::memory_order_relaxed);
        maxMomentary_.store(kMinusInfinityDb, std::memory_order_relaxed);
        maxShortTerm_.store(kMinusInfinityDb, std::memory_order_relaxed);
        updateCount_.store(0u, std::memory_order_relaxed);
    }

    // Per-channel weights (all 1.0 by default). For 5.1, set Ls / Rs to 1.41 and LFE to 0.
    void setChannelWeight(int channel, float weight) noexcept {
        if (channel >= 0 && channel < kMaxChannels)
            channelWeights_[static_cast<size_t>(channel)] = weight;
    }
    float getChannelWeight(int channel) const noexcept {
        return (channel >= 0 && channel < kMaxChannels) ? channelWeights_[static_cast<size_t>(channel)] : 0.0f;
    }

    // Audio thread: no allocation, no locks
    void process(const AudioBlock& block) noexcept {
        const int nch = std::min(numChannels_, block.getNumChannels());
        const int n = block.getNumSamples();
        for (int i = 0; i < n; ++i) {
            for (int ch = 0; ch < nch; ++ch) {
                // K-weighting: two biquads in series
                const float y = stage2_.processSample(ch, stage1_.processSample(ch, block.getSample(ch, i)));
                stepAccumulators_[static_cast<size_t>(ch)] += static_cast<double>(y) * y;
            }
            if (++stepSampleCount_ >= stepSamples_)
                finishStep();
        }
    }

    // ---- thread-safe getters -------------------------------------------------------------
    float getMomentaryLufs() const noexcept { return momentary_.load(std::memory_order_relaxed); }   // 400 ms
    float getShortTermLufs() const noexcept { return shortTerm_.load(std::memory_order_relaxed); }   // 3 s
    float getIntegratedLufs() const noexcept { return integrated_.load(std::memory_order_relaxed); } // gated
    // Maxima: only complete windows count (max momentary starts updating at 400 ms, max short-term at 3 s);
    // before that they return kMinusInfinityDb
    float getMaxMomentaryLufs() const noexcept { return maxMomentary_.load(std::memory_order_relaxed); }
    float getMaxShortTermLufs() const noexcept { return maxShortTerm_.load(std::memory_order_relaxed); }

    // Incremented every 100 ms (at the end of each sub-block). A UI or an exercise (e.g. LRA) can use it to
    // tell whether the short-term value has been updated.
    uint32_t getUpdateCount() const noexcept { return updateCount_.load(std::memory_order_relaxed); }

    double getSampleRate() const noexcept { return sampleRate_; }
    int getNumChannels() const noexcept { return numChannels_; }
    int getStepSamples() const noexcept { return stepSamples_; }

private:
    static constexpr int kHistogramBins = 800;        // [-70, +10) LUFS
    static constexpr float kHistogramMinLufs = -70.0f;
    static constexpr float kHistogramStepLu = 0.1f;

    // A 100 ms sub-block has ended: weight and sum the per-channel sums of squares, put the result into the
    // ring, and update the three loudness values
    void finishStep() noexcept {
        double weighted = 0.0;
        for (int ch = 0; ch < numChannels_; ++ch) {
            weighted += static_cast<double>(channelWeights_[static_cast<size_t>(ch)]) *
                        stepAccumulators_[static_cast<size_t>(ch)];
            stepAccumulators_[static_cast<size_t>(ch)] = 0.0;
        }
        stepSampleCount_ = 0;
        stepRing_[static_cast<size_t>(ringPos_)] = weighted; // this slot holds the sum of squares, not yet divided by the sample count
        ringPos_ = (ringPos_ + 1) % kShortTermSteps;
        if (stepsSeen_ < kShortTermSteps)
            ++stepsSeen_;

        // momentary: the latest 4 sub-blocks; short-term: the latest 30 (fewer if not that many have accumulated yet)
        const int momentarySteps = std::min(stepsSeen_, kBlockSteps);
        const int shortTermSteps = stepsSeen_;
        double sumM = 0.0, sumS = 0.0;
        for (int k = 0; k < shortTermSteps; ++k) {
            const int idx = (ringPos_ - 1 - k + kShortTermSteps) % kShortTermSteps;
            const double v = stepRing_[static_cast<size_t>(idx)];
            sumS += v;
            if (k < momentarySteps)
                sumM += v;
        }
        const double momentaryPower = sumM / (static_cast<double>(momentarySteps) * stepSamples_);
        const double shortTermPower = sumS / (static_cast<double>(shortTermSteps) * stepSamples_);
        const float momentaryLufs = powerToLufs(momentaryPower);
        const float shortTermLufs = powerToLufs(shortTermPower);
        momentary_.store(momentaryLufs, std::memory_order_relaxed);
        shortTerm_.store(shortTermLufs, std::memory_order_relaxed);
        // The maxima are updated only once the window is full: in the first 400 ms / 3 s only 1-3 (or fewer
        // than 30) sub-blocks are averaged, so a strong transient right at the start would be overestimated
        // (e.g. a 100 ms burst averaged over 100 ms instead of 400 ms reads 6 dB too high), and the result
        // would depend on where the content sits in the stream. The live momentary / short-term readings are
        // still shown as usual so the UI reacts from the very beginning.
        if (stepsSeen_ >= kBlockSteps && momentaryLufs > maxMomentary_.load(std::memory_order_relaxed))
            maxMomentary_.store(momentaryLufs, std::memory_order_relaxed);
        if (stepsSeen_ >= kShortTermSteps && shortTermLufs > maxShortTerm_.load(std::memory_order_relaxed))
            maxShortTerm_.store(shortTermLufs, std::memory_order_relaxed);

        // integrated: only a complete 400 ms block counts as a gating block
        if (stepsSeen_ >= kBlockSteps && momentaryLufs > kAbsoluteGateLufs) {
            int bin = static_cast<int>(std::floor((momentaryLufs - kHistogramMinLufs) / kHistogramStepLu));
            bin = std::clamp(bin, 0, kHistogramBins - 1);
            ++histogramCount_[static_cast<size_t>(bin)];
            histogramPower_[static_cast<size_t>(bin)] += momentaryPower;
            integrated_.store(computeIntegrated(), std::memory_order_relaxed);
        }
        updateCount_.fetch_add(1u, std::memory_order_relaxed);
    }

    float computeIntegrated() const noexcept {
        // First gate: the absolute gate was already applied when blocks went into the histogram. Compute the
        // mean of the blocks that passed it
        double sumPower = 0.0;
        uint64_t count = 0;
        for (int b = 0; b < kHistogramBins; ++b) {
            sumPower += histogramPower_[static_cast<size_t>(b)];
            count += histogramCount_[static_cast<size_t>(b)];
        }
        if (count == 0)
            return kMinusInfinityDb;
        const float relativeGate = powerToLufs(sumPower / static_cast<double>(count)) + kRelativeGateLu;
        // Second gate: the relative gate (judged at the bin centre)
        double gatedPower = 0.0;
        uint64_t gatedCount = 0;
        for (int b = 0; b < kHistogramBins; ++b) {
            const float binCentre = kHistogramMinLufs + (static_cast<float>(b) + 0.5f) * kHistogramStepLu;
            if (binCentre >= relativeGate) {
                gatedPower += histogramPower_[static_cast<size_t>(b)];
                gatedCount += histogramCount_[static_cast<size_t>(b)];
            }
        }
        if (gatedCount == 0)
            return kMinusInfinityDb;
        return powerToLufs(gatedPower / static_cast<double>(gatedCount));
    }

    double sampleRate_ = 0.0;
    int numChannels_ = 0;
    int stepSamples_ = 1;
    Biquad stage1_, stage2_;
    std::array<float, kMaxChannels> channelWeights_{{1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f}};
    std::array<double, kMaxChannels> stepAccumulators_{};
    std::array<double, kShortTermSteps> stepRing_{};
    int stepSampleCount_ = 0;
    int ringPos_ = 0;
    int stepsSeen_ = 0;
    std::vector<uint32_t> histogramCount_;
    std::vector<double> histogramPower_;

    std::atomic<float> momentary_{kMinusInfinityDb};
    std::atomic<float> shortTerm_{kMinusInfinityDb};
    std::atomic<float> integrated_{kMinusInfinityDb};
    std::atomic<float> maxMomentary_{kMinusInfinityDb};
    std::atomic<float> maxShortTerm_{kMinusInfinityDb};
    std::atomic<uint32_t> updateCount_{0};
};

} // namespace dawlab::dsp
