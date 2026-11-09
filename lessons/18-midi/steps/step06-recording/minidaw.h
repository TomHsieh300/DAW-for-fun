#pragma once
// minidaw.h - Step 6 snapshot of chapter 18: recording MIDI.
//
// Step 5 brought a keyboard's messages to the audio thread with the time each one arrived. Recording puts them
// into a clip, as notes, where they were played:
//
//   - Where in the song was the player when a key went down? Where the song was *heard*. Unlike an audio take
//     (chapter 17) there is no input latency: the time stamp is taken when the message arrives. Only the output
//     latency (the device: in blockTime) and the mix's latency (plugin delay compensation) count.
//   - A loop records pass after pass. Overdub keeps everything; Replace lets each pass erase what it plays over,
//     so the last pass that covered a place is what is left there.
//   - Quantize moves notes to a grid - by a strength, with swing - and never loses what was played: it is a
//     function of the played notes, so it can be changed or taken back at any time.
//
// No threads here: the recorder gets the messages in order with their times.
//
// YOUR TURN (Step 6): songSampleAt(), tickAt(), NoteRecorder::noteOn(), noteOff(), stop(), mergeTake() and
// quantize() in starter/step06.cpp.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace minidaw {

constexpr int kTicksPerQuarter = 960;

struct Tempo {
    double sampleRate = 48000.0;
    double bpm = 120.0;
    double samplesPerTick() const noexcept { return sampleRate * 60.0 / (bpm * kTicksPerQuarter); }
};

// Step 2's note: ticks from the clip's zero (here the clip starts at the song's zero).
struct Note {
    std::int64_t start = 0;
    std::int64_t length = 1;
    int pitch = 60;
    int velocity = 100;
    int channel = 0;
    bool operator==(const Note& o) const noexcept {
        return start == o.start && length == o.length && pitch == o.pitch && velocity == o.velocity && channel == o.channel;
    }
};

// ---- Step 6: where the player was --------------------------------------------------------------------------
// What the audio thread knows about one block: when its first sample will be heard (Step 5's blockTime, which
// includes the device's output latency), which song sample it rendered there, and the mix's latency.
struct Anchor {
    double blockTime = 0.0;
    std::int64_t songSample = 0;
    double sampleRate = 48000.0;
    int mixLatency = 0; // samples: what is rendered at songSample is heard mixLatency samples after blockTime
};

// ---- YOUR TURN (starter/step06.cpp) -------------------------------------------------------------------------
// The song sample the player heard at `time`, rounded to the nearest sample:
//     songSample + (time - blockTime) x sampleRate - mixLatency
// Any anchor of the same straight run of playing will do: the song moves at the sample rate.
std::int64_t songSampleAt(double time, const Anchor& anchor) noexcept;
// The nearest tick to a sample (a recorded note lands on the tick closest to where it was played).
std::int64_t tickAt(std::int64_t sample, const Tempo& tempo) noexcept;

// ---- Step 6: recording -----------------------------------------------------------------------------------------
// The loop, in ticks. Off when to <= from.
struct LoopRange {
    std::int64_t from = 0, to = 0;
    bool on() const noexcept { return to > from; }
    std::int64_t length() const noexcept { return to - from; }
};

// What pass p of a recording covered: [from, to) in song ticks.
struct Coverage {
    int pass = 0;
    std::int64_t from = 0, to = 0;
};

struct TakeNote {
    Note note;
    int pass = 0;
};

// The recorder gets *unwrapped* ticks: song time as it was played, going on past the loop's end (pass 1 of a loop
// [0, 3840) is 3840 .. 7679, pass 2 is 7680 .. and so on). It wraps them itself.
class NoteRecorder {
public:
    static constexpr int kMaxOpen = 128;

