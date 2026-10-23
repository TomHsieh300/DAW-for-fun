#pragma once
// Distortion.h -- Chapter 07: hard clip, foldback, fuzz, rectify, and bitcrusher.
//
// Chapter 06's Saturation "gently rounds the waveform off"; this chapter's Distortion deliberately
// breaks the waveform, folds it back, cuts half of it away, or throws away its resolution. Signal flow:
//
//   in ──┬──────────────── dry (delayed by latency, aligned with wet) ──────────────────┐
//        │                                                                           ├─ mix ─→ out
//        └─ drive ─┬→ [ Oversampler ↑ → shaper → ↓ ] (HardClip/Foldback/Fuzz/Rectify)   │
//                  └→ [ sample & hold → quantize → latency delay ] (Bitcrush)            │
//                        pick one by type (crossfade on change) → DC blocker (Fuzz/Rectify) │
//                        → tone lowpass → output gain ──────────────────────────────────┘
//
// Design notes:
// - Every memoryless curve is a static function, so it can be plotted, used in exercises, and unit-tested on its own.
// - HardClip / Foldback / Fuzz / Rectify generate infinitely many harmonics, so they must run at the
//   oversampled rate; otherwise the high harmonics alias back into the audible band. Bitcrush's aliasing is
//   itself the effect, so it runs at the original sample rate. Both paths are padded to the same latency (the host needs only one fixed latency).
// - Both paths (and the DC blocker) **run on every block**; the type only decides which output is used.
//   That way, when the type changes, the new path's FIR / delay lines hold the "just now" input rather than stale audio from long ago;
//   together with the kTypeFadeMs crossfade, changing type does not click (cost: the oversampler idles along while Bitcrush is active).
// - Only process()/processSample()/setXxx()/reset() may be called on the audio thread;
//   prepare() is the only place that allocates.

#include <dawlab/AudioBuffer.h>
#include <dawlab/DspMath.h>
#include <dawlab/dsp/Biquad.h>
#include <dawlab/dsp/OnePole.h>
#include <dawlab/dsp/Oversampler.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <memory>
#include <vector>

namespace dawlab::dsp {

enum class DistortionType { HardClip, Foldback, Fuzz, Rectify, Bitcrush };

// Two ways to rectify: full-wave takes the absolute value (pitch goes up an octave); half-wave cuts the negative half-cycle (the fundamental remains)
enum class RectifyMode { FullWave, HalfWave };

inline const char* distortionTypeName(DistortionType type) noexcept {
    switch (type) {
    case DistortionType::HardClip: return "HardClip";
    case DistortionType::Foldback: return "Foldback";
    case DistortionType::Fuzz: return "Fuzz";
    case DistortionType::Rectify: return "Rectify";
    case DistortionType::Bitcrush: return "Bitcrush";
    }
    return "?";
}

namespace detail {

// Fixed-length integer delay: used to align the dry signal (and the bitcrush path, which bypasses the oversampler)
// with the latency introduced by the oversampler, so the mix knob does not produce comb filtering at its midpoint.
class FixedDelay {
public:
    void prepare(int delaySamples) {
        delay_ = std::max(0, delaySamples);
        buffer_.assign(static_cast<size_t>(std::max(1, delay_)), 0.0f);
        pos_ = 0;
    }
    void reset() noexcept {
        std::fill(buffer_.begin(), buffer_.end(), 0.0f);
        pos_ = 0;
    }
    float process(float x) noexcept {
        if (delay_ == 0)
            return x;
        const float y = buffer_[pos_]; // the value written delay_ samples ago
        buffer_[pos_] = x;
        if (++pos_ >= buffer_.size())
            pos_ = 0;
        return y;
    }

private:
    int delay_ = 0;
    std::vector<float> buffer_;
    size_t pos_ = 0;
};

// A parameter that is linearly interpolated once per block (the simplest way to avoid zipper noise):
// it moves from current to target over one block (512 samples ≈ 10 ms).
struct BlockRamp {
    float current = 1.0f;
    float target = 1.0f;

