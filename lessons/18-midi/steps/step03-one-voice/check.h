#pragma once
// check.h - the self-check of Step 3 (one voice). Defines main(); included at the END of starter/step03.cpp
// and solution/step03.cpp, so both run exactly the same checks.
//
// The oscillator is measured, not looked at: the level of every alias against the fundamental, for the
// naive saw and for PolyBLEP. The envelope is checked for its stages and for never jumping, the voice for
// mixing, velocity, silence, and block-size independence.

#include <dawlab/Common.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace step03_check {

using namespace minidaw;

constexpr double kRate = 48000.0;
constexpr double kPi = 3.14159265358979323846;

// The level of one frequency in a signal (Hann window, Goertzel), in dB relative to a full-scale sine.
inline double levelDb(const std::vector<float>& x, double hz) {
    const size_t n = x.size();
    const double w = 2.0 * kPi * hz / kRate;
    double re = 0.0, im = 0.0, windowSum = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double hann = 0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) / static_cast<double>(n - 1));
        re += hann * x[i] * std::cos(w * static_cast<double>(i));
        im -= hann * x[i] * std::sin(w * static_cast<double>(i));
        windowSum += hann;
    }
    return 20.0 * std::log10(std::max(2.0 * std::sqrt(re * re + im * im) / windowSum, 1e-12));
}

// Folds a frequency into [0, rate / 2].
inline double folded(double hz) {
    double f = std::fmod(hz, kRate);
    return f > kRate / 2.0 ? kRate - f : f;
}

inline void checkFrequency() {
    DAWLAB_CHECK(std::abs(noteFrequency(69) - 440.0) < 1e-9);
    DAWLAB_CHECK(std::abs(noteFrequency(81) - 880.0) < 1e-9);
    DAWLAB_CHECK(std::abs(noteFrequency(57) - 220.0) < 1e-9);
    DAWLAB_CHECK(std::abs(noteFrequency(60) - 261.6255653) < 1e-6);
    DAWLAB_CHECK(std::abs(noteFrequency(69, 2.0) - 493.8833013) < 1e-6);
    DAWLAB_CHECK(std::abs(noteFrequency(69, -0.5) - 427.4740541) < 1e-6);
}

inline void checkPolyBlep() {
    const double dt = 0.01;
    DAWLAB_CHECK(polyBlep(0.5, dt) == 0.0 && polyBlep(dt, dt) == 0.0 && polyBlep(1.0 - dt, dt) == 0.0);
    DAWLAB_CHECK(std::abs(polyBlep(0.0, dt) + 1.0) < 1e-12);         // right after the jump: -1
    DAWLAB_CHECK(std::abs(polyBlep(0.999999999, dt) - 1.0) < 1e-6);  // right before it: +1
    DAWLAB_CHECK(std::abs(polyBlep(0.005, dt) + 0.25) < 1e-12);      // x = 0.5: 1 - 0.25 - 1
    DAWLAB_CHECK(std::abs(polyBlep(0.995, dt) - 0.25) < 1e-12);      // x = -0.5: 0.25 - 1 + 1
}

inline std::vector<float> saw(double hz, bool naive, int n) {
    Saw s;
    s.setFrequency(hz, kRate);
    std::vector<float> x(static_cast<size_t>(n));
    for (float& v : x)
        v = naive ? s.nextNaive() : s.next();
    return x;
}

