#pragma once
// Exercise 12 - the self-check (main). Included at the end of the starter and of the solution.
//
// A VST3 instrument cannot be assumed on the machine that runs the check (CI has none), so the instrument here is
// a small JUCE AudioProcessor of the check's own - the same interface a loaded VST3 has. Loading a real file is
// checked with chapter 13's DawLab Compressor when it is built: an effect, which load() must refuse.

#include "app_rig.h"
#include "ex12_plugin_instrument.h"

#include <dawlab/Check.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

// ---- allocations, counted on this thread while `counting` (as tests/test_midi.cpp does) --------------------------
namespace ex12::check {
inline thread_local bool counting = false;
inline std::atomic<long> allocations{0};
} // namespace ex12::check

void* operator new(std::size_t n) {
    if (ex12::check::counting)
        ex12::check::allocations.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(n == 0 ? 1 : n))
        return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace ex12::check {

using namespace ex18rig;
using dawlab::engine::BlockEvent;
using dawlab::engine::MidiMessage;

// A sine for the last note held, ADDED to the buffer: left 0.25, right half of it (so the channels can be told
// apart), or one channel.
// It writes down every MIDI message it gets, with its sample position counted from its first block.
class TestSynth final : public juce::AudioProcessor {
public:
    explicit TestSynth(bool stereo = true)
        : juce::AudioProcessor(BusesProperties().withOutput("Out", stereo ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::mono(), true)) {
        log.reserve(1024);
    }
    struct Logged {
        std::int64_t sample;
        int bytes[3];
        int size;
    };
    std::vector<Logged> log;
    int resets = 0;
    const float* memory = nullptr; // where the host's buffer was in the first block
    bool moved = false;            // and whether it was ever somewhere else: JUCE allocates with malloc, not new
    float gain = 0.25f;

    const juce::String getName() const override { return "Test Synth"; }
    void prepareToPlay(double sampleRate, int) override { rate_ = sampleRate; }
    void releaseResources() override {}
    void reset() override {
        ++resets;
        note_ = -1;
    }
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override {
        if (memory == nullptr)
            memory = buffer.getReadPointer(0);
        moved = moved || buffer.getReadPointer(0) != memory;
        int done = 0;
        auto renderTo = [&](int to) {
            for (int i = done; i < to; ++i) {
                const float v = note_ < 0 ? 0.0f : gain * static_cast<float>(std::sin(phase_));
                if (note_ >= 0)
                    phase_ += 2.0 * 3.14159265358979 * 440.0 * std::pow(2.0, (note_ - 69) / 12.0) / rate_;
                buffer.addSample(0, i, v); // adds, as many synths do: the host clears the buffer
                if (buffer.getNumChannels() > 1)
                    buffer.addSample(1, i, 0.5f * v);
            }
            done = to;
        };
        for (const juce::MidiMessageMetadata m : midi) {
            renderTo(m.samplePosition);
            if (log.size() < log.capacity())
                log.push_back({rendered_ + m.samplePosition, {m.data[0], m.numBytes > 1 ? m.data[1] : -1, m.numBytes > 2 ? m.data[2] : -1}, m.numBytes});
            const juce::MidiMessage message = m.getMessage();
            if (message.isNoteOn())
                note_ = message.getNoteNumber();
            else if (message.isNoteOff() && message.getNoteNumber() == note_)
                note_ = -1;
        }
        renderTo(buffer.getNumSamples());
        rendered_ += buffer.getNumSamples();
    }
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& data) override { data.replaceAll(&gain, sizeof gain); }
    void setStateInformation(const void* data, int size) override {
        if (size == sizeof gain)
            std::memcpy(&gain, data, sizeof gain);
    }

private:
    double rate_ = 48000.0, phase_ = 0.0;
    int note_ = -1;
    std::int64_t rendered_ = 0;
};

