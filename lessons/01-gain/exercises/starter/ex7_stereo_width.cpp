// 練習 7（起始碼）：Mid/Side 立體聲寬度與 stereo balance
//
//   M = (L + R) / 2,  S = (L − R) / 2;   L' = M + w·S,  R' = M − w·S
//   balance b：b > 0 左聲道乘 (1 − b)，b < 0 右聲道乘 (1 + b)
// 完成 TODO 之後，self-check 全部通過就算完成。起始碼執行時會印出 FAIL，這是預期的。

#include <dawlab/Common.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace dawlab;

// in-place：channel 0 = L、channel 1 = R
void applyStereoWidth(AudioBlock stereo, float width) {
    if (stereo.getNumChannels() < 2)
        return;
    (void)width;
    // TODO: 逐 sample 算 mid / side，把 side 乘上 width 再 decode 回 L / R
}

void applyBalance(AudioBlock stereo, float balance) {
    if (stereo.getNumChannels() < 2)
        return;
    (void)balance;
    // TODO: 依 balance 的正負把其中一個 channel 乘上 (1 − |balance|)
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

    // 左 = 正弦波，右 = 0
    AudioBuffer src = makeSine(440.0, sr, 0.5, 0.5f, 2);
    std::fill_n(src.getWritePointer(1), src.getNumSamples(), 0.0f);

    AudioBuffer w0 = src;
    applyStereoWidth(w0, 0.0f);
    DAWLAB_CHECK(maxAbsDiff(w0, 0, w0, 1) < 1e-7f);
    DAWLAB_CHECK_NEAR(peakAbs(w0.getReadPointer(0), w0.getNumSamples()), 0.25, 1e-6);

    AudioBuffer w1 = src;
    applyStereoWidth(w1, 1.0f);
    DAWLAB_CHECK(maxAbsDiff(w1, 0, src, 0) < 1e-7f);
    DAWLAB_CHECK(maxAbsDiff(w1, 1, src, 1) < 1e-7f);

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

    // 不相關雜訊：width 2 → side +6.02 dB、mid 不變
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
    DAWLAB_CHECK_NEAR(gainToDb(mid2 / mid0), 0.0, 1e-3);
    DAWLAB_CHECK_NEAR(gainToDb(side2 / side0), 6.0206, 1e-3);

    // balance
    AudioBuffer bal = noise;
    applyBalance(bal, 1.0f);
    DAWLAB_CHECK(peakAbs(bal.getReadPointer(0), bal.getNumSamples()) == 0.0f);
    DAWLAB_CHECK(maxAbsDiff(bal, 1, noise, 1) == 0.0f);
    AudioBuffer half = noise;
    applyBalance(half, 0.5f);
    DAWLAB_CHECK_NEAR(gainToDb(rms(half, 0) / rms(noise, 0)), -6.0206, 1e-3);
    DAWLAB_CHECK_NEAR(gainToDb(rms(half, 1) / rms(noise, 1)), 0.0, 1e-6);

    return dawlab::test::summary("l01_ex7_starter");
}
