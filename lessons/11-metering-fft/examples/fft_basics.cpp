// Example 1: FFT correctness and performance
//
// What it demonstrates:
//   1. The hand-written radix-2 FFT is compared against a naive DFT computed straight from the definition
//      (N = 16 .. 4096); the error should stay below 1e-4
//   2. inverse(forward(x)) restores the original signal; forwardReal / inverseReal agree with the complex versions
//   3. A single bin's amplitude is cross-checked with Goertzel (Analysis.h)
//   4. Timing: FFT vs naive DFT, N = 256 .. 16384 (the naive DFT only runs up to 4096, otherwise it is too slow)
//   5. The pluck signal is cut into 2048-sample blocks, each block goes through forwardReal -> inverseReal,
//      and the result is written to a WAV file (round trip)
//   Every item is verified numerically with DAWLAB_CHECK, and [PASS] / [FAIL] is printed at the end (ctest runs
//   this program too).
//
// How to run: after building, run bin/Debug/l11_fft_basics.exe from the build directory (no arguments needed)
// Output file: dawlab_output/l11_fft_roundtrip.wav -- it should sound exactly like the original pluck
//
// Reminder: timings in the Debug configuration only show the trend (O(N log N) vs O(N^2)); the absolute
// values are much slower than in Release.

#include <dawlab/Common.h>
#include <dawlab/dsp/Fft.h>

#include <chrono>
#include <complex>
#include <cstdio>
#include <vector>

using namespace dawlab;
using dawlab::dsp::Fft;

