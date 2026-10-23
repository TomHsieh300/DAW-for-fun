#include "LimiterProcessor.h"

#include <algorithm>

namespace dawlab::plugin {

namespace {
juce::AudioParameterFloatAttributes withUnit(const char* unit, int decimals) {
    const juce::String suffix = juce::String(" ") + unit;
    return juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction([suffix, decimals](float v, int) { return juce::String(v, decimals) + suffix; })
        .withValueFromStringFunction([](const juce::String& text) { return text.getFloatValue(); });
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout LimiterProcessor::createParameterLayout() {
    using juce::AudioParameterFloat;
    using juce::NormalisableRange;
    using juce::ParameterID;
    namespace id = LimiterParamIds;

    NormalisableRange<float> releaseRange(1.0f, 1000.0f, 0.1f);
    releaseRange.setSkewForCentre(100.0f);

    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id::ceiling, 1}, "Ceiling",
                                                     NormalisableRange<float>(-24.0f, 0.0f, 0.1f), -1.0f,
                                                     withUnit("dB", 1)));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id::inputGain, 1}, "Input Gain",
                                                     NormalisableRange<float>(0.0f, 24.0f, 0.1f), 0.0f,
                                                     withUnit("dB", 1)));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id::release, 1}, "Release", releaseRange, 100.0f,
                                                     withUnit("ms", 1)));
    // lookahead 的上限 20 ms = dsp::Limiter 預設的 setMaxLookaheadMs(20)
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID{id::lookahead, 1}, "Lookahead",
                                                     NormalisableRange<float>(0.0f, 20.0f, 0.1f), 5.0f,
                                                     withUnit("ms", 1)));
    return layout;
}

LimiterProcessor::LimiterProcessor()
    : juce::AudioProcessor(BusesProperties()
                               .withInput("Input", juce::AudioChannelSet::stereo(), true)
                               .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state_(*this, nullptr, "DawLabLimiter", createParameterLayout()) {
    namespace id = LimiterParamIds;
    ceilingParam_ = state_.getRawParameterValue(id::ceiling);
    inputGainParam_ = state_.getRawParameterValue(id::inputGain);
    releaseParam_ = state_.getRawParameterValue(id::release);
    lookaheadParam_ = state_.getRawParameterValue(id::lookahead);
}

LimiterProcessor::~LimiterProcessor() = default;

void LimiterProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    maxBlockSize_ = std::max(1, samplesPerBlock);
    const int numChannels = std::max(getTotalNumInputChannels(), getTotalNumOutputChannels());
    // lookahead 上限用 dsp::Limiter 的預設（20 ms，在 prepare 時配置），與參數範圍 0..20 ms 一致
    limiter_.setLookaheadMs(lookaheadParam_->load());
    limiter_.prepare(sampleRate, maxBlockSize_, numChannels);
    limiter_.setCeilingDb(ceilingParam_->load());
    limiter_.setInputGainDb(inputGainParam_->load());
    limiter_.setReleaseMs(releaseParam_->load());
    limiter_.reset();
    appliedLookaheadMs_ = lookaheadParam_->load();

    // bypass 用的延遲線：processInternal 一次處理最多 maxBlockSize_ 個 sample，先把整段 push 進去，
    // 再讀「d = (n − 1 − i) + L」之前的值，所以容量要 ≥ (maxBlockSize − 1) + lookahead 上限。
    const int maxLookaheadSamples = std::max(0, dawlab::msToSamples(limiter_.getMaxLookaheadMs(), sampleRate));
    dryDelay_.prepare(maxLookaheadSamples + maxBlockSize_, numChannels);
    bypassStep_ = 1.0f / static_cast<float>(std::max(1, dawlab::msToSamples(10.0, sampleRate)));
    snapBypassMix_ = true;

    // 在 prepareToPlay 裡回報 latency：host（JUCE 的 VST3 hosting 也是）在 prepare 完成後讀取這個值
    setLatencySamples(limiter_.getLatencySamples());
}

bool LimiterProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

double LimiterProcessor::getTailLengthSeconds() const {
    // 輸入停止後，輸出還會再「吐」出 lookahead 那麼長的訊號
    const double sr = getSampleRate();
    return sr > 0.0 ? limiter_.getLatencySamples() / sr : 0.0;
}