    void snap(float v) noexcept { current = target = v; }
    float incrementFor(int numSamples) const noexcept {
        return numSamples > 0 ? (target - current) / static_cast<float>(numSamples) : 0.0f;
    }
};

} // namespace detail

class Distortion {
public:
    // Three constants of the Fuzz curve: the dead zone (the source of gating) and the different clip heights of the positive and negative half-cycles (asymmetric → even harmonics)
    static constexpr float kFuzzDeadZone = 0.1f;
    static constexpr float kFuzzPositiveCeiling = 1.0f;
    static constexpr float kFuzzNegativeCeiling = 0.6f;
    // At or above this tone frequency the post-lowpass is bypassed entirely
    static constexpr float kToneBypassHz = 20000.0f;
    // Highpass cutoff of the DC blocker (Fuzz / Rectify generate DC)
    static constexpr float kDcBlockerHz = 10.0f;
    // Maximum oversampling factor 2^3 = 8x (the Chapter 06 Oversampler supports up to 8x)
    static constexpr int kMaxOversamplingLog2 = 3;
    // Length of the crossfade between the old and new type outputs when the type changes
    static constexpr double kTypeFadeMs = 10.0;

    // ---- Standard interface -----------------------------------------------------------------

    // Call before prepare(): oversampling factor = 2^log2 (0 = no oversampling). Default is 2 (4x).
    void setOversamplingFactorLog2(int factorLog2) noexcept {
        osLog2_ = std::clamp(factorLog2, 0, kMaxOversamplingLog2);
    }

    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        sampleRate_ = sampleRate;
        // maxBlockSize <= 0 is invalid input; allocate at least 1, and process() also slices blocks to this length
        maxBlockSize_ = std::max(1, maxBlockSize);
        numChannels_ = numChannels;

        if (osLog2_ > 0) {
            oversampler_ = std::make_unique<Oversampler>(osLog2_);
            oversampler_->prepare(sampleRate, maxBlockSize_, numChannels);
            latency_ = oversampler_->getLatencySamples();
        } else {
            oversampler_.reset();
            latency_ = 0;
        }

        dcBlocker_.prepare(sampleRate, maxBlockSize_, numChannels);
        dcBlocker_.setType(OnePole::Type::Highpass);
        dcBlocker_.setCutoff(kDcBlockerHz);
        tone_.prepare(sampleRate, maxBlockSize_, numChannels);
        toneHzApplied_ = -1.0f; // force the tone coefficients to be recomputed on the next block
        prepared_ = true;

        fadeLength_ = std::max(1, msToSamples(kTypeFadeMs, sampleRate));
        snapTypeFade();

        dryDelay_.assign(static_cast<size_t>(numChannels), detail::FixedDelay());
        wetDelay_.assign(static_cast<size_t>(numChannels), detail::FixedDelay());
        for (auto& d : dryDelay_)
            d.prepare(latency_);
        for (auto& d : wetDelay_)
            d.prepare(latency_);
        dryBuffer_.setSize(numChannels, maxBlockSize_);
        crushBuffer_.setSize(numChannels, maxBlockSize_);
        holdValue_.assign(static_cast<size_t>(numChannels), 0.0f);
        holdCounter_.assign(static_cast<size_t>(numChannels), 0);

        driveGain_.snap(dbToGain(driveDb_));
        outputGain_.snap(dbToGain(outputGainDb_));
        mixRamp_.snap(mix_);
    }

    void reset() noexcept {
        if (oversampler_)
            oversampler_->reset();
        dcBlocker_.reset();
        tone_.reset();
        for (auto& d : dryDelay_)
            d.reset();
        for (auto& d : wetDelay_)
            d.reset();
        std::fill(holdValue_.begin(), holdValue_.end(), 0.0f);
        std::fill(holdCounter_.begin(), holdCounter_.end(), 0);
        driveGain_.snap(driveGain_.target);
        outputGain_.snap(outputGain_.target);
        mixRamp_.snap(mixRamp_.target);
        snapTypeFade();
    }

    // ---- Parameters ---------------------------------------------------------------------------

