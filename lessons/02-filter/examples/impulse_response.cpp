// l02_impulse_response: prints the impulse response (IR) of several filters.
//
// What it shows:
//   - Feed a unit impulse δ[n] (sample 0 = 1, all others 0) into a filter and the output is h[n].
//     h[n] fully describes an LTI (linear time-invariant) system: the output for any input is the
//     convolution of x with h.
//   - FIR (moving average): the IR has finite length and is 0 after N samples.
//   - IIR (one-pole): the IR is infinitely long, h[n] = (1 - a) a^n, decaying exponentially --
//     compared term by term with the theoretical values.
//   - Biquad lowpass: the higher the Q, the longer the IR rings (oscillates); at Q = 0.707 it
//     barely rings.
//   - Compute the "frequency response" directly from the IR: H(e^{jω}) = Σ h[n] e^{-jωn} (DTFT),
//     and compare with magnitudeDb().
//
// How to run: run with no arguments. It writes WAV files:
//   dawlab_output/l02_ir_biquad_q0707.wav, l02_ir_biquad_q8.wav (amplified IRs; you can hear the "ping" of Q=8)
#include <dawlab/Common.h>
#include <dawlab/dsp/Biquad.h>
#include <dawlab/dsp/OnePole.h>

#include <complex>
#include <cstdio>
#include <vector>

using namespace dawlab;
using dawlab::dsp::Biquad;
using dawlab::dsp::BiquadCoefficients;
using dawlab::dsp::OnePole;

constexpr double kSampleRate = 48000.0;

