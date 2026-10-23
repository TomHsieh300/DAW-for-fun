#pragma once
// Custom editor for DawLabCompressor: 7 knobs + a gain reduction meter + input / output peak.
//
// Thread rules: the editor lives on the message thread.
//   - Knobs are bound to the APVTS parameters through SliderAttachment: the user turns a knob -> the parameter changes (and the host is told so it can record automation);
//     the host automates a parameter -> the knob follows.
//   - The meters use a juce::Timer (30 Hz) to read the processor's std::atomic<float> values, and never touch the audio buffer.

#include "CompressorProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <memory>

namespace dawlab::plugin {

// Vertical bar: shows "how many dB are being compressed" (0 dB at the top, growing downward)
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
