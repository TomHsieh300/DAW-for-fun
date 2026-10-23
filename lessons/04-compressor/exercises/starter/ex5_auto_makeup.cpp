// 練習 5（起始碼）：auto makeup gain。
//
// 目標：
//   1. 實作 autoMakeupDb()：讓「0 dBFS 的輸入」經過靜態曲線後回到 0 dBFS 所需的 makeup（dB）；
//   2. 實作 steadyOutputPeakDb()：用真的 Compressor + makeup 跑一個正弦波，量穩態輸出 peak；
//   3. 通過 self-check（例如 T -20 / R 4 / hard knee → makeup = 15 dB）。
// 完成 TODO 之後，self-check 全部通過就算完成。起始碼執行時會印出 FAIL，這是預期的。

#include <dawlab/Common.h>
#include <dawlab/dsp/Compressor.h>

#include <cstdio>

using namespace dawlab;
using dawlab::dsp::Compressor;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

// 讓 0 dBFS 輸入 → 0 dBFS 輸出所需的 makeup gain（dB）
float autoMakeupDb(float thresholdDb, float ratio, float kneeDb) {
    (void)thresholdDb;
    (void)ratio;
    (void)kneeDb;
    // TODO: 用 Compressor::computeOutputDb(0, T, R, W) 算出 0 dBFS 會被壓到幾 dB，再取負號
    return 0.0f;
}

// 跑一個正弦波（1 秒），回傳穩態（最後 200 ms）的輸出 peak（dBFS）
float steadyOutputPeakDb(float inputDb, float thresholdDb, float ratio, float kneeDb, float makeupDb) {
    (void)thresholdDb;
    (void)ratio;
    (void)kneeDb;
    (void)makeupDb;
    AudioBuffer buf = makeSine(1000.0, kSampleRate, 1.0, dbToGain(inputDb), 1);
    // TODO: 建立 Compressor（attack 1 ms、release 100 ms、setMakeupGainDb(makeupDb)），逐 block 處理 buf
    const int tail = msToSamples(200.0, kSampleRate);
    return gainToDb(peakAbs(buf.getReadPointer(0) + buf.getNumSamples() - tail, tail));
}

} // namespace

int main() {
    std::printf("練習 5（起始碼）：auto makeup\n");

    struct Case {
        float thresholdDb, ratio, kneeDb, expectedMakeupDb;
    };
    const Case cases[] = {
        {-20.0f, 4.0f, 0.0f, 15.0f},
        {-20.0f, 4.0f, 10.0f, 15.0f},
        {-30.0f, 2.0f, 0.0f, 15.0f},
        {-10.0f, 10.0f, 0.0f, 9.0f},
        {-6.0f, 4.0f, 20.0f, 4.8f}, // 0 dBFS 落在 knee 內，要用二次曲線那段算
    };
    for (const Case& c : cases) {
        const float m = autoMakeupDb(c.thresholdDb, c.ratio, c.kneeDb);
        std::printf("  T %5.0f dB, R %4.0f:1, knee %4.0f dB → auto makeup %.3f dB（預期 %.3f）\n", c.thresholdDb,
                    c.ratio, c.kneeDb, m, c.expectedMakeupDb);
        DAWLAB_CHECK_NEAR(m, c.expectedMakeupDb, 1e-4);
    }

    const float makeup = autoMakeupDb(-20.0f, 4.0f, 0.0f);
    const float out0 = steadyOutputPeakDb(0.0f, -20.0f, 4.0f, 0.0f, makeup);
    const float out6 = steadyOutputPeakDb(-6.0f, -20.0f, 4.0f, 0.0f, makeup);
    const float expected6 = Compressor::computeOutputDb(-6.0f, -20.0f, 4.0f, 0.0f) + makeup;
    std::printf("  實跑：0 dBFS 正弦 → %.2f dBFS（預期 0.00）；-6 dBFS 正弦 → %.2f dBFS（預期 %.2f）\n", out0, out6,
                expected6);
    DAWLAB_CHECK_NEAR(out0, 0.0, 0.2);
    DAWLAB_CHECK_NEAR(out6, expected6, 0.2);
    return dawlab::test::summary("l04_ex5_starter");
}
