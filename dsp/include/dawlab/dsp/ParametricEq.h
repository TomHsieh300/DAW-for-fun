#pragma once
// ParametricEq: a 6-band parametric EQ (Chapter 03).
//
// Each band is one Biquad from Chapter 02 (RBJ Audio EQ Cookbook coefficients), and the six bands
// are connected in cascade. What a cascade means: H(z) = H1(z)·H2(z)·…·H6(z), so the dB responses
// "add up" -- which is also why getMagnitudeDb() can sum each band's magnitudeDb() to get the
// whole curve.
//
// Strategy for avoiding clicks when parameters change live (README section 2.9):
//   1. The three parameters that affect the coefficients (frequency, Q, gain) are each smoothed
//      with a linear ramp using SmoothedValue from Chapter 01 (frequency is smoothed in the log2
//      domain, so a sweep moves at the same speed in every octave; gain is smoothed in the dB
//      domain).
//   2. Every kDefaultUpdateInterval (default 32) samples we run a check (tick): the smoothers only
//      advance by the number of samples that have actually elapsed since the last tick, and only
//      bands whose parameter values differ from those used for the last coefficient computation
//      recompute their RBJ coefficients. The coefficients are not recomputed every sample (that
//      saves sin/cos/pow), and they are not "interpolated coefficients" either (see the README for
//      why interpolating is a bad idea).
//   3. A band's enabled switch uses a dry/wet mix with a 20 ms crossfade, so bands such as
//      LowCut/HighCut/Notch, which have no gain to fade out, do not click either.
//   4. "Structural" changes (band type, the slope of LowCut/HighCut) cannot be interpolated
//      smoothly -- there is no "halfway" between Peak and Notch. Instead the band is first faded
//      out to bypass with the mix (20 ms), the type is swapped and the filter memory cleared while
//      the band is fully bypassed, and then the new type is faded in (20 ms). That is about 40 ms
//      in total and sounds like one very short crossfade.
//
// The interface follows the contract in section 5 of docs/authoring-guide.md; it additionally
// provides EqSlope (LowCut/HighCut at 12 or 24 dB/oct), getResponse / getPhaseDegrees /
// getGroupDelaySamples, and helpers for converting between Q and bandwidth.

#include <dawlab/AudioBuffer.h>
#include <dawlab/DspMath.h>
#include <dawlab/dsp/Biquad.h>
#include <dawlab/dsp/SmoothedValue.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <complex>

namespace dawlab::dsp {

enum class EqBandType { LowCut, LowShelf, Peak, HighShelf, HighCut, Notch };

// Slope of LowCut / HighCut: Db12 = one 2nd-order Butterworth section (uses the band's q, default
// 0.707); Db24 = two 2nd-order sections cascaded into a 4th-order Butterworth (Q = 0.5412 and
// 1.3066; the band's q is ignored in this case).
enum class EqSlope { Db12, Db24 };

struct EqBand {
    EqBandType type = EqBandType::Peak;
    float frequency = 1000.0f; // Hz (Peak: center; Shelf: transition "midpoint"; Cut: -3 dB point)
    float q = 0.707f;          // Peak/Notch: bandwidth; Shelf: transition steepness (0.707 = steepest without overshoot); Cut: resonance
    float gainDb = 0.0f;       // Only meaningful for Peak / LowShelf / HighShelf
    bool enabled = false;
};

class ParametricEq {
public:
    static constexpr int kNumBands = 6;
    static constexpr float kMinFrequency = 10.0f;
    // The target value (what getBand returns) is only clamped to this "absolute" upper limit. The
    // sample-rate-dependent limit of 0.49·fs is applied only when computing coefficients
    // (clampFrequency), so a frequency set before prepare() is not permanently clamped by the
    // default 48 kHz.
    static constexpr float kMaxFrequency = 100000.0f;
    static constexpr float kMinQ = 0.05f;
    static constexpr float kMaxQ = 40.0f;
    static constexpr float kMaxGainDb = 36.0f;
    static constexpr int kDefaultUpdateInterval = 32; // Recompute coefficients every 32 samples (about 0.67 ms at 48 kHz)
    static constexpr double kDefaultSmoothingSeconds = 0.02;
    // The Q values of the two 2nd-order sections that make up a 4th-order Butterworth (same as
    // butterworthQ(4, k) in Chapter 02):
    //     Q_k = 1 / (2 cos((2k + 1) π / 8)),  k = 0 -> 0.5412,  k = 1 -> 1.3066
    // Neither section has its own -3 dB point at fc, but after multiplying them |H(fc)| = 1/√2,
    // so the overall response is still -3 dB there (README section 2.3).
    static constexpr double kButterworthQ1 = 0.54119610014619698;
    static constexpr double kButterworthQ2 = 1.30656296487637652;

