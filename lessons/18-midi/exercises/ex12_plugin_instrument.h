#pragma once
// Exercise 12 - A VST3 instrument on a track (optional, JUCE).
//
// Exercise 7 designed it; the engine now has the hook (Step 7, "Added after the chapter"): an engine::Instrument
// handed to Engine::setTrackInstrument() plays a track's notes instead of the built-in synth. This exercise wraps a
// JUCE AudioProcessor - which is what chapter 13's PluginHost gives for a VST3 file - as such an Instrument,
// without changing the app:
//
//   prepare()   the processor at the engine's rate and block size; a buffer for its channels and a MidiBuffer
//               with room for a block's events, so that render() never allocates.
//   render()    the block's events into the MidiBuffer at their offsets (their bytes: two for a program change or
//               channel pressure, three for the rest - engine::midiDataBytesOf()), the processor's processBlock(),
//               and its output ADDED to the track's channels: channel c to channel c; a mono processor to every one.
//   reset()     the processor's reset(): silent at once.
//   load()      a VST3 file through chapter 13's PluginHost. A plugin that takes no MIDI, or makes no sound, is
//               not an instrument: refused, with a reason.
//   state       the processor's state as base64 text, and back - what a project file would keep.
//
// What the engine still does for it: the ClipPlayer's table ends every note on a stop, a seek and a loop, so the
// plugin never needs an "all notes off" (Exercise 7, question 2). What it does not do: the plugin's latency is not
// compensated, and its editor is not shown.

#include "MainComponent.h"

#include <dawlab/host/PluginHost.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

namespace ex12 {

class PluginInstrument final : public dawlab::engine::Instrument {
public:
    static constexpr int kMaxEventsPerBlock = 512;

    explicit PluginInstrument(std::unique_ptr<juce::AudioProcessor> processor) : processor_(std::move(processor)) {}

    // ---- YOUR TURN (starter/ex12_plugin_instrument.cpp) ----------------------------------------------------------
    void prepare(double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void render(const dawlab::engine::BlockEvent* events, int numEvents, float* const* out, int numChannels, int numSamples) noexcept override;

    // The processor's state (getStateInformation) as base64; setStateText() false for text that is not base64.
    juce::String getStateText() const;
    bool setStateText(const juce::String& text);

    // A VST3 file as an instrument, or nullptr and why.
    static std::unique_ptr<PluginInstrument> load(dawlab::host::PluginHost& host, const juce::File& file, double sampleRate,
                                                  int blockSize, juce::String& error);

    // ---- provided -------------------------------------------------------------------------------------------------
    juce::AudioProcessor& getProcessor() noexcept { return *processor_; }

private:
    std::unique_ptr<juce::AudioProcessor> processor_;
    juce::AudioBuffer<float> buffer_;
    juce::MidiBuffer midi_;
};

} // namespace ex12
