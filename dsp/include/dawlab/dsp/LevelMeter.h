#pragma once
// Chapter 11: LevelMeter -- sample peak (with hold and dB/s decay) and sliding-window RMS.
//
// One meter serves several channels (each channel has independent state). process() runs on the audio
// thread and writes its measurements into std::atomic<float> (relaxed); the UI thread can read them at
// any time through getXxxDb().
//
// Three readings (all in dBFS, silence = kMinusInfinityDb = -100):
// - peak:      instant attack (jumps up immediately when a new |x| exceeds the current value), then falls
//              at a fixed dB/s rate (ballistics).
// - peakHold:  stays at the most recent maximum for the hold time, then falls at the same rate (never
//              below peak).
// - rms:       sliding-window RMS over rmsWindowMs = sqrt( (1/W) Σ x[n]^2 ).
//
// A dB/s decay in the linear domain is a fixed multiplier applied once per sample:
//     g = 10^( -decayDbPerSecond / (20 · fs) )
// because 20·log10(g^fs) = -decayDbPerSecond.

#include <dawlab/AudioBuffer.h>
#include <dawlab/DspMath.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <memory>
#include <vector>

namespace dawlab::dsp {

class LevelMeter {
public:
    static constexpr float kMaxRmsWindowMs = 3000.0f; // the ring buffer is sized from this in prepare()

    // The only place that may allocate
    void prepare(double sampleRate, int numChannels) {
        assert(sampleRate > 0.0 && numChannels > 0);
        sampleRate_ = sampleRate;
        numChannels_ = numChannels;
        maxRmsSamples_ = std::max(1, msToSamples(kMaxRmsWindowMs, sampleRate));
        channels_.assign(static_cast<size_t>(numChannels), ChannelState{});
        for (ChannelState& c : channels_)
            c.rmsRing.assign(static_cast<size_t>(maxRmsSamples_), 0.0f);
        peakDb_ = std::make_unique<std::atomic<float>[]>(static_cast<size_t>(numChannels));
        holdDb_ = std::make_unique<std::atomic<float>[]>(static_cast<size_t>(numChannels));
        rmsDb_ = std::make_unique<std::atomic<float>[]>(static_cast<size_t>(numChannels));
        updateCoefficients();
        reset();
    }

    void reset() noexcept {
        for (ChannelState& c : channels_) {
            c.peak = 0.0f;
            c.hold = 0.0f;
            c.holdCounter = 0;
            c.sumSquares = 0.0;
            c.ringPos = 0;
            std::fill(c.rmsRing.begin(), c.rmsRing.end(), 0.0f);
        }
        for (size_t ch = 0; ch < static_cast<size_t>(numChannels_); ++ch) {
            peakDb_[ch].store(kMinusInfinityDb, std::memory_order_relaxed);
            holdDb_[ch].store(kMinusInfinityDb, std::memory_order_relaxed);
            rmsDb_[ch].store(kMinusInfinityDb, std::memory_order_relaxed);
        }
        rmsWindowChanged_.store(false, std::memory_order_relaxed);
        rmsSamples_ = pendingRmsSamples_.load(std::memory_order_relaxed);
    }

    // Hold time; < 0 means infinite hold (until resetPeakHold()). Default 1000 ms.
    void setPeakHoldMs(float ms) noexcept {
        peakHoldMs_ = ms;
        updateCoefficients();
    }

    // Fall rate of peak / hold, default 24 dB/s
    void setDecayDbPerSecond(float dbPerSecond) noexcept {
        decayDbPerSecond_ = std::max(0.0f, dbPerSecond);
        updateCoefficients();
    }

    // RMS sliding-window length (capped at kMaxRmsWindowMs), default 300 ms. Changing it clears the window
    // contents so they re-accumulate.
    void setRmsWindowMs(float ms) noexcept {
        rmsWindowMs_ = std::clamp(ms, 1.0f, kMaxRmsWindowMs);
        updateCoefficients();
    }

    int getNumChannels() const noexcept { return numChannels_; }
    float getPeakHoldMs() const noexcept { return peakHoldMs_; }
    float getDecayDbPerSecond() const noexcept { return decayDbPerSecond_; }
    float getRmsWindowMs() const noexcept { return rmsWindowMs_; }

