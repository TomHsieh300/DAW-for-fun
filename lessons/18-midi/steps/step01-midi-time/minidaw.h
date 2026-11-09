#pragma once
// Chapter 18, Step 1: MIDI events and musical time.
//
// The model of this step:
//
//   MidiMessage           three bytes: a status byte (kind + channel) and two data bytes
//   Tempo                 how many samples one tick lasts: notes live in ticks (960 to a quarter note),
//                         the audio thread in samples
//   sampleOfTick()        tick -> sample, from the absolute tick every time (no running sum to drift)
//   firstTickAtOrAfter()  its exact inverse for a block's edges
//   sortEvents()          a sequence in playing order: at one tick, note-offs before note-ons
//   eventsInBlock()       the events of one audio block, each at its sample offset in the block
//   renderBlock()         an instrument rendered in pieces, cut at every event: sample-accurate
//
// No JUCE, no threads. Header-only: the starter and the solution define the functions marked
// YOUR TURN.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace minidaw {

// ---- MIDI messages ------------------------------------------------------------------------------
// A channel message is a status byte 0x80-0xEF (high nibble: the kind, low nibble: the channel 0-15)
// and up to two data bytes 0-127.
//
//   0x8n note velocity    note off
//   0x9n note velocity    note on - with velocity 0 it is a note off (keyboards send it that way)
//   0xBn number value     controller (64 = sustain pedal, 1 = mod wheel, ...)
//   0xEn lsb msb          pitch bend: 14 bits, (msb << 7 | lsb), 8192 = centre
enum class Kind { NoteOn, NoteOff, Controller, PitchBend, Other };

struct MidiMessage {
    std::uint8_t status = 0;
    std::uint8_t data1 = 0;
    std::uint8_t data2 = 0;

    static MidiMessage noteOn(int channel, int note, int velocity) noexcept { return make(0x90, channel, note, velocity); }
    static MidiMessage noteOff(int channel, int note, int velocity = 0) noexcept { return make(0x80, channel, note, velocity); }
    static MidiMessage controller(int channel, int number, int value) noexcept { return make(0xB0, channel, number, value); }
    // value -8192 .. 8191, 0 = no bend
    static MidiMessage pitchBend(int channel, int value) noexcept {
        const int v = std::clamp(value, -8192, 8191) + 8192;
        return make(0xE0, channel, v & 0x7F, v >> 7);
    }

    int channel() const noexcept { return status & 0x0F; }
    int note() const noexcept { return data1; }
    int velocity() const noexcept { return data2; }

    // ---- YOUR TURN (starter/step01.cpp) --------------------------------------------------------
    // The kind of message. A note on with velocity 0 is a NoteOff. Status bytes below 0x80 (data bytes)
    // and the kinds not listed above are Other.
    Kind kind() const noexcept;
    // -8192 .. 8191 for a pitch bend message.
    int pitchBendValue() const noexcept;

private:
    static MidiMessage make(int kind, int channel, int a, int b) noexcept {
        return {static_cast<std::uint8_t>(kind | (channel & 0x0F)), static_cast<std::uint8_t>(a & 0x7F), static_cast<std::uint8_t>(b & 0x7F)};
    }
};

// ---- musical time ---------------------------------------------------------------------------------
constexpr int kTicksPerQuarter = 960; // as chapter 14's Transport::kTicksPerBeat

struct Tempo {
    double sampleRate = 48000.0;
    double bpm = 120.0;
    // 120 BPM at 48 kHz: 25 samples a tick, exactly.
    double samplesPerTick() const noexcept { return sampleRate * 60.0 / (bpm * kTicksPerQuarter); }
};

// ---- YOUR TURN --------------------------------------------------------------------------------------
// round(tick x samplesPerTick), computed from the tick itself: adding samplesPerTick up tick after tick
// drifts, and after an hour a note would be late.
std::int64_t sampleOfTick(std::int64_t tick, const Tempo& tempo) noexcept;
// The smallest tick t with sampleOfTick(t) >= sample: so that the ticks of a block [s0, s1) are exactly
// [firstTickAtOrAfter(s0), firstTickAtOrAfter(s1)), and every tick belongs to exactly one block.
std::int64_t firstTickAtOrAfter(std::int64_t sample, const Tempo& tempo) noexcept;

// ---- sequences ----------------------------------------------------------------------------------------
struct TimedEvent {
    std::int64_t tick = 0;
    MidiMessage message;
};

// ---- YOUR TURN --------------------------------------------------------------------------------------
// Sorts by tick. At the same tick note-offs come first (a note played again right where it ended must
// sound again, not be cut by its own end); otherwise the order stays as it was (a stable sort).
void sortEvents(std::vector<TimedEvent>& events);

// An event in an audio block: `offset` samples after the block's first sample.
struct BlockEvent {
    int offset = 0;
    MidiMessage message;
};

// ---- YOUR TURN --------------------------------------------------------------------------------------
// The events of the sorted `sequence` that fall into the block [blockStart, blockStart + numSamples), in
// order, at offset sampleOfTick(tick) - blockStart. `out` is cleared first and must not grow beyond its
// capacity when the caller reserved enough: the audio thread calls this. Find the first one by binary
// search, not by walking from the start of the song.
void eventsInBlock(const std::vector<TimedEvent>& sequence, std::int64_t blockStart, int numSamples, const Tempo& tempo,
                   std::vector<BlockEvent>& out);

// ---- provided: the simplest instrument ----------------------------------------------------------------
// It outputs, every sample, the number of notes held - a staircase that shows exactly when each event
// took effect. A note off for a note that is not held changes nothing.
class CountingInstrument {
public:
    void handle(const MidiMessage& m) noexcept {
        const Kind k = m.kind();
        if (k == Kind::NoteOn) {
            ++held_[m.note()];
        } else if (k == Kind::NoteOff && held_[m.note()] > 0) {
            --held_[m.note()];
        }
    }
    void render(float* out, int numSamples) noexcept {
        int total = 0;
        for (const int h : held_)
            total += h;
        for (int i = 0; i < numSamples; ++i)
            out[i] = static_cast<float>(total);
    }

private:
    int held_[128] = {};
};

// ---- YOUR TURN --------------------------------------------------------------------------------------
// Renders numSamples into `out`, cut at every event: render up to the event's offset, hand the event over,
// go on. Several events at one offset all take effect before that sample.
void renderBlock(CountingInstrument& instrument, const std::vector<BlockEvent>& events, float* out, int numSamples) noexcept;

} // namespace minidaw
