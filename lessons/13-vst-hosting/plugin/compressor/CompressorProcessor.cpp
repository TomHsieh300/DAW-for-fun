// DawLabCompressor 的 AudioProcessor 實作。見 CompressorProcessor.h 的訊號流程圖。

#include "CompressorProcessor.h"
#include "CompressorEditor.h"

#include <dawlab/DspMath.h>

#include <algorithm>
#include <cmath>

namespace dawlab::plugin {

namespace {

// 數值 → 顯示文字（host 的自動化面板、GenericAudioProcessorEditor 都會用到）
juce::AudioParameterFloatAttributes withUnit(const char* unit, int decimals) {
    const juce::String suffix = juce::String(" ") + unit;
    return juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction([suffix, decimals](float v, int) { return juce::String(v, decimals) + suffix; })
        .withValueFromStringFunction([](const juce::String& text) { return text.getFloatValue(); });
}

} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout CompressorProcessor::createParameterLayout() {
    using juce::AudioParameterFloat;
    using juce::NormalisableRange;
    using juce::ParameterID;
    namespace id = CompressorParamIds;

    // NormalisableRange(start, end, interval, skew)：
    //   normalized = ((value − start) / (end − start))^skew
    //   skew < 1 讓滑桿前半段分配給小數值（attack / release / ratio 這種「小值區域比較重要」的參數）。
    NormalisableRange<float> attackRange(0.1f, 200.0f, 0.01f);
    attackRange.setSkewForCentre(10.0f); // 滑桿正中間（normalized 0.5）= 10 ms
    NormalisableRange<float> releaseRange(5.0f, 2000.0f, 0.1f);
    releaseRange.setSkewForCentre(100.0f); // 正中間 = 100 ms

    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    // ParameterID 的第二個參數是 version hint（VST3 / AU 用來判斷參數是哪一版加入的）
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id::threshold, 1}, "Threshold",
                                                     NormalisableRange<float>(-60.0f, 0.0f, 0.1f), -20.0f,
                                                     withUnit("dB", 1)));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id::ratio, 1}, "Ratio",
                                                     NormalisableRange<float>(1.0f, 20.0f, 0.01f, 0.5f), 4.0f,
                                                     withUnit(": 1", 2)));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id::knee, 1}, "Knee",
                                                     NormalisableRange<float>(0.0f, 24.0f, 0.1f), 6.0f,
                                                     withUnit("dB", 1)));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id::attack, 1}, "Attack", attackRange, 10.0f,
                                                     withUnit("ms", 2)));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id::release, 1}, "Release", releaseRange, 100.0f,
                                                     withUnit("ms", 1)));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id::makeup, 1}, "Makeup",
                                                     NormalisableRange<float>(-12.0f, 24.0f, 0.1f), 0.0f,
                                                     withUnit("dB", 1)));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id::mix, 1}, "Mix",
                                                     NormalisableRange<float>(0.0f, 100.0f, 0.1f), 100.0f,
                                                     withUnit("%", 1)));
    return layout;
}

CompressorProcessor::CompressorProcessor()
    : juce::AudioProcessor(BusesProperties()
                               .withInput("Input", juce::AudioChannelSet::stereo(), true)
                               .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state_(*this, nullptr, "DawLabCompressor", createParameterLayout()) {
    namespace id = CompressorParamIds;
    thresholdParam_ = state_.getRawParameterValue(id::threshold);
    ratioParam_ = state_.getRawParameterValue(id::ratio);
    kneeParam_ = state_.getRawParameterValue(id::knee);
    attackParam_ = state_.getRawParameterValue(id::attack);
    releaseParam_ = state_.getRawParameterValue(id::release);
    makeupParam_ = state_.getRawParameterValue(id::makeup);
    mixParam_ = state_.getRawParameterValue(id::mix);
}

CompressorProcessor::~CompressorProcessor() = default;

// ---------------------------------------------------------------------------
// 生命週期
// ---------------------------------------------------------------------------
void CompressorProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    // 唯一可以 allocate 的地方：host 保證之後每個 processBlock 最多 samplesPerBlock 個 sample
    // （我們仍然在 processBlock 裡防守性地切段，因為不是每個 host 都守規矩）。
    maxBlockSize_ = std::max(1, samplesPerBlock);
    const int numChannels = std::max(getTotalNumInputChannels(), getTotalNumOutputChannels());

    compressor_.prepare(sampleRate, maxBlockSize_, numChannels);
    compressor_.setMakeupGainDb(0.0f); // makeup 交給第 01 章的 Gain（它自帶 50 ms 平滑）
    makeup_.prepare(sampleRate, maxBlockSize_, numChannels);
    mix_.reset(sampleRate, 0.05);
    dry_.setSize(std::max(1, numChannels), maxBlockSize_, false, true, false);

    // 參數「瞬間到位」：剛開始播放時不要從預設值滑到目前值
    updateDspParameters();
    makeup_.reset();
    mix_.setCurrentAndTargetValue(mixParam_->load() * 0.01f);
    compressor_.reset();

    setLatencySamples(0); // feedforward、沒有 lookahead → 零延遲（Limiter plugin 就不是，見 LimiterProcessor）
}

