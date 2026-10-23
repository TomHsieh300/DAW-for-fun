// Example 1: comparing interpolation for fractional delay (linear vs cubic Hermite vs Lagrange)
//
// What it demonstrates:
//   1. The power-of-two size and mask of a circular buffer.
//   2. Push an impulse into a FractionalDelayLine and read it back at delay = 2.5 samples to
//      see the "kernel" of each interpolator directly (the coefficients one sample is spread
//      over by each method).
//   3. Measure the frequency response of the three interpolators at delay = 10.5 samples
//      (Δ = 0.5, the worst case) and compare with theory: linear has |H| = |cos(πf/fs)|, which
//      already drops 3 dB at 12 kHz and 11.7 dB at 20 kHz; cubic has
//      |H| = |9/8·cos(ω/2) − 1/8·cos(3ω/2)|, which drops only 1.07 dB at 12 kHz.
//
// How to run: no arguments needed, just run l09_interpolation_compare.
// Output files (dawlab_output/):
//   l09_interp_dry.wav     the original white noise
//   l09_interp_linear.wav  through linear interpolation at delay = 10.5 samples (highs clearly darker)
//   l09_interp_cubic.wav   through cubic interpolation at delay = 10.5 samples (hardly any audible difference)
// What you should hear: the linear version sounds as if a thin cloth were laid over it; the
//             cubic version is almost identical to the original.

#include <dawlab/Common.h>
#include <dawlab/dsp/FractionalDelayLine.h>

#include <cmath>
#include <cstdio>
#include <vector>

using dawlab::dsp::FractionalDelayLine;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

// Theoretical gain (dB) of linear interpolation at Δ = 0.5: |H| = |cos(ω/2)| = |cos(πf/fs)|
double linearTheoryDb(double f) {
    return dawlab::gainToDb(std::abs(std::cos(dawlab::kPi * f / kSampleRate)));
}

// Cubic (Catmull-Rom = 3rd-order Lagrange at Δ = 0.5, where the coefficients are
// -1/16, 9/16, 9/16, -1/16):
// |H| = |9/8·cos(ω/2) − 1/8·cos(3ω/2)|
double cubicTheoryDb(double f) {
    const double w = dawlab::kTwoPi * f / kSampleRate;
    return dawlab::gainToDb(std::abs(9.0 / 8.0 * std::cos(w / 2.0) - 1.0 / 8.0 * std::cos(3.0 * w / 2.0)));
}

enum class Method { Linear, Cubic, Lagrange };

float readWith(const FractionalDelayLine& line, Method m, float delay) {
    switch (m) {
    case Method::Linear: return line.readLinear(0, delay);
    case Method::Cubic: return line.readCubic(0, delay);
    default: return line.readLagrange(0, delay);
    }
}

double measure(Method m, float delay, double freq) {
    FractionalDelayLine line;
    line.prepare(64, 1);
    return dawlab::measureGainDb(
        [&](float x) {
            line.push(0, x);
            return readWith(line, m, delay);
        },
        freq, kSampleRate);
}

// Process the whole buffer block by block (block size 512) to mimic an audio callback
void renderDelayed(const dawlab::AudioBuffer& in, dawlab::AudioBuffer& out, Method m, float delay) {
    FractionalDelayLine line;
    line.prepare(64, in.getNumChannels());
    for (int start = 0; start < in.getNumSamples(); start += kBlockSize) {
        const int n = std::min(kBlockSize, in.getNumSamples() - start);
        for (int ch = 0; ch < in.getNumChannels(); ++ch) {
            const float* src = in.getReadPointer(ch) + start;
            float* dst = out.getWritePointer(ch) + start;
            for (int i = 0; i < n; ++i) {
                line.push(ch, src[i]);
                dst[i] = m == Method::Linear ? line.readLinear(ch, delay) : line.readCubic(ch, delay);
            }
        }
    }
}

} // namespace

