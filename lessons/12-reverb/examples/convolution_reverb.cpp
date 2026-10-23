// Example 3: convolution reverb -- uniform partitioned FFT convolution vs direct convolution
//
// What it demonstrates:
//   1. Use ConvolutionReverb::makeSyntheticImpulseResponse() to synthesize a stereo IR with RT60 = 0.3 s,
//      and apply it to a pluck signal with (a) direct convolution y[n] = sum h[k]*x[n-k] and
//      (b) partitioned FFT convolution, then compare the error (should only be at float precision)
//      and the running time.
//   2. Verify latency = partition size: the partitioned output is P samples later than the direct convolution.
//   3. Print the number of partitions, the FFT size and the spectrum memory usage, and compare with the
//      theoretical number of multiply-adds per output sample.
//   4. Process a whole pluck sequence with a 2 s IR and write WAVs (direct convolution would be too slow
//      at this length, so only the partitioned version runs).
//
// How to run: run with no arguments (ctest runs it too).
// Output files (build/dawlab_output/):
//   l12_conv_ir.wav         the synthesized 2 s stereo impulse response (sounds like a "thump" followed by a noisy tail)
//   l12_conv_dry.wav        the original pluck sequence
//   l12_conv_wet.wav        after the convolution reverb (mix = 0.35)
// What you should hear: the plucks are joined by a smooth tail, as if played in a medium-sized room; the tail
// has no metallic ringing, because the synthetic IR is "noise" rather than a "regular echo".

#include <dawlab/Common.h>
#include <dawlab/dsp/ConvolutionReverb.h>

#include <chrono>
#include <cstdio>
#include <vector>

using namespace dawlab;
using namespace dawlab::dsp;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

// Direct convolution (for teaching, O(N*M)): y[n] = sum_{k=0}^{M-1} h[k]*x[n-k]; only the first numOut outputs are computed
void directConvolve(const float* x, int n, const float* h, int m, float* y, int numOut) {
    for (int i = 0; i < numOut; ++i) {
        double acc = 0.0;
        const int kMax = std::min(m - 1, i);
        for (int k = 0; k <= kMax; ++k)
            if (i - k < n)
                acc += static_cast<double>(h[k]) * x[i - k];
        y[i] = static_cast<float>(acc);
    }
}

double elapsedMs(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

// Run ConvolutionReverb block by block (mix = 1 -> pure wet)
AudioBuffer runPartitioned(ConvolutionReverb& conv, const AudioBuffer& in) {
    AudioBuffer out = in;
    conv.reset();
    for (int start = 0; start < out.getNumSamples(); start += kBlockSize) {
        const int len = std::min(kBlockSize, out.getNumSamples() - start);
        conv.process(out.toBlock().getSubBlock(start, len));
    }
    return out;
}

} // namespace

