#include "DeviceTest.h"

#include "AudioHost.h"
#include "MainComponent.h"
#include "Session.h"
#include "TakeRecorder.h"

#include <dawlab/engine/All.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <vector>

namespace dawlab::app {

using namespace dawlab::engine;

namespace {

constexpr double kPi = 3.14159265358979323846;

void say(const char* format, const juce::String& text) {
    std::printf(format, text.toRawUTF8());
    std::fflush(stdout);
}

// A second callback on the device, next to the app's: the peak of every input channel.
class InputProbe final : public juce::AudioIODeviceCallback {
public:
    void audioDeviceIOCallbackWithContext(const float* const* inputs, int numInputs, float* const*, int, int numSamples,
                                          const juce::AudioIODeviceCallbackContext&) override {
        for (int c = 0; c < std::min(numInputs, kMax); ++c) {
            float peak = peaks_[c].load(std::memory_order_relaxed);
            if (inputs[c] != nullptr)
                for (int i = 0; i < numSamples; ++i)
                    peak = std::max(peak, std::abs(inputs[c][i]));
            peaks_[c].store(peak, std::memory_order_relaxed);
        }
        channels_.store(numInputs, std::memory_order_relaxed);
    }
    void audioDeviceAboutToStart(juce::AudioIODevice*) override {}
    void audioDeviceStopped() override {}
    void print() const {
        std::printf("  every input's peak:");
        for (int c = 0; c < std::min(channels_.load(), kMax); ++c)
            std::printf(" %d: %.1f dBFS", c + 1, peaks_[c].load() > 0.0f ? 20.0 * std::log10(peaks_[c].load()) : -999.0);
        std::printf("\n");
    }

private:
    static constexpr int kMax = 16;
    std::atomic<float> peaks_[kMax] = {};
    std::atomic<int> channels_{0};
};

// The main component without its window and without the user's takes folder, on the real device.
struct DeviceRig {
    std::unique_ptr<MainComponent> main = std::make_unique<MainComponent>(MainComponent::Mode::Offline);
    Session& session = main->getSession();
    Engine& engine = main->getEngine();
    AudioHost& host = main->getAudioHost();
    TakeRecorder& recorder = main->getTakeRecorder();
    juce::StringArray said;

    explicit DeviceRig(const juce::File& folder) {
        main->setSize(MainComponent::kDefaultWidth, MainComponent::kDefaultHeight);
        folder.createDirectory();
        recorder.setFolder(folder);
        recorder.onMessage = [this](const juce::String& message, bool alarm) {
            said.add(message);
            say(alarm ? "  recorder (alarm): %s\n" : "  recorder: %s\n", message);
        };
    }
    ~DeviceRig() {
        host.closeDevice();
        main.reset();
    }

    // Opens the default device with its inputs, arms `track` on input 1 with the monitor off.
    bool open(TrackId track, int bufferSize = 0) {
        if (const juce::String error = host.openDefaultDevice(); error.isNotEmpty()) {
            say("  cannot open the device: %s\n", error);
            return false;
        }
        if (!main->setArmed(track, true)) {
            say("  cannot arm: %s\n", main->getStatus());
            return false;
        }
        if (bufferSize > 0) {
            juce::AudioDeviceManager::AudioDeviceSetup setup = host.getDeviceManager().getAudioDeviceSetup();
            setup.bufferSize = bufferSize;
            if (const juce::String error = host.getDeviceManager().setAudioDeviceSetup(setup, true); error.isNotEmpty())
                say("  buffer size not changed: %s\n", error);
        }
        Track* t = engine.getTrack(track);
        t->setInputChannel(0);
        t->getMonitor().setMode(MonitorMode::Off); // a speaker and a microphone: never
        // The device applies the pending commands (the new project) with its first blocks.
        const std::uint64_t first = host.getNumCallbacks();
        for (int i = 0; i < 200 && host.getNumCallbacks() < first + 10; ++i)
            juce::Thread::sleep(5);
        if (host.getNumCallbacks() < first + 10) {
            std::printf("  the device does not call back\n");
            return false;
        }
        const Latencies latencies = host.getDeviceLatencies(0);
        say("  device: %s\n", host.getDeviceDescription());
        say("  inputs: %s\n", host.getInputNames().joinIntoString(", "));
        std::printf("  the driver says: input %d, output %d samples; the mix adds %d\n", latencies.input, latencies.output,
                    engine.getLatencySamples());
        return true;
    }

