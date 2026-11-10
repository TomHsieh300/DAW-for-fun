#include "PluginInstrument.h"

#include <algorithm>

namespace dawlab::app {

PluginInstrument::PluginInstrument(std::unique_ptr<juce::AudioProcessor> processor) : processor_(std::move(processor)) {}

PluginInstrument::~PluginInstrument() {
    if (prepared_)
        processor_->releaseResources();
}

void PluginInstrument::prepare(double sampleRate, int maxBlockSize) {
    if (prepared_)
        processor_->releaseResources();
    processor_->setRateAndBufferSizeDetails(sampleRate, maxBlockSize);
    processor_->prepareToPlay(sampleRate, maxBlockSize);
    prepared_ = true;
    const int channels = std::max({processor_->getTotalNumInputChannels(), processor_->getTotalNumOutputChannels(), 1});
    buffer_.setSize(channels, std::max(maxBlockSize, 1));
    midi_.ensureSize(static_cast<size_t>(kMaxEventsPerBlock) * 16);
    latency_.store(std::max(0, processor_->getLatencySamples()), std::memory_order_relaxed);
}

void PluginInstrument::reset() noexcept {
    processor_->reset();
}

void PluginInstrument::render(const engine::BlockEvent* events, int numEvents, float* const* out, int numChannels, int numSamples) noexcept {
    midi_.clear();
    for (int e = 0; e < std::min(numEvents, kMaxEventsPerBlock); ++e) {
        const engine::MidiMessage& m = events[e].message;
        const juce::uint8 bytes[3] = {m.status, m.data1, m.data2};
        midi_.addEvent(bytes, 1 + engine::midiDataBytesOf(m.status), std::clamp(events[e].offset, 0, std::max(numSamples - 1, 0)));
    }
    // A view of the first numSamples of the buffer: avoidReallocating keeps the memory.
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
    latency_.store(std::max(0, processor_->getLatencySamples()), std::memory_order_relaxed); // it may change with a setting
}

juce::MemoryBlock PluginInstrument::getState() const {
    juce::MemoryBlock state;
    processor_->getStateInformation(state);
    return state;
}

void PluginInstrument::setState(const juce::MemoryBlock& state) {
    if (state.getSize() > 0)
        processor_->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
}

} // namespace dawlab::app
