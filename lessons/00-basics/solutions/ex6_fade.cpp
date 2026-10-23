// 練習 6 解答：linear vs equal-power 的 fade in / fade out 與 crossfade。
//
// 執行：bin/Debug/l00_ex6_solution.exe（ctest label: solution）
// 輸出：dawlab_output/l00_ex6_crossfade_linear.wav、l00_ex6_crossfade_equal_power.wav

#include <dawlab/Common.h>

#include <cmath>
#include <cstdio>

namespace {

enum class FadeShape { Linear, EqualPower };

// 位置 t ∈ [0, 1] 的 fade-in 增益：
//   Linear      g(t) = t                 → 中點 0.5   (-6.02 dB)
//   EqualPower  g(t) = sin(t · π/2)      → 中點 0.707 (-3.01 dB)
// fade-out 用 1 - t 代入，所以 equal-power 的 fade-out 是 cos(t · π/2)，sin² + cos² = 1 → 功率守恆。
float fadeGain(float t, FadeShape shape) {
    t = std::clamp(t, 0.0f, 1.0f);
    switch (shape) {
    case FadeShape::Linear:
        return t;
    case FadeShape::EqualPower:
        return std::sin(t * static_cast<float>(dawlab::kPi) * 0.5f);
    }
    return t;
}

// 對 buffer 開頭 numSamples 個 sample 做 fade-in（gain 從 0 升到 1）
void applyFadeIn(dawlab::AudioBuffer& buffer, int numSamples, FadeShape shape) {
    const int n = std::min(numSamples, buffer.getNumSamples());
    if (n <= 1)
        return;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        float* x = buffer.getWritePointer(ch);
        for (int i = 0; i < n; ++i)
            x[i] *= fadeGain(static_cast<float>(i) / static_cast<float>(n - 1), shape);
    }
}

// 對 buffer 結尾 numSamples 個 sample 做 fade-out（gain 從 1 降到 0）
void applyFadeOut(dawlab::AudioBuffer& buffer, int numSamples, FadeShape shape) {
    const int total = buffer.getNumSamples();
    const int n = std::min(numSamples, total);
    if (n <= 1)
        return;
    const int start = total - n;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        float* x = buffer.getWritePointer(ch);
        for (int i = 0; i < n; ++i)
            x[start + i] *= fadeGain(1.0f - static_cast<float>(i) / static_cast<float>(n - 1), shape);
    }
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
    std::printf("fade-in 曲線  t=0 / 0.5 / 1：linear %.3f / %.3f / %.3f，equal-power %.3f / %.3f / %.3f\n",
                fadeGain(0.0f, FadeShape::Linear), fadeGain(0.5f, FadeShape::Linear), fadeGain(1.0f, FadeShape::Linear),
                fadeGain(0.0f, FadeShape::EqualPower), fadeGain(0.5f, FadeShape::EqualPower),
                fadeGain(1.0f, FadeShape::EqualPower));
    DAWLAB_CHECK_NEAR(fadeGain(0.5f, FadeShape::Linear), 0.5, 1e-6);
    DAWLAB_CHECK_NEAR(fadeGain(0.5f, FadeShape::EqualPower), std::sqrt(0.5), 1e-6);
    DAWLAB_CHECK_NEAR(fadeGain(1.0f, FadeShape::EqualPower), 1.0, 1e-6);
    // fade-in² + fade-out² 在任何位置都是 1（功率守恆）；linear 在中點只有 0.5
    for (float t : {0.1f, 0.3f, 0.5f, 0.8f}) {
        const float gi = fadeGain(t, FadeShape::EqualPower), go = fadeGain(1.0f - t, FadeShape::EqualPower);
        DAWLAB_CHECK_NEAR(gi * gi + go * go, 1.0, 1e-5);
    }
    {
        const float gi = fadeGain(0.5f, FadeShape::Linear), go = fadeGain(0.5f, FadeShape::Linear);
        DAWLAB_CHECK_NEAR(gi * gi + go * go, 0.5, 1e-6);
    }

    // 2. fade-in / fade-out 套在常數訊號（DC = 1.0）上，直接讀 sample 看曲線
    const int fadeLen = 1001; // 奇數，中點剛好是 index 500
    AudioBuffer dc(1, 4000);
    for (int i = 0; i < 4000; ++i)
        dc.setSample(0, i, 1.0f);
    AudioBuffer dcIn = dc, dcOut = dc;
    applyFadeIn(dcIn, fadeLen, FadeShape::Linear);
    applyFadeOut(dcOut, fadeLen, FadeShape::EqualPower);
    DAWLAB_CHECK_NEAR(dcIn.getSample(0, 0), 0.0, 1e-6);
    DAWLAB_CHECK_NEAR(dcIn.getSample(0, 500), 0.5, 1e-6);
    DAWLAB_CHECK_NEAR(dcIn.getSample(0, 1000), 1.0, 1e-6);
    DAWLAB_CHECK_NEAR(dcIn.getSample(0, 1001), 1.0, 1e-6); // fade 範圍外不動
    DAWLAB_CHECK_NEAR(dcOut.getSample(0, 4000 - fadeLen - 1), 1.0, 1e-6); // fade 範圍前不動
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

    // 4. crossfade 兩段「相同」的訊號：linear 剛好不變（0.5 + 0.5 = 1），equal-power 反而 +3 dB
    AudioBuffer s1 = makeSine(440.0, sr, 2.0, 0.3f, 2);
    const float sineRef = middleRmsDb(s1);
    const float linSame = middleRmsDb(crossfade(s1, s1, FadeShape::Linear)) - sineRef;
    const float eqSame = middleRmsDb(crossfade(s1, s1, FadeShape::EqualPower)) - sineRef;
    std::printf("相同素材   crossfade 中段 RMS 變化：linear %+.2f dB，equal-power %+.2f dB\n", linSame, eqSame);
    DAWLAB_CHECK_NEAR(linSame, 0.0, 0.05);
    DAWLAB_CHECK_NEAR(eqSame, 3.01, 0.1);

    writeWav(outputPath("l00_ex6_crossfade_linear.wav"), xfLinear, sr);
    writeWav(outputPath("l00_ex6_crossfade_equal_power.wav"), xfEqual, sr);
    return dawlab::test::summary("l00_ex6");
}
