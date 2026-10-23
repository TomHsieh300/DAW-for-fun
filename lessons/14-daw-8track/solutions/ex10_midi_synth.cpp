// l14_ex10_solution -- Exercise 10: a MIDI track with a simple synthesizer (solution).
//
// What it shows
//   - MIDI to timeline: a juce::MidiMessageSequence in ticks (960 per beat) becomes a sorted list
//     of NoteEvents in samples: start = round(ticks / 960 * samplesPerBeat).
//   - SimpleSynth: an *instrument insert*. It lives in a track's first insert slot and adds its
//     voices to whatever the track's clips produced (here: nothing). It is an EffectBase, so its
//     six parameters (Waveform, Attack, Decay, Sustain, Release, Gain) get an auto-generated panel
//     in the app like any other effect.
//   - Sample accuracy: notes start at the exact sample, found from the PlayHead's timeline
//     segments (so loops work: when the loop jumps back, the notes play again).
//   - 8 voices with an ADSR envelope, voice stealing (oldest voice), PolyBLEP saw / square.
//   - The note list is handed to the audio thread like the automation lane of Exercise 8:
//     atomic pointer in, SpscQueue back to the UI thread for deletion.
//
// Run
//   build/bin/Debug/l14_ex10_solution.exe
//
// Output
//   dawlab_output/l14_ex10_synth.wav   a 4-bar phrase (melody + chords), saw wave
//   Self-check: onset at sample 24000, pitch by Goertzel, 3 voices for a triad, release to
//   silence, block-size invariance, notes repeat in a loop.

#include <dawlab/Common.h>
#include <dawlab/engine/All.h>

#include "Session.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using namespace dawlab;
using namespace dawlab::engine;
using dawlab::app::Session;

namespace ex10 {

constexpr double kTicksPerBeat = 960.0;

struct NoteEvent {
    int64_t start = 0;  // timeline sample of the note-on
    int64_t length = 0; // samples until the note-off
    int note = 60;      // MIDI note number (69 = A4 = 440 Hz)
    float velocity = 1.0f;
};

// Pairs note-ons with their note-offs and converts ticks to samples. Sorted by start.
std::vector<NoteEvent> notesFromMidi(juce::MidiMessageSequence sequence, double samplesPerBeat) {
    sequence.updateMatchedPairs();
    std::vector<NoteEvent> notes;
    const auto toSamples = [samplesPerBeat](double ticks) {
        return static_cast<int64_t>(std::llround(ticks / kTicksPerBeat * samplesPerBeat));
    };
    for (int i = 0; i < sequence.getNumEvents(); ++i) {
        const auto* e = sequence.getEventPointer(i);
        if (!e->message.isNoteOn() || e->noteOffObject == nullptr)
            continue;
        NoteEvent n;
        n.start = toSamples(e->message.getTimeStamp());
        n.length = std::max<int64_t>(1, toSamples(e->noteOffObject->message.getTimeStamp()) - n.start);
        n.note = e->message.getNoteNumber();
        n.velocity = e->message.getFloatVelocity();
        notes.push_back(n);
    }
    std::stable_sort(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b) { return a.start < b.start; });
    return notes;
}

struct NoteList {
    std::vector<NoteEvent> notes;
};

namespace choices {
inline constexpr const char* kWaveform[] = {"Sine", "Saw", "Square"};
}

class SimpleSynth final : public EffectBase {
public:
    enum Param { Waveform, Attack, Decay, Sustain, Release, Gain, kCount };
    static constexpr int kMaxVoices = 8;

    SimpleSynth() : EffectBase(kInfos, kCount) {}
    ~SimpleSynth() override {
        delete pending_.exchange(nullptr);
        delete current_;
        collectRetired();
    }

    const char* getName() const noexcept override { return "SimpleSynth"; }
    void prepare(double sampleRate, int, int) override {
        finishPrepare(sampleRate);
        reset();
    }
    void reset() noexcept override {
        for (auto& v : voices_)
            v = Voice{};
        expectedNext_ = -1;
        activeVoices_.store(0);
    }
    void setPlayHead(const PlayHead& playHead) noexcept override { playHead_ = playHead; }

