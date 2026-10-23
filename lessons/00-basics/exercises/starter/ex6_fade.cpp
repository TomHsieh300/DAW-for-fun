// 練習 6 起始碼：linear vs equal-power 的 fade in / fade out 與 crossfade。
//
// 完成 fadeGain、applyFadeIn、applyFadeOut 三個 // TODO，讓所有 self-check 通過。
// 執行：bin/Debug/l00_ex6_starter.exe（未完成時會印出 FAIL，這是預期的）

#include <dawlab/Common.h>

#include <cmath>
#include <cstdio>

namespace {

enum class FadeShape { Linear, EqualPower };

// 位置 t ∈ [0, 1] 的 fade-in 增益：
//   Linear      g(t) = t
//   EqualPower  g(t) = sin(t · π/2)
// fade-out 用 1 - t 代入。
float fadeGain(float t, FadeShape shape) {
    t = std::clamp(t, 0.0f, 1.0f);
    // TODO: 依 shape 回傳對應的增益（提示：dawlab::kPi 是 double，要轉成 float）
    (void)shape;
    return 0.0f;
}

// 對 buffer 開頭 numSamples 個 sample 做 fade-in（gain 從 0 升到 1）。
// 第 i 個 sample 的 t = i / (n - 1)，所以第 0 個是 0、第 n-1 個是 1。
void applyFadeIn(dawlab::AudioBuffer& buffer, int numSamples, FadeShape shape) {
    const int n = std::min(numSamples, buffer.getNumSamples());
    if (n <= 1)
        return;
    // TODO: 對每個 channel 的前 n 個 sample 乘上 fadeGain(t, shape)
    (void)buffer;
    (void)shape;
}

// 對 buffer 結尾 numSamples 個 sample 做 fade-out（gain 從 1 降到 0）
void applyFadeOut(dawlab::AudioBuffer& buffer, int numSamples, FadeShape shape) {
    const int total = buffer.getNumSamples();
    const int n = std::min(numSamples, total);
    if (n <= 1)
        return;
    // TODO: 對每個 channel 的最後 n 個 sample 乘上 fadeGain(1 - t, shape)
    (void)shape;
}

// 把 a 整段 fade-out、b 整段 fade-in 之後相加 = crossfade
dawlab::AudioBuffer crossfade(dawlab::AudioBuffer a, dawlab::AudioBuffer b, FadeShape shape) {
    const int n = std::min(a.getNumSamples(), b.getNumSamples());
    applyFadeOut(a, n, shape);
    applyFadeIn(b, n, shape);
    a.addFrom(0, 0, b, 0, 0, n);
    if (a.getNumChannels() > 1)
        a.addFrom(1, 0, b, std::min(1, b.getNumChannels() - 1), 0, n);
    return a;
}

// 量測 buffer 中央 10% 區段的 RMS（第 0 channel）
float middleRmsDb(const dawlab::AudioBuffer& b) {
    const int n = b.getNumSamples();
    const int len = n / 10;
    return dawlab::gainToDb(dawlab::rms(b.getReadPointer(0) + (n - len) / 2, len));
}

} // namespace

int main() {
    using namespace dawlab;
    constexpr double sr = 48000.0;

    // 1. 增益曲線本身：中點與端點
    DAWLAB_CHECK_NEAR(fadeGain(0.5f, FadeShape::Linear), 0.5, 1e-6);
    DAWLAB_CHECK_NEAR(fadeGain(0.5f, FadeShape::EqualPower), std::sqrt(0.5), 1e-6);
    DAWLAB_CHECK_NEAR(fadeGain(1.0f, FadeShape::EqualPower), 1.0, 1e-6);
    for (float t : {0.1f, 0.3f, 0.5f, 0.8f}) {
        const float gi = fadeGain(t, FadeShape::EqualPower), go = fadeGain(1.0f - t, FadeShape::EqualPower);
        DAWLAB_CHECK_NEAR(gi * gi + go * go, 1.0, 1e-5); // 功率守恆
    }

    // 2. fade-in / fade-out 套在常數訊號（DC = 1.0）上，直接讀 sample 看曲線
    const int fadeLen = 1001;
    AudioBuffer dc(1, 4000);
    for (int i = 0; i < 4000; ++i)
        dc.setSample(0, i, 1.0f);
    AudioBuffer dcIn = dc, dcOut = dc;
    applyFadeIn(dcIn, fadeLen, FadeShape::Linear);
    applyFadeOut(dcOut, fadeLen, FadeShape::EqualPower);
    DAWLAB_CHECK_NEAR(dcIn.getSample(0, 0), 0.0, 1e-6);
    DAWLAB_CHECK_NEAR(dcIn.getSample(0, 500), 0.5, 1e-6);
    DAWLAB_CHECK_NEAR(dcIn.getSample(0, 1000), 1.0, 1e-6);
    DAWLAB_CHECK_NEAR(dcIn.getSample(0, 1001), 1.0, 1e-6);
    DAWLAB_CHECK_NEAR(dcOut.getSample(0, 4000 - fadeLen - 1), 1.0, 1e-6);
    DAWLAB_CHECK_NEAR(dcOut.getSample(0, 4000 - fadeLen), 1.0, 1e-6);
    DAWLAB_CHECK_NEAR(dcOut.getSample(0, 4000 - fadeLen + 500), std::sqrt(0.5), 1e-6);
    DAWLAB_CHECK_NEAR(dcOut.getSample(0, 3999), 0.0, 1e-6);

    // 3. crossfade 兩段「不相關」的白雜訊：equal-power 中段音量不變、linear 中段掉 3 dB
    AudioBuffer n1 = makeWhiteNoise(sr, 2.0, 0.3f, 2, 1);
    AudioBuffer n2 = makeWhiteNoise(sr, 2.0, 0.3f, 2, 999);
    const float refDb = middleRmsDb(n1);
    AudioBuffer xfLinear = crossfade(n1, n2, FadeShape::Linear);
    AudioBuffer xfEqual = crossfade(n1, n2, FadeShape::EqualPower);
    const float linDip = middleRmsDb(xfLinear) - refDb;
    const float eqDip = middleRmsDb(xfEqual) - refDb;
    std::printf("不相關素材 crossfade 中段 RMS 變化：linear %+.2f dB，equal-power %+.2f dB\n", linDip, eqDip);
    DAWLAB_CHECK_NEAR(linDip, -3.01, 0.25);
    DAWLAB_CHECK_NEAR(eqDip, 0.0, 0.25);

    // 4. crossfade 兩段「相同」的訊號：linear 剛好不變，equal-power 反而 +3 dB
    AudioBuffer s1 = makeSine(440.0, sr, 2.0, 0.3f, 2);
    const float sineRef = middleRmsDb(s1);
    DAWLAB_CHECK_NEAR(middleRmsDb(crossfade(s1, s1, FadeShape::Linear)) - sineRef, 0.0, 0.05);
    DAWLAB_CHECK_NEAR(middleRmsDb(crossfade(s1, s1, FadeShape::EqualPower)) - sineRef, 3.01, 0.1);

    writeWav(outputPath("l00_ex6_crossfade_linear.wav"), xfLinear, sr);
    writeWav(outputPath("l00_ex6_crossfade_equal_power.wav"), xfEqual, sr);
    return dawlab::test::summary("l00_ex6");
}
