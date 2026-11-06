#pragma once
// EffectBase: the boilerplate shared by every InsertEffect adapter (Step 5), plus
// PureDelayEffect, a tiny "latency only" insert used to test plugin delay compensation.
//
// An adapter has to do four things: own a DSP object, describe its parameters, forward
// parameter values to the right setter, and forward prepare / reset / process. EffectBase
// takes care of parameter storage and thread hand-over, so each adapter only writes
//
//   - a static table of ParameterInfo (name, unit, range, default, kind)
//   - applyParameter(index, value): one switch that calls the DSP setter
//   - prepare(): dsp_.prepare(...) followed by finishPrepare(sampleRate)
//   - processDsp(block): dsp_.process(block)
//
// How a knob reaches the DSP object without locks
//
//   UI thread                                   audio thread
//   setParameter(i, v)                          process(block)
//     values_[i] = clamp(v)   (atomic store)      mask = dirty_.exchange(0)
//     dirty_ |= 1 << i        (atomic RMW)        for each set bit i:
//                                                   applyParameter(i, values_[i])
//                                                 processDsp(block)
//
// The DSP object is therefore only ever touched by one thread at a time: the setup thread
// before the effect is handed to the engine, the audio thread afterwards. With at most 32
// parameters one uint32_t is the whole "what changed" set; several changes of the same
// parameter between two blocks collapse into one (only the newest value matters).
//
// Values set before prepare() are remembered and pushed into the DSP object by
// finishPrepare(), so "create -> configure -> hand to the engine" works in any order.

#include <dawlab/AudioBuffer.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

#include "InsertEffect.h"

namespace dawlab::engine {

constexpr int kMaxEffectParameters = 32; // ParametricEqEffect needs 30 (6 bands x 5)

class EffectBase : public InsertEffect {
public:
    int getNumParameters() const noexcept override { return numParameters_; }

    ParameterInfo getParameterInfo(int index) const noexcept override {
        return isValid(index) ? infos_[index] : ParameterInfo{};
    }

    float getParameter(int index) const noexcept override {
        return isValid(index) ? values_[static_cast<size_t>(index)].load(std::memory_order_relaxed) : 0.0f;
    }

    // Any thread. Clamps (and rounds Int / Bool / Choice), stores, marks the parameter dirty.
    void setParameter(int index, float value) noexcept override {
        if (!isValid(index))
            return;
        values_[static_cast<size_t>(index)].store(infos_[index].clamp(value), std::memory_order_relaxed);
        dirty_.fetch_or(1u << static_cast<unsigned>(index), std::memory_order_release);
    }

    // Audio thread (or setup code). Cheap when nothing changed: one atomic exchange.
    void applyPendingParameters() noexcept override {
        if (!prepared_)
            return; // stays dirty; finishPrepare() will apply everything
        uint32_t mask = dirty_.exchange(0u, std::memory_order_acquire);
        for (int i = 0; mask != 0u; ++i, mask >>= 1u)
            if (mask & 1u)
                applyParameter(i, values_[static_cast<size_t>(i)].load(std::memory_order_relaxed));
    }

    // Audio thread. Final: adapters implement processDsp() instead.
    void process(AudioBlock block) noexcept final {
        applyPendingParameters();
        processDsp(block);
    }

    double getSampleRate() const noexcept { return sampleRate_; }
    bool isPrepared() const noexcept { return prepared_; }

protected:
    EffectBase(const ParameterInfo* infos, int count) noexcept
        : infos_(infos), numParameters_(std::clamp(count, 0, kMaxEffectParameters)) {
        for (int i = 0; i < numParameters_; ++i)
            values_[static_cast<size_t>(i)].store(infos_[i].defaultValue, std::memory_order_relaxed);
    }

    // Push one (already clamped) value into the DSP object. Audio thread / setup only.
    virtual void applyParameter(int index, float value) noexcept = 0;

    // The actual processing. Audio thread.
    virtual void processDsp(AudioBlock block) noexcept = 0;

    // Adapters call this at the end of prepare(): remembers the sample rate and pushes every
    // stored value into the freshly prepared DSP object.
    void finishPrepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate;
        dirty_.store(0u, std::memory_order_relaxed);
        for (int i = 0; i < numParameters_; ++i)
            applyParameter(i, values_[static_cast<size_t>(i)].load(std::memory_order_relaxed));
        prepared_ = true;
    }

    bool isValid(int index) const noexcept { return index >= 0 && index < numParameters_; }

private:
    const ParameterInfo* infos_;
    int numParameters_;
    std::array<std::atomic<float>, kMaxEffectParameters> values_{};
    std::atomic<uint32_t> dirty_{0u};
    double sampleRate_ = 0.0;
    bool prepared_ = false;
};

// PureDelayEffect: delays the signal by N samples and *reports* N as its latency.
// It has no musical purpose; it exists so that tests (and Step 8 of the guide) can create
// inserts with known, different latencies and check that PDC lines them up again.
class PureDelayEffect final : public EffectBase {
public:
    enum Param { Delay, kCount };
    static constexpr int kMaxDelaySamples = 8191;

    explicit PureDelayEffect(int delaySamples = 0) : EffectBase(kInfos, kCount) {
        setParameter(Delay, static_cast<float>(delaySamples));
    }

    const char* getName() const noexcept override { return "PureDelay"; }

    void prepare(double sampleRate, int maxBlockSize, int numChannels) override {
        (void)maxBlockSize;
        numChannels_ = std::max(1, numChannels);
        ring_.assign(static_cast<size_t>(numChannels_) * kCapacity, 0.0f);
        writeIndex_ = 0;
        finishPrepare(sampleRate);
    }

    void reset() noexcept override {
        std::fill(ring_.begin(), ring_.end(), 0.0f);
        writeIndex_ = 0;
    }

    int getLatencySamples() const noexcept override { return delay_; }

protected:
    void applyParameter(int index, float value) noexcept override {
        if (index == Delay)
            delay_ = static_cast<int>(value);
    }

    void processDsp(AudioBlock block) noexcept override {
        if (ring_.empty())
            return;
        // y[n] = x[n - D], with a ring buffer of kCapacity samples per channel
        const int n = block.getNumSamples();
        const int nch = std::min(numChannels_, block.getNumChannels());
        int w = writeIndex_;
        for (int ch = 0; ch < nch; ++ch) {
            float* ring = ring_.data() + static_cast<size_t>(ch) * kCapacity;
            float* x = block.getChannelPointer(ch);
            w = writeIndex_;
            for (int i = 0; i < n; ++i) {
                ring[w] = x[i];
                int r = w - delay_;
                if (r < 0)
                    r += static_cast<int>(kCapacity);
                x[i] = ring[r];
                if (++w == static_cast<int>(kCapacity))
                    w = 0;
            }
        }
        writeIndex_ = w;
    }

private:
    static constexpr size_t kCapacity = static_cast<size_t>(kMaxDelaySamples) + 1;
    static constexpr ParameterInfo kInfos[kCount] = {
        {"Delay", "samples", 0.0f, static_cast<float>(kMaxDelaySamples), 0.0f, ParameterKind::Int, false, nullptr, 0},
    };

    std::vector<float> ring_;
    int numChannels_ = 0;
    int writeIndex_ = 0;
    int delay_ = 0;
};

} // namespace dawlab::engine