int main() {
    std::printf("=== Chapter 12 Example 3: convolution reverb (partitioned FFT) vs direct convolution ===\n\n");

    // ---- 1. Short IR: direct convolution vs partitioned, comparing error and speed -----
    {
        const double rt60 = 0.3;
        AudioBuffer ir = ConvolutionReverb::makeSyntheticImpulseResponse(kSampleRate, rt60, 2, 7);
        AudioBuffer dry = makePluckSequence(kSampleRate, 1.0, 0.4f, 2);
        const int n = dry.getNumSamples();
        const int m = ir.getNumSamples();

        ConvolutionReverb conv;
        conv.prepare(kSampleRate, kBlockSize, 2);
        conv.setImpulseResponse(ir);
        conv.setMix(1.0f);
        const int P = conv.getLatencySamples();

        std::printf("--- Short IR (RT60 = %.1f s -> %d samples, %d partitions) applied to %.1f s of pluck ---\n", rt60, m,
                    conv.getNumPartitions(), n / kSampleRate);
        std::printf("partition size P = %d, FFT size N = 2P = %d, latency = %d samples (%.2f ms)\n", P,
                    conv.getFftSize(), conv.getLatencySamples(), samplesToMs(conv.getLatencySamples(), kSampleRate));

        // Direct convolution: compute P extra samples so it lines up with the partitioned output (which is P samples later)
        AudioBuffer direct(2, n + P);
        auto t0 = std::chrono::steady_clock::now();
        for (int ch = 0; ch < 2; ++ch)
            directConvolve(dry.getReadPointer(ch), n, ir.getReadPointer(ch), m, direct.getWritePointer(ch), n + P);
        const double directMs = elapsedMs(t0);

        // Partitioned: pad the input with P zeros so the last P samples of output also come out
        AudioBuffer padded(2, n + P);
        for (int ch = 0; ch < 2; ++ch)
            padded.copyFrom(ch, 0, dry, ch, 0, n);
        t0 = std::chrono::steady_clock::now();
        AudioBuffer part = runPartitioned(conv, padded);
        const double partMs = elapsedMs(t0);

        // Error: part[i + P] should equal direct[i]
        double maxErr = 0.0, sigPeak = 0.0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i) {
                maxErr = std::max(maxErr, static_cast<double>(std::abs(part.getSample(ch, i + P) - direct.getSample(ch, i))));
                sigPeak = std::max(sigPeak, static_cast<double>(std::abs(direct.getSample(ch, i))));
            }
        // The first P samples must be 0 (latency)
        const float leading = peakAbs(part.getReadPointer(0), P);

        std::printf("direct convolution : %8.1f ms (%.2e multiply-adds)\n", directMs, 2.0 * n * static_cast<double>(m));
        std::printf("partitioned        : %8.1f ms (about %.2e complex multiply-adds + %d FFTs)\n", partMs,
                    2.0 * (n + P) / P * conv.getNumPartitions() * (conv.getFftSize() / 2 + 1),
                    2 * 2 * ((n + P) / P));
        std::printf("speedup            : %.1f x\n", directMs / std::max(partMs, 1e-3));
        std::printf("max error          : %.3e (signal peak %.3f -> below %.1f dB)\n", maxErr, sigPeak,
                    gainToDb(maxErr / sigPeak));
        std::printf("peak of the first %d outputs = %.3e (the output must be 0 during the latency)\n\n", P, leading);
        std::printf("Theoretical cost (per output sample): direct = M = %d multiply-adds; partitioned ~ K*(N/2+1)/P = %.0f "
                    "complex multiply-adds ~ %.0f real multiply-adds, plus two N-point FFTs every P samples\n\n",
                    m, static_cast<double>(conv.getNumPartitions()) * (conv.getFftSize() / 2 + 1) / P,
                    4.0 * conv.getNumPartitions() * (conv.getFftSize() / 2 + 1) / P);
    }

    // ---- 2. Long IR: partitioned only, write WAVs ----------------------------------
    {
        const double rt60 = 2.0;
        AudioBuffer ir = ConvolutionReverb::makeSyntheticImpulseResponse(kSampleRate, rt60, 2, 3);
        AudioBuffer dry = makePluckSequence(kSampleRate, 4.0, 0.4f, 2);

        ConvolutionReverb conv;
        conv.prepare(kSampleRate, kBlockSize, 2);
        conv.setImpulseResponse(ir);
        conv.setMix(0.35f);

        std::printf("--- Long IR (RT60 = %.1f s -> %d samples, %d partitions) ---\n", rt60, ir.getNumSamples(),
                    conv.getNumPartitions());
        std::printf("Spectrum memory: IR spectra + FDL = %.2f MiB (%d complex<float> bins per partition)\n",
                    static_cast<double>(conv.getSpectraMemoryBytes()) / (1024.0 * 1024.0), conv.getFftSize() / 2 + 1);
        std::printf("IR energy sum(h^2) = %.4f (the synthetic IR is normalized to 1), IR peak = %.3f\n",
                    static_cast<double>(rms(ir, 0)) * rms(ir, 0) * ir.getNumSamples(), peakAbs(ir));

        auto t0 = std::chrono::steady_clock::now();
        AudioBuffer wet = runPartitioned(conv, dry);
        const double ms = elapsedMs(t0);
        std::printf("Processing %.1f s of audio took %.1f ms (%.0f x real time)\n", dry.getNumSamples() / kSampleRate, ms,
                    dry.getNumSamples() / kSampleRate * 1000.0 / std::max(ms, 1e-3));
        std::printf("dry RMS = %.4f, wet(mix 0.35) RMS = %.4f, peak = %.3f, NaN/Inf = %s\n", rms(dry, 0), rms(wet, 0),
                    peakAbs(wet), hasNanOrInf(wet) ? "yes" : "no");

        writeWav(outputPath("l12_conv_ir.wav"), ir, kSampleRate);
        writeWav(outputPath("l12_conv_dry.wav"), dry, kSampleRate);
        writeWav(outputPath("l12_conv_wet.wav"), wet, kSampleRate);
        std::printf("\nOutput: dawlab_output/l12_conv_ir.wav, l12_conv_dry.wav, l12_conv_wet.wav\n");
    }
    return 0;
}
