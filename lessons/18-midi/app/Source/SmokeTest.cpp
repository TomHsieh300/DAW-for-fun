#include "SmokeTest.h"

#include "AudioHost.h"
#include "EffectMenu.h"
#include "Exporter.h"
#include "MainComponent.h"
#include "MediaLoader.h"
#include "Meters.h"
#include "MixerView.h"
#include "ParameterPanel.h"
#include "ProjectIO.h"
#include "Session.h"
#include "TakeRecorder.h"
#include "VstInsert.h"

#include <dawlab/Analysis.h>
#include <dawlab/Check.h>
#include <dawlab/Signals.h>
#include <dawlab/WavFile.h>
#include <dawlab/engine/All.h>
#include <dawlab/engine/DemoSong.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <random>
#include <set>
#include <vector>

#if !defined(_WIN32)
#include <sys/resource.h> // setrlimit: a write that fails half-way
#endif

namespace dawlab::app {

using namespace dawlab::engine;

// ---- the test plugin -------------------------------------------------------------------------------

namespace {

std::atomic<int> gTestPluginInstances{0};
std::atomic<long long> gTestPluginProcessCalls{0};

class TestGainParameter final : public juce::HostedAudioProcessorParameter {
public:
    juce::String getParameterID() const override { return "gain"; }
    float getValue() const override { return value_.load(); }
    void setValue(float v) override { value_.store(std::clamp(v, 0.0f, 1.0f)); }
    float getDefaultValue() const override { return 0.5f; }
    juce::String getName(int maximumLength) const override { return juce::String("Gain").substring(0, maximumLength); }
    juce::String getLabel() const override { return {}; }
    juce::String getText(float v, int) const override { return juce::String(20.0 * std::log10(std::max(1e-5, 2.0 * v)), 1) + " dB"; }
    float getValueForText(const juce::String& text) const override { return text.getFloatValue(); }

private:
    std::atomic<float> value_{0.5f};
};

class TestDelayPlugin final : public juce::AudioPluginInstance {
public:
    explicit TestDelayPlugin(int latency)
        : juce::AudioPluginInstance(BusesProperties()
                                        .withInput("Input", juce::AudioChannelSet::stereo(), true)
                                        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
          latency_(latency) {
        auto p = std::make_unique<TestGainParameter>();
        gain_ = p.get();
        addHostedParameter(std::move(p));
        setLatencySamples(latency_);
        ++gTestPluginInstances;
    }
    ~TestDelayPlugin() override { --gTestPluginInstances; }

    void fillInPluginDescription(juce::PluginDescription& d) const override {
        d.name = getName();
        d.descriptiveName = "Delay line with a gain, for tests";
        d.pluginFormatName = "Internal";
        d.manufacturerName = "DawLab";
        d.version = "1.0";
        d.uniqueId = 0x4c313454; // "L14T"
        d.isInstrument = false;
        d.numInputChannels = 2;
        d.numOutputChannels = 2;
    }
    const juce::String getName() const override { return "DawLab Test Delay"; }
    void prepareToPlay(double, int) override {
        ring_.setSize(2, latency_ + 1);
        ring_.clear();
        pos_ = 0;
    }
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override {
        ++gTestPluginProcessCalls;
        const float g = 2.0f * gain_->getValue();
        const int len = ring_.getNumSamples();
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            for (int ch = 0; ch < std::min(2, buffer.getNumChannels()); ++ch) {
                // y[n] = g * x[n - latency] through a ring buffer of latency + 1 samples
                ring_.setSample(ch, pos_, buffer.getSample(ch, i));
                const int readPos = (pos_ + 1) % len;
                buffer.setSample(ch, i, g * ring_.getSample(ch, readPos));
            }
            pos_ = (pos_ + 1) % len;
        }
    }
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& dest) override {
        const float v = gain_->getValue();
        dest.replaceAll(&v, sizeof(v));
    }
    void setStateInformation(const void* data, int size) override {
        if (size == static_cast<int>(sizeof(float))) {
            float v = 0.0f;
            std::memcpy(&v, data, sizeof(v));
            gain_->setValue(v);
        }
    }

private:
    int latency_;
    TestGainParameter* gain_ = nullptr;
    juce::AudioBuffer<float> ring_;
    int pos_ = 0;
};

float maxAbsDiff(const AudioBuffer& a, int offsetA, const AudioBuffer& b, int offsetB, int length) {
    float worst = 0.0f;
    for (int ch = 0; ch < std::min(a.getNumChannels(), b.getNumChannels()); ++ch)
        for (int i = 0; i < length; ++i)
            worst = std::max(worst, std::abs(a.getSample(ch, offsetA + i) - b.getSample(ch, offsetB + i)));
    return worst;
}

AudioBuffer renderSeconds(Engine& engine, double seconds) {
    RenderOptions ro;
    ro.numSamples = static_cast<int64_t>(std::llround(seconds * engine.getSampleRate()));
    return renderOffline(engine, ro);
}

} // namespace

std::unique_ptr<juce::AudioPluginInstance> createTestDelayPlugin(int latencySamples) {
    return std::make_unique<TestDelayPlugin>(std::max(0, latencySamples));
}
int getTestPluginInstanceCount() noexcept { return gTestPluginInstances.load(); }
long long getTestPluginProcessCount() noexcept { return gTestPluginProcessCalls.load(); }

// ---- a slow insert (chapter 15): more work per block than the block lasts --------------------------

class BusyEffect final : public EffectBase {
public:
    enum Param { Gain, kCount };
    explicit BusyEffect(double secondsPerBlock) : EffectBase(kInfos, kCount), seconds_(secondsPerBlock) {}
    const char* getName() const noexcept override { return "Busy"; }
    void prepare(double sr, int, int) override { finishPrepare(sr); }
    void reset() noexcept override {}

protected:
    void applyParameter(int, float) noexcept override {}
    void processDsp(AudioBlock) noexcept override {
        const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds_);
        while (std::chrono::steady_clock::now() < end) {
        } // a plugin that is simply too slow for this machine
    }

private:
    static constexpr ParameterInfo kInfos[kCount] = {
        {"Gain", "x", 0.0f, 1.0f, 1.0f, ParameterKind::Float, false, nullptr, 0},
    };
    double seconds_;
};

// ---- automation (chapter 16, Step 6) ---------------------------------------------------------------

juce::MouseEvent mouseAt(ArrangementView& view, int x, int y, juce::ModifierKeys mods, juce::Point<float> down) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), {static_cast<float>(x), static_cast<float>(y)}, mods, 1.0f, 0.0f, 0.0f, 0.0f,
                            0.0f, &view, &view, juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), 1, false);
}

float heardGain(float normalized) {
    float l = 0.0f, r = 0.0f;
    ChannelStrip::computeStripGains(ChannelStrip::faderInfo().fromNormalized(normalized), 0.0f, dsp::PanLaw::ConstantPower, PanMode::PanLaw, l, r);
    return l;
}

AutomationCurve testRamp(float a, float b) {
    AutomationCurve c;
    c.add({0, a, Shape::Linear});
    c.add({48000, b, Shape::Linear});
    return c;
}

// ---- recording (chapter 17, Step 7) -----------------------------------------------------------------
// There is no audio device here: the input comes from these tests, through the same processBlock() a
// device calls, and the disk thread is real.

class TakesFolder {
public:
    TakesFolder() {
        folder_ = juce::File::getCurrentWorkingDirectory().getChildFile("dawlab_output").getChildFile("l18_takes_" + juce::String(juce::Time::getHighResolutionTicks()));
        folder_.createDirectory();
    }
    ~TakesFolder() { folder_.deleteRecursively(); }
    const juce::File& get() const noexcept { return folder_; }

private:
    juce::File folder_;
};

// Ticks until the recording is over and its clips are there.
bool waitForTakes(MainComponent& main) {
    for (int i = 0; i < 3000 && main.getTakeRecorder().isBusy(); ++i) {
        main.tick();
        juce::Thread::sleep(2);
    }
    main.tick();
    return !main.getTakeRecorder().isBusy();
}

// What frame f of the input is: exact in a float, different for each frame, never 0.
float inputAt(std::int64_t frame) { return static_cast<float>(frame % (1 << 20) + 1) / static_cast<float>(1 << 21); }

// Plays `frames` frames with that input, from frame `first` of it, and ticks now and then.
void playInput(MainComponent& main, std::int64_t frames, std::int64_t first = 0, int block = 512) {
    std::vector<float> in(static_cast<size_t>(block));
    const float* channels[] = {in.data()};
    for (std::int64_t done = 0; done < frames; done += block) {
        const int n = static_cast<int>(std::min<std::int64_t>(block, frames - done));
        for (int i = 0; i < n; ++i)
            in[static_cast<size_t>(i)] = inputAt(first + done + i);
        main.simulatePlayback(n, n, InputBlock{channels, 1});
        if ((done / block) % 16 == 0) {
            main.tick();
            juce::Thread::yield();
        }
    }
}

// One block more with the transport stopped: the audio thread's way of hearing about it.
void stopTransport(MainComponent& main) {
    main.getEngine().getTransport().stop();
    playInput(main, 512);
}

struct RecordingRig {
    TakesFolder folder;
    std::unique_ptr<MainComponent> main = std::make_unique<MainComponent>(MainComponent::Mode::Offline);
    Session& session = main->getSession();
    Engine& engine = main->getEngine();
    TakeRecorder& recorder = main->getTakeRecorder();
    TrackId first, second;
    juce::String said; // the last thing the recorder said: the status line may be showing the CPU

    RecordingRig() {
        main->setSize(MainComponent::kDefaultWidth, MainComponent::kDefaultHeight);
        session.newProject();
        engine.applyPendingCommandsOffline();
        first = session.getTrackId(0);
        second = session.getTrackId(1);
        recorder.setFolder(folder.get());
        recorder.onMessage = [this](const juce::String& message, bool) { said = message; };
    }
    ~RecordingRig() { main.reset(); }
};

void testRecordingBasics() {
    RecordingRig rig;
    MainComponent& main = *rig.main;
    Session& session = rig.session;
    Engine& engine = rig.engine;

    // Nothing is armed: nothing starts, and it says so.
    DAWLAB_CHECK(!main.toggleRecord());
    DAWLAB_CHECK(main.getStatus().contains("arm"));
    DAWLAB_CHECK(!engine.getTransport().isPlaying());
    DAWLAB_CHECK(rig.folder.get().getNumberOfChildFiles(juce::File::findFiles) == 0);

    // An old take under the new one, which stays before and after it.
    auto old = std::make_shared<AudioBuffer>(1, 240000);
    for (int i = 0; i < 240000; ++i)
        old->setSample(0, i, 0.25f);
    session.setClips(rig.first, {engine::Clip::fromBuffer(old, 0, "old")});

    DAWLAB_CHECK(main.setArmed(rig.first, true));
    rig.recorder.setTrimSamples(100);
    engine.getTransport().setPosition(96000);
    DAWLAB_CHECK(session.perform(edit::Rename{StripTarget::ofTrack(rig.second), "Renamed"}, "Rename"));
    DAWLAB_CHECK(main.toggleRecord());
    DAWLAB_CHECK(engine.getTransport().isPlaying());
    DAWLAB_CHECK(rig.recorder.getPhase() == TakeRecorder::Phase::Recording);

    // While it runs: the arrangement shows what has come in, and nothing that would pull the ground
    // from under it can start.
    playInput(main, 48000);
    LiveTake live;
    for (int i = 0; i < 2000 && !rig.recorder.getLive(rig.first, live); ++i)
        juce::Thread::sleep(1);
    DAWLAB_CHECK(rig.recorder.getLive(rig.first, live));
    DAWLAB_CHECK(!rig.recorder.getLive(rig.second, live));
    main.tick();
    const juce::Image shown = main.createComponentSnapshot(main.getLocalBounds(), true, 1.0f);
    DAWLAB_CHECK(shown.isValid());
    main.newProject();
    DAWLAB_CHECK(main.getStatus().contains("recording"));
    DAWLAB_CHECK(session.getNumTracks() > 0 && session.hasTrack(rig.first));
    DAWLAB_CHECK(!main.deleteSelectedTrack());
    DAWLAB_CHECK(session.hasTrack(rig.first));
    DAWLAB_CHECK(!main.openProject(rig.folder.get().getChildFile("none.dawlab")).ok);
    // Undo and redo wait too - the undo of "Add track" would take away the track that records - and so does the
    // demo song, which replaces the whole project.
    DAWLAB_CHECK(!main.undo() && main.getStatus().contains("recording"));
    DAWLAB_CHECK(session.getStripName(session.trackRef(rig.second)) == "Renamed" && session.getHistory().canUndo());
    DAWLAB_CHECK(main.loadDemoSongNow() < 0 && session.hasTrack(rig.first) && main.getStatus().contains("recording"));

    // A new tempo waits too: it would move the playhead in samples, and a jump of the playhead ends a take. The
    // tempo slider goes back to the transport's tempo.
    const double bpmBefore = engine.getTransport().getTempo();
    juce::Slider& tempoSlider = main.getTransportBar().getTempoSlider();
    tempoSlider.setValue(bpmBefore + 20.0, juce::sendNotificationSync);
    std::printf("   a new tempo while recording: %.1f -> %.1f BPM, the slider shows %.1f; \"%s\"\n", bpmBefore,
                engine.getTransport().getTempo(), tempoSlider.getValue(), main.getStatus().toRawUTF8());
    DAWLAB_CHECK(engine.getTransport().getTempo() == bpmBefore && main.getStatus().contains("recording"));
    DAWLAB_CHECK(tempoSlider.getValue() == bpmBefore);
    DAWLAB_CHECK(rig.recorder.getPhase() == TakeRecorder::Phase::Recording);

    stopTransport(main);
    DAWLAB_CHECK(waitForTakes(main));
    DAWLAB_CHECK(rig.said.contains("Recorded 1 take"));

    const auto& clips = session.getClips(rig.first);
    DAWLAB_CHECK(clips.size() == 3);
    const std::int64_t total = engine.getLatencySamples() + 100;
    if (clips.size() == 3) {
        const engine::Clip& before = clips[0];
        const engine::Clip& take = clips[1];
        const engine::Clip& after = clips[2];
        // Where it was played, and everything that came in.
        DAWLAB_CHECK(take.start == 96000 - total);
        DAWLAB_CHECK(take.sourceOffset == 0);
        DAWLAB_CHECK(take.length == 48000);
        // The old take makes room: a piece before, and the rest of it after, at the right source.
        DAWLAB_CHECK(before.source == old && before.start == 0 && before.length == take.start);
        DAWLAB_CHECK(after.source == old && after.start == take.end());
        DAWLAB_CHECK(after.sourceOffset == take.end());
        DAWLAB_CHECK(after.end() == 240000);
        DAWLAB_CHECK(after.fadeInSamples > 0 && before.fadeOutSamples > 0);
        DAWLAB_CHECK(take.fadeInSamples > 0 && take.fadeOutSamples > 0);

        // Through JUCE's reader, which knows nothing about our chunk: bit for bit what came in.
        DAWLAB_CHECK(take.source != nullptr);
        int wrong = 0;
        if (take.source != nullptr)
            for (int i = 0; i < 48000; ++i)
                wrong += take.source->getSample(0, i) == inputAt(i) ? 0 : 1;
        std::printf("  a take through the real paths: at %lld, %lld frames, %d wrong (expected 0)\n", static_cast<long long>(take.start),
                    static_cast<long long>(take.length), wrong);
        DAWLAB_CHECK(wrong == 0);
    }
    // The file stays, with its final name.
    DAWLAB_CHECK(rig.folder.get().getNumberOfChildFiles(juce::File::findFiles, "*.wav") == 1);
    DAWLAB_CHECK(rig.folder.get().getNumberOfChildFiles(juce::File::findFiles, "*.recording") == 0);

    // One undo step, and back.
    DAWLAB_CHECK(main.undo());
    DAWLAB_CHECK(session.getClips(rig.first).size() == 1);
    DAWLAB_CHECK(main.redo());
    DAWLAB_CHECK(session.getClips(rig.first).size() == 3);

    // Stopped by hand, with the transport going on.
    engine.getTransport().setPosition(0);
    DAWLAB_CHECK(main.toggleRecord());
    playInput(main, 24000);
    DAWLAB_CHECK(main.toggleRecord());
    playInput(main, 512);
    DAWLAB_CHECK(engine.getTransport().isPlaying());
    DAWLAB_CHECK(waitForTakes(main));
    stopTransport(main);

    // Two tracks at once: one undo step, and the same place.
    session.newProject();
    engine.applyPendingCommandsOffline();
    const TrackId a = session.getTrackId(0), b = session.getTrackId(1);
    DAWLAB_CHECK(main.setArmed(a, true));
    DAWLAB_CHECK(main.setArmed(b, true));
    engine.getTrack(b)->setInputChannel(1);
    engine.getTransport().setPosition(48000);
    DAWLAB_CHECK(main.toggleRecord());
    // Longer than a device block: simulatePlayback() has to hand each block its own part of the input.
    std::vector<float> left(4096), right(4096);
    for (int i = 0; i < 4096; ++i) {
        left[static_cast<size_t>(i)] = inputAt(i);
        right[static_cast<size_t>(i)] = -inputAt(i);
    }
    const float* twoChannels[] = {left.data(), right.data()};
    for (int i = 0; i < 12; ++i)
        main.simulatePlayback(4096, 512, InputBlock{twoChannels, 2});
    stopTransport(main);
    DAWLAB_CHECK(waitForTakes(main));
    DAWLAB_CHECK(session.getClips(a).size() == 1 && session.getClips(b).size() == 1);
    DAWLAB_CHECK(main.undo()); // one undo step takes back both
    DAWLAB_CHECK(session.getClips(a).empty() && session.getClips(b).empty());
    DAWLAB_CHECK(main.redo());
    if (session.getClips(a).size() == 1 && session.getClips(b).size() == 1) {
        DAWLAB_CHECK(session.getClips(a)[0].start == session.getClips(b)[0].start);
        const engine::Clip& ca = session.getClips(a)[0];
        const engine::Clip& cb = session.getClips(b)[0];
        int wrongFrames = 0;
        for (int i = 0; i < 4096 * 12; ++i) {
            wrongFrames += ca.source->getSample(0, i) == inputAt(i % 4096) ? 0 : 1;
            wrongFrames += cb.source->getSample(0, i) == -inputAt(i % 4096) ? 0 : 1; // its own input
        }
        DAWLAB_CHECK(wrongFrames == 0);
    }

    // Disarming also turns the monitor off.
    main.cycleMonitor(a);
    DAWLAB_CHECK(engine.getTrack(a)->getMonitor().mode() == MonitorMode::Auto);
    DAWLAB_CHECK(main.getStatus().contains("late"));
    main.cycleMonitor(a);
    DAWLAB_CHECK(engine.getTrack(a)->getMonitor().mode() == MonitorMode::Always);
    DAWLAB_CHECK(main.setArmed(a, false));
    DAWLAB_CHECK(engine.getTrack(a)->getMonitor().mode() == MonitorMode::Off);
    // A monitor needs an armed track, and arms it.
    const TrackId c = session.getTrackId(2);
    main.cycleMonitor(c);
    DAWLAB_CHECK(engine.getTrack(c)->isArmed());
}

void testRecordingLoop() {
    RecordingRig rig;
    MainComponent& main = *rig.main;
    Session& session = rig.session;
    Engine& engine = rig.engine;
    Transport& tr = engine.getTransport();

    DAWLAB_CHECK(main.setArmed(rig.first, true));
    tr.setLoopRange(96000, 144000);
    tr.setLoopEnabled(true);
    tr.setPosition(96000);
    DAWLAB_CHECK(main.toggleRecord());
    const std::int64_t mix = engine.getLatencySamples();
    playInput(main, mix + 48000 * 3 + 24000);
    stopTransport(main);
    DAWLAB_CHECK(waitForTakes(main));

    const auto& clips = session.getClips(rig.first);
    DAWLAB_CHECK(clips.size() == 1);
    if (clips.size() != 1)
        return;
    const std::vector<Placement>* takes = session.getTakesOf(clips[0]);
    DAWLAB_CHECK(takes != nullptr);
    if (takes == nullptr)
        return;
    std::printf("  a loop of 3.5 passes: %d takes, one clip plays take %d\n", static_cast<int>(takes->size()), session.getCurrentTake(clips[0]) + 1);
    DAWLAB_CHECK(takes->size() == 4);
    DAWLAB_CHECK(session.getCurrentTake(clips[0]) == 2); // the last whole pass, not the piece after it
    DAWLAB_CHECK(clips[0].start == 96000 && clips[0].length == 48000);

    // Another pass of the same place, as one undo step.
    DAWLAB_CHECK(session.chooseTake(rig.first, 0, 0));
    DAWLAB_CHECK(session.getCurrentTake(session.getClips(rig.first)[0]) == 0);
    DAWLAB_CHECK(session.getClips(rig.first)[0].start == 96000);
    DAWLAB_CHECK(main.undo());
    DAWLAB_CHECK(session.getCurrentTake(session.getClips(rig.first)[0]) == 2);
    DAWLAB_CHECK(!session.chooseTake(rig.first, 0, 9));
    DAWLAB_CHECK(session.chooseTake(rig.first, 0, 1));

    // Each take is what was played in that pass.
    const engine::Clip chosen = session.getClips(rig.first)[0];
    DAWLAB_CHECK(chosen.source->getSample(0, static_cast<int>(chosen.sourceOffset)) == inputAt(chosen.sourceOffset));

    // Saved and opened: the same takes, on the same audio file, and the choice.
    const juce::File project = rig.folder.get().getChildFile("Song.dawlab");
    DAWLAB_CHECK(main.saveProject(project));
    const ProjectIO::OpenResult opened = main.openProject(project);
    DAWLAB_CHECK(opened.ok);
    DAWLAB_CHECK(opened.warnings.isEmpty());
    const TrackId reopened = session.getTrackId(0);
    DAWLAB_CHECK(session.getClips(reopened).size() == 1);
    if (session.getClips(reopened).size() == 1) {
        const engine::Clip& clip = session.getClips(reopened)[0];
        DAWLAB_CHECK(session.getCurrentTake(clip) == 1);
        DAWLAB_CHECK(session.getTakesOf(clip) != nullptr && session.getTakesOf(clip)->size() == 4);
        DAWLAB_CHECK(session.chooseTake(reopened, 0, 2));
        // An armed track is about now, not about the song.
        DAWLAB_CHECK(!engine.getTrack(reopened)->isArmed());

        // A device at another rate: the takes follow.
        session.handleSampleRateChange(48000.0, 24000.0);
        const engine::Clip& half = session.getClips(reopened)[0];
        DAWLAB_CHECK(half.start == 48000);
        DAWLAB_CHECK(session.getCurrentTake(half) == 2);
        DAWLAB_CHECK(session.chooseTake(reopened, 0, 0));
    }
}

