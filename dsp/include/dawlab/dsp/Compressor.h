#pragma once
// Chapter 04: Compressor (feedforward, log-domain smoothing).
//
// Signal flow (per sample):
//
//   sidechain x ──► detector ──► 20·log10 ──► gain computer ──► attack/release ──► 10^(-GR/20) ──┐
//                 (|x| or RMS)    (dB)        (static curve)     (smooth GR, dB)    (linear gain)   │
//   main x ───────────────────────────────────────────────────────────────────────────────────► × ──► × makeup ──► y
//
// Design basis: Giannoulis, Massberg & Reiss, "Digital Dynamic Range Compressor Design—A Tutorial
// and Analysis", JAES 60(6), 2012. The key points:
//   - the gain computer's input is the dB value of the "instantaneous" level (Peak mode) or the RMS level;
//   - attack / release smoothing sits after the gain computer and is done in the log (dB) domain (the structure
//     recommended in Fig. 7 of the paper), which makes the attack / release times independent of the threshold and
//     ratio, and makes the release curve an exponential decay in dB, which sounds the most natural;
//   - Decoupled smoothing is the default (see EnvelopeFollower.h), which tracks the peak of periodic signals exactly.
//
// Depends only on common/ and this chapter's EnvelopeFollower.h.

#include <dawlab/AudioBuffer.h>
#include <dawlab/DspMath.h>
#include <dawlab/dsp/EnvelopeFollower.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

namespace dawlab::dsp {

class Compressor {
public:
    Compressor() = default;

    // A std::atomic member makes the class non-copyable by default; define these by hand so that it can be put into a
    // std::vector / std::array (needed by the 8 tracks of chapter 14)
    Compressor(const Compressor& other) { copyFrom(other); }
    Compressor& operator=(const Compressor& other) {
        if (this != &other)
            copyFrom(other);
        return *this;
    }

    // ---- Standard interface ------------------------------------------------
    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        sampleRate_ = sampleRate;
        maxBlockSize_ = maxBlockSize;
        channels_.resize(static_cast<size_t>(std::max(1, numChannels)));
        for (auto& ch : channels_) {
            ch.rmsDetector.prepare(sampleRate);
            ch.rmsDetector.setMode(DetectorMode::Rms);
            ch.gainSmoother.prepare(sampleRate);
        }
        // Smooth the makeup gain with a one-pole of about 10 ms to avoid zipper noise when the knob is turned
        makeupSmoothCoeff_ = 1.0f - attackReleaseCoefficient(10.0, sampleRate);
        updateTimeConstants();
        reset();
    }

    void reset() noexcept {
        for (auto& ch : channels_) {
            ch.rmsDetector.reset();
            ch.gainSmoother.reset();
        }
        makeupGainSmoothed_ = dbToGain(makeupGainDb_);
        gainReductionDb_.store(0.0f, std::memory_order_relaxed);
    }

    // ---- Parameters --------------------------------------------------------
    void setThresholdDb(float db) noexcept { thresholdDb_ = db; }
    void setRatio(float ratio) noexcept { ratio_ = std::max(1.0f, ratio); } // 1:1 = no compression
    void setKneeDb(float kneeDb) noexcept { kneeDb_ = std::max(0.0f, kneeDb); } // 0 = hard knee
    void setAttackMs(float ms) noexcept {
        attackMs_ = std::max(0.0f, ms);
        updateTimeConstants();
    }
    void setReleaseMs(float ms) noexcept {
        releaseMs_ = std::max(0.0f, ms);
        updateTimeConstants();
    }
    void setMakeupGainDb(float db) noexcept { makeupGainDb_ = db; }
    // Toggling the link is safe (no click): in linked mode every channel still runs its own GR smoother, they just
    // all take the "same" GR target. So at the moment of switching, every channel starts from its own current GR and
    // moves smoothly toward the new target at the attack / release speed (see the comments in process()).
    void setStereoLink(bool linked) noexcept { stereoLink_ = linked; }
    void setDetectorMode(DetectorMode mode) noexcept { detectorMode_ = mode; }
    // Averaging window for RMS mode (attack = release = window)
    void setRmsWindowMs(float ms) noexcept {
        rmsWindowMs_ = std::max(0.0f, ms);
        updateTimeConstants();
    }
    void setSmoothingMode(SmoothingMode mode) noexcept {
        smoothingMode_ = mode;
        for (auto& ch : channels_)
            ch.gainSmoother.setMode(mode);
    }

