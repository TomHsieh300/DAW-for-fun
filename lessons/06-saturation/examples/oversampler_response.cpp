// Example 3: Inside the Oversampler - halfband frequency response, latency, passthrough error, CPU cost.
//
// What it shows:
//   1. Prints the design parameters (tap count, Kaiser beta, group delay) of each halfband FIR stage
//      for 2x / 4x / 8x.
//   2. Computes the frequency response of the stage-1 filter directly from its coefficients (at
//      96 kHz): where the passband stays flat, where the stopband starts, and how many dB it attenuates.
//      These numbers decide "how much aliasing oversampling can block".
//   3. Measures the "upsample -> do nothing -> downsample" delay with an impulse and verifies the number
//      reported by getLatencySamples().
//   4. Measures the passthrough gain error with sine waves (ideally 0 dB) and how far the image is
//      suppressed after upsampling.
//   5. Timing: how long upsampling + downsampling 10 seconds of noise takes at 1x / 2x / 4x / 8x (pure
//      filter cost, no nonlinearity).
//
// How to run: l06_oversampler_response (no arguments)
//
// Output files: none (this program only prints numbers).
//
// What you should see:
//   - Stage 1: passband error < 0.01 dB up to about 20 kHz, attenuation >= 80 dB above 28 kHz.
//   - The measured latency matches getLatencySamples() (2x = 31, 4x = 36, 8x = 38 samples @ 48 kHz).
//   - 1 kHz sine passthrough gain error < 0.01 dB; the 47 kHz image after upsampling is below -75 dB.
//   If the latency does not match, the program returns 1 (ctest then reports a failure).

#include <dawlab/Common.h>
#include <dawlab/dsp/Oversampler.h>

#include <chrono>
#include <complex>
#include <cstdio>
#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

// Frequency response of an FIR: H(f) = sum_n h[n] e^{-j 2 pi f n / rate}
double firMagnitudeDb(const std::vector<double>& h, double freq, double rate) {
    std::complex<double> acc(0.0, 0.0);
    for (size_t n = 0; n < h.size(); ++n) {
        const double w = -dawlab::kTwoPi * freq * static_cast<double>(n) / rate;
        acc += h[n] * std::complex<double>(std::cos(w), std::sin(w));
    }
    return dawlab::gainToDb(std::abs(acc));
}

// "Upsample -> do nothing -> downsample" over a whole buffer (simulates an audio callback, 512 samples at a time)
void roundTrip(dawlab::dsp::Oversampler& os, dawlab::AudioBuffer& buffer) {
    dawlab::AudioBlock whole(buffer);
    for (int start = 0; start < buffer.getNumSamples(); start += kBlockSize) {
        const int len = std::min(kBlockSize, buffer.getNumSamples() - start);
        dawlab::AudioBlock block = whole.getSubBlock(start, len);
        os.upsample(block);
        os.downsample(block);
    }
}

} // namespace

