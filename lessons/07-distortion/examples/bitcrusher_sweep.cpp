// Example 3: bitcrusher -- sweep the bit depth and downsample factor, measure SNR and compare with theory
//
// What it demonstrates:
//   (1) Bit depth 2 → 16: quantize a full-scale (0 dBFS) 997 Hz sine and measure SNR = 20 log10(rms(signal) / rms(error)),
//       then compare with the theoretical 6.02 N + 1.76 dB. Every bit removed costs about 6 dB of SNR.
//   (2) The same quantization, but the signal is only -20 dBFS: SNR is 20 dB lower -- quantization noise has a "fixed size", so quiet signals fare relatively worse.
//       (This is also why real 16-bit recordings need dither, as mentioned in Chapter 00.)
//   (3) Downsample factor 1 → 16 (sample & hold): the effective sample rate goes from 48k to 3k and SNR collapses,
//       and a 5 kHz sine through factor 8 (effective Nyquist 3 kHz) aliases to 1 kHz -- measured with Goertzel to show you.
//   (4) Writes WAVs of the pluck sequence for several settings.
//
// How to run: l07_bitcrusher_sweep (no arguments needed)
//
// Output files (in dawlab_output/):
//   l07_bitcrush_dry.wav     original
//   l07_bitcrush_12bit.wav   12 bit (barely audible difference; the tails may have a little hiss)
//   l07_bitcrush_8bit.wav    8 bit (clear quantization noise; the tails "break up")
//   l07_bitcrush_4bit.wav    4 bit (very rough)
//   l07_bitcrush_ds8.wav     16 bit but downsample x8 (effective 6 kHz: dull, with metallic aliasing)
//   l07_bitcrush_lofi.wav    8 bit + x4 (the classic lo-fi sampler sound)
//
// What to listen for: the fewer the bits, the more noise, and the noise "follows the notes" (it is not a steady hiss); the larger the downsample factor, the less high end,
//         but at the same time there are extra inharmonic aliasing overtones.
//
// Note: bitcrush does not need oversampling (the aliasing is the effect itself); this example turns oversampling off, so latency = 0
//       and the input and output can be compared sample by sample directly.

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

// SNR (dB) = 20 log10( rms(in) / rms(out - in) ); the two must be aligned (latency = 0)
double measureSnrDb(const dawlab::AudioBuffer& in, const dawlab::AudioBuffer& out) {
    const int n = in.getNumSamples();
    double sig = 0.0, err = 0.0;
    for (int i = 0; i < n; ++i) {
        const double x = in.getSample(0, i);
        const double e = out.getSample(0, i) - x;
        sig += x * x;
        err += e * e;
    }
    return err > 0.0 ? 10.0 * std::log10(sig / err) : 200.0;
}

dawlab::dsp::Distortion makeCrusher(float bits, int downsample, int numChannels) {
    dawlab::dsp::Distortion d;
    d.setOversamplingFactorLog2(0); // bitcrush does not need oversampling
    d.prepare(kSampleRate, kBlockSize, numChannels);
    d.setType(dawlab::dsp::DistortionType::Bitcrush);
    d.setDriveDb(0.0f);
    d.setToneHz(20000.0f);
    d.setMix(1.0f);
    d.setBitDepth(bits);
    d.setDownsampleFactor(downsample);
    return d;
}

} // namespace

