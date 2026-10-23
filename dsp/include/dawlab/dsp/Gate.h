#pragma once
// Gate.h -- noise gate (Chapter 08)
//
// What a gate does: it measures the signal level (the envelope); above the threshold it "opens" (gain = 0 dB),
// and below the threshold it "closes" (gain = range, for example -80 dB). To keep the gate from flickering around
// the threshold (chattering), opening and closing use different thresholds (hysteresis), the gate waits for a while
// (hold) before closing, and the opening and closing themselves are ramps (attack / release).
//
//   detection signal --> EnvelopeFollower --> dB --> state machine --> gain(dB) --> multiplied onto all channels
//                        (peak, 0.1 ms / 20 ms)      Closed / Attack / Open / Hold / Release
//
// State machine (one step per sample):
//   Closed  - env >= T_open ------------> Attack
//   Attack  - gainDb reaches 0 ---------> Open
//   Open    - env < T_close ------------> Hold (goes straight to Release when hold = 0)
//   Hold    - env >= T_open ------------> Open
//           - counter reaches zero -----> Release
//   Release - env >= T_open ------------> Attack (climbs from the current gain, no jump)
//           - gainDb falls to range ----> Closed
//   If the range is changed while in the Closed state (for example by automation), the gain glides to the new range
//   along a ramp, so no step is produced.
//
// The gain ramps at a "constant rate" in the dB domain: the attack time = the time to climb from range to 0 dB;
// the release time = the time to fall from 0 dB to range. A constant rate in dB is an exponential change in the
// linear domain, which sounds like a natural fade.
//
// Multiple channels are always stereo-linked: detection uses max|x| over all channels and the same gain is applied to
// every channel; otherwise the left and right channels would open and close independently and the stereo image
// would jump around.

#include <dawlab/AudioBuffer.h>
#include <dawlab/DspMath.h>
#include <dawlab/dsp/EnvelopeFollower.h>

#include <algorithm>
#include <atomic>
#include <cmath>

namespace dawlab::dsp {

enum class GateState { Closed, Attack, Open, Hold, Release };

inline const char* gateStateName(GateState s) noexcept {
    switch (s) {
    case GateState::Closed: return "Closed";
    case GateState::Attack: return "Attack";
    case GateState::Open: return "Open";
    case GateState::Hold: return "Hold";
    case GateState::Release: return "Release";
    }
    return "?";
}

class Gate {
public:
    // Attack of the (peak) detector: 0.1 ms, so that it catches the first peak of a transient
    static constexpr float kDetectorAttackMs = 0.1f;
    // Minimum glide rate (dB/sample) when the range changes in the Closed state: 0.01 dB/sample = 480 dB/s @ 48 kHz
    static constexpr float kMinRangeGlideDbPerSample = 0.01f;

    Gate() = default;
    // A std::atomic member makes the class non-copyable by default; define these by hand so it can be placed in a
    // std::vector (the 8-track DAW in Chapter 14)
    Gate(const Gate& other) { copyFrom(other); }
    Gate& operator=(const Gate& other) {
        if (this != &other)
            copyFrom(other);
        return *this;
    }

    // ---- Lifecycle -------------------------------------------------------
    void prepare(double sampleRate, int /*maxBlockSize*/, int numChannels) {
        sampleRate_ = sampleRate;
        numChannels_ = numChannels;
        detector_.prepare(sampleRate);
        detector_.setMode(DetectorMode::Peak);
        detector_.setAttackMs(kDetectorAttackMs);
        detector_.setReleaseMs(detectorReleaseMs_);
        updateTimes();
        reset();
    }

    void reset() noexcept {
        detector_.reset();
        state_.store(GateState::Closed, std::memory_order_relaxed);
        gainDb_ = rangeDb_;
        gain_ = rangeGain_;
        holdCounter_ = 0;
        gainReductionDb_.store(-rangeDb_, std::memory_order_relaxed);
    }

