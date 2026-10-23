// Exercise 3 solution: compute the alias frequencies, then verify them by measuring a real hard clip.
//
// The question: fs = 44.1 kHz, a 5 kHz sine goes through a very heavy hard clip (close to a square wave,
//   only odd harmonics, the k-th with amplitude ~ (4/pi)/k).
//   (a) Where do the 3rd, 5th, 7th, 9th, 11th, 13th and 15th harmonics land? Which ones alias, and to where?
//   (b) How many dB below the fundamental is the alias of the 9th harmonic (45 kHz)?
//   (c) With 2x oversampling (hard clip at 88.2 kHz), which harmonics still fold back into 0..22.05 kHz?
//
// Alias formula: f_a = | ((f + fs/2) mod fs) - fs/2 |   (fold f into 0..fs/2)
//   15 kHz -> 15 kHz (a real harmonic)
//   25 kHz -> 44.1 - 25 = 19.1 kHz        35 kHz -> 44.1 - 35 = 9.1 kHz
//   45 kHz -> 45 - 44.1 = 0.9 kHz          55 kHz -> 55 - 44.1 = 10.9 kHz
//   65 kHz -> 65 - 44.1 = 20.9 kHz         75 kHz -> 88.2 - 75 = 13.2 kHz
// (b) The 9th harmonic of a square wave has 1/9 of the fundamental's amplitude -> 20 log10(1/9) = -19.08 dB.
//     This 900 Hz tone is more than two octaves below the fundamental and has no integer relationship to
//     5 kHz at all - it is very harsh.
// (c) At 88.2 kHz: 25 and 35 kHz are "real" harmonics (< 44.1k) and are blocked by the lowpass during
//     downsampling; 45 -> 43.2, 55 -> 33.2, 65 -> 23.2, 75 -> 13.2 kHz, ... only the ones that land below
//     22.05 kHz after folding survive untouched (75 kHz -> 13.2 kHz is still an alias).
//     Those between 22.05 kHz and the stopband start (about 25.7 kHz) fall in the halfband's transition
//     band and are only partly attenuated; when downsampling they then fold around 22.05 kHz to fs - f:
//     65 kHz -> 23.2 kHz (attenuated by only about 15 dB) -> 20.9 kHz;
//     25 kHz also sits at the tail of the transition band but is already attenuated by about 45 dB
//     (-> 19.1 kHz), so in practice it can be considered blocked.
//     The program computes these two attenuations from the coefficients of stage 1 of this chapter's Oversampler.
//
// Run: l06_ex3_solution

#include <dawlab/Common.h>
#include <dawlab/dsp/Oversampler.h>

#include <algorithm>
#include <cstdio>

namespace {

double aliasFrequency(double f, double fs) {
    const double shifted = std::fmod(f + fs * 0.5, fs);
    return std::abs(shifted - fs * 0.5);
}

// Magnitude response (dB) of FIR h at frequency f (sample rate fsHigh): |sum h[n] e^{-jwn}|
double firResponseDb(const std::vector<double>& h, double f, double fsHigh) {
    const double w = dawlab::kTwoPi * f / fsHigh;
    double re = 0.0, im = 0.0;
    for (size_t n = 0; n < h.size(); ++n) {
        re += h[n] * std::cos(w * static_cast<double>(n));
        im -= h[n] * std::sin(w * static_cast<double>(n));
    }
    return dawlab::gainToDb(std::sqrt(re * re + im * im));
}

} // namespace