    // UI thread
    void setNotes(std::vector<NoteEvent> notes) {
        collectRetired();
        auto* list = new NoteList{std::move(notes)};
        delete pending_.exchange(list, std::memory_order_acq_rel);
    }
    int collectRetired() {
        int n = 0;
        NoteList* old = nullptr;
        while (retired_.pop(old)) {
            delete old;
            ++n;
        }
        return n;
    }
    int getActiveVoices() const noexcept { return activeVoices_.load(std::memory_order_relaxed); }

    static double noteToHz(int note) noexcept { return 440.0 * std::pow(2.0, (note - 69) / 12.0); }

protected:
    void applyParameter(int index, float v) noexcept override {
        const double sr = getSampleRate();
        switch (index) {
        case Waveform: waveform_ = static_cast<int>(v); break;
        case Attack: attackStep_ = 1.0 / std::max(1.0, v * 0.001 * sr); break;      // 0 -> 1 in Attack ms
        case Decay: decaySeconds_ = v * 0.001; decayStep_ = (1.0 - sustain_) / std::max(1.0, v * 0.001 * sr); break;
        case Sustain:
            sustain_ = v;
            decayStep_ = (1.0 - sustain_) / std::max(1.0, decaySeconds_ * sr);
            break;
        case Release: releaseSamples_ = std::max(1.0, v * 0.001 * sr); break;
        case Gain: gain_ = dbToGain(v); break;
        default: break;
        }
    }

    void processDsp(AudioBlock block) noexcept override {
        if (NoteList* fresh = pending_.exchange(nullptr, std::memory_order_acq_rel)) {
            if (current_ != nullptr && !retired_.push(current_)) { /* UI never collects: leak, never block */ }
            current_ = fresh;
        }
        if (!playHead_.isPlaying()) {
            releaseAll();
            renderVoices(block, 0, block.getNumSamples(), -1);
            expectedNext_ = -1;
            return;
        }
        for (int s = 0; s < playHead_.numSegments; ++s) {
            const TimelineSegment& seg = playHead_.segments[s];
            if (seg.timelineStart != expectedNext_)
                jumpTo(seg.timelineStart); // first block, seek or loop: notes do not continue
            // Render up to each note-on inside the segment, start the note, continue.
            int done = 0;
            while (done < seg.numSamples) {
                const int64_t t = seg.timelineStart + done;
                int len = seg.numSamples - done;
                if (current_ != nullptr && cursor_ < current_->notes.size()) {
                    if (current_->notes[cursor_].start < t) { // cannot happen after jumpTo(); be safe
                        ++cursor_;
                        continue;
                    }
                    len = static_cast<int>(std::min<int64_t>(len, current_->notes[cursor_].start - t));
                }
                if (len > 0) {
                    renderVoices(block, seg.blockOffset + done, len, t);
                    done += len;
                    continue;
                }
                // one or more notes start exactly at t
                while (current_ != nullptr && cursor_ < current_->notes.size() && current_->notes[cursor_].start == t)
                    noteOn(current_->notes[cursor_++]);
            }
            expectedNext_ = seg.timelineStart + seg.numSamples;
        }
    }

private:
    struct Voice {
        enum class Stage { Off, Attack, Decay, Sustain, Release } stage = Stage::Off;
        double phase = 0.0, increment = 0.0; // phase in cycles (0..1)
        double level = 0.0, releaseStep = 0.0;
        float velocity = 0.0f;
        int64_t start = 0, end = 0; // timeline samples (end = note-off)
    };

    void jumpTo(int64_t t) noexcept {
        releaseAll();
        cursor_ = 0;
        if (current_ != nullptr) // first note that starts at or after t (binary search)
            cursor_ = static_cast<size_t>(std::lower_bound(current_->notes.begin(), current_->notes.end(), t,
                                                           [](const NoteEvent& n, int64_t s) { return n.start < s; }) -
                                          current_->notes.begin());
    }