void testRecordingPunch() {
    RecordingRig rig;
    MainComponent& main = *rig.main;
    Session& session = rig.session;
    Engine& engine = rig.engine;
    Transport& tr = engine.getTransport();

    // The locators are the punch points, without a loop running.
    auto old = std::make_shared<AudioBuffer>(1, 96000);
    session.setClips(rig.first, {engine::Clip::fromBuffer(old, 0, "old")});
    DAWLAB_CHECK(main.setArmed(rig.first, true));
    tr.setLoopRange(48000, 72000);
    tr.setLoopEnabled(false);
    rig.recorder.setPunchEnabled(true);
    tr.setPosition(24000);
    DAWLAB_CHECK(main.toggleRecord());
    playInput(main, 72000 + engine.getLatencySamples());
    stopTransport(main);
    DAWLAB_CHECK(waitForTakes(main));
    const auto& clips = session.getClips(rig.first);
    DAWLAB_CHECK(clips.size() == 3);
    if (clips.size() == 3) {
        DAWLAB_CHECK(clips[1].start == 48000); // exact, wherever the blocks fell
        DAWLAB_CHECK(clips[1].length == 24000);
        DAWLAB_CHECK(clips[0].end() == 48000);
        DAWLAB_CHECK(clips[2].start == 72000);
        // The frames before the punch are in the file too.
        DAWLAB_CHECK(clips[1].sourceOffset > 0);
        // The frame played at the punch-in.
        const std::int64_t offset = clips[1].sourceOffset;
        DAWLAB_CHECK(clips[1].source->getSample(0, static_cast<int>(offset)) == inputAt(offset));
    }

    // Never reached: no clip at all, and nothing goes wrong.
    session.setClips(rig.first, {});
    tr.setPosition(0);
    DAWLAB_CHECK(main.toggleRecord());
    playInput(main, 4800);
    stopTransport(main);
    DAWLAB_CHECK(waitForTakes(main));
    DAWLAB_CHECK(session.getClips(rig.first).empty());
    DAWLAB_CHECK(rig.said.contains("Nothing recorded"));
}

void testRecordingWhenThingsGoWrong() {
    // The disk fills up: the recording stops, the song plays on, and what fitted is a take.
    {
        RecordingRig rig;
        MainComponent& main = *rig.main;
        DAWLAB_CHECK(main.setArmed(rig.first, true));
        DiskOps full;
        auto left = std::make_shared<std::atomic<std::uint64_t>>(108 + 4 * 24000 + 2);
        full.write = [left](const void* bytes, std::size_t numBytes, std::FILE* file) {
            const std::uint64_t space = left->load();
            const std::size_t written = std::fwrite(bytes, 1, static_cast<std::size_t>(std::min<std::uint64_t>(space, numBytes)), file);
            left->store(space - written);
            return written;
        };
        rig.recorder.setDiskOps(full);
        DAWLAB_CHECK(main.toggleRecord());
        for (int i = 0; i < 100 && rig.recorder.getPhase() == TakeRecorder::Phase::Recording; ++i) {
            playInput(main, 4800);
            juce::Thread::sleep(5);
        }
        DAWLAB_CHECK(rig.engine.getTransport().isPlaying());
        DAWLAB_CHECK(waitForTakes(main));
        const auto& clips = rig.session.getClips(rig.first);
        DAWLAB_CHECK(clips.size() == 1);
        if (clips.size() == 1) {
            const std::int64_t frames = clips[0].sourceOffset + clips[0].length;
            std::printf("  the disk is full after 24000 frames: a take of %lld frames, recording stopped\n", static_cast<long long>(frames));
            DAWLAB_CHECK(frames == 24000);
        }
        rig.engine.getTransport().stop();
    }
    // There is no way to create a file: it says so.
    {
        RecordingRig rig;
        MainComponent& main = *rig.main;
        DAWLAB_CHECK(main.setArmed(rig.first, true));
        DiskOps none;
        none.write = [](const void*, std::size_t, std::FILE*) { return std::size_t{0}; };
        rig.recorder.setDiskOps(none);
        juce::String said;
        rig.recorder.onMessage = [&said](const juce::String& message, bool) { said << message << "\n"; };
        DAWLAB_CHECK(main.toggleRecord());
        for (int i = 0; i < 100 && rig.recorder.getPhase() == TakeRecorder::Phase::Recording; ++i) {
            playInput(main, 4800);
            juce::Thread::sleep(5);
        }
        DAWLAB_CHECK(waitForTakes(main));
        DAWLAB_CHECK(said.contains("no take file"));
        DAWLAB_CHECK(rig.session.getClips(rig.first).empty());
        rig.recorder.onMessage = nullptr;
        rig.engine.getTransport().stop();
    }
    // The disk stalls: silence in the take, its timing right, and it says so.
    {
        RecordingRig rig;
        MainComponent& main = *rig.main;
        DAWLAB_CHECK(main.setArmed(rig.first, true));
        DiskOps slow;
        auto hold = std::make_shared<std::atomic<bool>>(true);
        slow.write = [hold](const void* bytes, std::size_t numBytes, std::FILE* file) {
            while (hold->load())
                juce::Thread::sleep(1);
            return std::fwrite(bytes, 1, numBytes, file);
        };
        rig.recorder.setDiskOps(slow);
        rig.recorder.setRingSeconds(0.1);
        juce::String said;
        rig.recorder.onMessage = [&said](const juce::String& message, bool) { said << message << "\n"; };
        DAWLAB_CHECK(main.toggleRecord());
        playInput(main, 48000);
        main.tick();
        DAWLAB_CHECK(said.contains("cannot keep up"));
        hold->store(false);
        juce::Thread::sleep(300); // it catches up
        playInput(main, 4800, 48000);
        juce::Thread::sleep(300);
        stopTransport(main);
        DAWLAB_CHECK(waitForTakes(main));
        const auto& clips = rig.session.getClips(rig.first);
        DAWLAB_CHECK(clips.size() == 1);
        if (clips.size() == 1) {
            const engine::Clip& clip = clips[0];
            // What came in after the gap is where it belongs: the gap kept the time.
            DAWLAB_CHECK(clip.sourceOffset + clip.length == 52800);
            int atPlace = 0, silent = 0, wrong = 0;
            for (int i = 0; i < 52800; ++i) {
                const float got = clip.source->getSample(0, i);
                atPlace += got == inputAt(i) ? 1 : 0;
                silent += got == 0.0f ? 1 : 0;
                wrong += got == inputAt(i) || got == 0.0f ? 0 : 1;
            }
            std::printf("  the disk stalls: %d frames in place, %d silent, %d wrong (expected 0)\n", atPlace, silent, wrong);
            DAWLAB_CHECK(wrong == 0);
            DAWLAB_CHECK(silent > 0);
            DAWLAB_CHECK(clip.source->getSample(0, 52799) == inputAt(52799));
        }
        rig.recorder.onMessage = nullptr;
    }
    // The device goes away during a take: nobody confirms, and it ends anyway.
    {
        RecordingRig rig;
        MainComponent& main = *rig.main;
        DAWLAB_CHECK(main.setArmed(rig.first, true));
        DAWLAB_CHECK(main.toggleRecord());
        playInput(main, 24000);
        rig.engine.finishRecordingsWithoutAudio(); // what AudioHost::audioDeviceStopped() does
        DAWLAB_CHECK(waitForTakes(main));
        DAWLAB_CHECK(rig.session.getClips(rig.first).size() == 1);
    }
    // The clips went to another rate while the take was on its way: the take follows them.
    {
        RecordingRig rig;
        MainComponent& main = *rig.main;
        DAWLAB_CHECK(main.setArmed(rig.first, true));
        rig.engine.getTransport().setPosition(48000);
        DAWLAB_CHECK(main.toggleRecord());
        playInput(main, 48000);
        const std::int64_t mix = rig.engine.getLatencySamples();
        stopTransport(main);
        rig.session.handleSampleRateChange(48000.0, 24000.0);
        DAWLAB_CHECK(waitForTakes(main));
        const auto& clips = rig.session.getClips(rig.first);
        DAWLAB_CHECK(clips.size() == 1);
        if (clips.size() == 1) {
            DAWLAB_CHECK(clips[0].start == (48000 - mix) / 2);
            DAWLAB_CHECK(std::abs(clips[0].length - 24000) < 256);
        }
    }
    // The track is deleted (by a way that does not ask): the take stays on disk, and it says where.
    {
        RecordingRig rig;
        MainComponent& main = *rig.main;
        DAWLAB_CHECK(main.setArmed(rig.first, true));
        DAWLAB_CHECK(main.toggleRecord());
        playInput(main, 24000);
        DAWLAB_CHECK(rig.session.removeTrack(rig.first));
        playInput(main, 512);
        rig.engine.stopRecording();
        playInput(main, 512);
        DAWLAB_CHECK(waitForTakes(main));
        DAWLAB_CHECK(rig.said.contains("kept in"));
        DAWLAB_CHECK(rig.folder.get().getNumberOfChildFiles(juce::File::findFiles, "*.wav") == 1);
    }
    // The program dies. The next start finds the take, puts it where it was played, on a new track.
    {
        RecordingRig rig;
        MainComponent& main = *rig.main;
        const Latencies device{240, 480, 0, 0};
        {
            Engine other;
            const TrackId id{1};
            other.addTrack(id);
            other.prepare(48000.0, 512);
            other.applyPendingCommandsOffline();
            other.getTrack(id)->setArmed(true);
            other.getTransport().setPosition(10000);
            RecordingSession dying;
            DAWLAB_CHECK(dying.begin(other, std::filesystem::u8path(rig.folder.get().getFullPathName().toStdString()), "crashed", device) == 1);
            other.getTransport().play();
            AudioBuffer out(2, 512);
            std::vector<float> in(512);
            const float* channels[] = {in.data()};
            for (int block = 0; block < 100; ++block) {
                for (int i = 0; i < 512; ++i)
                    in[static_cast<size_t>(i)] = inputAt(block * 512 + i);
                other.processBlock(out.toBlock(), InputBlock{channels, 1});
                dying.pump();
            }
        } // never finished
        DAWLAB_CHECK(rig.folder.get().getNumberOfChildFiles(juce::File::findFiles, "*.recording") == 1);
        const int tracksBefore = rig.session.getNumTracks();
        rig.session.getHistory().markSaved();
        DAWLAB_CHECK(main.getTakeRecorder().recover() == 1);
        DAWLAB_CHECK(rig.session.getNumTracks() == tracksBefore + 1);
        DAWLAB_CHECK(rig.session.getHistory().isModified()); // the recovered take is in no project file yet
        DAWLAB_CHECK(rig.folder.get().getNumberOfChildFiles(juce::File::findFiles, "*.recording") == 0);
        const TrackId recovered = rig.session.getTrackId(tracksBefore);
        const auto& clips = rig.session.getClips(recovered);
        DAWLAB_CHECK(clips.size() == 1);
        if (clips.size() == 1) {
            std::printf("  a crash: the take comes back at %lld, %lld frames\n", static_cast<long long>(clips[0].start), static_cast<long long>(clips[0].length));
            DAWLAB_CHECK(clips[0].start == 10000 - 720);
            DAWLAB_CHECK(clips[0].length == 51200);
            DAWLAB_CHECK(clips[0].source->getSample(0, 51199) == inputAt(51199));
        }
        DAWLAB_CHECK(main.getTakeRecorder().recover() == 0); // only once
        // A file that is not a take stays what it is.
        const juce::File junk = rig.folder.get().getChildFile("junk.wav.recording");
        DAWLAB_CHECK(junk.replaceWithText("not a take"));
        DAWLAB_CHECK(main.getTakeRecorder().recover() == 1);
        DAWLAB_CHECK(junk.existsAsFile());
        DAWLAB_CHECK(rig.said.contains("Could not recover"));
    }
}

// The takes are heard back where they were played: the whole way around, through the project's own
// playback of the recorded clip.
void testRecordingPlaysBack() {
    RecordingRig rig;
    MainComponent& main = *rig.main;
    Engine& engine = rig.engine;
    Session& session = rig.session;

    // A click track on one track, and through a cable into another one.
    auto clicks = std::make_shared<AudioBuffer>(1, 96000);
    for (int i = 4800; i < 96000; i += 4800)
        clicks->setSample(0, i, 1.0f);
    session.setClips(rig.first, {engine::Clip::fromBuffer(clicks, 0, "clicks")});
    DAWLAB_CHECK(main.setArmed(rig.second, true));
    constexpr int kCable = 720;
    rig.recorder.setTrimSamples(kCable); // the "driver" reports nothing here; the trim knows the rest
    DAWLAB_CHECK(main.toggleRecord());

    constexpr int kBlock = 480;
    AudioBuffer history(2, 96000 + kBlock);
    std::vector<float> in(kBlock);
    const float* channels[] = {in.data()};
    AudioHost& host = main.getAudioHost();
    for (int done = 0; done + kBlock <= 96000; done += kBlock) {
        for (int i = 0; i < kBlock; ++i)
            in[static_cast<size_t>(i)] = done + i >= kCable ? history.getSample(0, done + i - kCable) : 0.0f;
        float* outs[] = {history.getWritePointer(0) + done, history.getWritePointer(1) + done};
        host.processOffline(outs, 2, kBlock, InputBlock{channels, 1});
        if (done % (kBlock * 16) == 0)
            main.tick();
    }
    stopTransport(main);
    DAWLAB_CHECK(waitForTakes(main));
    const auto& clips = session.getClips(rig.second);
    DAWLAB_CHECK(clips.size() == 1);
    if (clips.size() != 1)
        return;
    const engine::Clip& take = clips[0];
    int heard = 0, misplaced = 0;
    for (std::int64_t i = 0; i < take.length; ++i) {
        if (std::abs(take.source->getSample(0, static_cast<int>(take.sourceOffset + i))) > 0.1f) {
            ++heard;
            misplaced += (take.start + i) % 4800 == 0 ? 0 : 1;
        }
    }
    std::printf("  the click track through a cable, with the mix's latency of %d: %d clicks, %d in the wrong place (expected 0)\n",
                engine.getLatencySamples(), heard, misplaced);
    DAWLAB_CHECK(heard >= 18);
    DAWLAB_CHECK(misplaced == 0);
}

void testRecording() {
    std::printf("Recording\n");
    testRecordingBasics();
    testRecordingLoop();
    testRecordingPunch();
    testRecordingWhenThingsGoWrong();
    testRecordingPlaysBack();
}

