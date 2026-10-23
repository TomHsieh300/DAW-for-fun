#include "AudioHost.h"

#include <algorithm>

namespace dawlab::app {

AudioHost::AudioHost(engine::Engine& engine) : engine_(engine) {}

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
    prepareInternal(device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
}

void AudioHost::audioDeviceStopped() { cpuLoad_.store(0.0f); }

void AudioHost::prepareOffline(double sampleRate, int blockSize) {
    outputLatency_.store(0); // no device, no device buffers
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

void AudioHost::processOffline(float* const* outputs, int numOutputChannels, int numSamples) noexcept {
    render(outputs, numOutputChannels, numSamples);
}

// ---- the audio callback -------------------------------------------------------------------------

void AudioHost::audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels,
                                                 float* const* outputChannelData, int numOutputChannels,
                                                 int numSamples, const juce::AudioIODeviceCallbackContext& context) {
    juce::ignoreUnused(inputChannelData, numInputChannels, context); // inputs: Exercise 9
    render(outputChannelData, numOutputChannels, numSamples);
}

void AudioHost::render(float* const* outputs, int numOutputChannels, int numSamples) noexcept {
    if (numOutputChannels <= 0 || numSamples <= 0)
        return;
    const juce::ScopedNoDenormals noDenormals; // FTZ/DAZ: denormals cost 100x on x86
    const auto t0 = juce::Time::getHighResolutionTicks();

    // The engine renders stereo into the first two device channels, directly in the device's
    // memory: AudioBlock is just a view of JUCE's channel pointer array.
    const int engineChannels = std::min(numOutputChannels, engine::kNumChannels);
    engine_.processBlock(AudioBlock(outputs, engineChannels, numSamples));
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

    // CPU load = time spent / time available, smoothed (one-pole, ~10 callbacks).
    const double sr = sampleRate_.load(std::memory_order_relaxed);
    if (sr > 0.0) {
        const double spent = juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - t0);
        const double available = numSamples / sr;
        const float load = static_cast<float>(spent / available);
        const float old = cpuLoad_.load(std::memory_order_relaxed);
        cpuLoad_.store(old + 0.1f * (load - old), std::memory_order_relaxed);
    }
    callbacks_.fetch_add(1, std::memory_order_relaxed);
}

} // namespace dawlab::app