    // Records from the start of the song until the transport has passed `seconds`.
    bool record(double seconds) {
        engine.getTransport().setPosition(0);
        if (!main->toggleRecord()) {
            say("  recording did not start: %s\n", main->getStatus());
            return false;
        }
        const auto until = static_cast<std::int64_t>(seconds * engine.getSampleRate());
        const juce::uint32 deadline = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(seconds * 1000.0) + 10000;
        while (engine.getTransport().getPosition() < until && juce::Time::getMillisecondCounter() < deadline) {
            main->tick();
            juce::Thread::sleep(20);
        }
        main->toggleRecord();
        engine.getTransport().stop();
        for (int i = 0; i < 1500 && recorder.isBusy(); ++i) { // the disk thread reads the take back
            main->tick();
            juce::Thread::sleep(10);
        }
        main->tick();
        if (recorder.isBusy()) {
            std::printf("  the take did not come back\n");
            return false;
        }
        return true;
    }
};

struct Level {
    float peak = 0.0f;
    double rms = 0.0;
    std::int64_t zeros = 0;
};

Level levelOf(const Clip& clip) {
    Level level;
    double sum = 0.0;
    for (std::int64_t i = 0; i < clip.length; ++i) {
        const float s = clip.source->getSample(0, static_cast<int>(clip.sourceOffset + i));
        level.peak = std::max(level.peak, std::abs(s));
        sum += static_cast<double>(s) * s;
        level.zeros += s == 0.0f ? 1 : 0;
    }
    level.rms = clip.length > 0 ? std::sqrt(sum / static_cast<double>(clip.length)) : 0.0;
    return level;
}

double dB(double value) { return value > 0.0 ? 20.0 * std::log10(value) : -999.0; }

void printLevel(const Level& level, std::int64_t length) {
    std::printf("  level: peak %.1f dBFS, rms %.1f dBFS, %lld of %lld samples exactly 0\n", dB(level.peak), dB(level.rms),
                static_cast<long long>(level.zeros), static_cast<long long>(length));
}

int countFiles(const juce::File& folder, const juce::String& pattern) {
    return folder.getNumberOfChildFiles(juce::File::findFiles, pattern);
}

// ---- record -----------------------------------------------------------------------------------------

int testRecord(double seconds) {
    const juce::File folder = juce::File::getCurrentWorkingDirectory().getChildFile("dawlab_output").getChildFile("l17_device_test").getChildFile("record");
    folder.deleteRecursively();
    DeviceRig rig(folder);
    const TrackId track = rig.session.getTrackId(0);
    std::printf("Recording %.1f s from the default input\n", seconds);
    if (!rig.open(track))
        return 1;
    InputProbe probe;
    rig.host.getDeviceManager().addAudioCallback(&probe);
    const bool recorded = rig.record(seconds);
    rig.host.getDeviceManager().removeAudioCallback(&probe);
    probe.print();
    if (!recorded)
        return 1;

    int failed = 0;
    auto check = [&failed](bool ok, const char* what) {
        std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
        failed += ok ? 0 : 1;
    };
    const auto& clips = rig.session.getClips(track);
    check(clips.size() == 1, "one take on the armed track");
    if (clips.size() != 1)
        return 1;
    const Clip& take = clips[0];
    const double rate = rig.engine.getSampleRate();
    std::printf("  take: at %lld, %lld frames = %.3f s\n", static_cast<long long>(take.start), static_cast<long long>(take.length),
                static_cast<double>(take.length) / rate);
    const Level level = levelOf(take);
    printLevel(level, take.length);
    const Latencies latencies = rig.host.getDeviceLatencies(0);
    const std::int64_t late = latencies.input + latencies.output + rig.engine.getLatencySamples();
    std::printf("  source offset %lld, the latencies %lld\n", static_cast<long long>(take.sourceOffset), static_cast<long long>(late));
    check(take.start - take.sourceOffset == -late, "its first frame sits where it was played, the latencies earlier");
    check(std::abs(static_cast<double>(take.length) / rate - seconds) < 0.25, "about as long as the recording");
    check(level.peak > 0.0f, "not digital silence (a muted or blocked microphone gives exact zeros)");
    check(level.zeros < take.length / 2, "mostly not exact zeros");
    check(rig.said.joinIntoString(" ").contains("Recorded 1 take"), "the recorder said it recorded one take");
    check(!rig.said.joinIntoString(" ").containsIgnoreCase("dropped"), "nothing dropped");
    check(countFiles(folder, "*.wav") == 1 && countFiles(folder, "*.recording") == 0, "one sealed .wav in the folder, no .recording");
    for (const auto& f : folder.findChildFiles(juce::File::findFiles, false, "*.wav"))
        say("  file: %s\n", f.getFullPathName());
    return failed == 0 ? 0 : 1;
}

// ---- hold and recover -------------------------------------------------------------------------------

int testHold(const juce::File& folder) {
    DeviceRig rig(folder);
    const TrackId track = rig.session.getTrackId(0);
    if (!rig.open(track))
        return 1;
    rig.engine.getTransport().setPosition(0);
    if (!rig.main->toggleRecord())
        return 1;
    const auto three = static_cast<std::int64_t>(3.0 * rig.engine.getSampleRate());
    bool told = false;
    const juce::uint32 deadline = juce::Time::getMillisecondCounter() + 120000;
    while (juce::Time::getMillisecondCounter() < deadline) {
        rig.main->tick();
        const std::int64_t position = rig.engine.getTransport().getPosition();
        if (!told && position >= three) {
            std::printf("RECORDING %lld %.0f\n", static_cast<long long>(position), rig.engine.getSampleRate());
            std::fflush(stdout);
            told = true;
        }
        juce::Thread::sleep(100);
    }
    std::printf("nobody killed me\n");
    return 3;
}

int testRecover(const juce::File& folder) {
    const int unfinished = countFiles(folder, "*.recording");
    std::printf("Recovering: %d .recording file(s) in %s\n", unfinished, folder.getFullPathName().toRawUTF8());
    DeviceRig rig(folder);
    const int before = rig.session.getNumTracks();
    const int found = rig.recorder.recover();
    int failed = 0;
    auto check = [&failed](bool ok, const char* what) {
        std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
        failed += ok ? 0 : 1;
    };
    check(found == 1 && unfinished == 1, "one take found");
    check(rig.session.getNumTracks() == before + 1, "on a new track");
    if (rig.session.getNumTracks() > before) {
        const TrackId track = rig.session.getTrackId(rig.session.getNumTracks() - 1);
        const auto& clips = rig.session.getClips(track);
        check(clips.size() == 1, "one clip");
        if (clips.size() == 1) {
            const Clip& take = clips[0];
            std::printf("  take: at %lld, %lld frames = %.3f s (at the clips' %.0f Hz)\n", static_cast<long long>(take.start),
                        static_cast<long long>(take.length), static_cast<double>(take.length) / rig.engine.getSampleRate(),
                        rig.engine.getSampleRate());
            printLevel(levelOf(take), take.length);
            check(take.length >= static_cast<std::int64_t>(3.0 * rig.engine.getSampleRate()), "at least the 3 s that were in when it said RECORDING");
            check(levelOf(take).peak > 0.0f, "not digital silence");
        }
    }
    check(countFiles(folder, "*.recording") == 0 && countFiles(folder, "*.wav") == 1, "sealed: a .wav, no .recording left");
    check(rig.recorder.recover() == 0, "only once");
    return failed == 0 ? 0 : 1;
}

// ---- latency ----------------------------------------------------------------------------------------

std::vector<float> makeChirp(double rate) {
    // 10 ms from 1 to 6 kHz: a laptop's speaker plays it, and it correlates to one sharp peak.
    const int n = static_cast<int>(std::lround(0.010 * rate));
    std::vector<float> chirp(static_cast<size_t>(n));
    const double f0 = 1000.0, f1 = 6000.0, T = n / rate;
    for (int i = 0; i < n; ++i) {
        const double t = i / rate;
        const double window = 0.5 - 0.5 * std::cos(2.0 * kPi * i / (n - 1));
        chirp[static_cast<size_t>(i)] = static_cast<float>(0.8 * window * std::sin(2.0 * kPi * (f0 * t + (f1 - f0) * t * t / (2.0 * T))));
    }
    return chirp;
}

int testLatency(int bufferSize) {
    const juce::File folder = juce::File::getCurrentWorkingDirectory().getChildFile("dawlab_output").getChildFile("l17_device_test").getChildFile("latency");
    folder.deleteRecursively();
    DeviceRig rig(folder);
    const TrackId clickTrack = rig.session.getTrackId(0);
    const TrackId recTrack = rig.session.getTrackId(1);
    std::printf("Latency: clicks from the speaker into the microphone%s\n", bufferSize > 0 ? (" at a buffer of " + juce::String(bufferSize)).toRawUTF8() : "");
    if (!rig.open(recTrack, bufferSize))
        return 1;

    const double rate = rig.engine.getSampleRate();
    const std::vector<float> chirp = makeChirp(rate);
    const auto every = static_cast<std::int64_t>(1.0 * rate); // longer than any round trip
    constexpr int kClicks = 8;
    auto clicks = std::make_shared<AudioBuffer>(1, static_cast<int>(every * (kClicks + 1)));
    clicks->clear();
    for (int c = 1; c <= kClicks; ++c)
        for (size_t k = 0; k < chirp.size(); ++k)
            clicks->setSample(0, static_cast<int>(c * every + static_cast<std::int64_t>(k)), chirp[k]);
    rig.session.setClips(clickTrack, {Clip::fromBuffer(clicks, 0, "clicks")});
    juce::Thread::sleep(100); // the device applies the clip

    if (!rig.record(static_cast<double>(every * (kClicks + 1)) / rate + 0.5))
        return 1;
    const auto& clips = rig.session.getClips(recTrack);
    if (clips.size() != 1) {
        std::printf("  [FAIL] no take\n");
        return 1;
    }
    const Clip& take = clips[0];
    printLevel(levelOf(take), take.length);
    auto recordedAt = [&take](std::int64_t t) {
        const std::int64_t i = t - take.start;
        return i >= 0 && i < take.length ? take.source->getSample(0, static_cast<int>(take.sourceOffset + i)) : 0.0f;
    };

    // For each click, where it correlates best with what was recorded around it, on the timeline.
    const auto early = static_cast<int>(0.1 * rate), range = static_cast<int>(0.9 * rate);
    std::vector<double> sum(static_cast<size_t>(early + range + 1), 0.0);
    std::vector<int> lags;
    int clear = 0;
    for (int c = 1; c <= kClicks; ++c) {
        std::vector<double> r(sum.size());
        for (int d = -early; d <= range; ++d) {
            double acc = 0.0;
            for (size_t k = 0; k < chirp.size(); ++k)
                acc += chirp[k] * recordedAt(c * every + d + static_cast<std::int64_t>(k));
            r[static_cast<size_t>(d + early)] = std::abs(acc);
            sum[static_cast<size_t>(d + early)] += std::abs(acc);
        }
        const auto best = std::max_element(r.begin(), r.end());
        std::vector<double> sorted = r;
        std::nth_element(sorted.begin(), sorted.begin() + static_cast<long>(sorted.size() / 2), sorted.end());
        const double median = sorted[sorted.size() / 2];
        const double ratio = median > 0.0 ? *best / median : 0.0;
        const int lag = static_cast<int>(best - r.begin()) - early;
        std::printf("  click %2d: %+6d samples (%+.2f ms), peak %.1f x the median\n", c, lag, 1000.0 * lag / rate, ratio);
        if (ratio > 8.0) {
            ++clear;
            lags.push_back(lag);
        }
    }
    const int lag = static_cast<int>(std::max_element(sum.begin(), sum.end()) - sum.begin()) - early;
    const Latencies latencies = rig.host.getDeviceLatencies(0);
    std::printf("  all clicks together: recorded %+d samples (%+.2f ms) after where they were played\n", lag, 1000.0 * lag / rate);
    std::printf("  the round trip is %d samples (%.2f ms): the driver said %d + %d, the mix %d\n",
                lag + latencies.input + latencies.output + rig.engine.getLatencySamples(),
                1000.0 * (lag + latencies.input + latencies.output + rig.engine.getLatencySamples()) / rate, latencies.input,
                latencies.output, rig.engine.getLatencySamples());
    std::printf("  the trim to set: %d samples (the sound's way through the air included: ~3 samples per 2 cm)\n", lag);

    int failed = 0;
    auto check = [&failed](bool ok, const char* what) {
        std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
        failed += ok ? 0 : 1;
    };
    check(clear >= kClicks * 3 / 4, "most clicks were heard clearly");
    if (!lags.empty()) {
        const auto [lo, hi] = std::minmax_element(lags.begin(), lags.end());
        std::printf("  the clicks agree within %d samples\n", *hi - *lo);
        check(*hi - *lo <= static_cast<int>(0.001 * rate), "they agree within 1 ms (one latency, not a drifting one)");
    }
    return failed == 0 ? 0 : 1;
}

} // namespace

int runDeviceTest(const juce::StringArray& args) {
    const int at = args.indexOf("--device-test");
    const juce::String what = args[at + 1];
    const juce::String arg = args[at + 2].unquoted();
    if (what == "record")
        return testRecord(arg.isNotEmpty() ? arg.getDoubleValue() : 3.0);
    if (what == "hold" && arg.isNotEmpty())
        return testHold(juce::File::getCurrentWorkingDirectory().getChildFile(arg));
    if (what == "recover" && arg.isNotEmpty())
        return testRecover(juce::File::getCurrentWorkingDirectory().getChildFile(arg));
    if (what == "latency")
        return testLatency(arg.getIntValue());
    std::printf("usage: \"DawLab 17.exe\" --device-test record [seconds] | hold <folder> | recover <folder> | latency [buffer]\n");
    return 2;
}

} // namespace dawlab::app