int main() {
    using dawlab::dsp::HalfbandStage;
    using dawlab::dsp::Oversampler;
    bool ok = true;

    // ---- 1. Design parameters ---------------------------------------------------------------
    std::printf("Halfband stage designs (Kaiser beta = %.1f):\n", Oversampler::kKaiserBeta);
    std::printf("%-6s %-12s %-10s %-12s %-16s %-14s\n", "Stage", "Factor", "Odd taps", "FIR length", "group delay", "working rate");
    const int oddTaps[] = {Oversampler::kOddTapsStage1, Oversampler::kOddTapsStage2, Oversampler::kOddTapsStage3};
    for (int stage = 1; stage <= 3; ++stage) {
        const int K = oddTaps[stage - 1] / 2;
        std::printf("%-6d x%-11d %-10d %-12d %-3d samples      %.0f kHz\n", stage, 1 << stage, 2 * K, 4 * K - 1,
                    2 * K - 1, kSampleRate * (1 << stage) / 1000.0);
    }

    // ---- 2. Stage-1 frequency response (at 96 kHz) -------------------------------------------
    {
        const std::vector<double> h = HalfbandStage::designHalfband(Oversampler::kOddTapsStage1, Oversampler::kKaiserBeta);
        double sum = 0.0;
        for (double v : h)
            sum += v;
        std::printf("\nStage-1 halfband: %zu taps, sum(h) = %.6f (DC gain %.4f dB)\n", h.size(), sum, dawlab::gainToDb(sum));
        std::printf("Coefficients near the centre h[0..7]: ");
        const size_t centre = h.size() / 2;
        for (size_t i = 0; i < 8; ++i)
            std::printf("%s%.5f", i ? ", " : "", h[centre + i]);
        std::printf("\n(The even-indexed taps are almost all 0 -- this is where the halfband saves half of the multiplications)\n\n");

        const double rate = kSampleRate * 2.0;
        std::printf("%-10s %12s\n", "freq (Hz)", "|H| (dB)");
        for (double f : {1000.0, 10000.0, 16000.0, 18000.0, 19000.0, 20000.0, 21000.0, 22000.0, 23000.0, 24000.0,
                         25000.0, 26000.0, 27000.0, 28000.0, 29000.0, 30000.0, 36000.0, 40000.0, 47000.0}) {
            std::printf("%-10.0f %12.3f\n", f, firMagnitudeDb(h, f, rate));
        }
        // Scan for the passband edge (last frequency with |error| < 0.01 dB) and the stopband start
        // (first frequency below -80 dB)
        double passEdge = 0.0, stopEdge = 0.0;
        for (double f = 0.0; f <= rate * 0.5; f += 100.0) {
            const double db = firMagnitudeDb(h, f, rate);
            if (std::abs(db) < 0.01)
                passEdge = f;
            if (stopEdge == 0.0 && f > rate * 0.25 && db < -80.0)
                stopEdge = f;
        }
        double worstStop = -200.0;
        for (double f = stopEdge; f <= rate * 0.5; f += 50.0)
            worstStop = std::max(worstStop, firMagnitudeDb(h, f, rate));
        std::printf("\npassband (|error| < 0.01 dB) extends to %.1f kHz; stopband (< -80 dB) starts at %.1f kHz, "
                    "worst case afterwards %.1f dB\n",
                    passEdge / 1000.0, stopEdge / 1000.0, worstStop);
        std::printf("Exactly -6.02 dB at 24 kHz (the old Nyquist): %.3f dB\n", firMagnitudeDb(h, 24000.0, rate));
    }

    // ---- 3. Latency: impulse round trip ------------------------------------------------------
    std::printf("\nLatency (impulse upsampled -> downsampled, in 48 kHz samples):\n");
    std::printf("%-8s %-12s %-12s %-10s\n", "Factor", "measured", "getLatency", "match?");
    for (int log2 = 0; log2 <= Oversampler::kMaxFactorLog2; ++log2) {
        Oversampler os(log2);
        os.prepare(kSampleRate, kBlockSize, 1);
        const int position = 100;
        dawlab::AudioBuffer buffer = dawlab::makeImpulse(4096, 1, position, 1.0f);
        roundTrip(os, buffer);
        int argmax = 0;
        float best = 0.0f;
        const float* d = buffer.getReadPointer(0);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (std::abs(d[i]) > best) {
                best = std::abs(d[i]);
                argmax = i;
            }
        const int measured = argmax - position;
        const bool same = measured == os.getLatencySamples();
        ok = ok && same;
        std::printf("%-8d %-12d %-12d %-10s (peak %.4f)\n", os.getFactor(), measured, os.getLatencySamples(),
                    same ? "yes" : "NO", best);
    }

    // ---- 4. Passthrough error + image suppression -------------------------------------------
    std::printf("\nPassthrough (sine 0.5, gain after upsample -> downsample; ideal 0 dB):\n");
    std::printf("%-10s %10s %10s %10s\n", "freq (Hz)", "2x", "4x", "8x");
    for (double f : {1000.0, 10000.0, 18000.0, 20000.0, 22000.0, 23000.0}) {
        std::printf("%-10.0f", f);
        for (int log2 = 1; log2 <= 3; ++log2) {
            Oversampler os(log2);
            os.prepare(kSampleRate, kBlockSize, 1);
            dawlab::AudioBuffer buffer = dawlab::makeSine(f, kSampleRate, 1.0, 0.5f, 1);
            const dawlab::AudioBuffer input = buffer;
            roundTrip(os, buffer);
            const int skip = 4096; // Skip the initial transient
            const int n = buffer.getNumSamples() - skip;
            const double a = dawlab::goertzelAmplitude(input.getReadPointer(0) + skip, n, f, kSampleRate);
            const double b = dawlab::goertzelAmplitude(buffer.getReadPointer(0) + skip, n, f, kSampleRate);
            const double gainDb = dawlab::gainToDb(b / a);
            std::printf(" %+10.4f", gainDb);
            if (f <= 18000.0)
                ok = ok && std::abs(gainDb) < 0.02;
        }
        std::printf("\n");
    }
    {
        // After upsampling 1 kHz by 2x, look at 1 kHz and its image at 96 kHz
        Oversampler os(1);
        os.prepare(kSampleRate, kBlockSize, 1);
        dawlab::AudioBuffer buffer = dawlab::makeSine(1000.0, kSampleRate, 1.0, 0.5f, 1);
        std::vector<float> up;
        dawlab::AudioBlock whole(buffer);
        for (int start = 0; start < buffer.getNumSamples(); start += kBlockSize) {
            const int len = std::min(kBlockSize, buffer.getNumSamples() - start);
            dawlab::AudioBlock u = os.upsample(whole.getSubBlock(start, len));
            up.insert(up.end(), u.getChannelPointer(0), u.getChannelPointer(0) + u.getNumSamples());
        }
        const double rate = kSampleRate * 2.0;
        const int skip = 8192;
        const int n = static_cast<int>(up.size()) - skip;
        // Zero-stuffing creates images around integer multiples of 48 kHz: 48 +/- 1 -> 47 kHz and 49 kHz
        // (49 kHz is the same as 47 kHz at a 96 kHz rate)
        const double a1 = dawlab::goertzelAmplitude(up.data() + skip, n, 1000.0, rate);
        const double a47 = dawlab::goertzelAmplitude(up.data() + skip, n, 47000.0, rate);
        std::printf("\n1 kHz upsampled 2x (96 kHz): 1 kHz = %.4f (input 0.5), image at 47 kHz = %.1f dB\n", a1,
                    dawlab::gainToDb(a47 / a1));
        std::printf("(With plain zero-stuffing the image would be as large as the original = 0 dB; the halfband lowpass suppresses it)\n");
        ok = ok && dawlab::gainToDb(a47 / a1) < -75.0;
    }

    // ---- 5. CPU cost -------------------------------------------------------------------------
    std::printf("\nCPU: 10 s of stereo noise, upsample + downsample (no nonlinearity):\n");
    std::printf("%-8s %12s %12s\n", "Factor", "time (ms)", "vs 2x");
    double base = 0.0;
    for (int log2 = 0; log2 <= Oversampler::kMaxFactorLog2; ++log2) {
        Oversampler os(log2);
        os.prepare(kSampleRate, kBlockSize, 2);
        dawlab::AudioBuffer buffer = dawlab::makeWhiteNoise(kSampleRate, 10.0, 0.5f, 2);
        const auto t0 = std::chrono::steady_clock::now();
        roundTrip(os, buffer);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (log2 == 1)
            base = ms;
        std::printf("%-8d %12.1f %12s\n", os.getFactor(), ms, log2 >= 1 ? std::to_string(ms / base).substr(0, 4).c_str() : "-");
        ok = ok && !dawlab::hasNanOrInf(buffer);
    }
#ifdef NDEBUG
    std::printf("(This is a Release build; absolute times vary by machine, look at the relative factors)\n");
#else
    std::printf("(This is a Debug build; absolute times are meaningless, look at the relative factors; Release is much faster)\n");
#endif

    std::printf("\n%s\n", ok ? "Latency matches getLatencySamples(), passthrough error < 0.02 dB, image < -75 dB."
                             : "[FAIL] A latency / passthrough / image check did not match!");
    return ok ? 0 : 1;
}
