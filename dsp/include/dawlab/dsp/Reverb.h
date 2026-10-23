#pragma once
// Chapter 12: Algorithmic reverb -- 8-line FDN (Feedback Delay Network)
//
// Architecture (per sample):
//
//   in L/R ──► pre-delay ──► input diffusion (4 allpass stages) ──┐
//                                                                  ▼
//        ┌─────────────────────────────────────────────┐   + ──► delay_i ──► v_i ─┐
//        │   feedback: u = A·v  (A = Householder matrix, lossless)                │
//        │   u_i → damping lowpass_i → × g_i (decay) → written back to delay_i    │
//        └───────────────────────────────────────────────────────────────────────┘
//   out L = Σ (±) v_even , out R = Σ (±) v_odd  ──► width (mid/side) ──► mix with dry
//
// Formulas:
//   g_i = 10^(-3 · L_i / (RT60 · fs))            attenuation of each line per trip around the loop (same as a comb)
//   A   = I - (2/N)·1·1ᵀ                          Householder reflection matrix, Aᵀ A = I (energy preserving)
//   damping: a one-pole lowpass (chapter 02's OnePole) inside the loop → highs lose a bit more each trip → shorter RT60 for the highs
//
// Another common architecture, Freeverb (8 parallel combs + 4 series allpasses), is described in the README;
// its components (CombFilter / AllpassDiffuser) are in ReverbBuildingBlocks.h.
//
// Uses chapter 09's FractionalDelayLine (the delay lines, which allow smooth size changes and modulation) and
// chapter 02's OnePole (damping).

#include <dawlab/AudioBuffer.h>
#include <dawlab/DspMath.h>
#include <dawlab/dsp/FractionalDelayLine.h>
#include <dawlab/dsp/OnePole.h>
#include <dawlab/dsp/ReverbBuildingBlocks.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace dawlab::dsp {

class Reverb {
public:
    static constexpr int kNumLines = 8;         // number of delay lines in the FDN
    static constexpr int kNumInputDiffusers = 4; // number of input allpass stages per channel
    static constexpr float kMaxPreDelayMs = 250.0f;
    static constexpr float kMaxModulationMs = 2.0f; // maximum depth of the built-in modulation (exercise 4 goes deeper)

    // ---- Standard interface ------------------------------------------------------
    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        (void)maxBlockSize;
        sampleRate_ = sampleRate;
        numChannels_ = std::clamp(numChannels, 1, 2);

        // Delay lines: longest = length at maximum size + headroom for rounding up to a prime + modulation headroom + interpolation headroom
        maxModulationSamples_ = msToSamples(kMaxModulationMs, sampleRate);
        maxLineDelay_ = msToSamples(kBaseDelaysMs[kNumLines - 1] * sizeToScale(1.0f), sampleRate) + 64 + maxModulationSamples_ + 4;
        lines_.prepare(maxLineDelay_, kNumLines);
        preDelay_.prepare(msToSamples(kMaxPreDelayMs, sampleRate) + 4, 2);
        damping_.prepare(sampleRate, 1, kNumLines);
        damping_.setType(OnePole::Type::Lowpass);

        for (int ch = 0; ch < 2; ++ch)
            for (int k = 0; k < kNumInputDiffusers; ++k) {
                AllpassDiffuser& ap = diffusers_[static_cast<size_t>(ch)][static_cast<size_t>(k)];
                // The left and right diffuser lengths differ slightly (x1.0 / x1.07), which makes the stereo image wider
                const double ms = kDiffuserMs[k] * (ch == 0 ? 1.0 : 1.07);
                ap.prepare(msToSamples(ms, sampleRate) + 1);
                ap.setDelaySamples(msToSamples(ms, sampleRate));
                ap.setGain(0.5f);
            }

