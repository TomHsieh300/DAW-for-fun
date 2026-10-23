#pragma once
// TransientShaper.h -- transient shaper (Chapter 08)
//
// A dynamics processor that ignores absolute loudness and looks only at "how fast the loudness changes": it splits each
// note into an attack (the onset) and a sustain, and boosts or cuts each of them independently.
//
// Detection: the same signal is fed to two EnvelopeFollowers
//   fast: attack 0.5 ms / release 30 ms   -> follows the signal's peak almost exactly
//   slow: attack 10 ms  / release 150 ms  -> a "average loudness" that lags a beat behind
//   d = 20*log10(fast / slow)
//     At the instant of the onset: fast has already shot up to the peak while slow is still climbing -> d > 0
//     Sustain / decay: fast falls with the signal while slow is still holding up high -> d < 0
//
// Gain mapping (dB):
//   d > 0: gainDb = attackDb  * min(d / 6 dB, 1)
//   d < 0: gainDb = sustainDb * min(-d / 6 dB, 1)
//   (once d exceeds +/-6 dB the full attackDb / sustainDb is applied; a slow envelope below -80 dBFS is treated as
//    silence and is not processed)
// Finally gainDb is smoothed with a 0.5 ms one-pole to avoid zipper noise caused by envelope ripple.
//
// Attack +12 dB makes drums hit harder and increases the crest factor; attack -12 dB makes drums rounder and more
// distant; sustain -12 dB shortens the tails (like shrinking the room), and sustain +12 dB lengthens them.

#include <dawlab/AudioBuffer.h>
#include <dawlab/DspMath.h>
#include <dawlab/dsp/EnvelopeFollower.h>

#include <algorithm>
#include <atomic>
#include <cmath>

namespace dawlab::dsp {

class TransientShaper {
public:
    static constexpr float kFastAttackMs = 0.5f;
    static constexpr float kFastReleaseMs = 30.0f;
    static constexpr float kSlowAttackMs = 10.0f;
    static constexpr float kSlowReleaseMs = 150.0f;
    static constexpr float kFullScaleDb = 6.0f;  // once |d| reaches this value, 100% of attackDb / sustainDb is applied
    static constexpr float kFloorDb = -80.0f;    // a slow envelope below this is treated as silence
    static constexpr float kGainSmoothMs = 0.5f;

    TransientShaper() = default;
    // A std::atomic member makes the class non-copyable by default; define these by hand so it can be placed in a
    // std::vector (the 8-track DAW in Chapter 14)
    TransientShaper(const TransientShaper& other) { copyFrom(other); }
    TransientShaper& operator=(const TransientShaper& other) {
        if (this != &other)
            copyFrom(other);
        return *this;
    }

    // ---- Lifecycle -------------------------------------------------------
    void prepare(double sampleRate, int /*maxBlockSize*/, int numChannels) {
        sampleRate_ = sampleRate;
        numChannels_ = numChannels;
        fast_.prepare(sampleRate);
        fast_.setMode(DetectorMode::Peak);
        fast_.setAttackMs(kFastAttackMs);
        fast_.setReleaseMs(kFastReleaseMs);
        slow_.prepare(sampleRate);
        slow_.setMode(DetectorMode::Peak);
        slow_.setAttackMs(kSlowAttackMs);
        slow_.setReleaseMs(kSlowReleaseMs);
        smoothCoef_ = static_cast<float>(std::exp(-1.0 / (kGainSmoothMs * 0.001 * sampleRate)));
        reset();
    }

    void reset() noexcept {
        fast_.reset();
        slow_.reset();
        gainDb_ = 0.0f;
        gain_ = 1.0f;
        lastGainDb_.store(0.0f, std::memory_order_relaxed);
    }

    // ---- Parameters -----------------------------------------------------------
    // -24..+24 dB: positive values boost the transient, negative values reduce it
    void setAttackDb(float db) noexcept { attackDb_ = std::clamp(db, -24.0f, 24.0f); }
    void setSustainDb(float db) noexcept { sustainDb_ = std::clamp(db, -24.0f, 24.0f); }
    float getAttackDb() const noexcept { return attackDb_; }
    float getSustainDb() const noexcept { return sustainDb_; }
    int getLatencySamples() const noexcept { return 0; }

    // The gain currently applied (dB, may be positive or negative; updated at the end of each block; thread-safe)
    float getLastGainDb() const noexcept { return lastGainDb_.load(std::memory_order_relaxed); }

    // ---- Processing -----------------------------------------------------------
    // Core: feed in the detection sample (max|x| across channels) and return the linear gain
    float nextGain(float detectorInput) noexcept {
        const float fast = fast_.processSample(detectorInput);
        const float slow = slow_.processSample(detectorInput);
        const float slowDb = gainToDb(slow);
        float target = 0.0f;
        if (slowDb > kFloorDb) {
            const float d = gainToDb(fast) - slowDb; // difference between the fast and slow envelopes (dB)
            if (d > 0.0f)
                target = attackDb_ * std::min(d / kFullScaleDb, 1.0f);
            else
                target = sustainDb_ * std::min(-d / kFullScaleDb, 1.0f);
        }
        gainDb_ = target + smoothCoef_ * (gainDb_ - target);
        gain_ = dbToGain(gainDb_);
        return gain_;
    }

    void process(AudioBlock block) noexcept {
        const int n = block.getNumSamples();
        const int nch = block.getNumChannels();
        for (int i = 0; i < n; ++i) {
            float det = 0.0f;
            for (int ch = 0; ch < nch; ++ch)
                det = std::max(det, std::abs(block.getChannelPointer(ch)[i]));
            const float g = nextGain(det);
            for (int ch = 0; ch < nch; ++ch)
                block.getChannelPointer(ch)[i] *= g;
        }
        lastGainDb_.store(gainDb_, std::memory_order_relaxed);
    }

private:
    void copyFrom(const TransientShaper& o) {
        sampleRate_ = o.sampleRate_;
        numChannels_ = o.numChannels_;
        attackDb_ = o.attackDb_;
        sustainDb_ = o.sustainDb_;
        fast_ = o.fast_;
        slow_ = o.slow_;
        smoothCoef_ = o.smoothCoef_;
        gainDb_ = o.gainDb_;
        gain_ = o.gain_;
        lastGainDb_.store(o.lastGainDb_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }

    double sampleRate_ = 48000.0;
    int numChannels_ = 2;
    float attackDb_ = 0.0f;
    float sustainDb_ = 0.0f;

    EnvelopeFollower fast_, slow_;
    float smoothCoef_ = 0.0f;
    float gainDb_ = 0.0f;
    float gain_ = 1.0f;
    std::atomic<float> lastGainDb_{0.0f};
};

} // namespace dawlab::dsp
