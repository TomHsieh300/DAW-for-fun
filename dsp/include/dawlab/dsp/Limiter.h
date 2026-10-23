#pragma once
// Limiter: a lookahead brickwall limiter (Chapter 05).
//
// Guarantee: for any input, the output sample peak is <= ceiling (linear value = dbToGain(ceilingDb)).
//
// Algorithm (per sample, stereo-linked; L = lookahead length in samples):
//
//   1. input gain      xs[n]  = x[n] * inputGain
//   2. required gain   g[n]   = min(1, ceiling / max_ch |xs_ch[n]|)
//   3. release         gr[n]  = min(g[n], gr[n-1] + (1 - gr[n-1]) * cRel)   (lets the gain only "recover slowly toward 1")
//   4. window minimum  m[n]   = min(gr[n-L], ..., gr[n])                      (sliding window min of length L+1)
//   5. moving average  s[n]   = (m[n-L] + ... + m[n]) / (L + 1)               (box filter of length L+1 = linear attack ramp)
//   6. output          y[n]   = xs[n-L] * s[n]                                (audio delayed by L samples)
//
// Why the output never exceeds the ceiling (full derivation in README section 2.5):
//   Every term m[n-k] (k = 0..L) averaged in step 5 is the minimum of a window that
//   "contains index n-L", so m[n-k] <= gr[n-L] <= g[n-L]; after averaging, s[n] <= g[n-L],
//   hence |y[n]| = |xs[n-L]| * s[n] <= |xs[n-L]| * g[n-L] <= ceiling.
//
// Call prepare() before use; the block passed to process() must not have more channels than the
// numChannels given to prepare() (Debug builds assert; Release builds clear the extra channels to
// 0 -- better silence than letting an unlimited signal through).
//
// Depends only on common/ and DelayLine.h; does not depend on JUCE.

#include <dawlab/AudioBuffer.h>
#include <dawlab/DspMath.h>
#include <dawlab/dsp/DelayLine.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

namespace dawlab::dsp {

// ---------------------------------------------------------------------------
// SlidingWindowMin: the minimum over a fixed-length window, done in O(1) amortized with a monotonic deque.
//
// Idea: the deque keeps only the elements that still have a chance to become the minimum --
// oldest to newest, with strictly increasing values. When a new value v arrives, older elements
// that are larger than (or equal to) v can never beat v again (they expire earlier), so we drop
// them. The front element is the current window minimum; it is popped only when it leaves the window.
// Each element is pushed at most once and popped at most once, so N samples cost O(N) in total,
// i.e. O(1) per sample on average.
//
// The deque is implemented as a pre-allocated circular array: push() does not allocate.
// ---------------------------------------------------------------------------
class SlidingWindowMin {
public:
    // Allocates; maxWindowLength >= 1
    void prepare(int maxWindowLength) {
        assert(maxWindowLength >= 1);
        capacity_ = maxWindowLength;
        values_.assign(static_cast<size_t>(capacity_), 0.0f);
        indices_.assign(static_cast<size_t>(capacity_), 0);
        window_ = capacity_;
        reset();
    }

    // Window length (1..maxWindowLength). Changing the length clears everything and starts accumulating again.
    void setWindowLength(int length) noexcept {
        window_ = std::clamp(length, 1, capacity_);
        reset();
    }

    int getWindowLength() const noexcept { return window_; }

    void reset() noexcept {
        head_ = 0;
        count_ = 0;
        sampleIndex_ = 0;
    }

    // Push a new value and return the minimum of "the most recent window values" (including this one).
    float push(float v) noexcept {
        const int64_t n = sampleIndex_++;

        // (a) Drop from the back every element >= v: they expire earlier than v and are no smaller, so they can never be the minimum again
        while (count_ > 0 && values_[static_cast<size_t>(back())] >= v)
            --count_;

        // (b) Drop the front if it has left the window [n - window + 1, n]
        while (count_ > 0 && indices_[static_cast<size_t>(head_)] <= n - window_) {
            head_ = (head_ + 1) % capacity_;
            --count_;
        }

        // (c) Put v at the back. The deque now holds <= window - 1 elements, so adding v cannot exceed capacity
        const int slot = (head_ + count_) % capacity_;
        values_[static_cast<size_t>(slot)] = v;
        indices_[static_cast<size_t>(slot)] = n;
        ++count_;

        return values_[static_cast<size_t>(head_)];
    }

private:
    int back() const noexcept { return (head_ + count_ - 1) % capacity_; }

