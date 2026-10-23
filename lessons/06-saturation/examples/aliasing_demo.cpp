// Example 2: Aliasing demo - a high-frequency sine plus a large drive, with and without oversampling.
//
// What it shows:
//   A 7 kHz sine goes through tanh (drive +18 dB, so the peak entering the curve is about 4). tanh is an
//   odd function and produces only odd harmonics: 21, 35, 49, 63, 77 kHz, ... Those above Nyquist
//   (24 kHz) fold back (alias) at 48 kHz:
//       35 -> |35 - 48| = 13 kHz, 49 -> 1 kHz, 63 -> 15 kHz, 77 -> 19 kHz, 91 -> 5 kHz, ...
//   The folded frequencies are NOT integer multiples of 7 kHz, so they sound like inharmonic noise.
//   With oversampling on, the nonlinearity runs at 2fs / 4fs / 8fs, so Nyquist becomes 48 / 96 / 192 kHz
//   and most of the high harmonics are removed by the halfband lowpass during downsampling instead of
//   folding back.
//
//   This program:
//   1. Prints a "prediction table": where each harmonic lands at 1x / 2x / 4x / 8x and whether it
//      survives in the output.
//   2. Measures the fundamental (7 kHz), the harmonic at 21 kHz, and every alias position
//      (1, 3, 5, 9, 11, 13, 15, 17, 19, 23 kHz) in dB relative to the fundamental; finally it computes a
//      total alias energy (sum of power at all alias positions / fundamental power).
//   3. Writes four WAVs for listening.
//
// How to run: l06_aliasing_demo (no arguments)
//
// Output files (in dawlab_output/):
//   l06_alias_7k_1x.wav / l06_alias_7k_2x.wav / l06_alias_7k_4x.wav / l06_alias_7k_8x.wav
//
// What you should hear:
//   Besides the 7 kHz tone, the 1x version has clearly audible, much lower tones (1 kHz, 5 kHz, ...), as
//   if another note were mixed in. Most of them are gone at 2x, and 4x / 8x leave an almost clean 7 kHz
//   (plus 21 kHz, which most people cannot hear).
//   The program returns 1 if the total alias energy does not fall as the factor increases.

#include <dawlab/Common.h>
#include <dawlab/dsp/Saturation.h>

#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr double kFundamental = 7000.0;
constexpr float kDriveDb = 18.0f;

// The frequency at which a tone f is recorded at sample rate `rate` (the folding formula):
//   f_alias = | ((f + rate/2) mod rate) - rate/2 |
double aliasFrequency(double f, double rate) {
    const double shifted = std::fmod(f + rate * 0.5, rate);
    return std::abs(shifted - rate * 0.5);
}

template <typename Processor>
void processInBlocks(dawlab::AudioBuffer& buffer, Processor& processor) {
    dawlab::AudioBlock whole(buffer);
    for (int start = 0; start < buffer.getNumSamples(); start += kBlockSize) {
        const int len = std::min(kBlockSize, buffer.getNumSamples() - start);
        processor.process(whole.getSubBlock(start, len));
    }
}

} // namespace

