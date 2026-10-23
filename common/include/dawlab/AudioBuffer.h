#pragma once
// AudioBuffer / AudioBlock: the audio data containers shared by the whole course.
//
// - AudioBuffer owns its memory and is allocated with setSize(). Allocation allocates,
//   so call it only in the prepare stage or during offline processing, never inside an audio callback.
// - AudioBlock is a non-owning view, so copying it is cheap.
//   The process() of every DSP component takes an AudioBlock, so the same DSP code can handle
//   our own AudioBuffer and can also wrap the pointer array of a juce::AudioBuffer directly (chapters 13 and 14).
//
// The data layout is non-interleaved (each channel is one contiguous block of memory), the same as JUCE / VST3.

#include <algorithm>
#include <cassert>
#include <vector>

namespace dawlab {

class AudioBuffer;

class AudioBlock {
public:
    AudioBlock() = default;

    AudioBlock(float* const* channels, int numChannels, int numSamples, int startSample = 0) noexcept
        : channels_(channels), numChannels_(numChannels), numSamples_(numSamples), start_(startSample) {}

    // Allows an AudioBuffer to convert implicitly to an AudioBlock, so you can simply write processor.process(buffer)
    AudioBlock(AudioBuffer& buffer) noexcept; // NOLINT(google-explicit-constructor)

    int getNumChannels() const noexcept { return numChannels_; }
    int getNumSamples() const noexcept { return numSamples_; }

    float* getChannelPointer(int channel) const noexcept {
        assert(channel >= 0 && channel < numChannels_);
        return channels_[channel] + start_;
    }

    float getSample(int channel, int index) const noexcept {
        assert(index >= 0 && index < numSamples_);
        return getChannelPointer(channel)[index];
    }

    void setSample(int channel, int index, float value) const noexcept {
        assert(index >= 0 && index < numSamples_);
        getChannelPointer(channel)[index] = value;
    }

    // Returns the sub-block [startSample, startSample + length) (for example to process a large block in smaller pieces)
    AudioBlock getSubBlock(int startSample, int length) const noexcept {
        assert(startSample >= 0 && length >= 0 && startSample + length <= numSamples_);
        return AudioBlock(channels_, numChannels_, length, start_ + startSample);
    }

    // Takes only some of the channels, e.g. a single channel out of a stereo block
    AudioBlock getChannelSubset(int firstChannel, int count) const noexcept {
        assert(firstChannel >= 0 && count >= 0 && firstChannel + count <= numChannels_);
        return AudioBlock(channels_ + firstChannel, count, numSamples_, start_);
    }

    void clear() const noexcept {
        for (int ch = 0; ch < numChannels_; ++ch)
            std::fill_n(getChannelPointer(ch), numSamples_, 0.0f);
    }

    void multiplyBy(float gain) const noexcept {
        for (int ch = 0; ch < numChannels_; ++ch) {
            float* d = getChannelPointer(ch);
            for (int i = 0; i < numSamples_; ++i)
                d[i] *= gain;
        }
    }

    // Copies from other; only the channel count and sample count that both have in common are processed
    void copyFrom(const AudioBlock& other) const noexcept {
        const int nch = std::min(numChannels_, other.numChannels_);
        const int n = std::min(numSamples_, other.numSamples_);
        for (int ch = 0; ch < nch; ++ch)
            std::copy_n(other.getChannelPointer(ch), n, getChannelPointer(ch));
    }

    // Adds other * gain on top (the basic mixing operation)
    void addFrom(const AudioBlock& other, float gain = 1.0f) const noexcept {
        const int nch = std::min(numChannels_, other.numChannels_);
        const int n = std::min(numSamples_, other.numSamples_);
        for (int ch = 0; ch < nch; ++ch) {
            const float* s = other.getChannelPointer(ch);
            float* d = getChannelPointer(ch);
            for (int i = 0; i < n; ++i)
                d[i] += s[i] * gain;
        }
    }

private:
    float* const* channels_ = nullptr;
    int numChannels_ = 0;
    int numSamples_ = 0;
    int start_ = 0;
};

class AudioBuffer {
public:
    AudioBuffer() = default;

