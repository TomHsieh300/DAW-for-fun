// 練習 4 解答：normalize 到指定 peak 與計算 clipping sample 數。
//
// 執行：bin/Debug/l00_ex4_solution.exe（ctest label: solution）
// 輸出：dawlab_output/l00_ex4_normalized.wav

#include <dawlab/Common.h>

#include <cmath>
#include <cstdio>

namespace {

// 計算 |x| >= threshold 的 sample 數（所有 channel 加總）。
// 在整數 PCM 的世界裡 threshold = 1.0 就是「撞到天花板」。
int countClippedSamples(const dawlab::AudioBuffer& buffer, float threshold = 1.0f) {
    int count = 0;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        const float* x = buffer.getReadPointer(ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (std::abs(x[i]) >= threshold)
                ++count;
    }
    return count;
}

// 把整個 buffer 的 peak 調整到 targetPeakDb（所有 channel 乘同一個 gain）。
// 回傳套用的 gain（dB）。全靜音的 buffer 不動，回傳 0。
float normalizeToPeakDb(dawlab::AudioBuffer& buffer, float targetPeakDb) {
    const float peak = dawlab::peakAbs(buffer);
    if (peak <= 0.0f)
        return 0.0f; // 不能除以 0；靜音 normalize 之後還是靜音
    // 想要 peak * gain == dbToGain(target)  →  gain = dbToGain(target) / peak
    const float gain = dawlab::dbToGain(targetPeakDb) / peak;
    buffer.applyGain(gain);
    return dawlab::gainToDb(gain);
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
    std::printf("立體聲：左 peak %.3f、右 peak %.3f（比例 %.3f，normalize 前 %.3f）\n",
                peakAbs(stereo.getReadPointer(0), stereo.getNumSamples()),
                peakAbs(stereo.getReadPointer(1), stereo.getNumSamples()), ratioAfter, ratioBefore);
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
    std::printf("振幅 1.5 的正弦波：%d / %d samples 超過 ±1.0（%.1f%%）\n", clipped, loud.getNumSamples(),
                100.0 * clipped / loud.getNumSamples());
    DAWLAB_CHECK(clipped == 26000);
    // normalize 到 -0.1 dBFS 之後就沒有任何 sample 超過 1.0 了
    normalizeToPeakDb(loud, -0.1f);
    DAWLAB_CHECK(countClippedSamples(loud) == 0);
    DAWLAB_CHECK_NEAR(peakAbs(loud), dbToGain(-0.1f), 1e-5);

    // 5. 真實素材：鼓組 loop normalize 到 -1 dBFS 並輸出
    AudioBuffer drums = makeDrumLoop(sr);
    const float drumsBefore = gainToDb(peakAbs(drums));
    const float drumsGain = normalizeToPeakDb(drums, -1.0f);
    std::printf("鼓組 loop：peak %.2f dBFS → 套用 %+.2f dB → %.2f dBFS\n", drumsBefore, drumsGain,
                gainToDb(peakAbs(drums)));
    DAWLAB_CHECK_NEAR(gainToDb(peakAbs(drums)), -1.0, 0.01);
    writeWav(outputPath("l00_ex4_normalized.wav"), drums, sr);

    return dawlab::test::summary("l00_ex4");
}
