#pragma once
// DeEsser.h -- de-esser (Chapter 08)
//
// The energy of vocal sibilance (the sounds s, sh, ch, t) is concentrated at 5-10 kHz and sounds harsh.
// A de-esser = "a compressor that listens only to the high frequencies":
//
//   x --+--> bandpass(f, Q=1.5) --> EnvelopeFollower --> GR = clamp(level - T, 0, range) --> g
//       |                                                                                    |
//       +--(wideband)   y = x * g                                                          <-+
//       +--(split-band) split with a 4th-order Linkwitz-Riley crossover: y = LP(x) + HP(x) * g (only the high band is reduced)
//
// - Detection uses the signal after the bandpass filter (center f, Q = 1.5). The -3 dB bandwidth of the analog
//   prototype is 0.72 f to 1.39 f (5.05-9.71 kHz at f = 7 kHz); the bandwidth of the digital RBJ bandpass is narrowed
//   by the bilinear transform (1/Q = 2 sinh(ln2/2 * BW * w0/sin w0)), and at 48 kHz is actually about
//   5.22-9.17 kHz (see README 2.5.2).
// - wideband: the whole signal is reduced together. Simple, but the vowel is also pulled down when sibilance occurs
//   (you may hear a "breathing" feel).
// - split-band: the crossover frequency = 0.72 f (the lower -3 dB point of the analog prototype), and only the high
//   band is reduced, so the vowel is unaffected. The LP + HP sum of an LR4 is an allpass (flat magnitude, changed
//   phase), so with gain = 1 the sound is unchanged (apart from phase).
// - listen: outputs the detection signal (x after the bandpass), used for tuning frequency / threshold.
// - range: the maximum reduction (positive value, for example 12 dB) -- do not reduce the sibilance until it vanishes
//   completely, or the speaker will sound like they have a lisp.
//
// The gain computer here uses inf:1 (however many dB the level exceeds the threshold is how many dB it is reduced,
// capped at range), which is the most intuitive for a de-esser.
// Threshold and range appear directly in the GR formula, so both go through a SmoothedValue (kParameterRampMs); changing
// them between blocks does not make the gain jump instantly; setters called after reset() and before the first sample
// is processed take effect directly.
// NaN / Inf in the input are replaced with 0 before entering any biquad: once the TDF-II state becomes NaN it never
// recovers.

#include <dawlab/AudioBuffer.h>
#include <dawlab/DspMath.h>
#include <dawlab/dsp/Biquad.h>
#include <dawlab/dsp/EnvelopeFollower.h>
#include <dawlab/dsp/SmoothedValue.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

namespace dawlab::dsp {

class DeEsser {
public:
    static constexpr float kDetectorQ = 1.5f;
    // Crossover frequency / detection center frequency = (sqrt(1 + 4Q^2) - 1) / (2Q), Q = 1.5 -> 0.7208 (the lower
    // -3 dB point of the analog prototype bandpass; the actual lower edge of the digital bandpass is slightly higher,
    // about 0.745 f at 48 kHz and f = 7 kHz)
    static constexpr float kCrossoverRatio = 0.7208f;
    // Linkwitz-Riley 4th order = two 2nd-order Butterworth filters in cascade (Q = 1/sqrt(2))
    static constexpr double kButterworthQ = 0.70710678118654752;
    // Length of the linear ramp when threshold / range change
    static constexpr float kParameterRampMs = 20.0f;

    DeEsser() = default;
    // A std::atomic member makes the class non-copyable by default; define these by hand so it can be placed in a
    // std::vector (the 8-track DAW in Chapter 14)
    DeEsser(const DeEsser& other) { copyFrom(other); }
    DeEsser& operator=(const DeEsser& other) {
        if (this != &other)
            copyFrom(other);
        return *this;
    }