void testAutomation() {
    std::printf("Automation\n");
    auto main = std::make_unique<MainComponent>(MainComponent::Mode::Offline);
    main->setSize(MainComponent::kDefaultWidth, MainComponent::kDefaultHeight);
    Session& session = main->getSession();
    Engine& engine = main->getEngine();
    session.newProject();
    engine.applyPendingCommandsOffline();
    const TrackId first = session.getTrackId(0), second = session.getTrackId(1);
    auto dc = std::make_shared<AudioBuffer>(1, 96000);
    for (int i = 0; i < 96000; ++i)
        dc->setSample(0, i, 1.0f);
    engine::Clip clip = engine::Clip::fromBuffer(dc, 0, "dc");
    session.setClips(first, {clip});
    const LaneAddress gain{StripTarget::ofTrack(first), EffectId{}, "Gain"};
    const LaneAddress pan{StripTarget::ofTrack(first), EffectId{}, "Pan"};

    // a. A lane in the model plays, through a bounce (the master's limiter stays out of the way: gain < 1).
    RenderOptions bounce;
    bounce.numSamples = 24000;
    LaneContent lane;
    lane.curve = testRamp(0.5f, 0.75f);
    DAWLAB_CHECK(session.perform(edit::SetLane{gain, lane}, "Test"));
    engine.applyPendingCommandsOffline();
    const AudioBuffer played = renderOffline(engine, bounce);
    std::printf("  a lane plays: sample 12000 %.5f (expected %.5f)\n", static_cast<double>(played.getSample(0, 12000)), static_cast<double>(heardGain(lane.curve.valueAt(12000))));
    DAWLAB_CHECK_NEAR(played.getSample(0, 12000), heardGain(lane.curve.valueAt(12000)), 1e-3);
    DAWLAB_CHECK(session.undo() && session.getLane(gain) == nullptr);
    engine.applyPendingCommandsOffline();
    const AudioBuffer flat = renderOffline(engine, bounce);
    DAWLAB_CHECK_NEAR(flat.getSample(0, 12000), heardGain(ChannelStrip::faderInfo().toNormalized(0.0f)), 1e-3);
    DAWLAB_CHECK(session.redo() && session.getLane(gain) != nullptr);

    // b. The point edits: one undo step each, a merged drag one together, sorted by position.
    const std::size_t steps = session.getHistory().undoCount();
    const int added = session.addPoint(gain, 24000, 0.25f);
    DAWLAB_CHECK(added == 1 && session.getLane(gain)->curve.size() == 3);
    int moved = added;
    for (int i = 1; i <= 5; ++i)
        moved = session.movePoint(gain, moved, 24000 + i * 10000, 0.25f + 0.01f * static_cast<float>(i), 77);
    session.getHistory().endMerge();
    std::printf("  a point dragged past its neighbour: index %d (expected 2), %d undo steps (expected 2)\n", moved, static_cast<int>(session.getHistory().undoCount() - steps));
    DAWLAB_CHECK(moved == 2 && session.getHistory().undoCount() == steps + 2);
    DAWLAB_CHECK(session.removePoint(gain, moved) && session.getLane(gain)->curve.size() == 2);
    DAWLAB_CHECK(session.undo() && session.getLane(gain)->curve.size() == 3);
    DAWLAB_CHECK(session.undo() && session.getLane(gain)->curve.points()[1].position == 24000); // the whole drag
    DAWLAB_CHECK(session.undo() && session.getLane(gain)->curve.size() == 2);
    DAWLAB_CHECK(session.movePoint(gain, 9, 0, 0.5f) == -1 && !session.removePoint(gain, -1));
    DAWLAB_CHECK(session.setLaneMode(pan, AutomationMode::Touch) && session.getLane(pan)->mode == AutomationMode::Touch);
    DAWLAB_CHECK(session.undo() && session.getLane(pan) == nullptr);

    // c. The hand: Touch mode, a recording is one undo step, and moving the fader adds no other.
    engine.getTrack(first)->setGainDb(-6.0f);
    DAWLAB_CHECK(session.setLaneMode(gain, AutomationMode::Touch));
    AudioBuffer block(kNumChannels, 512);
    engine.getTransport().setPosition(0);
    engine.getTransport().play();
    const std::size_t beforeRecording = session.getHistory().undoCount();
    const float inPass = ChannelStrip::faderInfo().toNormalized(-30.0f);
    session.touchLane(gain);
    DAWLAB_CHECK(session.handMoves(gain, -30.0f));
    for (int b = 0; b < 20; ++b)
        engine.processBlock(block.toBlock());
    session.releaseLane(gain);
    for (int b = 0; b < 2; ++b)
        engine.processBlock(block.toBlock());
    engine.getTransport().stop();
    const int passes = session.collectAutomation();
    std::printf("  a hand in Touch mode: %d pass, %d undo step, curve at 5120: %.3f (expected 1, 1, %.3f)\n", passes,
                static_cast<int>(session.getHistory().undoCount() - beforeRecording), static_cast<double>(session.getLane(gain)->curve.valueAt(5120)), static_cast<double>(inPass));
    DAWLAB_CHECK(passes == 1 && session.getHistory().undoCount() == beforeRecording + 1);
    DAWLAB_CHECK_NEAR(session.getLane(gain)->curve.valueAt(5120), inPass, 1e-5);
    DAWLAB_CHECK(session.undo());
    DAWLAB_CHECK_NEAR(session.getLane(gain)->curve.valueAt(5120), lane.curve.valueAt(5120), 1e-5);
    DAWLAB_CHECK(!session.handMoves({StripTarget::ofTrack(second), EffectId{}, "Gain"}, -3.0f)); // no lane: the usual edit

    // A first recording, on a fader behind latency, with no curve yet: the curve is the pass, from 0.
    const LaneAddress secondGain{StripTarget::ofTrack(second), EffectId{}, "Gain"};
    InsertEffect* limiter = session.setBuiltinInsert(session.trackRef(second), 0, EffectType::Limiter);
    engine.applyPendingCommandsOffline();
    engine.getTransport().setPosition(0);
    DAWLAB_CHECK(session.setLaneMode(secondGain, AutomationMode::Write));
    engine.getTrack(second)->setGainDb(-12.0f);
    engine.getTransport().play();
    for (int b = 0; b < 4; ++b)
        engine.processBlock(block.toBlock());
    engine.getTransport().stop();
    engine.processBlock(block.toBlock());
    session.collectAutomation();
    const LaneContent* recorded = session.getLane(secondGain);
    DAWLAB_CHECK(recorded != nullptr && recorded->curve.size() >= 2 && recorded->curve.points().front().position == 0);
    DAWLAB_CHECK(recorded != nullptr && recorded->mode == AutomationMode::Write);
    // With nothing to return to, the curve simply holds the hand's value before and after the pass.
    DAWLAB_CHECK(recorded != nullptr);
    if (recorded != nullptr)
        DAWLAB_CHECK_NEAR(recorded->curve.valueAt(100000), ChannelStrip::faderInfo().toNormalized(-12.0f), 1e-5);
    (void)limiter;
    session.setLane(secondGain, std::nullopt);
    session.clearInsert(session.trackRef(second), 0);
    engine.applyPendingCommandsOffline();
    engine.collectGarbage();

    // d. A lane leaves with its owner and comes back with it, under the same id.
    InsertEffect* fx = session.setBuiltinInsert(session.trackRef(first), 1, EffectType::Gain);
    engine.applyPendingCommandsOffline();
    const EffectId fxId = fx->getEffectId();
    fx->setParameterByName("Gain", -6.0f);
    const LaneAddress fxGain{StripTarget::ofTrack(first), fxId, "Gain"};
    LaneContent fxLane;
    fxLane.curve = testRamp(0.25f, 0.75f);
    DAWLAB_CHECK(session.setLane(fxGain, fxLane));
    engine.applyPendingCommandsOffline();
    engine.processBlock(block.toBlock());
    engine.collectGarbage();
    DAWLAB_CHECK_NEAR(session.knobValue(*fx, 0), -6.0f, 1e-4); // not the curve's value
    const std::size_t lanesWithEffect = session.getLanesOf(session.trackRef(first)).size();

    DAWLAB_CHECK(session.perform(edit::SetInsert{StripTarget::ofTrack(first), 1, InsertSnapshot{}}, "Remove"));
    DAWLAB_CHECK(session.getLane(fxGain) == nullptr && !session.isLaneLive(fxGain));
    DAWLAB_CHECK(session.getLanesOf(session.trackRef(first)).size() == lanesWithEffect - 1);
    DAWLAB_CHECK(session.undo());
    fx = session.getSlot(session.trackRef(first), 1).effect;
    std::printf("  effect removed and brought back: id %u (expected %u), lane %s, knob %.1f dB (expected -6.0)\n", fx != nullptr ? fx->getEffectId().value : 0u, fxId.value,
                session.getLane(fxGain) != nullptr ? "back" : "LOST", fx != nullptr ? static_cast<double>(session.knobValue(*fx, 0)) : 0.0);
    DAWLAB_CHECK(fx != nullptr && fx->getEffectId() == fxId && session.getLane(fxGain) != nullptr);
    if (fx != nullptr)
        DAWLAB_CHECK_NEAR(session.knobValue(*fx, 0), -6.0f, 1e-3); // not the value the curve had moved it to
    engine.applyPendingCommandsOffline();
    engine.getTransport().setPosition(24000);
    engine.processBlock(block.toBlock());
    DAWLAB_CHECK(fx != nullptr);
    DAWLAB_CHECK(session.isLaneLive(fxGain));
    DAWLAB_CHECK_NEAR(fx->getParameterNormalized(0), fxLane.curve.valueAt(24000), 1e-5); // and it plays
    engine.collectGarbage();

    // Another effect in that slot does not inherit the curve.
    InsertEffect* other = session.setBuiltinInsert(session.trackRef(first), 1, EffectType::Gain);
    DAWLAB_CHECK(other != nullptr && other->getEffectId() != fxId && session.getLane(fxGain) == nullptr);
    engine.applyPendingCommandsOffline();
    engine.collectGarbage();
    session.clearInsert(session.trackRef(first), 1);
    engine.applyPendingCommandsOffline();
    engine.collectGarbage();

    // The whole track, with an effect and lanes on both.
    fx = session.setBuiltinInsert(session.trackRef(first), 0, EffectType::Gain);
    fx->setParameterByName("Gain", -9.0f);
    const LaneAddress again{StripTarget::ofTrack(first), fx->getEffectId(), "Gain"};
    DAWLAB_CHECK(session.setLane(again, fxLane));
    engine.applyPendingCommandsOffline();
    engine.processBlock(block.toBlock()); // the curve has moved the parameter away from its knob
    DAWLAB_CHECK(session.perform(edit::RemoveTrack{first}, "Delete"));
    DAWLAB_CHECK(session.getLane(gain) == nullptr && session.getLane(again) == nullptr);
    engine.applyPendingCommandsOffline();
    engine.collectGarbage();
    DAWLAB_CHECK(session.undo() && session.getLane(gain) != nullptr && session.getLane(again) != nullptr);
    DAWLAB_CHECK(session.getLane(gain) != nullptr && session.getLane(gain)->curve == lane.curve);
    DAWLAB_CHECK(session.getLane(gain) != nullptr && session.getLane(gain)->mode == AutomationMode::Touch);
    engine.applyPendingCommandsOffline();
    DAWLAB_CHECK(engine.getTrack(first) != nullptr && engine.getTrack(first)->getOwnLane(engine::kLaneGain)->getCurve() != nullptr);

    // e. The file: everything comes back, under new ids, also in a project opened again.
    const juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("l18_automation", "");
    dir.createDirectory();
    const juce::File file = dir.getChildFile("Song.dawlab");
    juce::String error;
    session.setLaneMode(again, AutomationMode::Latch);
    engine.applyPendingCommandsOffline();
    engine.processBlock(block.toBlock()); // the curve moves the parameter again, away from its knob
    DAWLAB_CHECK(ProjectIO::save(session, file, error));
    const std::string saved = project::writeProject(ProjectIO::capture(session, "Song_media").state);
    {
        auto second2 = std::make_unique<MainComponent>(MainComponent::Mode::Offline);
        Session& opened = second2->getSession();
        // Burn some ids, so the file's cannot match by accident.
        const TrackId burnt = opened.addTrack();
        opened.removeTrack(burnt);
        const ProjectIO::OpenResult r = ProjectIO::open(opened, nullptr, file);
        DAWLAB_CHECK(r.ok && r.warnings.isEmpty());
        const TrackId newFirst = opened.getTrackId(0);
        DAWLAB_CHECK(newFirst != first);
        const LaneContent* openedGain = opened.getLane({StripTarget::ofTrack(newFirst), EffectId{}, "Gain"});
        const InsertSlotState& slot = opened.getSlot(opened.trackRef(newFirst), 0);
        const LaneContent* openedFx = slot.isEmpty() ? nullptr : opened.getLane({StripTarget::ofTrack(newFirst), slot.effect->getEffectId(), "Gain"});
        std::printf("  saved and opened: fader lane %s, effect lane %s, track id %u -> %u\n", openedGain != nullptr ? "back" : "LOST", openedFx != nullptr ? "back" : "LOST", first.value, newFirst.value);
        DAWLAB_CHECK(openedGain != nullptr && openedGain->curve == session.getLane(gain)->curve && openedGain->mode == AutomationMode::Touch);
        DAWLAB_CHECK(openedFx != nullptr && openedFx->mode == AutomationMode::Latch && openedFx->curve == fxLane.curve);
        if (!slot.isEmpty())
            DAWLAB_CHECK_NEAR(opened.knobValue(*slot.effect, 0), -9.0f, 1e-3); // the knob is saved, not where the curve was
        DAWLAB_CHECK(project::writeProject(ProjectIO::capture(opened, "Song_media").state) == saved);
        second2->getEngine().applyPendingCommandsOffline();
        // The engine has them too.
        DAWLAB_CHECK(second2->getEngine().getTrack(newFirst)->getOwnLane(engine::kLaneGain)->getCurve() != nullptr);
        DAWLAB_CHECK(!slot.isEmpty() && slot.effect->getLane(0)->getCurve() != nullptr);
    }
    {
        // A lane for a parameter this version does not know stays, silent, and is saved again.
        const juce::String text = file.loadFileAsString() + "lane 99 track:1 0 \"Future\" read 0.5\npoint 99 0 0.25 linear\n";
        const juce::File edited = dir.getChildFile("Edited.dawlab");
        DAWLAB_CHECK(edited.replaceWithText(text));
        DAWLAB_CHECK(dir.getChildFile("Song_media").copyDirectoryTo(dir.getChildFile("Edited_media")));
        auto third = std::make_unique<MainComponent>(MainComponent::Mode::Offline);
        const ProjectIO::OpenResult r = ProjectIO::open(third->getSession(), nullptr, edited);
        DAWLAB_CHECK(r.ok && r.warnings.size() == 1 && r.warnings[0].contains("Future"));
        const std::string again2 = project::writeProject(ProjectIO::capture(third->getSession(), "Edited_media").state);
        std::printf("  a lane for an unknown parameter: %s; saved again: %s\n", r.warnings.isEmpty() ? "no warning" : r.warnings[0].toRawUTF8(), again2.find("\"Future\"") != std::string::npos ? "kept" : "LOST");
        DAWLAB_CHECK(again2.find("\"Future\"") != std::string::npos);
    }
    dir.deleteRecursively();

    // f. Another sample rate: positions scale, values stay, and the history starts over.
    const int64_t before = session.getLane(gain)->curve.points()[1].position;
    session.applySampleRateConversion(session.getClipSampleRate() * 2.0, ConvertedBuffers{});
    DAWLAB_CHECK(session.getLane(gain)->curve.points()[1].position == before * 2);
    DAWLAB_CHECK(session.getLane(gain)->curve.points()[1].value == lane.curve.points()[1].value);
    session.applySampleRateConversion(session.getClipSampleRate() / 2.0, ConvertedBuffers{});
    DAWLAB_CHECK(session.getLane(gain)->curve.points()[1].position == before);

    // g. The view: rows of two heights, and the mouse.
    ArrangementView& view = main->getArrangement();
    session.dispatchPendingMessages();
    const int laneHeightBefore = view.getLaneHeight();
    view.toggleAutomation(first);
    DAWLAB_CHECK(view.isAutomationShown(first) && *view.getShownLane(first) == gain);
    const auto row0 = view.getLaneBounds(0), lane0 = view.getAutomationBounds(0), row1 = view.getLaneBounds(1);
    std::printf("  the first track's lane shown: row %d .. %d, lane .. %d, next row from %d; clip rows %d -> %d high\n", row0.getY(), row0.getBottom(), lane0.getBottom(), row1.getY(), laneHeightBefore, view.getLaneHeight());
    DAWLAB_CHECK(lane0.getY() == row0.getBottom() && row1.getY() == lane0.getBottom() && lane0.getHeight() == ArrangementView::kAutomationHeight);
    DAWLAB_CHECK(view.getLaneHeight() <= laneHeightBefore && view.getAutomationBounds(1).isEmpty());
    DAWLAB_CHECK(view.laneAt(lane0.getCentreY()) == 0 && view.automationLaneAt(lane0.getCentreY()) == 0);
    DAWLAB_CHECK(view.automationLaneAt(row0.getCentreY()) == -1 && view.laneAt(row1.getCentreY()) == 1);
    DAWLAB_CHECK(session.getLane(gain) != nullptr && view.getLaneHeight() >= ArrangementView::kMinLaneHeight);
    DAWLAB_CHECK(std::abs(view.yToValue(0, view.valueToY(0, 0.25f)) - 0.25f) < 0.02f);

    // Free from the beat grid (Alt), so that positions are what the mouse says.
    const juce::ModifierKeys free(juce::ModifierKeys::altModifier | juce::ModifierKeys::leftButtonModifier);
    const int sizeBefore = session.getLane(gain)->curve.size();
    const std::size_t historyBefore = session.getHistory().undoCount();
    const int x = view.getWidth() - 40, y = lane0.getCentreY();
    const juce::Point<float> at{static_cast<float>(x), static_cast<float>(y)};
    view.mouseDown(mouseAt(view, x, y, free, at));
    DAWLAB_CHECK(session.getLane(gain)->curve.size() == sizeBefore + 1);
    const int point = view.pointAt(0, static_cast<float>(x), static_cast<float>(y));
    DAWLAB_CHECK(point >= 0);
    view.mouseDrag(mouseAt(view, x + 5, y - 10, free, at));
    view.mouseDrag(mouseAt(view, x + 10, y - 20, free, at));
    view.mouseUp(mouseAt(view, x + 10, y - 20, free, at));
    const int draggedPoint = view.pointAt(0, static_cast<float>(x + 10), static_cast<float>(y - 20));
    std::printf("  a click and a drag in the lane: %d points more, %d undo step, the point under the mouse: %s\n", session.getLane(gain)->curve.size() - sizeBefore,
                static_cast<int>(session.getHistory().undoCount() - historyBefore), draggedPoint >= 0 ? "yes" : "no");
    DAWLAB_CHECK(draggedPoint >= 0 && session.getLane(gain)->curve.size() == sizeBefore + 1);
    DAWLAB_CHECK(session.getHistory().undoCount() == historyBefore + 1);
    // A right click on it removes it; on nothing does nothing.
    const juce::ModifierKeys right(juce::ModifierKeys::rightButtonModifier);
    view.mouseDown(mouseAt(view, x + 10, y - 20, right, {static_cast<float>(x + 10), static_cast<float>(y - 20)}));
    DAWLAB_CHECK(session.getLane(gain)->curve.size() == sizeBefore);
    view.mouseDown(mouseAt(view, x + 10, y + 20, right, {static_cast<float>(x + 10), static_cast<float>(y + 20)}));
    DAWLAB_CHECK(session.getLane(gain)->curve.size() == sizeBefore);
    view.mouseUp(mouseAt(view, x + 10, y + 20, right, {static_cast<float>(x + 10), static_cast<float>(y + 20)}));

    // The "A" in the header toggles the lane; a deleted track takes its lane off the screen.
    const auto toggle = view.getAutomationToggleBounds(1);
    const juce::ModifierKeys left(juce::ModifierKeys::leftButtonModifier);
    view.mouseDown(mouseAt(view, toggle.getCentreX(), toggle.getCentreY(), left, toggle.getCentre().toFloat()));
    view.mouseUp(mouseAt(view, toggle.getCentreX(), toggle.getCentreY(), left, toggle.getCentre().toFloat()));
    DAWLAB_CHECK(view.isAutomationShown(second));
    DAWLAB_CHECK(view.getLaneBounds(2).getY() == view.getAutomationBounds(1).getBottom());
    DAWLAB_CHECK(session.perform(edit::RemoveTrack{second}, "Delete"));
    session.dispatchPendingMessages();
    DAWLAB_CHECK(!view.isAutomationShown(second));
    const auto parameters = session.getAutomatableParameters(session.trackRef(first));
    std::printf("  what can be automated on the first track: %d parameters\n", static_cast<int>(parameters.size()));
    DAWLAB_CHECK(parameters.size() >= 3 && parameters[0].second == gain && parameters[1].second == pan && parameters[2].second == again);

    // A bounce plays the curve, even with a lane in Write mode; nothing is recorded.
    session.setLaneMode(gain, AutomationMode::Write);
    engine.applyPendingCommandsOffline();
    const auto sizeBeforeBounce = session.getLane(gain)->curve.size();
    renderOffline(engine, bounce);
    DAWLAB_CHECK(session.collectAutomation() == 0 && session.getLane(gain)->curve.size() == sizeBeforeBounce);

    // File > New and the demo song clear all of it, also on the fixed strips.
    const LaneAddress masterGain{StripTarget::of(ChannelRef::master()), EffectId{}, "Gain"};
    DAWLAB_CHECK(session.setLane(masterGain, lane));
    engine.applyPendingCommandsOffline();
    session.newProject();
    engine.applyPendingCommandsOffline();
    DAWLAB_CHECK(session.getLanesOf(ChannelRef::master()).empty());
    DAWLAB_CHECK(engine.getStrip(ChannelRef::master())->getOwnLane(engine::kLaneGain)->getCurve() == nullptr);
    DAWLAB_CHECK(!engine.getStrip(ChannelRef::master())->getOwnLane(engine::kLaneGain)->isActive());
    engine.collectGarbage();
    DAWLAB_CHECK(engine.getGarbageLeaked() == 0);
}

// ---- instruments and MIDI clips (chapter 18, Step 8) --------------------------------------------------

engine::Track* engineTrack(MainComponent& main, TrackId id) { return main.getEngine().getTrack(id); }

MidiClipModel midiClip(std::int64_t start, std::vector<engine::Note> played) {
    MidiClipModel c;
    c.start = start;
    c.length = 4 * 960;
    c.name = "Riff";
    c.played = std::move(played);
    return c;
}

void testMidiModel() {
    std::printf("instruments and MIDI clips: the model\n");
    auto mainPtr = std::make_unique<MainComponent>(MainComponent::Mode::Offline);
    MainComponent& main = *mainPtr;
    Session& session = main.getSession();
    Engine& engine = main.getEngine();
    session.newProject();
    engine.applyPendingCommandsOffline();

    // An instrument track, as one undo step.
    const TrackId synth = main.addTrack(TrackFormat::Mono, true);
    engine.applyPendingCommandsOffline();
    DAWLAB_CHECK(synth.isValid() && session.isInstrument(synth) && engineTrack(main, synth)->isInstrument());
    DAWLAB_CHECK(session.getStripName(session.trackRef(synth)).startsWith("Synth"));
    DAWLAB_CHECK(main.undo() && !session.hasTrack(synth));
    DAWLAB_CHECK(main.redo() && session.isInstrument(synth));
    engine.applyPendingCommandsOffline();

    // A clip, as one undo step: it gets an id, and the engine plays it.
    DAWLAB_CHECK(session.perform(edit::SetMidiClips{synth, {midiClip(0, {{0, 960, 60, 100, 0}, {250, 960, 64, 100, 0}})}}, "Add MIDI clip"));
    engine.applyPendingCommandsOffline();
    const auto& clips = session.getMidiClips(synth);
    DAWLAB_CHECK(clips.size() == 1 && clips[0].id > 0);
    const std::vector<engine::MidiClip>* playing = engineTrack(main, synth)->getMidiClips();
    DAWLAB_CHECK(playing != nullptr && playing->size() == 1 && (*playing)[0].notes.size() == 2);
    engine.getTransport().setPosition(0);
    engine.getTransport().play();
    main.simulatePlayback(4800);
    DAWLAB_CHECK(engineTrack(main, synth)->getSynth()->activeVoices() == 1); // the note at tick 250 is at 6250
    main.simulatePlayback(4800);
    DAWLAB_CHECK(engineTrack(main, synth)->getSynth()->activeVoices() == 2);
    engine.getTransport().stop();
    main.simulatePlayback(512);

    // Quantize: the engine plays 250 on the grid, the clip keeps 250 as played.
    std::vector<MidiClipModel> quantized = session.getMidiClips(synth);
    quantized[0].quantize = {240, 1.0, 0.0};
    DAWLAB_CHECK(session.perform(edit::SetMidiClips{synth, quantized}, "Quantize"));
    engine.applyPendingCommandsOffline();
    playing = engineTrack(main, synth)->getMidiClips();
    DAWLAB_CHECK(playing != nullptr && (*playing)[0].notes[1].start == 240 && session.getMidiClips(synth)[0].played[1].start == 250);
    const int clipId = session.getMidiClips(synth)[0].id;
    DAWLAB_CHECK((*playing)[0].id == clipId); // the same clip for the player, through the edit
    DAWLAB_CHECK(main.undo());
    engine.applyPendingCommandsOffline();
    DAWLAB_CHECK(session.getMidiClips(synth)[0].quantize.strength == 0.0 && (*engineTrack(main, synth)->getMidiClips())[0].notes[1].start == 250);
    DAWLAB_CHECK(main.redo());

    // Not an instrument any more, and back, as undo steps.
    DAWLAB_CHECK(session.perform(edit::SetInstrument{synth, false}, "Audio track"));
    DAWLAB_CHECK(!session.isInstrument(synth) && !engineTrack(main, synth)->isInstrument());
    DAWLAB_CHECK(main.undo() && session.isInstrument(synth) && engineTrack(main, synth)->isInstrument());

    // Deleted and brought back: the instrument and its clips, with the same clip ids.
    const std::vector<MidiClipModel> before = session.getMidiClips(synth);
    main.getArrangement().setSelectedTrack(synth);
    DAWLAB_CHECK(main.deleteSelectedTrack() && !session.hasTrack(synth));
    engine.applyPendingCommandsOffline();
    DAWLAB_CHECK(main.undo() && session.hasTrack(synth) && session.isInstrument(synth));
    engine.applyPendingCommandsOffline();
    DAWLAB_CHECK(session.getMidiClips(synth) == before && engineTrack(main, synth)->isInstrument());
    DAWLAB_CHECK(engineTrack(main, synth)->getMidiClips() != nullptr && engineTrack(main, synth)->getMidiClips()->size() == 1);

    // Saved and opened: the same, with the played notes and the quantize; new clip ids are fine.
    TakesFolder folder;
    const juce::File file = folder.get().getChildFile("Midi.dawlab");
    DAWLAB_CHECK(main.saveProject(file));
    const ProjectIO::OpenResult opened = main.openProject(file);
    DAWLAB_CHECK(opened.ok && opened.warnings.isEmpty());
    engine.applyPendingCommandsOffline();
    TrackId reopened;
    for (int i = 0; i < session.getNumTracks(); ++i)
        if (session.isInstrument(session.getTrackId(i)))
            reopened = session.getTrackId(i);
    DAWLAB_CHECK(reopened.isValid() && session.getMidiClips(reopened).size() == 1);
    if (reopened.isValid() && session.getMidiClips(reopened).size() == 1) {
        MidiClipModel a = session.getMidiClips(reopened)[0], b = before[0];
        a.id = b.id = 0;
        DAWLAB_CHECK(a == b && a.quantize.strength == 1.0 && a.played[1].start == 250);
        DAWLAB_CHECK(engineTrack(main, reopened)->isInstrument());
    }
    engine.collectGarbage();
    DAWLAB_CHECK(engine.getGarbageLeaked() == 0);
}

