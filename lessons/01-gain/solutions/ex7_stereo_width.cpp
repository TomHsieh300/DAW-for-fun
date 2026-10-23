// 練習 7 解答：Mid/Side 立體聲寬度與 stereo balance
//
//   M = (L + R) / 2    S = (L − R) / 2         （encode）
//   L = M + S          R = M − S               （decode，剛好還原）
//   width w：S' = w · S  →  L' = M + w·S，R' = M − w·S
//     w = 0 → L' = R' = M（mono）、w = 1 → 原樣、w = 2 → 左右差異放大一倍
//   mono 相容：(L' + R') / 2 = M，與 w 無關——寬度處理不會改變 mono 下的聲音。
//
//   balance b ∈ [-1, 1]（立體聲訊號用）：b > 0 把左聲道乘 (1 − b)，右聲道不動；b < 0 反之。
//   和 pan 的差別：pan 把 mono 訊號「分配」到左右；balance 只把其中一邊「調小」，
//   右聲道原本的內容永遠不會跑到左邊。

#include <dawlab/Common.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace dawlab;

// in-place：channel 0 = L、channel 1 = R
void applyStereoWidth(AudioBlock stereo, float width) {
    if (stereo.getNumChannels() < 2)
        return;
    float* l = stereo.getChannelPointer(0);
    float* r = stereo.getChannelPointer(1);
    const float w = std::max(0.0f, width);
    for (int i = 0; i < stereo.getNumSamples(); ++i) {
        const float mid = 0.5f * (l[i] + r[i]);
        const float side = 0.5f * (l[i] - r[i]) * w;
        l[i] = mid + side;
        r[i] = mid - side;
    }
}

void applyBalance(AudioBlock stereo, float balance) {
    if (stereo.getNumChannels() < 2)
        return;
    const float b = std::clamp(balance, -1.0f, 1.0f);
    const float gL = b > 0.0f ? 1.0f - b : 1.0f;
    const float gR = b < 0.0f ? 1.0f + b : 1.0f;
    stereo.getChannelSubset(0, 1).multiplyBy(gL);
    stereo.getChannelSubset(1, 1).multiplyBy(gR);
}

namespace {

float maxAbsDiff(const AudioBuffer& a, int chA, const AudioBuffer& b, int chB) {
    float m = 0.0f;
    for (int i = 0; i < a.getNumSamples(); ++i)
        m = std::max(m, std::abs(a.getSample(chA, i) - b.getSample(chB, i)));
    return m;
}

} // namespace