    // process() starts a kTypeFadeMs crossfade at the beginning of the next block (no click);
    // if the type is changed again while a crossfade is in progress, the next one starts only after this one finishes.
    // If called after prepare() / reset() and before the first block, there is no fade and the change takes effect immediately.
    void setType(DistortionType type) noexcept { type_ = type; }
    void setRectifyMode(RectifyMode mode) noexcept { rectifyMode_ = mode; }

    // Gain applied before the shaper (0..60 dB). The higher the drive, the more of the waveform hits the clipping region.
    void setDriveDb(float db) noexcept {
        driveDb_ = std::clamp(db, 0.0f, 60.0f);
        driveGain_.target = dbToGain(driveDb_);
    }

    // Post lowpass (the tone knob): trims the harsh high harmonics after distortion. >= 20 kHz = bypass.
    void setToneHz(float hz) noexcept { toneHz_ = std::clamp(hz, 200.0f, kToneBypassHz); }

    void setMix(float mix) noexcept {
        mix_ = std::clamp(mix, 0.0f, 1.0f);
        mixRamp_.target = mix_;
    }

    void setOutputGainDb(float db) noexcept {
        outputGainDb_ = std::clamp(db, -60.0f, 24.0f);
        outputGain_.target = dbToGain(outputGainDb_);
    }

    // Bitcrush: quantizer bit depth (fractional values such as 5.5 bit are allowed) and the sample & hold factor (1 = no sample-rate reduction)
    // Note that for bits < 2 the only positive code left is 0 (see quantize()), so the output is asymmetric and carries DC.
    void setBitDepth(float bits) noexcept { bitDepth_ = std::clamp(bits, 1.0f, 24.0f); }
    void setDownsampleFactor(int factor) noexcept { downsampleFactor_ = std::clamp(factor, 1, 64); }

    DistortionType getType() const noexcept { return type_; }
    RectifyMode getRectifyMode() const noexcept { return rectifyMode_; }
    float getDriveDb() const noexcept { return driveDb_; }
    float getToneHz() const noexcept { return toneHz_; }
    float getMix() const noexcept { return mix_; }
    float getOutputGainDb() const noexcept { return outputGainDb_; }
    float getBitDepth() const noexcept { return bitDepth_; }
    int getDownsampleFactor() const noexcept { return downsampleFactor_; }
    // After prepare, reports the factor actually in effect (consistent with getLatencySamples()); a value passed to
    // setOversamplingFactorLog2() after prepare only takes effect at the next prepare. Before prepare, reports the configured value.
    int getOversamplingFactor() const noexcept {
        if (!prepared_)
            return 1 << osLog2_;
        return oversampler_ ? oversampler_->getFactor() : 1;
    }

    // The same for every type (the bitcrush path and the dry path are padded with the same delay)
    int getLatencySamples() const noexcept { return latency_; }

    // ---- Static curves (memoryless): the input u has already been multiplied by drive ---------

    // hard clip: y = clamp(u, -1, 1). As drive → ∞ it approaches a square wave (THD → 48.3%).
    static float hardClip(float u) noexcept { return std::clamp(u, -1.0f, 1.0f); }

    // foldback / wavefolding: the part beyond ±1 is not cut off but "folded back".
    // Equivalent to feeding u into a triangle wave of period 4: y = u for u in [-1, 1]; y = 2 - u for u in [1, 3]; y = u - 4 for u in [3, 5] ...
    static float foldback(float u) noexcept {
        float t = std::fmod(u + 1.0f, 4.0f); // shift, then take the remainder over the period; range (-4, 4)
        if (t < 0.0f)
            t += 4.0f; // range [0, 4)
        return t < 2.0f ? t - 1.0f : 3.0f - t;
    }

    // fuzz: models a high-gain transistor stage with abnormal bias.
    //   1. For |u| < dead zone nothing conducts → as a note decays it suddenly "shuts the gate" (gating / sputter)
    //   2. The positive half-cycle reaches at most +1.0 and the negative at least -0.6 (asymmetric → even harmonics + DC, so a DC block follows)
    static float fuzz(float u) noexcept {
        const float a = std::abs(u);
        if (a <= kFuzzDeadZone)
            return 0.0f;
        const float v = a - kFuzzDeadZone; // continuous from the dead-zone edge, no jump
        return u > 0.0f ? std::min(v, kFuzzPositiveCeiling) : -std::min(v, kFuzzNegativeCeiling);
    }