int main() {
    std::printf("=== Chapter 09, Example 1: fractional delay interpolation comparison ===\n\n");

    // ---- 1. Circular buffer size -------------------------------------------
    {
        FractionalDelayLine line;
        line.prepare(1000, 2);
        std::printf("prepare(maxDelay = 1000) -> buffer size = %d (a power of two, mask = 0x%X)\n\n",
                    line.getBufferSize(), line.getBufferSize() - 1);
    }

    // ---- 2. Interpolation kernels: an impulse through delay = 2.5 ----------
    {
        std::printf("Output of an impulse through a delay of 2.5 samples (one row per sample time n):\n");
        std::printf("   n   linear    cubic   lagrange\n");
        FractionalDelayLine lin, cub, lag;
        lin.prepare(16, 1);
        cub.prepare(16, 1);
        lag.prepare(16, 1);
        for (int n = 0; n < 7; ++n) {
            const float x = n == 0 ? 1.0f : 0.0f;
            lin.push(0, x);
            cub.push(0, x);
            lag.push(0, x);
            std::printf("  %2d  %+7.4f  %+7.4f  %+7.4f\n", n, lin.readLinear(0, 2.5f), cub.readCubic(0, 2.5f),
                        lag.readLagrange(0, 2.5f));
        }
        std::printf("  -> linear uses just two coefficients (0.5, 0.5); cubic and Lagrange are identical at Δ = 0.5:\n");
        std::printf("     (-1/16, 9/16, 9/16, -1/16) = (-0.0625, 0.5625, 0.5625, -0.0625)\n\n");
    }

    // ---- 3. Frequency response measurement (delay = 10.5) -------------------
    {
        const float delay = 10.5f;
        const double freqs[] = {100, 500, 1000, 2000, 4000, 6000, 8000, 10000, 12000, 15000, 18000, 20000};
        std::printf("Frequency response (dB) at a delay of %.1f samples, fs = %.0f Hz:\n", delay, kSampleRate);
        std::printf("   freq(Hz)  linear  theory   cubic  theory  lagrange\n");
        for (double f : freqs) {
            const double gl = measure(Method::Linear, delay, f);
            const double gc = measure(Method::Cubic, delay, f);
            const double gg = measure(Method::Lagrange, delay, f);
            std::printf("  %8.0f  %+6.2f  %+6.2f  %+6.2f  %+6.2f  %+6.2f\n", f, gl, linearTheoryDb(f), gc,
                        cubicTheoryDb(f), gg);
        }
        std::printf("  -> linear drops 3 dB at fs/4 = 12 kHz and 11.7 dB at 20 kHz; cubic drops only 1.07 dB at 12 kHz.\n");
        std::printf("     Both are lowpass filters: that is why chorus / flanger (chapter 10) should use cubic interpolation.\n\n");
    }

    // ---- 4. Write WAVs to listen to -------------------------------------------
    {
        dawlab::AudioBuffer noise = dawlab::makeWhiteNoise(kSampleRate, 3.0, 0.25f);
        dawlab::AudioBuffer linear(noise.getNumChannels(), noise.getNumSamples());
        dawlab::AudioBuffer cubic(noise.getNumChannels(), noise.getNumSamples());
        renderDelayed(noise, linear, Method::Linear, 10.5f);
        renderDelayed(noise, cubic, Method::Cubic, 10.5f);
        dawlab::writeWav(dawlab::outputPath("l09_interp_dry.wav"), noise, kSampleRate);
        dawlab::writeWav(dawlab::outputPath("l09_interp_linear.wav"), linear, kSampleRate);
        dawlab::writeWav(dawlab::outputPath("l09_interp_cubic.wav"), cubic, kSampleRate);
        std::printf("White-noise RMS: dry %.4f, linear %.4f, cubic %.4f (what linear loses is high-frequency energy)\n",
                    dawlab::rms(noise, 0), dawlab::rms(linear, 0), dawlab::rms(cubic, 0));
        std::printf("Wrote dawlab_output/l09_interp_dry.wav, l09_interp_linear.wav, l09_interp_cubic.wav\n");
    }
    return 0;
}
