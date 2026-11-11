#pragma once
// Exercise 4 - the self-check (main). Included at the end of the starter and of the solution.

#include "ex4_fine_fit.h"

#include <dawlab/Check.h>

#include <cstdio>
#include <random>

namespace ex4::check {

inline void checkRefine() {
    // Three samples of a parabola with its top at +0.3: the top, exactly.
    const auto parabola = [](double d) { return 5.0 - 2.0 * (d - 0.3) * (d - 0.3); };
    DAWLAB_CHECK(std::fabs(refine(parabola(-1.0), parabola(0.0), parabola(1.0)) - 0.3) < 1e-12);
    DAWLAB_CHECK(std::fabs(refine(parabola(-1.0) + 7.0, parabola(0.0) + 7.0, parabola(1.0) + 7.0) - 0.3) < 1e-12); // a constant does not move it
    DAWLAB_CHECK(refine(1.0, 2.0, 1.0) == 0.0);                                                            // symmetric
    DAWLAB_CHECK(std::fabs(refine(0.0, 1.0, 0.5) - (0.5 * (0.0 - 0.5) / (0.0 - 2.0 + 0.5))) < 1e-12);    // the formula
    DAWLAB_CHECK(refine(1.0, 1.0, 1.0) == 0.0 && refine(1.0, 0.0, 1.0) == 0.0 && refine(0.0, 1.0, 2.0) == 0.0); // flat, a valley, a slope
    DAWLAB_CHECK(refine(std::nan(""), 1.0, 0.0) == 0.0);
    // With the middle the best of the three the top is never more than half a sample away. If it is not the best
    // (it should be), the top can be further: clamped to half a sample - beyond that it belongs to the neighbour.
    DAWLAB_CHECK(refine(3.0, 2.0, 0.0) == -0.5 && refine(0.0, 2.0, 3.0) == 0.5);
    DAWLAB_CHECK(std::fabs(refine(2.0, 2.000001, 0.0)) <= 0.5);
}

inline void checkReading() {
    std::mt19937 rng(40);
    std::vector<float> noise(1000);
    for (float& v : noise)
        v = std::uniform_real_distribution<float>(-1.0f, 1.0f)(rng);
    int off = 0;
    for (int i = 2; i < 997; ++i)
        off += readHermite(noise, i) == static_cast<double>(noise[static_cast<size_t>(i)]) ? 0 : 1; // on a sample: the sample
    DAWLAB_CHECK(off == 0);
    DAWLAB_CHECK(std::fabs(readHermite({0.0f, 1.0f, 4.0f, 9.0f, 16.0f}, 2.25) - 5.0625) < 1e-9); // a parabola, exactly
    DAWLAB_CHECK(readHermite(noise, -10.5) == 0.0 && readHermite(noise, 2000.0) == 0.0);
    DAWLAB_CHECK(std::fabs(readHermite({0.5f, 0.5f, 0.5f, 0.5f}, 1.7) - 0.5) < 1e-9);
}

inline void checkUnchanged() {
    std::mt19937 rng(41);
    std::vector<float> noise(30000);
    for (float& v : noise)
        v = std::uniform_real_distribution<float>(-1.0f, 1.0f)(rng);
    std::vector<double> positions(20000);
    for (size_t n = 0; n < positions.size(); ++n)
        positions[n] = 3000.0 + static_cast<double>(n);
    const std::vector<float> out = stretchFine(noise, positions, {});
    float worst = out.size() == positions.size() ? 0.0f : 1.0f;
    for (size_t n = 0; n < out.size() && n < positions.size(); ++n)
        worst = std::max(worst, std::fabs(out[n] - noise[n + 3000]));
    std::printf("  noise at speed 1: at most %.1g from the input\n", static_cast<double>(worst));
    DAWLAB_CHECK(worst < 1e-5f);
    DAWLAB_CHECK((stretchFine(noise, positions, {1023, 256}).empty() && stretchFine(noise, positions, {2, 0}).empty() &&
                  stretchFine(noise, positions, {1024, -1}).empty() && stretchFine(noise, {}, {}).empty()));
}

inline void checkTones() {
    // Two tones whose period is not a whole number of samples, at four speeds: the fit to a fraction and Step 4's.
    bool fine = true;
    double worstGain = 1e9;
    for (const double hz : {220.0, 440.0}) {
        const std::vector<float> tone = sine(hz, 4 * 48000, 0.5);
        for (const double speed : {0.5, 0.8, 1.25, 2.0}) {
            const int length = static_cast<int>(2.0 * 48000 / speed);
            const std::vector<float> whole = stretch(tone, steady(speed, length)), part = stretchFine(tone, steady(speed, length), {});
            if (static_cast<int>(part.size()) != length) {
                fine = false;
                continue;
            }
            const Tone a = measureTone(whole, 2048, whole.size() - 2048, hz), b = measureTone(part, 2048, part.size() - 2048, hz);
            worstGain = std::min(worstGain, b.noiseDb - a.noiseDb);
            fine = fine && b.noiseDb > 85.0 && std::fabs(b.cents) < 0.05;
            if (speed == 0.5 || speed == 2.0)
                std::printf("  %3.0f Hz at speed %.2f: what is not the tone %5.1f dB below it (whole samples: %4.1f dB), %+.3f cents\n", hz, speed, b.noiseDb, a.noiseDb,
                            b.cents);
        }
    }
    std::printf("  the fit to a fraction is at least %.0f dB better on these 8 tones\n", worstGain);
    DAWLAB_CHECK(fine && worstGain > 40.0);

    // A tone whose period IS a whole number of samples (1 kHz: 48): whole samples fit it exactly, and the fraction
    // costs what reading between samples costs.
    const std::vector<float> even = sine(1000.0, 4 * 48000, 0.5);
    const std::vector<float> whole = stretch(even, steady(0.8, 120000)), part = stretchFine(even, steady(0.8, 120000), {});
    const Tone a = measureTone(whole, 2048, 118000, 1000.0), b = measureTone(part, 2048, 118000, 1000.0);
    std::printf("  1 kHz at speed 0.80 (a period of 48 samples): whole samples %.0f dB, a fraction %.0f dB\n", a.noiseDb, b.noiseDb);
    DAWLAB_CHECK(a.noiseDb > 120.0 && b.noiseDb > 75.0);
}

inline void checkFollows() {
    // The speed changes in the middle, and the pieces still land where the positions say.
    const std::vector<float> tone = sine(440.0, 4 * 48000, 0.5);
    std::vector<double> positions(96000);
    for (size_t n = 0; n < positions.size(); ++n)
        positions[n] = n < 48000 ? 0.75 * static_cast<double>(n) : 36000.0 + 1.5 * static_cast<double>(n - 48000);
    const std::vector<float> out = stretchFine(tone, positions, {});
    DAWLAB_CHECK(out.size() == 96000);
    if (out.size() != 96000)
        return;
    const Tone slow = measureTone(out, 2048, 46000, 440.0), fast = measureTone(out, 50000, 94000, 440.0);
    DAWLAB_CHECK(slow.noiseDb > 85.0 && fast.noiseDb > 85.0 && std::fabs(slow.cents) < 0.05 && std::fabs(fast.cents) < 0.05);
    // A click every half second: wherever it is heard, it is within half a window and the search of its place.
    std::vector<float> clicks(5 * 48000, 0.0f);
    for (int k = 0; k < 8; ++k)
        clicks[static_cast<size_t>(12000 + k * 24000)] = 1.0f;
    const std::vector<float> c = stretchFine(clicks, steady(0.5, 8 * 48000), {});
    int worst = 0, heard = 0;
    for (int k = 0; k < 8 && c.size() == 8u * 48000u; ++k) {
        const int want = 2 * (12000 + k * 24000);
        int loudest = -1;
        float peak = 0.3f;
        for (int s = std::max(want - 2048, 0); s < std::min(want + 2048, static_cast<int>(c.size())); ++s)
            if (std::fabs(c[static_cast<size_t>(s)]) > peak) {
                peak = std::fabs(c[static_cast<size_t>(s)]);
                loudest = s;
            }
        if (loudest >= 0) {
            ++heard;
            worst = std::max(worst, std::abs(loudest - want));
        }
    }
    DAWLAB_CHECK(heard == 8 && worst <= 512 + 256 + 1);
}

} // namespace ex4::check

int main() {
    using namespace ex4::check;
    std::printf("Exercise 4 - a fit to a fraction of a sample\n");
    checkRefine();
    checkReading();
    checkUnchanged();
    checkTones();
    checkFollows();
    return dawlab::test::summary("l20_ex4");
}
