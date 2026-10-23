#pragma once
// MixerView: 8 track strips, the FX bus strips and the master section (Steps 15 and 20).
//
//   track strip (x8)          bus strip (x2)       master section
//   +-----------+            +-----------+        +-----------+---------------------+
//   | 1 Drums   |  name      | Reverb    |        | Master    |  spectrum (master)  |
//   | [Load...] |  file      |           |        | [slot 1]  |                     |
//   | [slot 1 v]|  inserts   | [slot 1 v]|        | ...       |                     |
//   | ...  x4   |            | ...  x4   |        | (Ceiling) |                     |
//   | (A) (B)   |  sends     |           |        |  |  ||    |---------------------|
//   |   (pan)   |            |   (bal)   |        |  |  ||    |  M  S  I  (LUFS)    |
//   |  |  ||    |  fader +   |  |  ||    |        |           |                     |
//   |  |  ||    |  meter     |  |  ||    |        |           |                     |
//   | [M] [S]   |            | [M]       |        |           |                     |
//   +-----------+            +-----------+        +-----------+---------------------+
//
// Every control writes straight into the engine's atomics (ChannelStrip::setGainDb, setPan,
// setSendLevelDb, setMute, setSolo). Inserts go through Session (command queue). update() is
// called by MainComponent's 30 Hz timer: meters, and controls that changed elsewhere.
//
// Fader law (FaderLaw below): a fader position p in [0, 1] maps to
//
//   dB(p) = 12 - 72 (1 - p)^2          p(dB) = 1 - sqrt((12 - dB) / 72)
//
// so +12 dB is at the top, 0 dB at p = 0.59, -12 dB at 0.42, -30 dB at 0.24 and -60 dB at the
// bottom (shown as -inf). The square spends most of the travel on the musically useful range
// around 0 dB, like the scale printed next to a real fader.

#include "Meters.h"
#include "Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace dawlab::app {

struct FaderLaw {
    static constexpr float kTopDb = engine::kMaxFaderDb; // +12
    static constexpr float kRangeDb = 72.0f;             // top - bottom
    static constexpr float kBottomDb = kTopDb - kRangeDb; // -60 = "-inf"

    static float positionToDb(float position) noexcept;
    static float dbToPosition(float db) noexcept;
    // The engine value for a fader dB value: the bottom of the travel means silence.
    static float toEngineDb(float faderDb) noexcept;
    static juce::String format(float db); // "-6.0 dB", "-inf"
};

class ChannelStripView final : public juce::Component {
public:
    static constexpr int kTrackWidth = 98;
    static constexpr int kMasterWidth = 104;

    ChannelStripView(Session& session, engine::ChannelRef ref);
    ~ChannelStripView() override;

    engine::ChannelRef getChannel() const noexcept { return ref_; }

    // Callbacks (MainComponent connects them)
    std::function<void(int track)> onLoadRequested;
    // slot clicked: menu == true for the drop-down arrow / right click, false for the slot body
    std::function<void(engine::ChannelRef, int slot, juce::Component& anchor, bool menu)> onSlotClicked;

    void update();            // meters + values changed elsewhere (30 Hz)
    void refreshStructure();  // names of strip and slots (after Session changes)

    // Access for tests and the snapshot
    juce::Slider& getFader() noexcept { return fader_; }
    juce::Slider& getPan() noexcept { return pan_; }
    juce::Slider* getSendKnob(int bus) noexcept;
    juce::Button* getSlotButton(int slot) noexcept;
    juce::Button& getMuteButton() noexcept { return mute_; }
    juce::Button& getSoloButton() noexcept { return solo_; }
    const MeterBar& getMeter() const noexcept { return meter_; }
    int getNumSendKnobs() const noexcept { return static_cast<int>(sends_.size()); }

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    class SlotButton;
    engine::ChannelStrip& strip() noexcept;

    Session& session_;
    engine::ChannelRef ref_;
    juce::Label name_;
    juce::TextButton load_{"Load..."};
    std::vector<std::unique_ptr<SlotButton>> slots_;
    std::vector<std::unique_ptr<juce::Slider>> sends_;
    std::vector<std::unique_ptr<juce::Label>> sendLabels_;
    juce::Slider pan_{juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox};
    juce::Slider ceiling_{juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox};
    juce::Label panLabel_, ceilingLabel_, gainLabel_;
    juce::Slider fader_{juce::Slider::LinearVertical, juce::Slider::NoTextBox};
    MeterBar meter_{MeterBar::Orientation::Vertical};
    juce::TextButton mute_{"M"}, solo_{"S"};
};

class MixerView final : public juce::Component, private juce::ChangeListener {
public:
    explicit MixerView(Session& session);
    ~MixerView() override;

    // Forwarded to every strip
    std::function<void(int track)> onLoadRequested;
    std::function<void(engine::ChannelRef, int slot, juce::Component& anchor, bool menu)> onSlotClicked;

    void update(); // 30 Hz: strips + loudness (the spectrum is updated by updateSpectrum)
    bool updateSpectrum(dsp::SpectrumAnalyzer& analyzer) { return spectrum_.update(analyzer); }

    ChannelStripView& getStrip(int stripIndex) noexcept { return *strips_[static_cast<size_t>(stripIndex)]; }
    int getNumStrips() const noexcept { return static_cast<int>(strips_.size()); }
    const LoudnessDisplay& getLoudnessDisplay() const noexcept { return loudness_; }

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

    Session& session_;
    std::vector<std::unique_ptr<ChannelStripView>> strips_;
    SpectrumView spectrum_;
    LoudnessDisplay loudness_;
    juce::Label spectrumLabel_, loudnessLabel_, latencyLabel_;
};

} // namespace dawlab::app
