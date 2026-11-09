#pragma once
// check.h - the self-check of Step 1 (MIDI events and musical time). Defines main(); included at the END
// of starter/step01.cpp and solution/step01.cpp, so both run exactly the same checks.
//
// The heart of it is block invariance: a sequence rendered in blocks of 1, 7, 64, 512 and random sizes
// must give, sample for sample, what a reference that looks at every sample on its own gives.

#include <dawlab/Common.h>

#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

namespace step01_check {

using namespace minidaw;

inline void checkMessages() {
    DAWLAB_CHECK(MidiMessage::noteOn(0, 60, 100).kind() == Kind::NoteOn);
    DAWLAB_CHECK(MidiMessage::noteOn(3, 60, 1).kind() == Kind::NoteOn);
    DAWLAB_CHECK(MidiMessage::noteOn(3, 60, 0).kind() == Kind::NoteOff); // what most keyboards send
    DAWLAB_CHECK(MidiMessage::noteOff(15, 60, 64).kind() == Kind::NoteOff);
    DAWLAB_CHECK(MidiMessage::controller(0, 64, 127).kind() == Kind::Controller);
    DAWLAB_CHECK(MidiMessage::pitchBend(0, 0).kind() == Kind::PitchBend);
    DAWLAB_CHECK((MidiMessage{0xC5, 10, 0}.kind() == Kind::Other));  // program change
    DAWLAB_CHECK((MidiMessage{0xA0, 60, 30}.kind() == Kind::Other)); // polyphonic aftertouch
    DAWLAB_CHECK((MidiMessage{0xD0, 30, 0}.kind() == Kind::Other));  // channel pressure
    DAWLAB_CHECK((MidiMessage{0xF8, 0, 0}.kind() == Kind::Other));   // clock
    DAWLAB_CHECK((MidiMessage{0x3C, 0x40, 0}.kind() == Kind::Other)); // a data byte is no status
    DAWLAB_CHECK(MidiMessage::noteOn(9, 36, 90).channel() == 9);
    DAWLAB_CHECK(MidiMessage::noteOn(9, 36, 90).status == 0x99);

    // Pitch bend: 14 bits, centre 8192, lsb first.
    DAWLAB_CHECK((MidiMessage{0xE0, 0x00, 0x40}.pitchBendValue() == 0));
    DAWLAB_CHECK((MidiMessage{0xE0, 0x7F, 0x7F}.pitchBendValue() == 8191));
    DAWLAB_CHECK((MidiMessage{0xE0, 0x00, 0x00}.pitchBendValue() == -8192));
    DAWLAB_CHECK((MidiMessage{0xE0, 0x01, 0x00}.pitchBendValue() == -8191));
    DAWLAB_CHECK((MidiMessage{0xE0, 0x00, 0x01}.pitchBendValue() == -8064));
    int wrong = 0;
    for (int v = -8192; v <= 8191; ++v)
        wrong += MidiMessage::pitchBend(2, v).pitchBendValue() == v ? 0 : 1;
    DAWLAB_CHECK(wrong == 0);
}

inline void checkTime() {
    const Tempo t120{48000.0, 120.0};
    DAWLAB_CHECK(t120.samplesPerTick() == 25.0);
    DAWLAB_CHECK(sampleOfTick(0, t120) == 0);
    DAWLAB_CHECK(sampleOfTick(960, t120) == 24000);  // a quarter note: half a second
    DAWLAB_CHECK(sampleOfTick(3840, t120) == 96000); // a bar of 4/4
    DAWLAB_CHECK(firstTickAtOrAfter(0, t120) == 0);
    DAWLAB_CHECK(firstTickAtOrAfter(24000, t120) == 960);
    DAWLAB_CHECK(firstTickAtOrAfter(24001, t120) == 961);
    DAWLAB_CHECK(firstTickAtOrAfter(23999, t120) == 960);

    // 133 BPM: 22.556 samples a tick. Rounded, not truncated.
    const Tempo t133{48000.0, 133.0};
    DAWLAB_CHECK(sampleOfTick(960, t133) == 21654); // 21654.135
    DAWLAB_CHECK(sampleOfTick(1, t133) == 23);      // 22.556
    DAWLAB_CHECK(sampleOfTick(-1, t133) == -23);

    // No drift: an hour of music at 133 BPM, each tick from its own number.
    const std::int64_t hour = 133LL * 60 * kTicksPerQuarter;
    const long double exact = static_cast<long double>(hour) * 48000.0L * 60.0L / (133.0L * kTicksPerQuarter);
    DAWLAB_CHECK(sampleOfTick(hour, t133) == static_cast<std::int64_t>(exact + 0.5L));
    std::printf("  an hour at 133 BPM: tick %lld is sample %lld\n", static_cast<long long>(hour), static_cast<long long>(sampleOfTick(hour, t133)));

    // firstTickAtOrAfter is the exact inverse, for odd tempos and sample rates, near zero and far out.
    std::mt19937 rng(18);
    std::uniform_real_distribution<double> bpm(20.0, 999.0);
    std::uniform_int_distribution<std::int64_t> far(0, 48000LL * 3600 * 24);
    const double rates[] = {44100.0, 48000.0, 88200.0, 96000.0, 22050.0};
    int wrong = 0;
    for (int round = 0; round < 2000; ++round) {
        const Tempo t{rates[round % 5], round % 3 == 0 ? std::round(bpm(rng)) : bpm(rng)};
        const std::int64_t s = round < 200 ? round : far(rng);
        const std::int64_t first = firstTickAtOrAfter(s, t);
        wrong += sampleOfTick(first, t) >= s && sampleOfTick(first - 1, t) < s ? 0 : 1;
    }
    DAWLAB_CHECK(wrong == 0);
    // Fast tempos at low rates: several ticks round to one sample.
    const Tempo dense{22050.0, 999.0};
    DAWLAB_CHECK(dense.samplesPerTick() < 1.5);
    wrong = 0;
    for (std::int64_t s = 0; s < 5000; ++s) {
        const std::int64_t first = firstTickAtOrAfter(s, dense);
        wrong += sampleOfTick(first, dense) >= s && sampleOfTick(first - 1, dense) < s ? 0 : 1;
    }
    DAWLAB_CHECK(wrong == 0);
}

inline void checkSort() {
    std::vector<TimedEvent> e{
        {960, MidiMessage::noteOn(0, 62, 90)},  {0, MidiMessage::noteOn(0, 60, 100)},   {960, MidiMessage::controller(0, 1, 5)},
        {960, MidiMessage::noteOff(0, 60)},     {480, MidiMessage::noteOn(0, 64, 80)},  {960, MidiMessage::noteOn(0, 64, 0)},
        {960, MidiMessage::controller(0, 1, 6)}};
    sortEvents(e);
    DAWLAB_CHECK(e.size() == 7);
    if (e.size() != 7)
        return;
    DAWLAB_CHECK(e[0].tick == 0 && e[1].tick == 480);
    // At 960: the two note-offs (the one with velocity 0 too) in their order, then the rest in theirs.
    DAWLAB_CHECK(e[2].message.note() == 60 && e[2].message.kind() == Kind::NoteOff);
    DAWLAB_CHECK(e[3].message.note() == 64 && e[3].message.kind() == Kind::NoteOff);
    DAWLAB_CHECK(e[4].message.kind() == Kind::NoteOn && e[4].message.note() == 62);
    DAWLAB_CHECK(e[5].message.kind() == Kind::Controller && e[5].message.velocity() == 5);
    DAWLAB_CHECK(e[6].message.kind() == Kind::Controller && e[6].message.velocity() == 6);

    // Stable with many equal ticks: a controller's moves at one tick stay in the order they were made.
    std::vector<TimedEvent> moves;
    for (int i = 0; i < 300; ++i)
        moves.push_back({(i * 7) % 5 * 10, MidiMessage::controller(0, 7, i % 128)});
    sortEvents(moves);
    int outOfOrder = 0;
    for (size_t i = 1; i < moves.size(); ++i)
        if (moves[i].tick < moves[i - 1].tick ||
            (moves[i].tick == moves[i - 1].tick && (moves[i].message.velocity() + 128 - moves[i - 1].message.velocity()) % 128 != 5))
            ++outOfOrder;
    DAWLAB_CHECK(outOfOrder == 0);
}

// A song of notes: chords, repeated notes that end where they start again, notes of one tick, notes that
// cross many blocks. Sorted.
inline std::vector<TimedEvent> makeSong(std::mt19937& rng, int numNotes) {
    std::uniform_int_distribution<std::int64_t> start(0, 40 * kTicksPerQuarter);
    std::uniform_int_distribution<int> note(48, 72), len(1, 4 * kTicksPerQuarter);
    std::vector<TimedEvent> song;
    for (int i = 0; i < numNotes; ++i) {
        const std::int64_t s = i % 5 == 0 && !song.empty() ? song[song.size() - 1].tick : start(rng); // back to back
        const int n = note(rng);
        const int l = i % 7 == 0 ? 1 : len(rng);
        song.push_back({s, MidiMessage::noteOn(i % 16, n, 1 + i % 127)});
        song.push_back({s + l, i % 2 == 0 ? MidiMessage::noteOff(i % 16, n) : MidiMessage::noteOn(i % 16, n, 0)});
    }
    sortEvents(song);
    return song;
}

// What the counting instrument outputs at each sample, worked out sample by sample: every event whose
// sample is at or before this one has taken effect, in the sequence's order.
inline std::vector<float> reference(const std::vector<TimedEvent>& song, const Tempo& tempo, std::int64_t length) {
    std::vector<float> out(static_cast<size_t>(length));
    CountingInstrument instrument;
    size_t next = 0;
    float scratch = 0.0f;
    for (std::int64_t s = 0; s < length; ++s) {
        while (next < song.size() && sampleOfTick(song[next].tick, tempo) <= s)
            instrument.handle(song[next++].message);
        instrument.render(&scratch, 1);
        out[static_cast<size_t>(s)] = scratch;
    }
    return out;
}

inline std::vector<float> inBlocks(const std::vector<TimedEvent>& song, const Tempo& tempo, std::int64_t length, int blockSize,
                                   std::mt19937* randomSizes, int* eventsSeen) {
    std::vector<float> out(static_cast<size_t>(length));
    CountingInstrument instrument;
    std::vector<BlockEvent> events;
    events.reserve(song.size());
    const size_t capacity = events.capacity();
    std::uniform_int_distribution<int> size(1, 1024);
    *eventsSeen = 0;
    for (std::int64_t s = 0; s < length;) {
        const int n = static_cast<int>(std::min<std::int64_t>(randomSizes != nullptr ? size(*randomSizes) : blockSize, length - s));
        eventsInBlock(song, s, n, tempo, events);
        if (events.capacity() != capacity)
            return {}; // eventsInBlock allocated
        for (const BlockEvent& e : events)
            if (e.offset < 0 || e.offset >= n)
                return {};
        *eventsSeen += static_cast<int>(events.size());
        renderBlock(instrument, events, out.data() + s, n);
        s += n;
    }
    return out;
}

inline void checkBlocks() {
    // By hand: two notes, the second played again where it ends, at 120 BPM (25 samples a tick).
    const Tempo t120{48000.0, 120.0};
    std::vector<TimedEvent> song{{0, MidiMessage::noteOn(0, 60, 100)}, {2, MidiMessage::noteOn(0, 64, 100)},
                                 {4, MidiMessage::noteOn(0, 64, 100)}, {4, MidiMessage::noteOff(0, 64)},
                                 {6, MidiMessage::noteOff(0, 60)},     {8, MidiMessage::noteOff(0, 64)}};
    sortEvents(song);
    std::vector<BlockEvent> events;
    events.reserve(16);
    eventsInBlock(song, 40, 80, t120, events); // samples 40..119: ticks 2 (50) and 4 (100, two events)
    DAWLAB_CHECK(events.size() == 3);
    if (events.size() == 3) {
        DAWLAB_CHECK(events[0].offset == 10 && events[0].message.note() == 64);
        DAWLAB_CHECK(events[1].offset == 60 && events[1].message.kind() == Kind::NoteOff);
        DAWLAB_CHECK(events[2].offset == 60 && events[2].message.kind() == Kind::NoteOn);
    }
    eventsInBlock(song, 50, 50, t120, events); // the first sample of the block counts, the one after it not
    DAWLAB_CHECK(events.size() == 1 && events[0].offset == 0);
    eventsInBlock(song, 201, 100, t120, events); // tick 8 is at 200: the block before
    DAWLAB_CHECK(events.empty());
    eventsInBlock(song, 0, 0, t120, events);
    DAWLAB_CHECK(events.empty());

    float out[250];
    CountingInstrument instrument;
    for (std::int64_t s = 0; s < 250; s += 50) {
        eventsInBlock(song, s, 50, t120, events);
        renderBlock(instrument, events, out + s, 50);
    }
    // 1 from 0, 2 from 50, still 2 from 100 (64 ends and starts again), 1 from 150, 0 from 200.
    DAWLAB_CHECK(out[0] == 1.0f && out[49] == 1.0f && out[50] == 2.0f && out[99] == 2.0f && out[100] == 2.0f);
    DAWLAB_CHECK(out[149] == 2.0f && out[150] == 1.0f && out[199] == 1.0f && out[200] == 0.0f && out[249] == 0.0f);

    // Every block size, every tempo: what a sample-by-sample reference says, every event exactly once.
    std::mt19937 rng(1818);
    const Tempo tempos[] = {{48000.0, 120.0}, {44100.0, 133.0}, {22050.0, 999.0}, {96000.0, 61.7}};
    int failures = 0, blocksRun = 0;
    for (const Tempo& tempo : tempos) {
        const std::vector<TimedEvent> many = makeSong(rng, 300);
        const std::int64_t length = sampleOfTick(many.back().tick, tempo) + 100;
        const std::vector<float> ref = reference(many, tempo, length);
        for (const int block : {1, 7, 64, 512, 0}) {
            int seen = 0;
            const std::vector<float> got = inBlocks(many, tempo, length, block, block == 0 ? &rng : nullptr, &seen);
            failures += got == ref && seen == static_cast<int>(many.size()) ? 0 : 1;
            ++blocksRun;
        }
    }
    std::printf("  %d runs of 600 events in blocks of 1, 7, 64, 512 and random sizes: %d differ from the reference\n", blocksRun, failures);
    DAWLAB_CHECK(failures == 0);
}

inline void checkTempoChange() {
    // The same notes at another tempo: a note in ticks moves with it, as an audio clip in samples does not.
    const TimedEvent bar3{2 * 4 * kTicksPerQuarter, MidiMessage::noteOn(0, 60, 100)};
    const std::int64_t at120 = sampleOfTick(bar3.tick, {48000.0, 120.0});
    const std::int64_t at90 = sampleOfTick(bar3.tick, {48000.0, 90.0});
    DAWLAB_CHECK(at120 == 192000 && at90 == 256000);
    std::printf("  bar 3 starts at sample %lld at 120 BPM, at %lld at 90 BPM\n", static_cast<long long>(at120), static_cast<long long>(at90));
}

} // namespace step01_check

int main() {
    using namespace step01_check;
    std::printf("Step 1: MIDI events and musical time\n");
    checkMessages();
    checkTime();
    checkSort();
    if (dawlab::test::counters().failures > 0)
        return dawlab::test::summary("l18_step01"); // the blocks need all of the above
    checkBlocks();
    checkTempoChange();
    return dawlab::test::summary("l18_step01");
}
