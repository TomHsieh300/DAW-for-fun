#pragma once
// check.h - the self-check of Step 4 (polyphony). Defines main(); included at the END of starter/step04.cpp
// and solution/step04.cpp, so both run exactly the same checks.
//
// The allocator by hand (who gets which voice, who is stolen), the pedal, the bend and "all notes off",
// then stealing by ear (no click, a short delay), and a long random performance that must sound the same in
// any block size and leave nothing sounding.

#include <dawlab/Common.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <utility>
#include <vector>

namespace step04_check {

using namespace minidaw;

constexpr double kRate = 48000.0;

inline Voice::Settings settings() {
    Voice::Settings s;
    s.cutoffHz = 5000.0;
    s.attack = 0.005;
    s.decay = 0.1;
    s.sustain = 0.6;
    s.release = 0.3;
    return s;
}

inline void send(Synth& synth, const MidiMessage& m) {
    std::vector<BlockEvent> ev{{0, m}};
    float scratch[1];
    synth.render(ev, scratch, 1);
}

inline void run(Synth& synth, int samples) {
    std::vector<BlockEvent> none;
    std::vector<float> out(static_cast<size_t>(samples));
    synth.render(none, out.data(), samples);
}

inline int voiceOf(const Synth& s, int pitch) {
    for (int i = 0; i < s.numVoices(); ++i)
        if (s.voice(i).isActive() && s.voice(i).pitch() == pitch)
            return i;
    return -1;
}

inline void checkAllocation() {
    Synth s;
    s.prepare(kRate, 4, settings());
    for (const int p : {60, 62, 64, 65})
        send(s, MidiMessage::noteOn(0, p, 100));
    DAWLAB_CHECK(s.activeVoices() == 4 && voiceOf(s, 60) == 0 && voiceOf(s, 65) == 3);
    run(s, 2000);

    // Played again while it sounds: its own voice, nothing stolen.
    send(s, MidiMessage::noteOn(0, 60, 90));
    DAWLAB_CHECK(s.activeVoices() == 4 && voiceOf(s, 60) == 0 && s.stolen() == 0);

    // All four busy, one released: the released one goes, although 60 started first.
    send(s, MidiMessage::noteOff(0, 62));
    run(s, 2000);
    const int v62 = voiceOf(s, 62);
    send(s, MidiMessage::noteOn(0, 67, 100));
    DAWLAB_CHECK(s.stolen() == 1 && s.voice(v62).pitch() == 67 && s.voice(v62).isStealing());
    run(s, 200); // past the 2 ms fade
    DAWLAB_CHECK(!s.voice(v62).isStealing() && !s.voice(v62).isReleasing());

    // None released: the note that started longest ago goes - 64, since 60 was played again after it.
    send(s, MidiMessage::noteOn(0, 69, 100));
    DAWLAB_CHECK(s.stolen() == 2 && voiceOf(s, 64) < 0 && voiceOf(s, 60) == 0);
    run(s, 200);
    // The stolen note's note off finds no voice of its own: the new note on that voice plays on.
    send(s, MidiMessage::noteOff(0, 64));
    DAWLAB_CHECK(s.activeVoices() == 4);
    for (int i = 0; i < 4; ++i)
        DAWLAB_CHECK(!s.voice(i).isReleasing());

    // Of two released notes, the one released first goes.
    send(s, MidiMessage::noteOff(0, 65));
    run(s, 100);
    send(s, MidiMessage::noteOff(0, 60));
    run(s, 100);
    const int v65 = voiceOf(s, 65);
    send(s, MidiMessage::noteOn(0, 71, 100));
    DAWLAB_CHECK(v65 >= 0 && s.voice(v65).pitch() == 71 && voiceOf(s, 60) == 0);
    // A free voice before any stealing; and only the first numVoices voices count.
    run(s, 48000);
    send(s, MidiMessage::controller(0, Synth::kAllNotesOff, 0));
    run(s, 48000);
    DAWLAB_CHECK(s.activeVoices() == 0);
    for (int p = 40; p < 50; ++p)
        send(s, MidiMessage::noteOn(0, p, 100));
    DAWLAB_CHECK(s.activeVoices() == 4 && s.stolen() == 2 + 1 + 6);
    // Velocity 0 is a note off.
    send(s, MidiMessage::noteOn(0, 49, 0));
    DAWLAB_CHECK(s.voice(voiceOf(s, 49)).isReleasing());
}

// The cases the walk above does not reach.
inline void checkAllocationCases() {
    // An idle voice is taken before anything is stolen - also when other voices started long before it.
    Synth s;
    s.prepare(kRate, 4, settings());
    for (const int p : {60, 62, 64, 65})
        send(s, MidiMessage::noteOn(0, p, 100));
    send(s, MidiMessage::noteOff(0, 65));
    run(s, 48000); // 65 has died away; 60, 62 and 64 are still held
    DAWLAB_CHECK(s.activeVoices() == 3);
    send(s, MidiMessage::noteOn(0, 67, 100));
    DAWLAB_CHECK(s.stolen() == 0 && voiceOf(s, 67) == 3 && voiceOf(s, 60) == 0);

    // A released note goes before the oldest held one, even when it started last.
    send(s, MidiMessage::noteOff(0, 67));
    run(s, 100);
    send(s, MidiMessage::noteOn(0, 69, 100));
    DAWLAB_CHECK(s.stolen() == 1 && voiceOf(s, 69) == 3 && voiceOf(s, 60) == 0);

    // A voice that fades for a stolen note is not "released": its new note's key is down. The next note
    // takes the oldest held note instead, and the stolen-for note still comes.
    send(s, MidiMessage::noteOn(0, 71, 100)); // steals 60, the oldest
    DAWLAB_CHECK(voiceOf(s, 71) == 0 && s.voice(0).isStealing());
    send(s, MidiMessage::noteOn(0, 72, 100)); // 62 is next
    DAWLAB_CHECK(voiceOf(s, 72) == 1 && voiceOf(s, 71) == 0 && s.stolen() == 3);
    run(s, 200);
    DAWLAB_CHECK(!s.voice(0).isStealing() && s.voice(0).pitch() == 71 && !s.voice(0).isReleasing());
}

// How long a voice takes to fall silent after `act`.
inline int samplesToSilence(Synth& s, int pitch) {
    const int v = voiceOf(s, pitch);
    int n = 0;
    while (v >= 0 && s.voice(v).isActive() && n < 200000) {
        run(s, 1);
        ++n;
    }
    return n;
}

inline void checkReleaseOnce() {
    // A release takes 0.3 s (14400 samples), whatever happens during it: a second note off, or the pedal going
    // down and up again, must not start it over.
    Synth s;
    s.prepare(kRate, 4, settings());
    send(s, MidiMessage::noteOn(0, 60, 100));
    run(s, 9600);
    send(s, MidiMessage::noteOff(0, 60));
    run(s, 7200);
    send(s, MidiMessage::noteOff(0, 60)); // again
    DAWLAB_CHECK(std::abs(samplesToSilence(s, 60) - 7200) <= 2);

    send(s, MidiMessage::controller(0, Synth::kSustainPedal, 127));
    send(s, MidiMessage::noteOn(0, 62, 100));
    run(s, 9600);
    send(s, MidiMessage::noteOff(0, 62));
    send(s, MidiMessage::controller(0, Synth::kSustainPedal, 0)); // the release starts
    run(s, 7200);
    send(s, MidiMessage::controller(0, Synth::kSustainPedal, 127));
    send(s, MidiMessage::controller(0, Synth::kSustainPedal, 0)); // and again
    DAWLAB_CHECK(std::abs(samplesToSilence(s, 62) - 7200) <= 4);

    // render() writes the block, whatever was in the buffer before.
    Synth a, b;
    a.prepare(kRate, 4, settings());
    b.prepare(kRate, 4, settings());
    std::vector<BlockEvent> ev{{10, MidiMessage::noteOn(0, 60, 100)}};
    std::vector<float> clean(512, 0.0f), dirty(512, 0.75f);
    a.render(ev, clean.data(), 512);
    b.render(ev, dirty.data(), 512);
    DAWLAB_CHECK(clean == dirty);
}

inline void checkPedal() {
    Synth s;
    s.prepare(kRate, 8, settings());
    send(s, MidiMessage::controller(0, Synth::kSustainPedal, 127));
    DAWLAB_CHECK(s.pedalDown());
    send(s, MidiMessage::noteOn(0, 60, 100));
    send(s, MidiMessage::noteOff(0, 60));
    send(s, MidiMessage::noteOn(0, 64, 100));
    send(s, MidiMessage::noteOff(0, 64));
    send(s, MidiMessage::noteOn(0, 67, 100)); // still held when the pedal comes up
    // A pedal sends a stream of values as it moves: 100 after 127 is still down, and lets nothing go.
    send(s, MidiMessage::controller(0, Synth::kSustainPedal, 100));
    run(s, 4800);
    DAWLAB_CHECK(s.activeVoices() == 3);
    for (int i = 0; i < 3; ++i)
        DAWLAB_CHECK(!s.voice(i).isReleasing());
    // Released by the pedal, again under it: the same voice, and the pedal coming up does not end it.
    send(s, MidiMessage::noteOn(0, 64, 100));
    DAWLAB_CHECK(voiceOf(s, 64) == 1 && s.activeVoices() == 3);
    send(s, MidiMessage::controller(0, Synth::kSustainPedal, 0));
    DAWLAB_CHECK(!s.pedalDown());
    DAWLAB_CHECK(s.voice(voiceOf(s, 60)).isReleasing());
    DAWLAB_CHECK(!s.voice(voiceOf(s, 64)).isReleasing() && !s.voice(voiceOf(s, 67)).isReleasing());
    // 63 is "up" too; 64 counts as down.
    send(s, MidiMessage::controller(0, Synth::kSustainPedal, 64));
    send(s, MidiMessage::noteOff(0, 67));
    DAWLAB_CHECK(!s.voice(voiceOf(s, 67)).isReleasing());
    send(s, MidiMessage::controller(0, Synth::kSustainPedal, 63));
    DAWLAB_CHECK(s.voice(voiceOf(s, 67)).isReleasing());
    // Other controllers change nothing.
    send(s, MidiMessage::controller(0, 1, 100));
    DAWLAB_CHECK(!s.voice(voiceOf(s, 64)).isReleasing() && !s.pedalDown());
}

inline void checkBendAndPanic() {
    Synth s;
    s.prepare(kRate, 4, settings());
    send(s, MidiMessage::noteOn(0, 69, 100));
    send(s, MidiMessage::pitchBend(0, 8191));
    DAWLAB_CHECK(std::abs(s.bendSemitones() - 8191.0 / 8192.0 * 2.0) < 1e-12);
    DAWLAB_CHECK(std::abs(s.voice(0).frequency() - 440.0 * std::pow(2.0, s.bendSemitones() / 12.0)) < 1e-9);
    send(s, MidiMessage::pitchBend(0, -8192));
    DAWLAB_CHECK(std::abs(s.voice(0).frequency() - 391.99543598) < 1e-6); // two semitones down: G4
    send(s, MidiMessage::noteOn(0, 81, 100)); // a new note starts bent too
    DAWLAB_CHECK(std::abs(s.voice(voiceOf(s, 81)).frequency() - 783.99087196) < 1e-6);
    send(s, MidiMessage::pitchBend(0, 0));
    DAWLAB_CHECK(s.bendSemitones() == 0.0 && std::abs(s.voice(0).frequency() - 440.0) < 1e-9);

    // All notes off: everything releases - held, sustained, and a voice fading for a stolen note, whose note
    // then never starts. The pedal is forgotten too.
    send(s, MidiMessage::controller(0, Synth::kSustainPedal, 127));
    send(s, MidiMessage::noteOn(0, 72, 100));
    send(s, MidiMessage::noteOff(0, 72));
    send(s, MidiMessage::noteOn(0, 74, 100));
    run(s, 2000);
    send(s, MidiMessage::noteOn(0, 76, 100)); // the fifth: steals
    DAWLAB_CHECK(s.stolen() == 1);
    send(s, MidiMessage::controller(0, Synth::kAllNotesOff, 0));
    DAWLAB_CHECK(!s.pedalDown());
    for (int i = 0; i < 4; ++i)
        DAWLAB_CHECK(s.voice(i).isReleasing() && !s.voice(i).isStealing());
    run(s, 48000);
    DAWLAB_CHECK(s.activeVoices() == 0);
}

inline double maxStep(const std::vector<float>& x, size_t from, size_t to) {
    double m = 0.0;
    for (size_t i = from + 1; i < to; ++i)
        m = std::max(m, static_cast<double>(std::abs(x[i] - x[i - 1])));
    return m;
}

inline void checkStealingByEar() {
    // A four-note chord on four voices, then a fifth note: one voice fades in 2 ms and takes the new note.
    Synth s;
    s.prepare(kRate, 4, settings());
    std::vector<float> out(24000);
    std::vector<BlockEvent> ev{{0, MidiMessage::noteOn(0, 48, 127)}, {0, MidiMessage::noteOn(0, 55, 127)},
                               {0, MidiMessage::noteOn(0, 60, 127)}, {0, MidiMessage::noteOn(0, 64, 127)}};
    s.render(ev, out.data(), 12000);
    ev = {{100, MidiMessage::noteOn(0, 67, 127)}};
    s.render(ev, out.data() + 12000, 12000);
    const double before = maxStep(out, 6000, 12000);
    const double around = maxStep(out, 12000, 12600);
    std::printf("  stealing a voice of a 4-note chord: the largest step around it is %.3f, %.3f before it\n", around, before);
    DAWLAB_CHECK(around <= before * 1.2);
    // The new note started about 2 ms (96 samples) after its note on.
    DAWLAB_CHECK(voiceOf(s, 67) >= 0 && !s.voice(voiceOf(s, 67)).isStealing());

    Synth t;
    t.prepare(kRate, 4, settings());
    std::vector<BlockEvent> chord{{0, MidiMessage::noteOn(0, 48, 127)}, {0, MidiMessage::noteOn(0, 55, 127)},
                                  {0, MidiMessage::noteOn(0, 60, 127)}, {0, MidiMessage::noteOn(0, 64, 127)}};
    t.render(chord, out.data(), 12000);
    std::vector<BlockEvent> fifth{{0, MidiMessage::noteOn(0, 67, 127)}};
    t.render(fifth, out.data(), 95);
    DAWLAB_CHECK(t.voice(voiceOf(t, 67)).isStealing());
    std::vector<BlockEvent> none;
    t.render(none, out.data(), 3);
    DAWLAB_CHECK(!t.voice(voiceOf(t, 67)).isStealing());
}

struct Timed {
    std::int64_t at = 0;
    MidiMessage message;
};

inline std::vector<float> perform(const std::vector<Timed>& song, std::int64_t length, int block, std::mt19937* sizes, int& worst) {
    Synth s;
    s.prepare(kRate, 6, settings());
    std::vector<float> out(static_cast<size_t>(length));
    std::vector<BlockEvent> ev;
    ev.reserve(song.size());
    std::uniform_int_distribution<int> size(1, 700);
    size_t next = 0;
    worst = 0;
    for (std::int64_t done = 0; done < length;) {
        const int n = static_cast<int>(std::min<std::int64_t>(sizes != nullptr ? size(*sizes) : block, length - done));
        ev.clear();
        while (next < song.size() && song[next].at < done + n) {
            ev.push_back({static_cast<int>(song[next].at - done), song[next].message});
            ++next;
        }
        s.render(ev, out.data() + done, n);
        // One pitch is never held on two voices.
        int held[128] = {};
        for (int i = 0; i < s.numVoices(); ++i)
            if (s.voice(i).isActive() && !s.voice(i).isReleasing())
                worst = std::max(worst, ++held[s.voice(i).pitch()]);
        done += n;
    }
    worst = s.activeVoices() == 0 ? worst : 99; // and it ends in silence
    return out;
}

inline void checkRandomPerformance() {
    std::mt19937 rng(4004);
    std::uniform_int_distribution<int> pitch(52, 64), vel(0, 127), what(0, 99), gap(0, 3000);
    std::vector<Timed> song;
    std::int64_t t = 0;
    for (int i = 0; i < 800; ++i) {
        t += gap(rng);
        const int w = what(rng);
        if (w < 45)
            song.push_back({t, MidiMessage::noteOn(0, pitch(rng), vel(rng))}); // velocity 0 now and then: a note off
        else if (w < 85)
            song.push_back({t, MidiMessage::noteOff(0, pitch(rng))});
        else if (w < 92)
            song.push_back({t, MidiMessage::controller(0, Synth::kSustainPedal, what(rng) < 50 ? 0 : 127)});
        else if (w < 98)
            song.push_back({t, MidiMessage::pitchBend(0, what(rng) * 160 - 8000)});
        else
            song.push_back({t, MidiMessage::controller(0, Synth::kAllNotesOff, 0)});
    }
    song.push_back({t + 10, MidiMessage::controller(0, Synth::kAllNotesOff, 0)});
    const std::int64_t length = t + 48000;
    int worst = 0;
    const std::vector<float> ref = perform(song, length, 512, nullptr, worst);
    DAWLAB_CHECK(worst == 1);
    int differ = 0;
    for (const int block : {1, 64, 0}) {
        int w = 0;
        const std::vector<float> got = perform(song, length, block, block == 0 ? &rng : nullptr, w);
        differ += got == ref && w == 1 ? 0 : 1;
    }
    double peak = 0.0;
    for (const float x : ref)
        peak = std::max(peak, static_cast<double>(std::abs(x)));
    std::printf("  a random performance of 800 events on 6 voices, %.1f s: the same in blocks of 1, 64, 512 and random: %s\n",
                static_cast<double>(length) / kRate, differ == 0 ? "yes" : "no");
    DAWLAB_CHECK(differ == 0 && peak > 0.1);
}

} // namespace step04_check

int main() {
    using namespace step04_check;
    std::printf("Step 4: polyphony\n");
    checkAllocation();
    checkAllocationCases();
    checkReleaseOnce();
    checkPedal();
    checkBendAndPanic();
    if (dawlab::test::counters().failures > 0)
        return dawlab::test::summary("l18_step04");
    checkStealingByEar();
    checkRandomPerformance();
    return dawlab::test::summary("l18_step04");
}
