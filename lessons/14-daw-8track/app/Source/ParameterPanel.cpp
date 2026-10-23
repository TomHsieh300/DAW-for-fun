#include "ParameterPanel.h"

#include "DawLookAndFeel.h"
#include "VstInsert.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace dawlab::app {

using engine::ParameterInfo;
using engine::ParameterKind;

struct ParameterPanel::Control {
    int index = 0;
    ParameterInfo info;
    juce::Label caption;
    std::unique_ptr<juce::Slider> slider;
    std::unique_ptr<juce::ToggleButton> toggle;
    std::unique_ptr<juce::ComboBox> combo;
};

juce::String ParameterPanel::formatValue(const engine::InsertEffect& effect, int index, float value) {
    if (const auto* vst = dynamic_cast<const VstInsert*>(&effect))
        return vst->getParameterText(index, value);
    char text[64];
    engine::formatParameterValue(effect.getParameterInfo(index), value, text, static_cast<int>(sizeof(text)));
    return juce::String::fromUTF8(text);
}

ParameterPanel::ParameterPanel(engine::InsertEffect& effect) : effect_(effect) {
    bypass_.setToggleState(effect_.isBypassed(), juce::dontSendNotification);
    bypass_.onClick = [this] { effect_.setBypassed(bypass_.getToggleState()); };
    addAndMakeVisible(bypass_);
    latencyLabel_.setJustificationType(juce::Justification::centredRight);
    latencyLabel_.setColour(juce::Label::textColourId, DawColours::textDim());
    addAndMakeVisible(latencyLabel_);

    for (int i = 0; i < effect_.getNumParameters(); ++i) {
        auto c = std::make_unique<Control>();
        c->index = i;
        c->info = effect_.getParameterInfo(i);
        const ParameterInfo info = c->info;
        const float current = effect_.getParameter(i);
        c->caption.setText(juce::String::fromUTF8(info.name), juce::dontSendNotification);
        c->caption.setJustificationType(juce::Justification::centred);
        c->caption.setFont(DawLookAndFeel::uiFont(12.0f));
        c->caption.setColour(juce::Label::textColourId, DawColours::textDim());
        content_.addAndMakeVisible(c->caption);

        if (info.kind == ParameterKind::Choice && info.choices != nullptr && info.numChoices > 0) {
            c->combo = std::make_unique<juce::ComboBox>();
            for (int k = 0; k < info.numChoices; ++k)
                c->combo->addItem(juce::String::fromUTF8(info.choices[k]), k + 1);
            c->combo->setSelectedId(static_cast<int>(current - info.min) + 1, juce::dontSendNotification);
            c->combo->onChange = [this, i, info, combo = c->combo.get()] {
                effect_.setParameter(i, info.min + static_cast<float>(combo->getSelectedId() - 1));
            };
            content_.addAndMakeVisible(*c->combo);
        } else if (info.kind == ParameterKind::Bool) {
            c->toggle = std::make_unique<juce::ToggleButton>(current >= 0.5f ? "On" : "Off");
            c->toggle->setToggleState(current >= 0.5f, juce::dontSendNotification);
            c->toggle->onClick = [this, i, t = c->toggle.get()] {
                effect_.setParameter(i, t->getToggleState() ? 1.0f : 0.0f);
                t->setButtonText(t->getToggleState() ? "On" : "Off");
            };
            content_.addAndMakeVisible(*c->toggle);
        } else {
            auto s = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
            // Slider values are real parameter values; the 0..1 knob travel uses the engine's taper.
            juce::NormalisableRange<double> range(
                info.min, info.max,
                [info](double, double, double n) { return static_cast<double>(info.fromNormalized(static_cast<float>(n))); },
                [info](double, double, double v) { return static_cast<double>(info.toNormalized(static_cast<float>(v))); },
                [info](double, double, double v) { return static_cast<double>(info.clamp(static_cast<float>(v))); });
            s->setNormalisableRange(range);
            s->setDoubleClickReturnValue(true, info.defaultValue);
            s->setTextBoxStyle(juce::Slider::TextBoxBelow, false, kCellWidth - 6, 16);
            s->textFromValueFunction = [this, i](double v) { return formatValue(effect_, i, static_cast<float>(v)); };
            s->valueFromTextFunction = [](const juce::String& text) { return text.getDoubleValue(); };
            if (info.min < 0.0f && info.max > 0.0f)
                s->getProperties().set(DawLookAndFeel::kBipolarProperty, true);
            s->setValue(current, juce::dontSendNotification);
            s->onValueChange = [this, i, sl = s.get()] { effect_.setParameter(i, static_cast<float>(sl->getValue())); };
            c->slider = std::move(s);
            content_.addAndMakeVisible(*c->slider);
        }
        controls_.push_back(std::move(c));
    }

    viewport_.setViewedComponent(&content_, false);
    viewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport_);
    const auto ideal = getIdealSize();
    setSize(ideal.x, ideal.y);
    refreshFromEffect();
    startTimerHz(15);
}

ParameterPanel::~ParameterPanel() { stopTimer(); }

juce::Slider* ParameterPanel::getSlider(int i) const noexcept {
    return (i >= 0 && i < getNumControls()) ? controls_[static_cast<size_t>(i)]->slider.get() : nullptr;
}
juce::ToggleButton* ParameterPanel::getToggle(int i) const noexcept {
    return (i >= 0 && i < getNumControls()) ? controls_[static_cast<size_t>(i)]->toggle.get() : nullptr;
}
juce::ComboBox* ParameterPanel::getComboBox(int i) const noexcept {
    return (i >= 0 && i < getNumControls()) ? controls_[static_cast<size_t>(i)]->combo.get() : nullptr;
}

