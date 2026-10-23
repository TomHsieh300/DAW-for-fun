// l14_ex9_solution -- Exercise 9: recording audio input into a track (solution).
//
// What it shows
//   - RecordingCallback: a juce::AudioIODeviceCallback that does what AudioHost does (engine ->
//     outputs, wrapped without copying) and additionally hands the device *inputs* to an
//     InputRecorder while recording.
//   - InputRecorder: the audio thread only copies the input into a pre-allocated lock-free FIFO
//     (juce::AbstractFifo over an AudioBuffer). A "disk" thread (juce::Thread) drains the FIFO
//     into the growing take - allocation happens there, never on the audio thread. When the FIFO
//     is full the audio thread drops the block and counts an overrun instead of waiting.
//   - Placement: the take becomes a clip at
//         start = P_record - (L_in + L_out + L_pdc)
//     (Exercise 6): P_record is the transport position when recording started, L_in / L_out the
//     device latencies (AudioIODevice::getInputLatencyInSamples / getOutputLatencyInSamples),
//     L_pdc the mix latency (Engine::getLatencySamples).
//
// Run
//   build/bin/Debug/l14_ex9_solution.exe
//   No audio device is needed: the program calls the device callback itself, from a second
//   thread, with a synthetic input (a 440 Hz tone with a click every 0.1 s), exactly as a device
//   would.
//
// Output
//   dawlab_output/l14_ex9_take.wav  the recorded take
//   Self-check: take == input sample for sample, no overruns, clip position, overrun counting.

#include <dawlab/Common.h>
#include <dawlab/engine/All.h>

#include "Session.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

using namespace dawlab;
using namespace dawlab::engine;
using dawlab::app::Session;

namespace ex9 {

class InputRecorder {
public:
    // Non-audio thread. The FIFO holds `fifoSeconds` of audio: the disk thread may stall that long.
    void prepare(double sampleRate, int numChannels, double fifoSeconds) {
        numChannels_ = std::max(1, numChannels);
        const int capacity = std::max(1024, static_cast<int>(fifoSeconds * sampleRate));
        fifoBuffer_.setSize(numChannels_, capacity);
        fifo_.setTotalSize(capacity);
        take_.assign(static_cast<size_t>(numChannels_), {});
    }

    // UI thread
    void start() {
        fifo_.reset();
        for (auto& ch : take_)
            ch.clear();
        startPosition_.store(-1);
        overruns_.store(0);
        recording_.store(true, std::memory_order_release);
    }
    void stop() { recording_.store(false, std::memory_order_release); }
    bool isRecording() const noexcept { return recording_.load(std::memory_order_acquire); }
    // Timeline position of the first recorded sample (set by the audio thread), -1 before.
    int64_t getStartPosition() const noexcept { return startPosition_.load(); }
    int getOverruns() const noexcept { return overruns_.load(); }

    // Audio thread: copy one device block into the FIFO. Never waits, never allocates.
    // `timelinePosition` = where the transport is at the first sample of this block.
    void pushInput(const float* const* input, int numInputChannels, int numSamples, int64_t timelinePosition) noexcept {
        if (!isRecording() || numInputChannels <= 0)
            return;
        if (startPosition_.load(std::memory_order_relaxed) < 0)
            startPosition_.store(timelinePosition, std::memory_order_relaxed);
        if (fifo_.getFreeSpace() < numSamples) {
            overruns_.fetch_add(1, std::memory_order_relaxed); // disk too slow: drop, count, carry on
            return;
        }
        int start1, size1, start2, size2;
        fifo_.prepareToWrite(numSamples, start1, size1, start2, size2);
        for (int ch = 0; ch < numChannels_; ++ch) {
            const float* src = input[std::min(ch, numInputChannels - 1)]; // mono input -> both channels
            if (src == nullptr)
                continue;
            if (size1 > 0)
                std::copy_n(src, size1, fifoBuffer_.getWritePointer(ch) + start1);
            if (size2 > 0)
                std::copy_n(src + size1, size2, fifoBuffer_.getWritePointer(ch) + start2);
        }
        fifo_.finishedWrite(size1 + size2);
    }