    // ---- Parameters (may be called between blocks) ----------------------------------------
    // Opening threshold (dBFS, compared against the peak envelope)
    void setThresholdDb(float db) noexcept {
        thresholdDb_ = db;
        closeDb_ = thresholdDb_ - hysteresisDb_;
    }
    // Closing threshold = threshold - hysteresis (hysteresis >= 0)
    void setHysteresisDb(float db) noexcept {
        hysteresisDb_ = std::max(0.0f, db);
        closeDb_ = thresholdDb_ - hysteresisDb_;
    }
    // Attenuation applied while closed. The contract specifies a negative value (for example -80); as a safeguard,
    // either sign is treated as "attenuate by |db| dB".
    // Below kMinusInfinityDb (-100 dB) dbToGain returns 0 directly, which means complete silence.
    // Changing the range while the gate is closed does not jump instantly: the Closed state of nextGain() ramps
    // the gain to the new range.
    void setRangeDb(float db) noexcept {
        rangeDb_ = -std::abs(db);
        rangeGain_ = dbToGain(rangeDb_);
        updateTimes();
    }
    void setAttackMs(float ms) noexcept {
        attackMs_ = std::max(0.0f, ms);
        updateTimes();
    }
    void setHoldMs(float ms) noexcept {
        holdMs_ = std::max(0.0f, ms);
        updateTimes();
    }
    void setReleaseMs(float ms) noexcept {
        releaseMs_ = std::max(0.0f, ms);
        updateTimes();
    }
    // Extra: the detector's release (default 20 ms). The shorter it is, the more sensitive the gate, but the larger the
    // envelope ripple on low-frequency signals (see README 5.3).
    void setDetectorReleaseMs(float ms) noexcept {
        detectorReleaseMs_ = std::max(0.1f, ms);
        detector_.setReleaseMs(detectorReleaseMs_);
    }

    float getThresholdDb() const noexcept { return thresholdDb_; }
    float getHysteresisDb() const noexcept { return hysteresisDb_; }
    float getCloseThresholdDb() const noexcept { return closeDb_; }
    float getRangeDb() const noexcept { return rangeDb_; }
    float getAttackMs() const noexcept { return attackMs_; }
    float getHoldMs() const noexcept { return holdMs_; }
    float getReleaseMs() const noexcept { return releaseMs_; }
    float getDetectorReleaseMs() const noexcept { return detectorReleaseMs_; }
    int getLatencySamples() const noexcept { return 0; } // no lookahead -> zero latency (Exercise 6 adds it)

    // ---- Metering (thread-safe, for the UI) ---------------------------------------
    // Positive value: how many dB are currently being attenuated (updated once at the end of each block)
    float getGainReductionDb() const noexcept { return gainReductionDb_.load(std::memory_order_relaxed); }
    // Whether the gate is currently "open or opening" (Attack / Open / Hold). The Release period counts as closing.
    // The state is stored in a std::atomic (relaxed), so the UI thread can read it at any time (what it reads is the
    // state at "some recent sample").
    bool isOpen() const noexcept {
        const GateState s = state_.load(std::memory_order_relaxed);
        return s == GateState::Attack || s == GateState::Open || s == GateState::Hold;
    }
    GateState getState() const noexcept { return state_.load(std::memory_order_relaxed); }

    // ---- Processing -----------------------------------------------------------
    // Core: feed in one "detection" sample (usually max|x| across channels), advance the state machine one step, and
    // return the linear gain to multiply by. process() simply calls it for every sample; the exercises (lookahead,
    // sidechain) can also use it directly.
    float nextGain(float detectorInput) noexcept {
        const float env = detector_.processSample(detectorInput);
        const float envDb = gainToDb(env);
        const bool aboveOpen = envDb >= thresholdDb_;
        const bool belowClose = envDb < closeDb_;
        // Read the state into a local variable first and write the atomic back only once at the end (a relaxed load / store
        // is just a plain mov on x86)
        GateState state = state_.load(std::memory_order_relaxed);

        switch (state) {
        case GateState::Closed:
            if (gainDb_ == rangeDb_) {
                gain_ = rangeGain_;
            } else {
                // The range was changed while the gate was closed: glide there using the attack slope going up and the release
                // slope going down (at least 0.01 dB/sample)
                if (gainDb_ < rangeDb_)
                    gainDb_ = std::min(rangeDb_, gainDb_ + std::max(attackStepDb_, kMinRangeGlideDbPerSample));
                else
                    gainDb_ = std::max(rangeDb_, gainDb_ - std::max(releaseStepDb_, kMinRangeGlideDbPerSample));
                gain_ = gainDb_ == rangeDb_ ? rangeGain_ : dbToGain(gainDb_);
            }
            if (aboveOpen)
                state = GateState::Attack;
            break;

        case GateState::Attack:
            // Once triggered, open all the way (so the transient passes completely); the threshold is only looked at again
            // after reaching 0 dB
            gainDb_ += attackStepDb_;
            if (gainDb_ >= 0.0f) {
                gainDb_ = 0.0f;
                gain_ = 1.0f;
                state = GateState::Open;
            } else {
                gain_ = dbToGain(gainDb_);
            }
            break;

        case GateState::Open:
            gainDb_ = 0.0f;
            gain_ = 1.0f;
            if (belowClose) {
                if (holdSamples_ > 0) {
                    holdCounter_ = holdSamples_;
                    state = GateState::Hold;
                } else {
                    state = GateState::Release;
                }
            }
            break;

        case GateState::Hold:
            // During hold the gain stays at 0 dB; if the signal returns above the threshold, go back to Open (the counter is voided)
            if (aboveOpen)
                state = GateState::Open;
            else if (--holdCounter_ <= 0)
                state = GateState::Release;
            break;

        case GateState::Release:
            if (aboveOpen) {
                state = GateState::Attack; // climb from the current gain, no jump
            } else if (gainDb_ <= rangeDb_) {
                // This only happens when the range is raised (less attenuation) in the middle of a release: hand over to the
                // Closed state to glide up slowly, with no jump
                state = GateState::Closed;
            } else {
                gainDb_ = std::max(rangeDb_, gainDb_ - releaseStepDb_);
                if (gainDb_ <= rangeDb_) {
                    gain_ = rangeGain_;
                    state = GateState::Closed;
                } else {
                    gain_ = dbToGain(gainDb_);
                }
            }
            break;
        }
        state_.store(state, std::memory_order_relaxed);
        return gain_;
    }

