#pragma once
// Exporter: "File > Export mixdown..." (Step 19).
//
// The bounce is the engine's own offline renderer (engine/Renderer.h, Step 9): the very
// processBlock() the audio device calls, driven as fast as the CPU allows, latency-compensated.
// Two rules from Step 9 decide how the app must call it:
//
//   1. Nobody else may call processBlock() during the render. The device keeps running, but
//      AudioHost::setEngineAttached(false) detaches the engine from it first.
//   2. The render thread becomes the engine's command consumer and garbage collector for the
//      duration. So the message thread must not edit the project or collect garbage meanwhile
//      (MainComponent shows a modal overlay and pauses its garbage collection).
//
// The render runs on its own thread (ExportJob), so the GUI keeps repainting; the WAV file is
// written with JUCE's WavAudioFormat (it handles Unicode paths on Windows).

#include <dawlab/AudioBuffer.h>
#include <dawlab/engine/Engine.h>

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <functional>
#include <memory>

namespace dawlab::app {

struct ExportOptions {
    juce::File file;
    int bitDepth = 24;          // 16, 24 or 32 (float)
    double tailSeconds = 3.0;   // reverb / delay tails after the last clip
    int64_t numSamples = 0;     // song part; 0 = up to the end of the last clip
    int blockSize = 512;
};

struct ExportResult {
    bool ok = false;
    juce::String error;
    juce::File file;
    double seconds = 0.0;         // length of the file
    double renderSeconds = 0.0;   // wall-clock time of the render
    float peakDb = -100.0f;       // sample peak of the file
    float integratedLufs = -100.0f; // the master strip's integrated loudness of this render
    int latencySamples = 0;       // removed from the start of the file
};

class Exporter {
public:
    // Renders and writes the file on the calling thread. Preconditions: see rule 1 and 2 above.
    static ExportResult renderToFile(engine::Engine& engine, const ExportOptions& options,
                                     AudioBuffer* renderedOut = nullptr);

    // Writes `buffer` as a WAV file (16 / 24-bit PCM or 32-bit float).
    static bool writeWavFile(const juce::File& file, const AudioBuffer& buffer, double sampleRate, int bitDepth,
                             juce::String& error);
};

// Runs Exporter::renderToFile() on a background thread and reports on the message thread.
class ExportJob final : private juce::Thread {
public:
    ExportJob(engine::Engine& engine, ExportOptions options, std::function<void(const ExportResult&)> onDone);
    ~ExportJob() override; // waits for the render to finish

    void start() { startThread(); }
    bool isRunning() const { return isThreadRunning(); }

private:
    void run() override;

    engine::Engine& engine_;
    ExportOptions options_;
    std::function<void(const ExportResult&)> onDone_;
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
};

} // namespace dawlab::app
