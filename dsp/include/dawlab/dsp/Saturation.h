#pragma once
// Saturation: waveshaping-style "saturation" (the warm distortion of tape / tube / console gear).
//
// Signal flow (section 3.2 of the Chapter 06 README):
//
//   x ──> drive(dB) ──> [Oversampler xL] ──> shape(curve) ──> [/L] ──> DC blocker ──> output gain ──┐
//   │                                                                                                 mix ──> y
//   └──> delay by latency samples (dry must be aligned with wet, otherwise mixing causes comb filtering) ──┘
//
// - drive: gain applied before the curve. All the curves are designed with "slope = 1 for small signals",
//   so the more drive, the deeper the signal goes into the curved region and the more harmonics appear.
// - shape: four transfer curves (static float shape(curve, x), a pure function that exercises and examples can call directly):
//     Tanh        y = tanh(x)                         classic soft clip, produces only odd harmonics
//     Cubic       y = x - x^3/3 (|x| <= 1), else +/-2/3   polynomial; only a 3rd harmonic (for small signals), hard clip beyond |x| > 1
//     Arctan      y = (2/pi) atan(pi*x/2)              for small signals the 3rd-order term is larger (x^3 coefficient pi^2/12 ~ 0.82 vs tanh's 1/3), softer knee;
//                                                       but its singularities (x = +/-2i/pi ~ +/-0.64i) are closer to the real axis than tanh's (+/-i*pi/2 ~ +/-1.57i),
//                                                       so over the usual drive range (peak <~ 4) it actually has more high harmonics than tanh and they decay more slowly,
//                                                       which is worse for aliasing. Only at extremely high drive, because it approaches +/-1 very slowly (~1/x),
//                                                       does it have somewhat fewer low-to-mid harmonics than tanh (the highest harmonics still decay more slowly than tanh's)
//     Asymmetric  y = tanh(x) (x >= 0), tanh(2x)/2 (x < 0)   asymmetric positive/negative half-waves -> even harmonics + DC (tube-like)
// - DC blocker: an asymmetric curve makes the mean of the signal non-zero (DC offset). DC eats headroom,
//   fools downstream compressors, and clicks on fades, so it is removed with Chapter 02's OnePole
//   highpass (default 10 Hz).
// - Oversampler: harmonics created by the nonlinearity alias when they exceed Nyquist; before prepare, choose 1x/2x/4x/8x with setOversamplingFactorLog2().
// - mix: parallel dry/wet mixing (parallel saturation). The dry signal goes through the same delay as the wet one.
//
// Parameter changes all go through a linear ramp (20 ms) to avoid zipper noise. process() does not allocate.

#include <dawlab/AudioBuffer.h>
#include <dawlab/DspMath.h>
#include <dawlab/dsp/OnePole.h>
#include <dawlab/dsp/Oversampler.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace dawlab::dsp {

enum class SaturationCurve { Tanh, Cubic, Arctan, Asymmetric };

inline const char* saturationCurveName(SaturationCurve curve) noexcept {
    switch (curve) {
    case SaturationCurve::Tanh: return "Tanh";
    case SaturationCurve::Cubic: return "Cubic";
    case SaturationCurve::Arctan: return "Arctan";
    case SaturationCurve::Asymmetric: return "Asymmetric";
    }
    return "?";
}

class Saturation {
public:
    // ---- transfer curves (pure functions) ----------------------------------------------------
    static float shape(SaturationCurve curve, float x) noexcept {
        switch (curve) {
        case SaturationCurve::Tanh:
            return std::tanh(x);
        case SaturationCurve::Cubic:
            // At |x| = 1, y = 2/3 and the slope 1 - x^2 = 0: the value and first derivative are both continuous, so the join to the hard clip has no corner
            if (x >= 1.0f) return 2.0f / 3.0f;
            if (x <= -1.0f) return -2.0f / 3.0f;
            return x - x * x * x * (1.0f / 3.0f);
        case SaturationCurve::Arctan:
            // (2/pi) atan(pi*x/2): slope at the origin (2/pi)(pi/2) = 1, asymptote +/-1 (normalized the same way as tanh to ease comparison).
            // Series x - (pi^2/12) x^3 + ...: the 3rd-order term is larger than tanh's (x - x^3/3), so for the same drive there is more H3 and the knee is softer
            return static_cast<float>((2.0 / kPi) * std::atan(0.5 * kPi * static_cast<double>(x)));
        case SaturationCurve::Asymmetric:
            // Positive half-wave tanh(x) (saturates at +1), negative half-wave tanh(2x)/2 (saturates early, at -0.5).
            // Both sides have slope 1 at the origin, so the curve itself is smooth; asymmetry -> even harmonics + DC.
            return x >= 0.0f ? std::tanh(x) : 0.5f * std::tanh(2.0f * x);
        }
        return x;
    }