void CompressorProcessor::releaseResources() {
    // 停止播放時可以釋放記憶體；我們的 buffer 很小，留著讓下次 prepareToPlay 更快
}

bool CompressorProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    // 只接受 mono→mono 或 stereo→stereo（host 會用這個函式協商 bus layout）
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void CompressorProcessor::updateDspParameters() noexcept {
    compressor_.setThresholdDb(thresholdParam_->load(std::memory_order_relaxed));
    compressor_.setRatio(ratioParam_->load(std::memory_order_relaxed));
    compressor_.setKneeDb(kneeParam_->load(std::memory_order_relaxed));
    compressor_.setAttackMs(attackParam_->load(std::memory_order_relaxed));
    compressor_.setReleaseMs(releaseParam_->load(std::memory_order_relaxed));
    makeup_.setGainDb(makeupParam_->load(std::memory_order_relaxed));
    mix_.setTargetValue(mixParam_->load(std::memory_order_relaxed) * 0.01f); // % → 0..1
}

void CompressorProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ignoreUnused(midi);
    const juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    // 輸出比輸入多的 channel 裡面可能是垃圾，先清掉（JUCE 範本的慣例）
    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear(ch, 0, numSamples);

    const int numChannels = std::min({numOut, buffer.getNumChannels(), dry_.getNumChannels()});
    if (numChannels <= 0 || maxBlockSize_ <= 0)
        return;

    // 每個 block 讀一次參數（block 內的 automation 由 host 用切 sub-block 的方式提供，見練習 4）
    updateDspParameters();

    // 輸入 peak（給 editor 的 meter）
    float inPeak = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch)
        inPeak = std::max(inPeak, buffer.getMagnitude(ch, 0, numSamples));

    for (int start = 0; start < numSamples; start += maxBlockSize_)
        processChunk(buffer, start, std::min(maxBlockSize_, numSamples - start), numChannels);

    float outPeak = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch)
        outPeak = std::max(outPeak, buffer.getMagnitude(ch, 0, numSamples));
    inputPeakDb_.store(dawlab::gainToDb(inPeak), std::memory_order_relaxed);
    outputPeakDb_.store(dawlab::gainToDb(outPeak), std::memory_order_relaxed);
}

void CompressorProcessor::processChunk(juce::AudioBuffer<float>& buffer, int start, int length,
                                       int numChannels) noexcept {
    // 1) 留一份 dry（parallel compression 要用）
    for (int ch = 0; ch < numChannels; ++ch)
        dry_.copyFrom(ch, 0, buffer, ch, start, length);

    // 2) wet = makeup · compress(x)。用 dawlab::AudioBlock 直接包住 JUCE buffer 的指標（零複製），
    //    同一份 DSP 程式碼在離線測試、plugin、DAW 裡都能用。
    dawlab::AudioBlock block(buffer.getArrayOfWritePointers(), numChannels, length, start);
    compressor_.process(block);
    makeup_.process(block);

    // 3) out = (1 − m)·dry + m·wet。mix = 100 % 而且沒有在 ramp：wet 原封不動（bit-exact）。
    if (!mix_.isSmoothing() && mix_.getTargetValue() >= 1.0f)
        return;
    for (int i = 0; i < length; ++i) {
        const float m = mix_.getNextValue();
        for (int ch = 0; ch < numChannels; ++ch) {
            float* wet = block.getChannelPointer(ch);
            const float dry = dry_.getSample(ch, i);
            wet[i] = dry + m * (wet[i] - dry);
        }
    }
}

// ---------------------------------------------------------------------------
// editor
// ---------------------------------------------------------------------------
juce::AudioProcessorEditor* CompressorProcessor::createEditor() { return new CompressorEditor(*this); }

// ---------------------------------------------------------------------------
// state：APVTS 的 ValueTree → XML → binary（copyXmlToBinary 會加一個 magic number 與長度）
// ---------------------------------------------------------------------------
void CompressorProcessor::getStateInformation(juce::MemoryBlock& destData) {
    const auto tree = state_.copyState(); // copyState 是 thread-safe 的（內部有 lock）
    if (auto xml = tree.createXml())
        copyXmlToBinary(*xml, destData);
}

void CompressorProcessor::setStateInformation(const void* data, int sizeInBytes) {
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(state_.state.getType()))
            state_.replaceState(juce::ValueTree::fromXml(*xml)); // 會更新每個參數並通知 host 與 editor
}

} // namespace dawlab::plugin

// JUCE plugin wrapper 用這個工廠函式建立我們的 processor（每個 plugin 一個）
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new dawlab::plugin::CompressorProcessor(); }