    void noteOn(const NoteEvent& n) noexcept {
        // a free voice, else steal the one that started first
        Voice* v = &voices_[0];
        for (auto& candidate : voices_) {
            if (candidate.stage == Voice::Stage::Off) {
                v = &candidate;
                break;
            }
            if (candidate.start < v->start)
                v = &candidate;
        }
        *v = Voice{};
        v->stage = Voice::Stage::Attack;
        v->increment = noteToHz(n.note) / getSampleRate();
        v->velocity = n.velocity;
        v->start = n.start;
        v->end = n.start + n.length;
    }

    void release(Voice& v) noexcept {
        if (v.stage == Voice::Stage::Off || v.stage == Voice::Stage::Release)
            return;
        v.stage = Voice::Stage::Release;
        v.releaseStep = v.level / releaseSamples_; // linear to 0 in Release ms
    }
    void releaseAll() noexcept {
        for (auto& v : voices_)
            release(v);
    }

    // PolyBLEP: subtracts the band-limited step residual around the discontinuity (t = phase,
    // dt = increment), which removes most of the aliasing of a naive saw / square.
    static double polyBlep(double t, double dt) noexcept {
        if (t < dt) {
            t /= dt;
            return t + t - t * t - 1.0;
        }
        if (t > 1.0 - dt) {
            t = (t - 1.0) / dt;
            return t * t + t + t + 1.0;
        }
        return 0.0;
    }

    double oscillator(Voice& v) const noexcept {
        double y = 0.0;
        switch (waveform_) {
        case 0: y = std::sin(kTwoPi * v.phase); break;
        case 1: y = 2.0 * v.phase - 1.0 - polyBlep(v.phase, v.increment); break;
        default: {
            y = v.phase < 0.5 ? 1.0 : -1.0;
            y += polyBlep(v.phase, v.increment);
            y -= polyBlep(std::fmod(v.phase + 0.5, 1.0), v.increment);
            break;
        }
        }
        v.phase += v.increment;
        if (v.phase >= 1.0)
            v.phase -= 1.0;
        return y;
    }

    double envelope(Voice& v) noexcept {
        switch (v.stage) {
        case Voice::Stage::Attack:
            v.level += attackStep_;
            if (v.level >= 1.0) {
                v.level = 1.0;
                v.stage = Voice::Stage::Decay;
            }
            break;
        case Voice::Stage::Decay:
            v.level -= decayStep_;
            if (v.level <= sustain_) {
                v.level = sustain_;
                v.stage = Voice::Stage::Sustain;
            }
            break;
        case Voice::Stage::Sustain: v.level = sustain_; break;
        case Voice::Stage::Release:
            v.level -= v.releaseStep;
            if (v.level <= 0.0) {
                v.level = 0.0;
                v.stage = Voice::Stage::Off;
            }
            break;
        case Voice::Stage::Off: v.level = 0.0; break;
        }
        return v.level;
    }

    // Adds `len` samples of all voices at block offset `offset`; `t` = timeline position of the
    // first one (-1 = transport stopped: no note-offs to check).
    void renderVoices(AudioBlock block, int offset, int len, int64_t t) noexcept {
        int active = 0;
        for (auto& v : voices_) {
            if (v.stage == Voice::Stage::Off)
                continue;
            for (int i = 0; i < len; ++i) {
                if (t >= 0 && t + i == v.end)
                    release(v);
                const double env = envelope(v);
                const float y = static_cast<float>(oscillator(v) * env * v.velocity * gain_);
                for (int ch = 0; ch < block.getNumChannels(); ++ch)
                    block.getChannelPointer(ch)[offset + i] += y;
                if (v.stage == Voice::Stage::Off)
                    break;
            }
            active += v.stage != Voice::Stage::Off ? 1 : 0;
        }
        activeVoices_.store(active, std::memory_order_relaxed);
    }