namespace {

using Clock = std::chrono::steady_clock;

double millisecondsSince(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

// Random complex signal (deterministic)
std::vector<std::complex<float>> makeRandomComplex(int n, uint32_t seed) {
    NoiseGenerator noise(seed);
    std::vector<std::complex<float>> v(static_cast<size_t>(n));
    for (auto& c : v)
        c = std::complex<float>(noise.next(), noise.next());
    return v;
}

float maxAbsError(const std::vector<std::complex<float>>& a, const std::vector<std::complex<float>>& b) {
    float e = 0.0f;
    for (size_t i = 0; i < a.size(); ++i)
        e = std::max(e, std::abs(a[i] - b[i]));
    return e;
}

} // namespace

int main() {
    constexpr double sr = 48000.0;

    // ---- 1. Compare against the naive DFT -------------------------------------------------
    std::printf("== FFT vs naive DFT (max absolute error, random input with |x| <= 1) ==\n");
    std::printf("%8s %14s %14s\n", "N", "forward err", "inverse err");
    for (int order = 4; order <= 12; ++order) {
        const int n = 1 << order;
        Fft fft(order);
        const auto x = makeRandomComplex(n, 1000u + static_cast<uint32_t>(order));
        std::vector<std::complex<float>> reference(static_cast<size_t>(n)), got = x;
        dsp::naiveDft(x.data(), reference.data(), n);
        fft.forward(got.data());
        const float fwdErr = maxAbsError(got, reference);

        // Running the IFFT on the FFT output should give x back; the naive IDFT does the same
        std::vector<std::complex<float>> back = got;
        fft.inverse(back.data());
        const float invErr = maxAbsError(back, x);
        // The forward output has magnitude about √N and float has a relative error of about 1e-7, so the absolute
        // error grows slowly with N
        DAWLAB_CHECK(fwdErr < 1e-4f);
        DAWLAB_CHECK(invErr < 1e-5f);
        std::printf("%8d %14.3e %14.3e\n", n, fwdErr, invErr);
    }

    // ---- 2. forwardReal / inverseReal agree with the complex version ----------------------
    {
        constexpr int order = 10, n = 1 << order;
        Fft fft(order);
        AudioBuffer sine = makeSine(1000.0, sr, static_cast<double>(n) / sr, 0.5f, 1);
        std::vector<std::complex<float>> full(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i)
            full[static_cast<size_t>(i)] = std::complex<float>(sine.getSample(0, i), 0.0f);
        fft.forward(full.data());

        std::vector<std::complex<float>> half(static_cast<size_t>(fft.getNumRealBins()));
        fft.forwardReal(sine.getReadPointer(0), half.data());
        float err = 0.0f;
        for (int k = 0; k < fft.getNumRealBins(); ++k)
            err = std::max(err, std::abs(half[static_cast<size_t>(k)] - full[static_cast<size_t>(k)]));
        // Conjugate symmetry: X[N-k] == conj(X[k])
        float symErr = 0.0f;
        for (int k = 1; k < n / 2; ++k)
            symErr = std::max(symErr, std::abs(full[static_cast<size_t>(n - k)] - std::conj(full[static_cast<size_t>(k)])));

        std::vector<float> restored(static_cast<size_t>(n));
        fft.inverseReal(half.data(), restored.data());
        float rtErr = 0.0f;
        for (int i = 0; i < n; ++i)
            rtErr = std::max(rtErr, std::abs(restored[static_cast<size_t>(i)] - sine.getSample(0, i)));
        std::printf("\n== Real FFT (N = %d) ==\n", n);
        std::printf("forwardReal vs forward error = %.3e, conjugate-symmetry error = %.3e, inverseReal round-trip error = %.3e\n",
                    err, symErr, rtErr);
        DAWLAB_CHECK(err < 1e-6f);
        DAWLAB_CHECK(symErr < 1e-4f);
        DAWLAB_CHECK(rtErr < 1e-5f);

        // ---- 3. Cross-check with Goertzel: which bin does the 1 kHz sine land in, and what amplitude? ----
        // Bin width = fs/N = 46.875 Hz; 1000 Hz sits at bin 21.33, i.e. not on a bin centre, so it leaks
        int peakBin = 0;
        for (int k = 1; k < fft.getNumRealBins(); ++k)
            if (std::abs(half[static_cast<size_t>(k)]) > std::abs(half[static_cast<size_t>(peakBin)]))
                peakBin = k;
        const double binHz = sr / n;
        const double ampFromFft = 2.0 * std::abs(half[static_cast<size_t>(peakBin)]) / n; // rectangular window, CG = 1
        const double ampGoertzel = goertzelAmplitude(sine.getReadPointer(0), n, 1000.0, sr);
        std::printf("bin width = %.3f Hz, largest bin = %d (%.1f Hz), FFT amplitude estimate = %.4f, Goertzel = %.4f (true value 0.5)\n",
                    binHz, peakBin, peakBin * binHz, ampFromFft, ampGoertzel);
        std::printf("-> 1000 Hz is not on a bin centre (1000 / %.3f = %.2f); the energy leaks into the neighbouring bins, so the single-bin FFT reading is low.\n",
                    binHz, 1000.0 / binHz);
        // Theory: rectangular window, offset from the bin centre by δ = 1/3 bin -> amplitude times sinc(δ) = sin(π/3)/(π/3) = 0.827
        const double sincDelta = std::sin(kPi / 3.0) / (kPi / 3.0);
        std::printf("Theoretical value (rectangular scalloping): 0.5 x sinc(1/3) = %.4f\n", 0.5 * sincDelta);
        DAWLAB_CHECK(peakBin == 21);
        DAWLAB_CHECK_NEAR(ampGoertzel, 0.5, 1e-3);
        DAWLAB_CHECK_NEAR(ampFromFft, 0.5 * sincDelta, 0.005);
    }

    // ---- 4. Timing --------------------------------------------------------------------------
    std::printf("\n== Timing (averaged over repetitions for each N; in Debug only the trend matters) ==\n");
    std::printf("%8s %14s %14s %10s\n", "N", "FFT (ms)", "naive DFT (ms)", "ratio");
    double ratioAt4096 = 0.0;
    for (int order = 8; order <= 14; ++order) {
        const int n = 1 << order;
        Fft fft(order);
        auto x = makeRandomComplex(n, 7u);
        std::vector<std::complex<float>> work = x;

        const int reps = std::max(1, 20000 / n);
        auto t0 = Clock::now();
        for (int r = 0; r < reps; ++r) {
            work = x;
            fft.forward(work.data());
        }
        const double fftMs = millisecondsSince(t0) / reps;

        double naiveMs = -1.0;
        if (n <= 4096) {
            std::vector<std::complex<float>> out(static_cast<size_t>(n));
            t0 = Clock::now();
            dsp::naiveDft(x.data(), out.data(), n);
            naiveMs = millisecondsSince(t0);
            // Use the result: an optimizer may drop a computation nobody reads (Apple Clang -O3 removed the
            // whole naive DFT here, timing it at 0 ms). Comparing it with the FFT also checks both once more.
            float diff = 0.0f;
            for (int k = 0; k < n; ++k)
                diff = std::max(diff, std::abs(out[static_cast<size_t>(k)] - work[static_cast<size_t>(k)]));
            DAWLAB_CHECK(diff < 1e-3f);
        }
        if (n == 4096)
            ratioAt4096 = naiveMs / std::max(fftMs, 1e-9);
        if (naiveMs >= 0.0)
            std::printf("%8d %14.4f %14.3f %10.0f\n", n, fftMs, naiveMs, naiveMs / std::max(fftMs, 1e-9));
        else
            std::printf("%8d %14.4f %14s %10s\n", n, fftMs, "(skipped)", "-");
    }
    std::printf("Theory: the naive DFT is O(N^2) and the FFT is O(N log2 N); at N = 4096, N / log2(N) = %.0f times.\n", 4096.0 / 12.0);
    // Timings vary with the machine and its load, so only the order of magnitude is checked: at N = 4096 the FFT
    // must be at least 20 times faster (typically several hundred times in practice)
    DAWLAB_CHECK(ratioAt4096 > 20.0);

    // ---- 5. Round trip block by block, then write a WAV ------------------------------------
    {
        constexpr int order = 11, n = 1 << order; // 2048
        Fft fft(order);
        AudioBuffer plucks = makePluckSequence(sr, 3.0, 0.4f, 2);
        AudioBuffer out(plucks.getNumChannels(), plucks.getNumSamples());
        std::vector<std::complex<float>> spec(static_cast<size_t>(fft.getNumRealBins()));
        std::vector<float> block(static_cast<size_t>(n));
        float maxErr = 0.0f;
        for (int ch = 0; ch < plucks.getNumChannels(); ++ch) {
            for (int start = 0; start + n <= plucks.getNumSamples(); start += n) {
                fft.forwardReal(plucks.getReadPointer(ch) + start, spec.data());
                fft.inverseReal(spec.data(), block.data());
                for (int i = 0; i < n; ++i) {
                    out.setSample(ch, start + i, block[static_cast<size_t>(i)]);
                    maxErr = std::max(maxErr, std::abs(block[static_cast<size_t>(i)] - plucks.getSample(ch, start + i)));
                }
            }
        }
        writeWav(outputPath("l11_fft_roundtrip.wav"), out, sr);
        std::printf("\nRound trip (2048-point blocks) max error = %.3e -> dawlab_output/l11_fft_roundtrip.wav\n", maxErr);
        DAWLAB_CHECK(maxErr < 1e-5f);
    }
    return dawlab::test::summary("l11_fft_basics");
}
