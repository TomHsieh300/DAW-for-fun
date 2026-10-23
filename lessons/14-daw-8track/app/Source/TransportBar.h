#pragma once
// TransportBar: the strip at the top of the window (Step 13).
//
//   [|<] [>] [loop]   3.2.480   0:04.250   [ 120.0 BPM ]   master [=====|==  ]  M -14.2 S -15.0 I -15.5   CPU 12%
//
// Buttons call back into MainComponent (which owns the commands, so the space bar, the menu and
// the buttons all do the same thing). Everything shown is *read* from atomics in update(),
// which MainComponent's 30 Hz timer calls:
//
//   position (what is heard right now)    AudioHost::getAudiblePosition(): Transport position minus
//                                         the mix latency and the device output latency
//   playing / loop                        Transport (atomics, stage 1)
//   master peak                           Mixer::getMaster().getPeakDb() / getPeakHoldDb()
//   LUFS                                  Mixer::getMaster().getMomentary/ShortTerm/IntegratedLufs()
//   CPU                                   AudioHost::getCpuLoad() (time in callback / block time)
//
// The tempo slider writes Transport::setTempo() (an atomic). Clips are placed in samples, so a
// tempo change moves the bar grid, not the audio (stage 1, Exercise 7).

#include "Meters.h"

#include <dawlab/engine/Engine.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace dawlab::app {

class AudioHost;

class TransportBar final : public juce::Component {
public:
    TransportBar(engine::Engine& engine, AudioHost& audioHost);
    ~TransportBar() override;

    std::function<void()> onPlayStop;
    std::function<void()> onReturnToStart;
    std::function<void()> onToggleLoop;

    // Pulls the current state from the engine (message thread, ~30 Hz).
    void update();

    // "3.2.480" (bar.beat.tick, 1-based bar and beat) and "1:04.250" (m:ss.mmm).
    static juce::String formatBarsBeats(const engine::BarsBeats& position);
    static juce::String formatTime(double seconds);

    juce::String getPositionText() const { return barsBeats_.getText(); }
    juce::String getTimeText() const { return time_.getText(); }
    juce::String getCpuText() const { return cpu_.getText(); }
    const MeterBar& getMasterMeter() const noexcept { return masterMeter_; }

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    class IconButton;

    engine::Engine& engine_;
    AudioHost& audioHost_;
    std::unique_ptr<IconButton> returnButton_, playButton_, loopButton_;
    juce::Label barsBeats_, time_, cpu_, masterLabel_;
    juce::Slider tempo_{juce::Slider::LinearBar, juce::Slider::TextBoxLeft};
    MeterBar masterMeter_{MeterBar::Orientation::Horizontal};
    LoudnessDisplay loudness_;
};

} // namespace dawlab::app
