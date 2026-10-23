#pragma once
// ParameterPanel: a parameter editor generated automatically from an InsertEffect's
// self-description (Step 16).
//
// Stage 1 gave every effect a table of ParameterInfo: name, unit, range, default, kind
// (Float / Int / Bool / Choice), log taper, choice labels. That is all a GUI needs:
//
//   Float, Int   -> rotary knob. Slider value = the real value (e.g. -18 dB); the slider's
//                   NormalisableRange uses ParameterInfo::toNormalized / fromNormalized, so a
//                   frequency knob gets the same log taper as the engine's normalized API.
//   Bool         -> toggle button
//   Choice       -> combo box with the choice labels
//
// plus a Bypass toggle and, for dynamics effects (hasMeter()), a gain-reduction readout.
//
// Threading: the knob calls InsertEffect::setParameter() on the message thread - allowed from any
// thread (stage 1: an atomic store + a dirty bit; the audio thread applies it at the next
// block). A 15 Hz timer reads the values back (getParameter), so the panel also follows changes
// made elsewhere (automation, a second panel, a project load).
//
// The panel holds a reference to the effect. Whoever owns the panel must close it before the
// effect is removed (Session::onInsertWillBeRemoved -> MainComponent closes the window).

#include <dawlab/engine/InsertEffect.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace dawlab::app {

class ParameterPanel final : public juce::Component, private juce::Timer {
public:
    static constexpr int kCellWidth = 84;
    static constexpr int kCellHeight = 92;
    static constexpr int kHeaderHeight = 34;

    explicit ParameterPanel(engine::InsertEffect& effect);
    ~ParameterPanel() override;

    engine::InsertEffect& getEffect() noexcept { return effect_; }

    // Number of generated controls (= the effect's parameter count) and access for tests.
    int getNumControls() const noexcept { return static_cast<int>(controls_.size()); }
    juce::Slider* getSlider(int parameterIndex) const noexcept;
    juce::ToggleButton* getToggle(int parameterIndex) const noexcept;
    juce::ComboBox* getComboBox(int parameterIndex) const noexcept;
    juce::ToggleButton& getBypassButton() noexcept { return bypass_; }

    // Text for a value, "-18.0 dB" (built-in: formatParameterValue; VST3: the plugin's text).
    static juce::String formatValue(const engine::InsertEffect& effect, int index, float value);

    // Size that shows every control without scrolling (capped at 900 x 640).
    juce::Point<int> getIdealSize() const;

    // Pulls the current values from the effect (what the timer does).
    void refreshFromEffect();

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct Control;
    void timerCallback() override { refreshFromEffect(); }
    int getNumColumns() const noexcept;

    engine::InsertEffect& effect_;
    juce::ToggleButton bypass_{"Bypass"};
    juce::Label latencyLabel_;
    juce::Component content_; // declared before the viewport that shows it
    juce::Viewport viewport_;
    std::vector<std::unique_ptr<Control>> controls_;
    float meterDb_ = 0.0f;
};

// A top-level window holding a ParameterPanel. Closing it calls onClose (the owner deletes it).
class ParameterWindow final : public juce::DocumentWindow {
public:
    ParameterWindow(engine::InsertEffect& effect, const juce::String& title, juce::LookAndFeel* lookAndFeel);
    ~ParameterWindow() override;

    ParameterPanel& getPanel() noexcept { return *panel_; }
    std::function<void()> onClose;
    void closeButtonPressed() override;

private:
    ParameterPanel* panel_ = nullptr; // owned by the window (setContentOwned)
};

} // namespace dawlab::app