int main() {
    constexpr double fs = 44100.0;
    constexpr double f0 = 5000.0;

    // ---- (a) Table ---------------------------------------------------------------------------
    // The stage-1 halfband's stopband starts at 28 kHz (for fs = 48 kHz), i.e. 28/24 times the original
    // Nyquist; at 44.1 kHz that is about 25.7 kHz. Frequencies that fold to between 22.05 and 25.7 kHz
    // fall in the transition band: they are only partly attenuated and, when downsampling, fold around
    // fs/2 once more to fs - a2.
    const double stopbandEdge = fs * 0.5 * (28.0 / 24.0);
    // The stage-1 halfband (this is the one used when downsampling 88.2k -> 44.1k)
    const std::vector<double> halfband = dawlab::dsp::HalfbandStage::designHalfband(
        dawlab::dsp::Oversampler::kOddTapsStage1, dawlab::dsp::Oversampler::kKaiserBeta);
    std::printf("%-6s %-10s %-14s %-20s\n", "k", "k*f0", "1x (44.1k)", "2x (88.2k) first folds to");
    for (int k = 3; k <= 15; k += 2) {
        const double f = k * f0;
        const double a1 = aliasFrequency(f, fs);
        const double a2 = aliasFrequency(f, 2 * fs);
        char note[96];
        if (a2 < fs / 2)
            std::snprintf(note, sizeof(note), "%s", f < fs / 2 ? " (harmonic)" : " (survives)");
        else if (a2 < stopbandEdge)
            std::snprintf(note, sizeof(note), " (transition band: attenuated %.1f dB, then folds to %.1f kHz)",
                          -firResponseDb(halfband, a2, 2 * fs), (fs - a2) / 1000.0);
        else
            std::snprintf(note, sizeof(note), "%s", " (filtered during downsampling)");
        std::printf("%-6d %-10.0f %-8.1f kHz%s %-8.1f kHz%s\n", k, f, a1 / 1000.0, f < fs / 2 ? " (harmonic)" : " ALIAS ",
                    a2 / 1000.0, note);
    }
    DAWLAB_CHECK_NEAR(aliasFrequency(25000, fs), 19100, 1e-6);
    DAWLAB_CHECK_NEAR(aliasFrequency(35000, fs), 9100, 1e-6);
    DAWLAB_CHECK_NEAR(aliasFrequency(45000, fs), 900, 1e-6);
    DAWLAB_CHECK_NEAR(aliasFrequency(55000, fs), 10900, 1e-6);
    DAWLAB_CHECK_NEAR(aliasFrequency(65000, fs), 20900, 1e-6);
    DAWLAB_CHECK_NEAR(aliasFrequency(75000, fs), 13200, 1e-6);
    DAWLAB_CHECK_NEAR(aliasFrequency(75000, 2 * fs), 13200, 1e-6);
    DAWLAB_CHECK_NEAR(aliasFrequency(65000, 2 * fs), 23200, 1e-6);
    DAWLAB_CHECK(aliasFrequency(65000, 2 * fs) > fs / 2 && aliasFrequency(65000, 2 * fs) < stopbandEdge); // transition band
    DAWLAB_CHECK_NEAR(fs - aliasFrequency(65000, 2 * fs), 20900, 1e-6);                                // folds to 20.9 kHz after downsampling
    // 23.2 kHz is attenuated by only 10-20 dB (the surviving alias is still clearly audible); 25 kHz is
    // already attenuated by more than 40 dB
    const double att232 = -firResponseDb(halfband, 23200.0, 2 * fs);
    const double att25 = -firResponseDb(halfband, 25000.0, 2 * fs);
    DAWLAB_CHECK(att232 > 10.0 && att232 < 20.0);
    DAWLAB_CHECK(att25 > 40.0);

    // ---- (b) Measurement: sine x 100, then hard clip to +/-1 ~ square wave ------------------
    dawlab::AudioBuffer b = dawlab::makeSine(f0, fs, 1.0, 1.0f, 1);
    float* d = b.getWritePointer(0);
    const int n = b.getNumSamples();
    for (int i = 0; i < n; ++i)
        d[i] = std::clamp(100.0f * d[i], -1.0f, 1.0f);

    const double h1 = dawlab::goertzelAmplitude(d, n, f0, fs);
    const double a900 = dawlab::goertzelAmplitude(d, n, 900.0, fs);
    const double a9100 = dawlab::goertzelAmplitude(d, n, 9100.0, fs);
    std::printf("\nMeasurement: fundamental amplitude %.4f (theory 4/pi = %.4f)\n", h1, 4.0 / dawlab::kPi);
    std::printf("      900 Hz alias = %.2f dB (theory, 9th harmonic: %.2f dB)\n", dawlab::gainToDb(a900 / h1),
                dawlab::gainToDb(1.0 / 9.0));
    std::printf("      9.1 kHz alias = %.2f dB (theory, 7th harmonic: %.2f dB)\n", dawlab::gainToDb(a9100 / h1),
                dawlab::gainToDb(1.0 / 7.0));
    DAWLAB_CHECK_NEAR(h1, 4.0 / dawlab::kPi, 0.02);
    // Other, higher harmonics (e.g. the 873rd and 891st) also fold to 900 Hz, but their amplitude is only
    // about 1% of 1/9 -> error < 0.5 dB
    DAWLAB_CHECK_NEAR(dawlab::gainToDb(a900 / h1), dawlab::gainToDb(1.0 / 9.0), 0.5);
    DAWLAB_CHECK_NEAR(dawlab::gainToDb(a9100 / h1), dawlab::gainToDb(1.0 / 7.0), 0.5);
    // Control: 800 Hz is not the alias position of any low harmonic (gcd(5000, 44100) = 100, but reaching
    // 800 Hz needs k*5000 = +/-800 (mod 44100) with a very large k, amplitude < 1/400), so it should be quiet
    const double a800 = dawlab::goertzelAmplitude(d, n, 800.0, fs);
    std::printf("      Control 800 Hz = %.2f dB (only very high harmonics fold here)\n", dawlab::gainToDb(a800 / h1));
    DAWLAB_CHECK(dawlab::gainToDb(a800 / h1) < -40.0);

    return dawlab::test::summary("l06_ex3");
}
