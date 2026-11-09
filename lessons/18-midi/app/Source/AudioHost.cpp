#include "AudioHost.h"

#include <algorithm>

namespace dawlab::app {

AudioHost::AudioHost(engine::Engine& engine) : engine_(engine) { midiEvents_.reserve(engine::MidiFifo::kCapacity); }

AudioHost::~AudioHost() { closeDevice(); }

juce::String AudioHost::openDefaultDevice(int numInputChannels, int numOutputChannels) {
    // initialise() picks the default device type and device; with a null XML state it opens the
    // default output at the device's preferred sample rate and buffer size.
    const juce::String error = deviceManager_.initialise(numInputChannels, numOutputChannels, nullptr, true);
    if (error.isNotEmpty())
        return error;
    if (deviceManager_.getCurrentAudioDevice() == nullptr)
        return "No audio output device found";
    deviceOpen_ = true;
    setEngineAttached(true); // -> audioDeviceAboutToStart() -> Engine::prepare()
    return {};
}

void AudioHost::closeDevice() {
    deviceManager_.removeMidiInputDeviceCallback({}, this); // chapter 18: no more keys from the driver's thread
    setEngineAttached(false);
    deviceManager_.closeAudioDevice();
    deviceOpen_ = false;
}

bool AudioHost::isDeviceOpen() const { return deviceOpen_ && deviceManager_.getCurrentAudioDevice() != nullptr; }

void AudioHost::setEngineAttached(bool attached) {
    if (attached == attached_)
        return;
    attached_ = attached;
    if (attached)
        deviceManager_.addAudioCallback(this);    // calls audioDeviceAboutToStart() if a device runs
    else
        deviceManager_.removeAudioCallback(this); // returns when no callback is running any more
}

juce::String AudioHost::getDeviceDescription() const {
    if (auto* device = deviceManager_.getCurrentAudioDevice())
        return device->getTypeName() + ": " + device->getName() + ", " +
               juce::String(device->getCurrentSampleRate(), 0) + " Hz, " +
               juce::String(device->getCurrentBufferSizeSamples()) + " samples";
    return "No audio device (offline)";
}

// ---- preparing ----------------------------------------------------------------------------------

void AudioHost::prepareInternal(double sampleRate, int blockSize) {
    const double oldRate = engine_.getSampleRate();
    const bool unchanged = engine_.isPrepared() && oldRate == sampleRate && engine_.getMaxBlockSize() >= blockSize;
    if (!unchanged) {
        // The device does not run while we are here, so the engine may allocate and re-prepare
        // its inserts (Engine::prepare is documented for exactly this moment).
        engine_.prepare(sampleRate, std::max(blockSize, 512));
    }
    if (!analyzerReady_.load() || sampleRate_.load() != sampleRate) {
        analyzerReady_.store(false);
        analyzer_.prepare(sampleRate, 12); // 4096-point FFT: 11.7 Hz bins at 48 kHz
        analyzerReady_.store(true);
    }
    // A device may deliver more than its nominal buffer size; Engine::processBlock() splits long
    // blocks itself, the scratch buffer for the analyzer just needs some size.
    monoScratch_.assign(static_cast<size_t>(std::max(blockSize, 512)), 0.0f);
    sampleRate_.store(sampleRate);
    blockSize_.store(blockSize);
    if (!unchanged && onEnginePrepared)
        onEnginePrepared(oldRate, sampleRate);
}

void AudioHost::audioDeviceAboutToStart(juce::AudioIODevice* device) {
    // The driver's estimate of the time from our callback to the converter, in samples.
    outputLatency_.store(std::max(0, device->getOutputLatencyInSamples()));
    inputLatency_.store(std::max(0, device->getInputLatencyInSamples()));
    prepareInternal(device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
}

// No callback runs any more and none is on its way, so this is the moment that can say a recording is
// over without one (Step 6). The takes are finished by the disk thread.
void AudioHost::audioDeviceStopped() { engine_.finishRecordingsWithoutAudio(); }

void AudioHost::prepareOffline(double sampleRate, int blockSize) {
    outputLatency_.store(0); // no device, no device buffers
    inputLatency_.store(0);
    prepareInternal(sampleRate, blockSize);
}

// ---- the audible position ---------------------------------------------------------------------

int64_t AudioHost::compensatePosition(int64_t position, bool playing, int engineLatencySamples,
                                      int deviceOutputLatencySamples) noexcept {
    if (!playing)
        return position;
    const int64_t behind = static_cast<int64_t>(std::max(0, engineLatencySamples)) +
                           static_cast<int64_t>(std::max(0, deviceOutputLatencySamples));
    return std::max<int64_t>(0, position - behind);
}

int64_t AudioHost::getAudiblePosition() const noexcept {
    const engine::Transport& tr = engine_.getTransport();
    return compensatePosition(tr.getPosition(), tr.isPlaying(), engine_.getLatencySamples(),
                              getDeviceOutputLatencySamples());
}

void AudioHost::processOffline(float* const* outputs, int numOutputChannels, int numSamples, engine::InputBlock input) noexcept {
    render(outputs, numOutputChannels, numSamples, input);
}

// ---- the input --------------------------------------------------------------------------------------

juce::String AudioHost::ensureInputsOpen() {
    juce::AudioIODevice* device = deviceManager_.getCurrentAudioDevice();
    if (device == nullptr)
        return {}; // offline: the input comes from whoever calls processOffline()
    // Opened without inputs, a device of a type with separate input devices (WASAPI, DirectSound) has
    // none chosen at all: choose the system's default input first.
    juce::AudioDeviceManager::AudioDeviceSetup setup = deviceManager_.getAudioDeviceSetup();
    if (device->getInputChannelNames().isEmpty() && setup.inputDeviceName.isEmpty()) {
        if (juce::AudioIODeviceType* type = deviceManager_.getCurrentDeviceTypeObject(); type != nullptr && type->hasSeparateInputsAndOutputs()) {
            const juce::StringArray inputs = type->getDeviceNames(true);
            const int chosen = type->getDefaultDeviceIndex(true);
            if (!inputs.isEmpty()) {
                setup.inputDeviceName = inputs[std::max(0, chosen)];
                setup.useDefaultInputChannels = true;
                if (const juce::String error = deviceManager_.setAudioDeviceSetup(setup, true); error.isNotEmpty())
                    return error;
                device = deviceManager_.getCurrentAudioDevice();
                if (device == nullptr)
                    return "The device did not start again with its input";
                setup = deviceManager_.getAudioDeviceSetup();
            }
        }
    }
    const int available = device->getInputChannelNames().size();
    if (available == 0)
        return "This audio device has no inputs. Choose another one in Audio settings";
    if (device->getActiveInputChannels().countNumberOfSetBits() == available)
        return {};
    setup.useDefaultInputChannels = false;
    setup.inputChannels.setRange(0, available, true);
    return deviceManager_.setAudioDeviceSetup(setup, true);
}

juce::StringArray AudioHost::getInputNames() const {
    if (juce::AudioIODevice* device = deviceManager_.getCurrentAudioDevice())
        return device->getInputChannelNames();
    return {"Input 1", "Input 2"}; // offline
}

engine::Latencies AudioHost::getDeviceLatencies(int trimSamples) const noexcept {
    engine::Latencies latencies;
    latencies.input = inputLatency_.load(std::memory_order_relaxed);
    latencies.output = outputLatency_.load(std::memory_order_relaxed);
    latencies.trim = trimSamples;
    return latencies;
}

// ---- the keyboard (chapter 18) ----------------------------------------------------------------------

double AudioHost::now() const { return clock_ ? clock_() : juce::Time::getMillisecondCounterHiRes() * 0.001; }

juce::StringArray AudioHost::openMidiInputs() {
    juce::StringArray names;
    for (const juce::MidiDeviceInfo& device : juce::MidiInput::getAvailableDevices()) {
        if (!deviceManager_.isMidiInputDeviceEnabled(device.identifier))
            deviceManager_.setMidiInputDeviceEnabled(device.identifier, true);
        names.add(device.name);
    }
    deviceManager_.removeMidiInputDeviceCallback({}, this); // never twice
    deviceManager_.addMidiInputDeviceCallback({}, this);    // {}: every enabled input
    return names;
}

juce::StringArray AudioHost::getMidiInputNames() const {
    juce::StringArray names;
    for (const juce::MidiDeviceInfo& device : juce::MidiInput::getAvailableDevices())
        if (deviceManager_.isMidiInputDeviceEnabled(device.identifier))
            names.add(device.name);
    return names;
}

void AudioHost::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& m) {
    // The driver's thread. JUCE has already parsed the bytes (Step 5's MidiParser does the same) and stamped
    // the message with Time::getMillisecondCounterHiRes() / 1000 - our now().
    const juce::uint8* raw = m.getRawData();
    if (m.getRawDataSize() < 1 || raw[0] < 0x80 || raw[0] >= 0xF0)
        return; // only channel messages
    engine::MidiMessage message;
    message.status = raw[0];
    message.data1 = m.getRawDataSize() > 1 ? raw[1] : 0;
    message.data2 = m.getRawDataSize() > 2 ? raw[2] : 0;
    pushMidi({m.getTimeStamp(), message});
}

void AudioHost::pushMidi(const engine::TimedMidi& message) noexcept {
    playFifo_.push(message);
    recordFifo_.push(message);
}

void AudioHost::publishAnchor(const Anchor& a) noexcept {
    const uint64_t v = anchorVersion_.load(std::memory_order_relaxed);
    anchorVersion_.store(v + 1, std::memory_order_relaxed); // odd: writing
    std::atomic_thread_fence(std::memory_order_release);
    anchorTime_.store(a.blockTime, std::memory_order_relaxed);
    anchorSample_.store(a.songSample, std::memory_order_relaxed);
    anchorRate_.store(a.sampleRate, std::memory_order_relaxed);
    anchorMix_.store(a.mixLatency, std::memory_order_relaxed);
    anchorPlaying_.store(a.playing, std::memory_order_relaxed);
    anchorBlock_.store(a.block, std::memory_order_relaxed);
    anchorRendered_.store(a.renderedBefore, std::memory_order_relaxed);
    anchorNumSamples_.store(a.numSamples, std::memory_order_relaxed);
    anchorVersion_.store(v + 2, std::memory_order_release); // even: done
}

bool AudioHost::getAnchor(Anchor& out) const noexcept {
    for (int attempt = 0; attempt < 1000; ++attempt) {
        const uint64_t v0 = anchorVersion_.load(std::memory_order_acquire);
        if (v0 % 2 != 0)
            continue;
        Anchor a;
        a.blockTime = anchorTime_.load(std::memory_order_relaxed);
        a.songSample = anchorSample_.load(std::memory_order_relaxed);
        a.sampleRate = anchorRate_.load(std::memory_order_relaxed);
        a.mixLatency = anchorMix_.load(std::memory_order_relaxed);
        a.playing = anchorPlaying_.load(std::memory_order_relaxed);
        a.block = anchorBlock_.load(std::memory_order_relaxed);
        a.renderedBefore = anchorRendered_.load(std::memory_order_relaxed);
        a.numSamples = anchorNumSamples_.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (anchorVersion_.load(std::memory_order_relaxed) == v0) {
            out = a;
            return v0 != 0;
        }
    }
    return false;
}

// ---- the audio callback -------------------------------------------------------------------------

void AudioHost::audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels,
                                                 float* const* outputChannelData, int numOutputChannels,
                                                 int numSamples, const juce::AudioIODeviceCallbackContext& context) {
    juce::ignoreUnused(context);
    render(outputChannelData, numOutputChannels, numSamples, engine::InputBlock{inputChannelData, numInputChannels});
}