    float getThresholdDb() const noexcept { return thresholdDb_; }
    float getRatio() const noexcept { return ratio_; }
    float getKneeDb() const noexcept { return kneeDb_; }
    float getAttackMs() const noexcept { return attackMs_; }
    float getReleaseMs() const noexcept { return releaseMs_; }
    float getMakeupGainDb() const noexcept { return makeupGainDb_; }
    bool getStereoLink() const noexcept { return stereoLink_; }
    DetectorMode getDetectorMode() const noexcept { return detectorMode_; }
    float getRmsWindowMs() const noexcept { return rmsWindowMs_; }
    SmoothingMode getSmoothingMode() const noexcept { return smoothingMode_; }
    int getLatencySamples() const noexcept { return 0; } // feedforward, no lookahead → zero latency

    // The current gain reduction (positive dB; e.g. 6 means it is currently squashing by 6 dB).
    // Written by the audio thread and read by the UI thread: std::atomic with relaxed ordering is enough, no lock needed.
    float getGainReductionDb() const noexcept { return gainReductionDb_.load(std::memory_order_relaxed); }

    // ---- Static gain computer (transfer curve) -----------------------------
    // Takes an input level x (dB) and returns the output level y (dB). Giannoulis et al. 2012, eq. (4), W = knee width:
    //
    //            x                                   if 2(x-T) < -W         (below the knee: untouched)
    //   y(x) =   x + (1/R - 1)·(x - T + W/2)² / (2W)  if 2|x-T| <= W         (inside the knee: quadratic)
    //            T + (x - T)/R                        if 2(x-T) > W          (above the knee: slope 1/R)
    //
    // W = 0 degenerates to a hard knee: y = x (x <= T), y = T + (x-T)/R (x > T).
    static float computeOutputDb(float inputDb, float thresholdDb, float ratio, float kneeDb) noexcept {
        ratio = std::max(1.0f, ratio);
        kneeDb = std::max(0.0f, kneeDb);
        const float over = inputDb - thresholdDb; // x - T
        if (kneeDb <= 0.0f) // Hard knee: spelled out explicitly so the /(2W) below never meets W = 0 (also avoids MSVC's C4723)
            return over > 0.0f ? thresholdDb + over / ratio : inputDb;
        if (2.0f * over <= -kneeDb)
            return inputDb;
        if (2.0f * over >= kneeDb)
            return thresholdDb + over / ratio;
        const float t = over + 0.5f * kneeDb; // 0 .. W, measured from the lower edge of the knee
        return inputDb + (1.0f / ratio - 1.0f) * t * t / (2.0f * kneeDb);
    }

    // gain reduction = input dB - output dB (positive)
    static float computeGainReductionDb(float inputDb, float thresholdDb, float ratio, float kneeDb) noexcept {
        return inputDb - computeOutputDb(inputDb, thresholdDb, ratio, kneeDb);
    }

    // ---- Processing --------------------------------------------------------
    // Use itself as the sidechain (the usual case)
    void process(AudioBlock block) noexcept { process(block, block); }

    // External sidechain: the detector looks at the sidechain, and the gain is applied to the block.
    // The sidechain may have fewer channels than the block (e.g. a mono sidechain controlling a stereo signal); the last channel is reused.
    void process(AudioBlock block, const AudioBlock& sidechain) noexcept {
        const int numCh = std::min(block.getNumChannels(), static_cast<int>(channels_.size()));
        const int numSamples = block.getNumSamples();
        const int scCh = sidechain.getNumChannels();
        if (numCh <= 0 || scCh <= 0)
            return;

        const float makeupTarget = dbToGain(makeupGainDb_);
        float blockMaxGr = 0.0f;

        for (int i = 0; i < numSamples; ++i) {
            // Makeup gain smoothing: g += c·(target - g)
            makeupGainSmoothed_ += makeupSmoothCoeff_ * (makeupTarget - makeupGainSmoothed_);

            if (stereoLink_) {
                // Stereo link: all channels share the "largest level" to compute one GR target, so the stereo image does not drift left or right.
                // Every channel's smoother takes this shared target: at steady state everyone's GR is identical;
                // and right after switching from unlinked, each channel converges smoothly from its own previous GR, with no instantaneous jump.
                // (If only channel 0's smoother ran, the other channels' smoothers would freeze at their old values,
                //  and switching back to unlinked later would make the gain suddenly jump back to that old value → click.)
                float level = 0.0f;
                for (int ch = 0; ch < numCh; ++ch) {
                    const float sc = sidechain.getSample(std::min(ch, scCh - 1), i);
                    level = std::max(level, detect(channels_[static_cast<size_t>(ch)], sc));
                }
                const float grTarget = computeGainReductionTarget(level);
                for (int ch = 0; ch < numCh; ++ch) {
                    const float gr = channels_[static_cast<size_t>(ch)].gainSmoother.process(grTarget);
                    blockMaxGr = std::max(blockMaxGr, gr);
                    block.getChannelPointer(ch)[i] *= dbToGain(-gr) * makeupGainSmoothed_;
                }
            } else {
                // Channels are independent: each has its own detector and smoother
                for (int ch = 0; ch < numCh; ++ch) {
                    auto& state = channels_[static_cast<size_t>(ch)];
                    const float sc = sidechain.getSample(std::min(ch, scCh - 1), i);
                    const float gr = smoothGainReduction(state, detect(state, sc));
                    blockMaxGr = std::max(blockMaxGr, gr);
                    block.getChannelPointer(ch)[i] *= dbToGain(-gr) * makeupGainSmoothed_;
                }
            }
        }
        gainReductionDb_.store(blockMaxGr, std::memory_order_relaxed);
    }

private:
    struct ChannelState {
        EnvelopeFollower rmsDetector;      // Used only in Rms mode
        AttackReleaseFilter gainSmoother;  // Smooths the gain reduction in the dB domain
    };