    // ---- provided ---------------------------------------------------------------------------------------------
    // Recording starts at song tick `startTick` (inside the loop when the loop is on).
    void start(std::int64_t startTick, LoopRange loop) {
        start_ = startTick;
        loop_ = loop;
        notes_.clear();
        passes_.clear();
        open_.clear();
    }
    // Pass and song tick of an unwrapped tick. Pass 0 runs from the start to the loop's end; pass k >= 1 is the
    // k-th time round the whole loop.
    int passOf(std::int64_t u) const noexcept {
        if (!loop_.on() || u < loop_.to)
            return 0;
        return static_cast<int>((u - loop_.to) / loop_.length()) + 1;
    }
    std::int64_t songTickOf(std::int64_t u) const noexcept {
        if (!loop_.on() || u < loop_.to)
            return u;
        return loop_.from + (u - loop_.to) % loop_.length();
    }
    // The unwrapped tick where pass p ends (the loop's end, or never without a loop).
    std::int64_t passEnd(int p) const noexcept {
        if (!loop_.on())
            return INT64_MAX;
        return loop_.to + static_cast<std::int64_t>(p) * loop_.length();
    }
    const std::vector<TakeNote>& notes() const noexcept { return notes_; }
    const std::vector<Coverage>& passes() const noexcept { return passes_; }

    // ---- YOUR TURN --------------------------------------------------------------------------------------------
    // A key down at unwrapped tick u. A key of that pitch and channel that is still down is closed here first (a
    // keyboard that sends two note ons). Velocity 0 is a note off.
    void noteOn(std::int64_t u, int pitch, int velocity, int channel);
    // A key up: closes the open note of that pitch and channel; nothing if there is none. A note becomes a
    // TakeNote: its start is the song tick of its note on, its pass the pass of its note on, and its length
    // reaches the note off - but not past the end of its pass (a note held over the loop's end ends there).
    // At least 1 tick.
    void noteOff(std::int64_t u, int pitch, int channel);
    // Recording stops at unwrapped tick u: every key still down is closed there, and passes() gets what each pass
    // covered, in song ticks: pass 0 from the start, the others from the loop's start, each to its end or to the
    // stop. A pass that covered nothing (stopped exactly at its start) is left out.
    void stop(std::int64_t u);

private:
    struct Open {
        std::int64_t on = 0;
        int pitch = 0, velocity = 0, channel = 0;
    };
    std::int64_t start_ = 0;
    LoopRange loop_;
    std::vector<TakeNote> notes_;
    std::vector<Coverage> passes_;
    std::vector<Open> open_;
};

enum class RecordMode {
    Overdub, // keep every pass, and what the clip had
    Replace, // each pass erases what it plays over
};

// ---- YOUR TURN ------------------------------------------------------------------------------------------------
// The clip after the recording, sorted by start (stable: equal starts keep the order existing, then the take's).
//   Overdub   the existing notes and every recorded note.
//   Replace   a note survives unless a LATER pass covered its start: an existing note (which counts as pass -1)
//             unless any pass did, a note of pass p unless a pass after p did.
std::vector<Note> mergeTake(const std::vector<Note>& existing, const NoteRecorder& take, RecordMode mode);

// ---- Step 6: quantize ------------------------------------------------------------------------------------------
struct QuantizeSettings {
    std::int64_t grid = 240;  // ticks: 240 = a sixteenth note
    double strength = 1.0;    // 0 = as played, 1 = on the grid
    double swing = 0.0;       // 0 .. 0.5: every second grid line is later by this fraction of the grid
};

// ---- YOUR TURN ------------------------------------------------------------------------------------------------
// The notes moved towards the grid. Only the start moves; the length stays. For each note:
//     line   = the nearest grid line, n = start / grid rounded to the nearest (n x grid)
//     target = line, plus llround(swing x grid) when n is odd
//     start  = llround(start + strength x (target - start)), at least 0
// The result is sorted by start (stable). `played` is not changed: quantize is computed from it every time.
std::vector<Note> quantize(const std::vector<Note>& played, const QuantizeSettings& settings);

} // namespace minidaw
