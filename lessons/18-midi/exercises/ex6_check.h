#pragma once
// Exercise 6 - the self-check (main). Included at the end of the starter and of the solution.

#include "ex6_mono_synth.h"

#include <dawlab/Check.h>

#include <cmath>
#include <cstdio>
#include <vector>

namespace ex6::check {

constexpr double kRate = 48000.0;
constexpr int kGlide = 2400; // 0.05 s

inline MonoSynth synth(bool legato = true, double glide = 0.05) {
    MonoSynth s;
    MonoSettings settings;
    settings.legato = legato;
    settings.glideSeconds = glide;
    s.prepare(kRate, settings);
    return s;
}

// Renders n samples, returns their peak.
inline float play(MonoSynth& s, int n) {
    std::vector<float> buf(static_cast<size_t>(n), 0.0f);
    s.render(buf.data(), n);
    float peak = 0.0f;
    for (const float v : buf)
        peak = std::max(peak, std::fabs(v));
    return peak;
}

inline MidiMessage on(int note, int velocity = 100) {
    return MidiMessage::noteOn(0, note, velocity);
}
inline MidiMessage off(int note) {
    return MidiMessage::noteOff(0, note);
}

inline void checkOneNote() {
    MonoSynth s = synth();
    DAWLAB_CHECK(s.target() == -1);
    s.handle(on(60, 64));
    DAWLAB_CHECK(s.target() == 60 && s.pitch() == 60.0 && s.envelope().stage() == Adsr::Stage::Attack);
    DAWLAB_CHECK(s.gain() == minidaw::velocityGain(64) && s.numKeys() == 1);
    const float peak = play(s, 4800);
    s.handle(off(60));
    DAWLAB_CHECK(s.envelope().stage() == Adsr::Stage::Release && s.numKeys() == 0);
    play(s, 24000);
    DAWLAB_CHECK(!s.envelope().isActive() && play(s, 512) == 0.0f && peak > 0.05f);
    std::printf("  one note: starts at its pitch with an attack, sounds (peak %.2f), releases to silence\n", peak);
}

inline void checkLegatoGlide() {
    MonoSynth s = synth();
    s.handle(on(60, 90));
    play(s, 12000); // into the sustain
    const double level = s.envelope().level();
    s.handle(on(64, 50));
    DAWLAB_CHECK(s.envelope().stage() == Adsr::Stage::Sustain && s.envelope().level() == level); // not restarted
    DAWLAB_CHECK(s.target() == 64 && s.pitch() == 60.0 && s.gain() == minidaw::velocityGain(50));
    play(s, 1200);
    DAWLAB_CHECK_NEAR(s.pitch(), 62.0, 1e-9);
    play(s, 1199);
    const bool notYet = s.pitch() < 64.0;
    play(s, 1);
    DAWLAB_CHECK(notYet && s.pitch() == 64.0);
    play(s, 1000);
    DAWLAB_CHECK(s.pitch() == 64.0);
    std::printf("  legato: E over a held C keeps the envelope and slides 4 semitones in exactly %d samples\n", kGlide);

    // The newest key goes up: back to C, sliding, with C's velocity.
    s.handle(off(64));
    DAWLAB_CHECK(s.target() == 60 && s.gain() == minidaw::velocityGain(90) && s.envelope().stage() == Adsr::Stage::Sustain);
    play(s, 600);
    DAWLAB_CHECK_NEAR(s.pitch(), 63.0, 1e-9);
    // A new note during the slide: a new slide from where the pitch is, over the whole glide time.
    s.handle(on(67));
    play(s, 1200);
    DAWLAB_CHECK_NEAR(s.pitch(), 65.0, 1e-9);
    play(s, 1200);
    DAWLAB_CHECK(s.pitch() == 67.0);
    // An octave takes as long as a semitone.
    s.handle(off(67));
    s.handle(on(72));
    play(s, kGlide - 1);
    const bool octaveNotYet = s.pitch() < 72.0;
    play(s, 1);
    DAWLAB_CHECK(octaveNotYet && s.pitch() == 72.0);
    s.handle(on(73));
    play(s, kGlide - 1);
    const bool semitoneNotYet = s.pitch() < 73.0;
    play(s, 1);
    DAWLAB_CHECK(semitoneNotYet && s.pitch() == 73.0);
    std::printf("  back to the key still down, a new slide from mid-slide, an octave as long as a semitone: done\n");
}

inline void checkPriority() {
    MonoSynth s = synth(true, 0.0);
    s.handle(on(60));
    s.handle(on(64));
    s.handle(on(67));
    DAWLAB_CHECK(s.target() == 67 && s.pitch() == 67.0 && s.numKeys() == 3);
    s.handle(off(64)); // not the one sounding
    DAWLAB_CHECK(s.target() == 67 && s.pitch() == 67.0 && s.numKeys() == 2);
    s.handle(off(67));
    DAWLAB_CHECK(s.target() == 60 && s.pitch() == 60.0 && s.envelope().isActive() && s.envelope().stage() != Adsr::Stage::Release);
    s.handle(off(64)); // not down any more
    DAWLAB_CHECK(s.numKeys() == 1 && s.target() == 60);
    s.handle(on(60)); // already down
    DAWLAB_CHECK(s.numKeys() == 1);
    s.handle(off(60));
    DAWLAB_CHECK(s.envelope().stage() == Adsr::Stage::Release && s.numKeys() == 0);
    std::printf("  last-note priority: C E G, E up changes nothing, G up returns to C, C up releases\n");

    // From silence: no slide, even while the last note still fades.
    MonoSynth t = synth();
    t.handle(on(60));
    play(t, 2000);
    t.handle(off(60));
    play(t, 2000);
    t.handle(on(64));
    DAWLAB_CHECK(t.pitch() == 64.0 && t.envelope().stage() == Adsr::Stage::Attack);
    play(t, 10);
    DAWLAB_CHECK(t.pitch() == 64.0);

    // Too many keys: the oldest is pushed out.
    MonoSynth u = synth(true, 0.0);
    for (int p = 40; p <= 56; ++p)
        u.handle(on(p));
    DAWLAB_CHECK(u.numKeys() == MonoSynth::kMaxKeys && u.target() == 56);
    u.handle(off(56));
    DAWLAB_CHECK(u.target() == 55);
    for (int p = 41; p <= 55; ++p)
        u.handle(off(p));
    DAWLAB_CHECK(u.numKeys() == 0 && u.envelope().stage() == Adsr::Stage::Release);
    u.handle(off(40));
    DAWLAB_CHECK(u.numKeys() == 0);

    // All notes off.
    MonoSynth v = synth();
    v.handle(on(60));
    v.handle(on(62));
    v.handle(MidiMessage::controller(0, 123, 0));
    DAWLAB_CHECK(v.numKeys() == 0 && v.envelope().stage() == Adsr::Stage::Release);
    v.handle(on(70)); // from silence again
    DAWLAB_CHECK(v.pitch() == 70.0 && v.envelope().stage() == Adsr::Stage::Attack);
}

inline void checkRetrigger() {
    MonoSynth s = synth(false);
    s.handle(on(60));
    play(s, 12000);
    const double level = s.envelope().level();
    s.handle(on(64));
    DAWLAB_CHECK(s.envelope().stage() == Adsr::Stage::Attack && s.envelope().level() == level && level > 0.5);
    play(s, 1200);
    DAWLAB_CHECK_NEAR(s.pitch(), 62.0, 1e-9); // it still glides
    s.handle(off(64));
    DAWLAB_CHECK(s.envelope().stage() == Adsr::Stage::Attack && s.target() == 60);
    // A key that is not the one sounding goes up: nothing restarts.
    MonoSynth r = synth(false, 0.0);
    r.handle(on(60));
    r.handle(on(64));
    r.handle(on(67));
    play(r, 12000);
    r.handle(off(64));
    DAWLAB_CHECK(r.envelope().stage() == Adsr::Stage::Sustain && r.target() == 67);
    std::printf("  legato off: every new note restarts the attack from the level it is at, and still glides\n");

    // A note from silence starts its saw from the beginning: the same samples as the first note ever.
    MonoSynth fresh = synth(), used = synth();
    used.handle(on(50));
    play(used, 3001); // a phase that is not 0
    used.handle(off(50));
    play(used, 48000); // silent again
    fresh.handle(on(62));
    used.handle(on(62));
    std::vector<float> a(2000, 0.0f), b(2000, 0.0f);
    fresh.render(a.data(), 2000);
    used.render(b.data(), 2000);
    DAWLAB_CHECK(a == b);

    // The output follows the pitch: a slide of an octave doubles the number of cycles a second.
    MonoSynth w = synth(true, 0.0);
    w.handle(on(57)); // 220 Hz
    std::vector<float> buf(48000, 0.0f);
    w.render(buf.data(), 24000);
    w.handle(on(69)); // 440 Hz
    w.render(buf.data() + 24000, 24000);
    auto crossings = [&buf](int from, int to) {
        int n = 0;
        for (int i = from + 1; i < to; ++i)
            n += buf[static_cast<size_t>(i - 1)] < 0.0f && buf[static_cast<size_t>(i)] >= 0.0f ? 1 : 0;
        return n;
    };
    const int low = crossings(12000, 24000), high = crossings(36000, 48000);
    std::printf("  the sound: %d cycles in a quarter second at A3, %d at A4\n", low, high);
    DAWLAB_CHECK(std::abs(low - 55) <= 1 && std::abs(high - 110) <= 1);
}

} // namespace ex6::check

int main() {
    using namespace ex6::check;
    std::printf("Exercise 6 - mono, legato and glide\n");
    checkOneNote();
    checkLegatoGlide();
    checkPriority();
    checkRetrigger();
    return dawlab::test::summary("l18_ex6");
}
