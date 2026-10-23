// Example 4: verifying LoudnessMeter (ITU-R BS.1770-4 / EBU R128) against known signals
//
// What it demonstrates:
//   1. K-weighting: the standard 48 kHz coefficients vs coefficients redesigned from the analog prototype with the
//      bilinear transform (they should be almost identical), plus the 44.1 kHz / 96 kHz frequency responses (the
//      curves agree). The coefficients run in the chapter 02 Biquad.
//   2. The BS.1770-4 verification condition: a -20 dBFS 997 Hz sine fed into a single channel -> -23.01 LUFS
//      (-3.01 at 0 dBFS).
//   3. The same signal fed into both left and right channels -> double the power -> +3.01 LU -> -20.0 LUFS.
//   4. Gating: 3 s of -20 dBFS followed by 3 s of silence and a 3 s very quiet -50 dBFS section gives
//      integrated ≈ -23.23 LUFS (the silence is blocked by the absolute gate; the quiet section is blocked by the
//      relative gate; only the 3 blocks straddling the boundary pull the average down by 0.22 LU). The ungated
//      average is -27.78 LUFS.
//   Every section is verified numerically with DAWLAB_CHECK, and [PASS] / [FAIL] is printed at the end (ctest runs
//   this program too).
//   5. Momentary / short-term / integrated timelines of the drum loop and the pluck sequence.
//
// How to run: bin/Debug/l11_loudness.exe (no arguments needed; argv[1] may name an input WAV, whose LUFS are printed)
// Output files: dawlab_output/l11_loudness_997hz_mono.wav, l11_loudness_997hz_stereo.wav, l11_loudness_gated.wav

#include <dawlab/Common.h>
#include <dawlab/dsp/Biquad.h>
#include <dawlab/dsp/LoudnessMeter.h>

#include <cstdio>
#include <string>

using namespace dawlab;
using dawlab::dsp::BiquadCoefficients;
using dawlab::dsp::LoudnessMeter;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

// Feed block by block; print a timeline when printEverySeconds > 0
void feed(LoudnessMeter& meter, AudioBuffer& audio, double printEverySeconds = 0.0) {
    const int printEvery = printEverySeconds > 0.0 ? secondsToSamples(printEverySeconds, kSampleRate) : 0;
    int nextPrint = printEvery;
    if (printEvery > 0)
        std::printf("%8s %12s %12s %12s\n", "time(s)", "momentary", "short-term", "integrated");
    for (int start = 0; start < audio.getNumSamples(); start += kBlockSize) {
        const int len = std::min(kBlockSize, audio.getNumSamples() - start);
        meter.process(AudioBlock(audio).getSubBlock(start, len));
        if (printEvery > 0 && start + len >= nextPrint) {
            std::printf("%8.2f %9.2f LUFS %9.2f LUFS %9.2f LUFS\n", (start + len) / kSampleRate,
                        meter.getMomentaryLufs(), meter.getShortTermLufs(), meter.getIntegratedLufs());
            nextPrint += printEvery;
        }
    }
}

double kWeightingDb(double freq, double sr) {
    return LoudnessMeter::makeKWeightingStage1(sr).magnitudeDb(freq, sr) +
           LoudnessMeter::makeKWeightingStage2(sr).magnitudeDb(freq, sr);
}

} // namespace

