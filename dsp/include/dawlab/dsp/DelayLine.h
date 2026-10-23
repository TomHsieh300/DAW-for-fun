#pragma once
// DelayLine: an integer-sample delay line (circular buffer). The Chapter 05 Limiter uses it to
// delay the audio for lookahead.
//
// Idea:
//   A system that delays by D samples is y[n] = x[n - D]. To do this we must "remember" the
//   last D inputs, so we need a block of memory of length >= D + 1. With a circular buffer,
//   each push only writes one slot and advances the write index by one; reading counts back D
//   slots. No data ever needs to be moved.
//
//   write index  w  ->  the newest sample is stored at buffer[w-1]
//   read(ch, d)      =  buffer[(w - 1 - d) mod size]
//
//        +---+---+---+---+---+---+---+---+
//        |   | x[n-5] x[n-4] x[n-3] x[n-2] x[n-1] x[n] |   |
//        +---+---+---+---+---+---+---+---+
//                                        ^ w: the next write goes here
//                      ^ read(3)         ^ read(0)
//
// Interface (the API contract; Chapter 09's FractionalDelayLine keeps the same design):
//   prepare(maxDelaySamples, numChannels)  allocate memory (the only place that allocates)
//   reset()                                clear to 0
//   push(ch, x)                            push one new sample into channel ch
//   read(ch, d)                            read the sample from "d pushes ago"; d = 0 is the one just pushed
//
// Each channel has its own write index, so the order and number of pushes can differ per
// channel (for example, when processing mono you only push channel 0).

#include <algorithm>
#include <cassert>
#include <vector>

namespace dawlab::dsp {

class DelayLine {
public:
    // maxDelaySamples: the largest delay read() will later allow. Allocates, so do not call it on the audio thread.
    void prepare(int maxDelaySamples, int numChannels) {
        assert(maxDelaySamples >= 0 && numChannels >= 0);
        maxDelay_ = maxDelaySamples;
        numChannels_ = numChannels;
        size_ = maxDelaySamples + 1; // must hold both "now" and "maxDelay ago", hence length + 1
        buffer_.assign(static_cast<size_t>(size_) * static_cast<size_t>(numChannels), 0.0f);
        writeIndex_.assign(static_cast<size_t>(numChannels), 0);
    }

    void reset() noexcept {
        std::fill(buffer_.begin(), buffer_.end(), 0.0f);
        std::fill(writeIndex_.begin(), writeIndex_.end(), 0);
    }

    int getMaxDelaySamples() const noexcept { return maxDelay_; }
    int getNumChannels() const noexcept { return numChannels_; }

    // Push one new sample. O(1), no allocation.
    void push(int channel, float x) noexcept {
        assert(channel >= 0 && channel < numChannels_);
        int& w = writeIndex_[static_cast<size_t>(channel)];
        channelData(channel)[w] = x;
        if (++w >= size_) // circular: wrap back to the start at the end
            w = 0;
    }

    // Read the sample from delaySamples pushes ago (0 = the newest one).
    // A delaySamples above maxDelay is clamped so we never read from a position we shouldn't.
    float read(int channel, int delaySamples) const noexcept {
        assert(channel >= 0 && channel < numChannels_);
        const int d = std::clamp(delaySamples, 0, maxDelay_);
        int idx = writeIndex_[static_cast<size_t>(channel)] - 1 - d;
        if (idx < 0) // counting back past the start: wrap around from the end (same as mod size, without the negative-remainder trap)
            idx += size_;
        return channelData(channel)[idx];
    }

private:
    float* channelData(int channel) noexcept {
        return buffer_.data() + static_cast<size_t>(channel) * static_cast<size_t>(size_);
    }
    const float* channelData(int channel) const noexcept {
        return buffer_.data() + static_cast<size_t>(channel) * static_cast<size_t>(size_);
    }

    int maxDelay_ = 0;
    int numChannels_ = 0;
    int size_ = 1;
    std::vector<float> buffer_;   // all channels laid out contiguously: [ch0's size_ slots][ch1's size_ slots]...
    std::vector<int> writeIndex_; // each channel's own write position
};

} // namespace dawlab::dsp
