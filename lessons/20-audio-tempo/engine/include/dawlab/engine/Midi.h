#pragma once
// MIDI in the engine (chapter 18, Step 7): Steps 1-5 of this chapter, put where the engine needs them.
//
//   MidiMessage, ticks          Step 1   three bytes; notes live in ticks (960 to a quarter note) and are
//                                        played at the sample of their tick: sampleOfTick() from the tick,
//                                        firstTickAtOrAfter() its exact inverse for a segment's edges
//   MidiClip, ClipPlayer        Step 2   a window on notes; a table of what was started, so that a stop, a
//                                        seek, a loop wrap or a clip that goes away ends what sounds.
//                                        Here the player sees the engine's TimelineSegments: a segment that
//                                        does not continue the last one is a jump, and no segments is a stop
//   Voice, Synth                Steps 3-4 a PolyBLEP saw, a lowpass, an ADSR; a pool of voices with
//                                        stealing, the sustain pedal and pitch bend. The engine's synth ADDS
//                                        to its buffer
//   MidiParser, MidiFifo,       Step 5   for the host: bytes to messages, the MIDI thread to the audio
//   drainMidi()                          thread, arrival times to sample offsets
//   MidiBlock                            what the host hands processBlock(): one block's live messages
//
// Everything that runs on the audio thread has fixed sizes and allocates nothing. The vectors are reserved
// in prepare().

#include "Transport.h"

#include <dawlab/dsp/StateVariableFilter.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dawlab::engine {

// ---- Step 1: messages and ticks ------------------------------------------------------------------------
enum class MidiKind { NoteOn, NoteOff, Controller, PitchBend, Other };

struct MidiMessage {
    std::uint8_t status = 0;
    std::uint8_t data1 = 0;
    std::uint8_t data2 = 0;

    static MidiMessage noteOn(int channel, int note, int velocity) noexcept { return make(0x90, channel, note, velocity); }
    static MidiMessage noteOff(int channel, int note, int velocity = 0) noexcept { return make(0x80, channel, note, velocity); }
    static MidiMessage controller(int channel, int number, int value) noexcept { return make(0xB0, channel, number, value); }
    static MidiMessage pitchBend(int channel, int value) noexcept {
        const int v = std::clamp(value, -8192, 8191) + 8192;
        return make(0xE0, channel, v & 0x7F, v >> 7);
    }

    int channel() const noexcept { return status & 0x0F; }
    int note() const noexcept { return data1; }
    int velocity() const noexcept { return data2; }
    MidiKind kind() const noexcept {
        switch (status & 0xF0) {
        case 0x80: return MidiKind::NoteOff;
        case 0x90: return data2 == 0 ? MidiKind::NoteOff : MidiKind::NoteOn;
        case 0xB0: return MidiKind::Controller;
        case 0xE0: return MidiKind::PitchBend;
        default: return MidiKind::Other;
        }
    }
    int pitchBendValue() const noexcept { return ((data2 << 7) | data1) - 8192; }
    bool operator==(const MidiMessage& o) const noexcept { return status == o.status && data1 == o.data1 && data2 == o.data2; }

private:
    static MidiMessage make(int kind, int channel, int a, int b) noexcept {
        return {static_cast<std::uint8_t>(kind | (channel & 0x0F)), static_cast<std::uint8_t>(a & 0x7F), static_cast<std::uint8_t>(b & 0x7F)};
    }
};

constexpr int kTicksPerQuarter = Transport::kTicksPerBeat; // 960

inline double samplesPerTick(double sampleRate, double bpm) noexcept { return sampleRate * 60.0 / (bpm * kTicksPerQuarter); }

inline int64_t sampleOfTick(int64_t tick, double samplesPerTickValue) noexcept {
    return std::llround(static_cast<double>(tick) * samplesPerTickValue);
}

inline int64_t firstTickAtOrAfter(int64_t sample, double samplesPerTickValue) noexcept {
    int64_t t = static_cast<int64_t>(std::ceil(static_cast<double>(sample) / samplesPerTickValue));
    while (sampleOfTick(t - 1, samplesPerTickValue) >= sample)
        --t;
    return t;
}