    ParametricEq() { setDefaultBands(); }

    // ---- Standard interface -------------------------------------------------------------

    // The only place that may allocate: sets up each Biquad's per-channel state and aligns all
    // smoothers with the current settings.
    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        sampleRate_ = sampleRate;
        numChannels_ = numChannels;
        for (auto& b : bands_) {
            b.stage1.prepare(sampleRate, maxBlockSize, numChannels);
            b.stage2.prepare(sampleRate, maxBlockSize, numChannels);
            b.logFreq.reset(sampleRate, smoothingSeconds_);
            b.q.reset(sampleRate, smoothingSeconds_);
            b.gainDb.reset(sampleRate, smoothingSeconds_);
            b.mix.reset(sampleRate, smoothingSeconds_);
        }
        reset();
    }

    // Clears the filter memory and makes the smoothers jump straight to their targets (no ramp at
    // the start). Frequencies are re-clamped using the current sample rate (this is where a
    // sample-rate change made in prepare takes effect).
    void reset() noexcept {
        for (auto& b : bands_) {
            b.stage1.reset();
            b.stage2.reset();
            snapToTarget(b);
        }
        samplesUntilUpdate_ = 0;
        samplesSinceTick_ = 0;
    }

    // ---- Parameters ---------------------------------------------------------------------
    // All setters must be called between blocks, from the same thread (the audio thread) that
    // calls process(): they modify the smoothers and the filter state, so running them
    // concurrently with process() is a data race.
    // Send UI parameters to the audio thread through an atomic / FIFO first, then apply them before
    // process() (Chapters 13 and 14).

    void setBand(int index, const EqBand& band) noexcept {
        assert(index >= 0 && index < kNumBands);
        Band& b = bands_[static_cast<size_t>(index)];
        EqBand t = band;
        // Ignore NaN / inf fields (keep the old value): std::clamp(NaN, lo, hi) returns NaN
        // unchanged, and once a NaN gets into a smoother, the whole band is broken after log2 / exp2.
        if (!std::isfinite(t.frequency))
            t.frequency = b.target.frequency;
        if (!std::isfinite(t.q))
            t.q = b.target.q;
        if (!std::isfinite(t.gainDb))
            t.gainDb = b.target.gainDb;
        // The target keeps the user's setting (clamped only to ranges that do not depend on the
        // sample rate); the 0.49·fs upper limit is applied later in targetLogFreq() /
        // makeCoefficients().
        t.frequency = std::clamp(t.frequency, kMinFrequency, kMaxFrequency);
        t.q = std::clamp(t.q, kMinQ, kMaxQ);
        t.gainDb = std::clamp(t.gainDb, -kMaxGainDb, kMaxGainDb);
        const bool paramsChanged =
            t.frequency != b.target.frequency || t.q != b.target.q || t.gainDb != b.target.gainDb;
        if (!paramsChanged && t.type == b.target.type && t.enabled == b.target.enabled)
            return; // Nothing changed (e.g. the host resends the same parameters every block): do nothing, the ramps keep their speed.
        if (paramsChanged)
            catchUpSmoothers(); // Advance all ramps to "now" first, so the new ramps start from the correct position.
        b.target = t;
        b.logFreq.setTargetValue(targetLogFreq(t));
        b.q.setTargetValue(t.q);
        b.gainDb.setTargetValue(t.gainDb);
        if (!b.active)
            snapParameters(b); // Fully bypassed: nobody can hear it, so jump the parameters straight to the target (the fade-in then shows the final curve).
        requestStructure(b);
        // Tick on the very next sample instead of waiting for the next update boundary. The tick
        // only advances the number of samples that have actually elapsed (samplesSinceTick_), so
        // ticking early does not make the ramps faster.
        samplesUntilUpdate_ = 0;
        // If a structural switch is queued, fade out to bypass first; otherwise fade to the mix
        // that corresponds to enabled.
        b.mix.setTargetValue((t.enabled && !b.pendingSwitch) ? 1.0f : 0.0f);
    }