    int capacity_ = 1;
    int window_ = 1;
    int head_ = 0;
    int count_ = 0;
    int64_t sampleIndex_ = 0;
    std::vector<float> values_;
    std::vector<int64_t> indices_;
};

// ---------------------------------------------------------------------------
// Limiter
// ---------------------------------------------------------------------------
class Limiter {
public:
    // Upper bound on lookahead (it decides how much memory prepare allocates). Call it before prepare; it takes effect at the next prepare. Default 20 ms.
    void setMaxLookaheadMs(float ms) noexcept { maxLookaheadMs_ = std::max(0.0f, ms); }

    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        assert(sampleRate > 0.0 && numChannels >= 1);
        (void)maxBlockSize; // this component works sample by sample and needs no block-size-dependent scratch memory
        sampleRate_ = sampleRate;
        numChannels_ = numChannels;
        preparedMaxLookaheadMs_ = maxLookaheadMs_;
        maxLookaheadSamples_ = std::max(0, msToSamples(maxLookaheadMs_, sampleRate));

        delay_.prepare(maxLookaheadSamples_, numChannels);
        windowMin_.prepare(maxLookaheadSamples_ + 1);
        averageRing_.assign(static_cast<size_t>(maxLookaheadSamples_ + 1), 1.0f);

        updateDerived();
        applyLookahead();
        reset();
    }

    void reset() noexcept {
        delay_.reset();
        windowMin_.reset();
        std::fill(averageRing_.begin(), averageRing_.end(), 1.0f);
        averageSum_ = static_cast<double>(averageLength_);
        averageIndex_ = 0;
        releasedGain_ = 1.0f;
        currentInputGain_ = targetInputGain_;
        gainReductionDb_.store(0.0f, std::memory_order_relaxed);
        safetyClipCount_.store(0, std::memory_order_relaxed);
    }

    // ---- Parameters (call between blocks) -------------------------------------------
    void setCeilingDb(float db) noexcept {
        ceilingDb_ = db;
        ceiling_ = dbToGain(db);
    }

    // The input gain moves to the new value with a linear ramp of about 20 ms, avoiding zipper noise.
    // If the value is unchanged we return immediately: a host (for example the Chapter 13 plugin)
    // often re-sets the parameter every block, and if we recomputed the step from the "remaining
    // distance" each time, the ramp would get slower and slower and never finish.
    void setInputGainDb(float db) noexcept {
        if (db == inputGainDb_)
            return;
        inputGainDb_ = db;
        targetInputGain_ = dbToGain(db);
        const double rampSamples = std::max(1.0, 0.02 * sampleRate_);
        inputGainRampStep_ = static_cast<float>(std::abs(targetInputGain_ - currentInputGain_) / rampSamples);
    }

    void setReleaseMs(float ms) noexcept {
        ms = std::max(0.0f, ms);
        if (ms == releaseMs_)
            return; // unchanged: no need to recompute exp() (the host may call this every block)
        releaseMs_ = ms;
        updateDerived();
    }

    // Effective lookahead = min(ms, upper bound), negative values count as 0; the actual number
    // of samples in effect is given by getLatencySamples().
    // Here we only remember the value the user asked for; the upper bound is applied at the point
    // of use, so the result is independent of the call order of setMaxLookaheadMs() / prepare()
    // (for example, setLookaheadMs(30) followed by setMaxLookaheadMs(50) gives 30 ms after prepare).
    // Changing the lookahead resets the gain-smoothing state (the window length changed), so do it while playback is stopped.
    void setLookaheadMs(float ms) noexcept {
        requestedLookaheadMs_ = std::max(0.0f, ms);
        if (sampleRate_ <= 0.0)
            return; // not prepared yet: remember it and apply it in prepare
        updateDerived();
        applyLookahead();
    }