int main() {
    constexpr double sr = 48000.0;

    // 測試訊號：左 = 正弦波，右 = 0（極端的「只有左邊有聲音」）
    AudioBuffer src = makeSine(440.0, sr, 0.5, 0.5f, 2);
    std::fill_n(src.getWritePointer(1), src.getNumSamples(), 0.0f);

    // width 0 → 兩邊都是 M = 0.5·L
    AudioBuffer w0 = src;
    applyStereoWidth(w0, 0.0f);
    DAWLAB_CHECK(maxAbsDiff(w0, 0, w0, 1) < 1e-7f);
    DAWLAB_CHECK_NEAR(peakAbs(w0.getReadPointer(0), w0.getNumSamples()), 0.25, 1e-6);

    // width 1 → 原樣
    AudioBuffer w1 = src;
    applyStereoWidth(w1, 1.0f);
    DAWLAB_CHECK(maxAbsDiff(w1, 0, src, 0) < 1e-7f);
    DAWLAB_CHECK(maxAbsDiff(w1, 1, src, 1) < 1e-7f);

    // width 2 → L' = M + 2S = 1.5·L，R' = M − 2S = −0.5·L（右邊出現反相的訊號）
    AudioBuffer w2 = src;
    applyStereoWidth(w2, 2.0f);
    DAWLAB_CHECK_NEAR(peakAbs(w2.getReadPointer(0), w2.getNumSamples()), 0.75, 1e-6);
    DAWLAB_CHECK_NEAR(peakAbs(w2.getReadPointer(1), w2.getNumSamples()), 0.25, 1e-6);
    DAWLAB_CHECK_NEAR(w2.getSample(1, 100), -0.5 * src.getSample(0, 100), 1e-6);

    // mono 相容：任何 width 下 (L'+R')/2 都等於原本的 M
    for (const AudioBuffer* b : {&w0, &w1, &w2}) {
        float m = 0.0f;
        for (int i = 0; i < src.getNumSamples(); ++i)
            m = std::max(m, std::abs(0.5f * (b->getSample(0, i) + b->getSample(1, i)) -
                                     0.5f * (src.getSample(0, i) + src.getSample(1, i))));
        DAWLAB_CHECK(m < 1e-6f);
    }
    std::printf("width 0/1/2：L peak %.3f / %.3f / %.3f，R peak %.3f / %.3f / %.3f（mono 和不變）\n",
                peakAbs(w0.getReadPointer(0), w0.getNumSamples()), peakAbs(w1.getReadPointer(0), w1.getNumSamples()),
                peakAbs(w2.getReadPointer(0), w2.getNumSamples()), peakAbs(w0.getReadPointer(1), w0.getNumSamples()),
                peakAbs(w1.getReadPointer(1), w1.getNumSamples()), peakAbs(w2.getReadPointer(1), w2.getNumSamples()));

    // 不相關的立體聲雜訊：width 2 讓 side 的 RMS 變成 2 倍（+6.02 dB），mid 不變
    AudioBuffer noise = makeWhiteNoise(sr, 1.0, 0.25f, 2, 5);
    auto midSideRms = [](const AudioBuffer& b, float& midRms, float& sideRms) {
        double m2 = 0.0, s2 = 0.0;
        for (int i = 0; i < b.getNumSamples(); ++i) {
            const double mid = 0.5 * (b.getSample(0, i) + b.getSample(1, i));
            const double side = 0.5 * (b.getSample(0, i) - b.getSample(1, i));
            m2 += mid * mid;
            s2 += side * side;
        }
        midRms = static_cast<float>(std::sqrt(m2 / b.getNumSamples()));
        sideRms = static_cast<float>(std::sqrt(s2 / b.getNumSamples()));
    };
    float mid0 = 0.0f, side0 = 0.0f, mid2 = 0.0f, side2 = 0.0f;
    midSideRms(noise, mid0, side0);
    AudioBuffer wide = noise;
    applyStereoWidth(wide, 2.0f);
    midSideRms(wide, mid2, side2);
    std::printf("不相關雜訊 width 2：mid %.2f → %.2f dB，side %.2f → %.2f dB（+%.2f dB）\n", gainToDb(mid0), gainToDb(mid2),
                gainToDb(side0), gainToDb(side2), gainToDb(side2 / side0));
    DAWLAB_CHECK_NEAR(gainToDb(mid2 / mid0), 0.0, 1e-3);
    DAWLAB_CHECK_NEAR(gainToDb(side2 / side0), 6.0206, 1e-3);

    // balance：+1 → 左聲道靜音、右聲道不動；+0.5 → 左 −6.02 dB
    AudioBuffer bal = noise;
    applyBalance(bal, 1.0f);
    DAWLAB_CHECK(peakAbs(bal.getReadPointer(0), bal.getNumSamples()) == 0.0f);
    DAWLAB_CHECK(maxAbsDiff(bal, 1, noise, 1) == 0.0f);
    AudioBuffer half = noise;
    applyBalance(half, 0.5f);
    DAWLAB_CHECK_NEAR(gainToDb(rms(half, 0) / rms(noise, 0)), -6.0206, 1e-3);
    DAWLAB_CHECK_NEAR(gainToDb(rms(half, 1) / rms(noise, 1)), 0.0, 1e-6);
    // 右聲道內容沒有跑進左聲道：左聲道仍然和原本的左聲道成比例
    DAWLAB_CHECK(maxAbsDiff(half, 0, [&] { AudioBuffer t = noise; t.applyGain(0.5f); return t; }(), 0) < 1e-6f);
    std::printf("balance +0.5：L %.2f dB、R %.2f dB；balance +1：L peak = %.1f dBFS\n",
                gainToDb(rms(half, 0) / rms(noise, 0)), gainToDb(rms(half, 1) / rms(noise, 1)),
                gainToDb(peakAbs(bal.getReadPointer(0), bal.getNumSamples())));

    // 試聽檔：撥弦琶音（左右用不同 seed 製造一點差異）依序 width 0 → 1 → 2
    {
        AudioBuffer l = makePluckSequence(sr, 2.0, 0.4f, 1);
        AudioBuffer r = makePluckSequence(sr, 2.0, 0.4f, 1);
        AudioBuffer demo(2, l.getNumSamples() * 3);
        const float widths[] = {0.0f, 1.0f, 2.0f};
        for (int k = 0; k < 3; ++k) {
            AudioBuffer seg(2, l.getNumSamples());
            seg.copyFrom(0, 0, l, 0, 0, l.getNumSamples());
            seg.copyFrom(1, 0, r, 0, 0, r.getNumSamples());
            // 讓右聲道晚 1 ms，左右才會有 side 成分可以放大
            AudioBuffer delayed(2, l.getNumSamples());
            delayed.copyFrom(0, 0, seg, 0, 0, seg.getNumSamples());
            const int d = msToSamples(1.0, sr);
            delayed.copyFrom(1, d, seg, 1, 0, seg.getNumSamples() - d);
            applyStereoWidth(delayed, widths[k]);
            demo.copyFrom(0, k * l.getNumSamples(), delayed, 0, 0, l.getNumSamples());
            demo.copyFrom(1, k * l.getNumSamples(), delayed, 1, 0, l.getNumSamples());
        }
        writeWav(outputPath("l01_ex7_width.wav"), demo, sr);
        std::printf("輸出 dawlab_output/l01_ex7_width.wav（width 0 → 1 → 2 各 2 秒）\n");
    }

    return dawlab::test::summary("l01_ex7");
}