inline void checkSaw() {
    // Shape: rising from -1 to 1, mean 0, the right number of cycles.
    const std::vector<float> slow = saw(100.0, false, 48000);
    double sum = 0.0;
    int wraps = 0;
    float lo = 0.0f, hi = 0.0f;
    for (size_t i = 0; i < slow.size(); ++i) {
        sum += slow[i];
        lo = std::min(lo, slow[i]);
        hi = std::max(hi, slow[i]);
        // A fall counts once, however many samples PolyBLEP spreads it over.
        if (i > 1 && slow[i] < slow[i - 1] && !(slow[i - 1] < slow[i - 2]))
            ++wraps;
    }
    DAWLAB_CHECK(std::abs(sum / 48000.0) < 1e-3 && lo > -1.01f && hi < 1.01f && wraps == 99);
    // Away from the jump it is the naive saw.
    const std::vector<float> naive = saw(100.0, true, 48000);
    DAWLAB_CHECK(slow[240] == naive[240] && slow[100] == naive[100]);

    // Aliasing. Every harmonic above 24 kHz folds back; measure each alias that does not fall next to a
    // real harmonic, against the fundamental.
    struct Aliases {
        double fundamental = 0.0, worst = -999.0, worstBelow10k = -999.0;
    };
    auto measure = [](double f, bool naive) {
        const std::vector<float> x = saw(f, naive, 1 << 15);
        Aliases a;
        if (f < 1.0)
            return a;
        a.fundamental = levelDb(x, f);
        for (int k = 2; k * f < 6.0 * kRate; ++k) {
            if (k * f < kRate / 2.0)
                continue;
            const double alias = folded(k * f);
            bool nearHarmonic = false;
            for (int h = 1; h * f < kRate / 2.0; ++h)
                nearHarmonic = nearHarmonic || std::abs(alias - h * f) < 60.0;
            if (nearHarmonic)
                continue;
            const double db = levelDb(x, alias);
            a.worst = std::max(a.worst, db);
            if (alias < 10000.0)
                a.worstBelow10k = std::max(a.worstBelow10k, db);
        }
        return a;
    };
    // A saw's harmonic k is 2/(pi k): the fundamental at -3.9 dB, the third at -13.5 dB.
    const Aliases a4 = measure(440.0, false), a4naive = measure(440.0, true);
    DAWLAB_CHECK(std::abs(a4.fundamental - 20.0 * std::log10(2.0 / kPi)) < 0.2);
    DAWLAB_CHECK(std::abs(levelDb(saw(440.0, false, 1 << 15), 1320.0) - 20.0 * std::log10(2.0 / (3.0 * kPi))) < 0.2);
    // A4: every alias of the naive saw 40 dB down; with PolyBLEP 70.
    DAWLAB_CHECK(a4naive.fundamental - a4naive.worst < 45.0 && a4.fundamental - a4.worst > 65.0);
    // D#7, 2489 Hz: the aliases that fold far down - below 10 kHz, where they are heard as wrong notes - go from
    // 24 dB to more than 45 dB below the fundamental. The ones just below 24 kHz improve much less.
    const double d7 = 2489.0158; // MIDI note 99, given here: the check must not depend on noteFrequency() working
    const Aliases high = measure(d7, false), highNaive = measure(d7, true);
    std::printf("  A4: worst alias %.1f dB down naive, %.1f dB with PolyBLEP\n", a4naive.fundamental - a4naive.worst,
                a4.fundamental - a4.worst);
    std::printf("  D#7: aliases below 10 kHz %.1f dB down naive, %.1f dB with PolyBLEP (just below 24 kHz: %.1f, %.1f)\n",
                highNaive.fundamental - highNaive.worstBelow10k, high.fundamental - high.worstBelow10k, highNaive.fundamental - highNaive.worst,
                high.fundamental - high.worst);
    DAWLAB_CHECK(highNaive.fundamental - highNaive.worstBelow10k < 30.0);
    DAWLAB_CHECK(high.fundamental - high.worstBelow10k > 45.0);
    DAWLAB_CHECK(high.worst < highNaive.worst - 5.0);
}

inline void checkAdsr() {
    Adsr e;
    e.set(kRate, 0.010, 0.100, 0.5, 0.200); // 480, 4800 samples to 0.5, 9600 samples
    DAWLAB_CHECK(!e.isActive() && e.next() == 0.0f);
    e.noteOn();
    std::vector<float> v;
    for (int i = 0; i < 480 + 4800 + 1000; ++i)
        v.push_back(e.next());
    DAWLAB_CHECK(v[0] == 0.0f && std::abs(v[240] - 0.5f) < 1e-4 && std::abs(v[480] - 1.0f) < 1e-4); // the attack
    DAWLAB_CHECK(std::abs(v[480 + 2400] - 0.75f) < 1e-3 && std::abs(v[480 + 4800] - 0.5f) < 1e-3);  // the decay
    DAWLAB_CHECK(e.stage() == Adsr::Stage::Sustain && v.back() == 0.5f);
    DAWLAB_CHECK(e.level() == 0.5); // exactly: the decay stops at the sustain level, not a little below it
    float peak = 0.0f;
    for (const float x : v)
        peak = std::max(peak, x);
    DAWLAB_CHECK(peak <= 1.0f); // no overshoot
    e.noteOff();
    DAWLAB_CHECK(e.stage() == Adsr::Stage::Release);
    int samples = 0;
    while (e.isActive() && samples < 100000) {
        e.next();
        ++samples;
    }
    DAWLAB_CHECK(std::abs(samples - 9600) <= 1 && e.level() == 0.0);

    // Released half-way up the attack: from there, down, in the release time.
    e.noteOn();
    for (int i = 0; i < 144; ++i)
        e.next(); // 0.3
    e.noteOff();
    DAWLAB_CHECK(std::abs(e.next() - 0.3f) < 1e-3);
    samples = 1;
    while (e.isActive() && samples < 100000) {
        e.next();
        ++samples;
    }
    DAWLAB_CHECK(std::abs(samples - 9600) <= 1);

    // Played again during the release: up from where it is, not from 0. A note off when silent: nothing.
    e.noteOn();
    for (int i = 0; i < 1000; ++i)
        e.next();
    e.noteOff();
    for (int i = 0; i < 4800; ++i)
        e.next(); // about 0.4
    const double before = e.level();
    e.noteOn();
    DAWLAB_CHECK(e.stage() == Adsr::Stage::Attack && std::abs(e.next() - before) < 1e-6);
    Adsr quiet;
    quiet.set(kRate, 0.01, 0.1, 0.5, 0.2);
    quiet.noteOff();
    DAWLAB_CHECK(!quiet.isActive() && quiet.next() == 0.0f);

    // Never a jump, however short the times: 0 counts as 1 ms, so no step is bigger than 1/48.
    Adsr fast;
    fast.set(kRate, 0.0, 0.0, 0.0, 0.0);
    double biggest = 0.0, last = 0.0;
    for (int round = 0; round < 50; ++round) {
        if (round % 3 == 0)
            fast.noteOn();
        if (round % 3 == 2)
            fast.noteOff();
        for (int i = 0; i < round * 7 % 61 + 1; ++i) {
            const double x = fast.next();
            biggest = std::max(biggest, std::abs(x - last));
            last = x;
        }
    }
    DAWLAB_CHECK(biggest <= 1.0 / 48.0 + 1e-6);
    // Sustain 1: no decay to speak of, straight on to Sustain.
    Adsr full;
    full.set(kRate, 0.001, 0.1, 1.0, 0.1);
    full.noteOn();
    for (int i = 0; i < 100; ++i)
        full.next();
    DAWLAB_CHECK(full.stage() == Adsr::Stage::Sustain && full.level() == 1.0);
}

