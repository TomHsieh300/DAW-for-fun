#pragma once
// PluginInstrument: a JUCE AudioProcessor - a VST3 instrument from chapter 13's PluginHost - as the engine's
// Instrument (chapter 19, Step 6; chapter 18's Exercise 12 in the app).
//
//   prepare()   the processor at the engine's rate and block size; a buffer for its channels and a MidiBuffer with
//               room for a block's events, so that render() never allocates.
//   render()    the block's events into the MidiBuffer at their offsets, processBlock(), and its output ADDED to the
//               track's channels (a mono processor to every one).
//   latency     the processor's, read in prepare() and after every block: the engine's delay compensation waits
//               for it (Step 3).
//   state       getState() / setState() on the message thread, as chapter 13's HostedPlugin does for an insert.
//
// What the engine still does for it: the MIDI player ends every note on a stop, a seek and a loop, so the plugin
// needs no "all notes off". Its editor is not shown (an insert's is, chapter 13): a limit of this chapter.

#include <dawlab/engine/Midi.h>

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>

namespace dawlab::app {

class PluginInstrument final : public engine::Instrument {
public:
    static constexpr int kMaxEventsPerBlock = 512;

    explicit PluginInstrument(std::unique_ptr<juce::AudioProcessor> processor);
    ~PluginInstrument() override;

    void prepare(double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void render(const engine::BlockEvent* events, int numEvents, float* const* out, int numChannels, int numSamples) noexcept override;
    int getLatencySamples() const noexcept override { return latency_.load(std::memory_order_relaxed); }

    // Message thread.
    juce::MemoryBlock getState() const;
    void setState(const juce::MemoryBlock& state);
    juce::String getName() const { return processor_->getName(); }
    juce::AudioProcessor& getProcessor() noexcept { return *processor_; }

    // True for a processor that can be an instrument: it takes MIDI and has an output.
    static bool isInstrument(const juce::AudioProcessor& processor) {
        return processor.acceptsMidi() && processor.getTotalNumOutputChannels() > 0;
    }

private:
    std::unique_ptr<juce::AudioProcessor> processor_;
    juce::AudioBuffer<float> buffer_;
    juce::MidiBuffer midi_;
    std::atomic<int> latency_{0};
    bool prepared_ = false;
};

} // namespace dawlab::app