    // Detector: returns the linear level (Peak = instantaneous |x|; Rms = moving-average RMS)
    float detect(ChannelState& state, float x) noexcept {
        if (detectorMode_ == DetectorMode::Peak)
            return std::abs(x);
        return state.rmsDetector.processSample(x);
    }

    // level (linear) → dB → static GR → GR after attack/release smoothing (dB, positive)
    float smoothGainReduction(ChannelState& state, float level) noexcept {
        return state.gainSmoother.process(computeGainReductionTarget(level));
    }

    // level (linear) → dB → static GR target (dB, positive, not yet smoothed)
    float computeGainReductionTarget(float level) const noexcept {
        // Non-finite guard: with level = Inf, gainToDb returns Inf and the static curve computes Inf - Inf = NaN,
        // so the smoother gets stuck at NaN and dbToGain(NaN) = 0 → the output is silenced permanently. A bad level is treated as 0 (no compression).
        if (!std::isfinite(level))
            level = 0.0f;
        const float levelDb = gainToDb(level); // 0 → -100 dB, so the GR is naturally 0
        return computeGainReductionDb(levelDb, thresholdDb_, ratio_, kneeDb_);
    }

    void copyFrom(const Compressor& o) {
        sampleRate_ = o.sampleRate_;
        maxBlockSize_ = o.maxBlockSize_;
        channels_ = o.channels_;
        thresholdDb_ = o.thresholdDb_;
        ratio_ = o.ratio_;
        kneeDb_ = o.kneeDb_;
        attackMs_ = o.attackMs_;
        releaseMs_ = o.releaseMs_;
        makeupGainDb_ = o.makeupGainDb_;
        stereoLink_ = o.stereoLink_;
        detectorMode_ = o.detectorMode_;
        rmsWindowMs_ = o.rmsWindowMs_;
        smoothingMode_ = o.smoothingMode_;
        makeupGainSmoothed_ = o.makeupGainSmoothed_;
        makeupSmoothCoeff_ = o.makeupSmoothCoeff_;
        gainReductionDb_.store(o.gainReductionDb_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }

    void updateTimeConstants() noexcept {
        for (auto& ch : channels_) {
            ch.gainSmoother.setAttackMs(attackMs_);
            ch.gainSmoother.setReleaseMs(releaseMs_);
            ch.gainSmoother.setMode(smoothingMode_);
            ch.rmsDetector.setAttackMs(rmsWindowMs_);
            ch.rmsDetector.setReleaseMs(rmsWindowMs_);
        }
    }

    double sampleRate_ = 48000.0;
    int maxBlockSize_ = 0;
    std::vector<ChannelState> channels_;

    float thresholdDb_ = -20.0f;
    float ratio_ = 4.0f;
    float kneeDb_ = 6.0f;
    float attackMs_ = 10.0f;
    float releaseMs_ = 100.0f;
    float makeupGainDb_ = 0.0f;
    bool stereoLink_ = true;
    DetectorMode detectorMode_ = DetectorMode::Peak;
    float rmsWindowMs_ = 10.0f;
    SmoothingMode smoothingMode_ = SmoothingMode::Decoupled;

    float makeupGainSmoothed_ = 1.0f;
    float makeupSmoothCoeff_ = 1.0f;
    std::atomic<float> gainReductionDb_{0.0f};
};

} // namespace dawlab::dsp
