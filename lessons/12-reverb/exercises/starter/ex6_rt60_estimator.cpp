// 練習 6 起始碼：Schroeder backward integration 估 RT60
//
// 目標：
//   1. 完成 schroederDecayDb()：EDC[n] = Σ_{k=n}^{N-1} h[k]²，轉成 dB（相對 EDC[0]）。
//      提示：從尾巴往回累加，一次迴圈就好（O(N)）。
//   2. 完成 estimateRt60()：在 EDC_dB 介於 startDb .. endDb 的區段做線性回歸（t 對 dB），
//      斜率 s（dB/s）→ RT60 = -60 / s。
//   3. self-check：理想指數衰減正弦的 EDC 在 t = RT60/2 時是 -30 dB；估計值與真值差 3% 以內。
//
// 執行時 self-check 會印 FAIL，完成 TODO 後應該全部 PASS。

#include <dawlab/Common.h>
#include <dawlab/dsp/ConvolutionReverb.h>
#include <dawlab/dsp/ReverbBuildingBlocks.h>

#include <cmath>
#include <cstdio>
#include <vector>

using namespace dawlab;
using namespace dawlab::dsp;

namespace {

constexpr double kSampleRate = 48000.0;

std::vector<double> schroederDecayDb(const float* h, int n) {
    std::vector<double> edc(static_cast<size_t>(n), 0.0);
    // TODO: backward integration + 轉 dB
    (void)h;
    return edc;
}

struct Rt60Estimate {
    double rt60 = 0.0;
    double slopeDbPerSecond = 0.0;
    bool valid = false;
};

Rt60Estimate estimateRt60(const std::vector<double>& edcDb, double sampleRate, double startDb = -5.0,
                          double endDb = -25.0) {
    Rt60Estimate r;
    // TODO: 線性回歸（只取 edcDb 在 startDb..endDb 之間的點），RT60 = -60 / slope
    (void)edcDb;
    (void)sampleRate;
    (void)startDb;
    (void)endDb;
    return r;
}

AudioBuffer makeDecayingSine(double freq, double rt60, double seconds) {
    AudioBuffer b(1, secondsToSamples(seconds, kSampleRate));
    const double k = std::log(1000.0) / rt60;
    for (int i = 0; i < b.getNumSamples(); ++i) {
        const double t = i / kSampleRate;
        b.setSample(0, i, static_cast<float>(std::exp(-k * t) * std::sin(kTwoPi * freq * t)));
    }
    return b;
}

} // namespace

int main() {
    std::printf("=== 練習 6：Schroeder backward integration（起始碼）===\n");

    {
        AudioBuffer sine = makeDecayingSine(440.0, 1.0, 1.3);
        const std::vector<double> edc = schroederDecayDb(sine.getReadPointer(0), sine.getNumSamples());
        const double at05 = edc[static_cast<size_t>(secondsToSamples(0.5, kSampleRate))];
        std::printf("指數衰減正弦（RT60 1 s）的 EDC @0.5 s = %.2f dB（理論 -30）\n", at05);
        DAWLAB_CHECK_NEAR(at05, -30.0, 0.5);
        const Rt60Estimate e = estimateRt60(edc, kSampleRate);
        std::printf("估計 RT60 = %.3f s\n", e.rt60);
        DAWLAB_CHECK(e.valid);
        DAWLAB_CHECK_NEAR(e.rt60, 1.0, 0.03);
    }
    for (double truth : {0.5, 1.5}) {
        AudioBuffer ir = ConvolutionReverb::makeSyntheticImpulseResponse(kSampleRate, truth, 1, 11);
        const Rt60Estimate e = estimateRt60(schroederDecayDb(ir.getReadPointer(0), ir.getNumSamples()), kSampleRate);
        std::printf("合成 IR 真值 %.1f s → 估計 %.3f s\n", truth, e.rt60);
        DAWLAB_CHECK(e.valid);
        DAWLAB_CHECK_NEAR(e.rt60 / truth, 1.0, 0.1);
    }

    return dawlab::test::summary("l12_ex6_starter");
}
