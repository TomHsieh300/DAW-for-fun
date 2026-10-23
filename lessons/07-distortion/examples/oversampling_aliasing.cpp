// Example 4: does hard clip really need oversampling? Measure the aliasing and see
//
// What it demonstrates:
//   (1) A 5 kHz sine through a 20 dB-drive hard clip generates odd harmonics at 48 kHz: 15k, 25k, 35k, 45k, 55k ...
//       Only 15 kHz is inside Nyquist (24 kHz); the rest "fold back":
//         25k → 48k - 25k = 23k, 35k → 13k, 45k → 3k, 55k → 55k - 48k = 7k, 65k → 17k ...
//       These frequencies have no harmonic relationship with 5 kHz and sound like inharmonic "metallic" noise.
//       The table compares the alias level (dBFS) at 3 kHz, 7 kHz, 13 kHz, and 23 kHz for 1x / 2x / 4x / 8x oversampling,
//       then works out, for each factor, the lowest harmonic number that folds back to 3 kHz / 7 kHz, explaining why 2x and 4x are nearly the same and only 8x improves things substantially.
//       self-check: latency = 0 / 31 / 36 / 38 samples (derived from the halfband lengths), 4x is over 20 dB lower than 1x,
//       8x is over 20 dB lower than 4x, 2x and 4x differ by less than 6 dB, and the true harmonics are unaffected.
//   (2) Control group: the same signal through Bitcrush (8 bit, no downsample) -- quantization noise is spread across the whole band anyway,
//       so oversampling does not help (and S&H aliasing is part of the effect to begin with).
//   (3) Writes a log sweep (100 Hz → 10 kHz) hard-clipped at 1x and at 4x, so you can hear the alias "chirps" sweeping downward.
//
// How to run: l07_oversampling_aliasing (no arguments needed)
//
// Output files (in dawlab_output/):
//   l07_alias_sweep_1x.wav   no oversampling: as the sweep rises you hear several "downward-sweeping" noise lines
//   l07_alias_sweep_4x.wav   4x oversampling: much cleaner
//
// What to listen for: in the 1x version, once the sweep passes 2-3 kHz, downward-falling whistles appear in the background; the 4x version has almost none.

#include <dawlab/Common.h>
#include <dawlab/dsp/Distortion.h>

#include <cmath>
#include <cstdio>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

template <typename Processor>
void processInBlocks(dawlab::AudioBuffer& buffer, Processor& processor) {
    dawlab::AudioBlock whole(buffer);
    for (int start = 0; start < buffer.getNumSamples(); start += kBlockSize) {
        const int len = std::min(kBlockSize, buffer.getNumSamples() - start);
        processor.process(whole.getSubBlock(start, len));
    }
}

double levelDb(const dawlab::AudioBuffer& b, double freq) {
    const int skip = dawlab::secondsToSamples(0.2, kSampleRate);
    const int n = dawlab::secondsToSamples(0.5, kSampleRate);
    return dawlab::gainToDb(dawlab::goertzelAmplitude(b.getReadPointer(0) + skip, n, freq, kSampleRate));
}

} // namespace

