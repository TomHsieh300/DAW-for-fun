// l00_db_math: verifies every "numerical claim" in this chapter's README with actual numbers.
//
// What it demonstrates:
//   1. A dB <-> linear gain table (+6 dB ≈ ×2, -20 dB = ×0.1, ...).
//   2. Summing two identical signals = +6.02 dB; summing two uncorrelated signals = +3.01 dB.
//   3. Crest factor: sine 3.01 dB, square 0 dB, uniform white noise 4.77 dB, drum loop higher still.
//   4. Aliasing: a 30 kHz sine sampled at 48 kHz measures as 18 kHz.
//      It also writes a sweep "up to 40 kHz" so you can hear the frequency fold back.
//   5. Quantization: quantize a signal to N bits, measure the SNR, and compare with 6.02N + 1.76 dB.
//   6. Clipping: hard-clip a sine of amplitude 1.5 at ±1.0, count the clipped samples and measure THD.
//
// How to run:
//   cd <build directory>
//   bin/Debug/l00_db_math.exe
//
// Output files:
//   l00_sum_identical.wav      two identical 1 kHz sines summed (-12 dBFS + -12 dBFS = -6 dBFS)
//   l00_sum_uncorrelated.wav   two uncorrelated white-noise signals summed (-12 dBFS + -12 dBFS ≈ -9 dBFS)
//   l00_aliasing_30k.wav       30 kHz sine @ 48 kHz -- sounds / looks like 18 kHz
//   l00_aliasing_sweep.wav     20 Hz -> 40 kHz sweep @ 48 kHz -- the pitch falls back after passing 24 kHz
//   l00_quantized_8bit.wav     a -20 dBFS sine quantized to 8 bits (audible hiss and distortion)
//   l00_quantized_16bit.wav    the same signal quantized to 16 bits
//   l00_clipped_sine.wav       a hard-clipped sine (harsh odd harmonics)
//
// What you should hear:
//   The aliasing_sweep pitch climbs at first, then, once it hits 24 kHz (Nyquist), turns around
//   and falls -- that is aliasing.
//   quantized_8bit has obvious noise under the sine; the 16-bit version sounds the same as a clean sine.
//   clipped_sine sounds "brighter" and harsher than a clean sine because of the extra 3rd, 5th, 7th... harmonics.

#include <dawlab/Common.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

constexpr double kSampleRate = 48000.0;

// Quantizes a double-valued signal to numBits bits (signed integer, round to nearest) and
// returns the quantized value.
// The valid codes of an N-bit signed integer are -2^(N-1) ... 2^(N-1) - 1 (16 bit: -32768 ... 32767),
// 2^N levels in total.
// You must clamp after rounding: otherwise x = 1.0 yields +2^(N-1) (+32768 for 16 bit), a code
// that integer PCM cannot store.
// And because the positive side has one fewer step, the largest positive value integer PCM can
// represent is 1 - 2^-(N-1), slightly below 1.0.
double quantize(double x, int numBits) {
    const double scale = std::ldexp(1.0, numBits - 1); // 2^(N-1): e.g. 16 bit -> 32768
    const double code = std::clamp(std::round(x * scale), -scale, scale - 1.0);
    return code / scale;
}

// Measures the SNR (in dB) after quantizing a sine near 1 kHz to numBits, and optionally writes a WAV file
double measureQuantizationSnrDb(int numBits, float amplitudeDb, const char* fileName) {
    using namespace dawlab;
    const int n = secondsToSamples(1.0, kSampleRate);
    const double amp = dbToGain(double(amplitudeDb));
    // Use 997 Hz rather than 1000 Hz: 1000 Hz is an integer ratio of 48 kHz, so the quantization error would repeat instead of looking like noise
    const double freq = 997.0;
    AudioBuffer out(1, n);
    double signalPower = 0.0, noisePower = 0.0;
    for (int i = 0; i < n; ++i) {
        const double x = amp * std::sin(kTwoPi * freq * i / kSampleRate);
        const double q = quantize(x, numBits);
        const double e = q - x; // quantization error
        signalPower += x * x;
        noisePower += e * e;
        out.setSample(0, i, static_cast<float>(q));
    }
    if (fileName)
        writeWav(outputPath(fileName), out, kSampleRate, 24);
    return 10.0 * std::log10(signalPower / noisePower);
}

} // namespace