int main() {
    using dawlab::dsp::Saturation;
    using dawlab::dsp::SaturationCurve;

    // ---- 1. Prediction table -----------------------------------------------------------------
    std::printf("%.0f Hz sine -> tanh (drive %+.0f dB). Where the odd harmonics end up at each factor (fs = %.0f kHz):\n", kFundamental,
                kDriveDb, kSampleRate / 1000.0);
    std::printf("%-6s %-10s | %-14s | %-22s | %-22s | %-22s\n", "Harm.", "Freq (kHz)", "1x -> lands at", "2x (Nyq 48k)",
                "4x (Nyq 96k)", "8x (Nyq 192k)");
    for (int k = 3; k <= 29; k += 2) {
        const double f = kFundamental * k;
        std::printf("%-6d %-10.0f |", k, f / 1000.0);
        for (int log2 = 0; log2 <= 3; ++log2) {
            const double rate = kSampleRate * (1 << log2);
            const double fa = aliasFrequency(f, rate);
            // It stays in the output if the folded frequency is below the original Nyquist (24 kHz); otherwise the
            // downsampling lowpass blocks it.
            const bool survives = fa < kSampleRate * 0.5;
            const bool isHarmonic = std::abs(fa - f) < 1.0; // Did not fold at all (already below Nyquist)
            char text[64];
            if (isHarmonic)
                std::snprintf(text, sizeof(text), "%.0f kHz (harmonic)", fa / 1000.0);
            else if (survives)
                std::snprintf(text, sizeof(text), "%.0f kHz ALIAS", fa / 1000.0);
            else
                std::snprintf(text, sizeof(text), "%.0f kHz -> filtered", fa / 1000.0);
            std::printf(" %-20s |", text);
        }
        std::printf("\n");
    }
    std::printf("(\"filtered\" = still above 24 kHz after folding, so the halfband lowpass blocks it during downsampling; ALIAS = stays in the audible range)\n\n");

    // ---- 2. Measurement ----------------------------------------------------------------------
    // All aliases land on odd kHz values (because gcd(7000, 48000) = 1000 and only odd harmonics exist);
    // 7 and 21 (the real harmonics) are excluded.
    const std::vector<double> aliasHz = {1000.0, 3000.0, 5000.0, 9000.0, 11000.0, 13000.0, 15000.0, 17000.0, 19000.0,
                                         23000.0};
    std::vector<double> totalAliasDb;
    std::printf("Measurement (dB relative to the 7 kHz fundamental; input amplitude 0.5, drive %+.0f dB -> peak entering tanh ~ %.2f):\n", kDriveDb,
                0.5 * dawlab::dbToGain(kDriveDb));
    std::printf("%-12s %10s %10s %10s %10s\n", "Frequency", "1x", "2x", "4x", "8x");

    std::vector<std::vector<double>> table(aliasHz.size() + 2, std::vector<double>(4, 0.0));
    for (int log2 = 0; log2 <= 3; ++log2) {
        dawlab::AudioBuffer buffer = dawlab::makeSine(kFundamental, kSampleRate, 2.0, 0.5f, 1);
        Saturation sat;
        sat.setOversamplingFactorLog2(log2);
        sat.prepare(kSampleRate, kBlockSize, 1);
        sat.setCurve(SaturationCurve::Tanh);
        sat.setDriveDb(kDriveDb);
        sat.setOutputGainDb(-6.0f); // tanh output peak ~ 1; reduce by 6 dB to leave headroom in the WAV
        sat.setMix(1.0f);
        sat.reset(); // Let the parameter ramps settle immediately
        processInBlocks(buffer, sat);

        const int skip = 4800; // Skip the first 0.1 s (latency + transient)
        const float* d = buffer.getReadPointer(0) + skip;
        const int n = buffer.getNumSamples() - skip;
        const double h1 = dawlab::goertzelAmplitude(d, n, kFundamental, kSampleRate);
        table[0][static_cast<size_t>(log2)] = 0.0;
        table[1][static_cast<size_t>(log2)] =
            dawlab::gainToDb(dawlab::goertzelAmplitude(d, n, kFundamental * 3.0, kSampleRate) / h1);
        double aliasPower = 0.0;
        for (size_t i = 0; i < aliasHz.size(); ++i) {
            const double a = dawlab::goertzelAmplitude(d, n, aliasHz[i], kSampleRate);
            table[i + 2][static_cast<size_t>(log2)] = dawlab::gainToDb(a / h1);
            aliasPower += a * a;
        }
        totalAliasDb.push_back(10.0 * std::log10(std::max(aliasPower / (h1 * h1), 1.0e-12)));

        dawlab::writeWav(dawlab::outputPath("l06_alias_7k_" + std::to_string(1 << log2) + "x.wav"), buffer, kSampleRate);
    }
    auto printRow = [&](const char* label, const std::vector<double>& row) {
        std::printf("%-12s", label);
        for (double v : row)
            std::printf(" %10.1f", std::max(v, -99.9));
        std::printf("\n");
    };
    printRow("7 kHz (H1)", table[0]);
    printRow("21 kHz (H3)", table[1]);
    for (size_t i = 0; i < aliasHz.size(); ++i) {
        char label[32];
        std::snprintf(label, sizeof(label), "%2.0f kHz alias", aliasHz[i] / 1000.0);
        printRow(label, table[i + 2]);
    }
    printRow("alias total", totalAliasDb);
    std::printf("(alias total = 10*log10(sum of alias power / fundamental power); the more negative, the cleaner. -96 dB is roughly the 16-bit quantization noise floor.)\n");
    std::printf("\nWrote dawlab_output/l06_alias_7k_{1x,2x,4x,8x}.wav\n");

    const bool ok = totalAliasDb[1] < totalAliasDb[0] - 6.0 && totalAliasDb[2] < totalAliasDb[1] - 6.0 &&
                    totalAliasDb[3] <= totalAliasDb[2] + 1.0;
    std::printf("%s\n", ok ? "Total alias energy falls as the oversampling factor increases." : "[FAIL] Total alias energy did not fall as expected!");
    return ok ? 0 : 1;
}