        // Coefficient for parameter smoothing (about a 30 ms time constant)
        smoothCoef_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.03 * sampleRate)));
        lfoIncrement_ = static_cast<float>(kTwoPi * modulationRateHz_ / sampleRate);

        // Recompute every parameter that depends on the sample rate (a setter may have been called before prepare)
        updateDelayTargets();
        updateDecayGains();
        setDampingHz(dampingHz_);
        setPreDelayMs(preDelayMs_);
        setModulationDepthMs(modulationDepthMs_);
        reset();
    }

    void reset() noexcept {
        lines_.reset();
        preDelay_.reset();
        damping_.reset();
        for (auto& chDiffusers : diffusers_)
            for (auto& ap : chDiffusers)
                ap.reset();
        lineOut_.fill(0.0f);
        lfoPhase_ = 0.0f;
        // Parameters that are being smoothed jump straight to their targets: after a reset no sound is left over, so the jump cannot click,
        // and this also avoids the effect where, if parameters are set after prepare, the first stretch of output has
        // mix / size / pre-delay slowly gliding in.
        currentDelay_ = targetDelay_;
        currentPreDelay_ = targetPreDelay_;
        currentMix_ = mix_;
        currentWidth_ = width_;
        modulationScale_ = freeze_ ? 0.0f : 1.0f;
    }

    // ---- Parameters (contract) -----------------------------------------------------
    // Every float setter rejects NaN first: std::clamp(NaN, lo, hi) returns the NaN unchanged, so a single NaN parameter
    // (e.g. a faulty host automation) would turn the output permanently into NaN, and even reset() could not recover it.
    // On receiving a NaN the previous valid value is kept.

    // RT60 (seconds): the time for the reverb to decay by 60 dB. 0.1 .. 60
    void setDecaySeconds(float seconds) noexcept {
        if (std::isnan(seconds))
            return;
        decaySeconds_ = std::clamp(seconds, 0.1f, 60.0f);
        updateDecayGains();
    }
    // Gap between the direct sound and the start of the reverb (0 .. 250 ms); changes are smoothed
    void setPreDelayMs(float ms) noexcept {
        if (std::isnan(ms))
            return;
        preDelayMs_ = std::clamp(ms, 0.0f, kMaxPreDelayMs);
        targetPreDelay_ = static_cast<float>(preDelayMs_ * 0.001 * sampleRate_);
    }
    // Cutoff of the damping lowpass; >= 20000 means no damping
    void setDampingHz(float hz) noexcept {
        if (std::isnan(hz))
            return;
        dampingHz_ = std::clamp(hz, 200.0f, 20000.0f);
        dampingBypassed_ = dampingHz_ >= 20000.0f;
        damping_.setCutoff(std::min(dampingHz_, static_cast<float>(sampleRate_ * 0.45)));
    }
    // 0..1: delay-line lengths x(0.5 .. 1.5); larger sounds like a bigger space (higher modal density, sparser early part)
    void setSize(float size) noexcept {
        if (std::isnan(size))
            return;
        size_ = std::clamp(size, 0.0f, 1.0f);
        updateDelayTargets();
        updateDecayGains();
    }
    // 0 = mono (L=R), 1 = full stereo
    void setWidth(float width) noexcept {
        if (std::isnan(width))
            return;
        width_ = std::clamp(width, 0.0f, 1.0f);
    }
    // 0 = all dry, 1 = all wet (linear mix)
    void setMix(float mix) noexcept {
        if (std::isnan(mix))
            return;
        mix_ = std::clamp(mix, 0.0f, 1.0f);
    }
    // freeze: g_i = 1, damping off, no new signal is fed in, modulation fades out (about 30 ms) -> the tail is extended indefinitely.
    // Modulation must stop: linear interpolation at a fractional position is effectively a lowpass that loses a little on every trip around the loop,
    // so a frozen tail would slowly get darker and quieter; with the read position back at an integer length the loop is strictly lossless.
    void setFreeze(bool shouldFreeze) noexcept {
        freeze_ = shouldFreeze;
        updateDecayGains();
    }

    // ---- Extra parameters ----------------------------------------------------------
    // Built-in delay-line modulation (depth in ms, rate in Hz); the default 0 = off. Exercise 4 discusses its effect on metallic ringing.
    void setModulationDepthMs(float ms) noexcept {
        if (std::isnan(ms))
            return;
        modulationDepthMs_ = std::clamp(ms, 0.0f, kMaxModulationMs);
        modulationDepth_ = static_cast<float>(modulationDepthMs_ * 0.001 * sampleRate_);
    }
    void setModulationRateHz(float hz) noexcept {
        if (std::isnan(hz))
            return;
        modulationRateHz_ = std::clamp(hz, 0.01f, 5.0f);
        lfoIncrement_ = static_cast<float>(kTwoPi * modulationRateHz_ / sampleRate_);
    }

    float getDecaySeconds() const noexcept { return decaySeconds_; }
    float getPreDelayMs() const noexcept { return preDelayMs_; }
    float getDampingHz() const noexcept { return dampingHz_; }
    float getSize() const noexcept { return size_; }
    float getWidth() const noexcept { return width_; }
    float getMix() const noexcept { return mix_; }
    bool isFrozen() const noexcept { return freeze_; }
    // Current length (in samples) of delay line i, for the examples to print tables
    float getLineDelaySamples(int i) const noexcept { return targetDelay_[static_cast<size_t>(i)]; }
    // Decay gain g_i of line i
    float getLineGain(int i) const noexcept { return lineGain_[static_cast<size_t>(i)]; }

    // ---- Processing ----------------------------------------------------------------
    // In place; supports 1 or 2 channels (with mono input the output is also mono: the reverb of the left channel is used)
    void process(AudioBlock block) noexcept {
        const int nch = std::min(block.getNumChannels(), 2);
        const int n = block.getNumSamples();
        if (nch == 0)
            return;
        float* left = block.getChannelPointer(0);
        float* right = nch > 1 ? block.getChannelPointer(1) : nullptr;

        for (int i = 0; i < n; ++i) {
            const float dryL = left[i];
            const float dryR = right ? right[i] : dryL;
            float wetL, wetR;
            processSampleStereo(dryL, dryR, wetL, wetR);

            // Smooth mix / width (avoids zipper noise)
            currentMix_ += smoothCoef_ * (mix_ - currentMix_);
            currentWidth_ += smoothCoef_ * (width_ - currentWidth_);

            // width: mid/side
            const float mid = 0.5f * (wetL + wetR);
            const float side = 0.5f * (wetL - wetR) * currentWidth_;
            wetL = mid + side;
            wetR = mid - side;

            left[i] = dryL * (1.0f - currentMix_) + wetL * currentMix_;
            if (right)
                right[i] = dryR * (1.0f - currentMix_) + wetR * currentMix_;
        }
    }

    // Computes only the wet signal (no dry, no width/mix), for anyone who wants to build their own send/return
    void processSampleStereo(float inL, float inR, float& wetL, float& wetR) noexcept {
        // 1. Pre-delay (a fractional delay whose changes are smoothed)
        currentPreDelay_ += smoothCoef_ * (targetPreDelay_ - currentPreDelay_);
        preDelay_.push(0, inL);
        preDelay_.push(1, inR);
        float dL = preDelay_.readLinear(0, currentPreDelay_);
        float dR = preDelay_.readLinear(1, currentPreDelay_);

        // 2. Input diffusion: 4 series allpass stages that scatter a single pulse into a small cluster
        for (auto& ap : diffusers_[0])
            dL = ap.processSample(dL);
        for (auto& ap : diffusers_[1])
            dR = ap.processSample(dR);

        // While frozen, no new signal is fed in
        const float inputGain = freeze_ ? 0.0f : kInputGain;
        dL *= inputGain;
        dR *= inputGain;

        // 3. Read the output v_i of every delay line (including modulation and smoothed length changes)
        lfoPhase_ += lfoIncrement_;
        if (lfoPhase_ >= static_cast<float>(kTwoPi))
            lfoPhase_ -= static_cast<float>(kTwoPi);
        // When frozen, the modulation fades smoothly to 0 (zeroing it directly would make the read position jump -> click) and fades back in when the freeze is released.
        // Near the end point it snaps straight to 0 / 1: exponential smoothing never reaches the end point, and a small leftover fractional position would still cause loss.
        modulationScale_ += smoothCoef_ * ((freeze_ ? 0.0f : 1.0f) - modulationScale_);
        if (freeze_ && modulationScale_ < 1.0e-4f)
            modulationScale_ = 0.0f;
        else if (!freeze_ && modulationScale_ > 0.9999f)
            modulationScale_ = 1.0f;
        const float depth = modulationDepth_ * modulationScale_;
        float sum = 0.0f;
        for (int k = 0; k < kNumLines; ++k) {
            const size_t i = static_cast<size_t>(k);
            // Glide smoothly toward the target length. Exponential smoothing in float "gets stuck" at lengths of a few thousand samples:
            // once the gap is below about 0.1-0.2 sample, the increment smoothCoef_*diff is smaller than half a ulp, so adding it changes nothing
            // and the read position stays on a fractional value forever
            // (linear interpolation = lowpass -> the loop is no longer lossless when frozen). So when the addition has no effect, snap straight to the target.
            const float nextDelay = currentDelay_[i] + smoothCoef_ * (targetDelay_[i] - currentDelay_[i]);
            currentDelay_[i] = nextDelay == currentDelay_[i] ? targetDelay_[i] : nextDelay;
            // Each line uses an LFO with a different phase (k*45 degrees), so the lines do not all "wobble" together
            const float lfo = depth > 0.0f ? depth * std::sin(lfoPhase_ + static_cast<float>(k) * 0.7853982f) : 0.0f;
            // This is "read first, write after": the last push was u[n-1], and readLinear(d) returns u[n-1-d],
            // so we must read d = L - 1 to get v[n] = u[n - L] (the loop length is then exactly L, which makes the formula for g_i exact).
            const float v = lines_.readLinear(k, currentDelay_[i] - 1.0f + lfo);
            lineOut_[i] = v;
            sum += v;
        }

        // 4. Householder feedback: u_i = v_i - (2/N)·Σ v_j, then damping + decay gain, then add the input and write back
        const float scale = 2.0f / static_cast<float>(kNumLines);
        for (int k = 0; k < kNumLines; ++k) {
            const size_t i = static_cast<size_t>(k);
            float u = lineOut_[i] - scale * sum;
            if (!dampingBypassed_ && !freeze_)
                u = damping_.processSample(k, u);
            u *= lineGain_[i];
            u += (k % 2 == 0) ? dL : dR; // even lines take the left input, odd lines the right
            lines_.push(k, reverb_detail::flushDenormal(u));
        }

        // 5. Output: take 4 lines each for left and right with alternating signs, which decorrelates L/R
        wetL = lineOut_[0] - lineOut_[2] + lineOut_[4] - lineOut_[6];
        wetR = lineOut_[1] - lineOut_[3] + lineOut_[5] - lineOut_[7];
    }