void LimiterProcessor::applyLookaheadIfChanged() noexcept {
    const float la = lookaheadParam_->load(std::memory_order_relaxed);
    if (la == appliedLookaheadMs_)
        return;
    appliedLookaheadMs_ = la;
    limiter_.setLookaheadMs(la);
    // 播放中改 latency：JUCE 的 wrapper 會通知 host（VST3：restartComponent(kLatencyChanged)），
    // host 多半會在下一次 prepare（或停止 / 重新開始播放）時重新計算 PDC。
    setLatencySamples(limiter_.getLatencySamples());
}

void LimiterProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ignoreUnused(midi);
    processInternal(buffer, false);
}

// JUCE 的預設 processBlockBypassed() 什麼都不做（輸出 = 輸入，延遲 0）。可是我們回報的 latency 是 L，
// host 的 PDC 會把其他 track 延遲 L 來配合我們 → bypass 時這條 track 會提早 L 個 sample（PDC 錯位）。
// 所以 bypass 時輸出「延遲 L 的原音」，時間位置與 bypass 前完全相同。
void LimiterProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ignoreUnused(midi);
    processInternal(buffer, true);
}

void LimiterProcessor::processInternal(juce::AudioBuffer<float>& buffer, bool bypassed) noexcept {
    const juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear(ch, 0, numSamples);

    limiter_.setCeilingDb(ceilingParam_->load(std::memory_order_relaxed));
    limiter_.setInputGainDb(inputGainParam_->load(std::memory_order_relaxed));
    limiter_.setReleaseMs(releaseParam_->load(std::memory_order_relaxed));
    applyLookaheadIfChanged();

    const int numChannels = std::min({numOut, buffer.getNumChannels(), dryDelay_.getNumChannels()});
    if (numChannels <= 0 || maxBlockSize_ <= 0)
        return;

    const float target = bypassed ? 1.0f : 0.0f;
    if (snapBypassMix_) {
        bypassMix_ = target;
        snapBypassMix_ = false;
    }

    float* const* data = buffer.getArrayOfWritePointers();
    // 即使 host 違約給了比 maxBlockSize 大的 block，也切段處理（延遲線容量只保證 maxBlockSize）
    for (int start = 0; start < numSamples; start += maxBlockSize_) {
        const int n = std::min(maxBlockSize_, numSamples - start);

        // 1) 原音先 push 進延遲線（limiter 是 in-place，處理完原音就不見了）
        for (int ch = 0; ch < numChannels; ++ch)
            for (int i = 0; i < n; ++i)
                dryDelay_.push(ch, data[ch][start + i]);

        // 2) limiter 照常處理：bypass 時也跑，讓 envelope / lookahead 狀態保持最新，切回來時不會有舊聲音
        dawlab::AudioBlock block(data, numChannels, numSamples);
        limiter_.process(block.getSubBlock(start, n));

        // 3) 完全沒有 bypass、也沒在 fade：輸出就是 limiter 的結果（不碰任何 sample）
        if (bypassMix_ == 0.0f && target == 0.0f)
            continue;

        // 4) crossfade：y = (1 − m)·wet + m·dry[n − L]，m 每個 sample 往目標移動 bypassStep_（10 ms 走完）
        //    第 i 個 sample 的原音是 (n − 1 − i) 次 push 之前的值，再往前 L 個 sample
        const int latency = limiter_.getLatencySamples();
        for (int i = 0; i < n; ++i) {
            if (bypassMix_ < target)
                bypassMix_ = std::min(target, bypassMix_ + bypassStep_);
            else if (bypassMix_ > target)
                bypassMix_ = std::max(target, bypassMix_ - bypassStep_);
            const float m = bypassMix_;
            const int d = (n - 1 - i) + latency;
            for (int ch = 0; ch < numChannels; ++ch) {
                float& y = data[ch][start + i];
                y = (1.0f - m) * y + m * dryDelay_.read(ch, d);
            }
        }
    }
}

void LimiterProcessor::getStateInformation(juce::MemoryBlock& destData) {
    if (auto xml = state_.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void LimiterProcessor::setStateInformation(const void* data, int sizeInBytes) {
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(state_.state.getType()))
            state_.replaceState(juce::ValueTree::fromXml(*xml));
}

} // namespace dawlab::plugin

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new dawlab::plugin::LimiterProcessor(); }
