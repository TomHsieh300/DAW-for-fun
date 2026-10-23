// l02_filter_response: theoretical frequency response vs. measurement.
//
// What it shows:
//   1. One-pole lowpass: compares the exp version (a = exp(-2π fc/fs)) with the bilinear version,
//      and uses bisection to find the actual -3 dB point, to see how far apart the two are as
//      the cutoff rises.
//   2. The 8 RBJ biquad filter types: puts BiquadCoefficients::magnitudeDb() (substituting
//      z = e^{jω} into H(z)) and measureGainDb() (actually feeding sine waves through the filter
//      and measuring the output) in one table; the two should agree.
//   3. Slope: how many dB a lowpass drops per octave above the cutoff (biquad ~ 12 dB/oct,
//      one-pole ~ 6 dB/oct).
//   4. Cramping: when the cutoff approaches Nyquist, the bilinear transform "squeezes" the analog
//      prototype's curve toward Nyquist.
//
// How to run: run with no arguments. There is no WAV output, only tables are printed; the
// self-check at the end confirms that theory and measurement agree.
#include <dawlab/Common.h>
#include <dawlab/dsp/Biquad.h>
#include <dawlab/dsp/OnePole.h>

#include <cstdio>
#include <functional>
#include <limits>
#include <vector>

using namespace dawlab;
using dawlab::dsp::Biquad;
using dawlab::dsp::BiquadCoefficients;
using dawlab::dsp::OnePole;

constexpr double kSampleRate = 48000.0;

// ---- Two one-pole designs --------------------------------------------------------------
// exp version: H(z) = (1-a) / (1 - a z^-1), a = exp(-2π fc/fs)
static double onePoleExpDb(double fc, double f) {
    const double a = OnePole::coefficientForCutoff(fc, kSampleRate);
    const std::complex<double> z1 = std::polar(1.0, -kTwoPi * f / kSampleRate);
    return gainToDb(std::abs((1.0 - a) / (1.0 - a * z1)));
}

// bilinear version: analog H(s) = 1/(1 + s/ωc), s = (2/T)(1 - z^-1)/(1 + z^-1); after prewarping
//   g = tan(π fc/fs), H(z) = g(1 + z^-1) / ((1+g) - (1-g) z^-1)
static double onePoleBilinearDb(double fc, double f) {
    const double g = std::tan(kPi * fc / kSampleRate);
    const std::complex<double> z1 = std::polar(1.0, -kTwoPi * f / kSampleRate);
    return gainToDb(std::abs(g * (1.0 + z1) / ((1.0 + g) - (1.0 - g) * z1)));
}

// Bisection for the frequency where responseDb(f) = targetDb (responseDb must be monotonic on [lo, hi])
static double findFrequencyAtDb(const std::function<double(double)>& responseDb, double targetDb, double lo,
                                double hi) {
    const bool decreasing = responseDb(lo) > responseDb(hi);
    for (int i = 0; i < 60; ++i) {
        const double mid = std::sqrt(lo * hi); // geometric mean: bisect on a logarithmic frequency axis
        const bool above = responseDb(mid) > targetDb;
        if (above == decreasing)
            lo = mid;
        else
            hi = mid;
    }
    return std::sqrt(lo * hi);
}

