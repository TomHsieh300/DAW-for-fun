#pragma once
// minidaw.h - Step 2 snapshot of chapter 18: playing a MIDI clip.
//
// Step 1 played a list of events. A DAW plays clips: notes with a start and a length, in a window of the
// timeline, under a transport that loops, jumps and stops. Every note on needs exactly one note off, and the
// sequence alone does not give it in these cases:
//
//   - a note that is longer than its clip           it ends at the clip's end
//   - the loop wraps, the user seeks                every sounding note ends where the jump happens
//   - the transport stops                           every sounding note ends in that block
//   - a clip is muted or deleted while it sounds    its notes end in the next block
//
// So the player keeps a table of what it started - the sounding notes - and ends them itself. A note off
// that nobody started is never sent.
//
// Controllers are different: a controller has a value, not a start and an end. After a jump the instrument
// still has the value from wherever the song was before, so the player *chases* it: it sends the value each
// controller had at the new position. Notes are not chased - a note started half-way through sounds wrong.
//
// YOUR TURN (Step 2): MidiClip::place(), ClipPlayer::allNotesOff(), chase() and process() in
// starter/step02.cpp.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace minidaw {

// ---- Step 1 (solved) ----------------------------------------------------------------------------------
enum class Kind { NoteOn, NoteOff, Controller, PitchBend, Other };

struct MidiMessage {
    std::uint8_t status = 0;
    std::uint8_t data1 = 0;
    std::uint8_t data2 = 0;

    static MidiMessage noteOn(int channel, int note, int velocity) noexcept { return make(0x90, channel, note, velocity); }
    static MidiMessage noteOff(int channel, int note, int velocity = 0) noexcept { return make(0x80, channel, note, velocity); }
    static MidiMessage controller(int channel, int number, int value) noexcept { return make(0xB0, channel, number, value); }

    int channel() const noexcept { return status & 0x0F; }
    int note() const noexcept { return data1; }
    int velocity() const noexcept { return data2; }
    Kind kind() const noexcept {
        switch (status & 0xF0) {
        case 0x80: return Kind::NoteOff;
        case 0x90: return data2 == 0 ? Kind::NoteOff : Kind::NoteOn;
        case 0xB0: return Kind::Controller;
        case 0xE0: return Kind::PitchBend;
        default: return Kind::Other;
        }
    }

private:
    static MidiMessage make(int kind, int channel, int a, int b) noexcept {
        return {static_cast<std::uint8_t>(kind | (channel & 0x0F)), static_cast<std::uint8_t>(a & 0x7F), static_cast<std::uint8_t>(b & 0x7F)};
    }
};

constexpr int kTicksPerQuarter = 960;

struct Tempo {
    double sampleRate = 48000.0;
    double bpm = 120.0;
    double samplesPerTick() const noexcept { return sampleRate * 60.0 / (bpm * kTicksPerQuarter); }
};

inline std::int64_t sampleOfTick(std::int64_t tick, const Tempo& tempo) noexcept {
    return std::llround(static_cast<double>(tick) * tempo.samplesPerTick());
}

inline std::int64_t firstTickAtOrAfter(std::int64_t sample, const Tempo& tempo) noexcept {
    std::int64_t t = static_cast<std::int64_t>(std::ceil(static_cast<double>(sample) / tempo.samplesPerTick()));
    while (sampleOfTick(t - 1, tempo) >= sample)
        --t;
    return t;
}

struct TimedEvent {
    std::int64_t tick = 0;
    MidiMessage message;
};

inline void sortEvents(std::vector<TimedEvent>& events) {
    std::stable_sort(events.begin(), events.end(), [](const TimedEvent& a, const TimedEvent& b) {
        if (a.tick != b.tick)
            return a.tick < b.tick;
        return a.message.kind() == Kind::NoteOff && b.message.kind() != Kind::NoteOff;
    });
}

struct BlockEvent {
    int offset = 0;
    MidiMessage message;
};