int main() {
    using namespace dawlab;
    const int oneSecond = secondsToSamples(1.0, kSampleRate);

    // ---- 1. dB table ------------------------------------------------------
    std::printf("== dB <-> linear gain (gain = 10^(dB/20), dB = 20·log10(gain)) ==\n");
    const float dbTable[] = {12.0f, 6.0f, 3.0f, 0.0f, -3.0f, -6.0f, -12.0f, -20.0f, -40.0f, -60.0f};
    for (float db : dbTable)
        std::printf("  %+6.1f dB  →  × %.4f\n", db, dbToGain(db));
    std::printf("  × 2 = %+.2f dB, × 0.5 = %+.2f dB, × √2 = %+.2f dB\n", gainToDb(2.0f), gainToDb(0.5f),
                gainToDb(static_cast<float>(std::sqrt(2.0))));
    DAWLAB_CHECK_NEAR(gainToDb(2.0f), 6.0206, 0.001);
    DAWLAB_CHECK_NEAR(dbToGain(-20.0f), 0.1, 1e-6);

    // ---- 2. Summing identical vs uncorrelated signals ----------------------
    std::printf("\n== Summing two -12 dBFS signals ==\n");
    AudioBuffer sineA = makeSine(1000.0, kSampleRate, 1.0, dbToGain(-12.0f), 1);
    AudioBuffer sumIdentical = sineA;
    sumIdentical.addFrom(0, 0, sineA, 0, 0, oneSecond); // identical signals: the amplitudes add directly
    const float identicalDb = gainToDb(rms(sumIdentical, 0)) - gainToDb(rms(sineA, 0));
    std::printf("  Identical sines: RMS rises by %+.2f dB (amplitude ×2)\n", identicalDb);
    DAWLAB_CHECK_NEAR(identicalDb, 6.02, 0.02);

    AudioBuffer noiseA = makeWhiteNoise(kSampleRate, 1.0, 1.0f, 1, 1);
    AudioBuffer noiseB = makeWhiteNoise(kSampleRate, 1.0, 1.0f, 1, 777);
    // First normalize each to -12 dBFS RMS, then sum
    noiseA.applyGain(dbToGain(-12.0f) / rms(noiseA, 0));
    noiseB.applyGain(dbToGain(-12.0f) / rms(noiseB, 0));
    AudioBuffer sumUncorrelated = noiseA;
    sumUncorrelated.addFrom(0, 0, noiseB, 0, 0, oneSecond); // uncorrelated: the powers add, so the amplitude only grows by √2
    const float uncorrelatedDb = gainToDb(rms(sumUncorrelated, 0)) - gainToDb(rms(noiseA, 0));
    std::printf("  Uncorrelated white noise: RMS rises by %+.2f dB (power ×2, amplitude ×√2)\n", uncorrelatedDb);
    DAWLAB_CHECK_NEAR(uncorrelatedDb, 3.01, 0.1);
    writeWav(outputPath("l00_sum_identical.wav"), sumIdentical, kSampleRate);
    writeWav(outputPath("l00_sum_uncorrelated.wav"), sumUncorrelated, kSampleRate);

    // ---- 3. crest factor ---------------------------------------------------
    std::printf("\n== crest factor = peak / RMS ==\n");
    auto crestDb = [](const AudioBuffer& b) { return gainToDb(peakAbs(b)) - gainToDb(rms(b, 0)); };
    AudioBuffer square(1, oneSecond);
    for (int i = 0; i < oneSecond; ++i)
        square.setSample(0, i, (i / 24) % 2 == 0 ? 0.5f : -0.5f); // 1 kHz square wave (the sign flips every 24 samples)
    AudioBuffer sineFull = makeSine(1000.0, kSampleRate, 1.0, 0.5f, 1);
    AudioBuffer noiseFull = makeWhiteNoise(kSampleRate, 1.0, 0.5f, 1);
    AudioBuffer drums = makeDrumLoop(kSampleRate);
    std::printf("  square wave         %5.2f dB (theory: 0)\n", crestDb(square));
    std::printf("  sine wave           %5.2f dB (theory: 20·log10(√2) = 3.01)\n", crestDb(sineFull));
    std::printf("  uniform white noise %5.2f dB (theory: 20·log10(√3) = 4.77)\n", crestDb(noiseFull));
    std::printf("  drum loop           %5.2f dB (percussion peaks sit far above the average level)\n", crestDb(drums));
    DAWLAB_CHECK_NEAR(crestDb(square), 0.0, 0.01);
    DAWLAB_CHECK_NEAR(crestDb(sineFull), 3.01, 0.02);
    DAWLAB_CHECK_NEAR(crestDb(noiseFull), 4.77, 0.1);
    DAWLAB_CHECK(crestDb(drums) > 8.0f);

    // ---- 4. Aliasing -------------------------------------------------------
    std::printf("\n== Aliasing: f_in = 30 kHz, fs = 48 kHz ==\n");
    AudioBuffer sine30k = makeSine(30000.0, kSampleRate, 1.0, 0.5f, 1);
    const double at30k = goertzelAmplitude(sine30k.getReadPointer(0), oneSecond, 30000.0, kSampleRate);
    const double at18k = goertzelAmplitude(sine30k.getReadPointer(0), oneSecond, 18000.0, kSampleRate);
    std::printf("  Generated a 30 kHz sine of amplitude 0.5; measurements:\n");
    std::printf("    at 18 kHz (|30000 - 48000|) the amplitude is %.4f  <- all the energy has folded here\n", at18k);
    std::printf("    (at 30 kHz it measures %.4f: Goertzel sees the same set of samples at 30 kHz and at 18 kHz)\n", at30k);
    DAWLAB_CHECK_NEAR(at18k, 0.5, 0.01);
    writeWav(outputPath("l00_aliasing_30k.wav"), sine30k, kSampleRate);
    // A sweep up to 40 kHz: makeLogSweep just computes sin(phase), so nothing stops us from exceeding Nyquist
    writeWav(outputPath("l00_aliasing_sweep.wav"), makeLogSweep(20.0, 40000.0, kSampleRate, 6.0, 0.5f, 1),
             kSampleRate);

    // ---- 5. Quantization ---------------------------------------------------
    std::printf("\n== Quantization SNR (full-scale sine) vs 6.02·N + 1.76 dB ==\n");
    std::printf("  %4s %12s %12s\n", "bits", "measured", "formula");
    for (int bits : {8, 12, 16, 20, 24}) {
        const double measured = measureQuantizationSnrDb(bits, 0.0f, nullptr);
        const double formula = 6.02 * bits + 1.76;
        std::printf("  %4d %9.2f dB %9.2f dB\n", bits, measured, formula);
        DAWLAB_CHECK_NEAR(measured, formula, 1.0);
    }
    const double snr8at20 = measureQuantizationSnrDb(8, -20.0f, "l00_quantized_8bit.wav");
    const double snr16at20 = measureQuantizationSnrDb(16, -20.0f, "l00_quantized_16bit.wav");
    std::printf("  With the signal lowered to -20 dBFS the SNR drops by 20 dB too: 8 bit -> %.2f dB, 16 bit -> %.2f dB\n", snr8at20,
                snr16at20);
    DAWLAB_CHECK_NEAR(snr8at20, 6.02 * 8 + 1.76 - 20.0, 1.5);

    // ---- 6. Clipping -------------------------------------------------------
    // Note: 1 kHz is an integer ratio of 48 kHz (48 times), so when an odd harmonic above
    // Nyquist aliases back, it lands exactly on |k·1000 - 48000| = the position of another
    // *odd* harmonic (e.g. H25 -> 23 kHz = H23) and does not pollute the even-harmonic bins.
    // This coincidence is deliberate: it makes the number of clipped samples a tidy 26000
    // (Exercise 4). With a generic frequency (e.g. 1.1 kHz) the aliases land at positions that
    // are not integer multiples of the fundamental and turn into inharmonic noise.
    std::printf("\n== Clipping: a 1 kHz sine of amplitude 1.5 hard-clipped at ±1.0 ==\n");
    AudioBuffer loud = makeSine(1000.0, kSampleRate, 1.0, 1.5f, 1);
    int clipped = 0;
    for (int i = 0; i < oneSecond; ++i) {
        const float x = loud.getSample(0, i);
        if (std::abs(x) >= 1.0f) {
            ++clipped;
            loud.setSample(0, i, x > 0.0f ? 1.0f : -1.0f);
        }
    }
    const double thd = thdPercent(loud.getReadPointer(0), oneSecond, 1000.0, kSampleRate);
    const std::vector<double> harmonics = harmonicLevelsDb(loud.getReadPointer(0), oneSecond, 1000.0, kSampleRate, 5);
    std::printf("  clipped samples = %d / %d (%.1f%%; theory: the fraction with |sin| >= 2/3 ≈ 54%%)\n", clipped, oneSecond,
                100.0 * clipped / oneSecond);
    std::printf("  THD = %.2f%%; harmonics relative to the fundamental: ", thd);
    for (size_t h = 0; h < harmonics.size(); ++h)
        std::printf("H%zu %+.1f dB  ", h + 1, harmonics[h]);
    std::printf("\n  (the even harmonics H2 and H4 are near -100 dB = absent: symmetric clipping produces only odd harmonics)\n");
    DAWLAB_CHECK(clipped == 26000);
    DAWLAB_CHECK(thd > 5.0);
    writeWav(outputPath("l00_clipped_sine.wav"), loud, kSampleRate);

    std::printf("\n");
    return dawlab::test::summary("l00_db_math");
}