    const EqBand& getBand(int index) const noexcept {
        assert(index >= 0 && index < kNumBands);
        return bands_[static_cast<size_t>(index)].target;
    }

    // Small helpers so a UI can change a single field (they all go through setBand, so they also click-free)
    void setBandEnabled(int index, bool enabled) noexcept {
        EqBand b = getBand(index);
        b.enabled = enabled;
        setBand(index, b);
    }
    void setBandType(int index, EqBandType type) noexcept {
        EqBand b = getBand(index);
        b.type = type;
        setBand(index, b);
    }
    void setBandFrequency(int index, float hz) noexcept {
        EqBand b = getBand(index);
        b.frequency = hz;
        setBand(index, b);
    }
    void setBandQ(int index, float q) noexcept {
        EqBand b = getBand(index);
        b.q = q;
        setBand(index, b);
    }
    void setBandGainDb(int index, float gainDb) noexcept {
        EqBand b = getBand(index);
        b.gainDb = gainDb;
        setBand(index, b);
    }

    // Slope of LowCut / HighCut (ignored for the other band types)
    void setSlope(int index, EqSlope slope) noexcept {
        assert(index >= 0 && index < kNumBands);
        Band& b = bands_[static_cast<size_t>(index)];
        if (slope == b.slope)
            return;
        b.slope = slope;
        requestStructure(b);
        samplesUntilUpdate_ = 0;
        b.mix.setTargetValue((b.target.enabled && !b.pendingSwitch) ? 1.0f : 0.0f);
    }
    EqSlope getSlope(int index) const noexcept {
        assert(index >= 0 && index < kNumBands);
        return bands_[static_cast<size_t>(index)].slope;
    }

    // Smoothing time in seconds. Call before prepare(); the default is 20 ms. 0 = no smoothing
    // (parameters and the enable switch change instantly).
    void setSmoothingSeconds(double seconds) noexcept { smoothingSeconds_ = std::max(0.0, seconds); }
    double getSmoothingSeconds() const noexcept { return smoothingSeconds_; }

    // How many samples pass between coefficient recomputations while smoothing (1 = every sample:
    // smoothest but most CPU).
    void setCoefficientUpdateInterval(int samples) noexcept { updateInterval_ = std::max(1, samples); }
    int getCoefficientUpdateInterval() const noexcept { return updateInterval_; }

    double getSampleRate() const noexcept { return sampleRate_; }

    // ---- Frequency response (for UI drawing; uses the "target" parameters, independent of smoothing progress)

    // dB of the whole EQ curve: the dB values of the enabled bands add up (cascade = complex
    // multiplication = dB addition).
    double getMagnitudeDb(double freq) const noexcept {
        double total = 0.0;
        for (int i = 0; i < kNumBands; ++i)
            total += getBandMagnitudeDb(i, freq);
        return total;
    }

