// 練習 2（解答）：Q ↔ bandwidth（octave）換算，並用 Biquad 實際驗證「頻寬邊緣」的增益
//
// 公式（README 2.4 節推導）：
//   BW = (2 / ln 2) · asinh(1 / (2Q))           Q → octave
//   Q  = 2^(BW/2) / (2^BW − 1)                   octave → Q
//   頻寬邊緣：f_{1,2} = f0 · ( sqrt(1 + 1/(4Q²)) ∓ 1/(2Q) )，f1·f2 = f0²，f2 − f1 = f0 / Q
// RBJ peak 的頻寬定義在「中點增益」（gainDb / 2）；bandpass / notch 定義在 -3 dB。

#include <dawlab/Common.h>
#include <dawlab/dsp/Biquad.h>

#include <cstdio>

using namespace dawlab;
using namespace dawlab::dsp;

namespace {

double qToBandwidthOctaves(double q) { return 2.0 / std::log(2.0) * std::asinh(1.0 / (2.0 * q)); }

double bandwidthOctavesToQ(double bw) {
    const double p = std::pow(2.0, bw);
    return std::sqrt(p) / (p - 1.0);
}

void bandEdges(double f0, double q, double& fLow, double& fHigh) {
    const double r = std::sqrt(1.0 + 1.0 / (4.0 * q * q));
    fLow = f0 * (r - 1.0 / (2.0 * q));
    fHigh = f0 * (r + 1.0 / (2.0 * q));
}

} // namespace

