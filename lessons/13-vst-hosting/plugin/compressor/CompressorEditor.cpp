#include "CompressorEditor.h"

#include <algorithm>

namespace dawlab::plugin {

namespace {
constexpr float kMeterRangeDb = 24.0f; // meter 顯示 0..24 dB 的 gain reduction
const juce::Colour kBackground{0xff1e2228};
const juce::Colour kAccent{0xffe0a030};
} // namespace

// ---------------------------------------------------------------------------
// GainReductionMeter
// ---------------------------------------------------------------------------
void GainReductionMeter::setGainReductionDb(float db) {
    // 顯示用的 ballistics：上升立刻跟上，下降每 frame 最多回彈 1 dB（約 30 dB/s），眼睛才看得清楚
    db = std::clamp(db, 0.0f, kMeterRangeDb);
    displayDb_ = db > displayDb_ ? db : std::max(db, displayDb_ - 1.0f);
    if (db >= peakHoldDb_) {
        peakHoldDb_ = db;
        holdFrames_ = 45; // 約 1.5 s
    } else if (--holdFrames_ <= 0) {
        peakHoldDb_ = std::max(db, peakHoldDb_ - 0.5f);
    }
    repaint();
}

void GainReductionMeter::paint(juce::Graphics& g) {
    const auto area = getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(juce::Colours::black);
    g.fillRect(area);

    const float h = area.getHeight() * displayDb_ / kMeterRangeDb;
    g.setColour(kAccent);
    g.fillRect(area.withHeight(h)); // 從上往下長：壓得越多 bar 越長

    const float holdY = area.getY() + area.getHeight() * peakHoldDb_ / kMeterRangeDb;
    g.setColour(juce::Colours::white);
    g.drawHorizontalLine(static_cast<int>(holdY), area.getX(), area.getRight());

    // 刻度：每 6 dB 一條
    g.setColour(juce::Colours::grey);
    g.setFont(juce::FontOptions(10.0f));
    for (int db = 6; db < static_cast<int>(kMeterRangeDb); db += 6) {
        const float y = area.getY() + area.getHeight() * static_cast<float>(db) / kMeterRangeDb;
        g.drawHorizontalLine(static_cast<int>(y), area.getX(), area.getX() + 6.0f);
        g.drawText(juce::String(db), juce::Rectangle<float>(area.getX() + 8.0f, y - 6.0f, 20.0f, 12.0f),
                   juce::Justification::left);
    }
}

// ---------------------------------------------------------------------------
// CompressorEditor
// ---------------------------------------------------------------------------
CompressorEditor::CompressorEditor(CompressorProcessor& processor)
    : juce::AudioProcessorEditor(processor), processor_(processor) {
    namespace id = CompressorParamIds;
    const char* ids[] = {id::threshold, id::ratio, id::knee, id::attack, id::release, id::makeup, id::mix};
    const char* names[] = {"Threshold", "Ratio", "Knee", "Attack", "Release", "Makeup", "Mix"};

    for (size_t i = 0; i < knobs_.size(); ++i) {
        auto& k = knobs_[i];
        k.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        k.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 18);
        k.slider.setColour(juce::Slider::rotarySliderFillColourId, kAccent);
        addAndMakeVisible(k.slider);
        k.label.setText(names[i], juce::dontSendNotification);
        k.label.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(k.label);
        // attachment 要在 slider 之後建立、在 slider 之前銷毀（std::array 裡 attachment 是最後一個成員，
        // 解構順序是反向的，所以 attachment 會先被刪掉）
        k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            processor_.getValueTreeState(), ids[i], k.slider);
    }

    addAndMakeVisible(meter_);
    grText_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(grText_);
    ioText_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(ioText_);

    setSize(720, 250);
    startTimerHz(30);
}

CompressorEditor::~CompressorEditor() { stopTimer(); }

void CompressorEditor::paint(juce::Graphics& g) {
    g.fillAll(kBackground);
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    g.drawText("DawLab Compressor", 12, 6, 300, 24, juce::Justification::left);
    g.setColour(juce::Colours::grey);
    g.setFont(juce::FontOptions(12.0f));
    g.drawText("GR", getWidth() - 70, 8, 60, 16, juce::Justification::centred);
}

void CompressorEditor::resized() {
    auto area = getLocalBounds().reduced(10);
    area.removeFromTop(26);
    ioText_.setBounds(area.removeFromBottom(20));

    auto meterArea = area.removeFromRight(60);
    grText_.setBounds(meterArea.removeFromBottom(20));
    meter_.setBounds(meterArea.reduced(8, 0));

    const int knobWidth = area.getWidth() / static_cast<int>(knobs_.size());
    for (auto& k : knobs_) {
        auto column = area.removeFromLeft(knobWidth);
        k.label.setBounds(column.removeFromTop(20));
        k.slider.setBounds(column.reduced(2));
    }
}

void CompressorEditor::timerCallback() {
    const float gr = processor_.getGainReductionDb();
    meter_.setGainReductionDb(gr);
    grText_.setText(juce::String(gr, 1) + " dB", juce::dontSendNotification);
    ioText_.setText("in " + juce::String(processor_.getInputPeakDb(), 1) + " dBFS   out " +
                        juce::String(processor_.getOutputPeakDb(), 1) + " dBFS",
                    juce::dontSendNotification);
}

} // namespace dawlab::plugin