    float getCeilingDb() const noexcept { return ceilingDb_; }
    float getInputGainDb() const noexcept { return inputGainDb_; }
    float getReleaseMs() const noexcept { return releaseMs_; }
    // The lookahead currently in effect (clamped to [0, upper bound]); after prepare it agrees with getLatencySamples()
    float getLookaheadMs() const noexcept { return std::min(requestedLookaheadMs_, effectiveMaxLookaheadMs()); }
    // The value set by setMaxLookaheadMs() (the next prepare allocates memory with it)
    float getMaxLookaheadMs() const noexcept { return maxLookaheadMs_; }

    // Latency = the lookahead length in samples. A plugin host uses this value for plugin delay compensation.
    int getLatencySamples() const noexcept { return lookaheadSamples_; }

    // The largest gain reduction within the previous block (positive dB; 6 means 6 dB of reduction). Thread-safe.
    float getGainReductionDb() const noexcept { return gainReductionDb_.load(std::memory_order_relaxed); }

    // How many times the last safety clamp caught a sample that "clearly exceeded the ceiling"
    // (relative error > 1e-6). In theory this is always 0; it becomes > 0 only if the ceiling /
    // lookahead is changed during playback, or the input contains NaN / Inf.
    // Used as a debugging indicator (for example in a fuzz test).
    uint64_t getSafetyClipCount() const noexcept { return safetyClipCount_.load(std::memory_order_relaxed); }

    // ---- Processing -----------------------------------------------------------------
    void process(AudioBlock block) noexcept {
        assert(numChannels_ > 0 && "Limiter::prepare() not called");
        assert(block.getNumChannels() <= numChannels_ && "block has more channels than prepare()");
        const int numSamples = block.getNumSamples();
        const int numChannels = std::min(block.getNumChannels(), numChannels_);

        // Channels beyond what prepare() set up have no delay line and take no part in peak
        // detection, so we cannot guarantee <= ceiling for them: clear them to 0 (if prepare()
        // was never called, numChannels_ = 0, which clears the whole block).
        if (block.getNumChannels() > numChannels)
            block.getChannelSubset(numChannels, block.getNumChannels() - numChannels).clear();
        if (numChannels <= 0)
            return;

        const int L = lookaheadSamples_;
        const float ceiling = ceiling_;
        const float ceilingTolerance = ceiling * (1.0f + 1.0e-6f); // an overshoot at the level of floating-point rounding is not a violation
        const float cRel = releaseCoeff_;
        const double invLen = 1.0 / static_cast<double>(averageLength_);
        float minSmoothedGain = 1.0f;
        uint64_t clips = 0;

        for (int i = 0; i < numSamples; ++i) {
            // (1) Input gain (linear ramp) -> push into the delay line -> take |peak| over all channels (stereo link)
            if (currentInputGain_ != targetInputGain_) {
                if (std::abs(targetInputGain_ - currentInputGain_) <= inputGainRampStep_)
                    currentInputGain_ = targetInputGain_;
                else
                    currentInputGain_ += (targetInputGain_ > currentInputGain_) ? inputGainRampStep_ : -inputGainRampStep_;
            }
            float peak = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch) {
                const float xs = block.getSample(ch, i) * currentInputGain_;
                delay_.push(ch, xs);
                peak = std::max(peak, std::abs(xs)); // max ignores NaN; the safety clamp deals with it later
            }

            // (2) The gain this sample needs: g = min(1, ceiling / peak)
            const float g = (peak > ceiling) ? ceiling / peak : 1.0f;

            // (3) Release: upward, the gain can only return to 1 at one-pole speed; downward, it follows instantly
            float released = releasedGain_ + (1.0f - releasedGain_) * cRel;
            if (released > 1.0f - 1.0e-6f) // once close enough to 1, snap to 1 so it never stays a hair short
                released = 1.0f;
            releasedGain_ = std::min(g, released);

            // (4) Minimum within the lookahead window (length L+1)
            const float m = windowMin_.push(releasedGain_);

            // (5) Moving average of length L+1 -> linear attack ramp; by the time the peak reaches the output, the gain has just reached its target
            float& oldest = averageRing_[static_cast<size_t>(averageIndex_)];
            averageSum_ += static_cast<double>(m) - static_cast<double>(oldest);
            oldest = m;
            if (++averageIndex_ >= averageLength_)
                averageIndex_ = 0;
            // s = the average itself (README 2.4 step 4). Every term of the average lies in [0, 1], so s does too;
            // the clamp only guards against floating-point rounding of the running sum (accumulated in double,
            // so the error is far below the safety clamp's 1e-6 tolerance).
            // Note: the lower bound must not be m -- during release gr keeps rising, so m[n] = gr[n-L] is
            // actually larger than the average; using m as a lower bound would remove the box smoothing on the
            // release side, and with a very short release the gain would jump back in one sample (a click).
            const float s = std::clamp(static_cast<float>(averageSum_ * invLen), 0.0f, 1.0f);
            minSmoothedGain = std::min(minSmoothedGain, s);

            // (6) Output = delayed audio x smoothed gain; the safety clamp is the last line of defense
            for (int ch = 0; ch < numChannels; ++ch) {
                float y = delay_.read(ch, L) * s;
                if (!(std::abs(y) <= ceiling)) { // this also catches NaN (every comparison with NaN is false)
                    if (!(std::abs(y) <= ceilingTolerance))
                        ++clips;
                    y = std::isnan(y) ? 0.0f : std::clamp(y, -ceiling, ceiling);
                }
                block.setSample(ch, i, y);
            }
        }