    // rectify: full-wave = |u| (the fundamental disappears, leaving only even harmonics → one octave higher), half-wave = max(u, 0).
    // The output is limited to [0, 1]; anything above is clipped.
    static float rectify(float u, RectifyMode mode = RectifyMode::FullWave) noexcept {
        const float r = mode == RectifyMode::FullWave ? std::abs(u) : std::max(u, 0.0f);
        return std::min(r, 1.0f);
    }

    // Quantize to `bits` bits (mid-tread, rounded to the nearest step): step = 2 / 2^bits.
    // The integer code is limited to [-M, M - 1] with M = floor(2^(bits-1)), just like real integer PCM (16 bit = -32768..32767).
    // When bits is fractional, M is an integer so the output always lands on integer multiples of step (5.5 bit: M = 22, code = -22..21).
    // For bits < 2, M = 1 and the code is only -1 or 0: the whole positive half-cycle becomes 0, so the output is asymmetric and carries DC (true 1-bit two's complement behaves the same).
    static float quantize(float u, float bits) noexcept {
        const float levels = std::exp2(bits);
        const float step = 2.0f / levels;
        const float maxCode = std::floor(levels * 0.5f);
        const float code = std::clamp(std::floor(u / step + 0.5f), -maxCode, maxCode - 1.0f);
        return code * step;
    }

    // For plotting transfer curves: Rectify uses full-wave, Bitcrush uses 8 bit (S&H has state and cannot be expressed as a static function)
    static float shape(DistortionType type, float u) noexcept {
        switch (type) {
        case DistortionType::HardClip: return hardClip(u);
        case DistortionType::Foldback: return foldback(u);
        case DistortionType::Fuzz: return fuzz(u);
        case DistortionType::Rectify: return rectify(u, RectifyMode::FullWave);
        case DistortionType::Bitcrush: return quantize(u, 8.0f);
        }
        return u;
    }

    // Theoretical SNR of quantizing a full-scale sine to N bits: 6.02·N + 1.76 dB (+ 20·log10(A) when the amplitude is A)
    static double theoreticalSnrDb(double bits, double amplitude = 1.0) noexcept {
        return 6.0206 * bits + 1.7609 + 20.0 * std::log10(amplitude);
    }

    // ---- Processing ---------------------------------------------------------------------------

    // The host occasionally hands in a block larger than the one given to prepare (non-compliant, but it really happens): the internal buffers are all sized by maxBlockSize,
    // so the block is processed in maxBlockSize-sized chunks (the same strategy as Chapter 06 Saturation). FIR, filter, and S&H state carry over between chunks;
    // the only difference is that the drive / output / mix BlockRamps finish their ramp within the first chunk.
    void process(AudioBlock block) noexcept {
        if (numChannels_ <= 0)
            return; // prepare() has not been called yet
        const int total = block.getNumSamples();
        for (int start = 0; start < total; start += maxBlockSize_)
            processChunk(block.getSubBlock(start, std::min(maxBlockSize_, total - start)));
    }

