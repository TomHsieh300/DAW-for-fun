// Exercise 2 (solution): MIDI by the numbers.
//
// (a) ticks and samples at a tempo that does not divide evenly, and a running sum that drifts
// (b) how long a chord takes on a MIDI cable
// (c) jitter or a constant delay: what a block size costs
// (d) how fine a tick is
// (e) where a sawtooth's harmonics fold back
// (f) quantize with strength and swing
//
// The derivations are in solutions/README.md; this program only proves the numbers, with Step 1's
// sampleOfTick() where it applies.

#include "../exercises/model.h"

#include <dawlab/Check.h>

#include <cmath>
#include <cstdio>

using namespace minidaw;

int main() {
    std::printf("Exercise 2 - MIDI by the numbers\n");

    // (a) 97 BPM at 44.1 kHz.
    const Tempo odd{44100.0, 97.0};
    const double spt = odd.samplesPerTick();
    const std::int64_t bar33 = 32LL * 4 * kTicksPerQuarter;
    const std::int64_t at = sampleOfTick(bar33, odd);
    std::printf("(a) %.6f samples a tick; bar 33 = tick %lld = sample %lld (%.4f s)\n", spt, static_cast<long long>(bar33),
                static_cast<long long>(at), at / odd.sampleRate);
    DAWLAB_CHECK_NEAR(spt, 2646000.0 / 93120.0, 1e-12);
    DAWLAB_CHECK(bar33 == 122880 && at == 3491629);
    // A float that adds samplesPerTick once a tick, for an hour.
    const std::int64_t ticksPerHour = static_cast<std::int64_t>(97.0 * kTicksPerQuarter * 60.0);
    float sum = 0.0f;
    for (std::int64_t t = 0; t < ticksPerHour; ++t)
        sum += static_cast<float>(spt);
    const std::int64_t exact = sampleOfTick(ticksPerHour, odd);
    const double drift = static_cast<double>(sum) - static_cast<double>(exact);
    std::printf("    after an hour (%lld ticks): exact sample %lld, a running float sum is %.0f samples off (%.2f s)\n",
                static_cast<long long>(ticksPerHour), static_cast<long long>(exact), drift, drift / odd.sampleRate);
    DAWLAB_CHECK(ticksPerHour == 5587200 && exact == 158760000);
    DAWLAB_CHECK(std::fabs(drift) > 44100.0); // more than a second

    // (b) A MIDI cable: 31 250 bits a second, 10 bits a byte (start, 8 data, stop).
    const double byteSeconds = 10.0 / 31250.0;
    const int chord = 10;
    const int withRunning = 3 + (chord - 1) * 2;
    const int without = 3 * chord;
    std::printf("(b) a byte takes %.0f us; a note on %.2f ms; a chord of %d notes %d bytes = %.2f ms (%d bytes = %.2f ms "
                "without running status)\n",
                byteSeconds * 1e6, 3 * byteSeconds * 1e3, chord, withRunning, withRunning * byteSeconds * 1e3, without,
                without * byteSeconds * 1e3);
    DAWLAB_CHECK_NEAR(byteSeconds, 320e-6, 1e-15);
    DAWLAB_CHECK(withRunning == 21 && without == 30);
    DAWLAB_CHECK_NEAR(withRunning * byteSeconds * 1e3, 6.72, 1e-9);
    // The last note of the chord in samples at 48 kHz, from the first one's start.
    DAWLAB_CHECK_NEAR((withRunning - 2) * byteSeconds * 48000.0, 291.84, 1e-9);

    // (c) Blocks of 512 at 48 kHz, an output latency of 512.
    const double block = 512.0 / 48000.0;
    const double scheduled = (2 * 512 + 512) / 48000.0;
    std::printf("(c) as soon as possible: 0 to %.2f ms of jitter; scheduled: always %.0f ms; at 128 samples: %.2f ms and %.0f ms\n",
                block * 1e3, scheduled * 1e3, 128.0 / 48000.0 * 1e3, (3 * 128) / 48000.0 * 1e3);
    DAWLAB_CHECK_NEAR(block * 1e3, 10.6667, 1e-4);
    DAWLAB_CHECK_NEAR(scheduled * 1e3, 32.0, 1e-9);
    DAWLAB_CHECK_NEAR((3 * 128) / 48000.0 * 1e3, 8.0, 1e-9);

    // (d) A tick at 120 and 200 BPM, 48 kHz.
    const Tempo t120{48000.0, 120.0}, t200{48000.0, 200.0};
    std::printf("(d) a tick is %.0f samples = %.4f ms at 120 BPM, %.0f samples = %.4f ms at 200 BPM; a 1/16 at 120 BPM is %lld samples\n",
                t120.samplesPerTick(), t120.samplesPerTick() / 48.0, t200.samplesPerTick(), t200.samplesPerTick() / 48.0,
                static_cast<long long>(sampleOfTick(240, t120)));
    DAWLAB_CHECK(t120.samplesPerTick() == 25.0 && t200.samplesPerTick() == 15.0 && sampleOfTick(240, t120) == 6000);

    // (e) A sawtooth on C7 (note 96) at 48 kHz: harmonic k at k x f, folded back into [0, 24 kHz].
    const double f = 440.0 * std::pow(2.0, (96 - 69) / 12.0);
    auto folded = [](double hz) {
        double x = std::fmod(hz, 48000.0);
        return x > 24000.0 ? 48000.0 - x : x;
    };
    int firstAbove = 0, firstLow = 0;
    for (int k = 1; k < 100 && (firstAbove == 0 || firstLow == 0); ++k) {
        if (firstAbove == 0 && k * f > 24000.0)
            firstAbove = k;
        if (firstAbove != 0 && firstLow == 0 && folded(k * f) < 10000.0)
            firstLow = k;
    }
    std::printf("(e) C7 = %.2f Hz; harmonic %d (%.0f Hz) is the first above Nyquist and folds to %.0f Hz; harmonic %d is the first "
                "alias below 10 kHz, at %.0f Hz, %.1f dB under the fundamental without PolyBLEP\n",
                f, firstAbove, firstAbove * f, folded(firstAbove * f), firstLow, folded(firstLow * f), 20.0 * std::log10(1.0 / firstLow));
    DAWLAB_CHECK_NEAR(f, 2093.0045, 1e-3);
    DAWLAB_CHECK(firstAbove == 12 && firstLow == 19);
    DAWLAB_CHECK_NEAR(folded(12 * f), 22884, 1.0);
    DAWLAB_CHECK_NEAR(folded(19 * f), 8233, 1.0);

    // (f) Step 6's quantize: grid 1/8 = 480 ticks; swing 0.2 moves every second line by 96 ticks.
    const int grid = 480;
    const long swingTicks = std::lround(0.2 * grid);
    const std::int64_t swungSamples = sampleOfTick(swingTicks, t120);
    // A note played 30 ticks after line 2 (tick 990), strength 0.5, no swing: halfway to 960.
    const long halfway = 990 + std::lround(0.5 * (960 - 990));
    // The same note with swing 0.2: line 2 is even, so it does not move; a note at 1430 goes towards 1440 + 96.
    const long swung = 1430 + std::lround(1.0 * (1440 + swingTicks - 1430));
    std::printf("(f) swing 0.2 on 1/8: %ld ticks = %lld samples = %.0f ms late at 120 BPM; strength 0.5 moves tick 990 to %ld; "
                "tick 1430 with swing goes to %ld\n",
                swingTicks, static_cast<long long>(swungSamples), swungSamples / 48.0, halfway, swung);
    DAWLAB_CHECK(swingTicks == 96 && swungSamples == 2400 && halfway == 975 && swung == 1536);

    return dawlab::test::summary("l18_ex2");
}
