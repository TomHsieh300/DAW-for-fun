#pragma once
// Expander.h -- downward expander (Chapter 08)
//
// A compressor pulls down the part "above the threshold"; a downward expander pushes the part "below the threshold"
// even lower, making the quiet places quieter (noise, bleed, reverb tails). It is the "gentle version" of the gate.
//
// Static curve (gain computer, in dB, R = ratio >= 1, T = threshold, W = knee width):
//
//   y = x                                        x >= T + W/2        (unchanged)
//   y = x + (1 - R)(x - T - W/2)^2 / (2W)        T - W/2 <= x < T + W/2   (soft knee, quadratic)
//   y = T + R (x - T)                            x < T - W/2         (every 1 dB lower becomes R dB lower)
//
// This is the "top-bottom mirror" of the Chapter 04 compressor formula: replace (1/R - 1) with (1 - R), and replace
// +W/2 with -W/2.
// range: the lower limit of the gain (for example -60 dB), which prevents the signal from being pushed down until it
// disappears completely (that would be a gate).
//
// Time behavior: the Chapter 04 EnvelopeFollower serves as a "level detector with attack / release", and the smoothed
// level is then fed into the static curve. Signal rising -> the envelope follows with the attack -> the gain returns
// to 0 dB quickly (the gate opens); signal falling -> the envelope decays slowly with the release -> the gain
// reduction deepens slowly (the gate closes slowly). This is the naming convention of attack and release for
// expanders / gates: attack = how fast it opens, release = how fast it closes.
//
// Parameter smoothing: unlike the gate, the expander's threshold, ratio, knee, and range appear directly in the gain
// formula (g = max(range, (R - 1)(x - T))), so changing them between blocks would make the gain jump to the new value
// in one sample (a click). Each of those four parameters therefore goes through a SmoothedValue (Chapter 01) and walks
// linearly to the new value within kParameterRampMs.
// Exception: setters called after reset() and before the first sample is processed take effect directly (no ramp),
// so the "prepare -> set parameters -> start processing" initialization flow does not glide in from the defaults.

#include <dawlab/AudioBuffer.h>
#include <dawlab/DspMath.h>
#include <dawlab/dsp/EnvelopeFollower.h>
#include <dawlab/dsp/SmoothedValue.h>

#include <algorithm>
#include <atomic>
#include <cmath>

namespace dawlab::dsp {

class Expander {
public:
    // Length of the linear ramp when threshold / ratio / knee / range change
    static constexpr float kParameterRampMs = 20.0f;

    Expander() = default;
    // A std::atomic member makes the class non-copyable by default; define these by hand so it can be placed in a
    // std::vector (the 8-track DAW in Chapter 14)
    Expander(const Expander& other) { copyFrom(other); }
    Expander& operator=(const Expander& other) {
        if (this != &other)
            copyFrom(other);
        return *this;
    }

    // ---- Static gain computer (pure functions, used directly by the exercises and tables) -------------------------
    // Returns the output level (dB). ratio >= 1; ratio = 1 means no processing. kneeDb <= 0 is treated as a hard knee.
    static float computeOutputDb(float inputDb, float thresholdDb, float ratio, float kneeDb) noexcept {
        const float r = std::max(1.0f, ratio);
        const float w = std::max(0.0f, kneeDb);
        const float halfW = 0.5f * w;
        if (inputDb >= thresholdDb + halfW)
            return inputDb;
        if (w > 0.0f && inputDb >= thresholdDb - halfW) {
            const float d = inputDb - thresholdDb - halfW; // <= 0
            return inputDb + (1.0f - r) * d * d / (2.0f * w);
        }
        return thresholdDb + r * (inputDb - thresholdDb);
    }

    // Returns the gain (dB, <= 0) = output - input
    static float computeGainDb(float inputDb, float thresholdDb, float ratio, float kneeDb) noexcept {
        return computeOutputDb(inputDb, thresholdDb, ratio, kneeDb) - inputDb;
    }

    // ---- Lifecycle -------------------------------------------------------
    void prepare(double sampleRate, int /*maxBlockSize*/, int numChannels) {
        sampleRate_ = sampleRate;
        numChannels_ = numChannels;
        detector_.prepare(sampleRate);
        detector_.setMode(mode_);
        // Decoupled (peak-hold first, then attack smoothing): the level of a steady sine wave = the true peak.
        // During the release, Branching lets the envelope ride up and down with the waveform and so systematically
        // underestimates the peak (about 1 dB low at 1 kHz with a 5 ms attack), and the expander multiplies this error
        // by (R - 1) into the output.
        detector_.setSmoothingMode(SmoothingMode::Decoupled);
        detector_.setAttackMs(attackMs_);
        detector_.setReleaseMs(releaseMs_);
        const double rampSeconds = kParameterRampMs * 0.001;
        thresholdSmoothed_.reset(sampleRate, rampSeconds);
        ratioSmoothed_.reset(sampleRate, rampSeconds);
        kneeSmoothed_.reset(sampleRate, rampSeconds);
        rangeSmoothed_.reset(sampleRate, rampSeconds);
        reset();
    }

    void reset() noexcept {
        detector_.reset();
        // Snap the parameters directly to their targets (no ramp); setters called afterwards and before the first sample also take effect directly
        thresholdSmoothed_.setCurrentAndTargetValue(thresholdDb_);
        ratioSmoothed_.setCurrentAndTargetValue(ratio_);
        kneeSmoothed_.setCurrentAndTargetValue(kneeDb_);
        rangeSmoothed_.setCurrentAndTargetValue(rangeDb_);
        hasProcessed_ = false;
        gainDb_ = rangeDb_;
        gain_ = dbToGain(rangeDb_);
        gainReductionDb_.store(-rangeDb_, std::memory_order_relaxed);
    }