        // gainToDb(1) = 0 -> negating gives -0.0, which printf prints as "-0.00"; max turns it back into +0.0
        gainReductionDb_.store(std::max(0.0f, -gainToDb(minSmoothedGain)), std::memory_order_relaxed);
        if (clips > 0)
            safetyClipCount_.fetch_add(clips, std::memory_order_relaxed);
    }

private:
    void updateDerived() noexcept {
        if (sampleRate_ <= 0.0)
            return;
        lookaheadSamples_ = std::clamp(msToSamples(getLookaheadMs(), sampleRate_), 0, maxLookaheadSamples_);
        averageLength_ = lookaheadSamples_ + 1;
        // One-pole release: each sample covers (1 - exp(-1/(tau fs))) of the remaining distance, tau = releaseMs
        const double tauSamples = static_cast<double>(releaseMs_) * 0.001 * sampleRate_;
        releaseCoeff_ = tauSamples > 0.0 ? static_cast<float>(1.0 - std::exp(-1.0 / tauSamples)) : 1.0f;
    }

    // After prepare, the upper bound is whatever prepare allocated (a later setMaxLookaheadMs() call only takes effect at the next prepare)
    float effectiveMaxLookaheadMs() const noexcept {
        return sampleRate_ > 0.0 ? preparedMaxLookaheadMs_ : maxLookaheadMs_;
    }

    // The window length changed: both the min window and the moving average start accumulating from scratch (assuming all earlier gains were 1)
    void applyLookahead() noexcept {
        windowMin_.setWindowLength(averageLength_);
        std::fill(averageRing_.begin(), averageRing_.end(), 1.0f);
        averageSum_ = static_cast<double>(averageLength_);
        averageIndex_ = 0;
    }

    // Parameters
    float maxLookaheadMs_ = 20.0f;         // the value of setMaxLookaheadMs() (used by the next prepare)
    float preparedMaxLookaheadMs_ = 20.0f; // the upper bound actually allocated by the last prepare
    float requestedLookaheadMs_ = 5.0f;    // the value requested by setLookaheadMs() (>= 0, upper bound not yet applied)
    float ceilingDb_ = -1.0f;
    float ceiling_ = dbToGain(-1.0f);
    float inputGainDb_ = 0.0f;
    float targetInputGain_ = 1.0f;
    float releaseMs_ = 100.0f;

    // Quantities derived from the parameters
    double sampleRate_ = 0.0;
    int numChannels_ = 0;
    int maxLookaheadSamples_ = 0;
    int lookaheadSamples_ = 0;
    int averageLength_ = 1;
    float releaseCoeff_ = 1.0f;
    float inputGainRampStep_ = 1.0f;

    // State
    DelayLine delay_;
    SlidingWindowMin windowMin_;
    std::vector<float> averageRing_;
    double averageSum_ = 1.0;
    int averageIndex_ = 0;
    float releasedGain_ = 1.0f;
    float currentInputGain_ = 1.0f;

    // Values for the UI / debugging to read
    std::atomic<float> gainReductionDb_{0.0f};
    std::atomic<uint64_t> safetyClipCount_{0};
};

} // namespace dawlab::dsp