void testMidiEditing() {
    std::printf("instruments and MIDI clips: editing\n");
    auto mainPtr = std::make_unique<MainComponent>(MainComponent::Mode::Offline);
    MainComponent& main = *mainPtr;
    main.setSize(MainComponent::kDefaultWidth, MainComponent::kDefaultHeight);
    Session& session = main.getSession();
    Engine& engine = main.getEngine();
    session.newProject();
    engine.applyPendingCommandsOffline();
    const TrackId synth = main.addTrack(TrackFormat::Mono, true);
    engine.applyPendingCommandsOffline();
    History& history = session.getHistory();

    // A clip of one bar at bar 2, moved by a drag of three steps: one undo step.
    const int added = session.addMidiClip(synth, 3840, 3840);
    DAWLAB_CHECK(added == 0 && history.getUndoName() == "New MIDI clip");
    history.endMerge();
    const std::uint32_t key = 0x12345678u;
    session.moveMidiClip(synth, 0, 4800, key);
    session.moveMidiClip(synth, 0, 5760, key);
    session.moveMidiClip(synth, 0, 7680, key);
    history.endMerge();
    DAWLAB_CHECK(session.getMidiClips(synth)[0].start == 7680 && history.getUndoName() == "Move MIDI clip");
    DAWLAB_CHECK(main.undo() && session.getMidiClips(synth)[0].start == 3840);
    DAWLAB_CHECK(main.redo());

    // The arrangement finds it where it plays (bar 3 at 120 BPM: sample 192000).
    ArrangementView& view = main.getArrangement();
    view.setView(0.0, 400.0);
    const int lane = session.indexOf(synth);
    const juce::Rectangle<int> bounds = view.getMidiClipBounds(lane, 0);
    DAWLAB_CHECK(!bounds.isEmpty() && std::abs(view.xToSample(static_cast<float>(bounds.getX())) - 192000.0) < 800.0);
    DAWLAB_CHECK(view.midiClipAt(lane, static_cast<float>(bounds.getCentreX())) == 0);
    DAWLAB_CHECK(view.midiClipAt(lane, static_cast<float>(bounds.getX() - 20)) == -1);

    // The piano roll, with gestures instead of a mouse.
    const int clipId = session.getMidiClips(synth)[0].id;
    PianoRoll roll(session, synth, clipId);
    roll.setSize(900, 480);
    roll.setView(0.0, 4.0); // 4 ticks a pixel
    roll.setGrid(240);
    auto at = [&roll](double tick, int pitch) { return juce::Point<float>(roll.tickToX(tick), roll.pitchToY(pitch) + 4.0f); };
    const juce::ModifierKeys none;
    const juce::ModifierKeys alt(juce::ModifierKeys::altModifier);
    const juce::ModifierKeys right(juce::ModifierKeys::rightButtonModifier);
    auto played = [&session, synth] { return session.getMidiClips(synth)[0].played; };

    // Draw: a note on the grid (250 -> 240), dragged out to tick 900 (the line after it: 960).
    roll.press(at(250, 60), none);
    roll.dragTo(at(600, 60), none);
    roll.dragTo(at(900, 60), none);
    roll.release();
    DAWLAB_CHECK((played().size() == 1 && played()[0] == engine::Note{240, 720, 60, 100, 0} && history.getUndoName() == "Add note"));
    // Move: in time (on the grid) and in pitch, three steps, one undo step.
    roll.press(at(300, 60), none);
    roll.dragTo(at(500, 61), none);
    roll.dragTo(at(780, 62), none);
    roll.release();
    DAWLAB_CHECK(played().size() == 1 && played()[0].start == 720 && played()[0].pitch == 62 && played()[0].length == 720);
    DAWLAB_CHECK(history.getUndoName() == "Move note");
    DAWLAB_CHECK(main.undo() && played()[0].start == 240 && played()[0].pitch == 60);
    DAWLAB_CHECK(main.redo());
    // Resize: grab its end and drag.
    const juce::Rectangle<float> note = roll.getNoteBounds(0);
    roll.press({note.getRight() - 2.0f, note.getCentreY()}, none);
    roll.dragTo({roll.tickToX(1900), note.getCentreY()}, none);
    roll.release();
    DAWLAB_CHECK(played()[0].start == 720 && played()[0].length == 1920 - 720 && history.getUndoName() == "Note length");
    // Velocity: Alt and up.
    roll.press({note.getCentreX(), note.getCentreY()}, alt);
    roll.dragTo({note.getCentreX(), note.getCentreY() - 40.0f}, alt);
    roll.release();
    DAWLAB_CHECK(played()[0].velocity == 120 && played()[0].start == 720 && history.getUndoName() == "Note velocity");
    // A second note, then delete the first with a right click.
    roll.press(at(2000, 64), none);
    roll.release();
    DAWLAB_CHECK(played().size() == 2);
    roll.press({roll.getNoteBounds(0).getCentreX(), roll.getNoteBounds(0).getCentreY()}, right);
    roll.release();
    DAWLAB_CHECK(played().size() == 1 && played()[0].pitch == 64 && history.getUndoName() == "Delete note");
    // A click on nothing outside the notes area does nothing.
    roll.press({10.0f, 100.0f}, none);
    roll.release();
    DAWLAB_CHECK(played().size() == 1);
    // Six gestures, six undo steps back to the empty clip.
    for (int i = 0; i < 6; ++i)
        main.undo();
    DAWLAB_CHECK(played().empty() && session.getMidiClips(synth)[0].start == 7680);

    // Quantize and mute from the clip's menu, as undo steps; the roll and the arrangement paint it all.
    for (int i = 0; i < 6; ++i)
        main.redo();
    DAWLAB_CHECK(main.quantizeMidiClip(synth, 0, {240, 1.0, 1.0 / 3.0}) && history.getUndoName() == "Quantize");
    DAWLAB_CHECK(session.getMidiClips(synth)[0].played[0].start == 1920 && session.getMidiClips(synth)[0].notes()[0].start == 1920);
    DAWLAB_CHECK(main.setMidiClipMuted(synth, 0, true) && session.getMidiClips(synth)[0].muted);
    engine.applyPendingCommandsOffline();
    DAWLAB_CHECK((*engine.getTrack(synth)->getMidiClips())[0].muted);
    const juce::Image rollImage = roll.createComponentSnapshot(roll.getLocalBounds());
    const juce::Image mainImage = main.createComponentSnapshot(main.getLocalBounds());
    DAWLAB_CHECK(rollImage.isValid() && mainImage.isValid());
    // The clip deleted while its roll is open: the roll draws that it is gone, and its gestures do nothing.
    DAWLAB_CHECK(session.removeMidiClip(synth, 0) && roll.getClip() == nullptr);
    roll.press(at(250, 60), none);
    roll.release();
    DAWLAB_CHECK(session.getMidiClips(synth).empty());
    DAWLAB_CHECK(roll.createComponentSnapshot(roll.getLocalBounds()).isValid());
    DAWLAB_CHECK(main.openPianoRoll(synth, 0) == nullptr); // offline, and no such clip anyway
}

// A player at a keyboard, against the offline device: the test's clock, blocks of 512 samples, no output
// latency. Block k is heard at k x 512 / rate (its blockTime); a key played in time with what is heard at
// unwrapped song sample S (counted from the recording's start, through loop wraps) goes down at
// (S + mix latency) / rate. Every key arrives before the block that is being rendered when it goes down.
struct Keyboard {
    MainComponent& main;
    double clock = 0.0;
    struct Key {
        double time;
        engine::MidiMessage message;
    };
    std::vector<Key> keys;

    explicit Keyboard(MainComponent& m) : main(m) {
        main.getAudioHost().setClock([this] { return clock; });
    }
    ~Keyboard() { main.getAudioHost().setClock({}); }
    Keyboard(const Keyboard&) = delete;
    Keyboard& operator=(const Keyboard&) = delete;

    // A note at unwrapped song tick `tick`, `length` ticks long, at 120 BPM (25 samples a tick).
    void note(std::int64_t tick, std::int64_t length, int pitch, int velocity = 100) {
        const double mix = main.getEngine().getLatencySamples();
        keys.push_back({(25.0 * static_cast<double>(tick) + mix) / 48000.0, engine::MidiMessage::noteOn(0, pitch, velocity)});
        keys.push_back({(25.0 * static_cast<double>(tick + length) + mix) / 48000.0, engine::MidiMessage::noteOff(0, pitch)});
    }
    // Plays `blocks` blocks, from the time of block `first` (k x 512 / 48000).
    void play(int first, int blocks) {
        std::stable_sort(keys.begin(), keys.end(), [](const Key& a, const Key& b) { return a.time < b.time; });
        for (int k = first; k < first + blocks; ++k) {
            clock = k * 512.0 / 48000.0;
            for (auto it = keys.begin(); it != keys.end() && it->time <= clock + 512.0 / 48000.0;) {
                main.getAudioHost().pushMidi({it->time, it->message});
                it = keys.erase(it);
            }
            main.simulatePlayback(512);
            if (k % 8 == 0)
                main.tick();
        }
    }
};

void testMidiRecording() {
    std::printf("instruments and MIDI clips: recording the keyboard\n");
    auto mainPtr = std::make_unique<MainComponent>(MainComponent::Mode::Offline);
    MainComponent& main = *mainPtr;
    Session& session = main.getSession();
    Engine& engine = main.getEngine();
    Transport& tr = engine.getTransport();
    session.newProject();
    engine.applyPendingCommandsOffline();
    const TrackId synth = main.addTrack(TrackFormat::Mono, true);
    const TrackId audio = session.getTrackId(0);
    engine.applyPendingCommandsOffline();
    DAWLAB_CHECK(main.setArmed(synth, true));

    // Two bars, straight: every note on the tick it was played, its length as held.
    {
        Keyboard kb(main);
        kb.note(0, 480, 60);
        kb.note(960, 480, 64, 90);
        kb.note(1920, 240, 67);
        kb.note(3001, 299, 72); // off the beat: recorded where it was played
        tr.setPosition(0);
        DAWLAB_CHECK(main.toggleRecord() && main.getMidiRecording().isRecording() && tr.isPlaying());
        kb.play(0, 20);
        DAWLAB_CHECK(engine.getTrack(synth)->getSynth()->activeVoices() == 1); // the key heard while recording
        // Nor a new tempo: the notes after it would land on the wrong ticks.
        const double bpmBefore = tr.getTempo();
        main.getTransportBar().getTempoSlider().setValue(bpmBefore + 20.0, juce::sendNotificationSync);
        DAWLAB_CHECK(tr.getTempo() == bpmBefore && main.getStatus().contains("recording"));

        kb.play(20, 355);                                                         // to 2 bars
        DAWLAB_CHECK(main.toggleRecord() && !main.getMidiRecording().isRecording());
    }
    tr.stop();
    main.simulatePlayback(512);
    DAWLAB_CHECK(session.getMidiClips(synth).size() == 1 && session.getHistory().getUndoName() == "Record MIDI");
    if (session.getMidiClips(synth).size() == 1) {
        const MidiClipModel& take = session.getMidiClips(synth)[0];
        // It ends where the song was heard when Record was pressed again: the last block's time, minus the mix.
        DAWLAB_CHECK(take.start == 0 && take.length > 7500 && take.length < 7680 && take.played.size() == 4);
        if (take.played.size() == 4) {
            DAWLAB_CHECK((take.played[0] == engine::Note{0, 480, 60, 100, 0} && take.played[1] == engine::Note{960, 480, 64, 90, 0}));
            DAWLAB_CHECK((take.played[2] == engine::Note{1920, 240, 67, 100, 0} && take.played[3] == engine::Note{3001, 299, 72, 100, 0}));
        }
        std::printf("  4 notes played against a device in 512-sample blocks: recorded at ticks %lld, %lld, %lld, %lld\n",
                    static_cast<long long>(take.played[0].start), static_cast<long long>(take.played[1].start),
                    static_cast<long long>(take.played[2].start), static_cast<long long>(take.played[3].start));
    }
    DAWLAB_CHECK(session.getClips(audio).empty()); // no audio take: only an instrument track was armed
    DAWLAB_CHECK(main.undo() && session.getMidiClips(synth).empty());

    // A loop of one bar, 2.5 passes, over an old clip with a note at beat 4. Pass 0 plays beat 1, pass 1 beat 2,
    // pass 2 beat 1 again and stops at beat 3.
    auto loopTake = [&](MidiRecordMode mode) {
        session.setMidiClips(synth, {midiClip(0, {{2880, 240, 48, 100, 0}})});
        engine.applyPendingCommandsOffline();
        tr.setLoopRange(0, 96000);
        tr.setLoopEnabled(true);
        tr.setPosition(0);
        main.setMidiRecordMode(mode);
        Keyboard kb(main);
        kb.note(0, 240, 60);
        kb.note(3840 + 960, 240, 62);
        kb.note(7680, 240, 64);
        DAWLAB_CHECK(main.toggleRecord());
        kb.play(0, 470); // 2.45 bars
        tr.stop();       // stopping the transport ends it too
        kb.play(470, 1);
        main.tick();
        DAWLAB_CHECK(!main.getMidiRecording().isRecording());
        tr.setLoopEnabled(false);
    };
    loopTake(MidiRecordMode::Overdub);
    std::vector<int> pitches;
    for (const MidiClipModel& c : session.getMidiClips(synth))
        for (const engine::Note& n : c.played)
            pitches.push_back(n.pitch);
    std::sort(pitches.begin(), pitches.end());
    DAWLAB_CHECK((pitches == std::vector<int>{48, 60, 62, 64})); // everything
    DAWLAB_CHECK(main.undo());
    loopTake(MidiRecordMode::Replace);
    pitches.clear();
    for (const MidiClipModel& c : session.getMidiClips(synth))
        for (const engine::Note& n : c.played)
            pitches.push_back(n.pitch);
    // Pass 1 covered the whole loop: pass 0's note and the old note at beat 4 are gone. Pass 2 covered beats
    // 1-2: pass 1's note at beat 2 is gone too.
    DAWLAB_CHECK((pitches == std::vector<int>{64}));
    if (session.getMidiClips(synth).size() == 2) {
        const MidiClipModel& take = session.getMidiClips(synth)[1];
        DAWLAB_CHECK(take.start == 0 && take.length == 3840 && take.played.size() == 1 && take.played[0].start == 0);
    }
    DAWLAB_CHECK(main.undo() && session.getMidiClips(synth)[0].played.size() == 1); // the old note back

    // Nothing armed: nothing starts. An armed instrument track and an armed audio track: both record.
    DAWLAB_CHECK(main.setArmed(synth, false));
    DAWLAB_CHECK(!main.toggleRecord() && !main.getMidiRecording().isRecording());
    tr.stop();
    main.simulatePlayback(512);
}

// ---- audio files at the boundary (MediaLoader::readChecked) ---------------------------------------------
// What a file's header declares is checked before it is believed. The files are written by hand, 1000 frames.

void putLittleEndian(std::vector<unsigned char>& bytes, std::uint32_t value, int numBytes) {
    for (int i = 0; i < numBytes; ++i)
        bytes.push_back(static_cast<unsigned char>((value >> (8 * i)) & 0xffu));
}