void checkWrapper() {
    auto synth = std::make_unique<TestSynth>();
    TestSynth& s = *synth;
    PluginInstrument instrument(std::move(synth));
    instrument.prepare(48000.0, 512);
    std::vector<float> left(512), right(512);
    float* out[] = {left.data(), right.data()};

    // A note on at 100 into a block that already holds 0.1: added, from sample 100, the right channel at half.
    std::fill(left.begin(), left.end(), 0.1f);
    std::fill(right.begin(), right.end(), 0.1f);
    const BlockEvent first[] = {{100, MidiMessage::noteOn(0, 69, 100)}, {100, MidiMessage::controller(0, 64, 127)},
                                {300, MidiMessage::pitchBend(2, 1000)}, {400, {0xC3, 7, 0}}};
    counting = true;
    instrument.render(first, 4, out, 2, 512);
    counting = false;
    bool before = true;
    for (int i = 0; i < 101; ++i)
        before = before && left[static_cast<size_t>(i)] == 0.1f && right[static_cast<size_t>(i)] == 0.1f;
    const bool sounding = std::fabs(left[200] - 0.1f) > 0.01f && std::fabs((right[200] - 0.1f) * 2.0f - (left[200] - 0.1f)) < 1e-6f;
    DAWLAB_CHECK(before && sounding);
    DAWLAB_CHECK(s.log.size() == 4);
    if (s.log.size() == 4) {
        DAWLAB_CHECK(s.log[0].sample == 100 && s.log[0].bytes[0] == 0x90 && s.log[0].bytes[1] == 69 && s.log[0].size == 3);
        DAWLAB_CHECK(s.log[1].sample == 100 && s.log[1].bytes[0] == 0xB0 && s.log[1].bytes[2] == 127);
        DAWLAB_CHECK(s.log[2].sample == 300 && s.log[2].bytes[0] == 0xE2 && s.log[2].size == 3);
        DAWLAB_CHECK(s.log[3].sample == 400 && s.log[3].bytes[0] == 0xC3 && s.log[3].bytes[1] == 7 && s.log[3].size == 2);
    }
    // A short block, a note off, then reset().
    const BlockEvent second[] = {{10, MidiMessage::noteOff(0, 69)}};
    std::fill(left.begin(), left.end(), 0.0f);
    std::fill(right.begin(), right.end(), 0.0f);
    counting = true;
    instrument.render(second, 1, out, 2, 64);
    counting = false;
    DAWLAB_CHECK(s.log.size() == 5 && s.log.back().sample == 512 + 10 && left[5] != 0.0f && left[20] == 0.0f && left[100] == 0.0f);
    instrument.render(first, 1, out, 2, 512);
    instrument.reset();
    std::fill(left.begin(), left.end(), 0.0f);
    instrument.render(nullptr, 0, out, 2, 512);
    DAWLAB_CHECK(s.resets >= 1 && ex18rig::peakOf(left) == 0.0f);
    std::printf("  events at their samples (a note, a pedal, a bend, a program change), the sound added, reset: done\n");
    std::printf("  allocations while rendering: %ld\n", allocations.load());
    DAWLAB_CHECK(allocations.load() == 0 && !s.moved);

    // A mono processor goes to both channels.
    PluginInstrument mono(std::make_unique<TestSynth>(false));
    mono.prepare(48000.0, 512);
    std::fill(left.begin(), left.end(), 0.0f);
    std::fill(right.begin(), right.end(), 0.0f);
    mono.render(first, 1, out, 2, 512);
    DAWLAB_CHECK(left[300] != 0.0f && left[300] == right[300]);

    // The state as text, and back.
    const juce::String text = instrument.getStateText();
    s.gain = 0.5f;
    DAWLAB_CHECK(instrument.setStateText(text) && s.gain == 0.25f);
    DAWLAB_CHECK(!instrument.setStateText("") && s.gain == 0.25f);
    std::printf("  a mono processor on both channels; the state as \"%s\" and back\n", text.toRawUTF8());
}

void checkOnATrack() {
    Rig rig;
    const auto track = rig.instrumentTrack();
    MidiClipModel clip;
    clip.played = {{960, 960, 69, 100, 0}};
    rig.session.setMidiClips(track, {clip});
    rig.engine.applyPendingCommandsOffline();
    auto synth = std::make_unique<TestSynth>();
    TestSynth* s = synth.get();
    auto instrument = std::make_unique<PluginInstrument>(std::move(synth));
    DAWLAB_CHECK(rig.engine.setTrackInstrument(track, std::move(instrument)) != nullptr);
    rig.engine.getTransport().play();
    const std::vector<float> out = rig.play(72000);
    // The note at tick 960 = sample 24000, its end at 48000; the built-in synth silent, the track sounding.
    DAWLAB_CHECK(s->log.size() == 2 && s->log[0].sample == 24000 && s->log[1].sample == 48000);
    const size_t mix = static_cast<size_t>(rig.engine.getLatencySamples()); // heard that much later
    DAWLAB_CHECK(peakOf(out, 0, 24000 + mix) == 0.0f && peakOf(out, 24000 + mix, 48000 + mix) > 0.01f && peakOf(out, 48100 + mix) == 0.0f);
    DAWLAB_CHECK(rig.engineTrack(track)->getSynth()->activeVoices() == 0);
    std::printf("  on an instrument track: the note on at sample %lld, its end at %lld, heard in the mix\n",
                static_cast<long long>(s->log.empty() ? -1 : s->log[0].sample), static_cast<long long>(s->log.size() < 2 ? -1 : s->log[1].sample));
    // The built-in synth again; the plugin comes back as garbage.
    rig.engine.setTrackInstrument(track, nullptr);
    rig.play(512);
    DAWLAB_CHECK(rig.engineTrack(track)->getReplacementInstrument() == nullptr); // (the app's tick() collects the garbage)

    // A real VST3: chapter 13's compressor is an effect, so it is refused.
#ifdef DAWLAB_L13_COMPRESSOR_VST3
    juce::String error;
    const auto loaded = PluginInstrument::load(rig.main->getPluginHost(), juce::File(DAWLAB_L13_COMPRESSOR_VST3), 48000.0, 512, error);
    DAWLAB_CHECK(loaded == nullptr && error.contains("not an instrument"));
    std::printf("  load(\"DawLabCompressor.vst3\"): %s\n", error.toRawUTF8());
#else
    std::printf("  (chapter 13's plugin is not built here: loading a real VST3 is not checked)\n");
#endif
    juce::String missing;
    DAWLAB_CHECK(PluginInstrument::load(rig.main->getPluginHost(), juce::File("no such plugin.vst3"), 48000.0, 512, missing) == nullptr &&
                 missing.isNotEmpty());
}

} // namespace ex12::check

int main() {
    juce::ScopedJuceInitialiser_GUI juce;
    std::printf("Exercise 12 - a VST3 instrument on a track\n");
    ex12::check::checkWrapper();
    ex12::check::checkOnATrack();
    return dawlab::test::summary("l18_ex12");
}
