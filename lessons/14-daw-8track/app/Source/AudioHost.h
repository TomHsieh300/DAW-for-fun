#pragma once
// AudioHost: connects the engine to a real audio device (Step 11).
//
// JUCE's AudioDeviceManager opens the device (WASAPI, DirectSound or ASIO on Windows) and calls
// our juce::AudioIODeviceCallback on its high-priority audio thread:
//
//   audioDeviceAboutToStart(device)    the device is about to run: sample rate and buffer size
//                                      are known, no callback is running -> Engine::prepare()
//   audioDeviceIOCallbackWithContext   every buffer: wrap JUCE's float** as a dawlab::AudioBlock
//      (inputs, outputs, n)            (no copy) and call Engine::processBlock(), then feed the
//                                      spectrum analyzer and measure the CPU load
//   audioDeviceStopped()               the device stopped (settings changed, app closing)
//
// There is no conversion layer in between: JUCE hands us non-interleaved float channel
// pointers, which is exactly what AudioBlock views. The callback is real-time code: no
// allocation, no locks, no logging.
//
// Without a device (the smoke test, the snapshot, a machine without audio output) the same
// render path is driven by hand: prepareOffline() + processOffline().

#include <dawlab/dsp/SpectrumAnalyzer.h>
#include <dawlab/engine/Engine.h>

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <vector>

namespace dawlab::app {

class AudioHost final : private juce::AudioIODeviceCallback {
public:
    explicit AudioHost(engine::Engine& engine);
    ~AudioHost() override;

    // Opens the system's default output device (stereo; `numInputChannels` inputs for
    // recording, Exercise 9). Returns an error message, empty on success.
    juce::String openDefaultDevice(int numInputChannels = 0, int numOutputChannels = 2);
    void closeDevice();
    bool isDeviceOpen() const;
    juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager_; }

    // Detaches / re-attaches the engine from the device (the device keeps running and outputs
    // silence). The export uses it: after setEngineAttached(false) returns, no device callback
    // touches the engine any more (JUCE removes callbacks under its audio lock).
    void setEngineAttached(bool attached);
    bool isEngineAttached() const noexcept { return attached_; }

    // ---- without a device -----------------------------------------------------------------
    // Prepares the engine and the analyzer as audioDeviceAboutToStart() would.
    void prepareOffline(double sampleRate, int blockSize);
    // Runs exactly what the device callback runs, on the calling thread.
    void processOffline(float* const* outputs, int numOutputChannels, int numSamples) noexcept;

    // ---- status (any thread) --------------------------------------------------------------
    double getSampleRate() const noexcept { return sampleRate_.load(); }
    int getBlockSize() const noexcept { return blockSize_.load(); }
    float getCpuLoad() const noexcept { return cpuLoad_.load(std::memory_order_relaxed); } // 0..1, smoothed
    uint64_t getNumCallbacks() const noexcept { return callbacks_.load(std::memory_order_relaxed); }
    juce::String getDeviceDescription() const;

    // ---- what the listener hears (Steps 13, 14) ---------------------------------------------
    // The transport position is where the engine *renders*. The sound of that sample leaves the
    // speakers later: first the mix latency (PDC, Step 8: the demo song's 752 samples), then the
    // device's own output latency (driver and converter buffers after our callback). Every
    // display of "where we are" - the bars.beats.ticks / m:ss counters and the playhead - shows
    //
    //     audible = max(0, position - engineLatency - deviceOutputLatency)    while playing
    //     audible = position                                                  while stopped
    //
    // (Stopped, nothing sounds: the display shows where playback will start.) Message thread.
    int64_t getAudiblePosition() const noexcept;
    // The pure formula, so tests can feed it any latencies.
    static int64_t compensatePosition(int64_t position, bool playing, int engineLatencySamples,
                                      int deviceOutputLatencySamples) noexcept;
    // What the open device reports as its output latency, in samples (0 without a device).
    int getDeviceOutputLatencySamples() const noexcept { return outputLatency_.load(std::memory_order_relaxed); }

    // The master output, mono (L+R)/2, for the spectrum display. computeSpectrum() is for the
    // message thread only.
    dsp::SpectrumAnalyzer& getSpectrumAnalyzer() noexcept { return analyzer_; }
    bool isAnalyzerReady() const noexcept { return analyzerReady_.load(); }

    // Called after the engine was prepared for a device start, on the thread that started the
    // device (normally the message thread), with the previous and the new sample rate.
    std::function<void(double oldRate, double newRate)> onEnginePrepared;

private:
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels,
                                          float* const* outputChannelData, int numOutputChannels, int numSamples,
                                          const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    void prepareInternal(double sampleRate, int blockSize);
    void render(float* const* outputs, int numOutputChannels, int numSamples) noexcept;

    engine::Engine& engine_;
    juce::AudioDeviceManager deviceManager_;
    dsp::SpectrumAnalyzer analyzer_;
    std::vector<float> monoScratch_;

    std::atomic<double> sampleRate_{0.0};
    std::atomic<int> blockSize_{0};
    std::atomic<int> outputLatency_{0};
    std::atomic<float> cpuLoad_{0.0f};
    std::atomic<uint64_t> callbacks_{0};
    std::atomic<bool> analyzerReady_{false};
    bool attached_ = false;
    bool deviceOpen_ = false;
};

} // namespace dawlab::app
