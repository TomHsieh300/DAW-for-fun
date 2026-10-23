// Exercise 2 solution: the hand-calculation questions on static curves and time constants, checked one by one in code.
//
// This program does no signal processing. It simply takes the hand-calculated answers in solutions/README.md and
// checks them against Compressor::computeOutputDb() and attackReleaseCoefficient(), making sure the numbers in the
// book are correct.
// Run: l04_ex2_solution (no arguments); it writes no WAV files.
#include <dawlab/Common.h>
#include <dawlab/dsp/Compressor.h>
#include <dawlab/dsp/EnvelopeFollower.h>

#include <cmath>
#include <cstdio>

using namespace dawlab;
using dawlab::dsp::attackReleaseCoefficient;
using dawlab::dsp::coefficientToTimeMs;
using dawlab::dsp::Compressor;

int main() {
    std::printf("Exercise 2: static curves and time constants\n");

    // ---- 1. T = -24, R = 3, hard knee ----
    std::printf("\n(1) T = -24 dB, R = 3:1, hard knee\n");
    struct Row {
        float in, out;
    };
    const Row hard[] = {{-30.0f, -30.0f}, {-24.0f, -24.0f}, {-12.0f, -20.0f}, {0.0f, -16.0f}};
    for (const Row& r : hard) {
        const float y = Compressor::computeOutputDb(r.in, -24.0f, 3.0f, 0.0f);
        std::printf("  Input %6.1f dB → output %7.3f dB, GR %6.3f dB (by hand: %.3f / %.3f)\n", r.in, y, r.in - y,
                    r.out, r.in - r.out);
        DAWLAB_CHECK_NEAR(y, r.out, 1e-4);
    }

    // ---- 2. Same as above, soft knee W = 12 ----
    // Knee range T ± W/2 = -30 .. -18 dB
    // x = -24: y = -24 + (1/3 - 1)·6² / 24 = -24 - 1 = -25
    // x = -20: y = -20 + (1/3 - 1)·10² / 24 = -20 - 2.7778 = -22.7778
    std::printf("\n(2) soft knee W = 12 dB (knee range -30 .. -18 dB)\n");
    const Row soft[] = {{-30.0f, -30.0f}, {-24.0f, -25.0f}, {-20.0f, -22.777778f}};
    for (const Row& r : soft) {
        const float y = Compressor::computeOutputDb(r.in, -24.0f, 3.0f, 12.0f);
        std::printf("  Input %6.1f dB → output %8.4f dB (by hand: %.4f)\n", r.in, y, r.out);
        DAWLAB_CHECK_NEAR(y, r.out, 1e-4);
    }

    // ---- 3. Upper edge of the knee, -18 dB: the quadratic = the straight line = -22 ----
    const float upperSoft = -18.0f + (1.0f / 3.0f - 1.0f) * 12.0f * 12.0f / 24.0f; // use the quadratic formula directly
    const float upperHard = Compressor::computeOutputDb(-18.0f, -24.0f, 3.0f, 0.0f);
    std::printf("\n(3) Upper edge of the knee, -18 dB: quadratic %.4f dB, hard-knee line %.4f dB\n", upperSoft,
                upperHard);
    DAWLAB_CHECK_NEAR(upperSoft, -22.0, 1e-4);
    DAWLAB_CHECK_NEAR(upperHard, -22.0, 1e-4);
    // The slopes must join as well: take a numerical derivative 0.001 dB to either side of the upper edge; both should be 1/R
    const float h = 0.001f;
    const float slopeBelow = (Compressor::computeOutputDb(-18.0f, -24.0f, 3.0f, 12.0f) -
                              Compressor::computeOutputDb(-18.0f - h, -24.0f, 3.0f, 12.0f)) / h;
    std::printf("    Slope just inside the knee ≈ %.3f (should be 1/R = 0.333)\n", slopeBelow);
    DAWLAB_CHECK_NEAR(slopeBelow, 1.0 / 3.0, 0.01);

    // ---- 4. fs = 44.1 kHz, attack 5 ms, release 200 ms ----
    const double fs = 44100.0;
    const float aA = attackReleaseCoefficient(5.0, fs);
    const float aR = attackReleaseCoefficient(200.0, fs);
    std::printf("\n(4) fs = 44100: αA = exp(-1/220.5) = %.6f, αR = exp(-1/8820) = %.6f\n", aA, aR);
    DAWLAB_CHECK_NEAR(aA, 0.995475, 2e-6);
    DAWLAB_CHECK_NEAR(aR, 0.999887, 2e-6);
    std::printf("    Inverted: τA = %.3f ms, τR = %.2f ms\n", coefficientToTimeMs(aA, fs), coefficientToTimeMs(aR, fs));

    // ---- 5. Time to 90% / 99%: simulate the step response numerically and compare with τ·ln10 and τ·ln100 ----
    auto stepTimeMs = [&](float alpha, double fraction) {
        double y = 0.0;
        for (int n = 1; n < 1000000; ++n) {
            y = alpha * y + (1.0 - alpha) * 1.0;
            if (y >= fraction)
                return n * 1000.0 / fs;
        }
        return -1.0;
    };
    const double t90 = stepTimeMs(aA, 0.90);
    const double t99 = stepTimeMs(aA, 0.99);
    const double r10 = stepTimeMs(aR, 0.90); // Release decaying to 10% = "already 90% of the way", the same formula as attack
    std::printf("\n(5) Attack to 90%%: %.2f ms (τ·ln10 = 11.51); to 99%%: %.2f ms (τ·ln100 = 23.03)\n", t90, t99);
    std::printf("    Release decaying to 10%%: %.1f ms (τ·ln10 = 460.5)\n", r10);
    DAWLAB_CHECK_NEAR(t90, 5.0 * std::log(10.0), 0.05);
    DAWLAB_CHECK_NEAR(t99, 5.0 * std::log(100.0), 0.05);
    DAWLAB_CHECK_NEAR(r10, 200.0 * std::log(10.0), 0.1);

    // ---- 6. A panel's "10 ms to 90%" converted to the 63.2% definition: τ = 10 / ln10 = 4.343 ms ----
    const double tau = 10.0 / std::log(10.0);
    const double check90 = stepTimeMs(attackReleaseCoefficient(tau, fs), 0.90);
    std::printf("\n(6) setAttackMs(%.3f) → actual time to 90%%: %.2f ms (target 10)\n", tau, check90);
    DAWLAB_CHECK_NEAR(tau, 4.343, 0.001);
    DAWLAB_CHECK_NEAR(check90, 10.0, 0.05);

    return dawlab::test::summary("l04_ex2");
}