    // Detect from the signal itself (stereo-link: max|x| across channels)
    void process(AudioBlock block) noexcept { process(block, block); }

    // Detect from a sidechain (for example a kick track triggering a pad track). The sidechain may have a different
    // number of channels from main; the number of samples is the smaller of the two.
    void process(AudioBlock main, const AudioBlock& sidechain) noexcept {
        const int n = std::min(main.getNumSamples(), sidechain.getNumSamples());
        const int nMain = main.getNumChannels();
        const int nSc = sidechain.getNumChannels();
        for (int i = 0; i < n; ++i) {
            float det = 0.0f;
            for (int ch = 0; ch < nSc; ++ch)
                det = std::max(det, std::abs(sidechain.getChannelPointer(ch)[i]));
            const float g = nextGain(det);
            for (int ch = 0; ch < nMain; ++ch)
                main.getChannelPointer(ch)[i] *= g;
        }
        gainReductionDb_.store(-gainDb_, std::memory_order_relaxed);
    }

private:
    void copyFrom(const Gate& o) {
        sampleRate_ = o.sampleRate_;
        numChannels_ = o.numChannels_;
        thresholdDb_ = o.thresholdDb_;
        hysteresisDb_ = o.hysteresisDb_;
        closeDb_ = o.closeDb_;
        rangeDb_ = o.rangeDb_;
        rangeGain_ = o.rangeGain_;
        attackMs_ = o.attackMs_;
        holdMs_ = o.holdMs_;
        releaseMs_ = o.releaseMs_;
        detectorReleaseMs_ = o.detectorReleaseMs_;
        attackStepDb_ = o.attackStepDb_;
        releaseStepDb_ = o.releaseStepDb_;
        holdSamples_ = o.holdSamples_;
        holdCounter_ = o.holdCounter_;
        detector_ = o.detector_;
        state_.store(o.state_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        gainDb_ = o.gainDb_;
        gain_ = o.gain_;
        gainReductionDb_.store(o.gainReductionDb_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }

    void updateTimes() noexcept {
        const double attackSamples = attackMs_ * 0.001 * sampleRate_;
        const double releaseSamples = releaseMs_ * 0.001 * sampleRate_;
        holdSamples_ = msToSamples(holdMs_, sampleRate_);
        // Slope (dB / sample): the attack must climb from range to 0 dB within attackMs.
        // The reference span is at least 1 dB: with range = 0 (which means the gate is switched off), a slope of 0 would
        // leave a gain that is in Attack forever unable to reach 0 dB.
        const double spanDb = std::max(1.0, static_cast<double>(-rangeDb_));
        attackStepDb_ = attackSamples >= 1.0 ? static_cast<float>(spanDb / attackSamples) : 1.0e9f;
        releaseStepDb_ = releaseSamples >= 1.0 ? static_cast<float>(spanDb / releaseSamples) : 1.0e9f;
    }

    double sampleRate_ = 48000.0;
    int numChannels_ = 2;

    float thresholdDb_ = -40.0f;
    float hysteresisDb_ = 6.0f;
    float closeDb_ = -46.0f;
    float rangeDb_ = -80.0f;
    float rangeGain_ = 0.0001f; // dbToGain(-80)
    float attackMs_ = 1.0f;
    float holdMs_ = 50.0f;
    float releaseMs_ = 100.0f;
    float detectorReleaseMs_ = 20.0f;

    float attackStepDb_ = 0.0f;
    float releaseStepDb_ = 0.0f;
    int holdSamples_ = 0;
    int holdCounter_ = 0;

    EnvelopeFollower detector_;
    std::atomic<GateState> state_{GateState::Closed};
    float gainDb_ = -80.0f;
    float gain_ = 0.0f;
    std::atomic<float> gainReductionDb_{80.0f};
};

} // namespace dawlab::dsp
