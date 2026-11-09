// Chapter 18, Step 1 (solution): MIDI events and musical time.
//
// Build and run:  l18_step01_solution   (ctest, label "solution")
// Your version:   starter/step01.cpp -> l18_step01_starter

#include "../minidaw.h"

namespace minidaw {

Kind MidiMessage::kind() const noexcept {
    switch (status & 0xF0) {
    case 0x80: return Kind::NoteOff;
    case 0x90: return data2 == 0 ? Kind::NoteOff : Kind::NoteOn;
    case 0xB0: return Kind::Controller;
    case 0xE0: return Kind::PitchBend;
    default: return Kind::Other; // data bytes, aftertouch, program change, system messages
    }
}

int MidiMessage::pitchBendValue() const noexcept {
    return ((data2 << 7) | data1) - 8192;
}

std::int64_t sampleOfTick(std::int64_t tick, const Tempo& tempo) noexcept {
    return std::llround(static_cast<double>(tick) * tempo.samplesPerTick());
}

std::int64_t firstTickAtOrAfter(std::int64_t sample, const Tempo& tempo) noexcept {
    // ceil() gives a tick at or after `sample`, so it rounds to `sample` or later. But rounding also pulls
    // the ticks just before it up to `sample`: step back while the tick before still gets there.
    std::int64_t t = static_cast<std::int64_t>(std::ceil(static_cast<double>(sample) / tempo.samplesPerTick()));
    while (sampleOfTick(t - 1, tempo) >= sample)
        --t;
    return t;
}

void sortEvents(std::vector<TimedEvent>& events) {
    std::stable_sort(events.begin(), events.end(), [](const TimedEvent& a, const TimedEvent& b) {
        if (a.tick != b.tick)
            return a.tick < b.tick;
        return a.message.kind() == Kind::NoteOff && b.message.kind() != Kind::NoteOff;
    });
}

void eventsInBlock(const std::vector<TimedEvent>& sequence, std::int64_t blockStart, int numSamples, const Tempo& tempo,
                   std::vector<BlockEvent>& out) {
    out.clear();
    const std::int64_t from = firstTickAtOrAfter(blockStart, tempo);
    const std::int64_t to = firstTickAtOrAfter(blockStart + numSamples, tempo);
    auto it = std::lower_bound(sequence.begin(), sequence.end(), from, [](const TimedEvent& e, std::int64_t t) { return e.tick < t; });
    for (; it != sequence.end() && it->tick < to; ++it)
        out.push_back({static_cast<int>(sampleOfTick(it->tick, tempo) - blockStart), it->message});
}

void renderBlock(CountingInstrument& instrument, const std::vector<BlockEvent>& events, float* out, int numSamples) noexcept {
    int done = 0;
    for (const BlockEvent& e : events) {
        instrument.render(out + done, e.offset - done); // up to the event; nothing if it is at the same sample
        done = e.offset;
        instrument.handle(e.message);
    }
    instrument.render(out + done, numSamples - done);
}

} // namespace minidaw

#include "../check.h"