    // Audio thread: no allocation, no locks
    void process(const AudioBlock& block) noexcept {
        applyPendingRequests();
        if (rmsWindowChanged_.exchange(false, std::memory_order_relaxed)) {
            // The window length changed: clear the ring and re-accumulate (RMS reads low for a short while)
            rmsSamples_ = pendingRmsSamples_.load(std::memory_order_relaxed);
            for (ChannelState& c : channels_) {
                std::fill(c.rmsRing.begin(), c.rmsRing.end(), 0.0f);
                c.sumSquares = 0.0;
                c.ringPos = 0;
            }
        }
        const int nch = std::min(numChannels_, block.getNumChannels());
        const int n = block.getNumSamples();
        for (int ch = 0; ch < nch; ++ch) {
            ChannelState& c = channels_[static_cast<size_t>(ch)];
            const float* x = block.getChannelPointer(ch);
            for (int i = 0; i < n; ++i) {
                const float a = std::abs(x[i]);

                // --- peak: instant attack, fixed dB/s release ---
                if (a > c.peak)
                    c.peak = a;
                else
                    c.peak *= decayPerSample_;

                // --- peak hold ---
                if (a >= c.hold) {
                    c.hold = a;
                    c.holdCounter = holdSamples_;
                } else if (holdSamples_ < 0) {
                    // infinite hold: do nothing
                } else if (c.holdCounter > 0) {
                    --c.holdCounter;
                } else {
                    c.hold = std::max(c.hold * decayPerSample_, c.peak);
                }

                // --- Sliding-window RMS: the ring buffer stores x^2 and we maintain the sum over the window ---
                const float sq = a * a;
                float& slot = c.rmsRing[static_cast<size_t>(c.ringPos)];
                c.sumSquares += static_cast<double>(sq) - static_cast<double>(slot);
                slot = sq;
                if (++c.ringPos >= rmsSamples_) {
                    c.ringPos = 0;
                    // Re-sum exactly once per trip around the ring to remove accumulated floating-point error
                    // (still O(1) per sample amortized)
                    double exact = 0.0;
                    for (int j = 0; j < rmsSamples_; ++j)
                        exact += c.rmsRing[static_cast<size_t>(j)];
                    c.sumSquares = exact;
                }
            }
            // Flush tiny values to 0 to avoid denormals and a reading that never reaches -inf
            if (c.peak < 1e-10f)
                c.peak = 0.0f;
            if (c.hold < 1e-10f)
                c.hold = 0.0f;
            if (c.sumSquares < 0.0)
                c.sumSquares = 0.0;

            const double meanSquare = c.sumSquares / static_cast<double>(rmsSamples_);
            peakDb_[static_cast<size_t>(ch)].store(gainToDb(c.peak), std::memory_order_relaxed);
            holdDb_[static_cast<size_t>(ch)].store(gainToDb(c.hold), std::memory_order_relaxed);
            rmsDb_[static_cast<size_t>(ch)].store(gainToDb(static_cast<float>(std::sqrt(meanSquare))),
                                                  std::memory_order_relaxed);
        }
    }

    // The getters below are thread-safe (callable from any thread)
    float getPeakDb(int ch) const noexcept { return load(peakDb_.get(), ch); }
    float getRmsDb(int ch) const noexcept { return load(rmsDb_.get(), ch); }
    float getPeakHoldDb(int ch) const noexcept { return load(holdDb_.get(), ch); }

    // Clear the hold (e.g. the user clicked the meter). This is normally called from the UI thread, so it
    // does not touch audio-thread state directly; it sets an atomic flag and lets the audio thread clear
    // it on the next process() call.
    void resetPeakHold() noexcept { resetHoldRequested_.store(true, std::memory_order_relaxed); }

    // Convert dB/s into a per-sample linear multiplier (public so exercises and examples can check it)
    static float decayCoefficient(float dbPerSecond, double sampleRate) noexcept {
        return static_cast<float>(std::pow(10.0, -static_cast<double>(dbPerSecond) / (20.0 * sampleRate)));
    }

private:
    struct ChannelState {
        float peak = 0.0f;
        float hold = 0.0f;
        int holdCounter = 0;
        double sumSquares = 0.0;
        int ringPos = 0;
        std::vector<float> rmsRing;
    };

    float load(const std::atomic<float>* v, int ch) const noexcept {
        if (v == nullptr || ch < 0 || ch >= numChannels_)
            return kMinusInfinityDb;
        return v[ch].load(std::memory_order_relaxed);
    }

    // Audio thread: handle requests sent from the UI thread
    void applyPendingRequests() noexcept {
        if (resetHoldRequested_.exchange(false, std::memory_order_relaxed))
            for (ChannelState& c : channels_) {
                c.hold = c.peak;
                c.holdCounter = 0;
            }
    }

    void updateCoefficients() noexcept {
        if (sampleRate_ <= 0.0)
            return;
        decayPerSample_ = decayCoefficient(decayDbPerSecond_, sampleRate_);
        holdSamples_ = peakHoldMs_ < 0.0f ? -1 : msToSamples(peakHoldMs_, sampleRate_);
        const int wanted = std::clamp(msToSamples(rmsWindowMs_, sampleRate_), 1, maxRmsSamples_);
        if (wanted != pendingRmsSamples_.load(std::memory_order_relaxed)) {
            pendingRmsSamples_.store(wanted, std::memory_order_relaxed);
            rmsWindowChanged_.store(true, std::memory_order_relaxed);
        }
    }

    double sampleRate_ = 0.0;
    int numChannels_ = 0;
    int maxRmsSamples_ = 1;
    float peakHoldMs_ = 1000.0f;
    float decayDbPerSecond_ = 24.0f;
    float rmsWindowMs_ = 300.0f;
    float decayPerSample_ = 1.0f;
    int holdSamples_ = 0;
    int rmsSamples_ = 1;
    std::atomic<int> pendingRmsSamples_{1};
    std::atomic<bool> rmsWindowChanged_{false};

    std::vector<ChannelState> channels_;
    std::unique_ptr<std::atomic<float>[]> peakDb_, holdDb_, rmsDb_;
    std::atomic<bool> resetHoldRequested_{false};
};

} // namespace dawlab::dsp