int main() {
    using dawlab::dsp::Distortion;
    using dawlab::dsp::DistortionType;
    bool ok = true;

    const double f0 = 5000.0;
    const double aliasFreqs[] = {3000.0, 7000.0, 13000.0, 23000.0}; // 45k→3k, 55k→7k, 35k→13k, 25k→23k
    const double harmonicFreqs[] = {5000.0, 15000.0};

    std::printf("(1) %.0f Hz sine (-6 dBFS), HardClip drive 20 dB: dBFS of harmonics and aliases\n", f0);
    std::printf("%6s %8s |", "OS", "latency");
    for (double f : harmonicFreqs)
        std::printf(" H@%5.0fk", f / 1000.0);
    std::printf(" |");
    for (double f : aliasFreqs)
        std::printf(" A@%5.0fk", f / 1000.0);
    std::printf("\n");

    double alias3k[4] = {0, 0, 0, 0};
    double alias7k[4] = {0, 0, 0, 0};
    double h5k[4] = {0, 0, 0, 0}, h15k[4] = {0, 0, 0, 0};
    int latency[4] = {0, 0, 0, 0};
    for (int log2 = 0; log2 <= 3; ++log2) {
        dawlab::AudioBuffer sine = dawlab::makeSine(f0, kSampleRate, 1.0, 0.5f, 1);
        Distortion d;
        d.setOversamplingFactorLog2(log2);
        d.prepare(kSampleRate, kBlockSize, 1);
        d.setType(DistortionType::HardClip);
        d.setDriveDb(20.0f);
        d.setToneHz(20000.0f);
        processInBlocks(sine, d);

        std::printf("%5dx %5d smp |", 1 << log2, d.getLatencySamples());
        for (double f : harmonicFreqs)
            std::printf(" %8.1f", levelDb(sine, f));
        std::printf(" |");
        for (double f : aliasFreqs)
            std::printf(" %8.1f", levelDb(sine, f));
        std::printf("\n");
        alias3k[log2] = levelDb(sine, 3000.0);
        alias7k[log2] = levelDb(sine, 7000.0);
        h5k[log2] = levelDb(sine, 5000.0);
        h15k[log2] = levelDb(sine, 15000.0);
        latency[log2] = d.getLatencySamples();
    }
    std::printf("(H = true harmonic, A = alias. At 1x the 3 kHz alias comes from the 9th harmonic at 45 kHz: |45k - 48k| = 3k)\n"
                "(Oversampling only \"reduces\" aliasing, it does not \"eliminate\" it: a hard clip has infinitely many harmonics that fall off only as about 1/n,\n"
                "  so at 4x (192 kHz) the 39th harmonic at 195 kHz still folds back to 3 kHz, just much smaller.\n"
                "  The 23 kHz column barely drops: the 25 kHz harmonic lies in the transition band of the halfband filter (centered on 24 kHz),\n"
                "  so it is attenuated only slightly, and the 23 kHz it folds to is inaudible anyway.)\n");
    // Why are 2x and 4x almost the same at 3 kHz / 7 kHz? At the high rate L·fs, the n-th harmonic folds to |n·f0 - k·L·fs|.
    // 2x (96 kHz) and 4x (192 kHz) share the "fold point" 192 kHz: the 39th harmonic at 195 kHz → 3 kHz and the 37th at 185 kHz → 7 kHz
    // cannot escape in either case; 8x (384 kHz) only folds back at the 77th (385 kHz → 1 kHz) and the 153rd (765 kHz → 3 kHz), which are much smaller.
    std::printf("\nWhy 2x ≈ 4x? Find the lowest harmonic that folds back to 3 kHz / 7 kHz at each factor:\n");
    std::printf("%6s %22s %22s\n", "OS", "-> 3 kHz", "-> 7 kHz");
    for (int log2 = 0; log2 <= 3; ++log2) {
        const int fsHigh = 48000 << log2;
        int lowest[2] = {0, 0};
        const int targets[2] = {3000, 7000};
        for (int t = 0; t < 2; ++t) {
            for (int h = 3; h < 400 && lowest[t] == 0; h += 2) { // hard clip has only odd harmonics
                const int f = 5000 * h;
                const int folded = std::abs(f - fsHigh * ((f + fsHigh / 2) / fsHigh)); // |f - k·fsHigh|, k = round(f / fsHigh)
                if (folded == targets[t])
                    lowest[t] = h;
            }
        }
        std::printf("%5dx %12d (%3d kHz) %12d (%3d kHz)\n", 1 << log2, lowest[0], lowest[0] * 5, lowest[1], lowest[1] * 5);
    }

    // The latency formula (Chapter 06 Oversampler): Σ_i 2(2K_i - 1)·2^(S-i) samples "at the highest rate", divided by 2^S and truncated to an integer
    //   K_i = (odd tap count of each stage) / 2 = 16, 6, 4 → 2x: 62/2 = 31; 4x: (124 + 22)/4 = 36.5 → 36; 8x: (248 + 44 + 14)/8 = 38.25 → 38
    {
        using dawlab::dsp::Oversampler;
        const int oddTaps[3] = {Oversampler::kOddTapsStage1, Oversampler::kOddTapsStage2, Oversampler::kOddTapsStage3};
        bool latencyOk = latency[0] == 0;
        for (int S = 1; S <= 3; ++S) {
            int units = 0;
            for (int i = 1; i <= S; ++i)
                units += 2 * (oddTaps[i - 1] - 1) * (1 << (S - i)); // 2(2K - 1), where 2K = the odd tap count
            latencyOk = latencyOk && latency[S] == units / (1 << S);
        }
        std::printf("self-check: latency = 0 / 31 / 36 / 38 (derived from the halfband lengths) %s\n", latencyOk ? "OK" : "NG");
        ok = ok && latencyOk;
    }

    // 4x must be much cleaner than 1x; 8x much cleaner than 4x; 2x and 4x differ by less than 6 dB at 3 kHz (the same harmonics fold back);
    // the true harmonics (5 kHz, 15 kHz) are unaffected by oversampling
    {
        const bool c1 = (alias3k[0] - alias3k[2]) > 20.0 && (alias7k[0] - alias7k[2]) > 15.0;
        const bool c2 = (alias3k[2] - alias3k[3]) > 20.0 && (alias7k[2] - alias7k[3]) > 20.0;
        const bool c3 = std::abs(alias3k[1] - alias3k[2]) < 6.0 && std::abs(alias7k[1] - alias7k[2]) < 6.0;
        bool c4 = true;
        for (int i = 1; i <= 3; ++i)
            c4 = c4 && std::abs(h5k[i] - h5k[0]) < 0.2 && std::abs(h15k[i] - h15k[0]) < 0.5;
        std::printf("self-check: 4x is > 20 dB below 1x %s, 8x is > 20 dB below 4x %s, 2x ≈ 4x (< 6 dB) %s, harmonics unchanged %s\n",
                    c1 ? "OK" : "NG", c2 ? "OK" : "NG", c3 ? "OK" : "NG", c4 ? "OK" : "NG");
        ok = ok && c1 && c2 && c3 && c4;
    }

    // ---- (2) Bitcrush control group ----
    {
        std::printf("\n(2) The same sine through Bitcrush 8 bit (drive 0 dB, no downsample): quantization noise level at 3 kHz / 7 kHz\n");
        std::printf("%6s %10s %10s\n", "OS", "@3k", "@7k");
        double crush7k[2] = {0.0, 0.0};
        for (int log2 : {0, 2}) {
            dawlab::AudioBuffer sine = dawlab::makeSine(f0, kSampleRate, 1.0, 0.5f, 1);
            Distortion d;
            d.setOversamplingFactorLog2(log2);
            d.prepare(kSampleRate, kBlockSize, 1);
            d.setType(DistortionType::Bitcrush);
            d.setBitDepth(8.0f);
            d.setDriveDb(0.0f);
            d.setToneHz(20000.0f);
            processInBlocks(sine, d);
            std::printf("%5dx %10.1f %10.1f\n", 1 << log2, levelDb(sine, 3000.0), levelDb(sine, 7000.0));
            crush7k[log2 == 0 ? 0 : 1] = levelDb(sine, 7000.0);
        }
        ok = ok && std::abs(crush7k[0] - crush7k[1]) < 0.01;
        std::printf("(The two rows are identical: the Bitcrush path never goes through the oversampler, and quantization noise is not something oversampling can handle)\n");
    }

    // ---- (3) sweep WAV ----
    {
        for (int log2 : {0, 2}) {
            dawlab::AudioBuffer sweep = dawlab::makeLogSweep(100.0, 10000.0, kSampleRate, 4.0, 0.5f, 2);
            Distortion d;
            d.setOversamplingFactorLog2(log2);
            d.prepare(kSampleRate, kBlockSize, 2);
            d.setType(DistortionType::HardClip);
            d.setDriveDb(20.0f);
            d.setToneHz(20000.0f);
            d.setOutputGainDb(-6.0f);
            processInBlocks(sweep, d);
            ok = ok && !dawlab::hasNanOrInf(sweep);
            const char* name = log2 == 0 ? "l07_alias_sweep_1x.wav" : "l07_alias_sweep_4x.wav";
            dawlab::writeWav(dawlab::outputPath(name), sweep, kSampleRate);
        }
        std::printf("\n(3) Wrote dawlab_output/l07_alias_sweep_1x.wav and l07_alias_sweep_4x.wav\n");
    }

    std::printf("%s\n", ok ? "All as expected." : "[FAIL] Some item did not meet expectations!");
    return ok ? 0 : 1;
}