    // Single-sample version: only for situations that "do not need oversampling" (e.g. offline analysis, bitcrush).
    // Note: this version bypasses the oversampler and does no dry/wet delay alignment (latency = 0),
    // does not ramp parameters, and does not crossfade on type changes (it uses the current type directly). Tone changes do apply.
    float processSample(int channel, float x) noexcept {
        updateToneCoefficients(); // just one float comparison; coefficients are recomputed only if the tone changed
        float y = shapeStateful(channel, x * driveGain_.target);
        if (type_ == DistortionType::Fuzz || type_ == DistortionType::Rectify)
            y = dcBlocker_.processSample(channel, y);
        if (!toneBypass_)
            y = tone_.processSample(channel, y);
        return x * (1.0f - mix_) + y * outputGain_.target * mix_;
    }

private:
    void processChunk(AudioBlock block) noexcept {
        const int n = block.getNumSamples();
        const int nch = std::min(block.getNumChannels(), numChannels_);
        if (n <= 0 || nch <= 0)
            return;
        assert(n <= maxBlockSize_);
        block = block.getChannelSubset(0, nch); // channels beyond those given to prepare are left untouched

        updateToneCoefficients();

        // 1. dry copy (delayed by latency samples to align with wet)
        for (int ch = 0; ch < nch; ++ch) {
            const float* x = block.getChannelPointer(ch);
            float* d = dryBuffer_.getWritePointer(ch);
            auto& delay = dryDelay_[static_cast<size_t>(ch)];
            for (int i = 0; i < n; ++i)
                d[i] = delay.process(x[i]);
        }

        // 2. type crossfade state: when not fading, fromType_ == toType_ and the weight w = 1
        //    The first block after prepare() / reset() has no "old sound" to fade from, so the current type is used directly
        //    (the common pattern is to call setType() after prepare(), which should not cost an extra fade).
        if (!processedSinceReset_) {
            snapTypeFade();
            processedSinceReset_ = true;
        } else if (fadeRemaining_ == 0 && type_ != toType_) {
            startTypeFade(type_);
        }
        const bool fading = fadeRemaining_ > 0;
        const float fadeDone = static_cast<float>(fadeLength_ - fadeRemaining_); // how many samples of this fade have elapsed
        const float invFade = 1.0f / static_cast<float>(fadeLength_);
        // New-type weight w(i) for sample i (may be fractional: oversampled samples): linear 0 → 1
        auto fadeWeight = [fading, fadeDone, invFade](float i) noexcept {
            return fading ? std::min(1.0f, (fadeDone + i + 1.0f) * invFade) : 1.0f;
        };

        const float g0 = driveGain_.current;

        // 3a. Bitcrush path (original sample rate): runs regardless of the current type so its state never freezes on stale audio
        {
            const float inc = driveGain_.incrementFor(n);
            for (int ch = 0; ch < nch; ++ch) {
                const float* x = block.getChannelPointer(ch);
                float* c = crushBuffer_.getWritePointer(ch);
                auto& delay = wetDelay_[static_cast<size_t>(ch)];
                for (int i = 0; i < n; ++i) // does not go through the oversampler, so pad the same latency ourselves
                    c[i] = delay.process(crush(ch, x[i] * (g0 + inc * static_cast<float>(i))));
            }
        }

        // 3b. Waveshaper path (HardClip / Foldback / Fuzz / Rectify); the result is written straight back into block.
        //     When the type changes within this path (e.g. HardClip → Foldback), the two curves are crossfaded at the shaper output.
        const bool shaperFading = osFromType_ != osToType_;
        if (oversampler_) {
            // Process at the oversampled rate: the harmonics have more room and do not alias back
            AudioBlock up = oversampler_->upsample(block);
            const int un = up.getNumSamples();
            const float inc = driveGain_.incrementFor(un);
            const float invFactor = 1.0f / static_cast<float>(oversampler_->getFactor());
            for (int ch = 0; ch < nch; ++ch) {
                float* x = up.getChannelPointer(ch);
                for (int i = 0; i < un; ++i) {
                    const float u = x[i] * (g0 + inc * static_cast<float>(i));
                    const float yTo = shapeMemoryless(osToType_, u);
                    x[i] = shaperFading
                               ? lerp(shapeMemoryless(osFromType_, u), yTo, fadeWeight(static_cast<float>(i) * invFactor))
                               : yTo;
                }
            }
            oversampler_->downsample(block);
        } else {
            // No oversampling (setOversamplingFactorLog2(0)): shape directly at the original sample rate
            const float inc = driveGain_.incrementFor(n);
            for (int ch = 0; ch < nch; ++ch) {
                float* x = block.getChannelPointer(ch);
                for (int i = 0; i < n; ++i) {
                    const float fi = static_cast<float>(i);
                    const float u = x[i] * (g0 + inc * fi);
                    const float yTo = shapeMemoryless(osToType_, u);
                    x[i] = shaperFading ? lerp(shapeMemoryless(osFromType_, u), yTo, fadeWeight(fi)) : yTo;
                }
            }
        }
        driveGain_.current = driveGain_.target;

        // 3c. Path selection: cw = the share of the Bitcrush path (moves linearly between the two paths during a fade)
        const float crushFrom = fromType_ == DistortionType::Bitcrush ? 1.0f : 0.0f;
        const float crushTo = toType_ == DistortionType::Bitcrush ? 1.0f : 0.0f;
        if (fading || crushTo > 0.0f) {
            for (int ch = 0; ch < nch; ++ch) {
                float* x = block.getChannelPointer(ch);
                const float* c = crushBuffer_.getReadPointer(ch);
                for (int i = 0; i < n; ++i) {
                    const float cw = lerp(crushFrom, crushTo, fadeWeight(static_cast<float>(i)));
                    x[i] = lerp(x[i], c[i], cw);
                }
            }
        }

        // 3d. DC blocker: asymmetric (Fuzz) and rectify both generate DC.
        //     It runs on every block (so its state never freezes), but only Fuzz / Rectify use its output;
        //     other types are completely unaffected (e.g. the bitcrush SNR measurement needs wet and dry aligned sample by sample).
        const float dcFrom = needsDcBlocker(fromType_) ? 1.0f : 0.0f;
        const float dcTo = needsDcBlocker(toType_) ? 1.0f : 0.0f;
        for (int ch = 0; ch < nch; ++ch) {
            float* x = block.getChannelPointer(ch);
            for (int i = 0; i < n; ++i) {
                const float blocked = dcBlocker_.processSample(ch, x[i]);
                if (fading)
                    x[i] = lerp(x[i], blocked, lerp(dcFrom, dcTo, fadeWeight(static_cast<float>(i))));
                else if (dcTo > 0.0f)
                    x[i] = blocked;
            }
        }
        advanceTypeFade(n);

        // 4. tone (post lowpass)
        if (!toneBypass_) {
            for (int ch = 0; ch < nch; ++ch) {
                float* x = block.getChannelPointer(ch);
                for (int i = 0; i < n; ++i)
                    x[i] = tone_.processSample(ch, x[i]);
            }
        }

        // 5. output gain and dry/wet mix
        const float o0 = outputGain_.current, oInc = outputGain_.incrementFor(n);
        const float m0 = mixRamp_.current, mInc = mixRamp_.incrementFor(n);
        for (int ch = 0; ch < nch; ++ch) {
            float* x = block.getChannelPointer(ch);
            const float* d = dryBuffer_.getReadPointer(ch);
            for (int i = 0; i < n; ++i) {
                const float fi = static_cast<float>(i);
                const float m = m0 + mInc * fi;
                x[i] = d[i] * (1.0f - m) + x[i] * (o0 + oInc * fi) * m;
            }
        }
        outputGain_.current = outputGain_.target;
        mixRamp_.current = mixRamp_.target;
    }