int ParameterPanel::getNumColumns() const noexcept {
    const int n = getNumControls();
    // The 6-band EQ has 5 parameters per band: one band per row reads like a hardware EQ.
    if (n == 30 && std::strncmp(effect_.getName(), "ParametricEQ", 12) == 0)
        return 5;
    return std::clamp(n <= 8 ? n : (n <= 16 ? (n + 1) / 2 : 8), 1, 8);
}

juce::Point<int> ParameterPanel::getIdealSize() const {
    const int cols = getNumColumns();
    const int rows = (getNumControls() + cols - 1) / cols;
    const int w = std::max(280, cols * kCellWidth + 16);
    const int h = kHeaderHeight + rows * kCellHeight + 12;
    return {std::min(w, 900), std::min(h, 640)};
}

void ParameterPanel::refreshFromEffect() {
    for (auto& c : controls_) {
        const float v = effect_.getParameter(c->index);
        if (c->slider != nullptr && !c->slider->isMouseButtonDown())
            c->slider->setValue(v, juce::dontSendNotification);
        if (c->toggle != nullptr) {
            c->toggle->setToggleState(v >= 0.5f, juce::dontSendNotification);
            c->toggle->setButtonText(v >= 0.5f ? "On" : "Off");
        }
        if (c->combo != nullptr)
            c->combo->setSelectedId(static_cast<int>(v - c->info.min) + 1, juce::dontSendNotification);
    }
    bypass_.setToggleState(effect_.isBypassed(), juce::dontSendNotification);
    // getReportedLatencySamples(): an atomic copy. getLatencySamples() itself may only be called by the
    // audio thread (InsertEffect.h, threading contract) - a limiter computes it from its lookahead.
    latencyLabel_.setText("latency " + juce::String(effect_.getReportedLatencySamples()) + " smp", juce::dontSendNotification);
    if (effect_.hasMeter()) {
        const float m = effect_.getMeterDb();
        if (std::abs(m - meterDb_) > 0.05f) {
            meterDb_ = m;
            repaint(getLocalBounds().removeFromTop(kHeaderHeight));
        }
    }
}

void ParameterPanel::paint(juce::Graphics& g) {
    g.fillAll(DawColours::panel());
    auto header = getLocalBounds().removeFromTop(kHeaderHeight).reduced(8, 6);
    g.setColour(DawColours::background());
    g.fillRect(getLocalBounds().removeFromTop(kHeaderHeight));
    if (effect_.hasMeter()) {
        // Gain reduction: a bar growing from the left, 0 .. 24 dB.
        auto meter = header.withTrimmedLeft(110).withWidth(std::min(160, header.getWidth() - 220)).reduced(0, 4);
        g.setColour(DawColours::panelLight());
        g.fillRect(meter);
        const float p = std::clamp(meterDb_ / 24.0f, 0.0f, 1.0f);
        g.setColour(DawColours::accentWarm());
        g.fillRect(meter.withWidth(static_cast<int>(std::round(p * static_cast<float>(meter.getWidth())))));
        g.setColour(DawColours::text());
        g.setFont(DawLookAndFeel::uiFont(12.0f));
        g.drawText("GR " + juce::String(meterDb_, 1) + " dB", meter.translated(meter.getWidth() + 6, 0).withWidth(80),
                   juce::Justification::centredLeft);
    }
}

void ParameterPanel::resized() {
    auto area = getLocalBounds();
    auto header = area.removeFromTop(kHeaderHeight).reduced(8, 6);
    bypass_.setBounds(header.removeFromLeft(100));
    latencyLabel_.setBounds(header.removeFromRight(110));
    viewport_.setBounds(area);

    const int cols = getNumColumns();
    const int rows = (getNumControls() + cols - 1) / cols;
    content_.setSize(std::max(area.getWidth() - viewport_.getScrollBarThickness(), cols * kCellWidth + 16),
                     rows * kCellHeight + 12);
    for (int i = 0; i < getNumControls(); ++i) {
        auto& c = *controls_[static_cast<size_t>(i)];
        const auto cell = juce::Rectangle<int>(8 + (i % cols) * kCellWidth, 6 + (i / cols) * kCellHeight, kCellWidth,
                                               kCellHeight)
                              .reduced(3);
        auto inner = cell;
        c.caption.setBounds(inner.removeFromTop(16));
        if (c.slider)
            c.slider->setBounds(inner);
        if (c.toggle)
            c.toggle->setBounds(inner.withSizeKeepingCentre(inner.getWidth(), 26));
        if (c.combo)
            c.combo->setBounds(inner.withSizeKeepingCentre(inner.getWidth(), 24));
    }
}

// ---- ParameterWindow ----------------------------------------------------------------------------

ParameterWindow::ParameterWindow(engine::InsertEffect& effect, const juce::String& title,
                                 juce::LookAndFeel* lookAndFeel)
    : juce::DocumentWindow(title, DawColours::panel(), juce::DocumentWindow::closeButton) {
    if (lookAndFeel != nullptr)
        setLookAndFeel(lookAndFeel);
    setUsingNativeTitleBar(true);
    auto panel = std::make_unique<ParameterPanel>(effect);
    panel_ = panel.get();
    setContentOwned(panel.release(), true);
    setResizable(true, false);
    setAlwaysOnTop(true);
}

ParameterWindow::~ParameterWindow() {
    clearContentComponent();
    setLookAndFeel(nullptr);
}

void ParameterWindow::closeButtonPressed() {
    if (onClose)
        onClose();
    else
        setVisible(false);
}

} // namespace dawlab::app