// An event in an audio block: `offset` samples after the block's first sample.
struct BlockEvent {
    int offset = 0;
    MidiMessage message;
};

// One block's live MIDI, for processBlock(): sorted by offset, offsets inside the block.
struct MidiBlock {
    const BlockEvent* events = nullptr;
    int numEvents = 0;
};

// ---- Step 2: clips and the player ----------------------------------------------------------------------
struct Note {
    int64_t start = 0;  // ticks from the clip's own zero
    int64_t length = 1; // ticks, at least 1
    int pitch = 60;
    int velocity = 100;
    int channel = 0;
    bool operator==(const Note& o) const noexcept {
        return start == o.start && length == o.length && pitch == o.pitch && velocity == o.velocity && channel == o.channel;
    }
};

struct ControllerPoint {
    int64_t tick = 0;
    int number = 1;
    int value = 0;
    int channel = 0;
};

// A window on its material: material tick `offset` .. `offset + length` at timeline tick `start`. Notes and
// controller points are sorted by their tick.
struct MidiClip {
    int id = 0;          // stays the same through edits; never reused
    int64_t start = 0;   // timeline tick
    int64_t length = 0;  // ticks
    int64_t offset = 0;  // a trimmed start
    bool muted = false;
    std::vector<Note> notes;
    std::vector<ControllerPoint> controllers;

    int64_t end() const noexcept { return start + length; }
    int64_t timelineTick(int64_t t) const noexcept { return start + t - offset; }
    bool place(const Note& n, int64_t& on, int64_t& off) const noexcept {
        if (n.start < offset || n.start >= offset + length)
            return false;
        on = timelineTick(n.start);
        off = std::min(timelineTick(n.start + n.length), end());
        return true;
    }
};

class ClipPlayer {
public:
    static constexpr int kMaxSounding = 128;
    static constexpr int kMaxEventsPerSegment = 1024;

    ClipPlayer() { scratch_.reserve(kMaxEventsPerSegment); }

    // Audio thread. Appends the events of this block to `out` (not cleared), offsets in the block, in order
    // within each segment. No segments: the transport stopped - every sounding note ends at offset 0.
    // Chapter 19: a segment's ticks are its own (Transport::advance()), and its ticksJump says whether they go
    // on from the last segment's: a tempo change that moves the samples keeps the notes sounding.
    void process(const std::vector<MidiClip>* clips, const TimelineSegment* segments, int numSegments, const TempoMap& map,
                 double sampleRate, std::vector<BlockEvent>& out) noexcept {
        if (numSegments == 0) {
            allNotesOff(0, out);
            fresh_ = true;
            return;
        }
        static const std::vector<MidiClip> none;
        const std::vector<MidiClip>& list = clips != nullptr ? *clips : none;
        for (int s = 0; s < numSegments; ++s) {
            const TimelineSegment& seg = segments[s];
            if (seg.ticksJump || fresh_) { // a jump: play, seek, loop wrap - or the first segment it sees
                allNotesOff(seg.blockOffset, out);
                chase(list, seg.tickFrom, seg.blockOffset, out);
            }
            fresh_ = false;
            processSegment(list, seg, map, sampleRate, out);
        }
    }

    // A note off for everything that sounds, at `offset`.
    void allNotesOff(int offset, std::vector<BlockEvent>& out) noexcept {
        for (int i = 0; i < numSounding_; ++i)
            pushEvent(out, {offset, MidiMessage::noteOff(sounding_[static_cast<size_t>(i)].channel, sounding_[static_cast<size_t>(i)].pitch)});
        numSounding_ = 0;
    }
    // Forget everything (the track is gone, or prepare()). No events.
    void reset() noexcept {
        numSounding_ = 0;
        fresh_ = true;
    }

    int soundingCount() const noexcept { return numSounding_; }
    long droppedNotes() const noexcept { return dropped_; }

private:
    struct Sounding {
        int clipId = 0, channel = 0, pitch = 0;
        int64_t offTick = 0;
    };
    struct Pending {
        int64_t tick = 0;
        int rank = 0; // 0 note off, 1 controller, 2 note on
        int seq = 0;
        MidiMessage message;
    };

