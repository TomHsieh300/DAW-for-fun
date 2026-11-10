// Exercise 12 (starter): a VST3 instrument on a track.
//
// Fill in the TODOs. Until then the self-check prints FAIL (expected). The solution is
// solutions/ex12_plugin_instrument.cpp. What the functions must do is in ../ex12_plugin_instrument.h.

#include "../ex12_plugin_instrument.h"

namespace ex12 {

void PluginInstrument::prepare(double sampleRate, int maxBlockSize) {
    // TODO: setRateAndBufferSizeDetails() and prepareToPlay(); buffer_ for the processor's channels (inputs or
    //       outputs, whichever is more, at least 1) and maxBlockSize samples; midi_.ensureSize() for
    //       kMaxEventsPerBlock events.
    (void)sampleRate;
    (void)maxBlockSize;
}

void PluginInstrument::reset() noexcept {
    // TODO
}

void PluginInstrument::render(const dawlab::engine::BlockEvent* events, int numEvents, float* const* out, int numChannels,
                              int numSamples) noexcept {
    // TODO: the events into midi_ (MidiBuffer::addEvent(bytes, size, offset)); buffer_ to numSamples without
    //       reallocating (setSize(..., avoidReallocating = true)), cleared; processBlock(); ADD its output to out.
    (void)events;
    (void)numEvents;
    (void)out;
    (void)numChannels;
    (void)numSamples;
}

juce::String PluginInstrument::getStateText() const {
    // TODO: getStateInformation() into a MemoryBlock, toBase64Encoding().
    return {};
}

bool PluginInstrument::setStateText(const juce::String& text) {
    // TODO: fromBase64Encoding(), then setStateInformation().
    (void)text;
    return false;
}

std::unique_ptr<PluginInstrument> PluginInstrument::load(dawlab::host::PluginHost& host, const juce::File& file, double sampleRate,
                                                         int blockSize, juce::String& error) {
    // TODO: host.loadFromFile(); refuse what takes no MIDI or has no output ("... is not an instrument").
    (void)host;
    (void)file;
    (void)sampleRate;
    (void)blockSize;
    error = "not done yet";
    return nullptr;
}

} // namespace ex12

#include "../ex12_check.h"