    // dB of a single band (disabled = 0 dB)
    double getBandMagnitudeDb(int index, double freq) const noexcept {
        const Band& b = bands_[static_cast<size_t>(index)];
        if (!b.target.enabled)
            return 0.0;
        double db = makeCoefficients(sampleRate_, b.target, b.slope).magnitudeDb(freq, sampleRate_);
        if (usesTwoStages(b.target.type, b.slope))
            db += makeSecondStageCoefficients(sampleRate_, b.target).magnitudeDb(freq, sampleRate_);
        return db;
    }

    // Complex response H(e^{jω}) of the whole EQ (product of all enabled bands)
    std::complex<double> getResponse(double freq) const noexcept {
        std::complex<double> h(1.0, 0.0);
        for (const auto& b : bands_) {
            if (!b.target.enabled)
                continue;
            h *= makeCoefficients(sampleRate_, b.target, b.slope).response(freq, sampleRate_);
            if (usesTwoStages(b.target.type, b.slope))
                h *= makeSecondStageCoefficients(sampleRate_, b.target).response(freq, sampleRate_);
        }
        return h;
    }

    // Phase in degrees (-180..180)
    double getPhaseDegrees(double freq) const noexcept { return std::arg(getResponse(freq)) * 180.0 / kPi; }

    // Group delay τ(ω) = -dφ/dω, estimated by numerical differentiation, in samples
    double getGroupDelaySamples(double freq) const noexcept {
        const double df = std::max(0.5, freq * 1e-3);
        const double p1 = std::arg(getResponse(freq - df));
        const double p2 = std::arg(getResponse(freq + df));
        double dphi = p2 - p1; // Unwrap into (-π, π]
        while (dphi > kPi)
            dphi -= kTwoPi;
        while (dphi <= -kPi)
            dphi += kTwoPi;
        const double dOmega = kTwoPi * (2.0 * df) / sampleRate_;
        return -dphi / dOmega;
    }

    // ---- Static helpers: the formulas match README sections 2.4-2.6 ---------------------

    // Q -> bandwidth (octaves): BW = (2 / ln 2) · asinh(1 / (2Q))
    static double qToBandwidthOctaves(double q) noexcept { return 2.0 / std::log(2.0) * std::asinh(1.0 / (2.0 * q)); }

    // bandwidth (octaves) -> Q: Q = 1 / (2 sinh(ln 2 · BW / 2)) = 2^(BW/2) / (2^BW - 1)
    static double bandwidthOctavesToQ(double bw) noexcept { return 1.0 / (2.0 * std::sinh(std::log(2.0) * bw / 2.0)); }

    // RBJ shelf slope parameter S -> Q: Q = 1 / sqrt((A + 1/A)(1/S - 1) + 2); for S = 1 this is 0.707 at any gain
    static double shelfSlopeToQ(double slope, double gainDb) noexcept {
        const double A = std::pow(10.0, gainDb / 40.0);
        return 1.0 / std::sqrt((A + 1.0 / A) * (1.0 / slope - 1.0) + 2.0);
    }

    // The "midpoint gain" bandwidth edges of a peak: f_{1,2} = f0 ( sqrt(1 + 1/(4Q²)) ∓ 1/(2Q) )
    static void bandEdges(double f0, double q, double& fLow, double& fHigh) noexcept {
        const double r = std::sqrt(1.0 + 1.0 / (4.0 * q * q));
        fLow = f0 * (r - 1.0 / (2.0 * q));
        fHigh = f0 * (r + 1.0 / (2.0 * q));
    }

    static float clampFrequency(float hz, double sampleRate) noexcept {
        return std::clamp(hz, kMinFrequency, static_cast<float>(0.49 * sampleRate));
    }

    static bool usesTwoStages(EqBandType type, EqSlope slope) noexcept {
        return slope == EqSlope::Db24 && (type == EqBandType::LowCut || type == EqBandType::HighCut);
    }