    static bool needsDcBlocker(DistortionType t) noexcept {
        return t == DistortionType::Fuzz || t == DistortionType::Rectify;
    }

    // Same as static shape(), but Rectify uses the current rectifyMode_ (HalfWave must also take effect on the oversampled path)
    float shapeMemoryless(DistortionType t, float u) const noexcept {
        switch (t) {
        case DistortionType::HardClip: return hardClip(u);
        case DistortionType::Foldback: return foldback(u);
        case DistortionType::Fuzz: return fuzz(u);
        case DistortionType::Rectify: return rectify(u, rectifyMode_);
        case DistortionType::Bitcrush: return quantize(u, bitDepth_); // never used: Bitcrush goes through crush()
        }
        return u;
    }

    // Used by processSample(): the stateful bitcrush (sample & hold) and the other curves are dispatched here
    float shapeStateful(int ch, float u) noexcept {
        return type_ == DistortionType::Bitcrush ? crush(ch, u) : shapeMemoryless(type_, u);
    }

    // ---- type crossfade ----
    // fromType_ → toType_: the overall output (which path, and whether the DC blocker is used) fades between these two types.
    // osFromType_ → osToType_: the curves used inside the shaper path. Bitcrush is not on the shaper path,
    // so while Bitcrush is active the shaper path idles on "the last non-Bitcrush type" and its state stays fresh.
    static float lerp(float a, float b, float t) noexcept { return a + (b - a) * t; }

