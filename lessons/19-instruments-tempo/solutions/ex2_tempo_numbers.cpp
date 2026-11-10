// Exercise 2 (solution): tempo by the numbers.
//
// (a) how long a ramp takes, and where its half time is
// (b) the sample of a bar on a map, at two sample rates
// (c) the latency plan of Step 3, and what a key played now costs
// (d) what a MIDI file's whole microseconds lose
// (e) a ramp as steps: the tempo in the middle of a step, or the one that keeps the time
//
// The derivations are in solutions/README.md; this program only proves the numbers, with Step 1's map.

#include "../steps/step02-transport/minidaw.h"

#include <dawlab/Check.h>

#include <cmath>
#include <cstdio>

using namespace minidaw;

int main() {
    std::printf("Exercise 2 - tempo by the numbers\n");

    // (a) 60 to 120 BPM over four bars.
    TempoMap ramp;
    ramp.set(48000.0, {{0, 60.0, true}, {4 * 3840, 120.0}});
    const double whole = ramp.secondsAt(4 * 3840), half = ramp.secondsAt(2 * 3840);
    const double halfTimeTick = ramp.tickAt(whole / 2.0);
    std::printf("(a) the ramp takes %.4f s (16 ln 2); its middle bar line is at %.4f s (16 ln 1.5); half the time is at tick %.1f\n"
                "    (bar %d, %.2f beats in), where the tempo is %.2f BPM (60 x sqrt 2)\n",
                whole, half, halfTimeTick, static_cast<int>(halfTimeTick / 3840) + 1, std::fmod(halfTimeTick, 3840.0) / 960.0,
                60.0 + 60.0 * halfTimeTick / (4 * 3840));
    DAWLAB_CHECK_NEAR(whole, 16.0 * std::log(2.0), 1e-12);
    DAWLAB_CHECK_NEAR(half, 16.0 * std::log(1.5), 1e-12);
    DAWLAB_CHECK_NEAR(halfTimeTick, 15360.0 * (std::sqrt(2.0) - 1.0), 1e-6);
    DAWLAB_CHECK_NEAR(60.0 + 60.0 * halfTimeTick / 15360.0, 60.0 * std::sqrt(2.0), 1e-9);

    // (b) 120 BPM, and 90 from bar 3: where is bar 5?
    TempoMap jump48, jump441;
    jump48.set(48000.0, {{0, 120.0}, {2 * 3840, 90.0}});
    jump441.set(44100.0, {{0, 120.0}, {2 * 3840, 90.0}});
    std::printf("(b) bar 5 at %.4f s: sample %lld at 48 kHz, %lld at 44.1 kHz\n", jump48.secondsAt(4 * 3840),
                static_cast<long long>(jump48.sampleOfTick(4 * 3840)), static_cast<long long>(jump441.sampleOfTick(4 * 3840)));
    DAWLAB_CHECK_NEAR(jump48.secondsAt(4 * 3840), 4.0 + 8.0 * 60.0 / 90.0, 1e-12);
    DAWLAB_CHECK(jump48.sampleOfTick(4 * 3840) == 448000 && jump441.sampleOfTick(4 * 3840) == 411600);

    // (c) Track latencies 0, 256, 64 + 32 and 4096; the keyboard scheduled 1024 samples late, an output latency of 512.
    const int latency[] = {0, 256, 96, 4096};
    int total = 0;
    for (const int l : latency)
        total = std::max(total, l);
    const int heard = 1024 + 0 + (total - 0) + 512;  // on the synth, waiting for the sampler
    const int heardLow = 1024 + 0 + 0 + 512;         // the synth armed, low-latency monitoring
    const int totalLow = 4096;                       // ... the others still wait for the sampler
    std::printf("(c) total %d; waits %d %d %d %d; a key on the synth is heard after %d samples (%.1f ms), with low-latency\n"
                "    monitoring after %d (%.0f ms) - early against the others by %d samples (%.1f ms)\n",
                total, total - latency[0], total - latency[1], total - latency[2], total - latency[3], heard, heard / 48.0, heardLow,
                heardLow / 48.0, totalLow, totalLow / 48.0);
    DAWLAB_CHECK(total == 4096 && heard == 5632 && heardLow == 1536);
    DAWLAB_CHECK_NEAR(heard / 48.0, 117.33, 0.01);

    // (d) 97.5 BPM in a MIDI file: a whole number of microseconds a beat.
    const long micros = std::lround(60000000.0 / 97.5);
    const double back = 60000000.0 / static_cast<double>(micros);
    const double drift = 600.0 * (97.5 / back - 1.0); // after ten minutes of the song
    std::printf("(d) 97.5 BPM is %ld us a beat, which reads back as %.6f BPM: %.3f ms late after ten minutes\n", micros, back, drift * 1e3);
    DAWLAB_CHECK(micros == 615385);
    DAWLAB_CHECK_NEAR(back, 97.499939, 1e-6);
    DAWLAB_CHECK_NEAR(drift * 1e3, 0.375, 0.001);

    // (e) 120 to 60 BPM over one beat, written as four steps of a sixteenth.
    TempoMap down;
    down.set(48000.0, {{0, 120.0, true}, {960, 60.0}});
    const double exact = down.secondsAt(960);
    double middle = 0.0, kept = 0.0;
    for (int step = 0; step < 4; ++step) {
        const double from = step * 240.0, to = from + 240.0;
        middle += 240.0 * 60.0 / (960.0 * (120.0 - 60.0 * (from + 120.0) / 960.0)); // the tempo in the middle of the step
        const double bpm = 240.0 * 60.0 / (960.0 * (down.secondsAt(to) - down.secondsAt(from))); // the tempo that keeps its time
        kept += 240.0 * 60.0 / (960.0 * bpm);
    }
    std::printf("(e) the beat takes %.5f s (ln 2); steps at their middle tempo take %.5f s: %.2f ms early; steps that keep the time: %.5f s\n",
                exact, middle, (exact - middle) * 1e3, kept);
    DAWLAB_CHECK_NEAR(exact, std::log(2.0), 1e-12);
    DAWLAB_CHECK_NEAR((exact - middle) * 1e3, 1.93, 0.01);
    DAWLAB_CHECK_NEAR(kept, exact, 1e-12);

    return dawlab::test::summary("l19_ex2");
}