    static void pushEvent(std::vector<BlockEvent>& out, BlockEvent e) noexcept {
        if (out.size() < out.capacity())
            out.push_back(e);
    }

    void chase(const std::vector<MidiClip>& clips, int64_t tick, int offset, std::vector<BlockEvent>& out) const noexcept {
        for (const MidiClip& clip : clips) {
            if (clip.muted || tick < clip.start || tick >= clip.end())
                continue;
            const int64_t material = tick - clip.start + clip.offset;
            std::array<int16_t, 16 * 128> last;
            last.fill(-1);
            for (const ControllerPoint& p : clip.controllers) {
                if (p.tick >= material)
                    break;
                last[static_cast<size_t>((p.channel & 15) * 128 + (p.number & 127))] = static_cast<int16_t>(p.value);
            }
            for (size_t i = 0; i < last.size(); ++i)
                if (last[i] >= 0)
                    pushEvent(out, {offset, MidiMessage::controller(static_cast<int>(i / 128), static_cast<int>(i % 128), last[i])});
        }
    }

    void processSegment(const std::vector<MidiClip>& clips, const TimelineSegment& seg, const TempoMap& map, double sampleRate,
                        std::vector<BlockEvent>& out) noexcept {
        // Notes whose clip has gone or been muted end now.
        for (int i = 0; i < numSounding_;) {
            const Sounding& n = sounding_[static_cast<size_t>(i)];
            const auto clip = std::find_if(clips.begin(), clips.end(), [&n](const MidiClip& c) { return c.id == n.clipId; });
            if (clip == clips.end() || clip->muted) {
                pushEvent(out, {seg.blockOffset, MidiMessage::noteOff(n.channel, n.pitch)});
                sounding_[static_cast<size_t>(i)] = sounding_[static_cast<size_t>(--numSounding_)];
            } else {
                ++i;
            }
        }
        const int64_t t0 = seg.tickFrom;
        const int64_t t1 = seg.tickTo;
        scratch_.clear();
        int seq = 0;
        auto room = [this] { return scratch_.size() + kMaxSounding < scratch_.capacity(); };
        auto pend = [this, &seq](int64_t tick, int rank, MidiMessage m) { scratch_.push_back({tick, rank, seq++, m}); };
        for (const MidiClip& clip : clips) {
            if (clip.muted || clip.end() <= t0 || clip.start >= t1)
                continue;
            const int64_t first = t0 - clip.start + clip.offset;
            auto it = std::lower_bound(clip.notes.begin(), clip.notes.end(), first, [](const Note& n, int64_t t) { return n.start < t; });
            for (; it != clip.notes.end() && clip.timelineTick(it->start) < t1; ++it) {
                int64_t on = 0, off = 0;
                if (!clip.place(*it, on, off))
                    continue;
                if (numSounding_ == kMaxSounding || !room()) {
                    ++dropped_;
                    continue;
                }
                sounding_[static_cast<size_t>(numSounding_++)] = {clip.id, it->channel, it->pitch, off};
                pend(on, 2, MidiMessage::noteOn(it->channel, it->pitch, it->velocity));
            }
            for (const ControllerPoint& p : clip.controllers) {
                const int64_t t = clip.timelineTick(p.tick);
                if (p.tick >= clip.offset && p.tick < clip.offset + clip.length && t >= t0 && t < t1 && room())
                    pend(t, 1, MidiMessage::controller(p.channel, p.number, p.value));
            }
        }
        for (int i = 0; i < numSounding_;) {
            const Sounding& n = sounding_[static_cast<size_t>(i)];
            if (n.offTick < t1) {
                pend(std::max(n.offTick, t0), 0, MidiMessage::noteOff(n.channel, n.pitch));
                sounding_[static_cast<size_t>(i)] = sounding_[static_cast<size_t>(--numSounding_)];
            } else {
                ++i;
            }
        }
        std::sort(scratch_.begin(), scratch_.end(), [](const Pending& a, const Pending& b) {
            if (a.tick != b.tick)
                return a.tick < b.tick;
            if (a.rank != b.rank)
                return a.rank < b.rank;
            return a.seq < b.seq;
        });
        // At a loop's edge rounding may put a tick one sample outside its segment: keep it inside.
        for (const Pending& p : scratch_) {
            const int64_t at = std::clamp<int64_t>(map.sampleOfTick(p.tick, sampleRate) - seg.timelineStart, 0, seg.numSamples - 1);
            pushEvent(out, {static_cast<int>(at) + seg.blockOffset, p.message});
        }
    }