private:
    // size -> delay-length scale factor 0.5 .. 1.5
    static float sizeToScale(float size) noexcept { return 0.5f + size; }

    void updateDelayTargets() noexcept {
        const float scale = sizeToScale(size_);
        for (int k = 0; k < kNumLines; ++k) {
            const size_t i = static_cast<size_t>(k);
            // Convert to samples and take the next prime: this makes the line lengths mutually coprime (README, "Why coprime")
            int samples = nextPrime(msToSamples(kBaseDelaysMs[k] * scale, sampleRate_));
            if (maxLineDelay_ > 0) // the upper limit is only known after prepare: leave room for modulation and interpolation
                samples = std::min(samples, maxLineDelay_ - maxModulationSamples_ - 4);
            targetDelay_[i] = static_cast<float>(samples);
        }
    }

    // g_i = 10^(-3·L_i / (RT60·fs)); g_i = 1 while frozen
    void updateDecayGains() noexcept {
        for (int k = 0; k < kNumLines; ++k) {
            const size_t i = static_cast<size_t>(k);
            lineGain_[i] = freeze_ ? 1.0f
                                   : static_cast<float>(std::pow(10.0, -3.0 * targetDelay_[i] / (decaySeconds_ * sampleRate_)));
        }
    }

    // Base lengths of the 8 lines (ms, at size = 0.5): spread roughly evenly over 30 .. 75 ms and not in integer ratios
    static constexpr std::array<double, kNumLines> kBaseDelaysMs = {29.7, 37.1, 41.1, 43.7, 53.3, 59.9, 67.3, 73.1};
    // Input diffuser lengths (ms): taken from the input diffusion settings of Dattorro 1997 (converted to ms)
    static constexpr std::array<double, kNumInputDiffusers> kDiffuserMs = {4.77, 3.60, 12.73, 9.31};
    static constexpr float kInputGain = 0.35f;

    double sampleRate_ = 48000.0;
    int numChannels_ = 2;
    int maxLineDelay_ = 0;
    int maxModulationSamples_ = 0;

    FractionalDelayLine lines_;    // kNumLines delay lines
    FractionalDelayLine preDelay_; // 2 lines (L/R)
    OnePole damping_;              // lowpass with kNumLines channels
    std::array<std::array<AllpassDiffuser, kNumInputDiffusers>, 2> diffusers_{};

    std::array<float, kNumLines> targetDelay_{};
    std::array<float, kNumLines> currentDelay_{};
    std::array<float, kNumLines> lineGain_{};
    std::array<float, kNumLines> lineOut_{};

    float decaySeconds_ = 2.0f;
    float preDelayMs_ = 0.0f;
    float dampingHz_ = 6000.0f;
    bool dampingBypassed_ = false;
    float size_ = 0.5f;
    float width_ = 1.0f;
    float mix_ = 0.3f;
    bool freeze_ = false;

    float targetPreDelay_ = 0.0f, currentPreDelay_ = 0.0f;
    float currentMix_ = 0.3f, currentWidth_ = 1.0f;
    float smoothCoef_ = 0.001f;

    float modulationDepthMs_ = 0.0f;
    float modulationDepth_ = 0.0f; // samples
    float modulationRateHz_ = 0.5f;
    float lfoPhase_ = 0.0f, lfoIncrement_ = 0.0f;
    float modulationScale_ = 1.0f; // 1 = modulation fully on; smoothly reduced to 0 while frozen
};

} // namespace dawlab::dsp
