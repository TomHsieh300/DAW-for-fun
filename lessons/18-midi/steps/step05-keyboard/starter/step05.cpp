// Step 5 (starter): a keyboard.
//
// Build and run:  l18_step05_starter   (prints [FAIL] until all five TODOs are done)
// Reference:      ../solution/step05.cpp, docs/step-05-keyboard.md ("Your turn")
//
// MidiParser, MidiFifo, Timing and BlockClock are in ../minidaw.h.

#include "../minidaw.h"

namespace minidaw {

bool MidiParser::feed(std::uint8_t byte, MidiMessage& out) noexcept {
    // TODO 1: The byte ranges of the header, from the top: real time, system (SysEx or common), status, data.
    //         For data: dataBytesOf(running_) tells when a message is complete.
    (void)byte;
    (void)out;
    (void)running_;
    (void)data_;
    (void)have_;
    (void)sysex_;
    (void)skipped_;
    return false;
}

bool MidiFifo::push(const TimedMidi& m) noexcept {
    // TODO 2: Full when written - read == kCapacity (read_ with acquire). Otherwise store the item at
    //         written & (kCapacity - 1), then written + 1 with release.
    (void)m;
    return false;
}

bool MidiFifo::pop(TimedMidi& m) noexcept {
    // TODO 3: Empty when read == written (written_ with acquire). Otherwise copy the item, then read + 1 with
    //         release.
    (void)m;
    return false;
}

const TimedMidi* MidiFifo::peek() const noexcept {
    // TODO 4: As pop(), without taking it.
    return nullptr;
}

void drain(MidiFifo& fifo, const BlockClock& clock, Timing timing, double delay, std::vector<BlockEvent>& out, long& late) noexcept {
    // TODO 5: Clear out. As soon as possible: pop everything to offset 0. Scheduled: peek; the offset of
    //         time + delay in this block (std::floor); stop at the block's end; clamp a negative offset to 0
    //         and count it in `late`; pop.
    (void)fifo;
    (void)clock;
    (void)timing;
    (void)delay;
    (void)late;
    out.clear();
}

} // namespace minidaw

#include "../check.h"