    std::array<Sounding, kMaxSounding> sounding_{};
    int numSounding_ = 0;
    bool fresh_ = true; // nothing played since reset() or a stop: the next segment starts with a chase
    long dropped_ = 0;
    std::vector<Pending> scratch_;
};

// ---- Steps 3-4: the synth ------------------------------------------------------------------------------------
inline double noteFrequency(int pitch, double bendSemitones = 0.0) noexcept {
    return 440.0 * std::pow(2.0, (pitch - 69 + bendSemitones) / 12.0);
}

inline double polyBlep(double t, double dt) noexcept {
    if (t < dt) {
        const double x = t / dt;
        return x + x - x * x - 1.0;
    }
    if (t > 1.0 - dt) {
        const double x = (t - 1.0) / dt;
        return x * x + x + x + 1.0;
    }
    return 0.0;
}

class Saw {
public:
    void setFrequency(double hz, double sampleRate) noexcept { dt_ = std::clamp(hz / sampleRate, 0.0, 0.5); }
    void reset(double phase = 0.0) noexcept { phase_ = phase; }
    float next() noexcept {
        const double v = 2.0 * phase_ - 1.0 - polyBlep(phase_, dt_);
        phase_ += dt_;
        if (phase_ >= 1.0)
            phase_ -= 1.0;
        return static_cast<float>(v);
    }

private:
    double phase_ = 0.0, dt_ = 0.0;
};

class Adsr {
public:
    enum class Stage { Idle, Attack, Decay, Sustain, Release };
    static constexpr double kMinSeconds = 0.001;

    void set(double sampleRate, double attack, double decay, double sustain, double release) noexcept {
        auto samples = [sampleRate](double s) { return std::max(s, kMinSeconds) * sampleRate; };
        attackSamples_ = samples(attack);
        decaySamples_ = samples(decay);
        sustain_ = std::clamp(sustain, 0.0, 1.0);
        releaseSamples_ = samples(release);
    }
    void noteOn() noexcept { stage_ = Stage::Attack; }
    void noteOff() noexcept { releaseOver(releaseSamples_); }
    void releaseOver(double samples) noexcept {
        if (stage_ == Stage::Idle)
            return;
        stage_ = Stage::Release;
        releaseStep_ = level_ / std::max(samples, 1.0);
    }
    // Silent at once (a track that is gone; prepare()).
    void reset() noexcept {
        stage_ = Stage::Idle;
        level_ = 0.0;
    }
    float next() noexcept {
        const auto out = static_cast<float>(level_);
        switch (stage_) {
        case Stage::Attack:
            level_ += 1.0 / attackSamples_;
            if (level_ >= 1.0) {
                level_ = 1.0;
                stage_ = Stage::Decay;
            }
            break;
        case Stage::Decay:
            level_ -= (1.0 - sustain_) / decaySamples_;
            if (level_ <= sustain_) {
                level_ = sustain_;
                stage_ = Stage::Sustain;
            }
            break;
        case Stage::Release:
            level_ -= releaseStep_;
            if (level_ <= 0.0) {
                level_ = 0.0;
                stage_ = Stage::Idle;
            }
            break;
        case Stage::Sustain:
        case Stage::Idle: break;
        }
        return out;
    }
    Stage stage() const noexcept { return stage_; }
    bool isActive() const noexcept { return stage_ != Stage::Idle; }

private:
    double attackSamples_ = 480.0, decaySamples_ = 4800.0, sustain_ = 0.7, releaseSamples_ = 9600.0;
    double level_ = 0.0, releaseStep_ = 0.0;
    Stage stage_ = Stage::Idle;
};