    // ---- Parameters (may be called between blocks; threshold / ratio / knee / range are smoothed over kParameterRampMs) --------
    void setThresholdDb(float db) noexcept {
        thresholdDb_ = db;
        setSmoothedTarget(thresholdSmoothed_, thresholdDb_);
    }
    // 2 means every 1 dB below the threshold becomes 2 dB
    void setRatio(float ratio) noexcept {
        ratio_ = std::max(1.0f, ratio);
        setSmoothedTarget(ratioSmoothed_, ratio_);
    }
    void setKneeDb(float db) noexcept {
        kneeDb_ = std::max(0.0f, db);
        setSmoothedTarget(kneeSmoothed_, kneeDb_);
    }
    // Lower limit of the gain (dB). By convention the contract uses a negative value (for example -60); either sign is
    // treated as "attenuate by at most |db| dB".
    void setRangeDb(float db) noexcept {
        rangeDb_ = -std::abs(db);
        setSmoothedTarget(rangeSmoothed_, rangeDb_);
    }
    void setAttackMs(float ms) noexcept {
        attackMs_ = std::max(0.0f, ms);
        detector_.setAttackMs(attackMs_);
    }
    void setReleaseMs(float ms) noexcept {
        releaseMs_ = std::max(0.0f, ms);
        detector_.setReleaseMs(releaseMs_);
    }
    // Extra: detection mode (default Peak; Rms is more "musical" for vocals and ensembles but reacts slowly to transients)
    void setDetectorMode(DetectorMode mode) noexcept {
        mode_ = mode;
        detector_.setMode(mode);
    }

    float getThresholdDb() const noexcept { return thresholdDb_; }
    float getRatio() const noexcept { return ratio_; }
    float getKneeDb() const noexcept { return kneeDb_; }
    float getRangeDb() const noexcept { return rangeDb_; }
    float getAttackMs() const noexcept { return attackMs_; }
    float getReleaseMs() const noexcept { return releaseMs_; }
    DetectorMode getDetectorMode() const noexcept { return mode_; }
    int getLatencySamples() const noexcept { return 0; }

    // Positive value: how many dB are currently being attenuated (updated at the end of each block; thread-safe)
    float getGainReductionDb() const noexcept { return gainReductionDb_.load(std::memory_order_relaxed); }

    // ---- Processing -----------------------------------------------------------
    // Core: feed in the detection sample (max|x| across channels) and return the linear gain
    float nextGain(float detectorInput) noexcept {
        const float env = detector_.processSample(detectorInput);
        const float levelDb = gainToDb(env);
        // When a parameter is not changing, getNextValue() returns the target directly (a cheap fast path)
        const float thresholdDb = thresholdSmoothed_.getNextValue();
        const float ratio = ratioSmoothed_.getNextValue();
        const float kneeDb = kneeSmoothed_.getNextValue();
        const float rangeDb = rangeSmoothed_.getNextValue();
        hasProcessed_ = true;
        const float g = computeGainDb(levelDb, thresholdDb, ratio, kneeDb);
        const float newGainDb = std::max(rangeDb, g);
        if (newGainDb != gainDb_) { // only compute pow when the gain changes
            gainDb_ = newGainDb;
            gain_ = dbToGain(gainDb_);
        }
        return gain_;
    }

    // Detect from the signal itself (stereo-link: max|x| across channels)
    void process(AudioBlock block) noexcept { process(block, block); }

    // Extra: detect from a sidechain (the same interface as Gate). The number of samples is the smaller of the two.
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
    // No samples processed yet (just prepared / reset) -> take effect directly; otherwise ramp from the current value
    void setSmoothedTarget(SmoothedValue& s, float value) noexcept {
        if (hasProcessed_)
            s.setTargetValue(value);
        else
            s.setCurrentAndTargetValue(value);
    }

    void copyFrom(const Expander& o) {
        sampleRate_ = o.sampleRate_;
        numChannels_ = o.numChannels_;
        thresholdDb_ = o.thresholdDb_;
        ratio_ = o.ratio_;
        kneeDb_ = o.kneeDb_;
        rangeDb_ = o.rangeDb_;
        attackMs_ = o.attackMs_;
        releaseMs_ = o.releaseMs_;
        mode_ = o.mode_;
        detector_ = o.detector_;
        thresholdSmoothed_ = o.thresholdSmoothed_;
        ratioSmoothed_ = o.ratioSmoothed_;
        kneeSmoothed_ = o.kneeSmoothed_;
        rangeSmoothed_ = o.rangeSmoothed_;
        hasProcessed_ = o.hasProcessed_;
        gainDb_ = o.gainDb_;
        gain_ = o.gain_;
        gainReductionDb_.store(o.gainReductionDb_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }

    double sampleRate_ = 48000.0;
    int numChannels_ = 2;

    float thresholdDb_ = -40.0f;
    float ratio_ = 2.0f;
    float kneeDb_ = 0.0f;
    float rangeDb_ = -60.0f;
    float attackMs_ = 5.0f;
    float releaseMs_ = 100.0f;
    DetectorMode mode_ = DetectorMode::Peak;

    EnvelopeFollower detector_;
    SmoothedValue thresholdSmoothed_{-40.0f};
    SmoothedValue ratioSmoothed_{2.0f};
    SmoothedValue kneeSmoothed_{0.0f};
    SmoothedValue rangeSmoothed_{-60.0f};
    bool hasProcessed_ = false;
    float gainDb_ = 0.0f;
    float gain_ = 1.0f;
    std::atomic<float> gainReductionDb_{0.0f};
};

} // namespace dawlab::dsp
