// 練習 4 起始碼：normalize 到指定 peak 與計算 clipping sample 數。
//
// 完成下面兩個 // TODO 的函式，讓所有 self-check 通過。
// 執行：bin/Debug/l00_ex4_starter.exe（未完成時會印出 FAIL，這是預期的）
// 提示：dawlab::peakAbs、dawlab::dbToGain、dawlab::gainToDb、AudioBuffer::applyGain。

#include <dawlab/Common.h>

#include <cmath>
#include <cstdio>

namespace {

// 計算 |x| >= threshold 的 sample 數（所有 channel 加總）。
int countClippedSamples(const dawlab::AudioBuffer& buffer, float threshold = 1.0f) {
    // TODO: 走訪每個 channel、每個 sample，計算 std::abs(x) >= threshold 的個數
    (void)buffer;
    (void)threshold;
    return -1;
}

// 把整個 buffer 的 peak 調整到 targetPeakDb（所有 channel 乘同一個 gain）。
// 回傳套用的 gain（dB）。全靜音的 buffer 不動，回傳 0。
float normalizeToPeakDb(dawlab::AudioBuffer& buffer, float targetPeakDb) {
    // TODO:
    //   1. 找出 peak = 最大的 |sample|（提示：dawlab::peakAbs(buffer)）
    //   2. 若 peak <= 0 直接回傳 0（不能除以 0）
    //   3. gain = dbToGain(targetPeakDb) / peak，套用到整個 buffer
    //   4. 回傳 gainToDb(gain)
    (void)buffer;
    (void)targetPeakDb;
    return 0.0f;
}

} // namespace

int main() {
    using namespace dawlab;
    constexpr double sr = 48000.0;

    // 1. 振幅 0.25（-12.04 dBFS）的正弦波 normalize 到 -1 dBFS
    AudioBuffer sine = makeSine(440.0, sr, 1.0, 0.25f);
    const float applied = normalizeToPeakDb(sine, -1.0f);
    std::printf("正弦波 0.25 → -1 dBFS：套用 %+.2f dB，結果 peak = %.4f（%.2f dBFS）\n", applied, peakAbs(sine),
                gainToDb(peakAbs(sine)));
    DAWLAB_CHECK_NEAR(peakAbs(sine), dbToGain(-1.0f), 1e-5);
    DAWLAB_CHECK_NEAR(applied, -1.0 - gainToDb(0.25f), 0.01); // -1 - (-12.04) = +11.04 dB
    DAWLAB_CHECK(countClippedSamples(sine) == 0);

    // 2. 兩個 channel 音量不同：normalize 以「最大的那個 channel」為準，左右比例不變
    AudioBuffer stereo = makeSine(440.0, sr, 1.0, 0.5f);
    AudioBlock(stereo).getChannelSubset(1, 1).multiplyBy(0.5f); // 右聲道 -6 dB
    const float ratioBefore = peakAbs(stereo.getReadPointer(1), stereo.getNumSamples()) /
                              peakAbs(stereo.getReadPointer(0), stereo.getNumSamples());
    normalizeToPeakDb(stereo, 0.0f);
    const float ratioAfter = peakAbs(stereo.getReadPointer(1), stereo.getNumSamples()) /
                             peakAbs(stereo.getReadPointer(0), stereo.getNumSamples());
    DAWLAB_CHECK_NEAR(peakAbs(stereo), 1.0, 1e-5);
    DAWLAB_CHECK_NEAR(ratioAfter, ratioBefore, 1e-5);

    // 3. 靜音 buffer：不可以產生 NaN
    AudioBuffer silence(2, 1000);
    const float silentGain = normalizeToPeakDb(silence, -1.0f);
    DAWLAB_CHECK(silentGain == 0.0f);
    DAWLAB_CHECK(!hasNanOrInf(silence));
    DAWLAB_CHECK(peakAbs(silence) == 0.0f);

    // 4. clipping 計數：振幅 1.5 的 1 kHz 正弦波，每個週期 48 個 sample 裡有 26 個 |x| >= 1
    AudioBuffer loud = makeSine(1000.0, sr, 1.0, 1.5f, 1);
    const int clipped = countClippedSamples(loud);
    std::printf("振幅 1.5 的正弦波：%d / %d samples 超過 ±1.0\n", clipped, loud.getNumSamples());
    DAWLAB_CHECK(clipped == 26000);
    normalizeToPeakDb(loud, -0.1f);
    DAWLAB_CHECK(countClippedSamples(loud) == 0);
    DAWLAB_CHECK_NEAR(peakAbs(loud), dbToGain(-0.1f), 1e-5);

    // 5. 真實素材：鼓組 loop normalize 到 -1 dBFS 並輸出
    AudioBuffer drums = makeDrumLoop(sr);
    normalizeToPeakDb(drums, -1.0f);
    DAWLAB_CHECK_NEAR(gainToDb(peakAbs(drums)), -1.0, 0.01);
    writeWav(outputPath("l00_ex4_normalized.wav"), drums, sr);

    return dawlab::test::summary("l00_ex4");
}