    // ---- Lifecycle -------------------------------------------------------
    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        sampleRate_ = sampleRate;
        numChannels_ = numChannels;
        detectorFilter_.prepare(sampleRate, maxBlockSize, numChannels);
        lowpass1_.prepare(sampleRate, maxBlockSize, numChannels);
        lowpass2_.prepare(sampleRate, maxBlockSize, numChannels);
        highpass1_.prepare(sampleRate, maxBlockSize, numChannels);
        highpass2_.prepare(sampleRate, maxBlockSize, numChannels);
        detectorOut_.assign(static_cast<size_t>(numChannels), 0.0f);
        follower_.prepare(sampleRate);
        follower_.setMode(DetectorMode::Peak);
        follower_.setAttackMs(attackMs_);
        follower_.setReleaseMs(releaseMs_);
        thresholdSmoothed_.reset(sampleRate, kParameterRampMs * 0.001);
        rangeSmoothed_.reset(sampleRate, kParameterRampMs * 0.001);
        updateFilters();
        reset();
    }

    void reset() noexcept {
        detectorFilter_.reset();
        lowpass1_.reset();
        lowpass2_.reset();
        highpass1_.reset();
        highpass2_.reset();
        follower_.reset();
        thresholdSmoothed_.setCurrentAndTargetValue(thresholdDb_);
        rangeSmoothed_.setCurrentAndTargetValue(rangeDb_);
        hasProcessed_ = false;
        gainReductionDbCurrent_ = 0.0f;
        gain_ = 1.0f;
        gainReductionDb_.store(0.0f, std::memory_order_relaxed);
    }

    // ---- Parameters -----------------------------------------------------------
    // Detection center frequency (Hz), default 7000; the crossover frequency follows = 0.72 x f
    void setFrequency(float hz) noexcept {
        frequencyHz_ = std::clamp(hz, 1000.0f, static_cast<float>(sampleRate_ * 0.45));
        updateFilters();
    }
    void setThresholdDb(float db) noexcept {
        thresholdDb_ = db;
        setSmoothedTarget(thresholdSmoothed_, thresholdDb_);
    }
    // Maximum reduction (positive value, dB); either sign is treated as |db|
    void setRangeDb(float db) noexcept {
        rangeDb_ = std::abs(db);
        setSmoothedTarget(rangeSmoothed_, rangeDb_);
    }
    void setSplitBand(bool enabled) noexcept { splitBand_ = enabled; }
    void setListen(bool enabled) noexcept { listen_ = enabled; }
    // Extra: detector ballistics (default attack 1 ms, release 40 ms)
    void setAttackMs(float ms) noexcept {
        attackMs_ = std::max(0.0f, ms);
        follower_.setAttackMs(attackMs_);
    }
    void setReleaseMs(float ms) noexcept {
        releaseMs_ = std::max(0.0f, ms);
        follower_.setReleaseMs(releaseMs_);
    }

    float getFrequency() const noexcept { return frequencyHz_; }
    float getCrossoverHz() const noexcept { return frequencyHz_ * kCrossoverRatio; }
    float getThresholdDb() const noexcept { return thresholdDb_; }
    float getRangeDb() const noexcept { return rangeDb_; }
    bool isSplitBand() const noexcept { return splitBand_; }
    bool isListen() const noexcept { return listen_; }
    float getAttackMs() const noexcept { return attackMs_; }
    float getReleaseMs() const noexcept { return releaseMs_; }
    int getLatencySamples() const noexcept { return 0; } // IIR crossover: no whole-sample delay

    // Positive value: how many dB are currently being attenuated (updated at the end of each block; thread-safe)
    float getGainReductionDb() const noexcept { return gainReductionDb_.load(std::memory_order_relaxed); }