inline float velocityGain(int velocity) noexcept {
    const float v = static_cast<float>(std::clamp(velocity, 0, 127)) / 127.0f;
    return v * v;
}

struct VoiceSettings {
    double cutoffHz = 4000.0;
    double resonance = 0.9;
    double attack = 0.005, decay = 0.2, sustain = 0.6, release = 0.3;
    float level = 0.25f; // the whole instrument: a chord of eight voices still fits
};

class Voice {
public:
    static constexpr double kStealSeconds = 0.002;

    void prepare(double sampleRate, const VoiceSettings& s) {
        sampleRate_ = sampleRate;
        filter_.prepare(sampleRate, 512, 1);
        filter_.setType(dawlab::dsp::StateVariableFilter::Type::Lowpass);
        filter_.setCutoff(static_cast<float>(std::min(s.cutoffHz, 0.45 * sampleRate)));
        filter_.setQ(static_cast<float>(s.resonance));
        env_.set(sampleRate, s.attack, s.decay, s.sustain, s.release);
        level_ = s.level;
        reset();
    }
    // Audio thread: new settings for the notes that sound and the ones to come (the instrument's settings
    // panel, exercise 8). Nothing is reset; an envelope keeps its stage and level.
    void setSettings(const VoiceSettings& s) noexcept {
        filter_.setCutoff(static_cast<float>(std::min(s.cutoffHz, 0.45 * sampleRate_)));
        filter_.setQ(static_cast<float>(s.resonance));
        env_.set(sampleRate_, s.attack, s.decay, s.sustain, s.release);
        level_ = s.level;
    }
    void start(int pitch, int velocity) noexcept {
        stealing_ = false;
        if (!env_.isActive()) {
            saw_.reset(0.0);
            filter_.reset();
        }
        pitch_ = pitch;
        gain_ = velocityGain(velocity);
        saw_.setFrequency(noteFrequency(pitch, bend_), sampleRate_);
        env_.noteOn();
    }
    void release() noexcept {
        stealing_ = false;
        env_.noteOff();
    }
    void steal(int pitch, int velocity) noexcept {
        if (!env_.isActive()) {
            start(pitch, velocity);
            return;
        }
        env_.releaseOver(kStealSeconds * sampleRate_);
        stealing_ = true;
        pendingVelocity_ = velocity;
        pitch_ = pitch;
    }
    void setBend(double semitones) noexcept {
        bend_ = semitones;
        if (!stealing_)
            saw_.setFrequency(noteFrequency(pitch_, bend_), sampleRate_);
    }
    void reset() noexcept {
        env_.reset();
        stealing_ = false;
        pitch_ = -1;
        bend_ = 0.0;
    }
    void render(float* out, int numSamples) noexcept {
        for (int i = 0; i < numSamples; ++i) {
            if (stealing_ && !env_.isActive())
                start(pitch_, pendingVelocity_);
            if (!env_.isActive())
                return;
            out[i] += filter_.processSample(0, saw_.next()) * env_.next() * (gain_ * level_);
        }
    }
    bool isActive() const noexcept { return env_.isActive(); }
    bool isStealing() const noexcept { return stealing_; }
    bool isReleasing() const noexcept { return env_.stage() == Adsr::Stage::Release; }
    int pitch() const noexcept { return pitch_; }

private:
    double sampleRate_ = 48000.0;
    Saw saw_;
    dawlab::dsp::StateVariableFilter filter_;
    Adsr env_;
    float gain_ = 0.0f, level_ = 0.25f;
    int pitch_ = -1;
    double bend_ = 0.0;
    bool stealing_ = false;
    int pendingVelocity_ = 0;
};

class Synth {
public:
    static constexpr int kNumVoices = 16;
    static constexpr double kBendRange = 2.0;
    static constexpr int kSustainPedal = 64;
    static constexpr int kAllNotesOff = 123;