// Feed an impulse into any system in processSample form and collect the first n outputs
template <typename Fn>
static std::vector<double> impulseResponse(Fn&& processSample, int n) {
    std::vector<double> h(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        h[static_cast<size_t>(i)] = processSample(i == 0 ? 1.0f : 0.0f);
    return h;
}

// DTFT: H(e^{jω}) = Σ h[n] e^{-jωn}
static double dtftDb(const std::vector<double>& h, double f) {
    std::complex<double> sum = 0.0;
    for (size_t n = 0; n < h.size(); ++n)
        sum += h[n] * std::polar(1.0, -kTwoPi * f * static_cast<double>(n) / kSampleRate);
    return gainToDb(std::abs(sum));
}

// Ringing length: the position where |h[n]| last exceeds "1% of the IR peak" (-40 dB)
static int ringLength(const std::vector<double>& h) {
    double peak = 0.0;
    for (double v : h)
        peak = std::max(peak, std::abs(v));
    int last = 0;
    for (size_t n = 0; n < h.size(); ++n)
        if (std::abs(h[n]) > 0.01 * peak)
            last = static_cast<int>(n);
    return last;
}

static void printRow(const char* name, const std::vector<double>& h, int count) {
    std::printf("    %-26s", name);
    for (int i = 0; i < count; ++i)
        std::printf(" %7.4f", h[static_cast<size_t>(i)]);
    std::printf("\n");
}

int main() {
    std::printf("=== Chapter 02: impulse response (fs = %.0f Hz) ===\n\n", kSampleRate);
    constexpr int kShow = 12;
    std::printf("    %-26s", "n =");
    for (int i = 0; i < kShow; ++i)
        std::printf(" %7d", i);
    std::printf("\n");

    // ---- FIR: 4-point moving average -------------------------------------------------
    // y[n] = (x[n] + x[n-1] + x[n-2] + x[n-3]) / 4  ->  h = {0.25, 0.25, 0.25, 0.25, 0, 0, ...}
    {
        float d[3] = {0.0f, 0.0f, 0.0f};
        auto ma = [&](float x) {
            const float y = 0.25f * (x + d[0] + d[1] + d[2]);
            d[2] = d[1];
            d[1] = d[0];
            d[0] = x;
            return y;
        };
        const auto h = impulseResponse(ma, 64);
        printRow("FIR moving avg N=4", h, kShow);
        DAWLAB_CHECK_NEAR(h[3], 0.25, 1e-9);
        DAWLAB_CHECK_NEAR(h[4], 0.0, 1e-9); // all zeros after the N-th sample: finite length
        // Frequency response: a complete null at fs/N = 12 kHz, 0 dB at DC
        std::printf("      -> DTFT: DC = %.2f dB, 12 kHz = %.1f dB (the first null of an N-point average is at fs/N)\n",
                    dtftDb(h, 0.0), dtftDb(h, 12000.0));
        DAWLAB_CHECK(dtftDb(h, 12000.0) < -90.0);
    }

    // ---- IIR: one-pole lowpass ------------------------------------------------------
    {
        OnePole lp;
        lp.prepare(kSampleRate, 512, 1);
        lp.setCutoff(1000.0f);
        const double a = lp.getCoefficient();
        const auto h = impulseResponse([&](float x) { return lp.processSample(0, x); }, 4096);
        printRow("one-pole LP fc=1k", h, kShow);
        // Theory: h[n] = (1 - a) a^n
        double maxErr = 0.0;
        for (int n = 0; n < 64; ++n)
            maxErr = std::max(maxErr, std::abs(h[static_cast<size_t>(n)] - (1.0 - a) * std::pow(a, n)));
        std::printf("      -> a = %.6f, max error between h[n] and (1-a)a^n = %.2e; h[100] = %.2e (never truly reaches 0)\n", a,
                    maxErr, h[100]);
        DAWLAB_CHECK(maxErr < 1e-6);
        DAWLAB_CHECK(h[100] > 0.0);
        // The sum of the IR = DC gain = 1 (a lowpass passes DC completely)
        double sum = 0.0;
        for (double v : h)
            sum += v;
        DAWLAB_CHECK_NEAR(sum, 1.0, 1e-6);
        DAWLAB_CHECK_NEAR(dtftDb(h, 1000.0), -3.0, 0.1);
    }

    // ---- Biquad lowpass: the effect of Q on ringing ----------------------------------
    Biquad bq;
    bq.prepare(kSampleRate, 512, 1);
    std::vector<double> hQ0707, hQ8;
    for (double q : {0.7071, 2.0, 8.0}) {
        bq.setCoefficients(BiquadCoefficients::makeLowpass(kSampleRate, 1000.0, q));
        bq.reset();
        const auto h = impulseResponse([&](float x) { return bq.processSample(0, x); }, 48000);
        char name[32];
        std::snprintf(name, sizeof(name), "biquad LP fc=1k Q=%g", q);
        printRow(name, h, kShow);
        // "The index of the last sample where |h[n]| exceeds 1% of the peak" = the ringing length
        const int last = ringLength(h);
        std::printf("      -> time for |h[n]| to drop below 1%% of the peak: %d samples = %.1f ms; DTFT(1 kHz) = %.2f dB\n", last,
                    samplesToMs(last, kSampleRate), dtftDb(h, 1000.0));
        if (q < 1.0)
            hQ0707 = h;
        if (q > 7.0)
            hQ8 = h;
    }
    // Q=0.707 barely rings (decays quickly); Q=8 rings far longer
    {
        DAWLAB_CHECK(ringLength(hQ8) > 5 * ringLength(hQ0707));
        // The gain of Q = 8 at the cutoff ~ 20 log10(Q) = 18.06 dB
        DAWLAB_CHECK_NEAR(dtftDb(hQ8, 1000.0), 20.0 * std::log10(8.0), 0.1);
    }

    // Write WAVs (scaled by 0.5, repeated 4 times, so you can hear a "click" vs. a "ping")
    auto writeIr = [&](const std::vector<double>& h, const char* file) {
        AudioBuffer out(1, 48000);
        for (int rep = 0; rep < 4; ++rep)
            for (int n = 0; n < 12000 && n < static_cast<int>(h.size()); ++n)
                out.setSample(0, rep * 12000 + n, static_cast<float>(0.5 * h[static_cast<size_t>(n)]));
        writeWav(outputPath(file), out, kSampleRate);
    };
    writeIr(hQ0707, "l02_ir_biquad_q0707.wav");
    writeIr(hQ8, "l02_ir_biquad_q8.wav");
    std::printf("\nWrote dawlab_output/l02_ir_biquad_q0707.wav (a short click) and l02_ir_biquad_q8.wav (a 1 kHz ping)\n\n");

    return dawlab::test::summary("l02_impulse_response");
}