    // Disk thread: move everything that is in the FIFO into the take. Returns samples moved.
    int drain() {
        const int ready = fifo_.getNumReady();
        if (ready == 0)
            return 0;
        int start1, size1, start2, size2;
        fifo_.prepareToRead(ready, start1, size1, start2, size2);
        for (int ch = 0; ch < numChannels_; ++ch) {
            auto& dst = take_[static_cast<size_t>(ch)];
            const float* src = fifoBuffer_.getReadPointer(ch);
            dst.insert(dst.end(), src + start1, src + start1 + size1); // may allocate: fine here
            dst.insert(dst.end(), src + start2, src + start2 + size2);
        }
        fifo_.finishedRead(size1 + size2);
        return size1 + size2;
    }

    // UI thread, after stop() and a final drain(): the take as an immutable buffer.
    std::shared_ptr<const AudioBuffer> finishTake() const {
        const int n = take_.empty() ? 0 : static_cast<int>(take_[0].size());
        auto buffer = std::make_shared<AudioBuffer>(numChannels_, n);
        for (int ch = 0; ch < numChannels_; ++ch)
            std::copy_n(take_[static_cast<size_t>(ch)].data(), n, buffer->getWritePointer(ch));
        return buffer;
    }

    // Exercise 6's placement rule.
    static int64_t clipStart(int64_t recordStart, int inputLatency, int outputLatency, int pdcLatency) noexcept {
        return std::max<int64_t>(0, recordStart - (inputLatency + outputLatency + pdcLatency));
    }

private:
    int numChannels_ = 2;
    juce::AbstractFifo fifo_{1024};
    juce::AudioBuffer<float> fifoBuffer_;
    std::vector<std::vector<float>> take_;
    std::atomic<bool> recording_{false};
    std::atomic<int> overruns_{0};
    std::atomic<int64_t> startPosition_{-1};
};

// The device callback: engine output as in AudioHost, plus the recorder.
class RecordingCallback final : public juce::AudioIODeviceCallback {
public:
    RecordingCallback(Engine& engine, InputRecorder& recorder) : engine_(engine), recorder_(recorder) {}