    // ---- Configuration (before prepare) ------------------------------------------------------
    // 1x = 0, 2x = 1, 4x = 2, 8x = 3. Call before prepare() (prepare sets up the Oversampler accordingly).
    void setOversamplingFactorLog2(int factorLog2) noexcept {
        oversamplingLog2_ = std::clamp(factorLog2, 0, Oversampler::kMaxFactorLog2);
    }

    // ---- Standard interface ------------------------------------------------------------------
    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        sampleRate_ = sampleRate;
        numChannels_ = numChannels;
        // maxBlockSize <= 0 is invalid input, but process() always cuts chunks of length >= 1,
        // so every buffer below must be allocated with the clamped maxBlockSize_, otherwise we would write into a zero-sized buffer.
        maxBlockSize_ = std::max(1, maxBlockSize);
        oversampler_ = Oversampler(oversamplingLog2_);
        oversampler_.prepare(sampleRate, maxBlockSize_, numChannels);
        latency_ = oversampler_.getLatencySamples();

        dcBlocker_.prepare(sampleRate, maxBlockSize_, numChannels);
        dcBlocker_.setType(OnePole::Type::Highpass);
        dcBlocker_.setCutoff(dcBlockerHz_);

        // dry delay line: a ring buffer of length latency + 1 (with latency = 0 it simply reads back what was written)
        dryDelay_.assign(static_cast<size_t>(numChannels), std::vector<float>(static_cast<size_t>(latency_ + 1), 0.0f));
        dryPos_.assign(static_cast<size_t>(numChannels), 0);
        dryScratch_.setSize(numChannels, maxBlockSize_);

        const int rampSamples = std::max(1, static_cast<int>(std::lround(kRampSeconds * sampleRate)));
        driveGain_.prepare(rampSamples, dbToGain(driveDb_));
        outputGain_.prepare(rampSamples, dbToGain(outputGainDb_));
        mix_.prepare(rampSamples, mixTarget_);
        prepared_ = true;
        reset();
    }

    void reset() noexcept {
        oversampler_.reset();
        dcBlocker_.reset();
        for (auto& d : dryDelay_)
            std::fill(d.begin(), d.end(), 0.0f);
        std::fill(dryPos_.begin(), dryPos_.end(), 0);
        driveGain_.snap();
        outputGain_.snap();
        mix_.snap();
    }

    void setCurve(SaturationCurve curve) noexcept { curve_ = curve; }
    SaturationCurve getCurve() const noexcept { return curve_; }

    // drive: 0 dB = the signal enters the curve unchanged; +20 dB = multiplied by 10 before the curve
    void setDriveDb(float db) noexcept {
        driveDb_ = db;
        driveGain_.setTarget(dbToGain(db));
    }
    float getDriveDb() const noexcept { return driveDb_; }

    // 0 = all dry, 1 = all wet
    void setMix(float mix) noexcept {
        mixTarget_ = std::clamp(mix, 0.0f, 1.0f);
        mix_.setTarget(mixTarget_);
    }
    float getMix() const noexcept { return mixTarget_; }

    // Compensation gain at the end of the wet path (usually lowered again after pushing drive up)
    void setOutputGainDb(float db) noexcept {
        outputGainDb_ = db;
        outputGain_.setTarget(dbToGain(db));
    }
    float getOutputGainDb() const noexcept { return outputGainDb_; }

    // DC blocker (on by default, 10 Hz). Symmetric curves do not actually need it, but leaving it on does no harm
    void setDcBlockerEnabled(bool enabled) noexcept { dcBlockerEnabled_ = enabled; }
    void setDcBlockerHz(float hz) noexcept {
        dcBlockerHz_ = hz;
        if (prepared_)
            dcBlocker_.setCutoff(hz);
    }

    // The factor currently in effect (consistent with getLatencySamples()). Calling setOversamplingFactorLog2()
    // after prepare does not change it; it waits for the next prepare(). Before prepare it returns the factor
    // that "prepare will use".
    int getOversamplingFactor() const noexcept {
        return prepared_ ? oversampler_.getFactor() : (1 << oversamplingLog2_);
    }
    // The value stored by setOversamplingFactorLog2() that only takes effect at the next prepare() (0..3)
    int getPendingOversamplingFactorLog2() const noexcept { return oversamplingLog2_; }
    // Delay in samples at the original rate (= the Oversampler's delay; 0 at 1x)
    int getLatencySamples() const noexcept { return latency_; }

    // Hosts occasionally pass a larger block than the one given to prepare (non-compliant, but it really
    // happens): the internal buffers are all sized by maxBlockSize, so here the block is processed in chunks
    // of at most maxBlockSize. The ramp, FIR and DC blocker states carry over, so the result is identical to processing it in one go.
    void process(AudioBlock block) noexcept {
        const int total = block.getNumSamples();
        for (int start = 0; start < total; start += maxBlockSize_)
            processChunk(block.getSubBlock(start, std::min(maxBlockSize_, total - start)));
    }