int main() {
    using dawlab::dsp::Distortion;
    bool ok = true;
    // 997 Hz: not an integer ratio to 48000, so the quantization error does not repeat every cycle (a repeating error becomes harmonics rather than noise)
    const double f0 = 997.0;

    // ---- (1)(2) bit depth sweep ----
    for (float ampDb : {0.0f, -20.0f}) {
        const float amp = dawlab::dbToGain(ampDb);
        std::printf("%s(%d) Quantize a %.0f dBFS, %.0f Hz sine: step = 2 / 2^N, theoretical SNR = 6.02 N + 1.76 + 20 log10(A)\n",
                    ampDb == 0.0f ? "" : "\n", ampDb == 0.0f ? 1 : 2, ampDb, f0);
        std::printf("%6s %12s %12s %12s %8s\n", "bits", "step", "SNR meas.", "SNR theory", "diff");
        double snr8 = 0.0, snr16 = 0.0;
        for (float bits : {2.0f, 3.0f, 4.0f, 6.0f, 8.0f, 10.0f, 12.0f, 16.0f}) {
            dawlab::AudioBuffer in = dawlab::makeSine(f0, kSampleRate, 1.0, amp, 1);
            dawlab::AudioBuffer out = in;
            Distortion d = makeCrusher(bits, 1, 1);
            processInBlocks(out, d);
            const double snr = measureSnrDb(in, out);
            const double theory = Distortion::theoreticalSnrDb(bits, amp);
            std::printf("%6.0f %12.3g %12.2f %12.2f %+8.2f\n", bits, 2.0 / std::exp2(bits), snr, theory, snr - theory);
            if (bits == 8.0f)
                snr8 = snr;
            if (bits == 16.0f)
                snr16 = snr;
            // The theoretical formula assumes "uniformly distributed error and an unclipped signal". With few bits neither holds:
            //   - The largest positive value of a mid-tread quantizer is (2^(N-1) - 1)·step = 1 - step, so the positive peak at 0 dBFS gets cut off (4 bit: 0.125 is cut off!)
            //   - With too few levels (2 bit has only 4 levels) the error is highly correlated with the signal and does not behave like noise
            //   - When the signal is smaller than step/2 the whole thing quantizes to 0 → SNR = 0 dB (this is what happens at -20 dBFS with 2 and 3 bit: the tail simply disappears)
            if (bits >= 6.0f)
                ok = ok && std::abs(snr - theory) < 2.0;
        }
        std::printf("    8 → 16 bit: %.2f dB per bit (theory 6.02)\n", (snr16 - snr8) / 8.0);
        ok = ok && std::abs((snr16 - snr8) / 8.0 - 6.02) < 0.3;
    }
    std::printf("(One bit fewer → 6.02 dB less SNR; a signal 20 dB quieter → SNR also 20 dB lower: the size of the quantization noise does not change with the signal.\n"
                "  Deviations at low bit counts: at 0 dBFS the positive peak is cut by the mid-tread upper limit 1 - step; at -20 dBFS with 2-3 bit the signal is < step/2 and the whole thing becomes 0.)\n");

    // ---- (3) downsample sweep ----
    {
        std::printf("\n(3) sample & hold: %.0f Hz sine at 16 bit, 0 dBFS, downsample factor 1 → 16\n", f0);
        std::printf("%8s %14s %12s\n", "factor", "effective rate", "SNR meas.");
        double previousSnr = 1.0e9;
        for (int factor : {1, 2, 4, 8, 16}) {
            dawlab::AudioBuffer in = dawlab::makeSine(f0, kSampleRate, 1.0, 1.0f, 1);
            dawlab::AudioBuffer out = in;
            Distortion d = makeCrusher(16.0f, factor, 1);
            processInBlocks(out, d);
            const double snr = measureSnrDb(in, out);
            std::printf("%8d %11.0f Hz %12.2f\n", factor, kSampleRate / factor, snr);
            ok = ok && snr < previousSnr; // the larger the factor, the coarser the S&H staircase and the larger the error
            if (factor == 2)
                ok = ok && snr < 30.0;    // holding just one sample drops SNR from 98 dB to about 20 dB
            previousSnr = snr;
        }

        // 5 kHz sine, factor 8 (effective fs' = 6 kHz, Nyquist 3 kHz): 5 kHz aliases to |5000 - 6000| = 1000 Hz
        const double fin = 5000.0;
        const int factor = 8;
        const double fsEff = kSampleRate / factor;
        const double alias = std::abs(fin - fsEff);
        dawlab::AudioBuffer sine = dawlab::makeSine(fin, kSampleRate, 1.0, 0.5f, 1);
        Distortion d = makeCrusher(16.0f, factor, 1);
        processInBlocks(sine, d);
        const int skip = dawlab::secondsToSamples(0.1, kSampleRate);
        const int n = dawlab::secondsToSamples(0.5, kSampleRate);
        const double atIn = dawlab::gainToDb(dawlab::goertzelAmplitude(sine.getReadPointer(0) + skip, n, fin, kSampleRate));
        const double atAlias = dawlab::gainToDb(dawlab::goertzelAmplitude(sine.getReadPointer(0) + skip, n, alias, kSampleRate));
        const double atImage = dawlab::gainToDb(dawlab::goertzelAmplitude(sine.getReadPointer(0) + skip, n, fsEff + fin, kSampleRate));
        // The frequency response of sample & hold (zero-order hold) is a sinc: |H(f)| = |sin(π f / fs') / (π f / fs')|
        auto zohDb = [&](double f) { return dawlab::gainToDb(std::abs(std::sin(dawlab::kPi * f / fsEff) / (dawlab::kPi * f / fsEff))); };
        std::printf("\n    %.0f Hz sine (-6 dBFS), factor %d (fs' = %.0f Hz):\n", fin, factor, fsEff);
        std::printf("    %6.0f Hz (original)   %6.1f dBFS   (ZOH sinc theory: -6 %+.1f = %.1f)\n", fin, atIn, zohDb(fin), -6.0 + zohDb(fin));
        std::printf("    %6.0f Hz (alias)      %6.1f dBFS   <- a frequency that was not there originally (theory -6 %+.1f = %.1f)\n", alias, atAlias, zohDb(alias), -6.0 + zohDb(alias));
        std::printf("    %6.0f Hz (image)      %6.1f dBFS   <- fs' + f\n", fsEff + fin, atImage);
        ok = ok && std::abs(atAlias - (-6.0 + zohDb(alias))) < 1.0; // the alias level is determined by the ZOH sinc
        ok = ok && std::abs(atIn - (-6.0 + zohDb(fin))) < 1.0;
    }

    // ---- (4) WAV ----
    {
        dawlab::AudioBuffer dry = dawlab::makePluckSequence(kSampleRate, 4.0, 0.4f, 2);
        dawlab::writeWav(dawlab::outputPath("l07_bitcrush_dry.wav"), dry, kSampleRate);
        struct Setting {
            float bits;
            int factor;
            const char* name;
        };
        const Setting settings[] = {
            {12.0f, 1, "l07_bitcrush_12bit.wav"}, {8.0f, 1, "l07_bitcrush_8bit.wav"}, {4.0f, 1, "l07_bitcrush_4bit.wav"},
            {16.0f, 8, "l07_bitcrush_ds8.wav"},   {8.0f, 4, "l07_bitcrush_lofi.wav"},
        };
        std::printf("\n(4) pluck sequence (peak %.1f dBFS)\n", dawlab::gainToDb(dawlab::peakAbs(dry)));
        std::printf("%24s %6s %8s %12s\n", "file", "bits", "factor", "SNR(dB)");
        for (const Setting& s : settings) {
            dawlab::AudioBuffer out = dry;
            Distortion d = makeCrusher(s.bits, s.factor, 2);
            processInBlocks(out, d);
            ok = ok && !dawlab::hasNanOrInf(out);
            std::printf("%24s %6.0f %8d %12.2f\n", s.name, s.bits, s.factor, measureSnrDb(dry, out));
            dawlab::writeWav(dawlab::outputPath(s.name), out, kSampleRate);
        }
        std::printf("(The pluck's peak is about -5 dBFS and its RMS only about -20 dBFS, and the tails keep getting quieter, so the SNR at the same bit count is much lower than for a full-scale sine)\n");
    }

    std::printf("\nWrote dawlab_output/l07_bitcrush_*.wav\n");
    std::printf("%s\n", ok ? "All as expected." : "[FAIL] Some item did not meet expectations!");
    return ok ? 0 : 1;
}
