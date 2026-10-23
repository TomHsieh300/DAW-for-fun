// Exercise 7 solution (numerical check of the calculation + concept problems): comb filter peaks and
// notches, negative feedback, "tuning" with a comb
//
// The answers and derivations are in solutions/README.md; this program measures every number once:
//   1. A feedback comb with D = 240 samples (5 ms @ 48 kHz) and g = 0.8:
//        peaks at k·fs/D = k·200 Hz, |H| = 1/(1−g) = 5 -> +13.98 dB;
//        notches at (k+½)·200 Hz, |H| = 1/(1+g) = 0.556 -> −5.11 dB.
//   2. g = −0.8: peaks and notches swap places -- the peaks move to (k+½)·fs/D = 100, 300, 500 Hz...
//      (a pitch "half as high" + only odd harmonics).
//   3. To make the comb's pitch A4 = 440 Hz (fs = 44.1 kHz): D = 44100/440 = 100.227 samples, not an integer.
//        D rounded to the integer 100 -> peak at 441.0 Hz (+3.9 cents); 101 -> 436.63 Hz (−13.3 cents);
//        cents are computed from the theoretical frequency fs/D; the peak found by the sweep has only
//        0.1 Hz resolution (436.6 Hz) and is used just to verify the peak position.
//        With a cubic fractional delay -> the peak lands on 440 Hz.
//   4. Concept: the impulse response of a feedback comb is g^k once every D samples -> it does not decay when |g| >= 1.
//   5. The numbers for part 2 of exercise 2: a one-pole LP in the loop (g = 0.99) is still stable;
//      with a Q = 4 biquad LP (about +12 dB at the cutoff), even g = 0.5 blows up.
//
// Run: l09_ex7_solution (no arguments needed, no WAV output).

#include <dawlab/Common.h>
#include <dawlab/dsp/Biquad.h>
#include <dawlab/dsp/FractionalDelayLine.h>
#include <dawlab/dsp/OnePole.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

using dawlab::dsp::FractionalDelayLine;

namespace {

// y[n] = x[n] + g·y[n − D], where D may be fractional (cubic interpolation).
// Read first, push afterwards: at read time this sample has not been written yet, so read D − 1
// (the same convention as Delay.h).
class FractionalComb {
public:
    void prepare(float delaySamples, float g) {
        delay_ = delaySamples;
        g_ = g;
        line_.prepare(static_cast<int>(delaySamples) + 4, 1);
    }
    float processSample(float x) noexcept {
        const float y = x + g_ * line_.readCubic(0, delay_ - 1.0f);
        line_.push(0, y);
        return y;
    }

private:
    FractionalDelayLine line_;
    float delay_ = 2.0f;
    float g_ = 0.0f;
};

double combGainDb(float delaySamples, float g, double freq, double sampleRate) {
    FractionalComb comb;
    comb.prepare(delaySamples, g);
    // With loop g = 0.8, the echoes need 3D/(−log10 0.8) ~ 31 trips to decay to −60 dB, which for
    // D <= 240 is < 0.16 s, so the default 0.2 s warm-up of measureGainDb is enough for the transient to vanish
    return dawlab::measureGainDb([&](float x) { return comb.processSample(x); }, freq, sampleRate);
}

// Sweep [f0, f1] in steps of `step` and return the frequency with the largest gain
double findPeak(float delaySamples, float g, double f0, double f1, double step, double sampleRate) {
    double bestF = f0, bestG = -1.0e9;
    for (double f = f0; f <= f1 + 1.0e-9; f += step) {
        const double gdb = combGainDb(delaySamples, g, f, sampleRate);
        if (gdb > bestG) {
            bestG = gdb;
            bestF = f;
        }
    }
    return bestF;
}

double cents(double f, double ref) { return 1200.0 * std::log2(f / ref); }

} // namespace

