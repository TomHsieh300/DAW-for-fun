#pragma once
// DawLabLimiter：把第 05 章的 dawlab::dsp::Limiter 包成 VST3 plugin。
//
// 跟 DawLabCompressor 最大的不同：lookahead limiter 有延遲（latency）。
// plugin 必須用 setLatencySamples() 告訴 host「我的輸出比輸入晚 L 個 sample」，
// host 才能做 plugin delay compensation（PDC）：把其他沒經過這個 plugin 的訊號也延遲 L，讓大家對齊。
//
// 有 latency 的 plugin 還必須 override processBlockBypassed()：VST3 wrapper 會自動加一個 Bypass 參數，
// 打開時改呼叫 processBlockBypassed()。JUCE 的預設實作是直接 pass-through（延遲 0），
// 但 plugin 回報的 latency 仍是 L、host 的 PDC 也仍然補償 L → bypass 的訊號比其他 track 提早 L 個 sample。
// 所以 bypass 時我們輸出「延遲 L 的原音」，並用 10 ms crossfade 在 wet / dry 之間切換（同練習 7）。
//
// 另一個刻意的不同：hasEditor() 回傳 false。host 打開它時會改用 GenericAudioProcessorEditor
// （PluginWindow 的 fallback 路徑），正好用來示範「沒有自訂介面的 plugin」。

#include <dawlab/dsp/DelayLine.h>
#include <dawlab/dsp/Limiter.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace dawlab::plugin {

namespace LimiterParamIds {
inline constexpr const char* ceiling = "ceiling";
inline constexpr const char* inputGain = "inputGain";
inline constexpr const char* release = "release";
inline constexpr const char* lookahead = "lookahead";
} // namespace LimiterParamIds

class LimiterProcessor final : public juce::AudioProcessor {
public:
    LimiterProcessor();
    ~LimiterProcessor() override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    // Bypass 時：輸出延遲 getLatencySamples() 的原音，讓 bypass 前後的時間位置不變（PDC 維持正確）
    void processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; } // host 會用 GenericAudioProcessorEditor

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    float getGainReductionDb() const noexcept { return limiter_.getGainReductionDb(); }

private:
    void applyLookaheadIfChanged() noexcept;
    // processBlock 與 processBlockBypassed 共用：bypassed 決定 crossfade 的目標（0 = wet、1 = dry）
    void processInternal(juce::AudioBuffer<float>& buffer, bool bypassed) noexcept;

    juce::AudioProcessorValueTreeState state_;
    std::atomic<float>* ceilingParam_ = nullptr;
    std::atomic<float>* inputGainParam_ = nullptr;
    std::atomic<float>* releaseParam_ = nullptr;
    std::atomic<float>* lookaheadParam_ = nullptr;

    dawlab::dsp::Limiter limiter_;
    float appliedLookaheadMs_ = -1.0f;
    int maxBlockSize_ = 0;

    // bypass 用：原音的延遲線（容量 = lookahead 上限 + maxBlockSize，在 prepareToPlay 配置）
    dawlab::dsp::DelayLine dryDelay_;
    float bypassMix_ = 0.0f;     // 0 = 全 wet（limiter 輸出）、1 = 全 dry（延遲 L 的原音）
    float bypassStep_ = 1.0f;    // crossfade 每個 sample 的變化量 = 1 / (10 ms 的 sample 數)
    bool snapBypassMix_ = true;  // prepare 後的第一個 block 直接採用目標值（之前沒有輸出，不需要 fade）

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LimiterProcessor)
};

} // namespace dawlab::plugin
