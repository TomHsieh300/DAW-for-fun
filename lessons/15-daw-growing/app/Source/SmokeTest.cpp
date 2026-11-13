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
#include "VstInsert.h"

#include <dawlab/Analysis.h>
#include <dawlab/Check.h>
#include <dawlab/Signals.h>
#include <dawlab/WavFile.h>
#include <dawlab/engine/All.h>
#include <dawlab/engine/DemoSong.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
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

// ---- --smoke-test --------------------------------------------------------------------------------

void runSmokeTest(std::function<void(int exitCode)> onFinished) {
    constexpr double sr = 48000.0;
    std::setvbuf(stdout, nullptr, _IONBF, 0); // unbuffered: a crash must not swallow what was printed
    std::printf("DawLab 15 smoke test (no audio device)\n\n");

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
        const juce::File wav = juce::File::getCurrentWorkingDirectory().getChildFile("dawlab_output").getChildFile("l15_app_smoke_44k.wav");
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
            {writeWith(flac, "l15_app_smoke_44k.flac", 24), 0.05},
            {writeWith(ogg, "l15_app_smoke_44k.ogg", 16), 0.5},
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
        options.file = juce::File::getCurrentWorkingDirectory().getChildFile("dawlab_output").getChildFile("l15_app_smoke.wav");
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
        const juce::File dir = juce::File::getCurrentWorkingDirectory().getChildFile("dawlab_output").getChildFile("l15_project");
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
        DAWLAB_CHECK(b.getProjectFile() == copy && b.getWindowTitle() == "DawLab 15 - Demo");

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

        // Saved or not: the serial of the newest entry decides, so undo can make a project clean again.
        const juce::File saved = juce::File::getCurrentWorkingDirectory().getChildFile("dawlab_output").getChildFile("l15_undo").getChildFile("Undo.dawlab");
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
        juce::File::getCurrentWorkingDirectory().getChildFile("dawlab_output").getChildFile("l15_app_export.wav");
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
                onFinished(test::summary("l15_app_smoke_test"));
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
        onFinished(test::summary("l15_app_smoke_test"));
    }
}

// ---- --snapshot ------------------------------------------------------------------------------------

int runSnapshot(const juce::File& pngFile) {
    MainComponent main(MainComponent::Mode::Offline);
    main.setSize(MainComponent::kDefaultWidth, MainComponent::kDefaultHeight);
    const int unknown = main.loadDemoSongNow();
    Engine& engine = main.getEngine();
    engine.applyPendingCommandsOffline();

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
    return ok && unknown == 0 ? 0 : 1;
}

} // namespace dawlab::app