    // ---- Processing -----------------------------------------------------------
    void process(AudioBlock block) noexcept {
        const int n = block.getNumSamples();
        const int nch = std::min(block.getNumChannels(), numChannels_);
        for (int i = 0; i < n; ++i) {
            // 0. NaN / Inf guard (same as EnvelopeFollower): sanitize before feeding the biquads, otherwise the filter state
            //    becomes NaN permanently
            for (int ch = 0; ch < nch; ++ch) {
                float& x = block.getChannelPointer(ch)[i];
                if (!std::isfinite(x))
                    x = 0.0f;
            }
            // 1. Detection: bandpass each channel separately, then take max|.| for the stereo link
            float det = 0.0f;
            for (int ch = 0; ch < nch; ++ch) {
                const float d = detectorFilter_.processSample(ch, block.getChannelPointer(ch)[i]);
                detectorOut_[static_cast<size_t>(ch)] = d;
                det = std::max(det, std::abs(d));
            }
            // 2. Gain computer: inf:1, capped at range (threshold / range use their smoothed values)
            const float levelDb = gainToDb(follower_.processSample(det));
            const float thresholdDb = thresholdSmoothed_.getNextValue();
            const float rangeDb = rangeSmoothed_.getNextValue();
            hasProcessed_ = true;
            const float gr = std::clamp(levelDb - thresholdDb, 0.0f, rangeDb);
            if (gr != gainReductionDbCurrent_) {
                gainReductionDbCurrent_ = gr;
                gain_ = dbToGain(-gr);
            }
            // 3. Apply
            for (int ch = 0; ch < nch; ++ch) {
                float* d = block.getChannelPointer(ch);
                const float x = d[i];
                // The crossover filters always keep running (even in wideband mode), so switching modes does not click because the
                // filter state is empty
                const float low = lowpass2_.processSample(ch, lowpass1_.processSample(ch, x));
                const float high = highpass2_.processSample(ch, highpass1_.processSample(ch, x));
                if (listen_)
                    d[i] = detectorOut_[static_cast<size_t>(ch)];
                else if (splitBand_)
                    d[i] = low + high * gain_;
                else
                    d[i] = x * gain_;
            }
        }
        gainReductionDb_.store(gainReductionDbCurrent_, std::memory_order_relaxed);
    }

private:
    // No samples processed yet (just prepared / reset) -> take effect directly; otherwise ramp from the current value
    void setSmoothedTarget(SmoothedValue& s, float value) noexcept {
        if (hasProcessed_)
            s.setTargetValue(value);
        else
            s.setCurrentAndTargetValue(value);
    }

    void copyFrom(const DeEsser& o) {
        sampleRate_ = o.sampleRate_;
        numChannels_ = o.numChannels_;
        frequencyHz_ = o.frequencyHz_;
        thresholdDb_ = o.thresholdDb_;
        rangeDb_ = o.rangeDb_;
        attackMs_ = o.attackMs_;
        releaseMs_ = o.releaseMs_;
        splitBand_ = o.splitBand_;
        listen_ = o.listen_;
        detectorFilter_ = o.detectorFilter_;
        lowpass1_ = o.lowpass1_;
        lowpass2_ = o.lowpass2_;
        highpass1_ = o.highpass1_;
        highpass2_ = o.highpass2_;
        detectorOut_ = o.detectorOut_;
        follower_ = o.follower_;
        thresholdSmoothed_ = o.thresholdSmoothed_;
        rangeSmoothed_ = o.rangeSmoothed_;
        hasProcessed_ = o.hasProcessed_;
        gainReductionDbCurrent_ = o.gainReductionDbCurrent_;
        gain_ = o.gain_;
        gainReductionDb_.store(o.gainReductionDb_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }

    void updateFilters() noexcept {
        const double f = frequencyHz_;
        const double fc = getCrossoverHz();
        detectorFilter_.setCoefficients(BiquadCoefficients::makeBandpass(sampleRate_, f, kDetectorQ));
        const auto lp = BiquadCoefficients::makeLowpass(sampleRate_, fc, kButterworthQ);
        const auto hp = BiquadCoefficients::makeHighpass(sampleRate_, fc, kButterworthQ);
        lowpass1_.setCoefficients(lp);
        lowpass2_.setCoefficients(lp);
        highpass1_.setCoefficients(hp);
        highpass2_.setCoefficients(hp);
    }

    double sampleRate_ = 48000.0;
    int numChannels_ = 0;

    float frequencyHz_ = 7000.0f;
    float thresholdDb_ = -30.0f;
    float rangeDb_ = 12.0f;
    float attackMs_ = 1.0f;
    float releaseMs_ = 40.0f;
    bool splitBand_ = false;
    bool listen_ = false;

    Biquad detectorFilter_;
    Biquad lowpass1_, lowpass2_, highpass1_, highpass2_;
    std::vector<float> detectorOut_;
    EnvelopeFollower follower_;
    SmoothedValue thresholdSmoothed_{-30.0f};
    SmoothedValue rangeSmoothed_{12.0f};
    bool hasProcessed_ = false;

    float gainReductionDbCurrent_ = 0.0f;
    float gain_ = 1.0f;
    std::atomic<float> gainReductionDb_{0.0f};
};

} // namespace dawlab::dsp
