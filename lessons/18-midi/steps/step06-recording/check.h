#pragma once
// check.h - the self-check of Step 6 (recording MIDI). Defines main(); included at the END of
// starter/step06.cpp and solution/step06.cpp, so both run exactly the same checks.
//
// A player who plays exactly in time with what they hear, against a device with an output latency and a mix
// with plugin delay: every note must land on the tick it was played on. Then the recorder (keys, loops,
// passes), Overdub and Replace, and quantize.

#include <dawlab/Common.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

namespace step06_check {

using namespace minidaw;

const Tempo kTempo{48000.0, 120.0}; // 25 samples a tick, 3840 ticks a bar

inline void checkPlacement() {
    // A block rendered song sample 96000; it will be heard at 10.0 s; the mix delays by 240 samples. At 10.0 s
    // the player hears song sample 96000 - 240.
    const Anchor a{10.0, 96000, 48000.0, 240};
    DAWLAB_CHECK(songSampleAt(10.0, a) == 95760);
    DAWLAB_CHECK(songSampleAt(10.5, a) == 95760 + 24000);
    DAWLAB_CHECK(songSampleAt(9.9, a) == 95760 - 4800);
    DAWLAB_CHECK(songSampleAt(10.0 + 0.4 / 48000.0, a) == 95760); // to the nearest sample
    DAWLAB_CHECK(songSampleAt(10.0 + 0.6 / 48000.0, a) == 95761);
    DAWLAB_CHECK(tickAt(96000, kTempo) == 3840 && tickAt(96012, kTempo) == 3840 && tickAt(96013, kTempo) == 3841);
    DAWLAB_CHECK(tickAt(95988, kTempo) == 3840 && tickAt(95987, kTempo) == 3839);
    const Tempo t133{48000.0, 133.0};
    DAWLAB_CHECK(tickAt(21654, t133) == 960); // Step 1: tick 960 is sample 21654 at 133 BPM

    // The player: 200 notes, each played exactly when its tick is heard, give or take a quarter of a sample.
    // The audio thread renders 512-sample blocks; callbacks come at uneven times, but each block knows exactly
    // when it will be heard. Any of them is a good anchor.
    std::mt19937 rng(66);
    std::uniform_int_distribution<std::int64_t> tick(0, 32 * 3840);
    std::uniform_real_distribution<double> wobble(-0.25, 0.25);
    const double L = 0.0123; // the device's output latency
    const int mix = 752;     // chapter 14's demo song
    int wrong = 0;
    for (int i = 0; i < 200; ++i) {
        const std::int64_t intended = tick(rng);
        const std::int64_t sample = std::llround(intended * kTempo.samplesPerTick());
        // When is that sample heard? Block k renders song samples [512k, 512k + 512) and is heard from
        // 1.0 + L + 512k / 48000 + mix / 48000.
        const double heardAt = 1.0 + L + (static_cast<double>(sample) + mix) / 48000.0 + wobble(rng) / 48000.0;
        const std::int64_t k = (i * 7919) % 3000; // some block, any block
        const Anchor anchor{1.0 + L + 512.0 * k / 48000.0, 512 * k, 48000.0, mix};
        wrong += tickAt(songSampleAt(heardAt, anchor), kTempo) == intended ? 0 : 1;
    }
    std::printf("  200 notes played in time with what was heard (latency %.1f ms, mix %d samples): %d off their tick\n", 1000.0 * L, mix,
                wrong);
    DAWLAB_CHECK(wrong == 0);
}

inline Note n(std::int64_t start, std::int64_t length, int pitch, int velocity = 100, int channel = 0) {
    return {start, length, pitch, velocity, channel};
}

inline void checkRecorder() {
    NoteRecorder r;
    r.start(1000, {});
    r.noteOn(1000, 60, 90, 0);
    r.noteOn(1100, 64, 80, 0);
    r.noteOn(1150, 64, 70, 1); // another channel: another key
    r.noteOff(1500, 60, 0);
    r.noteOn(1600, 64, 0, 0); // velocity 0: 64 on channel 0 up
    r.noteOff(1700, 99, 0);   // never down: nothing
    r.noteOn(2000, 67, 100, 0);
    r.noteOn(2200, 67, 110, 0); // down twice: the first ends here
    r.noteOn(3000, 72, 100, 0);
    r.noteOff(3000, 72, 0); // the same tick: one tick long
    r.stop(4000);           // 64 on channel 1 and 67 are still down
    const std::vector<TakeNote>& t = r.notes();
    DAWLAB_CHECK(t.size() == 6);
    if (t.size() == 6) {
        DAWLAB_CHECK(t[0].note == n(1000, 500, 60, 90) && t[1].note == n(1100, 500, 64, 80));
        DAWLAB_CHECK(t[2].note == n(2000, 200, 67, 100) && t[3].note == n(3000, 1, 72, 100));
        // Closed by stop(), in the order they went down.
        DAWLAB_CHECK(t[4].note == n(1150, 2850, 64, 70, 1) && t[5].note == n(2200, 1800, 67, 110));
    }
    DAWLAB_CHECK(r.passes().size() == 1 && r.passes()[0].from == 1000 && r.passes()[0].to == 4000);

    // A loop of one bar [3840, 7680), from beat 3 of it: pass 0 is half a bar, then whole bars.
    const LoopRange loop{3840, 7680};
    NoteRecorder l;
    l.start(5760, loop);
    l.noteOn(6720, 60, 100, 0);  // pass 0, beat 4
    l.noteOff(6900, 60, 0);
    l.noteOn(7600, 62, 100, 0);  // pass 0, held over the loop's end
    l.noteOff(7800, 62, 0);      // ...into pass 1: ends at the loop's end
    l.noteOn(7680, 64, 100, 0);  // pass 1, beat 1 (unwrapped 7680 = song 3840)
    l.noteOff(8000, 64, 0);
    l.noteOn(11520 + 960, 65, 100, 0); // pass 2, beat 2
    l.stop(11520 + 1920);              // stopped at beat 3 of pass 2: 65 still down
    const std::vector<TakeNote>& lt = l.notes();
    DAWLAB_CHECK(lt.size() == 4);
    if (lt.size() == 4) {
        DAWLAB_CHECK(lt[0].note.start == 6720 && lt[0].note.length == 180 && lt[0].pass == 0);
        DAWLAB_CHECK(lt[1].note.start == 7600 && lt[1].note.length == 80 && lt[1].pass == 0);
        DAWLAB_CHECK(lt[2].note.start == 3840 && lt[2].note.length == 320 && lt[2].pass == 1);
        DAWLAB_CHECK(lt[3].note.start == 4800 && lt[3].note.length == 960 && lt[3].pass == 2);
    }
    const std::vector<Coverage>& c = l.passes();
    DAWLAB_CHECK(c.size() == 3);
    if (c.size() == 3) {
        DAWLAB_CHECK(c[0].pass == 0 && c[0].from == 5760 && c[0].to == 7680);
        DAWLAB_CHECK(c[1].pass == 1 && c[1].from == 3840 && c[1].to == 7680);
        DAWLAB_CHECK(c[2].pass == 2 && c[2].from == 3840 && c[2].to == 5760);
    }
    // Stopped exactly where a pass begins: that pass covered nothing.
    NoteRecorder e;
    e.start(3840, loop);
    e.stop(7680);
    DAWLAB_CHECK(e.passes().size() == 1 && e.passes()[0].to == 7680);
}

inline void checkMerge() {
    // The clip had two notes in the loop; three passes were recorded, the last stopped half-way.
    const LoopRange loop{0, 3840};
    NoteRecorder r;
    r.start(0, loop);
    r.noteOn(0, 60, 100, 0);     // pass 0, beat 1
    r.noteOff(480, 60, 0);
    r.noteOn(2880, 62, 100, 0);  // pass 0, beat 4
    r.noteOff(3200, 62, 0);
    r.noteOn(3840 + 960, 64, 100, 0); // pass 1, beat 2
    r.noteOff(3840 + 1200, 64, 0);
    r.noteOn(7680 + 0, 65, 100, 0);   // pass 2, beat 1
    r.noteOff(7680 + 300, 65, 0);
    r.stop(7680 + 1920);               // pass 2 covers beats 1-2
    const std::vector<Note> existing{n(1920, 100, 48), n(3000, 100, 50), n(5000, 100, 52)}; // the last is after the loop

    const std::vector<Note> over = mergeTake(existing, r, RecordMode::Overdub);
    DAWLAB_CHECK(over.size() == 7);
    DAWLAB_CHECK(over.size() == 7 && over[0].pitch == 60 && over[1].pitch == 65 && over[2].pitch == 64 && over[3].pitch == 48);
    // Equal starts: the clip's note first, then the take's.
    bool sorted = true;
    for (size_t i = 1; i < over.size(); ++i)
        sorted = sorted && over[i - 1].start <= over[i].start;
    DAWLAB_CHECK(sorted);

    // Replace: pass 1 covered the whole loop - pass 0's notes and the clip's notes in the loop are gone. Pass 2
    // covered beats 1-2: pass 1's note at beat 2 is gone too. What is left: pass 2's note, and the note after
    // the loop that nothing covered.
    const std::vector<Note> rep = mergeTake(existing, r, RecordMode::Replace);
    DAWLAB_CHECK(rep.size() == 2 && rep[0].pitch == 65 && rep[1].pitch == 52);

    // A plain take without a loop: Replace erases only where it recorded.
    NoteRecorder s;
    s.start(1000, {});
    s.noteOn(1500, 70, 100, 0);
    s.noteOff(1600, 70, 0);
    s.stop(2500);
    const std::vector<Note> plain = mergeTake({n(999, 10, 40), n(1000, 10, 41), n(2499, 10, 42), n(2500, 10, 43)}, s, RecordMode::Replace);
    DAWLAB_CHECK(plain.size() == 3 && plain[0].pitch == 40 && plain[1].pitch == 70 && plain[2].pitch == 43);
}

inline void checkQuantize() {
    const std::vector<Note> played{n(250, 100, 60), n(470, 50, 61), n(10, 20, 62), n(845, 80, 63), n(1199, 60, 64)};
    // Full strength: the nearest sixteenth (240).
    std::vector<Note> q = quantize(played, {240, 1.0, 0.0});
    DAWLAB_CHECK(q.size() == 5);
    if (q.size() == 5) {
        DAWLAB_CHECK(q[0] == n(0, 20, 62) && q[1] == n(240, 100, 60) && q[2] == n(480, 50, 61));
        DAWLAB_CHECK(q[3] == n(960, 80, 63) && q[4] == n(1200, 60, 64)); // 845 -> 960 (3.52 sixteenths)
    }
    // Half strength: half way there.
    q = quantize(played, {240, 0.5, 0.0});
    DAWLAB_CHECK(q.size() == 5 && q[0].start == 5 && q[1].start == 245 && q[2].start == 475 && q[3].start == 903 && q[4].start == 1200);
    // Swing: every second sixteenth 1/3 of a sixteenth later (80 ticks); the others stay on the grid.
    q = quantize(played, {240, 1.0, 1.0 / 3.0});
    DAWLAB_CHECK(q.size() == 5 && q[0].start == 0 && q[1].start == 320 && q[2].start == 480 && q[3].start == 960 && q[4].start == 1280);
    // Strength 0: as played (sorted). And what was played is never touched.
    q = quantize(played, {240, 0.0, 0.3});
    DAWLAB_CHECK(q.size() == 5 && q[0] == played[2] && q[4] == played[4]);
    DAWLAB_CHECK(played[0] == n(250, 100, 60) && played[3] == n(845, 80, 63));
    // Never before the song's start; a coarser grid: quarter notes.
    q = quantize({n(5, 10, 60), n(1100, 10, 61)}, {960, 1.0, 0.5}); // 1100 is nearest to line 1, an odd one
    DAWLAB_CHECK(q.size() == 2 && q[0].start == 0 && q[1].start == 960 + 480);
    q = quantize({n(100, 10, 60)}, {240, 2.0, 0.0}); // too strong: past the line, but not before 0
    DAWLAB_CHECK(q.size() == 1 && q[0].start == 0);
    // Stable: equal starts keep their order.
    q = quantize({n(230, 10, 70), n(250, 10, 71), n(240, 10, 72)}, {240, 1.0, 0.0});
    DAWLAB_CHECK(q.size() == 3 && q[0].pitch == 70 && q[1].pitch == 71 && q[2].pitch == 72);
}

} // namespace step06_check

int main() {
    using namespace step06_check;
    std::printf("Step 6: recording MIDI\n");
    checkPlacement();
    checkRecorder();
    checkMerge();
    checkQuantize();
    return dawlab::test::summary("l18_step06");
}
