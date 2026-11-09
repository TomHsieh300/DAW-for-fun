#pragma once
// minidaw.h - Step 5 snapshot of chapter 18: a keyboard.
//
// So far the notes came from clips. A keyboard is live: its bytes come in on a thread of the MIDI driver,
// whenever a key moves, and the audio thread must play them. Three problems, one after the other:
//
//   bytes -> messages   A MIDI cable carries bytes, not messages. A keyboard leaves out repeated status bytes
//                       ("running status"), a clock byte may arrive in the middle of a note, and system
//                       exclusive dumps can be any length. MidiParser turns the stream into messages.
//   thread -> thread    The MIDI thread may not wait for the audio thread, and the audio thread may not wait
//                       at all. A single-producer, single-consumer FIFO of fixed size (chapter 17's ring, for
//                       messages): two counters that only grow.
//   time -> sample      Every message carries the time it arrived. The audio thread renders a block ahead of
//                       when it is heard, and only every few milliseconds. Play everything at the start of
//                       the next block (as soon as possible), and a note's delay depends on when in the
//                       block period it was played: jitter. Or schedule every message a fixed delay after it
//                       arrived: more delay, but the same for every note.
//
// YOUR TURN (Step 5): MidiParser::feed(), MidiFifo::push(), pop(), and drain() in starter/step05.cpp.

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
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
    bool operator==(const MidiMessage& o) const noexcept { return status == o.status && data1 == o.data1 && data2 == o.data2; }

private:
    static MidiMessage make(int kind, int channel, int a, int b) noexcept {
        return {static_cast<std::uint8_t>(kind | (channel & 0x0F)), static_cast<std::uint8_t>(a & 0x7F), static_cast<std::uint8_t>(b & 0x7F)};
    }
};

struct BlockEvent {
    int offset = 0;
    MidiMessage message;
};

// ---- Step 5: bytes to messages ----------------------------------------------------------------------------
// How many data bytes a channel message has: 1 for program change (0xCn) and channel pressure (0xDn), 2 for
// the others.
inline int dataBytesOf(std::uint8_t status) noexcept {
    const int kind = status & 0xF0;
    return kind == 0xC0 || kind == 0xD0 ? 1 : 2;
}

class MidiParser {
public:
    // ---- YOUR TURN (starter/step05.cpp) -------------------------------------------------------------------
    // One byte of the stream. True when it completes a channel message, which is then in `out` (data2 is 0 for
    // a message with one data byte). The rules:
    //   0xF8-0xFF  real time (clock, start, stop, ...): may come between any two bytes, even inside a message.
    //              It changes nothing: false.
    //   0xF0       system exclusive starts: its data bytes are skipped up to 0xF7. Running status ends.
    //   0xF1-0xF7  system common (and the end of a SysEx): running status ends, their data bytes are skipped.
    //   0x80-0xEF  a status: it becomes the running status, collecting starts over.
    //   0x00-0x7F  data: collected for the running status; with none (or inside a SysEx) it is skipped, and
    //              skipped() counts the ones that are not SysEx. A complete message keeps the running status, so
    //              the next data byte starts the next message of the same kind.
    bool feed(std::uint8_t byte, MidiMessage& out) noexcept;

    // ---- provided -------------------------------------------------------------------------------------------
    long skipped() const noexcept { return skipped_; }

private:
    std::uint8_t running_ = 0; // 0: none
    std::uint8_t data_[2] = {0, 0};
    int have_ = 0;
    bool sysex_ = false;
    long skipped_ = 0;
};

// ---- Step 5: thread to thread -----------------------------------------------------------------------------
struct TimedMidi {
    double time = 0.0; // seconds, on the clock the driver and the audio callback share
    MidiMessage message;
};

// One producer (the MIDI thread), one consumer (the audio thread). kCapacity is a power of two.
class MidiFifo {
public:
    static constexpr std::size_t kCapacity = 1024;

    // ---- YOUR TURN ------------------------------------------------------------------------------------------
    // Producer. False if it is full: the message is lost and dropped() counts it.
    bool push(const TimedMidi& m) noexcept;
    // Consumer. False if it is empty.
    bool pop(TimedMidi& m) noexcept;
    // Consumer: the oldest message without taking it, or nullptr.
    const TimedMidi* peek() const noexcept;

    // ---- provided -------------------------------------------------------------------------------------------
    long dropped() const noexcept { return dropped_.load(std::memory_order_relaxed); }

private:
    std::array<TimedMidi, kCapacity> items_{};
    std::atomic<std::uint64_t> written_{0}; // only the producer changes it
    std::atomic<std::uint64_t> read_{0};    // only the consumer changes it
    std::atomic<long> dropped_{0};
};

// ---- Step 5: time to sample ---------------------------------------------------------------------------------
enum class Timing {
    AsSoonAsPossible, // everything waiting is played at the start of the block
    Scheduled,        // each message at its arrival time + a fixed delay
};

struct BlockClock {
    double blockTime = 0.0; // when the block's first sample will be heard, on the shared clock
    int numSamples = 512;
    double sampleRate = 48000.0;
};

// ---- YOUR TURN ------------------------------------------------------------------------------------------------
// The audio thread, at the start of a block: takes the messages for this block out of the FIFO into `out`
// (cleared first; reserved by the caller), in order.
//   AsSoonAsPossible   every message waiting, at offset 0.
//   Scheduled          a message is due at time + delay. Its offset is floor((due - blockTime) x sampleRate).
//                      An offset at or after the block's end: it and everything after it stay in the FIFO for a
//                      later block. A negative offset (the message came too late to be on time): offset 0, and
//                      `late` counts it.
void drain(MidiFifo& fifo, const BlockClock& clock, Timing timing, double delay, std::vector<BlockEvent>& out, long& late) noexcept;

} // namespace minidaw
