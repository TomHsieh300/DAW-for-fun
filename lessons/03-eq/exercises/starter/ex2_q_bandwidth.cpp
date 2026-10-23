// 練習 2（起始碼）：Q ↔ bandwidth（octave）換算，並用 Biquad 實際驗證「頻寬邊緣」的增益
//
// 目標：
//   1. 實作 qToBandwidthOctaves / bandwidthOctavesToQ / bandEdges（公式見 README 2.4 節）。
//   2. 用 BiquadCoefficients::makePeak 驗證：Q = √2 的 peak 在 f0·2^(±1/2) 的增益是 gainDb / 2。
// 執行時 self-check 會印出 FAIL，直到你把 TODO 補完。

#include <dawlab/Common.h>
#include <dawlab/dsp/Biquad.h>

#include <cstdio>

using namespace dawlab;
using namespace dawlab::dsp;

namespace {

// TODO: BW = (2 / ln 2) · asinh(1 / (2Q))
double qToBandwidthOctaves(double q) {
    (void)q;
    return 0.0;
}

// TODO: Q = 2^(BW/2) / (2^BW − 1)
double bandwidthOctavesToQ(double bw) {
    (void)bw;
    return 0.0;
}

// TODO: f_{1,2} = f0 · ( sqrt(1 + 1/(4Q²)) ∓ 1/(2Q) )
void bandEdges(double f0, double q, double& fLow, double& fHigh) {
    (void)q;
    fLow = f0;
    fHigh = f0;
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

    DAWLAB_CHECK_NEAR(bandwidthOctavesToQ(1.0), std::sqrt(2.0), 1e-9);
    DAWLAB_CHECK_NEAR(bandwidthOctavesToQ(2.0), 2.0 / 3.0, 1e-9);
    DAWLAB_CHECK_NEAR(bandwidthOctavesToQ(1.0 / 3.0), 4.3185, 5e-4);
    DAWLAB_CHECK_NEAR(qToBandwidthOctaves(std::sqrt(2.0)), 1.0, 1e-9);
    DAWLAB_CHECK_NEAR(qToBandwidthOctaves(0.707), 1.9002, 5e-4);
    for (double q : {0.3, 0.707, 1.0, 2.5, 12.0})
        DAWLAB_CHECK_NEAR(bandwidthOctavesToQ(qToBandwidthOctaves(q)), q, 1e-9);

    double f1 = 0.0, f2 = 0.0;
    bandEdges(1000.0, std::sqrt(2.0), f1, f2);
    std::printf("\n  f0 = 1000 Hz, Q = sqrt(2)（1 octave）：邊緣 = %.1f Hz / %.1f Hz\n", f1, f2);
    DAWLAB_CHECK_NEAR(f1, 1000.0 / std::sqrt(2.0), 0.01);
    DAWLAB_CHECK_NEAR(f2, 1000.0 * std::sqrt(2.0), 0.01);
    DAWLAB_CHECK_NEAR(f1 * f2, 1000.0 * 1000.0, 1e-6);
    DAWLAB_CHECK_NEAR(f2 - f1, 1000.0 / std::sqrt(2.0), 1e-6);

    // TODO: 用 BiquadCoefficients::makePeak(sr, 1000, sqrt(2), 12) 與 makeBandpass 驗證
    //       邊緣處 peak = +6 dB（中點增益）、bandpass = -3 dB；再用 measureGainDb 實測一次。
    const double gainDb = 12.0;
    const BiquadCoefficients peak = BiquadCoefficients::makePeak(sr, 1000.0, std::sqrt(2.0), gainDb);
    DAWLAB_CHECK_NEAR(peak.magnitudeDb(f1, sr), gainDb * 0.5, 0.1);
    DAWLAB_CHECK_NEAR(peak.magnitudeDb(f2, sr), gainDb * 0.5, 0.1);

    return dawlab::test::summary("l03_ex2_starter");
}