private:
    void processChunk(AudioBlock block) noexcept {
        const int n = block.getNumSamples();
        const int numChannels = std::min(block.getNumChannels(), numChannels_);
        if (n <= 0 || numChannels <= 0)
            return;

        // 1. Copy dry into scratch (needed later for the mix) and apply drive on the wet path (the ramp advances once per sample)
        for (int ch = 0; ch < numChannels; ++ch)
            std::copy_n(block.getChannelPointer(ch), n, dryScratch_.getWritePointer(ch));
        for (int i = 0; i < n; ++i) {
            const float drive = driveGain_.next();
            for (int ch = 0; ch < numChannels; ++ch)
                block.getChannelPointer(ch)[i] *= drive;
        }

        // 2. Upsample -> nonlinearity -> downsample (at 1x the Oversampler just copies)
        AudioBlock up = oversampler_.upsample(block);
        const int upLen = up.getNumSamples();
        for (int ch = 0; ch < numChannels; ++ch) {
            float* u = up.getChannelPointer(ch);
            switch (curve_) { // the switch sits outside the loop so it is not evaluated for every sample
            case SaturationCurve::Tanh:
                for (int i = 0; i < upLen; ++i) u[i] = std::tanh(u[i]);
                break;
            default:
                for (int i = 0; i < upLen; ++i) u[i] = shape(curve_, u[i]);
                break;
            }
        }
        oversampler_.downsample(block);

        // 3. DC blocker -> output gain -> mix with the delayed dry signal
        for (int i = 0; i < n; ++i) {
            const float outGain = outputGain_.next();
            const float mix = mix_.next();
            for (int ch = 0; ch < numChannels; ++ch) {
                float* d = block.getChannelPointer(ch);
                float wet = d[i];
                // When NaN arrives from upstream, it passes through the FIR (and leaves by itself after a few dozen
                // samples), but once it enters the recursive state of the DC blocker it never comes out
                // (NaN times anything is NaN) -> all later output would be NaN.
                // So non-finite values are replaced with 0 before the recursive structure: only a short
                // stretch of silence results, after which the signal recovers automatically.
                if (!std::isfinite(wet))
                    wet = 0.0f;
                if (dcBlockerEnabled_)
                    wet = dcBlocker_.processSample(ch, wet);
                wet *= outGain;
                const float dry = pushAndReadDry(ch, dryScratch_.getSample(ch, i)); // the dry sample delayed by latency samples
                d[i] = dry + (wet - dry) * mix; // = dry (1 - mix) + wet mix
            }
        }
    }

    // Linear ramp (same concept as Chapter 01's SmoothedValue; a minimal implementation here avoids a cross-chapter dependency)
    struct LinearRamp {
        float current = 1.0f, target = 1.0f, step = 0.0f;
        int remaining = 0, rampSamples = 1;
        void prepare(int samples, float value) noexcept {
            rampSamples = std::max(1, samples);
            current = target = value;
            remaining = 0;
        }
        void setTarget(float v) noexcept {
            target = v;
            remaining = rampSamples;
            step = (target - current) / static_cast<float>(rampSamples);
        }
        void snap() noexcept { current = target; remaining = 0; }
        float next() noexcept {
            if (remaining > 0) {
                current += step;
                if (--remaining == 0)
                    current = target;
            }
            return current;
        }
    };

    // The ring buffer has length latency + 1: x[n] is written at pos first, and the "next slot" holds x[n - latency], written latency samples ago.
    // With latency = 0 the length is 1 and the same slot is read back immediately after writing -> output unchanged.
    float pushAndReadDry(int ch, float x) noexcept {
        std::vector<float>& d = dryDelay_[static_cast<size_t>(ch)];
        int& pos = dryPos_[static_cast<size_t>(ch)];
        const int size = static_cast<int>(d.size());
        d[static_cast<size_t>(pos)] = x;
        pos = (pos + 1 == size) ? 0 : pos + 1;
        return d[static_cast<size_t>(pos)];
    }

    static constexpr double kRampSeconds = 0.02;

    SaturationCurve curve_ = SaturationCurve::Tanh;
    float driveDb_ = 0.0f;
    float outputGainDb_ = 0.0f;
    float mixTarget_ = 1.0f;
    float dcBlockerHz_ = 10.0f;
    bool dcBlockerEnabled_ = true;
    int oversamplingLog2_ = 1;
    bool prepared_ = false;

    double sampleRate_ = 48000.0;
    int numChannels_ = 0;
    int maxBlockSize_ = 1;
    int latency_ = 0;

    Oversampler oversampler_{1};
    OnePole dcBlocker_;
    std::vector<std::vector<float>> dryDelay_;
    std::vector<int> dryPos_;
    AudioBuffer dryScratch_;
    LinearRamp driveGain_, outputGain_, mix_;
};

} // namespace dawlab::dsp