int main() {
    constexpr double sr = 48000.0;
    std::printf("=== 第 03 章 練習 2：Q <-> bandwidth ===\n\n");

    std::printf("  Q -> bandwidth(octave)\n");
    for (double q : {0.5, 0.707, 1.0, 1.4142, 2.0, 4.3185, 10.0})
        std::printf("    Q = %6.3f  ->  BW = %.3f oct\n", q, qToBandwidthOctaves(q));
    std::printf("  bandwidth(octave) -> Q\n");
    for (double bw : {3.0, 2.0, 1.0, 0.5, 1.0 / 3.0, 1.0 / 6.0})
        std::printf("    BW = %.3f oct  ->  Q = %.4f\n", bw, bandwidthOctavesToQ(bw));

    // ---- 公式的 self-check ----
    DAWLAB_CHECK_NEAR(bandwidthOctavesToQ(1.0), std::sqrt(2.0), 1e-9); // 1 octave 剛好是 Q = √2
    DAWLAB_CHECK_NEAR(bandwidthOctavesToQ(2.0), 2.0 / 3.0, 1e-9);       // 2 octave：2/(4-1)
    DAWLAB_CHECK_NEAR(bandwidthOctavesToQ(1.0 / 3.0), 4.3185, 5e-4);   // 1/3 octave（graphic EQ 常見）
    DAWLAB_CHECK_NEAR(qToBandwidthOctaves(std::sqrt(2.0)), 1.0, 1e-9);
    DAWLAB_CHECK_NEAR(qToBandwidthOctaves(0.707), 1.9002, 5e-4);
    for (double q : {0.3, 0.707, 1.0, 2.5, 12.0}) // 來回換算要回到原點
        DAWLAB_CHECK_NEAR(bandwidthOctavesToQ(qToBandwidthOctaves(q)), q, 1e-9);

    // ---- 頻寬邊緣 ----
    double f1 = 0.0, f2 = 0.0;
    bandEdges(1000.0, std::sqrt(2.0), f1, f2);
    std::printf("\n  f0 = 1000 Hz, Q = sqrt(2)（1 octave）：邊緣 = %.1f Hz / %.1f Hz\n", f1, f2);
    DAWLAB_CHECK_NEAR(f1, 1000.0 / std::sqrt(2.0), 0.01); // 707.1
    DAWLAB_CHECK_NEAR(f2, 1000.0 * std::sqrt(2.0), 0.01); // 1414.2
    DAWLAB_CHECK_NEAR(f1 * f2, 1000.0 * 1000.0, 1e-6);    // 幾何對稱：f1·f2 = f0²
    DAWLAB_CHECK_NEAR(f2 - f1, 1000.0 / std::sqrt(2.0), 1e-6); // f2 − f1 = f0 / Q

    // ---- 用真的 Biquad 驗證：peak 在邊緣是「中點增益」，bandpass 在邊緣是 -3 dB ----
    const double gainDb = 12.0;
    const BiquadCoefficients peak = BiquadCoefficients::makePeak(sr, 1000.0, std::sqrt(2.0), gainDb);
    const BiquadCoefficients bp = BiquadCoefficients::makeBandpass(sr, 1000.0, std::sqrt(2.0));
    std::printf("  peak +%.0f dB：|H| @ %.1f = %+.3f dB, @ 1000 = %+.3f dB, @ %.1f = %+.3f dB\n", gainDb, f1,
                peak.magnitudeDb(f1, sr), peak.magnitudeDb(1000.0, sr), f2, peak.magnitudeDb(f2, sr));
    std::printf("  bandpass    ：|H| @ %.1f = %+.3f dB, @ 1000 = %+.3f dB, @ %.1f = %+.3f dB\n", f1,
                bp.magnitudeDb(f1, sr), bp.magnitudeDb(1000.0, sr), f2, bp.magnitudeDb(f2, sr));
    DAWLAB_CHECK_NEAR(peak.magnitudeDb(1000.0, sr), gainDb, 0.01);
    DAWLAB_CHECK_NEAR(peak.magnitudeDb(f1, sr), gainDb * 0.5, 0.1); // 中點增益（bilinear 在 1 kHz 的誤差 < 0.05 dB）
    DAWLAB_CHECK_NEAR(peak.magnitudeDb(f2, sr), gainDb * 0.5, 0.1);
    DAWLAB_CHECK_NEAR(bp.magnitudeDb(f1, sr), -3.01, 0.1);
    DAWLAB_CHECK_NEAR(bp.magnitudeDb(f2, sr), -3.01, 0.1);

    // 真的把正弦波送進 filter 量（不是只算公式）
    Biquad filter;
    filter.prepare(sr, 512, 1);
    filter.setCoefficients(peak);
    const double measured = measureGainDb([&](float x) { return filter.processSample(0, x); }, f2, sr);
    std::printf("  實測 peak @ %.1f Hz = %+.3f dB\n", f2, measured);
    DAWLAB_CHECK_NEAR(measured, gainDb * 0.5, 0.1);

    // ---- 練習 1（計算題）的手算對照 ----
    std::printf("\n  練習 1 對照：\n");
    std::printf("    A(+6 dB) = %.4f, A(-12 dB) = %.4f\n", std::pow(10.0, 6.0 / 40.0), std::pow(10.0, -12.0 / 40.0));
    DAWLAB_CHECK_NEAR(std::pow(10.0, 6.0 / 40.0), 1.4125, 1e-4);
    DAWLAB_CHECK_NEAR(std::pow(10.0, -12.0 / 40.0), 0.5012, 1e-4);
    std::printf("    BW = 2/3 oct -> Q = %.4f；Q = 2 -> BW = %.4f oct\n", bandwidthOctavesToQ(2.0 / 3.0),
                qToBandwidthOctaves(2.0));
    DAWLAB_CHECK_NEAR(bandwidthOctavesToQ(2.0 / 3.0), 2.1449, 1e-4);
    DAWLAB_CHECK_NEAR(qToBandwidthOctaves(2.0), 0.7140, 1e-4);
    // fs = 48 kHz、f0 = 12 kHz（ω0 = π/2：cos = 0、sin = 1）、Q = 1、+6 dB 的 peak，手算的正規化係數
    const BiquadCoefficients hand = BiquadCoefficients::makePeak(sr, 12000.0, 1.0, 6.0);
    // cos(π/2) 在 double 裡是 6e-17 而不是 0，印出來會變成 "-0.00000"；顯示時把這種捨入誤差歸零
    auto tidy = [](double v) { return std::abs(v) < 1e-12 ? 0.0 : v; };
    std::printf("    peak 12 kHz：b0 = %.5f b1 = %.5f b2 = %.5f a1 = %.5f a2 = %.5f\n", hand.b0, tidy(hand.b1), hand.b2,
                tidy(hand.a1), hand.a2);
    DAWLAB_CHECK_NEAR(hand.b0, 1.26019, 1e-4);
    DAWLAB_CHECK_NEAR(hand.b1, 0.0, 1e-9);
    DAWLAB_CHECK_NEAR(hand.b2, 0.21694, 1e-4);
    DAWLAB_CHECK_NEAR(hand.a1, 0.0, 1e-9);
    DAWLAB_CHECK_NEAR(hand.a2, 0.47713, 1e-4);
    DAWLAB_CHECK_NEAR(hand.magnitudeDb(12000.0, sr), 6.0, 1e-6); // 中心 = A² = +6 dB
    DAWLAB_CHECK_NEAR(hand.magnitudeDb(0.0, sr), 0.0, 1e-9);     // DC = 0 dB

    return dawlab::test::summary("l03_ex2");
}