    void audioDeviceIOCallbackWithContext(const float* const* inputs, int numInputs, float* const* outputs,
                                          int numOutputs, int numSamples,
                                          const juce::AudioIODeviceCallbackContext&) override {
        const juce::ScopedNoDenormals noDenormals;
        // The position this block will be rendered at (a pending seek is applied by this block).
        recorder_.pushInput(inputs, numInputs, numSamples, engine_.getTransport().getPosition());
        const int ch = std::min(numOutputs, kNumChannels);
        if (ch > 0)
            engine_.processBlock(AudioBlock(outputs, ch, numSamples));
        for (int c = ch; c < numOutputs; ++c)
            std::fill_n(outputs[c], numSamples, 0.0f);
    }
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override {
        engine_.prepare(device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
        recorder_.prepare(device->getCurrentSampleRate(), 2, 2.0);
    }
    void audioDeviceStopped() override {}

private:
    Engine& engine_;
    InputRecorder& recorder_;
};

class DiskThread final : public juce::Thread {
public:
    explicit DiskThread(InputRecorder& recorder) : juce::Thread("disk writer"), recorder_(recorder) {}
    void run() override {
        while (!threadShouldExit()) {
            recorder_.drain();
            wait(2); // a real disk writer would also write the WAV file here
        }
        recorder_.drain();
    }

private:
    InputRecorder& recorder_;
};

} // namespace ex9

int main() {
    const juce::ScopedJuceInitialiser_GUI juceInit;
    using namespace ex9;
    constexpr double sr = 48000.0;
    constexpr int block = 256;
    constexpr int inputLatency = 64, outputLatency = 96; // what a device would report

    Engine engine;
    engine.prepare(sr, 512);
    Session session(engine); // master: a limiter with 5 ms lookahead -> PDC 240 samples
    session.newProject();
    if (InsertEffect* lim = session.findInsert(ChannelRef::master(), "Limiter"))
        lim->setParameterByName("Lookahead", 5.0f);
    engine.applyPendingCommandsOffline();
    const int pdc = engine.getLatencySamples();

    InputRecorder recorder;
    recorder.prepare(sr, 2, 0.25); // a small FIFO, so the ring wraps around many times
    RecordingCallback callback(engine, recorder);

    // The input: 1.5 s of a 440 Hz tone with a click every 0.1 s, mono (one device input).
    const int total = static_cast<int>(1.5 * sr);
    std::vector<float> input(static_cast<size_t>(total + block)); // + one block of zeros (see below)
    for (int i = 0; i < total; ++i)
        input[static_cast<size_t>(i)] = 0.3f * static_cast<float>(std::sin(kTwoPi * 440.0 * i / sr)) + (i % 4800 == 0 ? 0.6f : 0.0f);

    // Start the transport at bar 2 (96000 samples at 120 BPM) and record while "playing".
    Transport& tr = engine.getTransport();
    tr.setPosition(96000);
    tr.play();
    juce::AudioBuffer<float> out(2, block);
    float* outs[2] = {out.getWritePointer(0), out.getWritePointer(1)};
    DiskThread disk(recorder);
    disk.startThread();
    recorder.start();

    // The "audio thread": a second std::thread calling the callback like a device, roughly paced.
    std::thread audio([&] {
        for (int done = 0; done < total; done += block) {
            const float* in[1] = {input.data() + done};
            callback.audioDeviceIOCallbackWithContext(in, 1, outs, 2, std::min(block, total - done), {});
            if ((done / block) % 8 == 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    audio.join();
    recorder.stop();
    disk.stopThread(2000); // final drain() inside run()
    const int64_t recordStart = recorder.getStartPosition();

    auto take = recorder.finishTake();
    float worst = 0.0f;
    for (int ch = 0; ch < take->getNumChannels(); ++ch)
        for (int i = 0; i < std::min(total, take->getNumSamples()); ++i)
            worst = std::max(worst, std::abs(take->getSample(ch, i) - input[static_cast<size_t>(i)]));
    std::printf("recorded %d of %d samples, %d overruns, max |take - input| = %g\n", take->getNumSamples(), total,
                recorder.getOverruns(), static_cast<double>(worst));
    DAWLAB_CHECK(take->getNumSamples() == total);
    DAWLAB_CHECK(recorder.getOverruns() == 0);
    DAWLAB_CHECK(worst == 0.0f);

    // Placement and the clip
    const int64_t start = InputRecorder::clipStart(recordStart, inputLatency, outputLatency, pdc);
    std::printf("recording started at %lld, latencies in %d + out %d + PDC %d -> clip at %lld\n",
                static_cast<long long>(recordStart), inputLatency, outputLatency, pdc, static_cast<long long>(start));
    DAWLAB_CHECK(recordStart == 96000);
    DAWLAB_CHECK(pdc == 240);
    DAWLAB_CHECK(start == 96000 - 64 - 96 - 240);
    session.setTrackAudio(0, take, "take 1", start);
    DAWLAB_CHECK(session.getClips(0).size() == 1);
    DAWLAB_CHECK(session.getClips(0)[0].start == start);
    writeWav(outputPath("l14_ex9_take.wav"), *take, sr);

    // Overruns: no disk thread draining -> the FIFO (0.25 s = 12000 samples) fills after 46 blocks.
    recorder.start();
    int pushed = 0;
    for (int done = 0; done < total; done += block, ++pushed) {
        const float* in[1] = {input.data() + done}; // the last block reads into the zero padding
        recorder.pushInput(in, 1, block, done);
    }
    recorder.stop();
    const int expectedOverruns = pushed - 12000 / block;
    std::printf("without a disk thread: %d blocks pushed, %d overruns (expected %d)\n", pushed,
                recorder.getOverruns(), expectedOverruns);
    DAWLAB_CHECK(recorder.getOverruns() == expectedOverruns);
    return test::summary("l14_ex9");
}
