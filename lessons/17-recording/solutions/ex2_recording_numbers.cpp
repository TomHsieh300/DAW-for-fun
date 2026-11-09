// Exercise 2 (solution): recording by the numbers.
//
// (a) where a take lands: the driver's latencies, and what 380 ms of real round trip does
// (b) how fast a take grows, and how long a WAV take can be
// (c) how large the ring must be for a disk that stalls
// (d) what monitoring adds, directly and through the inserts
// (e) what Exercise 3 saves
//
// The derivations are in solutions/README.md; this program only proves the numbers, with Step 4's
// place() and takesOf() where they apply.

#include "../exercises/model.h"

#include <dawlab/Check.h>

#include <cmath>
#include <cstdio>

using namespace minidaw;

int main() {
    std::printf("Exercise 2 - recording by the numbers\n");
    constexpr double kRate = 48000.0;

    // (a) The transport is at 2 s (bar 2 at 120 BPM in 4/4 is 2 s) when the first block comes in.
    const Latencies driver{480, 480, 240, 0};
    const Placement p = place(96000, driver, 48000);
    std::printf("(a) L = %lld samples = %.1f ms; the take starts at %lld (%.4f s)\n", static_cast<long long>(driver.total()),
                1e3 * driver.total() / kRate, static_cast<long long>(p.timelineStart), p.timelineStart / kRate);
    DAWLAB_CHECK(driver.total() == 1200 && p.timelineStart == 94800 && p.skipFrames == 0);
    // Step 7's laptop: the real round trip was about 380 ms, and the driver said 25.
    const std::int64_t real = static_cast<std::int64_t>(std::lround(0.380 * kRate));
    const std::int64_t off = real - driver.total();
    const double beats = off / kRate / 0.5; // a beat is 0.5 s at 120 BPM
    std::printf("    with a real round trip of %lld: %lld samples (%.0f ms) too late = %.2f beats at 120 BPM\n", static_cast<long long>(real),
                static_cast<long long>(off), 1e3 * off / kRate, beats);
    DAWLAB_CHECK(real == 18240 && off == 17040);
    DAWLAB_CHECK_NEAR(beats, 0.71, 1e-9);
    // Started at 0: the first 1200 frames were played before the song began.
    const Placement atZero = place(0, driver, 48000);
    DAWLAB_CHECK(atZero.timelineStart == 0 && atZero.skipFrames == 1200 && atZero.numFrames == 46800);

    // (b) 32-bit float, stereo, 48 kHz.
    const double bytesPerSecond = kRate * 2 * 4;
    const double maxSeconds = static_cast<double>(kMaxDataBytes) / bytesPerSecond;
    std::printf("(b) %.0f bytes a second, %.2f MB a minute, %.4f GB an hour; a WAV take ends after %.0f s = %.2f h\n", bytesPerSecond,
                bytesPerSecond * 60 / 1e6, bytesPerSecond * 3600 / 1e9, maxSeconds, maxSeconds / 3600);
    DAWLAB_CHECK(bytesPerSecond == 384000.0);
    DAWLAB_CHECK_NEAR(bytesPerSecond * 60 / 1e6, 23.04, 1e-9);
    DAWLAB_CHECK_NEAR(bytesPerSecond * 3600 / 1e9, 1.3824, 1e-9);
    DAWLAB_CHECK(kMaxDataBytes == 4294967195ULL);
    DAWLAB_CHECK_NEAR(maxSeconds, 11184.81, 0.01);

    // (c) A disk that stalls for 250 ms, blocks of 512 frames that go in whole.
    const std::int64_t stall = static_cast<std::int64_t>(0.250 * kRate);
    const std::int64_t ring = stall + 512;
    std::printf("(c) a 250 ms stall: %lld frames + one block = %lld frames = %lld bytes stereo\n", static_cast<long long>(stall),
                static_cast<long long>(ring), static_cast<long long>(ring * 2 * 4));
    DAWLAB_CHECK(stall == 12000 && ring == 12512 && ring * 8 == 100096);
    std::printf("    DawLab 17's ring of 10 s: %.2f MB stereo\n", 10 * bytesPerSecond / 1e6);
    DAWLAB_CHECK_NEAR(10 * bytesPerSecond / 1e6, 3.84, 1e-9);

    // (d) What the musician hears of themselves (Step 5): input + output, and through the inserts + the mix.
    const double direct = 1e3 * (driver.input + driver.output) / kRate;
    const double inserts = 1e3 * (driver.input + driver.output + driver.mix) / kRate;
    std::printf("(d) monitoring: directly %.1f ms, through the inserts %.1f ms; at 64-sample buffers %.2f ms\n", direct, inserts,
                1e3 * 128 / kRate);
    DAWLAB_CHECK_NEAR(direct, 20.0, 1e-9);
    DAWLAB_CHECK_NEAR(inserts, 25.0, 1e-9);

    // (e) Exercise 3: a punch at 10 s, recorded from 0, a preroll of 1 s, mono.
    RecordRange punch;
    punch.punchIn = 480000;
    punch.punchOut = 528000;
    const std::int64_t first = takesOf(0, driver, punch, 600000).front().skipFrames;
    const std::int64_t saved = first - 48000;
    std::printf("(e) the take starts at frame %lld of the stream; %lld frames = %.2f MB of mono are not written\n",
                static_cast<long long>(first), static_cast<long long>(saved), saved * 4 / 1e6);
    DAWLAB_CHECK(first == 481200 && saved == 433200);
    DAWLAB_CHECK_NEAR(saved * 4 / 1e6, 1.7328, 1e-9);
    return dawlab::test::summary("l17_ex2");
}