void AudioHost::render(float* const* outputs, int numOutputChannels, int numSamples, engine::InputBlock input) noexcept {
    if (numOutputChannels <= 0 || numSamples <= 0)
        return;
    const juce::ScopedNoDenormals noDenormals; // FTZ/DAZ: denormals cost 100x on x86

    // The engine renders stereo into the first two device channels, directly in the device's
    // memory: AudioBlock is just a view of JUCE's channel pointer array.
    const int engineChannels = std::min(numOutputChannels, engine::kNumChannels);

    // Chapter 18: the keyboard. This block's first sample is heard after the device's output latency; a key is
    // played a fixed delay after it arrived - two blocks and that latency, the worst case of Step 5 - so every
    // note is late by the same amount instead of by however the block periods fell.
    const double rate = sampleRate_.load(std::memory_order_relaxed);
    const double outputSeconds = rate > 0.0 ? outputLatency_.load(std::memory_order_relaxed) / rate : 0.0;
    const double blockTime = now() + outputSeconds;
    long late = 0;
    if (rate > 0.0)
        engine::drainMidi(playFifo_, blockTime, numSamples, rate, engine::MidiTiming::Scheduled,
                          2.0 * numSamples / rate + outputSeconds, midiEvents_, late);
    else
        midiEvents_.clear();
    if (late > 0)
        lateMidi_.fetch_add(late, std::memory_order_relaxed);
    const engine::Transport& tr = engine_.getTransport();
    const bool playing = tr.isPlaying();
    publishAnchor({blockTime, tr.getPosition(), rate, engine_.getLatencySamples(), playing, callbacks_.load(std::memory_order_relaxed) + 1,
                   renderedWhilePlaying_, numSamples});
    if (playing)
        renderedWhilePlaying_ += numSamples;

    engine_.processBlock(AudioBlock(outputs, engineChannels, numSamples), input,
                         engine::MidiBlock{midiEvents_.data(), static_cast<int>(midiEvents_.size())});
    for (int ch = engineChannels; ch < numOutputChannels; ++ch)
        if (outputs[ch] != nullptr)
            std::fill_n(outputs[ch], numSamples, 0.0f);

    // Spectrum of the master (mono sum), in chunks of the preallocated scratch buffer.
    if (analyzerReady_.load(std::memory_order_acquire) && !monoScratch_.empty()) {
        const float* left = outputs[0];
        const float* right = outputs[engineChannels > 1 ? 1 : 0];
        int done = 0;
        while (done < numSamples) {
            const int n = std::min(numSamples - done, static_cast<int>(monoScratch_.size()));
            for (int i = 0; i < n; ++i)
                monoScratch_[static_cast<size_t>(i)] = 0.5f * (left[done + i] + right[done + i]);
            analyzer_.pushSamples(monoScratch_.data(), n);
            done += n;
        }
    }

    callbacks_.fetch_add(1, std::memory_order_relaxed);
}

} // namespace dawlab::app
