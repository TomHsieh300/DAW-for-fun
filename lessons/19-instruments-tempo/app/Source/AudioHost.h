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
//                                      spectrum analyzer (the engine measures its own CPU
//                                      load, chapter 15: Engine::getCpuMeter())
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
#include <dawlab/engine/Midi.h>

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <vector>

namespace dawlab::app {

class AudioHost final : private juce::AudioIODeviceCallback, private juce::MidiInputCallback {
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

    // ---- the input (chapter 17, Step 7) -------------------------------------------------------
    // The app starts without inputs: asking for one is what brings up the microphone permission on
    // macOS, and it can make Bluetooth headphones sound like a telephone. The first armed track asks
    // for them; the device restarts. Returns what went wrong, empty on success. A device whose input
    // and output cannot run at one sample rate is an error here.
    juce::String ensureInputsOpen();
    juce::StringArray getInputNames() const;
    // What the driver reports, and the trim. The mix's latency is the engine's to add.
    engine::Latencies getDeviceLatencies(int trimSamples) const noexcept;

    // ---- the keyboard (chapter 18, Step 8) -------------------------------------------------------
    // The driver's thread pushes every channel message twice, with its time on the shared clock (now()): into
    // the FIFO the audio thread plays from (scheduled: Step 5, a fixed delay after it arrived), and into the one
    // the UI thread records from (MidiRecording). Opens every MIDI input the system has. Returns their names;
    // none (offline, or no keyboard) is not an error.
    juce::StringArray openMidiInputs();
    juce::StringArray getMidiInputNames() const;
    // What the driver's callback does; also for tests, which play their keys this way. Any thread.
    void pushMidi(const engine::TimedMidi& message) noexcept;
    engine::MidiFifo& getRecordFifo() noexcept { return recordFifo_; }
    // Seconds on the clock the MIDI time stamps use (JUCE's Time::getMillisecondCounterHiRes()). Tests replace it.
    double now() const;
    void setClock(std::function<double()> clock) { clock_ = std::move(clock); }

    // What the last block was, for placing what the keyboard played (Step 6): when its first sample will be
    // heard, which song sample it rendered there, the mix's latency, and whether the transport played. A
    // seqlock: the audio thread writes it every block, the UI thread reads a consistent copy. False until a
    // block has been processed.
    struct Anchor {
        double blockTime = 0.0;
        int64_t songSample = 0;
        double sampleRate = 48000.0;
        int mixLatency = 0;
        bool playing = false;
        uint64_t block = 0;         // counts the blocks
        int64_t renderedBefore = 0; // samples rendered while playing, before this block: it only grows, through
                                    // loop wraps too, so it unwraps a loop's positions (MidiRecording)
        int numSamples = 0;
        // What renderedBefore will be for the next block.
        int64_t renderedAfter() const noexcept { return renderedBefore + (playing ? numSamples : 0); }
    };
    bool getAnchor(Anchor& out) const noexcept;
    long getLateMidi() const noexcept { return lateMidi_.load(std::memory_order_relaxed); }

    // ---- without a device -----------------------------------------------------------------
    // Prepares the engine and the analyzer as audioDeviceAboutToStart() would.
    void prepareOffline(double sampleRate, int blockSize);
    // Runs exactly what the device callback runs, on the calling thread.
    void processOffline(float* const* outputs, int numOutputChannels, int numSamples, engine::InputBlock input = {}) noexcept;

    // ---- status (any thread) --------------------------------------------------------------
    double getSampleRate() const noexcept { return sampleRate_.load(); }
    int getBlockSize() const noexcept { return blockSize_.load(); }
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
    void handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message) override;
    void publishAnchor(const Anchor& a) noexcept;
    void render(float* const* outputs, int numOutputChannels, int numSamples, engine::InputBlock input) noexcept;

    engine::Engine& engine_;
    juce::AudioDeviceManager deviceManager_;
    dsp::SpectrumAnalyzer analyzer_;
    std::vector<float> monoScratch_;

    std::atomic<double> sampleRate_{0.0};
    std::atomic<int> blockSize_{0};
    std::atomic<int> outputLatency_{0};
    std::atomic<int> inputLatency_{0};
    std::atomic<uint64_t> callbacks_{0};
    std::atomic<bool> analyzerReady_{false};
    bool attached_ = false;
    bool deviceOpen_ = false;

    // chapter 18
    engine::MidiFifo playFifo_, recordFifo_;
    std::vector<engine::BlockEvent> midiEvents_; // the audio thread's: one block's live MIDI (reserved)
    std::atomic<long> lateMidi_{0};
    std::function<double()> clock_;
    std::atomic<uint64_t> anchorVersion_{0}; // odd while the audio thread writes
    std::atomic<double> anchorTime_{0.0}, anchorRate_{48000.0};
    std::atomic<int64_t> anchorSample_{0};
    std::atomic<int> anchorMix_{0};
    std::atomic<bool> anchorPlaying_{false};
    std::atomic<uint64_t> anchorBlock_{0};
    std::atomic<int64_t> anchorRendered_{0};
    std::atomic<int> anchorNumSamples_{0};
    int64_t renderedWhilePlaying_ = 0; // audio thread only
};

} // namespace dawlab::app