    static constexpr ParameterInfo kInfos[kCount] = {
        paramChoice("Waveform", choices::kWaveform, 1),
        paramFloat("Attack", "ms", 0.0f, 2000.0f, 5.0f),
        paramFloat("Decay", "ms", 1.0f, 4000.0f, 200.0f, true),
        paramFloat("Sustain", "", 0.0f, 1.0f, 0.7f),
        paramFloat("Release", "ms", 1.0f, 5000.0f, 150.0f, true),
        paramFloat("Gain", "dB", -60.0f, 0.0f, -12.0f),
    };

    std::array<Voice, kMaxVoices> voices_{};
    std::atomic<NoteList*> pending_{nullptr};
    NoteList* current_ = nullptr;
    SpscQueue<NoteList*> retired_{64};
    size_t cursor_ = 0;
    int64_t expectedNext_ = -1;
    PlayHead playHead_;
    std::atomic<int> activeVoices_{0};
    int waveform_ = 1;
    double attackStep_ = 1.0, decayStep_ = 0.0, decaySeconds_ = 0.2, sustain_ = 0.7, releaseSamples_ = 1.0;
    float gain_ = 0.25f;
};

} // namespace ex10

namespace {
constexpr double kSr = 48000.0;

AudioBuffer render(Engine& engine, int64_t start, int64_t numSamples, int blockSize = 512) {
    RenderOptions ro;
    ro.startSample = start;
    ro.numSamples = numSamples;
    ro.blockSize = blockSize;
    ro.compensateLatency = false;
    return renderOffline(engine, ro);
}
} // namespace

