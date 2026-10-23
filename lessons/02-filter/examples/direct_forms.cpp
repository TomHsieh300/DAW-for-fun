// l02_direct_forms: comparing Direct Form I / Direct Form II / Transposed Direct Form II.
//
// What it shows:
//   The three structures compute the same H(z); with infinite precision and fixed coefficients
//   their outputs are identical. They differ in:
//   (a) The number of state variables (DF-I has 4, DF-II / TDF-II have 2).
//   (b) The size of the internal nodes: take a "hard" filter -- a lowpass at fc = 30 Hz, Q = 0.707
//       (fs = 48 kHz), whose poles sit very close to z = 1, with b0 ~ 3.8e-6, a1 ~ -1.994,
//       a2 ~ 0.994. The DF-II intermediate variable w[n] becomes tens of thousands of times larger
//       than the output: when float adds an input of 0.3 to a w of 18908, the low bits of the input
//       are swallowed entirely; in fixed-point it would simply overflow.
//   (c) Behavior when the coefficients change: the state of DF-I is the real past inputs / outputs,
//       and the state of TDF-II is of the same magnitude as the output, so both are smooth when
//       the coefficients change; the state of DF-II is w, amplified tens of thousands of times, so
//       at the moment the coefficients change the output jumps a large step (a click).
//   (d) float round-off error: all three structures are in the same range (about -70 dB). Precision
//       has to come from double, and for *both* the coefficients and the state: switching only the
//       state to double while keeping float coefficients only reaches about -90 dB (float
//       coefficients move the poles away from the designed values, so what is computed is really a
//       different filter); with both coefficients and state in double, the difference from the
//       reference implementation drops below -200 dB. That is why dawlab::dsp::Biquad uses double
//       coefficients + double state + TDF-II. Biquad returns float to the outside, and the
//       quantization at that step (about -151 dB) is the error floor of its output.
//
// How to run: run with no arguments. There is no WAV output.
#include <dawlab/Common.h>
#include <dawlab/dsp/Biquad.h>

#include <cstdio>
#include <vector>

using namespace dawlab;
using dawlab::dsp::Biquad;
using dawlab::dsp::BiquadCoefficients;

constexpr double kSampleRate = 48000.0;

// ---- The three structures (for teaching, single channel, T = float or double) ----------
// DirectForm1 additionally lets the "state type T" and the "coefficient type C" be chosen
// separately, to isolate their individual effects on precision: the coefficients are first stored
// as C (quantized when C is float), arithmetic uses the more precise of the two types (Acc), and
// the result is stored back as T.
template <typename T, typename C = T>
struct DirectForm1 {
    using Acc = decltype(T{} * C{}); // float × float -> float; as soon as one side is double -> double
    C b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    T x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    void set(const BiquadCoefficients& c) {
        b0 = C(c.b0); b1 = C(c.b1); b2 = C(c.b2); a1 = C(c.a1); a2 = C(c.a2);
    }
    // y[n] = b0 x[n] + b1 x[n-1] + b2 x[n-2] - a1 y[n-1] - a2 y[n-2]: 4 states (past inputs and outputs)
    T process(T x) {
        const Acc y = Acc(b0) * x + Acc(b1) * x1 + Acc(b2) * x2 - Acc(a1) * y1 - Acc(a2) * y2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = T(y);
        return T(y);
    }
};

template <typename T>
struct DirectForm2 {
    T b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    T w1 = 0, w2 = 0;
    T maxW = 0; // observe the size of the intermediate variable
    void set(const BiquadCoefficients& c) {
        b0 = T(c.b0); b1 = T(c.b1); b2 = T(c.b2); a1 = T(c.a1); a2 = T(c.a2);
    }
    // w[n] = x[n] - a1 w[n-1] - a2 w[n-2]; y[n] = b0 w[n] + b1 w[n-1] + b2 w[n-2]: 2 states
    T process(T x) {
        const T w = x - a1 * w1 - a2 * w2;
        const T y = b0 * w + b1 * w1 + b2 * w2;
        w2 = w1;
        w1 = w;
        maxW = std::max(maxW, std::abs(w));
        return y;
    }
};

template <typename T>
struct TransposedDirectForm2 {
    T b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    T s1 = 0, s2 = 0;
    void set(const BiquadCoefficients& c) {
        b0 = T(c.b0); b1 = T(c.b1); b2 = T(c.b2); a1 = T(c.a1); a2 = T(c.a2);
    }
    // y[n] = b0 x[n] + s1; s1 = b1 x[n] - a1 y[n] + s2; s2 = b2 x[n] - a2 y[n]: 2 states (same magnitude as the output)
    T process(T x) {
        const T y = b0 * x + s1;
        s1 = b1 * x - a1 * y + s2;
        s2 = b2 * x - a2 * y;
        return y;
    }
};

