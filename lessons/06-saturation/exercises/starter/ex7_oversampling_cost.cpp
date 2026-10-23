// 練習 7（★★☆）：比較 1x / 2x / 4x / 8x oversampling 的 alias 能量與 CPU 成本
//
// 目標：用 dawlab::dsp::Saturation（Tanh、drive +18 dB）
//   1. 寫 aliasDb(log2)：7 kHz、peak 0.5 的正弦處理後，所有 alias 位置（奇數 kHz，扣掉 7、21 kHz）的功率總和 / 基頻功率（dB）
//   2. 寫 timeMs(log2, noise)：處理 10 秒 stereo noise 所花的毫秒數（std::chrono::steady_clock，跑 3 次取最小值）
//   3. 觀察：alias 隨倍率下降、時間隨倍率上升；4x 之後還值得嗎？
//
// 提示：
//   - 為什麼 alias 都在奇數 kHz？見 README 2.5 節（gcd(7000, 48000) = 1000、tanh 只有奇次諧波）。
//   - 先跳過開頭 0.1 秒（latency 與暫態）再量。
//   - 計時時不要把「複製 buffer」「prepare」算進去。
//
// 完成所有 TODO 之前，執行時會印出 FAIL（這是預期的）。
// 執行：l06_ex7_starter

#include <dawlab/Common.h>
#include <dawlab/dsp/Saturation.h>

#include <chrono>
#include <cstdio>
#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr double kF0 = 7000.0;

using dawlab::dsp::Saturation;
using dawlab::dsp::SaturationCurve;

[[maybe_unused]] void configure(Saturation& sat, int log2, int numChannels) {
    sat.setOversamplingFactorLog2(log2);
    sat.prepare(kSampleRate, kBlockSize, numChannels);
    sat.setCurve(SaturationCurve::Tanh);
    sat.setDriveDb(18.0f);
    sat.setMix(1.0f);
    sat.setOutputGainDb(-6.0f);
    sat.reset();
}

[[maybe_unused]] void processAll(Saturation& sat, dawlab::AudioBuffer& b) {
    dawlab::AudioBlock whole(b);
    for (int start = 0; start < b.getNumSamples(); start += kBlockSize)
        sat.process(whole.getSubBlock(start, std::min(kBlockSize, b.getNumSamples() - start)));
}

// TODO 1
double aliasDb(int log2) {
    (void)log2;
    return 0.0;
}

// TODO 2
double timeMs(int log2, const dawlab::AudioBuffer& noise) {
    (void)log2;
    (void)noise;
    return 0.0;
}

} // namespace

int main() {
    const double seconds = 10.0;
    const dawlab::AudioBuffer noise = dawlab::makeWhiteNoise(kSampleRate, seconds, 0.5f, 2);

    std::vector<double> alias, ms;
    for (int log2 = 0; log2 <= 3; ++log2) {
        alias.push_back(aliasDb(log2));
        ms.push_back(timeMs(log2, noise));
        std::printf("%dx：alias %.1f dB、%.1f ms\n", 1 << log2, alias.back(), ms.back());
    }
    DAWLAB_CHECK(alias[1] < alias[0] - 10.0);
    DAWLAB_CHECK(alias[2] < alias[1] - 20.0);
    DAWLAB_CHECK(alias[3] <= alias[2] + 1.0);
    DAWLAB_CHECK(alias[2] < -70.0);
    DAWLAB_CHECK(ms[2] > ms[0] * 1.5);
    DAWLAB_CHECK(ms[3] > ms[1]);
    return dawlab::test::summary("l06_ex7_starter");
}