int main(int argc, char** argv) {
    // ---- 1. K-weighting coefficients and frequency response ----------------------------------
    {
        std::printf("== 1. K-weighting ==\n");
        const BiquadCoefficients d1 = LoudnessMeter::makeKWeightingStage1(48000.0);
        const BiquadCoefficients d2 = LoudnessMeter::makeKWeightingStage2(48000.0);
        const double* t1 = LoudnessMeter::kStage1Table48k;
        const double* t2 = LoudnessMeter::kStage2Table48k;
        std::printf("stage 1 (high shelf) BS.1770-4 Table 1:        b0=%.11f b1=%.11f b2=%.11f a1=%.11f a2=%.11f\n", t1[0],
                    t1[1], t1[2], t1[3], t1[4]);
        std::printf("stage 1 redesigned from the analog prototype (48 kHz): b0=%.11f b1=%.11f b2=%.11f a1=%.11f a2=%.11f\n", d1.b0,
                    d1.b1, d1.b2, d1.a1, d1.a2);
        std::printf("stage 2 (highpass) BS.1770-4 Table 2:           b0=%.11f b1=%.11f b2=%.11f a1=%.11f a2=%.11f\n", t2[0],
                    t2[1], t2[2], t2[3], t2[4]);
        std::printf("stage 2 redesigned from the analog prototype (48 kHz): b0=%.11f b1=%.11f b2=%.11f a1=%.11f a2=%.11f\n", d2.b0,
                    d2.b1, d2.b2, d2.a1, d2.a2);
        const double maxDiff = std::max({std::abs(d1.b0 - t1[0]), std::abs(d1.b1 - t1[1]), std::abs(d1.b2 - t1[2]),
                                         std::abs(d1.a1 - t1[3]), std::abs(d1.a2 - t1[4]), std::abs(d2.b0 - t2[0]),
                                         std::abs(d2.b1 - t2[1]), std::abs(d2.b2 - t2[2]), std::abs(d2.a1 - t2[3]),
                                         std::abs(d2.a2 - t2[4])});
        std::printf("Max difference between the standard table and the redesign = %.2e (should be < 1e-10)\n\n", maxDiff);
        DAWLAB_CHECK(maxDiff < 1e-10);

        std::printf("%10s %12s %12s %12s\n", "freq (Hz)", "48 kHz", "44.1 kHz", "96 kHz");
        for (double f : {20.0, 38.0, 100.0, 300.0, 997.0, 1000.0, 2000.0, 5000.0, 10000.0, 16000.0})
            std::printf("%10.0f %9.3f dB %9.3f dB %9.3f dB\n", f, kWeightingDb(f, 48000.0), kWeightingDb(f, 44100.0),
                        kWeightingDb(f, 96000.0));
        DAWLAB_CHECK_NEAR(kWeightingDb(997.0, 48000.0), 0.691, 0.005);
        // The redesigned 44.1 / 96 kHz curves differ from the 48 kHz one by < 0.05 dB (the bilinear transform warps
        // high frequencies differently, hence the small difference)
        for (double f : {38.0, 100.0, 997.0, 2000.0, 10000.0}) {
            DAWLAB_CHECK_NEAR(kWeightingDb(f, 44100.0), kWeightingDb(f, 48000.0), 0.05);
            DAWLAB_CHECK_NEAR(kWeightingDb(f, 96000.0), kWeightingDb(f, 48000.0), 0.05);
        }
        // Counter-example: using the 48 kHz coefficient table directly at 44.1 kHz -> the curve shifts 8% toward low
        // frequencies (how far off is it at 2 kHz?)
        BiquadCoefficients wrong1, wrong2;
        wrong1.b0 = t1[0]; wrong1.b1 = t1[1]; wrong1.b2 = t1[2]; wrong1.a1 = t1[3]; wrong1.a2 = t1[4];
        wrong2.b0 = t2[0]; wrong2.b1 = t2[1]; wrong2.b2 = t2[2]; wrong2.a1 = t2[3]; wrong2.a2 = t2[4];
        const double wrongAt2k = wrong1.magnitudeDb(2000.0, 44100.0) + wrong2.magnitudeDb(2000.0, 44100.0);
        std::printf("\nCounter-example: using the 48 kHz coefficient table at 44.1 kHz -> %.3f dB at 2 kHz (correct: %.3f dB), %.3f dB at 997 Hz\n",
                    wrongAt2k, kWeightingDb(2000.0, 44100.0),
                    wrong1.magnitudeDb(997.0, 44100.0) + wrong2.magnitudeDb(997.0, 44100.0));
        DAWLAB_CHECK(std::abs(wrongAt2k - kWeightingDb(2000.0, 44100.0)) > 0.1);
        std::printf("-> The curves for all three sample rates agree: +0.691 dB at 997 Hz, which is where the -0.691 in the LUFS formula comes from;\n"
                    "   +4.04 dB at high frequencies (including the +0.043 dB from stage 2's un-normalized numerator), -6 dB at 38 Hz, rolling off below that.\n\n");
    }

    // ---- 2. -20 dBFS 997 Hz mono -> -23.01 LUFS ----------------------------------------------
    AudioBuffer mono = makeSine(997.0, kSampleRate, 5.0, dbToGain(-20.0f), 1);
    {
        LoudnessMeter meter;
        meter.prepare(kSampleRate, 1);
        feed(meter, mono);
        std::printf("== 2. -20 dBFS 997 Hz sine, single channel, 5 s ==\n");
        std::printf("momentary = %.2f  short-term = %.2f  integrated = %.2f LUFS (standard: -23.01; 0 dBFS -> -3.01)\n\n",
                    meter.getMomentaryLufs(), meter.getShortTermLufs(), meter.getIntegratedLufs());
        DAWLAB_CHECK_NEAR(meter.getMomentaryLufs(), -23.01, 0.02);
        DAWLAB_CHECK_NEAR(meter.getShortTermLufs(), -23.01, 0.02);
        DAWLAB_CHECK_NEAR(meter.getIntegratedLufs(), -23.01, 0.02);
        writeWav(outputPath("l11_loudness_997hz_mono.wav"), mono, kSampleRate);
    }

    // ---- 3. The same signal on left and right -> +3.01 LU ----------------------------------
    AudioBuffer stereo = makeSine(997.0, kSampleRate, 5.0, dbToGain(-20.0f), 2);
    {
        LoudnessMeter meter;
        meter.prepare(kSampleRate, 2);
        feed(meter, stereo);
        std::printf("== 3. The same signal on L and R ==\n");
        std::printf("integrated = %.2f LUFS (the two channels' powers add -> +3.01 LU -> -20.00)\n\n", meter.getIntegratedLufs());
        DAWLAB_CHECK_NEAR(meter.getIntegratedLufs(), -20.00, 0.02);
        writeWav(outputPath("l11_loudness_997hz_stereo.wav"), stereo, kSampleRate);
    }

    // ---- 4. gating ---------------------------------------------------------------------------
    {
        // 3 s of -20 dBFS, 3 s of silence, 3 s of -50 dBFS
        AudioBuffer gated = makeToneBursts(997.0, kSampleRate, {-20.0f, -200.0f, -50.0f}, 3.0, 1);
        LoudnessMeter meter;
        meter.prepare(kSampleRate, 1);
        std::printf("== 4. Gating: 3 s @ -20 dBFS -> 3 s silence -> 3 s @ -50 dBFS ==\n");
        feed(meter, gated, 1.0);
        // Expected value: the first section has 27 complete 400 ms blocks (power P) plus 3 blocks straddling the
        // "signal -> silence" boundary (power 3P/4, 2P/4, P/4, all passing the relative gate), so the average is
        // 28.5P/30 -> -23.01 + 10·log10(0.95) = -23.23.
        // The silent blocks (-∞) are blocked by the absolute gate; the -53 LUFS blocks are below the relative gate
        // (about -36) and are blocked too.
        const double p = std::pow(10.0, (-23.01 + 0.691) / 10.0);
        const double expectedGated = LoudnessMeter::powerToLufs(28.5 * p / 30.0);
        // The "average power" without gating: (3·P + 0 + 3·P/1000) / 9
        const double ungated = LoudnessMeter::powerToLufs((3.0 * p + 3.0 * p * 1e-3) / 9.0);
        std::printf("integrated = %.2f LUFS (expected %.2f: the silence is blocked by the absolute gate and the -53 LUFS section by the relative gate;\n"
                    "   the 3 \"half-full\" blocks on the boundary pass the gate and pull the average down by 10·log10(28.5/30) = -0.22 LU)\n",
                    meter.getIntegratedLufs(), expectedGated);
        std::printf("Without any gating, averaging the power over the whole 9 s -> %.2f LUFS (diluted by the silence)\n\n", ungated);
        DAWLAB_CHECK_NEAR(meter.getIntegratedLufs(), expectedGated, 0.05);
        DAWLAB_CHECK(meter.getIntegratedLufs() > ungated + 4.0f);
        writeWav(outputPath("l11_loudness_gated.wav"), gated, kSampleRate);
    }

    // ---- 5. Musical material -----------------------------------------------------------------
    {
        AudioBuffer drums = makeDrumLoop(kSampleRate, 2, 120.0, 0.8f, 2);
        LoudnessMeter meter;
        meter.prepare(kSampleRate, 2);
        std::printf("== 5a. Drum loop (4 s) ==\n");
        feed(meter, drums, 0.5);
        const float drumsIntegrated = meter.getIntegratedLufs();
        const float drumsPeakDb = gainToDb(peakAbs(drums));
        std::printf("integrated = %.2f LUFS, max momentary = %.2f, max short-term = %.2f, sample peak = %.2f dBFS\n\n",
                    drumsIntegrated, meter.getMaxMomentaryLufs(), meter.getMaxShortTermLufs(),
                    drumsPeakDb);
        // Highly dynamic material: max momentary is clearly above integrated, and integrated is far below the sample
        // peak (crest factor)
        DAWLAB_CHECK(meter.getMaxMomentaryLufs() > drumsIntegrated + 3.0f);
        DAWLAB_CHECK(drumsIntegrated < drumsPeakDb - 10.0f);

        AudioBuffer plucks = makePluckSequence(kSampleRate, 4.0, 0.4f, 2);
        meter.reset();
        std::printf("== 5b. Pluck sequence (4 s) ==\n");
        feed(meter, plucks, 0.5);
        std::printf("integrated = %.2f LUFS, max momentary = %.2f, sample peak = %.2f dBFS\n",
                    meter.getIntegratedLufs(), meter.getMaxMomentaryLufs(), gainToDb(peakAbs(plucks)));
        std::printf("-> Streaming-platform targets are about -14 LUFS (Spotify / YouTube), -16 (Apple) and -23 (EBU R128 broadcast):\n"
                    "   pushing the drum loop to -14 LUFS needs %.1f dB of gain, and the sample peak becomes %.2f dBFS --\n"
                    "   already beyond the recommended -1 dBTP ceiling; in practice you would first squash the peaks with a limiter (chapter 05) and then raise the level.\n\n",
                    -14.0f - drumsIntegrated, drumsPeakDb + (-14.0f - drumsIntegrated));
    }

    if (argc > 1) {
        WavData wav;
        std::string err;
        if (!readWav(argv[1], wav, &err)) {
            std::printf("Failed to read: %s\n", err.c_str());
            return 1;
        }
        LoudnessMeter meter;
        meter.prepare(wav.sampleRate, wav.buffer.getNumChannels());
        for (int start = 0; start < wav.buffer.getNumSamples(); start += kBlockSize) {
            const int len = std::min(kBlockSize, wav.buffer.getNumSamples() - start);
            meter.process(AudioBlock(wav.buffer).getSubBlock(start, len));
        }
        std::printf("%s: integrated = %.2f LUFS, max momentary = %.2f, max short-term = %.2f\n", argv[1],
                    meter.getIntegratedLufs(), meter.getMaxMomentaryLufs(), meter.getMaxShortTermLufs());
    }
    return dawlab::test::summary("l11_loudness");
}
