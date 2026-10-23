#pragma once
// DawLabCompressor 的自訂 editor：7 個旋鈕 + gain reduction meter + 輸入 / 輸出 peak。
//
// Thread 規則：editor 活在 message thread。
//   - 旋鈕透過 SliderAttachment 綁到 APVTS 的參數：使用者轉旋鈕 → 參數變化（並通知 host 錄 automation）；
//     host 自動化參數 → 旋鈕跟著動。
//   - meter 用 juce::Timer（30 Hz）讀 processor 的 std::atomic<float>，絕不碰 audio buffer。

#include "CompressorProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <memory>

namespace dawlab::plugin {

// 垂直 bar：顯示「正在壓幾 dB」（0 dB 在上，往下長）
class GainReductionMeter final : public juce::Component {
public:
    void setGainReductionDb(float db);
    void paint(juce::Graphics& g) override;

private:
    float displayDb_ = 0.0f;
    float peakHoldDb_ = 0.0f;
    int holdFrames_ = 0;
};

class CompressorEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit CompressorEditor(CompressorProcessor& processor);
    ~CompressorEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    struct Knob {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    CompressorProcessor& processor_;
    std::array<Knob, 7> knobs_;
    GainReductionMeter meter_;
    juce::Label grText_;
    juce::Label ioText_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompressorEditor)
};

} // namespace dawlab::plugin