inline void eventsInBlock(const std::vector<TimedEvent>& sequence, std::int64_t blockStart, int numSamples, const Tempo& tempo,
                          std::vector<BlockEvent>& out) {
    out.clear();
    const std::int64_t from = firstTickAtOrAfter(blockStart, tempo);
    const std::int64_t to = firstTickAtOrAfter(blockStart + numSamples, tempo);
    auto it = std::lower_bound(sequence.begin(), sequence.end(), from, [](const TimedEvent& e, std::int64_t t) { return e.tick < t; });
    for (; it != sequence.end() && it->tick < to; ++it)
        out.push_back({static_cast<int>(sampleOfTick(it->tick, tempo) - blockStart), it->message});
}

// ---- Step 2: clips --------------------------------------------------------------------------------------
// A note in a clip, in ticks from the clip's own zero (the start of the material, not of the window).
struct Note {
    std::int64_t start = 0;
    std::int64_t length = 1; // at least 1
    int pitch = 60;
    int velocity = 100;
    int channel = 0;
};

// A controller move in a clip, in ticks from the clip's own zero.
struct ControllerPoint {
    std::int64_t tick = 0;
    int number = 1;
    int value = 0;
    int channel = 0;
};

// The window: the clip shows its material from `offset` to `offset + length`, at timeline tick `start`.
// Notes and points are sorted by their tick.
struct MidiClip {
    int id = 0;                  // stays the same when the clip is edited; never reused
    std::int64_t start = 0;      // timeline tick of the window's first tick
    std::int64_t length = 0;     // ticks
    std::int64_t offset = 0;     // the material's tick at the window's first tick (a trimmed start)
    bool muted = false;
    std::vector<Note> notes;
    std::vector<ControllerPoint> controllers;

    std::int64_t end() const noexcept { return start + length; }
    // The timeline tick of material tick t.
    std::int64_t timelineTick(std::int64_t t) const noexcept { return start + t - offset; }

    // ---- YOUR TURN (starter/step02.cpp) ----------------------------------------------------------
    // Where note n plays on the timeline: [on, off). False if it does not start inside the window (a note
    // that starts before a trimmed start is not played); a note longer than the window ends at its end.
    bool place(const Note& n, std::int64_t& on, std::int64_t& off) const noexcept;
};

// ---- provided: a transport that plays, stops, seeks and loops ---------------------------------------------
// A block is one or two straight pieces of the timeline: two when the loop wraps inside it. `jump` marks a
// piece that does not continue the one before: the first after play() or seek(), and the one after a wrap.
struct Segment {
    int offset = 0;                 // in the block
    int numSamples = 0;
    std::int64_t timelineSample = 0; // the timeline sample of its first sample
    bool jump = false;
};

struct Block {
    int numSamples = 0;
    bool playing = false;
    int numSegments = 0;
    Segment segments[2];
};

class MiniTransport {
public:
    void play() noexcept {
        if (!playing_)
            jump_ = true;
        playing_ = true;
    }
    void stop() noexcept { playing_ = false; }
    void seek(std::int64_t sample) noexcept {
        position_ = std::max<std::int64_t>(0, sample);
        jump_ = true;
    }
    // A loop must be at least as long as a block: a block wraps at most once.
    void setLoop(std::int64_t from, std::int64_t to, bool on) noexcept {
        loopFrom_ = from;
        loopTo_ = to;
        loop_ = on && to > from;
    }
    std::int64_t position() const noexcept { return position_; }
    bool isPlaying() const noexcept { return playing_; }

    Block advance(int numSamples) noexcept {
        Block b;
        b.numSamples = numSamples;
        b.playing = playing_;
        if (!playing_)
            return b;
        if (loop_ && position_ == loopTo_) { // the last block ended exactly at the loop's end
            position_ = loopFrom_;
            jump_ = true;
        }
        if (loop_ && position_ < loopTo_ && position_ + numSamples > loopTo_) {
            const int first = static_cast<int>(loopTo_ - position_);
            b.segments[0] = {0, first, position_, jump_};
            b.segments[1] = {first, numSamples - first, loopFrom_, true};
            b.numSegments = 2;
            position_ = loopFrom_ + (numSamples - first);
        } else {
            b.segments[0] = {0, numSamples, position_, jump_};
            b.numSegments = 1;
            position_ += numSamples;
        }
        jump_ = false;
        return b;
    }

private:
    std::int64_t position_ = 0, loopFrom_ = 0, loopTo_ = 0;
    bool playing_ = false, loop_ = false, jump_ = false;
};

