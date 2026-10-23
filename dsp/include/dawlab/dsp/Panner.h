#pragma once
// Panner: converts a pan position (-1 = hard left, 0 = center, +1 = hard right) into left and right gains, then multiplies them into the left and right channels.
//
// Let theta = (pan + 1) / 2 in [0, 1] (0 = hard left, 0.5 = center, 1 = hard right). Three pan laws:
//
//   Linear        : gL = 1 - theta               gR = theta
//                   center 0.5 = -6.02 dB; gL + gR = 1 (constant amplitude sum); gL^2 + gR^2 at center = 0.5 (power drops by 3 dB)
//   ConstantPower : gL = cos(theta*pi/2)         gR = sin(theta*pi/2)
//                   (in the code gL is written as sin((1-theta)*pi/2), which is mathematically identical but gives exactly 0 on the opposite side at pan = +/-1)
//                   center 1/sqrt(2) = -3.01 dB; gL^2 + gR^2 = 1 always (constant power sum)
//   Minus4_5dB    : gL = sqrt(Linear_L * ConstantPower_L), and likewise for gR (the geometric mean of the two)
//                   center sqrt(0.5 * 0.7071) = 0.5946 = -4.52 dB, a compromise between the two laws
//
// Stereo versus mono (make sure you understand the difference):
// - process(block) multiplies gL into channel 0 and gR into channel 1.
//   * If the left and right content of the block is identical (a mono signal copied to two channels), this is a true mono pan: the image moves between left and right.
//   * If the block is genuinely stereo (left and right differ), this is actually a balance: one side is simply made quieter,
//     and the content of the right channel is not "moved" to the left. This is how the stereo tracks of most DAWs behave.
// - processMonoToStereo(mono, stereoOut) is the "mono in -> stereo out" pan.
// - When the block has only 1 channel, process() does nothing (there is no second channel to pan).
//
// The left and right gains are each linearly smoothed by a SmoothedValue (50 ms by default), so sweeping the pan produces no zipper noise.

#include <dawlab/AudioBuffer.h>
#include <dawlab/DspMath.h>
#include <dawlab/dsp/SmoothedValue.h>

#include <algorithm>
#include <cmath>

namespace dawlab::dsp {

enum class PanLaw { Linear, ConstantPower, Minus4_5dB };

inline const char* panLawName(PanLaw law) noexcept {
    switch (law) {
    case PanLaw::Linear: return "Linear";
    case PanLaw::ConstantPower: return "ConstantPower";
    case PanLaw::Minus4_5dB: return "Minus4_5dB";
    }
    return "?";
}

class Panner {
public:
    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        (void)maxBlockSize;
        (void)numChannels;
        sampleRate_ = sampleRate;
        left_.reset(sampleRate, rampSeconds_);
        right_.reset(sampleRate, rampSeconds_);
        snapToTarget();
    }

    // Snaps the left and right gains straight to the target (no ramp)
    void reset() noexcept { snapToTarget(); }

    // pan in [-1, 1]: -1 hard left, 0 center, +1 hard right (out-of-range values are clamped)
    // Parameter setters must be called between blocks, from the same thread that calls process() (the components in this chapter are not thread-safe;
    // for passing parameters across threads see chapters 13 and 14). NaN / inf are not valid positions and are ignored (the previous value is kept).
    void setPan(float pan) noexcept {
        if (!std::isfinite(pan))
            return;
        pan_ = std::clamp(pan, -1.0f, 1.0f);
        updateTargets();
    }
    float getPan() const noexcept { return pan_; }

    void setPanLaw(PanLaw law) noexcept {
        law_ = law;
        updateTargets();
    }
    PanLaw getPanLaw() const noexcept { return law_; }

    void setRampSeconds(double seconds) noexcept {
        rampSeconds_ = std::max(0.0, seconds);
        if (sampleRate_ > 0.0) {
            const int n = static_cast<int>(std::lround(rampSeconds_ * sampleRate_));
            left_.setRampLength(n);
            right_.setRampLength(n);
        }
    }