    // The coefficients of a band's (first) Biquad
    static BiquadCoefficients makeCoefficients(double sr, const EqBand& band, EqSlope slope = EqSlope::Db12) noexcept {
        const double f = clampFrequency(band.frequency, sr);
        const double q = std::clamp(band.q, kMinQ, kMaxQ);
        const double g = std::clamp(band.gainDb, -kMaxGainDb, kMaxGainDb);
        const bool two = usesTwoStages(band.type, slope);
        switch (band.type) {
        case EqBandType::LowCut: return BiquadCoefficients::makeHighpass(sr, f, two ? kButterworthQ1 : q);
        case EqBandType::HighCut: return BiquadCoefficients::makeLowpass(sr, f, two ? kButterworthQ1 : q);
        case EqBandType::LowShelf: return BiquadCoefficients::makeLowShelf(sr, f, q, g);
        case EqBandType::HighShelf: return BiquadCoefficients::makeHighShelf(sr, f, q, g);
        case EqBandType::Notch: return BiquadCoefficients::makeNotch(sr, f, q);
        case EqBandType::Peak:
        default: return BiquadCoefficients::makePeak(sr, f, q, g);
        }
    }

    // Coefficients of the second section for Db24 (only used by LowCut / HighCut; other types return identity)
    static BiquadCoefficients makeSecondStageCoefficients(double sr, const EqBand& band) noexcept {
        const double f = clampFrequency(band.frequency, sr);
        if (band.type == EqBandType::LowCut)
            return BiquadCoefficients::makeHighpass(sr, f, kButterworthQ2);
        if (band.type == EqBandType::HighCut)
            return BiquadCoefficients::makeLowpass(sr, f, kButterworthQ2);
        return BiquadCoefficients{};
    }

    // ---- Processing ---------------------------------------------------------------------

    // Per sample: call channels 0, 1, … in order for every frame; the parameter-smoothing counters advance on channel 0
    float processSample(int channel, float x) noexcept {
        if (channel == 0)
            advanceFrame();
        return processChain(channel, x);
    }

    void process(AudioBlock block) noexcept {
        const int n = block.getNumSamples();
        const int nch = std::min(block.getNumChannels(), numChannels_);
        for (int i = 0; i < n; ++i) {
            advanceFrame();
            for (int ch = 0; ch < nch; ++ch) {
                float* d = block.getChannelPointer(ch);
                d[i] = processChain(ch, d[i]);
            }
        }
    }

private:
    struct Band {
        EqBand target;                   // Target values set by the UI
        EqSlope slope = EqSlope::Db12;   // Slope set by the UI
        EqBandType runningType = EqBandType::Peak; // The type / slope the filter is "really running" right now
        EqSlope runningSlope = EqSlope::Db12;      // (still the old one while a structural switch is fading out)
        bool pendingSwitch = false;      // The type / slope must be swapped once the fade-out to bypass is done
        SmoothedValue logFreq, q, gainDb; // Parameters being smoothed (frequency is smoothed as log2(Hz))
        SmoothedValue mix;               // 0 = bypass, 1 = fully on; crossfades when enabled is toggled
        Biquad stage1, stage2;
        float mixCurrent = 0.0f;
        bool active = false;             // Whether this band's filter needs to run right now
        bool twoStages = false;
        bool dirty = false;              // The type / slope changed, so the next tick must recompute the coefficients
        float coeffLogFreq = 0.0f, coeffQ = 0.0f, coeffGainDb = 0.0f; // Which parameter values the current coefficients were computed from
    };
    // Note: the whole class is deliberately kept copyable / movable (no atomics, no pointers), so
    // the DAW in Chapter 14 can put it in a container such as std::vector<Track>.

    void setDefaultBands() noexcept {
        const EqBand defaults[kNumBands] = {
            {EqBandType::LowCut, 80.0f, 0.707f, 0.0f, false},     {EqBandType::LowShelf, 150.0f, 0.707f, 0.0f, false},
            {EqBandType::Peak, 400.0f, 1.0f, 0.0f, false},        {EqBandType::Peak, 2500.0f, 1.0f, 0.0f, false},
            {EqBandType::HighShelf, 8000.0f, 0.707f, 0.0f, false}, {EqBandType::HighCut, 16000.0f, 0.707f, 0.0f, false},
        };
        for (int i = 0; i < kNumBands; ++i) {
            bands_[static_cast<size_t>(i)].target = defaults[i];
            snapToTarget(bands_[static_cast<size_t>(i)]);
        }
    }