int main() {
    std::printf("=== Chapter 09, Exercise 7: comb filter calculations ===\n\n");

    // ---- 1. D = 240, g = 0.8 -----------------------------------------------------
    {
        const double fs = 48000.0;
        const float D = 240.0f, g = 0.8f;
        const double peakDb = dawlab::gainToDb(1.0 / (1.0 - g));
        const double notchDb = dawlab::gainToDb(1.0 / (1.0 + g));
        std::printf("D = 240 (5 ms), g = 0.8: theory peak %+.2f dB @ k·200 Hz, notch %+.2f dB @ (k+1/2)·200 Hz\n", peakDb,
                    notchDb);
        std::printf("   freq(Hz)   measured(dB)   theory(dB)\n");
        for (double f : {200.0, 300.0, 400.0, 500.0, 1000.0, 1100.0, 5000.0, 5100.0}) {
            const bool isPeak = std::fmod(f, 200.0) == 0.0;
            const double expect = isPeak ? peakDb : notchDb;
            const double got = combGainDb(D, g, f, fs);
            std::printf("   %7.0f    %+7.2f      %+7.2f\n", f, got, expect);
            DAWLAB_CHECK_NEAR(got, expect, 0.05);
        }
        DAWLAB_CHECK_NEAR(peakDb, 13.98, 0.01);
        DAWLAB_CHECK_NEAR(notchDb, -5.11, 0.01);
        std::printf("\n");
    }

    // ---- 2. Negative feedback: the peaks move to odd multiples of fs/(2D) ----------------
    {
        const double fs = 48000.0;
        const float D = 240.0f, g = -0.8f;
        std::printf("g = -0.8: the peaks should be at 100, 300, 500 Hz (odd multiples of fs/2D) and the notches at 200, 400 Hz\n");
        std::printf("   freq(Hz)   measured(dB)\n");
        for (double f : {100.0, 200.0, 300.0, 400.0, 500.0}) {
            const double got = combGainDb(D, g, f, fs);
            std::printf("   %7.0f    %+7.2f\n", f, got);
            const bool oddMultiple = static_cast<int>(f / 100.0) % 2 == 1;
            DAWLAB_CHECK_NEAR(got, oddMultiple ? 13.98 : -5.11, 0.05);
        }
        std::printf("\n");
    }

    // ---- 3. Tuning the comb to A4 = 440 Hz (fs = 44.1 kHz) ----------------------------
    {
        const double fs = 44100.0;
        const float g = 0.8f;
        const float exact = static_cast<float>(fs / 440.0); // 100.227
        const double pFrac = findPeak(exact, g, 435.0, 445.0, 0.1, fs);
        const double p100 = findPeak(100.0f, g, 435.0, 445.0, 0.1, fs);
        const double p101 = findPeak(101.0f, g, 435.0, 445.0, 0.1, fs);
        // The theoretical pitch fs/D and the cents; the peak found by the sweep (0.1 Hz steps) is only
        // used to verify the position -- computing cents from the rounded 436.6 Hz would give −13.4,
        // which has some rounding error compared with the true −13.3.
        const double f100 = fs / 100.0, f101 = fs / 101.0;
        std::printf("fs = 44.1 kHz, a comb that should sound at 440 Hz: D = %.3f samples\n", exact);
        std::printf("  D = 100 (integer)     -> theoretical fs/D = %.2f Hz (%+.1f cents), measured peak %.1f Hz\n", f100,
                    cents(f100, 440.0), p100);
        std::printf("  D = 101 (integer)     -> theoretical fs/D = %.2f Hz (%+.1f cents), measured peak %.1f Hz\n", f101,
                    cents(f101, 440.0), p101);
        std::printf("  D = %.3f (cubic) -> measured peak %.1f Hz\n", exact, pFrac);
        DAWLAB_CHECK_NEAR(cents(f100, 440.0), 3.93, 0.01);
        DAWLAB_CHECK_NEAR(cents(f101, 440.0), -13.30, 0.01);
        DAWLAB_CHECK_NEAR(p100, f100, 0.15);
        DAWLAB_CHECK_NEAR(p101, f101, 0.15);
        DAWLAB_CHECK_NEAR(pFrac, 440.0, 0.15);
        std::printf("  -> An integer delay can only make the discrete pitches fs/integer; accurate tuning needs a fractional delay.\n\n");
    }

    // ---- 4. Impulse response: g^k once every D samples ------------------------------------
    {
        const float D = 10.0f;
        for (float g : {0.9f, 1.0f, 1.1f}) {
            FractionalComb comb;
            comb.prepare(D, g);
            float y = 0.0f;
            for (int n = 0; n <= 100 * 10; ++n)
                y = comb.processSample(n == 0 ? 1.0f : 0.0f); // the last n = 1000 = the 100th echo
            std::printf("g = %.1f: amplitude of the 100th echo %.4g (theory g^100 = %.4g)\n", g, y, std::pow(g, 100.0f));
            DAWLAB_CHECK_NEAR(y / std::pow(g, 100.0f), 1.0, 1.0e-3);
        }
        std::printf("  -> |g| < 1 decays, = 1 never decays, > 1 blows up exponentially (with g = 1.1, 100 trips amplify by a factor of %.0f)\n",
                    std::pow(1.1, 100.0));
    }

    // ---- 5. Exercise 2.2: when the filter in the loop has gain > 1, |g| < 1 no longer guarantees stability ----
    {
        const double fs = 48000.0, fc = 1000.0;
        const int D = 240; // the comb's peaks are at k·200 Hz, and 1000 Hz happens to sit on the biquad's resonance
        const auto lp = dawlab::dsp::BiquadCoefficients::makeLowpass(fs, fc, 4.0);
        double maxDb = -1.0e9, maxF = 0.0;
        for (double f = 500.0; f <= 1500.0; f += 1.0) {
            const double m = lp.magnitudeDb(f, fs);
            if (m > maxDb) {
                maxDb = m;
                maxF = f;
            }
        }
        std::printf("\nQ = 4 biquad lowpass (fc = 1 kHz): %+.2f dB at fc, maximum %+.2f dB @ %.0f Hz\n",
                    lp.magnitudeDb(fc, fs), maxDb, maxF);
        std::printf("  With g = 0.5 the maximum loop gain = 0.5 x %.2f = %.2f > 1\n", dawlab::dbToGain(maxDb),
                    0.5 * dawlab::dbToGain(maxDb));
        DAWLAB_CHECK_NEAR(lp.magnitudeDb(fc, fs), 12.04, 0.05); // the gain of the RBJ lowpass at fc = Q
        DAWLAB_CHECK(0.5 * dawlab::dbToGain(maxDb) > 1.0);

        // Actually run it: y[n] = x[n] + g·F(y[n − D]), feed in an impulse, and look at the peak over 2.5-3.0 s
        auto runLoop = [&](auto&& filter, float g) {
            FractionalDelayLine line;
            line.prepare(D + 2, 1);
            const int total = dawlab::secondsToSamples(3.0, fs);
            const int tail = dawlab::secondsToSamples(2.5, fs);
            float tailPeak = 0.0f;
            for (int n = 0; n < total; ++n) {
                const float y = (n == 0 ? 1.0f : 0.0f) + g * filter(line.readInteger(0, D - 1));
                line.push(0, y);
                if (!std::isfinite(y)) // diverged until the float overflowed (inf / NaN); std::max returns 0 for NaN, so handle it specially
                    return std::numeric_limits<float>::infinity();
                if (n >= tail)
                    tailPeak = std::max(tailPeak, std::abs(y));
            }
            return tailPeak;
        };
        dawlab::dsp::OnePole onePole;
        onePole.setType(dawlab::dsp::OnePole::Type::Lowpass);
        onePole.prepare(fs, 512, 1);
        onePole.setCutoff(static_cast<float>(fc));
        const float tailOnePole = runLoop([&](float v) { return onePole.processSample(0, v); }, 0.99f);
        dawlab::dsp::Biquad biquad;
        biquad.prepare(fs, 512, 1);
        biquad.setCoefficients(lp);
        const float tailBiquad = runLoop([&](float v) { return biquad.processSample(0, v); }, 0.5f);
        std::printf("  Impulse in, peak over 2.5-3.0 s: one-pole LP, g = 0.99 -> %.3g (decays); "
                    "Q = 4 biquad, g = 0.5 -> %.3g (diverges)\n",
                    tailOnePole, tailBiquad);
        DAWLAB_CHECK(tailOnePole < 0.1f);
        DAWLAB_CHECK(tailBiquad > 100.0f);
    }

    return dawlab::test::summary("l09_ex7");
}
