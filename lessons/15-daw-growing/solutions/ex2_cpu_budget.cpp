// Exercise 2 (solution): the CPU budget by hand, checked with the engine's CpuMeter and CpuMonitor.
//
// The machine of the exercise, at 48 kHz:
//   o = 50 us per block      the callback itself, whatever its length
//   f = 0.40 us per sample   master and buses
//   c = 0.15 us per sample   each track with its inserts
//   time of one block of N samples with n tracks:  t = o + N (f + n c)
//
// (a) the budget of 64, 256 and 1024 samples
// (b) the load of 20 tracks at those three sizes
// (c) the most tracks that stay below the High mark (75 %) at each size
// (d) one block in 200 takes 2.2 times as long: what 100 tracks at 256 do, and the most tracks
//     that never overrun
// (e) what the meter shows after one block at 130 % in a steady 40 %
//
// The derivations are in solutions/README.md; this program only proves the numbers.

#include <dawlab/Check.h>
#include <dawlab/engine/CpuMeter.h>

#include <cmath>
#include <cstdio>
#include <initializer_list>

using namespace dawlab::engine;

namespace {

constexpr double kRate = 48000.0;
constexpr double kOverhead = 50e-6, kFixed = 0.40e-6, kPerTrack = 0.15e-6;

double budget(int n) { return n / kRate; }
double blockTime(int n, int tracks) { return kOverhead + n * (kFixed + tracks * kPerTrack); }
double load(int n, int tracks) { return blockTime(n, tracks) / budget(n); }
// The most tracks whose load is at most `limit`.
int mostTracks(int n, double limit) { return static_cast<int>(std::floor(((limit * budget(n) - kOverhead) / n - kFixed) / kPerTrack)); }

} // namespace

int main() {
    std::printf("Exercise 2 - the CPU budget\n");
    const int sizes[] = {64, 256, 1024};

    // (a)
    std::printf("(a) budget: %.4f ms, %.4f ms, %.4f ms\n", 1e3 * budget(64), 1e3 * budget(256), 1e3 * budget(1024));
    DAWLAB_CHECK_NEAR(1e3 * budget(64), 1.3333, 1e-4);
    DAWLAB_CHECK_NEAR(1e3 * budget(256), 5.3333, 1e-4);
    DAWLAB_CHECK_NEAR(1e3 * budget(1024), 21.3333, 1e-4);

    // (b)
    std::printf("(b) 20 tracks: %.2f %%, %.2f %%, %.2f %%\n", 100 * load(64, 20), 100 * load(256, 20), 100 * load(1024, 20));
    DAWLAB_CHECK_NEAR(1e6 * blockTime(256, 20), 920.4, 1e-6);
    DAWLAB_CHECK_NEAR(load(64, 20), 0.20070, 1e-5);
    DAWLAB_CHECK_NEAR(load(256, 20), 0.17258, 1e-5);
    DAWLAB_CHECK_NEAR(load(1024, 20), 0.16554, 1e-5);
    // The meter agrees: 3 s of blocks at that cost.
    for (const int n : sizes) {
        CpuMeter meter;
        meter.prepare(kRate);
        for (int i = 0; i < static_cast<int>(3.0 * kRate) / n; ++i)
            meter.addBlock(n, blockTime(n, 20));
        DAWLAB_CHECK_NEAR(meter.load(), load(n, 20), 1e-4);
        DAWLAB_CHECK(meter.overruns() == 0);
    }

    // (c)
    std::printf("(c) below 75 %%: %d, %d, %d tracks\n", mostTracks(64, 0.75), mostTracks(256, 0.75), mostTracks(1024, 0.75));
    DAWLAB_CHECK(mostTracks(64, 0.75) == 96);
    DAWLAB_CHECK(mostTracks(256, 0.75) == 100);
    DAWLAB_CHECK(mostTracks(1024, 0.75) == 101);
    for (const int n : sizes) {
        DAWLAB_CHECK(load(n, mostTracks(n, 0.75)) <= 0.75);
        DAWLAB_CHECK(load(n, mostTracks(n, 0.75) + 1) > 0.75);
    }

    // (d) 100 tracks at 256, one block in 200 at 2.2 times the time.
    const double spike = 2.2 * load(256, 100);
    const double perMinute = 60.0 * kRate / 256 / 200;
    std::printf("(d) 100 tracks at 256: %.1f %%, a slow block %.1f %%, %.2f overruns a minute; never over: %d tracks\n",
                100 * load(256, 100), 100 * spike, perMinute, mostTracks(256, 1.0 / 2.2));
    DAWLAB_CHECK_NEAR(1e6 * blockTime(256, 100), 3992.4, 1e-6);
    DAWLAB_CHECK_NEAR(load(256, 100), 0.748575, 1e-6);
    DAWLAB_CHECK(spike > 1.0);
    DAWLAB_CHECK_NEAR(perMinute, 56.25, 1e-9);
    DAWLAB_CHECK(mostTracks(256, 1.0 / 2.2) == 59);
    {
        CpuMeter meter;
        meter.prepare(kRate);
        CpuMonitor monitor(meter);
        const int blocks = static_cast<int>(60.0 * kRate) / 256;
        int polls = 0, overload = 0, high = 0;
        double nextPoll = 0.1;
        for (int i = 1; i <= blocks; ++i) {
            meter.addBlock(256, (i % 200 == 0 ? 2.2 : 1.0) * blockTime(256, 100));
            const double now = i * budget(256);
            if (now >= nextPoll) {
                nextPoll += 0.1;
                const CpuStatus s = monitor.poll(now);
                if (now > 5.0) { // after the first slow block
                    ++polls;
                    overload += s.state == CpuState::Overload ? 1 : 0;
                    high += s.state == CpuState::High ? 1 : 0;
                }
            }
        }
        std::printf("    the monitor, 5 s to 60 s: Overload in %d of %d polls (a slow block every %.2f s, held 2 s)\n", overload,
                    polls, 200 * budget(256));
        DAWLAB_CHECK(meter.overruns() == static_cast<std::uint32_t>(blocks / 200));
        DAWLAB_CHECK(overload == polls); // the warning never goes away, although the average is below 75 %
        // 59 tracks: the same slow blocks, no overrun.
        CpuMeter safe;
        safe.prepare(kRate);
        for (int i = 1; i <= blocks; ++i)
            safe.addBlock(256, (i % 200 == 0 ? 2.2 : 1.0) * blockTime(256, 59));
        DAWLAB_CHECK(safe.overruns() == 0);
    }

    // (e) a steady 40 %, then one block at 130 %.
    for (const int n : {sizes[0], sizes[1]}) {
        CpuMeter meter;
        meter.prepare(kRate);
        for (int i = 0; i < static_cast<int>(10.0 * kRate) / n; ++i)
            meter.addBlock(n, 0.4 * budget(n));
        meter.addBlock(n, 1.3 * budget(n));
        const double a = std::exp(-budget(n) / CpuMeter::kTimeConstantSeconds);
        std::printf("(e) at %d: the meter shows %.2f %% (a = %.6f), and counts %u overrun\n", n, 100.0 * meter.load(), a, meter.overruns());
        DAWLAB_CHECK_NEAR(meter.load(), 1.3 + a * (0.4 - 1.3), 1e-4);
        DAWLAB_CHECK(meter.overruns() == 1);
    }
    return dawlab::test::summary("l15_ex2");
}
