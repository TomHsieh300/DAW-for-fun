// Exercise 12 (solution): a VST3 instrument on a track.
//
// Build and run:  l18_ex12_solution   (ctest, label "solution"; needs -Juce)
// Your version:   exercises/starter/ex12_plugin_instrument.cpp -> l18_ex12_starter

#include "../exercises/ex12_plugin_instrument.h"

namespace ex12 {

void PluginInstrument::prepare(double sampleRate, int maxBlockSize) {
    processor_->setRateAndBufferSizeDetails(sampleRate, maxBlockSize);
    processor_->prepareToPlay(sampleRate, maxBlockSize);
    const int channels = std::max({processor_->getTotalNumInputChannels(), processor_->getTotalNumOutputChannels(), 1});
    buffer_.setSize(channels, std::max(maxBlockSize, 1));
    midi_.ensureSize(static_cast<size_t>(kMaxEventsPerBlock) * 16); // a few bytes of header and data an event
}

void PluginInstrument::reset() noexcept {
    processor_->reset();
}

void PluginInstrument::render(const dawlab::engine::BlockEvent* events, int numEvents, float* const* out, int numChannels,
                              int numSamples) noexcept {
    numSamples = std::min(numSamples, buffer_.getNumSamples());
    midi_.clear();
    for (int e = 0; e < std::min(numEvents, kMaxEventsPerBlock); ++e) {
        const dawlab::engine::MidiMessage& m = events[e].message;
        const juce::uint8 bytes[3] = {m.status, m.data1, m.data2};
        midi_.addEvent(bytes, 1 + dawlab::engine::midiDataBytesOf(m.status), std::clamp(events[e].offset, 0, numSamples - 1));
    }
    // A view of the first numSamples of the buffer: setSize() with avoidReallocating keeps the memory.
    buffer_.setSize(buffer_.getNumChannels(), numSamples, false, false, true);
    buffer_.clear();
    processor_->processBlock(buffer_, midi_);
    const int produced = processor_->getTotalNumOutputChannels();
    for (int ch = 0; ch < numChannels; ++ch) {
        const int from = produced == 1 ? 0 : ch; // a mono processor to every channel
        if (from >= produced)
            break;
        const float* src = buffer_.getReadPointer(from);
        for (int i = 0; i < numSamples; ++i)
            out[ch][i] += src[i];
    }
}

juce::String PluginInstrument::getStateText() const {
    juce::MemoryBlock state;
    processor_->getStateInformation(state);
    return state.toBase64Encoding();
}

bool PluginInstrument::setStateText(const juce::String& text) {
    juce::MemoryBlock state;
    if (text.isEmpty() || !state.fromBase64Encoding(text))
        return false;
    processor_->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    return true;
}

std::unique_ptr<PluginInstrument> PluginInstrument::load(dawlab::host::PluginHost& host, const juce::File& file, double sampleRate,
                                                         int blockSize, juce::String& error) {
    std::unique_ptr<juce::AudioPluginInstance> instance = host.loadFromFile(file, sampleRate, blockSize, error);
    if (instance == nullptr)
        return nullptr;
    if (!instance->acceptsMidi() || instance->getTotalNumOutputChannels() == 0) {
        error = instance->getName() + " is not an instrument: it takes no MIDI or makes no sound";
        return nullptr;
    }
    return std::make_unique<PluginInstrument>(std::move(instance));
}

} // namespace ex12

#include "../exercises/ex12_check.h"
