// Step 1 (starter): MIDI events and musical time.
//
// Build and run:  l18_step01_starter   (prints [FAIL] until all seven TODOs are done)
// Reference:      ../solution/step01.cpp, docs/step-01-midi-time.md ("Your turn")
//
// MidiMessage, Tempo, TimedEvent, BlockEvent and CountingInstrument are in ../minidaw.h.

#include "../minidaw.h"

namespace minidaw {

Kind MidiMessage::kind() const noexcept {
    // TODO 1: Look at the high nibble of the status byte: 0x80 NoteOff, 0x90 NoteOn (but NoteOff when the
    //         velocity is 0), 0xB0 Controller, 0xE0 PitchBend, anything else Other.
    return Kind::Other;
}

int MidiMessage::pitchBendValue() const noexcept {
    // TODO 2: 14 bits, data2 the high seven, data1 the low seven, minus the centre 8192.
    return 0;
}

std::int64_t sampleOfTick(std::int64_t tick, const Tempo& tempo) noexcept {
    // TODO 3: tick x samplesPerTick, rounded to the nearest sample (std::llround).
    (void)tick;
    (void)tempo;
    return 0;
}

std::int64_t firstTickAtOrAfter(std::int64_t sample, const Tempo& tempo) noexcept {
    // TODO 4: Start at ceil(sample / samplesPerTick): that tick reaches `sample`. Then step down while the tick
    //         before it still reaches it too (sampleOfTick() rounds, so it may).
    (void)sample;
    (void)tempo;
    return 0;
}

void sortEvents(std::vector<TimedEvent>& events) {
    // TODO 5: std::stable_sort by tick; at the same tick a NoteOff (kind()) goes before anything else.
    (void)events;
}

void eventsInBlock(const std::vector<TimedEvent>& sequence, std::int64_t blockStart, int numSamples, const Tempo& tempo,
                   std::vector<BlockEvent>& out) {
    // TODO 6: Clear out. The block's ticks are [firstTickAtOrAfter(blockStart), firstTickAtOrAfter(blockStart
    //         + numSamples)): std::lower_bound to the first, then push_back each event before the second,
    //         at offset sampleOfTick(tick) - blockStart.
    (void)sequence;
    (void)blockStart;
    (void)numSamples;
    (void)tempo;
    out.clear();
}

void renderBlock(CountingInstrument& instrument, const std::vector<BlockEvent>& events, float* out, int numSamples) noexcept {
    // TODO 7: Render from where you are to the event's offset, handle() the event, and so on; then the rest
    //         of the block.
    (void)events;
    instrument.render(out, numSamples);
}

} // namespace minidaw

#include "../check.h"