// A 16-bit mono PCM WAV that declares `rate`, holding `frames` frames of 0.25.
juce::File writeRawWav(const juce::File& file, std::uint32_t rate, std::uint32_t frames) {
    std::vector<unsigned char> v{'R', 'I', 'F', 'F'};
    putLittleEndian(v, 36u + 2u * frames, 4);
    v.insert(v.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    putLittleEndian(v, 16, 4);
    putLittleEndian(v, 1, 2); // PCM
    putLittleEndian(v, 1, 2); // mono
    putLittleEndian(v, rate, 4);
    putLittleEndian(v, rate * 2u, 4);
    putLittleEndian(v, 2, 2);
    putLittleEndian(v, 16, 2);
    v.insert(v.end(), {'d', 'a', 't', 'a'});
    putLittleEndian(v, 2u * frames, 4);
    for (std::uint32_t i = 0; i < frames; ++i)
        putLittleEndian(v, 8192, 2);
    file.replaceWithData(v.data(), v.size());
    return file;
}

void putBigEndian(unsigned char* at, std::uint32_t value) {
    for (int b = 0; b < 4; ++b)
        at[b] = static_cast<unsigned char>((value >> (24 - 8 * b)) & 0xffu);
}

// 1000 frames of 16-bit mono as an AIFF written by JUCE, whose COMM and SSND chunks then claim `declaredFrames`.
juce::File writeAiff(const juce::File& file, double rate, std::uint32_t declaredFrames) {
    file.deleteFile();
    {
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(file);
        juce::AiffAudioFormat aiff;
        const auto options = juce::AudioFormatWriterOptions{}.withSampleRate(rate).withNumChannels(1).withBitsPerSample(16);
        std::unique_ptr<juce::AudioFormatWriter> writer = aiff.createWriterFor(stream, options);
        std::vector<float> samples(1000, 0.25f);
        const float* channels[] = {samples.data()};
        if (writer == nullptr || !writer->writeFromFloatArrays(channels, 1, 1000))
            return {};
    }
    juce::MemoryBlock bytes;
    file.loadFileAsData(bytes);
    auto* data = static_cast<unsigned char*>(bytes.getData());
    for (size_t i = 0; i + 14 <= bytes.getSize(); ++i) {
        if (std::memcmp(data + i, "COMM", 4) == 0) // id, size, channels (2 bytes), then the frames
            putBigEndian(data + i + 10, declaredFrames);
        if (std::memcmp(data + i, "SSND", 4) == 0) // id, then its size: offset and block size (8 bytes) and the data
            putBigEndian(data + i + 4, 8u + 2u * declaredFrames);
    }
    file.replaceWithData(bytes.getData(), bytes.getSize());
    return file;
}

// A 32-bit float mono WAV of `frames` frames of 0.25, with a NaN at `bad[0]` and +Inf at `bad[1]`.
juce::File writeFloatWav(const juce::File& file, std::uint32_t frames, std::array<std::uint32_t, 2> bad) {
    std::vector<unsigned char> v{'R', 'I', 'F', 'F'};
    putLittleEndian(v, 36u + 4u * frames, 4);
    v.insert(v.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    putLittleEndian(v, 16, 4);
    putLittleEndian(v, 3, 2); // IEEE float
    putLittleEndian(v, 1, 2); // mono
    putLittleEndian(v, 48000, 4);
    putLittleEndian(v, 48000u * 4u, 4);
    putLittleEndian(v, 4, 2);
    putLittleEndian(v, 32, 2);
    v.insert(v.end(), {'d', 'a', 't', 'a'});
    putLittleEndian(v, 4u * frames, 4);
    for (std::uint32_t i = 0; i < frames; ++i) {
        const float x = i == bad[0]   ? std::numeric_limits<float>::quiet_NaN()
                        : i == bad[1] ? std::numeric_limits<float>::infinity()
                                      : 0.25f;
        std::uint32_t bits = 0;
        std::memcpy(&bits, &x, 4);
        putLittleEndian(v, bits, 4);
    }
    file.replaceWithData(v.data(), v.size());
    return file;
}

void testAudioFileBoundary() {
    const juce::File dir = juce::File::getCurrentWorkingDirectory().getChildFile("dawlab_output").getChildFile("l18_boundary");
    dir.deleteRecursively();
    dir.createDirectory();
    // The sample rate must be one audio has: a header that says 4 GHz would make 1000 frames a fraction of a
    // second, and 20 minutes of them billions of frames.
    const struct { std::uint32_t rate; bool readable; } rates[] = {
        {7999, false}, {8000, true}, {768000, true}, {768001, false}, {4000000000u, false}};
    for (const auto& r : rates) {
        const LoadedMedia media = MediaLoader::readAudioFile(writeRawWav(dir.getChildFile("rate-" + juce::String(r.rate) + ".wav"), r.rate, 1000), 48000.0);
        std::printf("   a WAV at %u Hz: %s\n", r.rate, media.audio != nullptr ? "read" : media.error.toRawUTF8());
        DAWLAB_CHECK((media.audio != nullptr) == r.readable);
        DAWLAB_CHECK(r.readable || media.error.contains("sample rate"));
    }
    // More frames than MediaLimits::kMaxFrames (20 minutes at 192 kHz), at a rate where 20 minutes would allow them.
    const juce::File many = writeAiff(dir.getChildFile("many.aiff"), 768000.0, static_cast<std::uint32_t>(MediaLimits::kMaxFrames + 1));
    const LoadedMedia tooMany = MediaLoader::readAudioFile(many, 48000.0);
    std::printf("   an AIFF at 768 kHz that declares %lld frames: %s\n", static_cast<long long>(MediaLimits::kMaxFrames + 1),
                tooMany.audio != nullptr ? "read" : tooMany.error.toRawUTF8());
    DAWLAB_CHECK(tooMany.audio == nullptr && tooMany.error.contains("20 minutes"));

    // A file cut off in the middle. An uncompressed one (WAV, AIFF) must hold what its header declares, so the cut
    // shows; JUCE's AIFF reader would read zeros for the missing part. A compressed one cannot be told from silence:
    // its decoder returns zeros too, and such a file loads (chapter 14, Step 18: a known limit).
    const juce::File whole = writeAiff(dir.getChildFile("whole.aiff"), 48000.0, 1000);
    const juce::File cut = dir.getChildFile("cut.aiff");
    {
        juce::MemoryBlock bytes;
        whole.loadFileAsData(bytes);
        cut.replaceWithData(bytes.getData(), bytes.getSize() - 1000); // half of the 2000 bytes of samples
    }
    const LoadedMedia wholeMedia = MediaLoader::readAudioFile(whole, 48000.0);
    const LoadedMedia cutMedia = MediaLoader::readAudioFile(cut, 48000.0);
    std::printf("   an AIFF of 1000 frames: %s; cut to 500: %s\n", wholeMedia.audio != nullptr ? "read" : wholeMedia.error.toRawUTF8(),
                cutMedia.audio != nullptr ? "read" : cutMedia.error.toRawUTF8());
    DAWLAB_CHECK(wholeMedia.audio != nullptr && wholeMedia.audio->getNumSamples() == 1000);
    DAWLAB_CHECK(cutMedia.audio == nullptr && cutMedia.error.contains("cut off"));
    // The known limit, pinned: a FLAC cut the same way still loads, the missing part silent.
    const juce::File flacFile = dir.getChildFile("cut.flac");
    {
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(flacFile);
        juce::FlacAudioFormat flac;
        const auto options = juce::AudioFormatWriterOptions{}.withSampleRate(48000.0).withNumChannels(1).withBitsPerSample(16);
        std::unique_ptr<juce::AudioFormatWriter> writer = flac.createWriterFor(stream, options);
        std::vector<float> samples(48000);
        for (size_t i = 0; i < samples.size(); ++i)
            samples[i] = 0.25f * static_cast<float>(std::sin(0.13 * static_cast<double>(i))); // not compressible to nothing
        const float* channels[] = {samples.data()};
        DAWLAB_CHECK(writer != nullptr && writer->writeFromFloatArrays(channels, 1, 48000));
    }
    {
        juce::MemoryBlock bytes;
        flacFile.loadFileAsData(bytes);
        flacFile.replaceWithData(bytes.getData(), bytes.getSize() / 2);
    }
    const LoadedMedia cutFlac = MediaLoader::readAudioFile(flacFile, 48000.0);
    std::printf("   a FLAC of 48000 frames cut in half: %s\n", cutFlac.audio != nullptr ? "read (the rest silent)" : cutFlac.error.toRawUTF8());
    DAWLAB_CHECK(cutFlac.audio != nullptr);

    // Samples that are not numbers: each NaN or Inf in a file becomes 0, counted, before anything else sees it.
    // Resampled too: the resampler would spread one NaN over a whole kernel width.
    const juce::File nanFile = writeFloatWav(dir.getChildFile("nan.wav"), 1000, {500, 600});
    for (const double rate : {48000.0, 44100.0}) {
        const LoadedMedia m = MediaLoader::readAudioFile(nanFile, rate);
        std::printf("   a float WAV with a NaN and an Inf, read at %.0f Hz: %s, %d set to 0, %s\n", rate,
                    m.audio != nullptr ? "read" : m.error.toRawUTF8(), m.nonFiniteSamples,
                    m.audio != nullptr && hasNanOrInf(*m.audio) ? "NaN or Inf in the clip" : "all finite");
        DAWLAB_CHECK(m.audio != nullptr && m.nonFiniteSamples == 2 && !hasNanOrInf(*m.audio));
        if (m.audio != nullptr && rate == 48000.0)
            DAWLAB_CHECK(m.audio->getSample(0, 500) == 0.0f && m.audio->getSample(1, 600) == 0.0f &&
                         m.audio->getSample(0, 499) == 0.25f);
    }
    dir.deleteRecursively();
}

// ---- the bar grid, zoomed fully out (review A8) ----------------------------------------------------
// At the fastest tempo in 1/4, zoomed out as far as the view goes, over 400 000 bars are in view. The
// arrangement draws a line on every 2^k-th bar so that the lines are 8 px apart (GridSpacing.h):
// counted here, not timed.
void testBarGrid() {
    std::printf("the bar grid, zoomed fully out\n");
    auto mainPtr = std::make_unique<MainComponent>(MainComponent::Mode::Offline);
    MainComponent& main = *mainPtr;
    main.setSize(MainComponent::kDefaultWidth, MainComponent::kDefaultHeight);
    Transport& tr = main.getEngine().getTransport();
    tr.setTempo(Transport::kMaxTempo);
    tr.setTimeSignature(1, 4);
    ArrangementView& view = main.getArrangement();
    view.setView(0.0, 1.0e6);
    const int width = view.getWidth() - ArrangementView::kHeaderWidth;
    const double bars = view.getSamplesPerPixel() * width / tr.getSamplesPerBar();
    const grid::BarGrid lines = view.getBarGrid(8.0);
    std::printf("   %.0f BPM 1/4, %.0f samples per pixel: %.0f bars over %d px -> %lld lines, every %lld bars\n",
                tr.getTempo(), view.getSamplesPerPixel(), bars, width, static_cast<long long>(lines.count),
                static_cast<long long>(lines.step));
    DAWLAB_CHECK(width > 0 && bars > 400000.0);
    DAWLAB_CHECK(lines.count > 0 && lines.count <= width / 8 + 1);
    view.zoomToFit();
    DAWLAB_CHECK(view.getBarGrid(8.0).step == 1); // a short song: every bar
}

// ---- --smoke-test --------------------------------------------------------------------------------

void runSmokeTest(std::function<void(int exitCode)> onFinished) {
    constexpr double sr = 48000.0;
    std::setvbuf(stdout, nullptr, _IONBF, 0); // unbuffered: a crash must not swallow what was printed
    std::printf("DawLab 18 smoke test (no audio device)\n\n");
    testBarGrid();
    testMidiModel();
    testMidiEditing();
    testMidiRecording();
    testAutomation();
    testRecording();


    // 1. The resampler used when loading files (MediaLoader.cpp)
    {
        const AudioBuffer sine = makeSine(1000.0, 44100.0, 1.0, 0.5f, 1);
        const AudioBuffer up = resampleBuffer(sine, 44100.0, 48000.0);
        const int expectedLength = static_cast<int>(std::ceil(44100 * 48000.0 / 44100.0));
        const int n = 24000; // measure in the middle, away from the edges
        const double amp = goertzelAmplitude(up.getReadPointer(0) + 12000, n, 1000.0, 48000.0);
        const AudioBuffer high = makeSine(20000.0, 48000.0, 1.0, 0.5f, 1);
        const AudioBuffer down = resampleBuffer(high, 48000.0, 22050.0);
        // 20 kHz cannot exist at 22.05 kHz; without a lowpass it would alias to 22050 - 20000 = 2050 Hz.
        const double alias = goertzelAmplitude(down.getReadPointer(0) + 4000, 11025, 2050.0, 22050.0);
        std::printf("1. resampler: 1 kHz 44.1k->48k amplitude %.4f (0.5 expected, %.3f dB), length %d;\n"
                    "   20 kHz 48k->22.05k alias at 2050 Hz: %.1f dB below the input\n",
                    amp, 20.0 * std::log10(amp / 0.5), up.getNumSamples(), 20.0 * std::log10(std::max(alias, 1e-9) / 0.5));
        DAWLAB_CHECK(up.getNumSamples() == expectedLength);
        DAWLAB_CHECK_NEAR(20.0 * std::log10(amp / 0.5), 0.0, 0.05);
        DAWLAB_CHECK(20.0 * std::log10(std::max(alias, 1e-9) / 0.5) < -60.0);

        // The edge of the transition band (README 3.10): 22.3 kHz is just above the 22.05 kHz
        // Nyquist frequency of a 44.1 kHz output and lies inside the kernel's transition band
        // (stopband edge about 0.513 f_out = 22.6 kHz). It folds to 44100 - 22300 = 21800 Hz,
        // attenuated by the kernel but not removed completely.
        const AudioBuffer edge = makeSine(22300.0, 48000.0, 1.0, 0.5f, 1);
        const AudioBuffer edgeDown = resampleBuffer(edge, 48000.0, 44100.0);
        const double edgeAlias = goertzelAmplitude(edgeDown.getReadPointer(0) + 11025, 22050, 21800.0, 44100.0);
        const double edgeDb = 20.0 * std::log10(std::max(edgeAlias, 1e-9) / 0.5);
        std::printf("   22.3 kHz 48k->44.1k (transition band) alias at 21800 Hz: %.1f dB relative to the input\n", edgeDb);
        DAWLAB_CHECK(edgeDb < -50.0 && edgeDb > -80.0);

        // The file path of the loader: a mono 44.1 kHz WAV becomes a stereo 48 kHz buffer.
        const juce::File wav = juce::File::getCurrentWorkingDirectory().getChildFile("dawlab_output").getChildFile("l18_app_smoke_44k.wav");
        juce::String error;
        DAWLAB_CHECK(Exporter::writeWavFile(wav, sine, 44100.0, 24, error));
#if !defined(_WIN32)
        {
            // A write that fails half-way - here the process may not write more than 4096 bytes to a file, as on
            // a full disk - leaves the file that was there as it was (an export over the last mix, a project's WAV).
            const juce::int64 sizeBefore = wav.getSize();
            const int filesBefore = wav.getParentDirectory().getNumberOfChildFiles(juce::File::findFiles);
            const AudioBuffer longer = makeSine(500.0, 44100.0, 2.0, 0.5f, 2);
            const auto oldHandler = std::signal(SIGXFSZ, SIG_IGN);
            rlimit old{};
            getrlimit(RLIMIT_FSIZE, &old);
            rlimit small = old;
            small.rlim_cur = 4096;
            setrlimit(RLIMIT_FSIZE, &small);
            juce::String why;
            const bool written = Exporter::writeWavFile(wav, longer, 44100.0, 24, why);
            setrlimit(RLIMIT_FSIZE, &old);
            std::signal(SIGXFSZ, oldHandler);
            std::printf("   a write that fails half-way: %s (\"%s\"); the old file %lld -> %lld bytes\n", written ? "written" : "refused",
                        why.toRawUTF8(), static_cast<long long>(sizeBefore), static_cast<long long>(wav.getSize()));
            DAWLAB_CHECK(!written && wav.getSize() == sizeBefore);
            DAWLAB_CHECK(wav.getParentDirectory().getNumberOfChildFiles(juce::File::findFiles) == filesBefore); // no temporary left
        }
#endif
        const LoadedMedia media = MediaLoader::readAudioFile(wav, 48000.0);
        DAWLAB_CHECK(media.audio != nullptr);
        if (media.audio != nullptr) {
            const double amp2 = goertzelAmplitude(media.audio->getReadPointer(1) + 12000, n, 1000.0, 48000.0);
            std::printf("   file 44.1 kHz mono -> %d channels, %d samples at 48 kHz, 1 kHz amplitude %.4f\n",
                        media.audio->getNumChannels(), media.audio->getNumSamples(), amp2);
            DAWLAB_CHECK(media.audio->getNumChannels() == 2 && media.audio->getNumSamples() == 48000);
            DAWLAB_CHECK_NEAR(media.fileSampleRate, 44100.0, 1e-9);
            DAWLAB_CHECK_NEAR(20.0 * std::log10(amp2 / 0.5), 0.0, 0.05);
        }

        // Compressed formats: write the same sine as FLAC (lossless) and Ogg Vorbis (lossy) with
        // JUCE's writers, then read them back through the loader. MP3 and Opus have no writer
        // here, so only their registration is checked.
        auto writeWith = [&](juce::AudioFormat& format, const juce::String& fileName, int bits) {
            const juce::File f = wav.getSiblingFile(fileName);
            f.deleteFile();
            std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(f);
            const auto options = juce::AudioFormatWriterOptions{}
                                     .withSampleRate(44100.0)
                                     .withNumChannels(1)
                                     .withBitsPerSample(bits)
                                     .withQualityOptionIndex(format.getQualityOptions().size() - 1);
            std::unique_ptr<juce::AudioFormatWriter> writer = format.createWriterFor(stream, options);
            const float* ch[] = {sine.getReadPointer(0)};
            const bool ok = writer != nullptr && writer->writeFromFloatArrays(ch, 1, sine.getNumSamples());
            writer.reset(); // flushes and closes the file
            return ok ? f : juce::File();
        };
        juce::FlacAudioFormat flac;
        juce::OggVorbisAudioFormat ogg;
        const struct { juce::File file; double toleranceDb; } compressed[] = {
            {writeWith(flac, "l18_app_smoke_44k.flac", 24), 0.05},
            {writeWith(ogg, "l18_app_smoke_44k.ogg", 16), 0.5},
        };
        for (const auto& c : compressed) {
            DAWLAB_CHECK(c.file.existsAsFile());
            const LoadedMedia m = MediaLoader::readAudioFile(c.file, 48000.0);
            DAWLAB_CHECK(m.audio != nullptr);
            if (m.audio == nullptr) {
                std::printf("   %s: %s\n", c.file.getFileName().toRawUTF8(), m.error.toRawUTF8());
                continue;
            }
            const double a = goertzelAmplitude(m.audio->getReadPointer(0) + 12000, n, 1000.0, 48000.0);
            std::printf("   %s -> %d channels at 48 kHz, 1 kHz amplitude %.4f\n", c.file.getFileName().toRawUTF8(),
                        m.audio->getNumChannels(), a);
            DAWLAB_CHECK(m.audio->getNumChannels() == 2 && std::abs(m.audio->getNumSamples() - 48000) <= 64);
            DAWLAB_CHECK_NEAR(20.0 * std::log10(a / 0.5), 0.0, c.toleranceDb);
        }
        for (const char* name : {"a.wav", "a.aiff", "a.flac", "a.ogg", "a.opus", "a.mp3"})
            DAWLAB_CHECK(MediaLoader::canRead(juce::File::getCurrentWorkingDirectory().getChildFile(name)));
        DAWLAB_CHECK(!MediaLoader::canRead(juce::File::getCurrentWorkingDirectory().getChildFile("notes.txt")));
        std::printf("   readable formats: %s\n", MediaLoader::getWildcard().toRawUTF8());
        testAudioFileBoundary();
    }

    // 2. Engine + Session + the demo song (the GUI's code path)
    Engine engine;
    engine.prepare(sr, 512);
    Session session(engine);
    session.newProject();
    DAWLAB_CHECK(session.hasMasterLimiter());
    const DemoSong song = makeDemoSong(sr);
    const int unknown = session.applyDemoSong(song);
    engine.applyPendingCommandsOffline();
    int slotsUsed = 0;
    bool chainsMatch = true;
    for (int s = 0; s < session.getNumStrips(); ++s) {
        const ChannelRef ref = session.stripRefAt(s);
        slotsUsed += session.getNumUsedSlots(ref);
        chainsMatch = chainsMatch && engine.getNumInserts(ref) == session.getNumUsedSlots(ref);
        for (int slot = 0; slot < kInsertSlotsPerStrip; ++slot) {
            const auto& st = session.getSlot(ref, slot);
            if (!st.isEmpty())
                chainsMatch = chainsMatch && engine.getInsert(ref, session.chainIndexOf(ref, slot)) == st.effect;
        }
    }
    const int latency = engine.getLatencySamples();
    int numClips = 0;
    std::set<const AudioBuffer*> buffers; // clips share buffers (Step 14's waveform cache relies on it)
    for (int t = 0; t < session.getNumTracks(); ++t)
        for (const Clip& c : session.getClips(session.getTrackId(t))) {
            ++numClips;
            buffers.insert(c.source.get());
        }
    std::printf("2. demo song: %d clips on %zu distinct buffers, %d inserts in GUI slots, %d unknown parameter names,\n"
                "   mix latency %d samples\n",
                numClips, buffers.size(), slotsUsed, unknown, latency);
    DAWLAB_CHECK(numClips == 140);
    DAWLAB_CHECK(buffers.size() == 28);
    DAWLAB_CHECK(unknown == 0);
    DAWLAB_CHECK(slotsUsed == static_cast<int>(song.inserts.size()));
    DAWLAB_CHECK(chainsMatch);
    DAWLAB_CHECK(session.getSongEndSample() > 0);
    DAWLAB_CHECK(latency == engine.getMixer().getMaxTrackLatencySamples() + engine.getMixer().getMaxBusLatencySamples() +
                                engine.getMixer().getMaster().getInsertLatencySamples());

    // 3. Export 2 s (Exporter: the same code as File > Export mixdown)
    AudioBuffer exported;
    {
        ExportOptions options;
        options.file = juce::File::getCurrentWorkingDirectory().getChildFile("dawlab_output").getChildFile("l18_app_smoke.wav");
        options.numSamples = static_cast<int64_t>(2.0 * sr);
        options.tailSeconds = 0.0;
        const ExportResult r = Exporter::renderToFile(engine, options, &exported);
        const float rmsDb = gainToDb(std::max(rms(exported, 0), rms(exported, 1)));
        std::printf("3. export: %s, %.2f s, peak %.2f dBFS, RMS %.1f dBFS, %s\n", r.ok ? "ok" : r.error.toRawUTF8(),
                    r.seconds, static_cast<double>(r.peakDb), static_cast<double>(rmsDb),
                    options.file.getFullPathName().toRawUTF8());
        DAWLAB_CHECK(r.ok);
        DAWLAB_CHECK(options.file.existsAsFile());
        DAWLAB_CHECK(exported.getNumSamples() == static_cast<int>(2.0 * sr));
        DAWLAB_CHECK(!hasNanOrInf(exported));
        DAWLAB_CHECK(r.peakDb > -30.0f);
        DAWLAB_CHECK(r.peakDb <= -1.0f + 0.05f); // the master limiter's ceiling
        DAWLAB_CHECK(rmsDb > -45.0f);
    }

    // 4. The device-callback path: AudioHost wraps JUCE's float** without copying. Playing the
    //    song through it must give the bounce, shifted by the mix latency. (The last `latency`
    //    samples of the bounce were rendered with the transport already stopped - the renderer's
    //    tail - so we compare the first n - latency samples.)
    {
        AudioHost host(engine);
        for (const int blockSize : {512, 480}) {
            host.prepareOffline(sr, blockSize);
            Transport& tr = engine.getTransport();
            tr.stop();
            tr.setPosition(0);
            engine.requestReset();
            tr.play();
            const int n = exported.getNumSamples();
            const int total = n + latency;
            juce::AudioBuffer<float> device(3, total); // 3 device outputs: the third must stay silent
            device.clear();
            device.setSample(2, 0, 1.0f);              // garbage the host has to overwrite
            for (int done = 0; done < total; done += blockSize) {
                const int len = std::min(blockSize, total - done);
                float* channels[3] = {device.getWritePointer(0) + done, device.getWritePointer(1) + done,
                                      device.getWritePointer(2) + done};
                host.processOffline(channels, 3, len);
            }
            tr.stop();
            AudioBuffer played(2, n);
            for (int ch = 0; ch < 2; ++ch)
                std::copy_n(device.getReadPointer(ch) + latency, n, played.getWritePointer(ch));
            const float diff = maxAbsDiff(played, 0, exported, 0, n - latency);
            std::printf("4. device callback, %d-sample blocks, 3 outputs: max |callback - bounce| = %.3g, CPU load %.1f%%\n",
                        blockSize, static_cast<double>(diff), 100.0 * engine.getCpuMeter().load());
            DAWLAB_CHECK(diff < 1.0e-4f);
            DAWLAB_CHECK(device.getMagnitude(2, 0, total) == 0.0f);
        }
        DAWLAB_CHECK(host.isAnalyzerReady());
    }

    // 5. A hosted plugin as an insert (Step 17), with latency: PDC must hide it completely.
    {
        const AudioBuffer before = renderSeconds(engine, 1.0);
        const int latencyBefore = engine.getLatencySamples();
        constexpr int kPluginLatency = 100;
        auto hosted = std::make_unique<host::HostedPlugin>(createTestDelayPlugin(kPluginLatency));
        auto vst = std::make_unique<VstInsert>(std::move(hosted));
        const ChannelRef master = ChannelRef::master();
        InsertEffect* fx = session.setVstInsert(master, 3, std::move(vst));
        engine.applyPendingCommandsOffline();
        DAWLAB_CHECK(fx != nullptr);
        if (fx != nullptr) {
            DAWLAB_CHECK(session.getSlot(master, 3).kind == SlotKind::Vst3);
            DAWLAB_CHECK(fx->getNumParameters() == 1);
            DAWLAB_CHECK(std::string(fx->getParameterInfo(0).name) == "Gain");
            DAWLAB_CHECK_NEAR(fx->getParameter(0), 0.5, 1e-6);
            const int latencyAfter = engine.getLatencySamples();
            const AudioBuffer with = renderSeconds(engine, 1.0);
            const float diff = maxAbsDiff(before, 0, with, 0, before.getNumSamples());
            fx->setParameter(0, 0.25f); // gain 2 * 0.25 = 0.5 = -6.02 dB
            const AudioBuffer quieter = renderSeconds(engine, 1.0);
            const double stepDb = gainToDb(static_cast<double>(peakAbs(quieter))) - gainToDb(static_cast<double>(peakAbs(before)));
            std::printf("5. test plugin on the master (latency %d): mix latency %d -> %d, |with - without| = %.3g, "
                        "gain 0.25 -> %.2f dB, %lld process calls\n",
                        kPluginLatency, latencyBefore, latencyAfter, static_cast<double>(diff), stepDb,
                        getTestPluginProcessCount());
            DAWLAB_CHECK(latencyAfter == latencyBefore + kPluginLatency);
            DAWLAB_CHECK(diff < 1.0e-6f);
            DAWLAB_CHECK_NEAR(stepDb, -6.02, 0.05);
            DAWLAB_CHECK(getTestPluginProcessCount() > 0);
        }
        session.clearInsert(master, 3);
        engine.applyPendingCommandsOffline();
        engine.collectGarbage();
        DAWLAB_CHECK(getTestPluginInstanceCount() == 0); // freed on this (the UI) thread
        DAWLAB_CHECK(engine.getLatencySamples() == latencyBefore);
    }

    // 5a. The same plugin on a mono track (the drums): VstInsert runs the stereo plugin on L = R
    //     and folds the result back to one channel. At gain 0.5 (x1) the bounce must not move.
    {
        const ChannelRef drums = session.trackRef(session.getTrackId(0));
        DAWLAB_CHECK(session.getTrackFormat(drums.trackId) == TrackFormat::Mono);
        const AudioBuffer before = renderSeconds(engine, 1.0);
        auto hosted = std::make_unique<host::HostedPlugin>(createTestDelayPlugin(100));
        InsertEffect* fx = session.setVstInsert(drums, 3, std::make_unique<VstInsert>(std::move(hosted)));
        engine.applyPendingCommandsOffline();
        DAWLAB_CHECK(fx != nullptr);
        if (fx != nullptr) {
            const AudioBuffer with = renderSeconds(engine, 1.0);
            const float diff = maxAbsDiff(before, 0, with, 0, before.getNumSamples()); // both channels
            std::printf("5a. test plugin on the mono drum track: |with - without| = %.3g\n", static_cast<double>(diff));
            DAWLAB_CHECK(diff < 1.0e-6f);
        }
        session.clearInsert(drums, 3);
        engine.applyPendingCommandsOffline();
        engine.collectGarbage();
        DAWLAB_CHECK(getTestPluginInstanceCount() == 0);
    }

    // 5b. Real VST3 plugins: chapter 13's DawLab Compressor and DawLab Limiter, when chapter 13 was
    //     built in the same tree. The limiter reports a latency (its lookahead), so it repeats
    //     section 5's PDC test with a real plugin: on the track whose inserts already have the
    //     largest latency, the mix latency must grow by exactly what the plugin reports, and with
    //     its ceiling at 0 dB (a pure delay for this track) the bounce must not move.
    {
        host::PluginHost pluginHost;
        const ChannelRef drums = session.trackRef(session.getTrackId(0));
        const juce::File compressorFile = EffectMenu::chapter13CompressorFile();
        if (compressorFile == juce::File()) {
            std::printf("5b. chapter 13's DawLab Compressor not built in this tree: VST3 file test skipped\n");
        } else {
            juce::String error;
            auto instance = pluginHost.loadFromFile(compressorFile, sr, 512, error);
            DAWLAB_CHECK(instance != nullptr);
            if (instance != nullptr) {
                InsertEffect* fx = session.setVstInsert(
                    drums, 3, std::make_unique<VstInsert>(std::make_unique<host::HostedPlugin>(std::move(instance))));
                engine.applyPendingCommandsOffline();
                DAWLAB_CHECK(fx != nullptr);
                if (fx != nullptr) {
                    const AudioBuffer withPlugin = renderSeconds(engine, 1.0);
                    std::printf("5b. VST3 %s on the drums: %d parameters, latency %d, 1 s render peak %.2f dBFS\n",
                                fx->getName(), fx->getNumParameters(), fx->getLatencySamples(),
                                static_cast<double>(gainToDb(peakAbs(withPlugin))));
                    DAWLAB_CHECK(fx->getNumParameters() > 0);
                    DAWLAB_CHECK(!hasNanOrInf(withPlugin));
                    DAWLAB_CHECK(peakAbs(withPlugin) > 0.01f);
                }
                session.clearInsert(drums, 3);
                engine.applyPendingCommandsOffline();
                engine.collectGarbage(); // the plugin is deleted here, before pluginHost goes away
            } else {
                std::printf("5b. VST3 load failed: %s\n", error.toRawUTF8());
            }
        }

        const juce::File limiterFile = EffectMenu::chapter13LimiterFile();
        if (limiterFile == juce::File()) {
            std::printf("5b. chapter 13's DawLab Limiter not built in this tree: VST3 PDC test skipped\n");
        } else {
            // The track with the largest insert latency (in the demo song: track 8, convolution reverb).
            int track = 0;
            const auto insertLatency = [&](int lane) { return engine.getTrack(session.getTrackId(lane))->getInsertLatencySamples(); };
            for (int t = 1; t < session.getNumTracks(); ++t)
                if (insertLatency(t) > insertLatency(track))
                    track = t;
            const ChannelRef ref = session.trackRef(session.getTrackId(track));
            const int latencyBefore = engine.getLatencySamples();
            const AudioBuffer without = renderSeconds(engine, 1.0);
            juce::String error;
            auto instance = pluginHost.loadFromFile(limiterFile, sr, 512, error);
            DAWLAB_CHECK(instance != nullptr);
            if (instance != nullptr) {
                InsertEffect* fx = session.setVstInsert(
                    ref, 3, std::make_unique<VstInsert>(std::make_unique<host::HostedPlugin>(std::move(instance))));
                DAWLAB_CHECK(fx != nullptr);
                if (fx != nullptr) {
                    const int ceiling = fx->findParameter("Ceiling");
                    DAWLAB_CHECK(ceiling >= 0);
                    fx->setParameter(ceiling, 1.0f); // normalized 1 = 0 dB (range -24 .. 0 dB)
                    engine.applyPendingCommandsOffline();
                    const int pluginLatency = fx->getLatencySamples();
                    const int latencyAfter = engine.getLatencySamples();
                    const AudioBuffer with = renderSeconds(engine, 1.0);
                    const int n = without.getNumSamples();
                    const float diff = maxAbsDiff(with, 0, without, 0, n);
                    // What a missing compensation would sound like: the same bounce, pluginLatency off.
                    const int lag = std::clamp(pluginLatency, 0, n);
                    const float shifted = maxAbsDiff(with, lag, without, 0, n - lag);
                    std::printf("5b. VST3 %s on track %d (insert latency now %d): plugin latency %d, "
                                "mix latency %d -> %d,\n    |with - without| = %.3g (the two compared %d samples "
                                "apart: %.3g)\n",
                                fx->getName(), track + 1, insertLatency(track),
                                pluginLatency, latencyBefore, latencyAfter, static_cast<double>(diff), lag,
                                static_cast<double>(shifted));
                    DAWLAB_CHECK(pluginLatency > 0);
                    DAWLAB_CHECK(latencyAfter - latencyBefore == pluginLatency);
                    DAWLAB_CHECK(diff < 1.0e-6f);
                    DAWLAB_CHECK(shifted > 1.0e-3f);
                }
                session.clearInsert(ref, 3);
                engine.applyPendingCommandsOffline();
                engine.collectGarbage();
                DAWLAB_CHECK(engine.getLatencySamples() == latencyBefore);
            } else {
                std::printf("5b. VST3 load failed: %s\n", error.toRawUTF8());
            }
        }
    }

    // 6. The GUI, offline: strips, controls -> engine, commands and shortcuts.
    {
        DAWLAB_CHECK_NEAR(FaderLaw::dbToPosition(0.0f), 0.5918, 1e-3);
        DAWLAB_CHECK_NEAR(FaderLaw::positionToDb(FaderLaw::dbToPosition(-12.0f)), -12.0, 1e-4);
        MainComponent main(MainComponent::Mode::Offline);
        DAWLAB_CHECK(main.getMixer().getNumStrips() == kNewProjectTracks + kNumBuses + 1);
        DAWLAB_CHECK(main.getMixer().getStrip(0).getNumSendKnobs() == kNumBuses);
        DAWLAB_CHECK(main.loadDemoSongNow() == 0);
        Engine& e = main.getEngine();
        Session& s6 = main.getSession();
        e.applyPendingCommandsOffline();
        main.tick(); // delivers the session's change messages: the views follow the new tracks
        const TrackId id0 = s6.getTrackId(0), id1 = s6.getTrackId(1);

        // drag and drop from the file browser -> the lane under the mouse, readable files only
        {
            main.setSize(MainComponent::kDefaultWidth, MainComponent::kDefaultHeight);
            auto& arrangement = main.getArrangement();
            const auto lane = arrangement.getLaneBounds(2).getCentre();
            const juce::StringArray dragged{"C:/music/kick.flac", "C:/music/readme.txt", "C:/music/bass.mp3"};
            TrackId droppedTrack;
            juce::StringArray droppedFiles;
            auto realHandler = arrangement.onFilesDropped;
            arrangement.onFilesDropped = [&](TrackId t, const juce::StringArray& f) {
                droppedTrack = t;
                droppedFiles = f;
            };
            DAWLAB_CHECK(arrangement.isInterestedInFileDrag(dragged));
            DAWLAB_CHECK(!arrangement.isInterestedInFileDrag(juce::StringArray{"C:/music/readme.txt"}));
            arrangement.fileDragEnter(dragged, lane.x, lane.y);
            DAWLAB_CHECK(arrangement.getDropTargetTrack() == s6.getTrackId(2));
            arrangement.filesDropped(dragged, lane.x, lane.y);
            DAWLAB_CHECK(!arrangement.getDropTargetTrack().isValid());
            DAWLAB_CHECK(droppedTrack == s6.getTrackId(2));
            DAWLAB_CHECK(droppedFiles.size() == 2 && droppedFiles[0].endsWith("kick.flac") &&
                         droppedFiles[1].endsWith("bass.mp3"));
            arrangement.onFilesDropped = realHandler;

            // the strip's format button toggles mono / stereo, and the pan mode follows
            const TrackFormat before = s6.getTrackFormat(id1);
            main.getMixer().getStrip(1).getFormatButton().onClick();
            const Track& t1 = *e.getTrack(id1);
            DAWLAB_CHECK(s6.getTrackFormat(id1) != before);
            DAWLAB_CHECK(t1.getPanMode() == (t1.getFormat() == TrackFormat::Mono ? PanMode::PanLaw : PanMode::Balance));
            main.getMixer().getStrip(1).getFormatButton().onClick();
            DAWLAB_CHECK(s6.getTrackFormat(id1) == before);
            std::printf("   format button: track 2 %s -> %s -> %s\n", trackFormatName(before),
                        trackFormatName(before == TrackFormat::Mono ? TrackFormat::Stereo : TrackFormat::Mono),
                        trackFormatName(s6.getTrackFormat(id1)));
            DAWLAB_CHECK(arrangement.onFilesDropped != nullptr); // MainComponent wired it up
            std::printf("   drag and drop: %d of 3 dragged files accepted, dropped on track %d\n", droppedFiles.size(),
                        s6.indexOf(droppedTrack) + 1);
        }

        // fader -> ChannelStrip::setGainDb
        auto& strip0 = main.getMixer().getStrip(0);
        strip0.getFader().setValue(FaderLaw::dbToPosition(-6.0f), juce::sendNotificationSync);
        DAWLAB_CHECK_NEAR(e.getTrack(id0)->getGainDb(), -6.0, 0.01);
        // A click on S: Button::triggerClick() would be asynchronous, so do what it does directly.
        strip0.getSoloButton().setToggleState(true, juce::dontSendNotification);
        strip0.getSoloButton().onClick();
        DAWLAB_CHECK(e.getTrack(id0)->isSoloed());
        e.getTrack(id0)->setSolo(false);

        // auto-generated panel -> InsertEffect::setParameter
        InsertEffect* comp = s6.getSlot(s6.trackRef(id0), 1).effect;
        DAWLAB_CHECK(comp != nullptr && std::string(comp->getName()) == "Compressor");
        if (comp != nullptr) {
            ParameterPanel panel(*comp);
            DAWLAB_CHECK(panel.getNumControls() == comp->getNumParameters());
            DAWLAB_CHECK(panel.getSlider(0) != nullptr);
            if (auto* threshold = panel.getSlider(0)) {
                DAWLAB_CHECK_NEAR(threshold->getValue(), -20.0, 1e-4);
                threshold->setValue(-30.0, juce::sendNotificationSync);
                DAWLAB_CHECK_NEAR(comp->getParameter(0), -30.0, 1e-4);
                DAWLAB_CHECK(threshold->getTextFromValue(-30.0) == ParameterPanel::formatValue(*comp, 0, -30.0f));
            }
            DAWLAB_CHECK(panel.getToggle(comp->findParameter("Stereo Link")) != nullptr ||
                         panel.getComboBox(comp->findParameter("Stereo Link")) != nullptr);
        }

        // commands: the menu / keyboard path
        auto& cm = main.getCommandManager();
        const bool wasPlaying = e.getTransport().isPlaying();
        cm.invokeDirectly(CommandIDs::playStop, false);
        DAWLAB_CHECK(e.getTransport().isPlaying() != wasPlaying);
        cm.getKeyMappings()->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey), &main);
        DAWLAB_CHECK(e.getTransport().isPlaying() == wasPlaying);
        cm.getKeyMappings()->keyPressed(juce::KeyPress('l'), &main);
        DAWLAB_CHECK(e.getTransport().isLoopEnabled());

        // Play through the simulated callback, then one UI tick. The displays show what is heard
        // (Steps 13, 14): the transport renders the mix latency ahead of the speakers, so after
        // 24000 + latency samples the listener is exactly at beat 2 (no device: device latency 0).
        e.getTransport().setLoopEnabled(false);
        e.getTransport().setPosition(0);
        e.getTransport().play();
        const int mixLatency = e.getLatencySamples();
        const int64_t beat2 = e.getTransport().barsBeatsToSamples({1, 2, 0}); // 24000
        main.simulatePlayback(beat2 + mixLatency);
        main.tick();
        const int64_t rendered = e.getTransport().getPosition();
        const int64_t playhead = main.getArrangement().getPlayheadPosition();
        std::printf("6. GUI offline: %d strips, position %s (playhead %lld, transport %lld = + mix latency %d),\n"
                    "   master peak %.1f dBFS, status \"%s\"\n",
                    main.getMixer().getNumStrips(), main.getTransportBar().getPositionText().toRawUTF8(),
                    static_cast<long long>(playhead), static_cast<long long>(rendered), mixLatency,
                    static_cast<double>(main.getTransportBar().getMasterMeter().getPeakDb(0)), main.getStatus().toRawUTF8());
        DAWLAB_CHECK(main.getTransportBar().getPositionText() == "1.2.000");
        DAWLAB_CHECK(main.getTransportBar().getMasterMeter().getPeakDb(0) > -40.0f);
        // Playhead latency compensation: shown = max(0, transport - mix latency - device latency).
        DAWLAB_CHECK(mixLatency > 0 && rendered == beat2 + mixLatency);
        DAWLAB_CHECK(playhead == beat2 && playhead < rendered);
        DAWLAB_CHECK(main.getAudioHost().getDeviceOutputLatencySamples() == 0);
        DAWLAB_CHECK(AudioHost::compensatePosition(24752, true, 752, 256) == 23744);  // a device adds its own
        DAWLAB_CHECK(AudioHost::compensatePosition(500, true, 752, 256) == 0);        // never before the start
        DAWLAB_CHECK(AudioHost::compensatePosition(24752, false, 752, 256) == 24752); // stopped: nothing sounds
        e.getTransport().stop();
        main.tick();
        DAWLAB_CHECK(main.getTransportBar().getPositionText() == "1.2.030"); // stopped: 24752 = beat 2 + 30 ticks

        // 6b. Tracks come and go through the GUI (chapter 15, Step 7).
        auto& arrangement = main.getArrangement();
        auto& mixer = main.getMixer();
        const int before = s6.getNumTracks();
        arrangement.selectLane(1); // the bass
        cm.invokeDirectly(CommandIDs::addStereoTrack, false);
        const TrackId added = arrangement.getSelectedTrack();
        const bool placed = s6.indexOf(added) == 2 && s6.getTrackFormat(added) == TrackFormat::Stereo &&
                            e.getTrack(added) != nullptr && added.value > id0.value;
        for (int i = 0; i < 40; ++i)
            cm.invokeDirectly(CommandIDs::addMonoTrack, false);
        main.tick();
        const TrackId last = arrangement.getSelectedTrack();
        const auto lane = arrangement.getLaneBounds(s6.indexOf(last));
        const bool laneVisible = lane.getY() >= ArrangementView::kRulerHeight && lane.getBottom() <= arrangement.getHeight();
        const ChannelStripView* lastStrip = mixer.getTrackStrip(last);
        const auto& viewport = mixer.getTrackViewport();
        const bool stripVisible = lastStrip != nullptr && lastStrip->getX() >= viewport.getViewPositionX() &&
                                  lastStrip->getRight() <= viewport.getViewPositionX() + viewport.getViewWidth();
        e.getTransport().setPosition(0);
        e.getTransport().play();
        main.simulatePlayback(24000);
        main.tick();
        e.getTransport().stop();
        const float peakWithMany = main.getTransportBar().getMasterMeter().getPeakDb(0);
        std::printf("6b. tracks: %d + 1 stereo (at lane %d) + 40 mono = %d tracks, %d strips in the mixer, %d in the engine;\n"
                    "    arrangement scrolls %d px, newest lane visible: %s, its strip visible: %s, master peak %.1f dBFS\n",
                    before, s6.indexOf(added) + 1, s6.getNumTracks(), mixer.getNumTracks(), e.getNumTracks(),
                    arrangement.getMaxScrollY(), laneVisible ? "yes" : "no", stripVisible ? "yes" : "no",
                    static_cast<double>(peakWithMany));
        DAWLAB_CHECK(placed);
        DAWLAB_CHECK(s6.getNumTracks() == before + 41 && mixer.getNumTracks() == before + 41 && e.getNumTracks() == before + 41);
        DAWLAB_CHECK(mixer.getNumStrips() == s6.getNumStrips());
        DAWLAB_CHECK(arrangement.getMaxScrollY() > 0 && laneVisible && stripVisible);
        DAWLAB_CHECK(peakWithMany > -40.0f);

        // Delete the drums while the editor of their compressor is open: the window closes first,
        // the bass takes the selection, everything the drums owned comes back as garbage.
        const ChannelRef drums = s6.trackRef(id0);
        InsertEffect* drumComp = s6.getSlot(drums, 1).effect;
        main.openInsertEditor(drums, 1, false);
        const bool editorOpen = main.getParameterWindow(drumComp) != nullptr;
        arrangement.setSelectedTrack(id0);
        cm.invokeDirectly(CommandIDs::deleteSelectedTrack, false);
        const bool editorClosed = main.getParameterWindow(drumComp) == nullptr;
        main.simulatePlayback(4096); // the audio thread applies RemoveTrack and hands the inserts back
        main.tick();                 // ... and the UI tick frees them
        std::printf("    delete the drums: editor open %s -> closed %s, selection -> %s, %d tracks, garbage pending %zu, "
                    "leaked %llu\n",
                    editorOpen ? "yes" : "no", editorClosed ? "yes" : "no",
                    s6.getStripName(s6.trackRef(arrangement.getSelectedTrack())).toRawUTF8(), s6.getNumTracks(),
                    e.getPendingGarbage(), static_cast<unsigned long long>(e.getGarbageLeaked()));
        DAWLAB_CHECK(editorOpen && editorClosed);
        DAWLAB_CHECK(!s6.hasTrack(id0) && e.getTrack(id0) == nullptr && mixer.getTrackStrip(id0) == nullptr);
        DAWLAB_CHECK(arrangement.getSelectedTrack() == id1);
        DAWLAB_CHECK(e.getPendingGarbage() == 0 && e.getGarbageLeaked() == 0);

        // Fill the pool: the last add is refused with a message, and the command says so too.
        int refusedAt = -1;
        while (refusedAt < 0) {
            if (!main.addTrack(TrackFormat::Mono).isValid())
                refusedAt = s6.getNumTracks();
            e.applyPendingCommandsOffline(); // no audio thread here: keep the queue short
        }
        juce::ApplicationCommandInfo addInfo(CommandIDs::addMonoTrack);
        main.getCommandInfo(CommandIDs::addMonoTrack, addInfo);
        const bool addDisabled = (addInfo.flags & juce::ApplicationCommandInfo::isDisabled) != 0;
        std::printf("    pool: full at %d tracks (kMaxTracks %d), add command disabled: %s, status \"%s\"\n", refusedAt,
                    kMaxTracks, addDisabled ? "yes" : "no", main.getStatus().toRawUTF8());
        DAWLAB_CHECK(refusedAt == kMaxTracks && addDisabled);
        DAWLAB_CHECK(main.getStatus().contains("limit"));

        // File > New: back to kNewProjectTracks, with new ids (ids only count up). Files dropped
        // on the last lane add the tracks they need.
        TrackId highest;
        for (int t = 0; t < s6.getNumTracks(); ++t)
            highest.value = std::max(highest.value, s6.getTrackId(t).value);
        cm.invokeDirectly(CommandIDs::newProject, false);
        e.applyPendingCommandsOffline();
        main.tick();
        const bool fresh = s6.getNumTracks() == kNewProjectTracks && s6.getTrackId(0).value > highest.value;
        main.loadDroppedFiles(s6.getTrackId(kNewProjectTracks - 1),
                              juce::StringArray{"C:/missing/a.wav", "C:/missing/b.wav", "C:/missing/c.wav"});
        std::printf("    File > New: %d tracks, first id %u (above %u); 3 files dropped on the last lane -> %d tracks\n",
                    kNewProjectTracks, s6.getTrackId(0).value, highest.value, s6.getNumTracks());
        DAWLAB_CHECK(fresh);
        DAWLAB_CHECK(s6.getNumTracks() == kNewProjectTracks + 2);
        e.applyPendingCommandsOffline();
        e.collectGarbage();
        DAWLAB_CHECK(e.getGarbageLeaked() == 0 && e.getCommandsDropped() == 0);

        // 6c. The CPU warning (chapter 15, Step 7): an insert that needs 2.5 times the duration of
        //     every block makes every block late. The display turns red and the status line says
        //     what to do. (The insert goes straight to the engine; Session does not need to know.)
        const ChannelRef busyTrack = s6.trackRef(s6.getTrackId(0));
        const double blockSeconds = MainComponent::kOfflineBlockSize / e.getSampleRate();
        e.addInsert(busyTrack, -1, std::make_unique<BusyEffect>(2.5 * blockSeconds));
        const std::uint32_t lateBefore = e.getCpuMeter().overruns();
        e.getTransport().play();
        main.simulatePlayback(4 * MainComponent::kOfflineBlockSize);
        e.getTransport().stop();
        main.tick();
        const CpuStatus cpu = main.getTransportBar().getCpuStatus();
        std::printf("6c. a 2.5x-too-slow insert: load %.0f %%, %u late blocks, CPU display \"%s\", status \"%s...\"\n",
                    100.0 * cpu.load, e.getCpuMeter().overruns() - lateBefore, main.getTransportBar().getCpuText().toRawUTF8(),
                    main.getStatus().substring(0, 40).toRawUTF8());
        DAWLAB_CHECK(cpu.state == CpuState::Overload && e.getCpuMeter().overruns() - lateBefore == 4);
        DAWLAB_CHECK(main.getTransportBar().getCpuText() == "CPU overload");
        DAWLAB_CHECK(main.getStatus().startsWith("Audio dropouts"));
        e.removeInsert(busyTrack, e.getNumInserts(busyTrack) - 1);
        e.applyPendingCommandsOffline();
        e.collectGarbage();
    }

    // 6d. Save and open (chapter 15, Step 8): the demo song with changes it does not have is saved,
    //     opened in a second window, rendered by both, and saved again from the second one.
    {
        const juce::File dir = juce::File::getCurrentWorkingDirectory().getChildFile("dawlab_output").getChildFile("l18_project");
        dir.deleteRecursively();
        dir.createDirectory();
        const juce::File file = dir.getChildFile("Demo.dawlab");

        // On the heap: a MainComponent holds an engine with kMaxTracks track slots (megabytes),
        // and a Debug build reserves the stack for every block of this function at once.
        auto aOwner = std::make_unique<MainComponent>(MainComponent::Mode::Offline);
        MainComponent& a = *aOwner;
        DAWLAB_CHECK(a.loadDemoSongNow() == 0);
        Session& sa = a.getSession();
        Engine& ea = a.getEngine();
        const TrackId bass = sa.getTrackId(1), lead = sa.getTrackId(5);
        sa.setStripName(sa.trackRef(bass), "Bass \"DI\"");
        ea.getTrack(lead)->setMute(true);
        ea.getTrack(bass)->setSendLevelDb(0, -12.5f);
        ea.getTrack(bass)->setSendPreFader(0, true);
        sa.setInsertBypassed(sa.trackRef(sa.getTrackId(0)), 0, true);
        ea.getTransport().setTempo(118.0);
        ea.getTransport().setLoopRangeBars(2, 5);
        ea.getTransport().setLoopEnabled(true);
        a.getArrangement().selectLane(7);
        a.addTrack(TrackFormat::Stereo); // an empty track at the end
        const bool saved = a.saveProject(file);
        const juce::String text1 = file.loadFileAsString();
        const int wavs = ProjectIO::mediaFolderFor(file).getNumberOfChildFiles(juce::File::findFiles, "*.wav");
        // The files are named by content: buffers with equal samples are stored once.
        const int distinct = static_cast<int>(ProjectIO::capture(sa, "x").media.size());
        const auto wavTime = ProjectIO::mediaFolderFor(file).findChildFiles(juce::File::findFiles, false, "*.wav")[0].getLastModificationTime();
        DAWLAB_CHECK(a.saveProject(file)); // unchanged audio is not written again
        const bool wavsKept = ProjectIO::mediaFolderFor(file).findChildFiles(juce::File::findFiles, false, "*.wav")[0].getLastModificationTime() == wavTime;
        ea.getTransport().setLoopEnabled(false); // render the first 2 s straight through, in both
        ea.applyPendingCommandsOffline();
        const AudioBuffer before = renderSeconds(ea, 2.0);

        auto bOwner = std::make_unique<MainComponent>(MainComponent::Mode::Offline);
        MainComponent& b = *bOwner;
        b.addTrack(TrackFormat::Mono); // this window's next ids are now not the ones `a` had
        const ProjectIO::OpenResult opened = b.openProject(file);
        Session& sb = b.getSession();
        Engine& eb = b.getEngine();
        const bool loopBack = eb.getTransport().isLoopEnabled() && eb.getTransport().getTempo() == 118.0;
        eb.getTransport().setLoopEnabled(false);
        eb.applyPendingCommandsOffline();
        const AudioBuffer after = renderSeconds(eb, 2.0);
        const float diff = maxAbsDiff(before, 0, after, 0, before.getNumSamples());
        eb.getTransport().setLoopEnabled(true);
        const juce::File copy = dir.getChildFile("copy").getChildFile("Demo.dawlab");
        copy.getParentDirectory().createDirectory();
        const bool savedAgain = b.saveProject(copy);
        const bool sameText = copy.loadFileAsString() == text1;
        std::printf("6d. save: %s, %d lines, %d WAVs for 28 buffers with %d different contents (a second save\n"
                    "    rewrote none: %s); open: %s, %d tracks, %d warnings,\n"
                    "    format %d, title \"%s\"; 2 s render of both: max |diff| = %.3g; saved again elsewhere: same text %s\n",
                    saved ? "ok" : "FAILED", juce::StringArray::fromLines(text1).size(), wavs, distinct, wavsKept ? "yes" : "no",
                    opened.ok ? "ok" : opened.error.toRawUTF8(), sb.getNumTracks(), opened.warnings.size(),
                    opened.formatVersion, b.getWindowTitle().toRawUTF8(), static_cast<double>(diff), sameText ? "yes" : "no");
        DAWLAB_CHECK(saved && savedAgain && opened.ok && opened.warnings.isEmpty());
        DAWLAB_CHECK(wavs == distinct && distinct > 0 && distinct <= 28 && opened.audioFiles == wavs && wavsKept);
        DAWLAB_CHECK(sb.getNumTracks() == sa.getNumTracks() && sb.getNumTracks() == kDemoTracks + 1);
        DAWLAB_CHECK(sb.getStripName(sb.trackRef(sb.getTrackId(1))) == "Bass \"DI\"");
        DAWLAB_CHECK(sb.getTrackFormat(sb.getTrackId(8)) == TrackFormat::Stereo && sb.getClips(sb.getTrackId(8)).empty());
        DAWLAB_CHECK(loopBack);
        DAWLAB_CHECK(diff == 0.0f && peakAbs(after) > 0.01f);
        DAWLAB_CHECK(sameText);
        DAWLAB_CHECK(b.getProjectFile() == copy && b.getWindowTitle() == "DawLab 18 - Demo");

        // A broken file is refused with its line, and the open project stays as it was.
        const juce::File broken = dir.getChildFile("broken.dawlab");
        broken.replaceWithText("DawLabProject 3\nsampleRate 48000\ntrack 1 \"A\" 0 0 mono 0 0\nclip 2 0 10 \"x.wav\"\n");
        const int tracksBefore = sb.getNumTracks();
        const ProjectIO::OpenResult refused = b.openProject(broken);
        std::printf("    broken file: %s; still %d tracks, status \"%s\"\n", refused.error.toRawUTF8(), sb.getNumTracks(),
                    b.getStatus().toRawUTF8());
        DAWLAB_CHECK(!refused.ok && refused.error.contains("line 4"));
        DAWLAB_CHECK(sb.getNumTracks() == tracksBefore && sb.getStripName(sb.trackRef(sb.getTrackId(1))) == "Bass \"DI\"");
        DAWLAB_CHECK(b.getStatus().startsWith("Not opened"));

        // Without its audio folder the project still opens: no clips, one warning per file.
        const juce::File alone = dir.getChildFile("alone").getChildFile("Demo.dawlab");
        alone.getParentDirectory().createDirectory();
        file.copyFileTo(alone);
        const ProjectIO::OpenResult partial = b.openProject(alone);
        std::printf("    without its audio: %s, %d tracks, %d clips, %d warnings (\"%s\")\n", partial.ok ? "opened" : "refused",
                    sb.getNumTracks(), static_cast<int>(sb.getClipSources().size()), partial.warnings.size(),
                    partial.warnings[0].toRawUTF8());
        DAWLAB_CHECK(partial.ok && partial.warnings.size() == distinct && sb.getClipSources().empty());
        // What the open left out is still in the file: Save never writes over it (that would lose it, and delete
        // audio that is there but does not read). Under another name it is saved, and that file is complete.
        const juce::String aloneText = alone.loadFileAsString();
        const bool overwritten = b.saveProject(alone);
        const juce::String refusedStatus = b.getStatus();
        const juce::File elsewhere = alone.getSiblingFile("Elsewhere.dawlab");
        const bool savedElsewhere = b.saveProject(elsewhere) && b.saveProject(elsewhere);
        std::printf("    saved over itself: %s (\"%s\"); under another name, twice: %s\n", overwritten ? "yes" : "refused",
                    refusedStatus.toRawUTF8(), savedElsewhere ? "yes" : "no");
        DAWLAB_CHECK(!overwritten && alone.loadFileAsString() == aloneText && refusedStatus.startsWith("Not saved"));
        DAWLAB_CHECK(savedElsewhere && b.getProjectFile() == elsewhere);

        // A NaN in one of the project's WAVs (another program wrote it) is set to 0 and noted. That loses nothing
        // worth keeping, so it is a note, not a warning: Save may write over the file.
        const juce::File nanProject = dir.getChildFile("nan").getChildFile("Demo.dawlab");
        nanProject.getParentDirectory().createDirectory();
        file.copyFileTo(nanProject);
        ProjectIO::mediaFolderFor(file).copyDirectoryTo(ProjectIO::mediaFolderFor(nanProject));
        {
            const juce::File wav = ProjectIO::mediaFolderFor(nanProject).findChildFiles(juce::File::findFiles, false, "*.wav")[0];
            juce::MemoryBlock bytes;
            wav.loadFileAsData(bytes);
            auto* data = static_cast<unsigned char*>(bytes.getData());
            for (size_t i = 0; i + 8 + 404 <= bytes.getSize(); ++i) {
                if (std::memcmp(data + i, "data", 4) == 0) {
                    const unsigned char nan[4] = {0x00, 0x00, 0xc0, 0x7f}; // a quiet NaN as a little-endian float
                    std::memcpy(data + i + 8 + 400, nan, 4);
                    break;
                }
            }
            wav.replaceWithData(bytes.getData(), bytes.getSize());
        }
        const ProjectIO::OpenResult withNan = b.openProject(nanProject);
        const juce::String nanStatus = b.getStatus();
        bool allFinite = true;
        for (const auto& source : sb.getClipSources())
            allFinite = allFinite && !hasNanOrInf(*source);
        const bool savedOverNan = b.saveProject(nanProject);
        std::printf("    a NaN in a WAV: %s, %d warnings, %d notes (\"%s\"), clips %s; saved over itself: %s\n",
                    withNan.ok ? "opened" : withNan.error.toRawUTF8(), withNan.warnings.size(), withNan.notes.size(),
                    withNan.notes[0].toRawUTF8(), allFinite ? "all finite" : "hold NaN or Inf", savedOverNan ? "yes" : "refused");
        DAWLAB_CHECK(withNan.ok && withNan.warnings.isEmpty() && withNan.notes.size() == 1);
        DAWLAB_CHECK(withNan.notes[0].contains("NaN") && nanStatus.contains(withNan.notes[0]));
        DAWLAB_CHECK(allFinite && savedOverNan);
        DAWLAB_CHECK(sb.getNumTracks() == kDemoTracks + 1);

        // Opened by an engine at 44.1 kHz: positions and audio are converted.
        auto cOwner = std::make_unique<MainComponent>(MainComponent::Mode::Offline);
        MainComponent& c = *cOwner;
        c.getAudioHost().prepareOffline(44100.0, 512);
        const ProjectIO::OpenResult at44 = c.openProject(file);
        Session& sc = c.getSession();
        const auto& bassClips = sc.getClips(sc.getTrackId(1));
        const bool converted = bassClips.size() > 2 && bassClips[2].start == 88200 && sc.getClipSampleRate() == 44100.0;
        c.getEngine().applyPendingCommandsOffline();
        const AudioBuffer at44Render = renderSeconds(c.getEngine(), 1.0);
        std::printf("    opened at 44.1 kHz: %s, bass clip 3 at %lld (88200 expected), peak %.1f dBFS\n",
                    at44.ok ? "ok" : at44.error.toRawUTF8(), bassClips.size() > 2 ? static_cast<long long>(bassClips[2].start) : -1LL,
                    static_cast<double>(gainToDb(peakAbs(at44Render))));
        DAWLAB_CHECK(at44.ok && converted);
        // The conversion that opening did is no change of its own; one after an edit forgets the entries (they are
        // at the old rate), not that the project has unsaved changes.
        const bool openedUnmodified = !sc.getHistory().isModified();
        const StripTarget firstStrip = StripTarget::ofTrack(sc.getTrackId(0));
        sc.perform(edit::Rename{firstStrip, sc.getStripName(sc.trackRef(sc.getTrackId(0)))}, "Rename");
        sc.applySampleRateConversion(48000.0, ConvertedBuffers{});
        DAWLAB_CHECK(openedUnmodified && sc.getHistory().isModified() && !sc.getHistory().canUndo());
        sc.applySampleRateConversion(44100.0, ConvertedBuffers{});
        DAWLAB_CHECK(!hasNanOrInf(at44Render) && peakAbs(at44Render) > 0.01f);
        for (MainComponent* m : {&a, &b, &c}) {
            m->getEngine().applyPendingCommandsOffline();
            m->getEngine().collectGarbage();
            DAWLAB_CHECK(m->getEngine().getGarbageLeaked() == 0 && m->getEngine().getCommandsDropped() == 0);
        }
    }

    // 6e. Undo and redo (chapter 15, Step 9).
    {
        auto mOwner = std::make_unique<MainComponent>(MainComponent::Mode::Offline);
        MainComponent& m = *mOwner;
        DAWLAB_CHECK(m.loadDemoSongNow() == 0);
        Session& s = m.getSession();
        Engine& e = m.getEngine();
        auto& cm = m.getCommandManager();
        // The whole project as text (Step 8's format): equal text = equal project.
        const auto stateText = [](Session& session) {
            return juce::String(project::writeProject(ProjectIO::capture(session, "m").state));
        };
        const auto settle = [](Engine& engine) { // what the audio thread and the UI tick would do
            engine.applyPendingCommandsOffline();
            engine.collectGarbage();
        };
        settle(e);
        const juce::String start = stateText(s);
        const bool cleanAtStart = !s.getHistory().canUndo() && !m.getWindowTitle().endsWith("*");

        // Delete the bass through the command; undo brings it back with its id, clips and inserts.
        const TrackId bass = s.getTrackId(1);
        const size_t bassClips = s.getClips(bass).size();
        const int bassInserts = s.getNumUsedSlots(s.trackRef(bass));
        m.getArrangement().setSelectedTrack(bass);
        cm.invokeDirectly(CommandIDs::deleteSelectedTrack, false);
        settle(e);
        juce::ApplicationCommandInfo undoInfo(CommandIDs::undo);
        m.getCommandInfo(CommandIDs::undo, undoInfo);
        const bool deleted = !s.hasTrack(bass) && m.getWindowTitle().endsWith(" *");
        cm.invokeDirectly(CommandIDs::undo, false);
        settle(e);
        const bool back = s.indexOf(bass) == 1 && s.getClips(bass).size() == bassClips &&
                          s.getNumUsedSlots(s.trackRef(bass)) == bassInserts && stateText(s) == start &&
                          !m.getWindowTitle().endsWith("*");
        cm.invokeDirectly(CommandIDs::redo, false);
        settle(e);
        const bool redone = !s.hasTrack(bass);
        cm.invokeDirectly(CommandIDs::undo, false);
        settle(e);
        std::printf("6e. undo: \"%s\"; deleted %s, back with id %u, %zu clips, %d inserts: %s, project as before: %s; redo %s\n",
                    undoInfo.shortName.toRawUTF8(), deleted ? "yes" : "no", bass.value, s.getClips(bass).size(),
                    s.getNumUsedSlots(s.trackRef(bass)), back ? "yes" : "no", stateText(s) == start ? "yes" : "no",
                    redone ? "works" : "FAILED");
        DAWLAB_CHECK(cleanAtStart && deleted && back && redone);
        DAWLAB_CHECK(undoInfo.shortName == "Undo Delete track Bass");

        // Two fader drags are two undo steps, however many moves each has.
        m.tick();
        ChannelStripView* bassStrip = m.getMixer().getTrackStrip(bass);
        DAWLAB_CHECK(bassStrip != nullptr);
        if (bassStrip != nullptr) {
            juce::Slider& fader = bassStrip->getFader();
            const float gain0 = e.getTrack(bass)->getGainDb();
            const size_t entries0 = s.getHistory().undoCount();
            const auto drag = [&fader](std::initializer_list<float> dbs) { // what a mouse drag does
                if (fader.onDragStart)
                    fader.onDragStart();
                for (float db : dbs)
                    fader.setValue(FaderLaw::dbToPosition(db), juce::sendNotificationSync);
                if (fader.onDragEnd)
                    fader.onDragEnd();
            };
            drag({-10.0f, -12.0f, -14.0f});
            drag({-3.0f, -1.0f});
            const size_t entries = s.getHistory().undoCount() - entries0;
            m.undo();
            const float afterOne = e.getTrack(bass)->getGainDb();
            m.undo();
            const float afterTwo = e.getTrack(bass)->getGainDb();
            std::printf("    two fader drags (3 and 2 moves): %zu undo entries; undo -> %.1f dB, undo -> %.1f dB (was %.1f)\n",
                        entries, static_cast<double>(afterOne), static_cast<double>(afterTwo), static_cast<double>(gain0));
            DAWLAB_CHECK(entries == 2);
            DAWLAB_CHECK_NEAR(afterOne, -14.0, 0.05);
            DAWLAB_CHECK(afterTwo == gain0);
        }

        // A knob in an effect's panel.
        const ChannelRef drums = s.trackRef(s.getTrackId(0));
        InsertEffect* comp = s.getSlot(drums, 1).effect;
        m.openInsertEditor(drums, 1, false);
        ParameterWindow* window = m.getParameterWindow(comp);
        DAWLAB_CHECK(window != nullptr && window->getPanel().getSlider(0) != nullptr);
        if (window != nullptr && window->getPanel().getSlider(0) != nullptr) {
            const float threshold0 = comp->getParameter(0);
            window->getPanel().getSlider(0)->setValue(-35.0, juce::sendNotificationSync);
            const float changed = comp->getParameter(0);
            m.undo();
            std::printf("    compressor threshold in its panel: %.1f -> %.1f, undo -> %.1f\n", static_cast<double>(threshold0),
                        static_cast<double>(changed), static_cast<double>(comp->getParameter(0)));
            DAWLAB_CHECK(changed == -35.0f && comp->getParameter(0) == threshold0);
        }

        // A choice in a list and a click on a switch have no drag: each is a step of its own. Two in a row of the
        // same parameter, and one undo takes back only the second.
        {
            const ChannelRef bassRef = s.trackRef(bass);
            InsertSnapshot saturation;
            saturation.kind = SlotKind::Builtin;
            saturation.type = EffectType::Saturation;
            InsertSnapshot eq = saturation;
            eq.type = EffectType::ParametricEq;
            DAWLAB_CHECK(s.perform(edit::SetInsert{StripTarget::ofTrack(bass), 2, saturation}, "Insert Saturation"));
            DAWLAB_CHECK(s.perform(edit::SetInsert{StripTarget::ofTrack(bass), 3, eq}, "Insert EQ"));
            InsertEffect* satFx = s.getSlot(bassRef, 2).effect;
            InsertEffect* eqFx = s.getSlot(bassRef, 3).effect;
            m.openInsertEditor(bassRef, 2, false);
            m.openInsertEditor(bassRef, 3, false);
            ParameterWindow* satWindow = satFx != nullptr ? m.getParameterWindow(satFx) : nullptr;
            ParameterWindow* eqWindow = eqFx != nullptr ? m.getParameterWindow(eqFx) : nullptr;
            const int curve = satFx != nullptr ? satFx->findParameter("Curve") : -1;
            const int enabled = eqFx != nullptr ? eqFx->findParameter("B1 Enabled") : -1;
            juce::ComboBox* combo = satWindow != nullptr ? satWindow->getPanel().getComboBox(curve) : nullptr;
            juce::ToggleButton* toggle = eqWindow != nullptr ? eqWindow->getPanel().getToggle(enabled) : nullptr;
            DAWLAB_CHECK(combo != nullptr && toggle != nullptr);
            if (combo != nullptr && toggle != nullptr) {
                const size_t entries0 = s.getHistory().undoCount();
                combo->setSelectedId(2, juce::sendNotificationSync); // Cubic
                combo->setSelectedId(3, juce::sendNotificationSync); // Arctan
                const size_t choices = s.getHistory().undoCount() - entries0;
                m.undo();
                const float curveAfterUndo = satFx->getParameter(curve);
                const bool on0 = toggle->getToggleState();
                const size_t entries1 = s.getHistory().undoCount();
                toggle->setToggleState(!on0, juce::sendNotificationSync);
                toggle->setToggleState(on0, juce::sendNotificationSync);
                const size_t clicks = s.getHistory().undoCount() - entries1;
                m.undo();
                const float enabledAfterUndo = eqFx->getParameter(enabled);
                std::printf("    two choices of the Saturation's curve: %zu undo entries, undo -> %.0f (1 expected);\n"
                            "    two clicks of the EQ's B1 Enabled: %zu undo entries, undo -> %.0f (%d expected)\n",
                            choices, static_cast<double>(curveAfterUndo), clicks, static_cast<double>(enabledAfterUndo),
                            on0 ? 0 : 1);
                DAWLAB_CHECK(choices == 2 && curveAfterUndo == 1.0f);
                DAWLAB_CHECK(clicks == 2 && enabledAfterUndo == (on0 ? 0.0f : 1.0f));
            }
        }

        // Saved or not: the serial of the newest entry decides, so undo can make a project clean again.
        const juce::File saved = juce::File::getCurrentWorkingDirectory().getChildFile("dawlab_output").getChildFile("l18_undo").getChildFile("Undo.dawlab");
        saved.getParentDirectory().createDirectory();
        s.perform(edit::Rename{StripTarget::ofTrack(bass), "Bass 2"}, "Rename Bass"); // the save has something to save
        const bool dirtyBeforeSave = m.getWindowTitle().endsWith(" *");
        DAWLAB_CHECK(m.saveProject(saved));
        const bool cleanAfterSave = dirtyBeforeSave && !m.getWindowTitle().endsWith("*");
        m.getArrangement().setSelectedTrack(bass);
        cm.invokeDirectly(CommandIDs::toggleMuteSelected, false);
        const bool dirtyAfterMute = m.getWindowTitle().endsWith(" *") && e.getTrack(bass)->isMuted();
        m.undo();
        const bool cleanAfterUndo = !m.getWindowTitle().endsWith("*") && !e.getTrack(bass)->isMuted();
        m.redo();
        const bool dirtyAfterRedo = m.getWindowTitle().endsWith(" *");
        std::printf("    title after save \"%s\"; mute -> \"*\" %s, undo -> clean %s, redo -> \"*\" %s\n",
                    m.getWindowTitle().toRawUTF8(), dirtyAfterMute ? "yes" : "no", cleanAfterUndo ? "yes" : "no",
                    dirtyAfterRedo ? "yes" : "no");
        DAWLAB_CHECK(cleanAfterSave && dirtyAfterMute && cleanAfterUndo && dirtyAfterRedo);

        // A VST3 insert comes back from its saved state when its track is undeleted. (The test
        // plugin stands in for a plugin file.)
        s.createVst3 = [](const juce::File&, const juce::MemoryBlock&, juce::String&) {
            return std::make_unique<VstInsert>(std::make_unique<host::HostedPlugin>(createTestDelayPlugin(64)));
        };
        InsertSnapshot vst;
        vst.kind = SlotKind::Vst3;
        vst.vstFile = "test.vst3";
        const TrackId lead = s.getTrackId(5);
        const bool vstIn = s.perform(edit::SetInsert{StripTarget::ofTrack(lead), 3, vst}, "Insert test plugin");
        const bool vstGone = s.perform(edit::RemoveTrack{lead}, "Delete track Lead") && !s.hasTrack(lead);
        settle(e);
        const bool vstBack = s.undo() && s.getSlot(s.trackRef(lead), 3).kind == SlotKind::Vst3;
        s.undo(); // the insert
        settle(e);
        std::printf("    VST3 insert on a deleted track: %s after undo\n", vstBack ? "back" : "LOST");
        DAWLAB_CHECK(vstIn && vstGone && vstBack);
        m.closeAllEditors();
        settle(e);
        DAWLAB_CHECK(getTestPluginInstanceCount() == 0);
    }

    // 6f. A random walk through every kind of edit: undoing all of it gives the project back
    //     exactly (as text and as sound), redoing all of it gives the end state again.
    {
        auto mOwner = std::make_unique<MainComponent>(MainComponent::Mode::Offline);
        MainComponent& m = *mOwner;
        DAWLAB_CHECK(m.loadDemoSongNow() == 0);
        Session& s = m.getSession();
        Engine& e = m.getEngine();
        const auto stateText = [&s] { return juce::String(project::writeProject(ProjectIO::capture(s, "m").state)); };
        const auto settle = [&e] {
            e.applyPendingCommandsOffline();
            e.collectGarbage();
        };
        settle();
        std::mt19937 rng(20261103);
        const auto pick = [&rng](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };
        const auto real = [&rng](float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(rng); };
        const auto anyTrack = [&] { return s.getTrackId(pick(0, s.getNumTracks() - 1)); };
        const auto anyStrip = [&]() -> StripTarget {
            const int r = pick(0, 9);
            if (r == 8)
                return StripTarget{ChannelKind::Bus, TrackId{}, pick(0, kNumBuses - 1)};
            if (r == 9)
                return StripTarget{};
            return StripTarget::ofTrack(anyTrack());
        };
        const EffectType types[] = {EffectType::Gain, EffectType::ParametricEq, EffectType::Compressor, EffectType::Delay,
                                    EffectType::Chorus, EffectType::Reverb, EffectType::Gate, EffectType::Filter};

        const juce::String before = stateText();
        const AudioBuffer soundBefore = renderSeconds(e, 1.0);
        std::vector<std::pair<TrackId, int>> slotsBefore; // which engine slot each track uses
        for (int lane = 0; lane < s.getNumTracks(); ++lane)
            slotsBefore.emplace_back(s.getTrackId(lane), s.trackRef(s.getTrackId(lane)).index);
        constexpr int kSteps = 150;
        int performed = 0;
        int kinds[14] = {};
        for (int step = 0; step < kSteps; ++step) {
            const int kind = pick(0, 13);
            bool ok = false;
            switch (kind) {
            case 0: ok = s.perform(s.makeNewTrack(pick(0, 1) ? TrackFormat::Mono : TrackFormat::Stereo, pick(-1, s.getNumTracks())), "add"); break;
            case 1: ok = s.getNumTracks() > 2 && s.perform(edit::RemoveTrack{anyTrack()}, "remove"); break;
            case 2: { // gain, sometimes as a merged drag of several steps
                const StripTarget t = anyStrip();
                const auto key = pick(0, 2) == 0 ? mergeKeyFor(t, 1) : 0u;
                ok = s.perform(edit::SetValue{t, edit::Field::Gain, 0, real(-30.0f, 6.0f)}, "gain", key);
                if (key != 0)
                    s.perform(edit::SetValue{t, edit::Field::Gain, 0, real(-30.0f, 6.0f)}, "gain", key);
                s.getHistory().endMerge();
                break;
            }
            case 3: ok = s.perform(edit::SetValue{anyStrip(), edit::Field::Pan, 0, real(-1.0f, 1.0f)}, "pan"); break;
            case 4: ok = s.perform(edit::SetValue{anyStrip(), edit::Field::Mute, 0, static_cast<float>(pick(0, 1))}, "mute"); break;
            case 5: ok = s.perform(edit::SetValue{StripTarget::ofTrack(anyTrack()), edit::Field::Solo, 0, static_cast<float>(pick(0, 1))}, "solo"); break;
            case 6: ok = s.perform(edit::SetValue{StripTarget::ofTrack(anyTrack()), edit::Field::SendLevel, pick(0, kNumBuses - 1), real(-40.0f, 0.0f)}, "send"); break;
            case 7: ok = s.perform(edit::Rename{anyStrip(), "Name " + juce::String(step)}, "rename"); break;
            case 8: ok = s.perform(edit::SetFormat{anyTrack(), pick(0, 1) ? TrackFormat::Mono : TrackFormat::Stereo}, "format"); break;
            case 9: {
                const TrackId t = anyTrack();
                const int n = static_cast<int>(s.getClips(t).size());
                s.getHistory().endMerge();
                ok = n > 0 && s.moveClip(t, pick(0, n - 1), pick(0, 8) * 24000) >= 0;
                s.getHistory().endMerge();
                break;
            }
            case 10: {
                InsertSnapshot insert;
                if (pick(0, 3) != 0) {
                    insert.kind = SlotKind::Builtin;
                    insert.type = types[pick(0, static_cast<int>(std::size(types)) - 1)];
                }
                ok = s.perform(edit::SetInsert{anyStrip(), pick(0, kInsertSlotsPerStrip - 1), insert}, "insert");
                break;
            }
            case 11: {
                const StripTarget t = anyStrip();
                const int slot = pick(0, kInsertSlotsPerStrip - 1);
                const InsertSlotState& st = s.getSlot(s.refOf(t), slot);
                if (!st.isEmpty() && st.effect->getNumParameters() > 0) {
                    const int index = pick(0, st.effect->getNumParameters() - 1);
                    const ParameterInfo info = st.effect->getParameterInfo(index);
                    ok = s.perform(edit::SetParameter{t, slot, index, info.min + real(0.0f, 1.0f) * (info.max - info.min)}, "param");
                }
                break;
            }
            case 12: ok = s.perform(edit::SetBypass{anyStrip(), pick(0, kInsertSlotsPerStrip - 1), pick(0, 1) == 1}, "bypass"); break;
            case 13: ok = s.perform(edit::SetTempo{static_cast<double>(pick(60, 180))}, "tempo"); break;
            default: break;
            }
            performed += ok ? 1 : 0;
            kinds[kind] += ok ? 1 : 0;
            if (step % 8 == 0)
                settle();
        }
        settle();
        const juce::String end = stateText();
        const AudioBuffer soundEnd = renderSeconds(e, 1.0);
        const size_t entries = s.getHistory().undoCount();
        int undos = 0;
        while (s.undo())
            if (++undos % 8 == 0)
                settle();
        settle();
        const bool sameText = stateText() == before;
        const AudioBuffer soundUndone = renderSeconds(e, 1.0);
        const float undoneDiff = maxAbsDiff(soundBefore, 0, soundUndone, 0, soundBefore.getNumSamples());
        int movedSlots = 0;
        for (const auto& [id, slot] : slotsBefore)
            movedSlots += s.trackRef(id).index != slot ? 1 : 0;
        int redos = 0;
        while (s.redo())
            if (++redos % 8 == 0)
                settle();
        settle();
        const bool sameEnd = stateText() == end;
        const AudioBuffer soundRedone = renderSeconds(e, 1.0);
        const float redoneDiff = maxAbsDiff(soundEnd, 0, soundRedone, 0, soundEnd.getNumSamples());
        int kindsUsed = 0;
        for (int k : kinds)
            kindsUsed += k > 0 ? 1 : 0;
        std::printf("6f. random walk: %d of %d edits applied (%d of 14 kinds), %zu undo entries; undo all (%d): project %s,\n"
                    "    sound max |diff| %.3g (%d of %zu tracks back in another engine slot); redo all (%d): project %s,\n"
                    "    sound max |diff| %.3g; %d tracks, leaked %llu\n",
                    performed, kSteps, kindsUsed, entries, undos, sameText ? "as before" : "DIFFERENT",
                    static_cast<double>(undoneDiff), movedSlots, slotsBefore.size(), redos, sameEnd ? "as at the end" : "DIFFERENT",
                    static_cast<double>(redoneDiff), s.getNumTracks(), static_cast<unsigned long long>(e.getGarbageLeaked()));
        DAWLAB_CHECK(performed > kSteps / 2 && kindsUsed == 14);
        DAWLAB_CHECK(undos == static_cast<int>(entries) && redos == undos);
        // The engine sums the tracks in SLOT order. A track that undo brings back may get another
        // slot, and floats added in another order round differently: then (and only then) the
        // sound may differ by a rounding step, far below anything audible (-120 dB).
        DAWLAB_CHECK(sameText && (movedSlots == 0 ? undoneDiff == 0.0f : undoneDiff < 1.0e-6f));
        DAWLAB_CHECK(sameEnd && redoneDiff == 0.0f);
        DAWLAB_CHECK(e.getGarbageLeaked() == 0 && e.getCommandsDropped() == 0);
    }

    // 7. A device change to 44.1 kHz: clip positions follow (Session::handleSampleRateChange).
    {
        const TrackId bass = session.getTrackId(1);
        const auto bassBefore = session.getClips(bass);
        engine.prepare(44100.0, 512); // what AudioHost does when the device restarts at 44.1 kHz
        session.handleSampleRateChange(sr, 44100.0);
        const auto& bassAfter = session.getClips(bass);
        const bool sameCount = bassAfter.size() == bassBefore.size() && bassBefore.size() > 2;
        DAWLAB_CHECK(sameCount);
        if (sameCount) {
            std::printf("7. device change 48 -> 44.1 kHz: bass clip 3 moved %lld -> %lld samples (bar 2 = 88200)\n",
                        static_cast<long long>(bassBefore[2].start), static_cast<long long>(bassAfter[2].start));
            DAWLAB_CHECK(bassBefore[2].start == 96000 && bassAfter[2].start == 88200);
            DAWLAB_CHECK(bassAfter[2].source->getNumSamples() == 44100); // 1 s pluck, resampled
        }
        const AudioBuffer converted = renderSeconds(engine, 1.0);
        DAWLAB_CHECK(!hasNanOrInf(converted) && peakAbs(converted) > 0.01f);
    }

    // 8. The spectrum display (Step 20): a sine exactly on bin 85 of the 4096-point FFT
    //    (85 * 48000 / 4096 = 996.09 Hz) at amplitude 0.5 must show up there at 20 log10(0.5) dBFS,
    //    and the log axis must put the geometric mean of 20 Hz and 20 kHz (632.5 Hz) in the middle.
    {
        dsp::SpectrumAnalyzer analyzer;
        analyzer.prepare(sr, 12);
        const double binHz = 85.0 * sr / 4096.0;
        const AudioBuffer tone = makeSine(binHz, sr, 0.25, 0.5f, 1);
        analyzer.pushSamples(tone.getReadPointer(0), tone.getNumSamples());
        SpectrumView view;
        view.setSize(300, 200);
        const bool updated = view.update(analyzer);
        double peakHz = 0.0;
        float peakDb = -200.0f;
        const bool hasPeak = view.getPeak(peakHz, peakDb);
        const float middle = SpectrumView::frequencyToProportion(std::sqrt(20.0 * 20000.0));
        std::printf("8. spectrum view: %.2f Hz sine at 0.5 -> peak at %.2f Hz, %.2f dBFS (%.2f expected); "
                    "632.5 Hz at x = %.3f\n",
                    binHz, peakHz, static_cast<double>(peakDb), 20.0 * std::log10(0.5), static_cast<double>(middle));
        DAWLAB_CHECK(updated && hasPeak);
        DAWLAB_CHECK_NEAR(peakHz, binHz, 1e-6);
        DAWLAB_CHECK_NEAR(peakDb, 20.0 * std::log10(0.5), 0.1);
        DAWLAB_CHECK_NEAR(middle, 0.5, 1e-6);
    }

    // 9. File > Export mixdown, the way the GUI does it (Step 19): MainComponent::exportMixdown()
    //    starts an ExportJob thread and covers the window; the result comes back through
    //    MessageManager::callAsync, i.e. only once this function has returned and the message loop
    //    runs. The whole song (8 bars at 120 BPM = 16 s) plus the default 3 s tail is exported.
    auto main = std::make_shared<MainComponent>(MainComponent::Mode::Offline);
    DAWLAB_CHECK(main->loadDemoSongNow() == 0);
    const juce::File file =
        juce::File::getCurrentWorkingDirectory().getChildFile("dawlab_output").getChildFile("l18_app_export.wav");
    const auto startTime = juce::Time::getMillisecondCounterHiRes();
    main->onExportFinished = [main, file, startTime, onFinished](const ExportResult& r) {
        const double wallSeconds = (juce::Time::getMillisecondCounterHiRes() - startTime) * 0.001;
        // We are inside MainComponent::finishExport(): finish (and delete the component) later.
        juce::MessageManager::callAsync([main, file, r, wallSeconds, onFinished]() mutable {
            WavData wav;
            const bool readBack = readWav(file.getFullPathName().toStdString(), wav);
            const auto& tr = main->getEngine().getTransport();
            std::printf("9. GUI export (ExportJob thread): %s, %.2f s, peak %.2f dBFS, %.1f LUFS integrated, latency %d "
                        "removed;\n   rendered in %.1f s while the message loop kept running; status line \"%s ...\"\n",
                        r.ok ? "ok" : r.error.toRawUTF8(), r.seconds, static_cast<double>(r.peakDb),
                        static_cast<double>(r.integratedLufs), r.latencySamples, wallSeconds,
                        main->getStatus().upToFirstOccurrenceOf(" ", false, false).toRawUTF8());
            DAWLAB_CHECK(r.ok);
            DAWLAB_CHECK(!main->isExporting());
            DAWLAB_CHECK(main->getStatus().startsWith("Exported"));
            // The render ran processBlock() as fast as it could; its slow blocks were never heard,
            // so they must not show up as dropouts (chapter 15).
            main->tick();
            DAWLAB_CHECK(main->getTransportBar().getCpuStatus().state != CpuState::Overload);
            DAWLAB_CHECK(main->getStatus().startsWith("Exported"));
            DAWLAB_CHECK(readBack && wav.buffer.getNumSamples() == 19 * 48000 && wav.buffer.getNumChannels() == 2);
            DAWLAB_CHECK_NEAR(r.seconds, 19.0, 1e-9);
            DAWLAB_CHECK(r.peakDb <= -1.0f + 0.05f && r.peakDb > -3.0f);
            DAWLAB_CHECK(r.latencySamples == main->getEngine().getLatencySamples());
            DAWLAB_CHECK(!tr.isPlaying() && tr.getPosition() == 0); // the renderer restored the transport
            main->onExportFinished = nullptr; // it holds the other reference to `main`

            // 10. A device restart at 44.1 kHz through MainComponent's own wiring (Step 11):
            //     AudioHost::prepareInternal() -> onEnginePrepared(48000, 44100) stops the
            //     transport -> callAsync -> startClipConversion() -> MediaLoader::convertBuffers()
            //     on the loader thread -> callAsync -> finishClipConversion() ->
            //     Session::applySampleRateConversion(). Section 7 tested the conversion itself;
            //     this tests that the app triggers it, keeps the message loop free meanwhile and
            //     resumes playback afterwards.
            const TrackId bass = main->getSession().getTrackId(1);
            const int64_t before = main->getSession().getClips(bass)[2].start;
            main->getEngine().getTransport().play();           // the user was listening...
            main->getAudioHost().prepareOffline(44100.0, 512); // ...when the device restarted
            const bool stoppedAtOnce = !main->getEngine().getTransport().isPlaying();
            const double t0 = juce::Time::getMillisecondCounterHiRes();
            auto ticks = std::make_shared<int>(0); // message-loop turns while converting
            // The poll function re-posts itself while the conversion runs. It refers to itself
            // only weakly; the pending message owns it.
            auto poll = std::make_shared<std::function<void()>>();
            std::weak_ptr<std::function<void()>> self = poll;
            *poll = [main, bass, before, onFinished, stoppedAtOnce, t0, ticks, self]() mutable {
                if (main->isConvertingSampleRate()) {
                    ++*ticks;
                    if (auto p = self.lock())
                        juce::Timer::callAfterDelay(10, [p] { (*p)(); });
                    return;
                }
                const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
                const int64_t after = main->getSession().getClips(bass)[2].start;
                const bool resumed = main->getEngine().getTransport().isPlaying();
                std::printf("10. GUI device change 48 -> 44.1 kHz (loader thread): engine %.0f Hz, transport stopped at "
                            "once: %s, bass clip 3 %lld -> %lld,\n    converted in %.0f ms with %d message-loop turns "
                            "meanwhile, playback resumed: %s, status \"%s\"\n",
                            main->getEngine().getSampleRate(), stoppedAtOnce ? "yes" : "no", static_cast<long long>(before),
                            static_cast<long long>(after), ms, *ticks, resumed ? "yes" : "no",
                            main->getStatus().toRawUTF8());
                DAWLAB_CHECK(main->getEngine().getSampleRate() == 44100.0);
                DAWLAB_CHECK(stoppedAtOnce);
                DAWLAB_CHECK(before == 96000 && after == 88200);
                DAWLAB_CHECK(main->getSession().getClipSampleRate() == 44100.0);
                DAWLAB_CHECK(main->getSession().getClips(bass)[2].source->getNumSamples() == 44100);
                DAWLAB_CHECK(resumed);
                DAWLAB_CHECK(main->getStatus().startsWith("Sample rate changed to 44100 Hz"));
                main->getEngine().getTransport().stop();
                // The GUI goes away on the message thread, after the export thread ended.
                main.reset();
                std::printf("\n");
                onFinished(test::summary("l18_app_smoke_test"));
            };
            juce::MessageManager::callAsync([poll] { (*poll)(); });
        });
    };
    main->exportMixdown(file);
    juce::ApplicationCommandInfo playInfo(CommandIDs::playStop);
    main->getCommandInfo(CommandIDs::playStop, playInfo);
    DAWLAB_CHECK(main->isExporting());                                                  // the job runs ...
    DAWLAB_CHECK((playInfo.flags & juce::ApplicationCommandInfo::isDisabled) != 0); // ... and editing is off
    // The demo song (it may have been asked for before the export started) replaces nothing meanwhile.
    DAWLAB_CHECK(main->loadDemoSongNow() < 0 && main->getStatus().contains("export"));
    if (!main->isExporting()) { // nothing will call back: finish now
        main->onExportFinished = nullptr;
        main.reset();
        onFinished(test::summary("l18_app_smoke_test"));
    }
}

