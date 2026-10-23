#pragma once
// DawLabCompressor：把第 04 章的 dawlab::dsp::Compressor 與第 01 章的 dawlab::dsp::Gain 包成 VST3 plugin。
//
//   input ──┬─────────────────────────────────────────────┐ dry（預先配置的 buffer）
//           │                                             │
//           └─► Compressor（threshold/ratio/knee/attack/release）─► Gain（makeup）─► wet
//                                                                                │
//                                         out = (1 − mix)·dry + mix·wet ◄────────┘
//
// 這個 class 是一個 juce::AudioProcessor。JUCE 的 plugin wrapper（juce_audio_plugin_client）會把它
// 翻譯成 VST3 的 IComponent / IAudioProcessor / IEditController；我們只要實作 AudioProcessor 的
// 生命週期函式即可：prepareToPlay → processBlock × N → releaseResources。
//
// 參數全部交給 juce::AudioProcessorValueTreeState（APVTS）管理：
//   - host 看到的是 0..1 的 normalized value；APVTS 依 NormalisableRange 換算成實際值（dB、ms…）；
//   - audio thread 透過 getRawParameterValue() 拿到 std::atomic<float>*，每個 block 讀一次（lock-free）；
//   - get/setStateInformation 把整棵 ValueTree 存成 XML（binary 包起來），preset 與專案存檔都靠它。

#include <dawlab/dsp/Compressor.h>
#include <dawlab/dsp/Gain.h>
#include <dawlab/dsp/SmoothedValue.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace dawlab::plugin {

// 參數 ID：存進 state 與 host 專案檔裡，發佈後就不要再改（改了舊專案會讀不回來）
namespace CompressorParamIds {
inline constexpr const char* threshold = "threshold";
inline constexpr const char* ratio = "ratio";
inline constexpr const char* knee = "knee";
inline constexpr const char* attack = "attack";
inline constexpr const char* release = "release";
inline constexpr const char* makeup = "makeup";
inline constexpr const char* mix = "mix";
} // namespace CompressorParamIds

class CompressorProcessor final : public juce::AudioProcessor {
public:
    CompressorProcessor();
    ~CompressorProcessor() override;

    // 參數佈局（名稱、範圍、預設值、顯示格式）。static：測試或文件也可以直接查詢。
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // ---- 生命週期 ----------------------------------------------------------------
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    using AudioProcessor::processBlock; // double 版本沿用預設實作

    // ---- editor ------------------------------------------------------------------
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // ---- 描述 --------------------------------------------------------------------
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // program（preset 清單）：有些 host 不接受 0 個 program，所以回報 1 個
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    // ---- state -------------------------------------------------------------------
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // ---- 給 editor 用（message thread 讀，audio thread 寫；都是 atomic）----------------
    float getGainReductionDb() const noexcept { return compressor_.getGainReductionDb(); }
    float getInputPeakDb() const noexcept { return inputPeakDb_.load(std::memory_order_relaxed); }
    float getOutputPeakDb() const noexcept { return outputPeakDb_.load(std::memory_order_relaxed); }
    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept { return state_; }

private:
    void updateDspParameters() noexcept;
    void processChunk(juce::AudioBuffer<float>& buffer, int start, int length, int numChannels) noexcept;

    juce::AudioProcessorValueTreeState state_;

    // 指向 APVTS 內部 atomic 的指標（建構時取得一次；audio thread 只做 load）
    std::atomic<float>* thresholdParam_ = nullptr;
    std::atomic<float>* ratioParam_ = nullptr;
    std::atomic<float>* kneeParam_ = nullptr;
    std::atomic<float>* attackParam_ = nullptr;
    std::atomic<float>* releaseParam_ = nullptr;
    std::atomic<float>* makeupParam_ = nullptr;
    std::atomic<float>* mixParam_ = nullptr;

    dawlab::dsp::Compressor compressor_;
    dawlab::dsp::Gain makeup_;
    dawlab::dsp::SmoothedValue mix_{1.0f};
    juce::AudioBuffer<float> dry_; // prepareToPlay 配置；processBlock 只複製進去
    int maxBlockSize_ = 0;

    std::atomic<float> inputPeakDb_{-100.0f};
    std::atomic<float> outputPeakDb_{-100.0f};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompressorProcessor)
};

} // namespace dawlab::plugin