    // The smoother's frequency target: log2(Hz), clamped to the valid range for the current sample rate
    float targetLogFreq(const EqBand& t) const noexcept { return std::log2(clampFrequency(t.frequency, sampleRate_)); }

    void snapParameters(Band& b) const noexcept {
        b.logFreq.setCurrentAndTargetValue(targetLogFreq(b.target));
        b.q.setCurrentAndTargetValue(b.target.q);
        b.gainDb.setCurrentAndTargetValue(b.target.gainDb);
    }

    // Make the smoothers jump straight to the target and recompute the coefficients
    void snapToTarget(Band& b) noexcept {
        snapParameters(b);
        b.mix.setCurrentAndTargetValue(b.target.enabled ? 1.0f : 0.0f);
        b.mixCurrent = b.mix.getCurrentValue();
        b.active = b.target.enabled;
        b.runningType = b.target.type;
        b.runningSlope = b.slope;
        b.pendingSwitch = false;
        updateCoefficients(b);
        b.dirty = false;
    }

    // If the type or slope differs from what is "running" -> structural switch.
    // While the band is sounding (active) it must fade out first; when fully bypassed it can be swapped directly, and nobody will hear it.
    void requestStructure(Band& b) noexcept {
        const bool isCut = b.target.type == EqBandType::LowCut || b.target.type == EqBandType::HighCut;
        const bool changed = b.target.type != b.runningType || (isCut && b.slope != b.runningSlope);
        if (!changed) {
            // The user changed the type back to the original while it was fading out: cancel the queued switch, and the mix can simply fade back.
            b.pendingSwitch = false;
            return;
        }
        if (b.active) {
            b.pendingSwitch = true;
        } else {
            b.runningType = b.target.type;
            b.runningSlope = b.slope;
            b.dirty = true;
        }
    }

    void updateCoefficients(Band& b) noexcept {
        // Remember the parameter values used this time: at a tick we recompute whenever the
        // current values differ from them (whether or not the band is "currently smoothing").
        b.coeffLogFreq = b.logFreq.getCurrentValue();
        b.coeffQ = b.q.getCurrentValue();
        b.coeffGainDb = b.gainDb.getCurrentValue();
        EqBand cur = b.target;
        cur.type = b.runningType;
        cur.frequency = std::exp2(b.coeffLogFreq);
        cur.q = b.coeffQ;
        cur.gainDb = b.coeffGainDb;
        b.stage1.setCoefficients(makeCoefficients(sampleRate_, cur, b.runningSlope));
        b.twoStages = usesTwoStages(cur.type, b.runningSlope);
        if (b.twoStages)
            b.stage2.setCoefficients(makeSecondStageCoefficients(sampleRate_, cur));
    }

    // Called once per frame (the same sample instant for all channels):
    //   - Coefficients: recomputed only every updateInterval_ samples (a tick). The tick advances
    //     the smoothers by the number of samples that have actually elapsed since the last tick,
    //     so the coefficients lag by at most updateInterval_ samples and never run ahead; when
    //     setBand() forces an early tick it also advances only the part that has elapsed, so the
    //     total ramp length is unaffected.
    //   - mix (the dry/wet crossfade): advances every sample. The mix is only a multiplication, so
    //     doing it per sample is cheap; if it also jumped one step every 32 samples, the crossfade
    //     itself would turn into a little zipper noise.
    void advanceFrame() noexcept {
        if (samplesUntilUpdate_ == 0) {
            tick(samplesSinceTick_);
            samplesSinceTick_ = 0;
            samplesUntilUpdate_ = updateInterval_;
        }
        --samplesUntilUpdate_;
        ++samplesSinceTick_;
        for (auto& b : bands_)
            if (b.mix.isSmoothing())
                b.mixCurrent = b.mix.getNextValue();
    }

