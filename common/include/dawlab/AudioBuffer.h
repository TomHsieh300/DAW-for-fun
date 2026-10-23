#pragma once
// AudioBuffer / AudioBlock：整套教材共用的音訊資料容器。
//
// - AudioBuffer 擁有記憶體（owning），用 setSize() 配置。配置會 allocate，
//   所以只能在 prepare 階段或離線處理時呼叫，絕對不要在 audio callback 裡呼叫。
// - AudioBlock 是不擁有記憶體的 view（non-owning view），複製它很便宜。
//   所有 DSP 元件的 process() 都吃 AudioBlock，這樣同一份 DSP 程式碼可以處理
//   我們自己的 AudioBuffer，也可以直接包住 juce::AudioBuffer 的指標陣列（第 13、14 章）。
//
// 資料排列是 non-interleaved（每個 channel 一段連續記憶體），跟 JUCE / VST3 一樣。

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

    // 允許 AudioBuffer 隱式轉成 AudioBlock，所以可以直接寫 processor.process(buffer)
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

    // 取出 [startSample, startSample + length) 的子區塊（例如把大 block 切成小段處理）
    AudioBlock getSubBlock(int startSample, int length) const noexcept {
        assert(startSample >= 0 && length >= 0 && startSample + length <= numSamples_);
        return AudioBlock(channels_, numChannels_, length, start_ + startSample);
    }

    // 只取部分 channel，例如從立體聲 block 取出單一 channel
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

    // 從 other 複製；只處理兩者共同擁有的 channel 數與 sample 數
    void copyFrom(const AudioBlock& other) const noexcept {
        const int nch = std::min(numChannels_, other.numChannels_);
        const int n = std::min(numSamples_, other.numSamples_);
        for (int ch = 0; ch < nch; ++ch)
            std::copy_n(other.getChannelPointer(ch), n, getChannelPointer(ch));
    }

    // 把 other * gain 疊加進來（mixing 的基本動作）
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

    // vector 的 move 會轉移同一塊 heap 記憶體，所以舊的 channel 指標仍然有效
    AudioBuffer(AudioBuffer&&) noexcept = default;
    AudioBuffer& operator=(AudioBuffer&&) noexcept = default;

    // 重新配置並清成 0。會 allocate —— 不可以在 audio thread 呼叫。
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