// ---- --snapshot ------------------------------------------------------------------------------------

static constexpr int kSnapshotBlock = 512;

int runSnapshot(const juce::File& pngFile) {
    MainComponent main(MainComponent::Mode::Offline);
    main.setSize(MainComponent::kDefaultWidth, MainComponent::kDefaultHeight);
    const int unknown = main.loadDemoSongNow();
    Engine& engine = main.getEngine();
    engine.applyPendingCommandsOffline();
    {
        // Two lanes on screen: a fader that rises into bar 3, a compressor threshold with a step.
        Session& demo = main.getSession();
        const Transport& t = engine.getTransport();
        const auto bars = [&t](double b) { return static_cast<std::int64_t>(b * t.getSamplesPerBar()); };
        const TrackId drums = demo.getTrackId(0), bass = demo.getTrackId(1);
        const LaneAddress fader{StripTarget::ofTrack(drums), EffectId{}, "Gain"};
        LaneContent rising;
        rising.curve.add({0, 0.72f, Shape::Linear});
        rising.curve.add({bars(2.0), 0.72f, Shape::Linear});
        rising.curve.add({bars(3.0), 0.86f, Shape::Linear});
        rising.curve.add({bars(6.0), 0.86f, Shape::Hold});
        rising.curve.add({bars(7.0), 0.78f, Shape::Linear});
        demo.setLane(fader, rising);
        main.getArrangement().showAutomation(drums, fader);
        const LaneAddress panning{StripTarget::ofTrack(bass), EffectId{}, "Pan"};
        LaneContent moving;
        moving.mode = AutomationMode::Touch;
        moving.curve.add({0, 0.5f, Shape::Linear});
        moving.curve.add({bars(4.0), 0.35f, Shape::Linear});
        moving.curve.add({bars(8.0), 0.65f, Shape::Linear});
        demo.setLane(panning, moving);
        main.getArrangement().showAutomation(bass, panning);
        engine.applyPendingCommandsOffline();
    }

    // Play from bar 1 until bar 2 beat 3 is *heard*, as if an audio device were running, with UI
    // ticks: the transport ends the mix latency further on (Step 13).
    Transport& tr = engine.getTransport();
    tr.setPosition(0);
    tr.play();
    const int64_t target = tr.barsBeatsToSamples({2, 3, 0}) + engine.getLatencySamples();
    const int64_t tickSamples = static_cast<int64_t>(engine.getSampleRate() / 30.0);
    for (int64_t done = 0; done < target; done += tickSamples) {
        main.simulatePlayback(std::min(tickSamples, target - done));
        main.tick();
    }
    main.getArrangement().selectLane(0);
    // These blocks were not played against a clock: one that took long says nothing about dropouts.
    main.getTransportBar().forgetCpuHistory();
    main.setStatus("Snapshot: demo song at bar 2 beat 3 (simulated playback, no audio device)");
    main.tick();

    pngFile.getParentDirectory().createDirectory();
    const juce::Image image = main.createComponentSnapshot(main.getLocalBounds(), true, 1.0f);
    bool ok = false;
    {
        pngFile.deleteFile();
        juce::FileOutputStream out(pngFile);
        juce::PNGImageFormat png;
        ok = out.openedOk() && png.writeImageToStream(image, out);
    }

    // The auto-generated parameter panel of the drum compressor, as a second picture.
    const juce::File panelFile = pngFile.getSiblingFile(pngFile.getFileNameWithoutExtension() + "_panel.png");
    float gainReductionDb = 0.0f;
    Session& session = main.getSession();
    if (InsertEffect* comp = session.getSlot(session.trackRef(session.getTrackId(0)), 1).effect) {
        gainReductionDb = comp->getMeterDb();
        ParameterPanel panel(*comp);
        panel.setLookAndFeel(&main.getDawLookAndFeel());
        panel.refreshFromEffect();
        const juce::Image panelImage = panel.createComponentSnapshot(panel.getLocalBounds(), true, 1.0f);
        panelFile.deleteFile();
        juce::FileOutputStream out(panelFile);
        juce::PNGImageFormat png;
        ok = ok && out.openedOk() && png.writeImageToStream(panelImage, out);
        panel.setLookAndFeel(nullptr);
    }
    std::printf("snapshot %s: %dx%d, %s, position %s, unknown parameters %d\n", ok ? "written" : "FAILED",
                image.getWidth(), image.getHeight(), pngFile.getFullPathName().toRawUTF8(),
                main.getTransportBar().getPositionText().toRawUTF8(), unknown);
    const auto& master = engine.getMixer().getMaster();
    std::printf("meters at that point: master peak %.1f / %.1f dBFS, loudness M %.1f  S %.1f  I %.1f LUFS, "
                "mix latency %d samples\n",
                static_cast<double>(master.getPeakDb(0)), static_cast<double>(master.getPeakDb(1)),
                static_cast<double>(master.getMomentaryLufs()), static_cast<double>(master.getShortTermLufs()),
                static_cast<double>(master.getIntegratedLufs()), engine.getLatencySamples());
    std::printf("panel snapshot: %s (drum compressor, gain reduction %.1f dB)\n", panelFile.getFullPathName().toRawUTF8(),
                static_cast<double>(gainReductionDb));

    // A third picture: a recording in progress on a new track, with the song playing on.
    const juce::File takeFile = pngFile.getSiblingFile(pngFile.getFileNameWithoutExtension() + "_recording.png");
    {
        TakesFolder takes;
        TakeRecorder& recorder = main.getTakeRecorder();
        recorder.setFolder(takes.get());
        const TrackId armed = session.addTrack(TrackFormat::Mono, "Vocal");
        engine.applyPendingCommandsOffline();
        session.dispatchPendingMessages();
        main.setArmed(armed, true);
        engine.getTrack(armed)->getMonitor().setMode(MonitorMode::Auto);
        main.getArrangement().hideAutomation(session.getTrackId(0));
        main.getArrangement().hideAutomation(session.getTrackId(1));
        main.getArrangement().setSelectedTrack(armed);
        main.getArrangement().scrollToTrack(armed);
        tr.setPosition(tr.barsBeatsToSamples({3, 1, 0}));
        if (!main.toggleRecord()) {
            std::printf("recording snapshot: the recording did not start: %s\n", main.getStatus().toRawUTF8());
            ok = false;
        }
        const AudioBuffer voice = makeSine(220.0, engine.getSampleRate(), 8.0, 0.5f, 1);
        int offset = 0;
        const auto sampleRate = static_cast<int>(engine.getSampleRate());
        for (int seconds = 0; seconds < 6; ++seconds) {
            std::vector<float> in(static_cast<size_t>(sampleRate));
            for (int i = 0; i < sampleRate; ++i) // a little phrasing, so that it looks like something
                in[static_cast<size_t>(i)] = voice.getSample(0, offset + i) * (0.3f + 0.7f * std::abs(std::sin(0.0007f * static_cast<float>(offset + i))));
            const float* channels[] = {in.data()};
            main.simulatePlayback(sampleRate, kSnapshotBlock, InputBlock{channels, 1});
            offset += sampleRate;
            main.tick();
            juce::Thread::sleep(20);
        }
        juce::Thread::sleep(100);
        main.getTransportBar().forgetCpuHistory();
        main.setStatus("Snapshot: recording the armed track (simulated input, no audio device)");
        main.tick();
        const juce::Image recording = main.createComponentSnapshot(main.getLocalBounds(), true, 1.0f);
        takeFile.deleteFile();
        juce::FileOutputStream out(takeFile);
        juce::PNGImageFormat png;
        if (!out.openedOk() || !png.writeImageToStream(recording, out)) {
            std::printf("recording snapshot: cannot write %s: %s\n", takeFile.getFullPathName().toRawUTF8(),
                        out.getStatus().getErrorMessage().toRawUTF8());
            ok = false;
        }
        tr.stop();
        std::vector<float> nothing(512);
        const float* channels[] = {nothing.data()};
        main.simulatePlayback(512, 512, InputBlock{channels, 1});
        for (int i = 0; i < 2000 && recorder.isBusy(); ++i) {
            main.tick();
            juce::Thread::sleep(2);
        }
    }
    std::printf("recording snapshot: %s\n", takeFile.getFullPathName().toRawUTF8());
    return ok && unknown == 0 ? 0 : 1;
}

} // namespace dawlab::app