    void prepare(double sampleRate, const VoiceSettings& settings) {
        for (Voice& v : voices_)
            v.prepare(sampleRate, settings);
        reset();
    }
    // Audio thread: see Voice::setSettings().
    void setSettings(const VoiceSettings& settings) noexcept {
        for (Voice& v : voices_)
            v.setSettings(settings);
    }
    // Silent at once: every voice, the pedal, the bend.
    void reset() noexcept {
        for (Voice& v : voices_)
            v.reset();
        slots_ = {};
        pedal_ = false;
        bend_ = 0.0;
    }
    // ADDS numSamples to out, cut at the events (sorted by offset).
    void render(const BlockEvent* events, int numEvents, float* out, int numSamples) noexcept {
        int done = 0;
        auto renderTo = [this, out, &done](int to) {
            for (Voice& v : voices_)
                v.render(out + done, to - done);
            done = to;
        };
        for (int e = 0; e < numEvents; ++e) {
            renderTo(std::clamp(events[e].offset, done, numSamples));
            handle(events[e].message);
        }
        renderTo(numSamples);
    }
    void handle(const MidiMessage& m) noexcept {
        switch (m.kind()) {
        case MidiKind::NoteOn: {
            const int i = allocate(m.note());
            Voice& v = voices_[static_cast<size_t>(i)];
            if (v.isActive() && v.pitch() != m.note()) {
                v.steal(m.note(), m.velocity());
                ++stolen_;
            } else {
                v.start(m.note(), m.velocity());
            }
            slots_[static_cast<size_t>(i)] = {true, false, ++clock_, 0};
            break;
        }
        case MidiKind::NoteOff:
            for (size_t i = 0; i < voices_.size(); ++i) {
                Slot& s = slots_[i];
                if (!voices_[i].isActive() || voices_[i].pitch() != m.note() || !s.keyDown)
                    continue;
                s.keyDown = false;
                if (pedal_) {
                    s.sustained = true;
                } else {
                    voices_[i].release();
                    s.released = ++clock_;
                }
            }
            break;
        case MidiKind::Controller:
            if (m.note() == kSustainPedal) {
                const bool down = m.velocity() >= 64;
                if (!down)
                    for (size_t i = 0; i < voices_.size(); ++i)
                        if (slots_[i].sustained) {
                            slots_[i].sustained = false;
                            voices_[i].release();
                            slots_[i].released = ++clock_;
                        }
                pedal_ = down;
            } else if (m.note() == kAllNotesOff) {
                for (size_t i = 0; i < voices_.size(); ++i) {
                    if (voices_[i].isActive() && (!voices_[i].isReleasing() || voices_[i].isStealing())) {
                        voices_[i].release();
                        slots_[i].released = ++clock_;
                    }
                    slots_[i].keyDown = false;
                    slots_[i].sustained = false;
                }
                pedal_ = false;
            }
            break;
        case MidiKind::PitchBend:
            bend_ = m.pitchBendValue() / 8192.0 * kBendRange;
            for (Voice& v : voices_)
                v.setBend(bend_);
            break;
        case MidiKind::Other: break;
        }
    }
    int activeVoices() const noexcept {
        return static_cast<int>(std::count_if(voices_.begin(), voices_.end(), [](const Voice& v) { return v.isActive(); }));
    }
    long stolen() const noexcept { return stolen_; }

private:
    struct Slot {
        bool keyDown = false, sustained = false;
        long started = 0, released = 0;
    };
    int allocate(int pitch) const noexcept {
        for (size_t i = 0; i < voices_.size(); ++i)
            if (voices_[i].isActive() && voices_[i].pitch() == pitch)
                return static_cast<int>(i);
        for (size_t i = 0; i < voices_.size(); ++i)
            if (!voices_[i].isActive())
                return static_cast<int>(i);
        int best = -1;
        for (size_t i = 0; i < voices_.size(); ++i)
            if (voices_[i].isReleasing() && !slots_[i].keyDown &&
                (best < 0 || slots_[i].released < slots_[static_cast<size_t>(best)].released))
                best = static_cast<int>(i);
        if (best >= 0)
            return best;
        best = 0;
        for (size_t i = 1; i < voices_.size(); ++i)
            if (slots_[i].started < slots_[static_cast<size_t>(best)].started)
                best = static_cast<int>(i);
        return best;
    }

