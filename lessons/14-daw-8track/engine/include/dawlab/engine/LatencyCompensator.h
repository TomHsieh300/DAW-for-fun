#pragma once
// LatencyCompensator: a plain integer delay used for plugin delay compensation (PDC).
//
// If track A has a limiter with 240 samples of lookahead and track B has nothing, A's audio
// arrives 240 samples late at the master. To keep them aligned we delay B by 240 samples.
// That is all PDC is: every path is padded with (maxLatency - ownLatency) samples of delay.
//
// The ring buffer is allocated once in prepare(). Changing the delay amount only moves the
// read offset; the signal jumps (a click) but that only happens on structural changes.

#include <dawlab/AudioBuffer.h>

#include <algorithm>
#include <vector>

namespace dawlab::engine {

class LatencyCompensator {
public:
    void prepare(int maxDelaySamples, int numChannels) {
        maxDelay_ = std::max(0, maxDelaySamples);
        capacity_ = maxDelay_ + 1;
        ring_.assign(static_cast<size_t>(numChannels) * static_cast<size_t>(capacity_), 0.0f);
        numChannels_ = numChannels;
        reset();
    }

    void reset() noexcept {
        std::fill(ring_.begin(), ring_.end(), 0.0f);
        writeIndex_ = 0;
    }

    int getMaxDelaySamples() const noexcept { return maxDelay_; }
    int getDelaySamples() const noexcept { return delay_; }

    // Returns the delay actually applied (clamped to the maximum).
    int setDelaySamples(int samples) noexcept {
        delay_ = std::clamp(samples, 0, maxDelay_);
        return delay_;
    }

    // The ring is written even when the delay is 0, so that it always holds the most recent
    // input: when the delay later grows from 0 to D, the output repeats the last D samples
    // (a small jump) instead of replaying stale audio from long ago.
    void process(AudioBlock block) noexcept {
        if (ring_.empty())
            return;
        const int n = block.getNumSamples();
        const int nch = std::min(numChannels_, block.getNumChannels());
        int w = writeIndex_;
        for (int ch = 0; ch < nch; ++ch) {
            float* ring = ring_.data() + static_cast<size_t>(ch) * static_cast<size_t>(capacity_);
            float* x = block.getChannelPointer(ch);
            w = writeIndex_;
            for (int i = 0; i < n; ++i) {
                int r = w - delay_;
                if (r < 0)
                    r += capacity_;
                ring[w] = x[i];
                x[i] = ring[r]; // delay 0: r == w, the sample just written
                if (++w == capacity_)
                    w = 0;
            }
        }
        writeIndex_ = w;
    }

private:
    std::vector<float> ring_;
    int numChannels_ = 0;
    int capacity_ = 1;
    int maxDelay_ = 0;
    int delay_ = 0;
    int writeIndex_ = 0;
};

} // namespace dawlab::engine