inline Voice::Settings settings() {
    Voice::Settings s;
    s.cutoffHz = 3000.0;
    s.attack = 0.01;
    s.decay = 0.1;
    s.sustain = 0.5;
    s.release = 0.05;
    return s;
}

inline double rms(const std::vector<float>& x, size_t from, size_t to) {
    double s = 0.0;
    for (size_t i = from; i < to; ++i)
        s += static_cast<double>(x[i]) * x[i];
    return std::sqrt(s / static_cast<double>(to - from));
}

// note on at 0, note off at `offAt`, `length` samples, in blocks of `block`.
inline std::vector<float> play(int pitch, int velocity, int offAt, int length, int block, float bed = 0.0f) {
    Voice v;
    v.prepare(kRate, settings());
    std::vector<float> out(static_cast<size_t>(length), bed);
    for (int done = 0; done < length; done += block) {
        const int n = std::min(block, length - done);
        if (done == 0)
            v.start(pitch, velocity);
        if (done <= offAt && offAt < done + n) { // split the block at the note off
            v.render(out.data() + done, offAt - done);
            v.release();
            v.render(out.data() + offAt, done + n - offAt);
        } else {
            v.render(out.data() + done, n);
        }
    }
    return out;
}

inline void checkVoice() {
    // It adds to what is there, and starts from silence: no click.
    const std::vector<float> a = play(57, 127, 24000, 30000, 512);
    const std::vector<float> bed = play(57, 127, 24000, 30000, 512, 0.25f);
    int wrong = 0;
    for (size_t i = 0; i < a.size(); ++i)
        wrong += std::abs(bed[i] - (a[i] + 0.25f)) < 1e-6f ? 0 : 1;
    DAWLAB_CHECK(wrong == 0);
    DAWLAB_CHECK(std::abs(a[0]) < 1e-6f && std::abs(a[1]) < 0.01f);
    // It sounds, then stops: 50 ms of release after the note off, then exact silence.
    DAWLAB_CHECK(rms(a, 12000, 24000) > 0.05);
    const size_t silentFrom = 24000 + 2400 + 2;
    double after = 0.0;
    for (size_t i = silentFrom; i < a.size(); ++i)
        after = std::max(after, static_cast<double>(std::abs(a[i])));
    DAWLAB_CHECK(after == 0.0);
    double tail = 0.0;
    for (size_t i = 24000 + 2300; i < silentFrom; ++i)
        tail = std::max(tail, static_cast<double>(std::abs(a[i])));
    DAWLAB_CHECK(tail < 0.02); // the release ends quietly

    // Velocity: 64 is about a quarter of 127.
    const std::vector<float> soft = play(57, 64, 24000, 30000, 512);
    const double ratio = rms(a, 12000, 24000) / rms(soft, 12000, 24000);
    DAWLAB_CHECK(std::abs(ratio - (127.0 / 64.0) * (127.0 / 64.0)) < 0.01);
    std::printf("  A3 at velocity 127 is %.2f times as loud as at 64 (%.1f dB)\n", ratio, 20.0 * std::log10(ratio));

    // The block size changes nothing.
    wrong = 0;
    for (const int block : {1, 7, 64, 1000}) {
        const std::vector<float> b = play(57, 127, 24000, 30000, block);
        for (size_t i = 0; i < a.size(); ++i)
            wrong += b[i] == a[i] ? 0 : 1;
    }
    DAWLAB_CHECK(wrong == 0);

    // The filter takes the top off: at a cutoff of 1500 Hz, A5's fifth harmonic (4400 Hz, 1.5 octaves above
    // it) is more than 12 dB weaker than with the filter wide open - 12 dB an octave.
    Voice open, closed;
    Voice::Settings wide = settings(), narrow = settings();
    wide.cutoffHz = 20000.0;
    narrow.cutoffHz = 1500.0;
    open.prepare(kRate, wide);
    closed.prepare(kRate, narrow);
    std::vector<float> o(24000, 0.0f), c(24000, 0.0f);
    open.start(81, 127);
    closed.start(81, 127);
    open.render(o.data(), 24000);
    closed.render(c.data(), 24000);
    const double h5open = levelDb(o, 5 * 880.0) - levelDb(o, 880.0), h5closed = levelDb(c, 5 * 880.0) - levelDb(c, 880.0);
    DAWLAB_CHECK(h5closed < h5open - 12.0);

    // Played again while it still sounds: the waveform does not jump back to the start of its cycle.
    Voice v;
    v.prepare(kRate, settings());
    std::vector<float> x(9600, 0.0f);
    v.start(57, 127);
    v.render(x.data(), 4800);
    v.release();
    v.render(x.data() + 4800, 1000);
    v.start(57, 127);
    v.render(x.data() + 5800, 3800);
    double jump = 0.0;
    for (size_t i = 5790; i < 5810; ++i)
        jump = std::max(jump, static_cast<double>(std::abs(x[i] - x[i - 1])));
    double typical = 0.0;
    for (size_t i = 4000; i < 4800; ++i)
        typical = std::max(typical, static_cast<double>(std::abs(x[i] - x[i - 1])));
    DAWLAB_CHECK(jump <= typical * 1.05);
    DAWLAB_CHECK(v.isActive() && v.envelope().stage() == Adsr::Stage::Decay);

    // More exactly: divide each sample by the envelope and the gain, and what is left - the filtered saw - goes
    // on through the second note on as if nothing had happened.
    auto filtered = [](bool again) {
        Voice w;
        w.prepare(kRate, settings());
        std::vector<double> out;
        float x = 0.0f;
        w.start(57, 127);
        for (int i = 0; i < 6000; ++i) {
            if (i == 4800)
                w.release();
            if (i == 5800 && again)
                w.start(57, 127);
            const double level = w.envelope().level();
            x = 0.0f;
            w.render(&x, 1);
            out.push_back(level > 1e-3 ? x / (level * velocityGain(127)) : 0.0);
        }
        return out;
    };
    const std::vector<double> released = filtered(false), playedAgain = filtered(true);
    int differ = 0;
    for (size_t i = 5800; i < 6000; ++i)
        differ += std::abs(released[i] - playedAgain[i]) < 1e-4 ? 0 : 1;
    DAWLAB_CHECK(differ == 0);

    // A note after a note that has died away sounds exactly like the first note of a new voice: the saw and the
    // filter start from rest, with nothing left over from before.
    Voice reused, fresh;
    reused.prepare(kRate, settings());
    fresh.prepare(kRate, settings());
    std::vector<float> first(20000, 0.0f), second(4800, 0.0f), clean(4800, 0.0f);
    reused.start(64, 100);
    reused.render(first.data(), 3000);
    reused.release();
    reused.render(first.data() + 3000, 17000);
    DAWLAB_CHECK(!reused.isActive());
    reused.start(57, 127);
    reused.render(second.data(), 4800);
    fresh.start(57, 127);
    fresh.render(clean.data(), 4800);
    DAWLAB_CHECK(second == clean);

    // An idle voice adds nothing at all, and a silent one is not active.
    Voice idle;
    idle.prepare(kRate, settings());
    std::vector<float> nothing(512, 0.5f);
    idle.render(nothing.data(), 512);
    DAWLAB_CHECK(!idle.isActive() && nothing[0] == 0.5f && nothing[511] == 0.5f);
}

} // namespace step03_check

int main() {
    using namespace step03_check;
    std::printf("Step 3: one voice\n");
    checkFrequency();
    checkPolyBlep();
    checkSaw();
    checkAdsr();
    if (dawlab::test::counters().failures > 0)
        return dawlab::test::summary("l18_step03");
    checkVoice();
    return dawlab::test::summary("l18_step03");
}