int main() {
    std::printf("=== Chapter 02: theoretical frequency response vs. measurement (fs = %.0f Hz) ===\n\n", kSampleRate);

    // ---- 1. one-pole: exp version vs. bilinear version ----------------------------------
    std::printf("[1] One-pole lowpass: actual -3.01 dB point of the exp version vs. the bilinear version\n");
    std::printf("    %8s  %12s  %12s  %12s\n", "fc (Hz)", "a=exp(..)", "exp -3dB", "bilinear -3dB");
    for (double fc : {100.0, 1000.0, 5000.0, 10000.0, 15000.0}) {
        const double a = OnePole::coefficientForCutoff(fc, kSampleRate);
        const double f3bil = findFrequencyAtDb([&](double f) { return onePoleBilinearDb(fc, f); }, -3.0103, 1.0,
                                               kSampleRate * 0.4999);
        // The exp version's gain at Nyquist is (1-a)/(1+a), not 0: when fc is too high the whole curve stays above -3 dB, so there is no -3 dB point at all
        const double nyquistDb = onePoleExpDb(fc, kSampleRate * 0.5);
        if (nyquistDb < -3.0103) {
            const double f3exp = findFrequencyAtDb([&](double f) { return onePoleExpDb(fc, f); }, -3.0103, 1.0,
                                                   kSampleRate * 0.4999);
            std::printf("    %8.0f  %12.6f  %12.1f  %12.1f\n", fc, a, f3exp, f3bil);
        } else {
            std::printf("    %8.0f  %12.6f  %12s  %12.1f   <- the exp version is only %.2f dB even at Nyquist\n", fc, a, "none", f3bil,
                        nyquistDb);
            DAWLAB_CHECK(fc >= 12000.0); // only happens when the cutoff is very high
        }
        // The bilinear version's -3 dB point always lands exactly on fc (the purpose of prewarping)
        DAWLAB_CHECK_NEAR(f3bil, fc, fc * 0.001);
    }
    std::printf("    -> The exp version is accurate when fc << fs and drifts more the closer fc gets to Nyquist; the bilinear version is always exact.\n\n");
    // Numbers quoted in the text: at fc = 1 kHz the exp version is off by only 0.14%, at fc = 10 kHz by about 19%
    DAWLAB_CHECK_NEAR(findFrequencyAtDb([](double f) { return onePoleExpDb(1000.0, f); }, -3.0103, 1.0, 23999.0),
                      1001.4, 0.5);
    DAWLAB_CHECK_NEAR(findFrequencyAtDb([](double f) { return onePoleExpDb(10000.0, f); }, -3.0103, 1.0, 23999.0),
                      11895.0, 5.0);
    // The other rows of the table in section 2.8: the exp version's -3 dB point for fc = 5 kHz, and only -2.45 dB at Nyquist for fc = 15 kHz
    DAWLAB_CHECK_NEAR(findFrequencyAtDb([](double f) { return onePoleExpDb(5000.0, f); }, -3.0103, 1.0, 23999.0),
                      5188.9, 0.5);
    DAWLAB_CHECK_NEAR(onePoleExpDb(15000.0, kSampleRate * 0.5), -2.45, 0.01);

    // Measurements of the OnePole class: a 1 kHz lowpass should be about -3 dB at 1 kHz and about -20 dB at 10 kHz (6 dB/oct)
    {
        OnePole lp;
        lp.prepare(kSampleRate, 512, 1);
        lp.setType(OnePole::Type::Lowpass);
        lp.setCutoff(1000.0f);
        std::printf("[1b] OnePole class measurements (Lowpass fc = 1 kHz)\n");
        std::printf("    %8s  %10s  %10s\n", "f (Hz)", "theory dB", "measured");
        for (double f : {100.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0}) {
            lp.reset();
            const double theory = onePoleExpDb(1000.0, f);
            const double meas = measureGainDb([&](float x) { return lp.processSample(0, x); }, f, kSampleRate);
            std::printf("    %8.0f  %10.2f  %10.2f\n", f, theory, meas);
            DAWLAB_CHECK_NEAR(meas, theory, 0.05);
        }
        OnePole hp;
        hp.prepare(kSampleRate, 512, 1);
        hp.setType(OnePole::Type::Highpass);
        hp.setCutoff(1000.0f);
        // Highpass theory: H(z) = h (1 - z^-1) / (1 - a z^-1), h = (1 + a) / 2
        const auto onePoleHpDb = [](double fcHz, double f) {
            const double ah = OnePole::coefficientForCutoff(fcHz, kSampleRate);
            const double h = 0.5 * (1.0 + ah);
            const std::complex<double> z1 = std::polar(1.0, -kTwoPi * f / kSampleRate);
            return gainToDb(std::abs(h * (1.0 - z1) / (1.0 - ah * z1)));
        };
        std::printf("    Highpass fc = 1 kHz (measured / theory):");
        double hpMeas[3] = {};
        int idx = 0;
        for (double f : {20.0, 1000.0, 20000.0}) {
            hp.reset();
            const double meas = measureGainDb([&](float x) { return hp.processSample(0, x); }, f, kSampleRate);
            const double theory = onePoleHpDb(1000.0, f);
            std::printf("  %.0f Hz = %.2f / %.2f dB", f, meas, theory);
            DAWLAB_CHECK_NEAR(meas, theory, 0.05);
            hpMeas[idx++] = meas;
        }
        std::printf("\n");
        // Numbers quoted in README section 2.9: 20 Hz ~ the 6 dB/oct extrapolation 20 log10(20/1000) = -33.98 dB, -3 dB at fc, 0 dB at high frequencies
        DAWLAB_CHECK_NEAR(hpMeas[0], 20.0 * std::log10(20.0 / 1000.0), 0.1);
        DAWLAB_CHECK_NEAR(hpMeas[1], -3.0, 0.05);
        DAWLAB_CHECK_NEAR(hpMeas[2], 0.0, 0.05);
        DAWLAB_CHECK_NEAR(onePoleHpDb(1000.0, kSampleRate * 0.5), 0.0, 1e-9); // h = (1+a)/2: exactly 0 dB at Nyquist

        // NaN parameter guards (README section 4): std::clamp / std::max cannot stop NaN, so the setter / factories must block it first
        {
            const double before = hp.getCoefficient();
            hp.setCutoff(std::numeric_limits<float>::quiet_NaN()); // ignored, the last valid value is kept
            DAWLAB_CHECK(hp.getCoefficient() == before && std::isfinite(hp.processSample(0, 0.5f)));
            const double qnan = std::numeric_limits<double>::quiet_NaN();
            const BiquadCoefficients bad[] = {BiquadCoefficients::makeLowpass(kSampleRate, qnan, 0.7),
                                              BiquadCoefficients::makeLowpass(kSampleRate, 1000.0, qnan),
                                              BiquadCoefficients::makePeak(kSampleRate, 1000.0, 1.0, qnan)};
            for (const BiquadCoefficients& c : bad)
                DAWLAB_CHECK(std::isfinite(c.b0) && std::isfinite(c.a1) && c.isStable());
        }

        // Hand-calculation cross-check (exercise 1): both one-pole coefficient sets at fc = 1 kHz, and the exp version's gain at Nyquist
        const double a = lp.getCoefficient();
        const double nyqDb = gainToDb((1.0 - a) / (1.0 + a));
        const double g = std::tan(kPi * 1000.0 / kSampleRate);
        const double pole = (1.0 - g) / (1.0 + g);
        std::printf("    Hand-calculation check: exp version a = %.6f, b0 = 1 - a = %.6f, Nyquist gain (1-a)/(1+a) = %.2f dB\n", a, 1.0 - a,
                    nyqDb);
        std::printf("                            bilinear version g = tan(π fc/fs) = %.6f, pole = (1-g)/(1+g) = %.6f, b0 = b1 = g/(1+g) = %.6f\n\n",
                    g, pole, g / (1.0 + g));
        DAWLAB_CHECK_NEAR(a, 0.877306, 1e-6);
        DAWLAB_CHECK_NEAR(nyqDb, -23.69, 0.01);
        DAWLAB_CHECK_NEAR(onePoleExpDb(1000.0, kSampleRate * 0.5), nyqDb, 1e-9);
        DAWLAB_CHECK_NEAR(pole, 0.876976, 1e-6);
        DAWLAB_CHECK(onePoleBilinearDb(1000.0, kSampleRate * 0.4999) < -80.0); // the zero sits at z = -1: the response heads to -∞ at Nyquist
    }

    // ---- 2. The eight biquad filters: theory vs. measurement ----------------------------
    struct Case {
        const char* name;
        BiquadCoefficients c;
    };
    const double fc = 1000.0, q = BiquadCoefficients::kButterworthQ;
    const std::vector<Case> cases = {
        {"Lowpass  Q=0.707", BiquadCoefficients::makeLowpass(kSampleRate, fc, q)},
        {"Highpass Q=0.707", BiquadCoefficients::makeHighpass(kSampleRate, fc, q)},
        {"Bandpass Q=2    ", BiquadCoefficients::makeBandpass(kSampleRate, fc, 2.0)},
        {"Notch    Q=2    ", BiquadCoefficients::makeNotch(kSampleRate, fc, 2.0)},
        {"Allpass  Q=0.707", BiquadCoefficients::makeAllpass(kSampleRate, fc, q)},
        {"Peak +6dB Q=1   ", BiquadCoefficients::makePeak(kSampleRate, fc, 1.0, 6.0)},
        {"LowShelf +6dB   ", BiquadCoefficients::makeLowShelf(kSampleRate, fc, q, 6.0)},
        {"HighShelf -6dB  ", BiquadCoefficients::makeHighShelf(kSampleRate, fc, q, -6.0)},
    };
    const double freqs[] = {50.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 16000.0};

    std::printf("[2] RBJ biquad (fc = 1 kHz): each cell is \"theory / measured\" in dB\n");
    std::printf("    %-17s", "filter");
    for (double f : freqs)
        std::printf("  %13.0f", f);
    std::printf("\n");

    Biquad filter;
    filter.prepare(kSampleRate, 512, 1);
    for (const Case& cs : cases) {
        std::printf("    %-17s", cs.name);
        filter.setCoefficients(cs.c);
        for (double f : freqs) {
            filter.reset();
            const double theory = cs.c.magnitudeDb(f, kSampleRate);
            const double meas =
                measureGainDb([&](float x) { return filter.processSample(0, x); }, f, kSampleRate, 0.5, 0.3);
            std::printf("  %6.2f/%6.2f", theory, meas);
            if (theory > -50.0)
                DAWLAB_CHECK_NEAR(meas, theory, 0.1);
            else
                DAWLAB_CHECK(meas < -50.0); // notch center: the measurement is limited by window leakage, so only require "very deep"
        }
        std::printf("\n");
        DAWLAB_CHECK(cs.c.isStable());
    }
    std::printf("\n");

    // Self-checks for a few "textbook numbers"
    const BiquadCoefficients lpf = cases[0].c;
    DAWLAB_CHECK_NEAR(lpf.magnitudeDb(1000.0, kSampleRate), -3.01, 0.05);  // Butterworth: -3 dB at the cutoff
    DAWLAB_CHECK_NEAR(lpf.magnitudeDb(20.0, kSampleRate), 0.0, 0.01);      // 0 dB in the passband
    DAWLAB_CHECK_NEAR(cases[2].c.magnitudeDb(1000.0, kSampleRate), 0.0, 0.01); // bandpass peak is 0 dB
    DAWLAB_CHECK(cases[3].c.magnitudeDb(1000.0, kSampleRate) < -60.0);        // notch center is almost fully cancelled
    DAWLAB_CHECK_NEAR(cases[4].c.magnitudeDb(300.0, kSampleRate), 0.0, 1e-9); // allpass is 0 dB at all frequencies
    DAWLAB_CHECK_NEAR(std::abs(cases[4].c.phase(1000.0, kSampleRate)), kPi, 1e-6); // allpass phase is ±180° at fc
    DAWLAB_CHECK_NEAR(cases[5].c.magnitudeDb(1000.0, kSampleRate), 6.0, 0.01); // peak at fc = +6 dB
    DAWLAB_CHECK_NEAR(cases[6].c.magnitudeDb(20.0, kSampleRate), 6.0, 0.05);   // low shelf is +6 dB at low frequencies
    DAWLAB_CHECK_NEAR(cases[7].c.magnitudeDb(20000.0, kSampleRate), -6.0, 0.05); // high shelf is -6 dB at high frequencies

    // ---- 3. Slope: how many dB per octave ---------------------------------------------
    std::printf("[3] Slope (attenuation per octave above the cutoff)\n");
    {
        const double d1 = lpf.magnitudeDb(4000.0, kSampleRate) - lpf.magnitudeDb(2000.0, kSampleRate);
        const double d2 = lpf.magnitudeDb(8000.0, kSampleRate) - lpf.magnitudeDb(4000.0, kSampleRate);
        // For the one-pole use fc = 100 Hz: far enough from Nyquist to see a clean -6 dB/oct asymptote
        const double o1 = onePoleExpDb(100.0, 1600.0) - onePoleExpDb(100.0, 800.0);
        const double o2 = onePoleExpDb(1000.0, 4000.0) - onePoleExpDb(1000.0, 2000.0);
        std::printf("    biquad LPF fc=1k    2k->4k: %.2f dB   4k->8k: %.2f dB   (asymptotically -12 dB/oct; cramping has started by 8 kHz, so it is steeper)\n",
                    d1, d2);
        std::printf("    one-pole LP fc=100  800->1.6k: %.2f dB                  (asymptotically -6 dB/oct)\n", o1);
        std::printf("    one-pole LP fc=1k   2k->4k: %.2f dB   (the exp version is not -∞ at Nyquist, so for a high fc the slope is less than 6 dB/oct)\n", o2);
        DAWLAB_CHECK_NEAR(d1, -12.0, 0.6);
        DAWLAB_CHECK(d2 < d1 - 0.5); // cramping: steeper the closer to Nyquist
        DAWLAB_CHECK_NEAR(o1, -6.0, 0.3);
        DAWLAB_CHECK(o2 > -6.0 && o2 < -4.5);
    }
    std::printf("\n");

    // ---- 4. Cramping: cutoff close to Nyquist ----------------------------------------
    std::printf("[4] Cramping: analog prototype vs. the digital response after bilinear (LPF Q=0.707)\n");
    std::printf("    %8s  %8s  %12s  %12s\n", "fc", "f", "analog dB", "digital dB");
    for (double cutoff : {1000.0, 10000.0, 18000.0}) {
        const BiquadCoefficients c = BiquadCoefficients::makeLowpass(kSampleRate, cutoff, q);
        for (double ratio : {0.5, 1.0, 1.2}) {
            const double f = cutoff * ratio;
            if (f >= kSampleRate * 0.5)
                continue;
            // Analog second-order Butterworth: |H|² = 1 / (1 + (f/fc)^4)
            const double analog = -10.0 * std::log10(1.0 + std::pow(f / cutoff, 4.0));
            const double digital = c.magnitudeDb(f, kSampleRate);
            std::printf("    %8.0f  %8.0f  %12.2f  %12.2f\n", cutoff, f, analog, digital);
            if (ratio == 1.0)
                DAWLAB_CHECK_NEAR(digital, -3.01, 0.01); // prewarping: always -3.01 dB at the cutoff
            if (ratio == 1.2 && cutoff == 1000.0)
                DAWLAB_CHECK_NEAR(digital, analog, 0.02); // low fc: almost identical to analog
        }
    }
    // Numbers in the section 2.14 table: 20% above the cutoff the analog response is -4.88 dB everywhere; the digital response is -5.89 / -16.79 dB at fc = 10k / 18k
    {
        const auto digitalAt = [&](double cutoff, double f) {
            return BiquadCoefficients::makeLowpass(kSampleRate, cutoff, q).magnitudeDb(f, kSampleRate);
        };
        const double analog12 = -10.0 * std::log10(1.0 + std::pow(1.2, 4.0));
        DAWLAB_CHECK_NEAR(analog12, -4.88, 0.01);
        DAWLAB_CHECK_NEAR(digitalAt(10000.0, 12000.0), -5.89, 0.01);
        DAWLAB_CHECK_NEAR(digitalAt(18000.0, 21600.0), -16.79, 0.01);
        DAWLAB_CHECK(digitalAt(18000.0, 21600.0) < analog12 - 10.0); // cramping: more than 10 dB of extra attenuation
    }
    std::printf("    -> The response always lines up at the cutoff (guaranteed by prewarping), but the curve above the cutoff is squeezed toward Nyquist (= -∞ at 24 kHz).\n");
    std::printf("       With fc = 18 kHz the attenuation at 21.6 kHz is far deeper than analog; that is cramping.\n\n");

    return dawlab::test::summary("l02_filter_response");
}