// ---- Step 2: the player ---------------------------------------------------------------------------------
// Runs on the audio thread: process() gets the clips and a block, and writes the block's MIDI events in
// order of offset (at one offset: note offs first). No allocation: the tables have fixed sizes, and `out`
// and the scratch list are reserved by the caller / by the constructor.
class ClipPlayer {
public:
    static constexpr int kMaxSounding = 128; // more sounding notes than that: the new ones are not played
    static constexpr int kMaxEventsPerSegment = 1024;

    // A note the player started and has not ended yet.
    struct Sounding {
        int clipId = 0;
        int channel = 0;
        int pitch = 0;
        std::int64_t offTick = 0; // the timeline tick where it ends
    };

    ClipPlayer() { scratch_.reserve(kMaxEventsPerSegment); }

    // ---- YOUR TURN ------------------------------------------------------------------------------------
    // A note off at `offset` for every sounding note; the table is empty afterwards.
    void allNotesOff(int offset, std::vector<BlockEvent>& out) noexcept;
    // For every clip that is not muted and whose window contains `tick`: for every controller (number and
    // channel) that has a point in the clip's material before the material tick of `tick` - before the
    // window too - the value of the last such point, at `offset`. In order of clip, then channel, then number.
    // A point exactly at `tick` is not chased: the segment that starts there sends it.
    void chase(const std::vector<MidiClip>& clips, std::int64_t tick, int offset, std::vector<BlockEvent>& out) const noexcept;
    // One block:
    //   not playing        if anything sounds: allNotesOff(0)
    //   every segment      a jump: allNotesOff and chase at the segment's offset first. Then the
    //                      segment's ticks [firstTickAtOrAfter(start), firstTickAtOrAfter(end)):
    //                        - sounding notes whose clip is gone or muted: off at the segment's offset
    //                        - note ons of notes that start in those ticks (place()), added to the table
    //                          (when the table is full, not played, and droppedNotes() counts it)
    //                        - note offs of sounding notes whose offTick is in those ticks, also of the ones
    //                          just started; they leave the table
    //                        - controller points of unmuted clips in those ticks, inside the window (when the
    //                          list is full, not sent, and droppedEvents() counts it)
    //                      all at offset sampleOfTick(tick) - segment's timeline sample + segment's offset,
    //                      in order of tick; at one tick note offs, then controllers, then note ons (a note
    //                      starts with its controllers set), each kind in the order found.
    // `out` is cleared first.
    void process(const std::vector<MidiClip>& clips, const Block& block, const Tempo& tempo, std::vector<BlockEvent>& out) noexcept;

    // ---- provided ---------------------------------------------------------------------------------------
    int soundingCount() const noexcept { return numSounding_; }
    const Sounding& sounding(int i) const noexcept { return sounding_[static_cast<size_t>(i)]; }
    long droppedNotes() const noexcept { return dropped_; }
    // Controller points that found no room in a segment's list, and were not sent.
    long droppedEvents() const noexcept { return droppedEvents_; }

private:
    std::array<Sounding, kMaxSounding> sounding_{};
    int numSounding_ = 0;
    long dropped_ = 0;
    long droppedEvents_ = 0;
    // Collected events of one segment, before they are put in order: the tick, and a rank that orders the
    // kinds at one tick, and the order found within a kind.
    struct Pending {
        std::int64_t tick = 0;
        int rank = 0; // 0 = note off, 1 = controller, 2 = note on
        int seq = 0;  // the order found
        MidiMessage message;
    };
    std::vector<Pending> scratch_;
};

} // namespace minidaw