    AudioBuffer(int numChannels, int numSamples) { setSize(numChannels, numSamples); }

    AudioBuffer(const AudioBuffer& other)
        : numChannels_(other.numChannels_), numSamples_(other.numSamples_), data_(other.data_) {
        rebuildPointers();
    }

    AudioBuffer& operator=(const AudioBuffer& other) {
        if (this != &other) {
            numChannels_ = other.numChannels_;
            numSamples_ = other.numSamples_;
            data_ = other.data_;
            rebuildPointers();
        }
        return *this;
    }

    // Moving a vector transfers the same heap block, so the old channel pointers remain valid
    AudioBuffer(AudioBuffer&&) noexcept = default;
    AudioBuffer& operator=(AudioBuffer&&) noexcept = default;

    // Reallocates and clears to 0. This allocates, so it must not be called on the audio thread.
    void setSize(int numChannels, int numSamples) {
        assert(numChannels >= 0 && numSamples >= 0);
        numChannels_ = numChannels;
        numSamples_ = numSamples;
        data_.assign(static_cast<size_t>(numChannels) * static_cast<size_t>(numSamples), 0.0f);
        rebuildPointers();
    }

    int getNumChannels() const noexcept { return numChannels_; }
    int getNumSamples() const noexcept { return numSamples_; }

    float* getWritePointer(int channel) noexcept {
        assert(channel >= 0 && channel < numChannels_);
        return channelPtrs_[static_cast<size_t>(channel)];
    }

    const float* getReadPointer(int channel) const noexcept {
        assert(channel >= 0 && channel < numChannels_);
        return channelPtrs_[static_cast<size_t>(channel)];
    }

    float* const* getArrayOfWritePointers() noexcept { return channelPtrs_.data(); }

    float getSample(int channel, int index) const noexcept {
        assert(index >= 0 && index < numSamples_);
        return getReadPointer(channel)[index];
    }

    void setSample(int channel, int index, float value) noexcept {
        assert(index >= 0 && index < numSamples_);
        getWritePointer(channel)[index] = value;
    }

    void addSample(int channel, int index, float value) noexcept {
        assert(index >= 0 && index < numSamples_);
        getWritePointer(channel)[index] += value;
    }

    void clear() noexcept { std::fill(data_.begin(), data_.end(), 0.0f); }

    void applyGain(float gain) noexcept {
        for (float& s : data_)
            s *= gain;
    }

    void copyFrom(int destChannel, int destStart, const AudioBuffer& source, int sourceChannel,
                  int sourceStart, int numSamples) noexcept {
        assert(destStart + numSamples <= numSamples_);
        assert(sourceStart + numSamples <= source.numSamples_);
        std::copy_n(source.getReadPointer(sourceChannel) + sourceStart, numSamples,
                    getWritePointer(destChannel) + destStart);
    }

    void addFrom(int destChannel, int destStart, const AudioBuffer& source, int sourceChannel,
                 int sourceStart, int numSamples, float gain = 1.0f) noexcept {
        assert(destStart + numSamples <= numSamples_);
        assert(sourceStart + numSamples <= source.numSamples_);
        const float* s = source.getReadPointer(sourceChannel) + sourceStart;
        float* d = getWritePointer(destChannel) + destStart;
        for (int i = 0; i < numSamples; ++i)
            d[i] += s[i] * gain;
    }

    AudioBlock toBlock() noexcept { return AudioBlock(*this); }

private:
    void rebuildPointers() {
        channelPtrs_.resize(static_cast<size_t>(numChannels_));
        for (int ch = 0; ch < numChannels_; ++ch)
            channelPtrs_[static_cast<size_t>(ch)] =
                data_.data() + static_cast<size_t>(ch) * static_cast<size_t>(numSamples_);
    }

    int numChannels_ = 0;
    int numSamples_ = 0;
    std::vector<float> data_;
    std::vector<float*> channelPtrs_;
};

inline AudioBlock::AudioBlock(AudioBuffer& buffer) noexcept
    : AudioBlock(buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples()) {}

} // namespace dawlab
