// 練習 5 解答：auto makeup gain。
//
// 想法：靜態曲線告訴我們「0 dBFS 的輸入會被壓到幾 dB」，把這個量補回來，0 dBFS 進就 0 dBFS 出：
//   makeup = -computeOutputDb(0, T, R, W)
// 很多 plugin 只補一半（makeup / 2），因為真實音樂的 peak 很少一直在 0 dBFS，補滿會太大聲。
// self-check：
//   - T -20 / R 4 / hard knee → makeup = 15 dB；soft knee 10 dB 時 0 dBFS 在 knee 之外，makeup 一樣 15 dB
//   - 用 0 dBFS 正弦波跑真的 Compressor + makeup → 穩態輸出 peak ≈ 0 dBFS
//   - -6 dBFS 正弦波 → 輸出 = computeOutputDb(-6) + 15 = -1.5 dBFS

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
    return -Compressor::computeOutputDb(0.0f, thresholdDb, ratio, kneeDb);
}

// 跑一個正弦波，回傳穩態（最後 200 ms）的輸出 peak（dBFS）
float steadyOutputPeakDb(float inputDb, float thresholdDb, float ratio, float kneeDb, float makeupDb) {
    Compressor comp;
    comp.prepare(kSampleRate, kBlockSize, 1);
    comp.setThresholdDb(thresholdDb);
    comp.setRatio(ratio);
    comp.setKneeDb(kneeDb);
    comp.setAttackMs(1.0f);
    comp.setReleaseMs(100.0f);
    comp.setMakeupGainDb(makeupDb);
    AudioBuffer buf = makeSine(1000.0, kSampleRate, 1.0, dbToGain(inputDb), 1);
    AudioBlock whole(buf);
    for (int start = 0; start < buf.getNumSamples(); start += kBlockSize)
        comp.process(whole.getSubBlock(start, std::min(kBlockSize, buf.getNumSamples() - start)));
    const int tail = msToSamples(200.0, kSampleRate);
    return gainToDb(peakAbs(buf.getReadPointer(0) + buf.getNumSamples() - tail, tail));
}

} // namespace

int main() {
    std::printf("練習 5：auto makeup\n");

    struct Case {
        float thresholdDb, ratio, kneeDb, expectedMakeupDb;
    };
    const Case cases[] = {
        {-20.0f, 4.0f, 0.0f, 15.0f},   // 0 - (-20 + 20/4) = 15
        {-20.0f, 4.0f, 10.0f, 15.0f},  // 0 dBFS 在 knee（-25..-15）之外，和 hard knee 一樣
        {-30.0f, 2.0f, 0.0f, 15.0f},   // 0 - (-30 + 30/2) = 15
        {-10.0f, 10.0f, 0.0f, 9.0f},   // 0 - (-10 + 10/10) = 9
        {-6.0f, 4.0f, 20.0f, 4.8f},    // 0 dBFS 落在 knee（-16..+4）內：0 + (1/4-1)·(0+6+10)²/(2·20) = -0.75·256/40 = -4.8
    };
    for (const Case& c : cases) {
        const float m = autoMakeupDb(c.thresholdDb, c.ratio, c.kneeDb);
        std::printf("  T %5.0f dB, R %4.0f:1, knee %4.0f dB → auto makeup %.3f dB（預期 %.3f）\n", c.thresholdDb,
                    c.ratio, c.kneeDb, m, c.expectedMakeupDb);
        DAWLAB_CHECK_NEAR(m, c.expectedMakeupDb, 1e-4);
    }

    // 動態驗證：0 dBFS 正弦波 + auto makeup → 穩態輸出 0 dBFS
    const float makeup = autoMakeupDb(-20.0f, 4.0f, 0.0f);
    const float out0 = steadyOutputPeakDb(0.0f, -20.0f, 4.0f, 0.0f, makeup);
    const float out6 = steadyOutputPeakDb(-6.0f, -20.0f, 4.0f, 0.0f, makeup);
    const float expected6 = Compressor::computeOutputDb(-6.0f, -20.0f, 4.0f, 0.0f) + makeup;
    std::printf("  實跑：0 dBFS 正弦 → %.2f dBFS（預期 0.00）；-6 dBFS 正弦 → %.2f dBFS（預期 %.2f）\n", out0, out6,
                expected6);
    DAWLAB_CHECK_NEAR(out0, 0.0, 0.2);
    DAWLAB_CHECK_NEAR(out6, expected6, 0.2);

    // 沒有 makeup 的對照
    std::printf("  對照（無 makeup）：0 dBFS 正弦 → %.2f dBFS\n", steadyOutputPeakDb(0.0f, -20.0f, 4.0f, 0.0f, 0.0f));
    std::printf("  「補一半」版本：makeup/2 = %.2f dB\n", makeup * 0.5f);
    return dawlab::test::summary("l04_ex5");
}