    void snapTypeFade() noexcept {
        fromType_ = toType_ = type_;
        if (type_ != DistortionType::Bitcrush)
            osToType_ = type_;
        osFromType_ = osToType_;
        fadeRemaining_ = 0;
        processedSinceReset_ = false;
    }

    void startTypeFade(DistortionType newType) noexcept {
        fromType_ = toType_;
        toType_ = newType;
        if (newType != DistortionType::Bitcrush) {
            // When switching from Bitcrush, the shaper path's weight starts at 0, so just swap the curve;
            // otherwise crossfade between the old and new curves
            osFromType_ = fromType_ == DistortionType::Bitcrush ? newType : osToType_;
            osToType_ = newType;
        }
        fadeRemaining_ = fadeLength_;
    }

    void advanceTypeFade(int numSamples) noexcept {
        if (fadeRemaining_ == 0)
            return;
        fadeRemaining_ = std::max(0, fadeRemaining_ - numSamples);
        if (fadeRemaining_ == 0) {
            fromType_ = toType_;
            osFromType_ = osToType_;
        }
    }

    // sample & hold: look at the input only once every downsampleFactor_ samples and hold the previous value the rest of the time,
    // then quantize. Effective sample rate = sampleRate / factor; the S&H staircase produces imaging / aliasing.
    float crush(int ch, float u) noexcept {
        const auto i = static_cast<size_t>(ch);
        if (holdCounter_[i] == 0)
            holdValue_[i] = u;
        if (++holdCounter_[i] >= downsampleFactor_)
            holdCounter_[i] = 0;
        return quantize(holdValue_[i], bitDepth_);
    }

    void updateToneCoefficients() noexcept {
        if (toneHz_ == toneHzApplied_)
            return;
        toneHzApplied_ = toneHz_;
        const bool wasBypassed = toneBypass_;
        toneBypass_ = toneHz_ >= kToneBypassHz;
        if (!toneBypass_ && wasBypassed)
            tone_.reset(); // the biquad was not running during bypass and its state is stale: clear it so old audio is not spat out
        if (!toneBypass_)
            tone_.setCoefficients(BiquadCoefficients::makeLowpass(sampleRate_, static_cast<double>(toneHz_), 0.7071));
    }

    DistortionType type_ = DistortionType::HardClip;
    RectifyMode rectifyMode_ = RectifyMode::FullWave;
    float driveDb_ = 0.0f;
    float toneHz_ = kToneBypassHz;
    float toneHzApplied_ = -1.0f;
    bool toneBypass_ = true;
    float mix_ = 1.0f;
    float outputGainDb_ = 0.0f;
    float bitDepth_ = 8.0f;
    int downsampleFactor_ = 1;
    int osLog2_ = 2;
    bool prepared_ = false; // whether prepare() has been called (used by getOversamplingFactor())

    DistortionType fromType_ = DistortionType::HardClip, toType_ = DistortionType::HardClip;
    DistortionType osFromType_ = DistortionType::HardClip, osToType_ = DistortionType::HardClip;
    int fadeLength_ = 1;
    int fadeRemaining_ = 0;
    bool processedSinceReset_ = false;

    double sampleRate_ = 48000.0;
    int maxBlockSize_ = 0;
    int numChannels_ = 0;
    int latency_ = 0;

    std::unique_ptr<Oversampler> oversampler_;
    OnePole dcBlocker_;
    Biquad tone_;
    std::vector<detail::FixedDelay> dryDelay_, wetDelay_;
    AudioBuffer dryBuffer_, crushBuffer_;
    std::vector<float> holdValue_;
    std::vector<int> holdCounter_;
    detail::BlockRamp driveGain_, outputGain_, mixRamp_;
};

} // namespace dawlab::dsp