int main() {
    const juce::ScopedJuceInitialiser_GUI juceInit;
    using namespace ex10;

    Engine engine;
    engine.prepare(kSr, 512);
    Session session(engine);
    session.newProject();
    session.clearInsert(ChannelRef::master(), 3); // measure the synth itself, no limiter
    engine.getMixer().getTrack(0).setPanMode(PanMode::Balance);

    // A MIDI clip: A4 on beat 2 (1 beat long), a C major triad on beat 4 (2 beats), more notes later.
    juce::MidiMessageSequence seq;
    const auto add = [&seq](int note, double beat, double beats, float velocity = 1.0f) {
        seq.addEvent(juce::MidiMessage::noteOn(1, note, velocity), beat * kTicksPerBeat);
        seq.addEvent(juce::MidiMessage::noteOff(1, note), (beat + beats) * kTicksPerBeat);
    };
    add(69, 1.0, 1.0);
    for (int n : {60, 64, 67})
        add(n, 3.0, 2.0, 0.8f);
    for (int k = 0; k < 8; ++k)
        add(57 + (k % 4) * 3, 6.0 + k * 0.5, 0.4, 0.7f);
    for (int n : {57, 60, 64})
        add(n, 12.0, 3.5, 0.6f);
    const double spb = engine.getTransport().getSamplesPerBeat();
    const std::vector<NoteEvent> notes = notesFromMidi(seq, spb);
    const NoteEvent firstNote = notes.empty() ? NoteEvent{} : notes[0];
    std::printf("MIDI -> %zu notes; first: note %d at %lld for %lld samples\n", notes.size(), firstNote.note,
                static_cast<long long>(firstNote.start), static_cast<long long>(firstNote.length));
    DAWLAB_CHECK(notes.size() == 15);
    DAWLAB_CHECK(firstNote.start == 24000 && firstNote.length == 24000 && firstNote.note == 69);

    auto synthOwner = std::make_unique<SimpleSynth>();
    SimpleSynth& synth = *synthOwner;
    synth.setParameterByName("Waveform", 0.0f); // sine for the measurements
    synth.setParameterByName("Attack", 2.0f);
    synth.setParameterByName("Release", 50.0f);
    engine.addInsert(ChannelRef::track(0), 0, std::move(synthOwner));
    engine.applyPendingCommandsOffline();
    synth.setNotes(notes);

    // 1. onset: silence before sample 24000, sound right after it
    AudioBuffer a = render(engine, 0, 2 * 96000);
    int first = -1;
    for (int i = 0; i < a.getNumSamples() && first < 0; ++i)
        if (a.getSample(0, i) != 0.0f)
            first = i;
    std::printf("first non-zero output sample: %d (note-on at 24000)\n", first);
    DAWLAB_CHECK(first >= 24000 && first <= 24001);

    // 2. pitch: 440 Hz, not its neighbours
    const double a440 = goertzelAmplitude(a.getReadPointer(0) + 30000, 12000, 440.0, kSr);
    const double a466 = goertzelAmplitude(a.getReadPointer(0) + 30000, 12000, 466.16, kSr);
    std::printf("A4: 440 Hz %.1f dB, 466 Hz %.1f dB\n", 20.0 * std::log10(a440), 20.0 * std::log10(a466 + 1e-12));
    DAWLAB_CHECK(20.0 * std::log10(a440 / (a466 + 1e-12)) > 30.0);

    // 3. release: the note ends at 48000, release 50 ms = 2400 samples -> silent from 50400 on
    float tail = 0.0f;
    for (int i = 50410; i < 71000; ++i)
        tail = std::max(tail, std::abs(a.getSample(0, i)));
    DAWLAB_CHECK(tail == 0.0f);
    DAWLAB_CHECK(std::abs(a.getSample(0, 49000)) > 0.0f || std::abs(a.getSample(0, 49001)) > 0.0f);

    // 4. a triad = 3 voices
    Transport& tr = engine.getTransport();
    tr.setPosition(72000);
    tr.play();
    AudioBuffer blk(2, 512);
    for (int k = 0; k < 20; ++k)
        engine.processBlock(blk);
    std::printf("voices during the triad: %d\n", synth.getActiveVoices());
    DAWLAB_CHECK(synth.getActiveVoices() == 3);
    tr.stop();

    // 5. block-size invariance and 6. loop: bar 1 looped twice plays the A4 twice
    AudioBuffer b = render(engine, 0, 2 * 96000, 100);
    float worst = 0.0f;
    for (int i = 0; i < a.getNumSamples(); ++i)
        worst = std::max(worst, std::abs(a.getSample(0, i) - b.getSample(0, i)));
    std::printf("block size 512 vs 100: max difference %g\n", static_cast<double>(worst));
    DAWLAB_CHECK(worst == 0.0f);

    tr.setLoopRange(0, 96000);
    tr.setLoopEnabled(true);
    tr.setPosition(0);
    engine.requestReset();
    tr.play();
    AudioBuffer looped(2, 2 * 96000);
    for (int done = 0; done < looped.getNumSamples(); done += 512)
        engine.processBlock(looped.toBlock().getSubBlock(done, std::min(512, looped.getNumSamples() - done)));
    tr.stop();
    tr.setLoopEnabled(false);
    const double pass1 = goertzelAmplitude(looped.getReadPointer(0) + 30000, 12000, 440.0, kSr);
    const double pass2 = goertzelAmplitude(looped.getReadPointer(0) + 96000 + 30000, 12000, 440.0, kSr);
    std::printf("loop of bar 1: 440 Hz in pass 1 %.3f, pass 2 %.3f\n", pass1, pass2);
    DAWLAB_CHECK_NEAR(pass2, pass1, 1e-4);
    DAWLAB_CHECK(pass1 > 0.1);

    // A phrase to listen to: saw wave
    synth.setParameterByName("Waveform", 1.0f);
    synth.setParameterByName("Release", 300.0f);
    AudioBuffer phrase = render(engine, 0, 4 * 96000);
    DAWLAB_CHECK(!hasNanOrInf(phrase));
    writeWav(outputPath("l14_ex10_synth.wav"), phrase, kSr);
    return test::summary("l14_ex10");
}