    // Before setBand() changes parameters: advance all smoothers to "now" (the samples elapsed
    // since the last tick), so the new ramps start from the current values and the next tick does
    // not advance the same stretch again. The coefficients are recomputed by the tick that follows immediately.
    void catchUpSmoothers() noexcept {
        if (samplesSinceTick_ <= 0)
            return;
        for (auto& b : bands_) {
            b.logFreq.skip(samplesSinceTick_);
            b.q.skip(samplesSinceTick_);
            b.gainDb.skip(samplesSinceTick_);
        }
        samplesSinceTick_ = 0;
    }

    // Runs once every updateInterval_ samples (or on the sample after a setBand):
    // advances the smoothers by numSamples (the samples elapsed since the last tick) and
    // recomputes the coefficients when needed.
    void tick(int numSamples) noexcept {
        for (auto& b : bands_) {
            // Structural switch: once faded out to bypass -> swap type / slope, clear the filter memory, then fade back in
            if (b.pendingSwitch && !b.mix.isSmoothing() && b.mix.getCurrentValue() <= 0.0f) {
                b.pendingSwitch = false;
                b.runningType = b.target.type;
                b.runningSlope = b.slope;
                snapParameters(b); // mix = 0 right now: the new type fades in directly with the final parameters, with no need to sweep while fading in
                b.dirty = true;
                b.active = false; // Lets the !wasActive branch below clear the old filter memory
                b.mix.setTargetValue(b.target.enabled ? 1.0f : 0.0f);
            }
            const bool wasActive = b.active;
            b.logFreq.skip(numSamples); // skip does nothing when not smoothing
            b.q.skip(numSamples);
            b.gainDb.skip(numSamples);
            b.mixCurrent = b.mix.getCurrentValue();
            b.active = b.mixCurrent > 0.0f || b.mix.isSmoothing();
            if (!b.active)
                continue;
            if (!wasActive) { // Just went from fully bypassed to active: the filter memory is stale, so clear it before crossfading in
                b.stage1.reset();
                b.stage2.reset();
            }
            // Recompute only when "the parameter values differ from those used at the last
            // coefficient computation": this holds at every tick for a band that is smoothing;
            // with smoothing = 0, setTargetValue makes the value jump to the target, and this
            // condition is also what applies it to the filter.
            const bool paramsMoved = b.logFreq.getCurrentValue() != b.coeffLogFreq ||
                                     b.q.getCurrentValue() != b.coeffQ || b.gainDb.getCurrentValue() != b.coeffGainDb;
            if (paramsMoved || !wasActive || b.dirty) {
                updateCoefficients(b);
                b.dirty = false;
            }
        }
    }

    float processChain(int ch, float x) noexcept {
        float y = x;
        for (auto& b : bands_) {
            if (!b.active)
                continue;
            float wet = b.stage1.processSample(ch, y);
            if (b.twoStages)
                wet = b.stage2.processSample(ch, wet);
            // dry/wet crossfade: with mix = 1 this is just wet; when enabled is toggled, mix ramps over 20 ms
            y = (b.mixCurrent >= 1.0f) ? wet : y + b.mixCurrent * (wet - y);
        }
        return y;
    }

    std::array<Band, kNumBands> bands_;
    double sampleRate_ = 48000.0;
    double smoothingSeconds_ = kDefaultSmoothingSeconds;
    int numChannels_ = 0;
    int updateInterval_ = kDefaultUpdateInterval;
    int samplesUntilUpdate_ = 0; // How many samples remain until the next tick
    int samplesSinceTick_ = 0;   // How many samples have actually elapsed since the last tick (the amount the next tick advances the smoothers by)
};

} // namespace dawlab::dsp