    std::array<Voice, kNumVoices> voices_{};
    std::array<Slot, kNumVoices> slots_{};
    long clock_ = 0, stolen_ = 0;
    bool pedal_ = false;
    double bend_ = 0.0;
};

// ---- what a track plays its notes with (added after the chapter, for exercises 8 and 12) ------------------
// Every slot of the engine has the built-in synth, as a SynthInstrument. Engine::setTrackInstrument() can put
// another Instrument in its place - a VST3 instrument, say - and take it out again.
//
// Threading contract (as InsertEffect's):
//   prepare()   non-audio thread, may allocate; never while the instrument is in a running engine
//               (Engine::setTrackInstrument() prepares it for you).
//   reset(), render()  audio thread only.
class Instrument {
public:
    virtual ~Instrument() = default;
    virtual void prepare(double sampleRate, int maxBlockSize) = 0;
    // Silent at once, nothing remembered.
    virtual void reset() noexcept = 0;
    // The block's events (sorted by offset) through the instrument; its sound ADDED to out[0 .. numChannels).
    virtual void render(const BlockEvent* events, int numEvents, float* const* out, int numChannels, int numSamples) noexcept = 0;
    // Chapter 19: how many samples after a note its sound comes (a sampler that reads ahead). Audio thread; the
    // track's latency includes it, and the delay compensation waits for it (Step 3).
    virtual int getLatencySamples() const noexcept { return 0; }
};

// The built-in synth as an Instrument. Its settings can change while it plays: setSettings() may be called on
// any thread, and the next block hands them to the voices. Each value is an atomic of its own, so for one block
// a change may be half applied; the next block applies the rest.
class SynthInstrument final : public Instrument {
public:
    explicit SynthInstrument(const VoiceSettings& settings = {}) noexcept { setSettings(settings); }

    void setSettings(const VoiceSettings& s) noexcept {
        cutoffHz_.store(s.cutoffHz, std::memory_order_relaxed);
        resonance_.store(s.resonance, std::memory_order_relaxed);
        attack_.store(s.attack, std::memory_order_relaxed);
        decay_.store(s.decay, std::memory_order_relaxed);
        sustain_.store(s.sustain, std::memory_order_relaxed);
        release_.store(s.release, std::memory_order_relaxed);
        level_.store(s.level, std::memory_order_relaxed);
        version_.fetch_add(1, std::memory_order_release);
    }
    VoiceSettings getSettings() const noexcept {
        VoiceSettings s;
        s.cutoffHz = cutoffHz_.load(std::memory_order_relaxed);
        s.resonance = resonance_.load(std::memory_order_relaxed);
        s.attack = attack_.load(std::memory_order_relaxed);
        s.decay = decay_.load(std::memory_order_relaxed);
        s.sustain = sustain_.load(std::memory_order_relaxed);
        s.release = release_.load(std::memory_order_relaxed);
        s.level = level_.load(std::memory_order_relaxed);
        return s;
    }

    void prepare(double sampleRate, int maxBlockSize) override {
        applied_ = version_.load(std::memory_order_acquire);
        synth_.prepare(sampleRate, getSettings());
        mono_.assign(static_cast<size_t>(std::max(1, maxBlockSize)), 0.0f);
    }
    void reset() noexcept override { synth_.reset(); }
    void render(const BlockEvent* events, int numEvents, float* const* out, int numChannels, int numSamples) noexcept override {
        const std::uint32_t version = version_.load(std::memory_order_acquire);
        if (version != applied_) {
            applied_ = version;
            synth_.setSettings(getSettings());
        }
        const int n = std::min(numSamples, static_cast<int>(mono_.size()));
        float* mono = mono_.data();
        std::fill(mono, mono + n, 0.0f);
        synth_.render(events, numEvents, mono, n);
        for (int ch = 0; ch < numChannels; ++ch)
            for (int i = 0; i < n; ++i)
                out[ch][i] += mono[i];
    }
    const Synth& synth() const noexcept { return synth_; }

private:
    Synth synth_;
    std::vector<float> mono_;
    std::atomic<double> cutoffHz_{0.0}, resonance_{0.0}, attack_{0.0}, decay_{0.0}, sustain_{0.0}, release_{0.0};
    std::atomic<float> level_{0.0f};
    std::atomic<std::uint32_t> version_{0};
    std::uint32_t applied_ = 0; // audio thread
};