    // The left and right gains currently in use (during a ramp they lie between the old and new values)
    float getCurrentLeftGain() const noexcept { return left_.getCurrentValue(); }
    float getCurrentRightGain() const noexcept { return right_.getCurrentValue(); }
    bool isSmoothing() const noexcept { return left_.isSmoothing() || right_.isSmoothing(); }

    // Pure function: computes the left and right gains (linear factors) from pan and law. The pan law table in the example calls it directly.
    static void computeGains(float pan, PanLaw law, float& left, float& right) noexcept {
        // NaN / inf are treated as center, which guarantees the output gains are always finite
        const float p = std::isfinite(pan) ? std::clamp(pan, -1.0f, 1.0f) : 0.0f;
        const float theta = 0.5f * (p + 1.0f); // 0 = hard left, 0.5 = center, 1 = hard right
        const float linL = 1.0f - theta;
        const float linR = theta;
        // cos(theta*pi/2) is written as sin((1-theta)*pi/2): float(pi)/2 is slightly larger than the true pi/2,
        // so computing cos(float(pi)/2) directly gives -4.37e-8 (negative, not 0), leaving an inverted-phase residue in the left channel at hard right.
        // With sin, both ends are sin(0) = 0 (exact), and the result is symmetric between left and right and never negative.
        const float halfPi = static_cast<float>(kPi) * 0.5f;
        const float cpL = std::sin((1.0f - theta) * halfPi);
        const float cpR = std::sin(theta * halfPi);
        switch (law) {
        case PanLaw::Linear:
            left = linL;
            right = linR;
            break;
        case PanLaw::ConstantPower:
            left = cpL;
            right = cpR;
            break;
        case PanLaw::Minus4_5dB:
            // Geometric mean: the dB value is exactly the average of the two laws (the average of -6.02 and -3.01 = -4.52 dB)
            left = std::sqrt(linL * cpL);
            right = std::sqrt(linR * cpR);
            break;
        }
    }

    // In place: channel 0 times gL, channel 1 times gR (identical channels = mono pan; different channels = balance)
    void process(AudioBlock block) noexcept {
        if (block.getNumChannels() < 2)
            return;
        left_.applyGain(block.getChannelPointer(0), block.getNumSamples());
        right_.applyGain(block.getChannelPointer(1), block.getNumSamples());
    }

    // Mono in -> stereo out: stereoOut[0] = mono * gL, stereoOut[1] = mono * gR
    // mono may be the same memory as channel 0 of stereoOut (the right channel is computed before the left).
    void processMonoToStereo(const float* mono, AudioBlock stereoOut) noexcept {
        if (stereoOut.getNumChannels() < 2)
            return;
        const int n = stereoOut.getNumSamples();
        float* outL = stereoOut.getChannelPointer(0);
        float* outR = stereoOut.getChannelPointer(1);
        if (!left_.isSmoothing() && !right_.isSmoothing()) {
            const float gL = left_.getTargetValue();
            const float gR = right_.getTargetValue();
            for (int i = 0; i < n; ++i) {
                const float x = mono[i];
                outR[i] = x * gR;
                outL[i] = x * gL;
            }
            return;
        }
        for (int i = 0; i < n; ++i) {
            const float x = mono[i];
            const float gL = left_.getNextValue();
            const float gR = right_.getNextValue();
            outR[i] = x * gR;
            outL[i] = x * gL;
        }
    }

private:
    void updateTargets() noexcept {
        float l = 0.0f, r = 0.0f;
        computeGains(pan_, law_, l, r);
        left_.setTargetValue(l);
        right_.setTargetValue(r);
    }

    void snapToTarget() noexcept {
        float l = 0.0f, r = 0.0f;
        computeGains(pan_, law_, l, r);
        left_.setCurrentAndTargetValue(l);
        right_.setCurrentAndTargetValue(r);
    }

    double sampleRate_ = 0.0;
    double rampSeconds_ = 0.05;
    float pan_ = 0.0f;
    PanLaw law_ = PanLaw::ConstantPower;
    SmoothedValue left_{0.70710678f};
    SmoothedValue right_{0.70710678f};
};

} // namespace dawlab::dsp