int main() {
    std::printf("=== Chapter 02: Direct Form I / II / Transposed II comparison ===\n\n");

    const double fc = 30.0;
    const BiquadCoefficients c = BiquadCoefficients::makeLowpass(kSampleRate, fc, BiquadCoefficients::kButterworthQ);
    std::printf("[1] LPF fc = %.0f Hz, Q = 0.707, fs = %.0f:\n", fc, kSampleRate);
    std::printf("    b0 = %.6e  b1 = %.6e  b2 = %.6e\n    a1 = %.9f  a2 = %.9f\n", c.b0, c.b1, c.b2, c.a1, c.a2);
    const auto [p1, p2] = c.poles();
    std::printf("    poles: %.6f %+.6fj, |p| = %.6f (very close to the unit circle)\n\n", p1.real(), p1.imag(), std::abs(p1));

    // Input: white noise + a 20 Hz sine (inside the passband, so it should pass almost untouched)
    const int n = 48000 * 4;
    AudioBuffer in = makeWhiteNoise(kSampleRate, 4.0, 0.3f, 1);
    for (int i = 0; i < n; ++i)
        in.addSample(0, i, static_cast<float>(0.3 * std::sin(kTwoPi * 20.0 * i / kSampleRate)));

    // ---- [2] Round-off error: compared against a TDF-II with "double coefficients + double state" (output kept as double too) --------
    // The reference must have a double output: dawlab::dsp::Biquad is double internally, but
    // processSample() returns float, so using it as the reference would hide any result more
    // accurate than float quantization (about -151 dB).
    DirectForm1<float> df1f;                 // float coefficients, float state
    DirectForm2<float> df2f;
    TransposedDirectForm2<float> tdf2f;
    DirectForm1<double, float> df1sd;        // float coefficients, double state (only the state changed)
    DirectForm1<float, double> df1cd;        // double coefficients, float state (only the coefficients changed)
    DirectForm1<double> df1d;                // double coefficients, double state
    TransposedDirectForm2<double> ref;       // reference: double TDF-II, double output
    df1f.set(c);
    df2f.set(c);
    tdf2f.set(c);
    df1sd.set(c);
    df1cd.set(c);
    df1d.set(c);
    ref.set(c);
    Biquad bq; // dawlab::dsp::Biquad: double coefficients + state, but float output
    bq.prepare(kSampleRate, 512, 1);
    bq.setCoefficients(c);

    double err1 = 0, err2 = 0, errT = 0, err1sd = 0, err1cd = 0, err1d = 0, errBq = 0, errQ = 0, refPow = 0;
    double peakRef = 0;
    for (int i = 0; i < n; ++i) {
        const float x = in.getSample(0, i);
        const double y1 = df1f.process(x);
        const double y2 = df2f.process(x);
        const double yT = tdf2f.process(x);
        const double y1sd = df1sd.process(x);
        const double y1cd = df1cd.process(x);
        const double y1d = df1d.process(x);
        const double yB = bq.processSample(0, x);
        const double yR = ref.process(x);
        const double yQ = static_cast<float>(yR); // round only the reference output to float
        if (i >= 48000) { // skip the first 1 s of transient
            err1 += (y1 - yR) * (y1 - yR);
            err2 += (y2 - yR) * (y2 - yR);
            errT += (yT - yR) * (yT - yR);
            err1sd += (y1sd - yR) * (y1sd - yR);
            err1cd += (y1cd - yR) * (y1cd - yR);
            err1d += (y1d - yR) * (y1d - yR);
            errBq += (yB - yR) * (yB - yR);
            errQ += (yQ - yR) * (yQ - yR);
            refPow += yR * yR;
            peakRef = std::max(peakRef, std::abs(yR));
        }
    }
    const auto errDb = [&](double e) { return 10.0 * std::log10(std::max(e, 1e-30) / refPow); };
    std::printf("[2] Output error relative to the double TDF-II reference (double coefficients + state + output) (RMS, dB re. output):\n");
    std::printf("    Structure  Coeffs  State\n");
    std::printf("    DF-I    float   float  (4 states)   %8.2f dB\n", errDb(err1));
    std::printf("    DF-II   float   float  (2 states)   %8.2f dB\n", errDb(err2));
    std::printf("    TDF-II  float   float  (2 states)   %8.2f dB\n", errDb(errT));
    std::printf("    DF-I    float   double              %8.2f dB   <- state only: coefficient quantization moves the poles\n", errDb(err1sd));
    std::printf("    DF-I    double  float               %8.2f dB   <- coefficients only: the state's round-off is still there\n", errDb(err1cd));
    std::printf("    DF-I    double  double              %8.2f dB   <- both in double\n", errDb(err1d));
    std::printf("    dawlab::dsp::Biquad (double inside, float output)%8.2f dB\n", errDb(errBq));
    std::printf("    (For comparison: just rounding the reference output to float   %8.2f dB <- Biquad's error is exactly this quantization floor)\n",
                errDb(errQ));
    std::printf("    Reference output peak = %.3f; DF-II intermediate variable w[n] peak = %.1f (amplified %.0fx)\n\n", peakRef, df2f.maxW,
                df2f.maxW / peakRef);
    // All three float structures are in the same range (below -60 dB)
    DAWLAB_CHECK(errDb(err1) < -60.0 && errDb(err2) < -60.0 && errDb(errT) < -60.0);
    DAWLAB_CHECK(std::abs(errDb(err1) - errDb(errT)) < 10.0);
    // Changing only the state is not enough: the error with float coefficients is still far above -130 dB; changing only the coefficients is not enough either
    DAWLAB_CHECK(errDb(err1sd) > -110.0);
    DAWLAB_CHECK(errDb(err1cd) > -110.0);
    // Coefficients and state both in double: the two double implementations agree to below -200 dB
    DAWLAB_CHECK(errDb(err1d) < -200.0);
    // Biquad's error = the quantization of the float output (the two differ by < 0.5 dB)
    DAWLAB_CHECK_NEAR(errDb(errBq), errDb(errQ), 0.5);
    DAWLAB_CHECK(errDb(errBq) < -140.0);
    DAWLAB_CHECK(df2f.maxW > 1000.0f * peakRef);

    // ---- [3] Click when the coefficients change: halfway through a 20 Hz sine the cutoff jumps from 30 Hz to 60 Hz -----------------
    std::printf("[3] Largest jump between output samples at the moment the coefficients change (fc 30 -> 60 Hz):\n");
    const BiquadCoefficients c2 = BiquadCoefficients::makeLowpass(kSampleRate, 2.0 * fc, BiquadCoefficients::kButterworthQ);
    DirectForm1<double> s1;
    DirectForm2<double> s2;
    TransposedDirectForm2<double> sT;
    s1.set(c);
    s2.set(c);
    sT.set(c);
    const int switchAt = 48000 * 2;
    double prev1 = 0, prev2 = 0, prevT = 0, before = 0, jump1 = 0, jump2 = 0, jumpT = 0;
    for (int i = 0; i < switchAt + 2000; ++i) {
        if (i == switchAt) {
            s1.set(c2);
            s2.set(c2);
            sT.set(c2);
        }
        const double x = 0.3 * std::sin(kTwoPi * 20.0 * i / kSampleRate);
        const double y1 = s1.process(x), y2 = s2.process(x), yT = sT.process(x);
        if (i > 0) {
            const double d1 = std::abs(y1 - prev1), d2 = std::abs(y2 - prev2), dT = std::abs(yT - prevT);
            if (i < switchAt)
                before = std::max(before, d1); // normal largest difference between samples of a 20 Hz sine
            else {
                jump1 = std::max(jump1, d1);
                jump2 = std::max(jump2, d2);
                jumpT = std::max(jumpT, dT);
            }
        }
        prev1 = y1;
        prev2 = y2;
        prevT = yT;
    }
    std::printf("    Normal largest jump before the switch %.5f (the 20 Hz sine itself)\n", before);
    std::printf("    DF-I   %.5f (%.1fx)\n", jump1, jump1 / before);
    std::printf("    DF-II  %.5f (%.1fx) <- the state w is an amplified quantity, so changing the b coefficients makes it \"jump\" immediately\n", jump2, jump2 / before);
    std::printf("    TDF-II %.5f (%.1fx)\n\n", jumpT, jumpT / before);
    DAWLAB_CHECK(jump2 > 20.0 * jumpT);
    DAWLAB_CHECK(jumpT < 3.0 * before);
    DAWLAB_CHECK(jump1 < 3.0 * before);

    std::printf("Conclusion: DF-II's internal nodes get amplified and changing coefficients clicks, so it is unsuitable for audio; DF-I behaves well but needs 4 states;\n"
                "            TDF-II uses 2 states, has internal magnitudes equal to the output, and is smooth when coefficients change, so dawlab::dsp::Biquad uses TDF-II.\n"
                "            The float round-off of the three is about the same (about -70 dB); switching *both* coefficients and state to double is the real fix,\n"
                "            after which Biquad's output error is just the quantization of the float output itself (about -151 dB).\n\n");
    return dawlab::test::summary("l02_direct_forms");
}