// ---- Step 5: for the host ---------------------------------------------------------------------------------
inline int midiDataBytesOf(std::uint8_t status) noexcept {
    const int kind = status & 0xF0;
    return kind == 0xC0 || kind == 0xD0 ? 1 : 2;
}

class MidiParser {
public:
    bool feed(std::uint8_t byte, MidiMessage& out) noexcept {
        if (byte >= 0xF8)
            return false;
        if (byte >= 0xF0) {
            sysex_ = byte == 0xF0;
            running_ = 0;
            have_ = 0;
            return false;
        }
        if (byte >= 0x80) {
            sysex_ = false;
            running_ = byte;
            have_ = 0;
            return false;
        }
        if (sysex_)
            return false;
        if (running_ == 0) {
            ++skipped_;
            return false;
        }
        data_[have_++] = byte;
        if (have_ < midiDataBytesOf(running_))
            return false;
        out = {running_, data_[0], have_ == 2 ? data_[1] : std::uint8_t{0}};
        have_ = 0;
        return true;
    }
    long skipped() const noexcept { return skipped_; }

private:
    std::uint8_t running_ = 0;
    std::uint8_t data_[2] = {0, 0};
    int have_ = 0;
    bool sysex_ = false;
    long skipped_ = 0;
};

struct TimedMidi {
    double time = 0.0; // seconds, on the clock the driver and the audio callback share
    MidiMessage message;
};

class MidiFifo {
public:
    static constexpr std::size_t kCapacity = 1024;

    bool push(const TimedMidi& m) noexcept {
        const uint64_t w = written_.load(std::memory_order_relaxed);
        if (w - read_.load(std::memory_order_acquire) == kCapacity) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        items_[w & (kCapacity - 1)] = m;
        written_.store(w + 1, std::memory_order_release);
        return true;
    }
    bool pop(TimedMidi& m) noexcept {
        const uint64_t r = read_.load(std::memory_order_relaxed);
        if (r == written_.load(std::memory_order_acquire))
            return false;
        m = items_[r & (kCapacity - 1)];
        read_.store(r + 1, std::memory_order_release);
        return true;
    }
    const TimedMidi* peek() const noexcept {
        const uint64_t r = read_.load(std::memory_order_relaxed);
        if (r == written_.load(std::memory_order_acquire))
            return nullptr;
        return &items_[r & (kCapacity - 1)];
    }
    long dropped() const noexcept { return dropped_.load(std::memory_order_relaxed); }

private:
    std::array<TimedMidi, kCapacity> items_{};
    std::atomic<uint64_t> written_{0};
    std::atomic<uint64_t> read_{0};
    std::atomic<long> dropped_{0};
};

enum class MidiTiming { AsSoonAsPossible, Scheduled };

// The host's audio callback, before processBlock(): this block's live messages from the FIFO into `out`.
// blockTime: when the block's first sample will be heard. Scheduled: each message at time + delay; later ones
// stay for a later block, late ones go to offset 0 and are counted.
inline void drainMidi(MidiFifo& fifo, double blockTime, int numSamples, double sampleRate, MidiTiming timing, double delay,
                      std::vector<BlockEvent>& out, long& late) noexcept {
    out.clear();
    TimedMidi m;
    if (timing == MidiTiming::AsSoonAsPossible) {
        while (out.size() < out.capacity() && fifo.pop(m))
            out.push_back({0, m.message});
        return;
    }
    while (out.size() < out.capacity()) {
        const TimedMidi* next = fifo.peek();
        if (next == nullptr)
            break;
        const auto offset = static_cast<long long>(std::floor((next->time + delay - blockTime) * sampleRate));
        if (offset >= numSamples)
            break;
        if (offset < 0)
            ++late;
        out.push_back({static_cast<int>(std::max<long long>(offset, 0)), next->message});
        fifo.pop(m);
    }
}

} // namespace dawlab::engine
