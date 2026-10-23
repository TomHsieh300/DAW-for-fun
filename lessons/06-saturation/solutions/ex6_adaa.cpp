// 練習 6 解答：一階 ADAA（antiderivative anti-aliasing）hard clip，量測 aliasing 改善多少
//
// 想法（Parker et al. 2016、Bilbao et al. 2017）：
//   naive：y[n] = f(x[n])——只在取樣點上看非線性，取樣點之間發生的事（尖角、產生的高頻）全部被忽略後折返。
//   ADAA：假設 x 在 x[n-1] 與 x[n] 之間是直線，取 f 在這段直線上的「平均值」：
//       y[n] = 1/(x[n] - x[n-1]) ∫_{x[n-1]}^{x[n]} f(u) du = (F(x[n]) - F(x[n-1])) / (x[n] - x[n-1])
//   其中 F 是 f 的反導函式（antiderivative）。這個平均等於先做一個「連續時間的矩形 lowpass」再取樣，
//   所以高頻諧波在折返之前就被壓低了。
//
// hard clip：f(x) = clamp(x, -1, 1)
//   F(x) = x²/2            （|x| ≤ 1）
//        = |x| - 1/2        （|x| > 1）   ← 在 |x| = 1 處值（1/2）與斜率（±1）都連續
//
// 數值問題：x[n] ≈ x[n-1] 時分子分母都趨近 0（0/0）。這時改用中點近似 f((x[n] + x[n-1]) / 2)
// （這正是那段直線上的平均值在 Δ → 0 時的極限）。分子用 double 算，否則 float 的相減誤差會很大。
//
// 代價：
//   - 在線性區（|x| < 1）y[n] = (x[n] + x[n-1]) / 2——一個 2-tap 平均：多了半個 sample 的延遲，
//     而且有 lowpass 效果 |H| = cos(ω/2)：7 kHz @ 48 kHz → cos(π·7/48) = 0.897 → -0.94 dB；20 kHz → -11.7 dB。
//   - 一階 ADAA 對「緩和」的 aliasing 改善有限；高 drive、高頻時仍建議與 2x oversampling 搭配。
//
// 執行：l06_ex6_solution

#include <dawlab/Common.h>
#include <dawlab/dsp/Oversampler.h>

#include <algorithm>
#include <cstdio>
#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr double kF0 = 7000.0;
constexpr float kDrive = 4.0f; // 振幅 0.5 × 4 = 2 → 深度 clip

class HardClipAdaa {
public:
    static double f(double x) noexcept { return std::clamp(x, -1.0, 1.0); }
    // F(x) = ∫ f
    static double antiderivative(double x) noexcept {
        const double ax = std::abs(x);
        return ax <= 1.0 ? 0.5 * x * x : ax - 0.5;
    }

    void reset() noexcept { x1_ = 0.0; }

    float processSample(float xIn) noexcept {
        const double x = xIn;
        const double dx = x - x1_;
        double y;
        if (std::abs(dx) > 1.0e-6)
            y = (antiderivative(x) - antiderivative(x1_)) / dx; // (F(x[n]) - F(x[n-1])) / (x[n] - x[n-1])
        else
            y = f(0.5 * (x + x1_));                             // 0/0 的極限：中點
        x1_ = x;
        return static_cast<float>(y);
    }

private:
    double x1_ = 0.0;
};

struct Result {
    double h1Db = 0.0;     // 基頻相對於「理想」的 dB（看 ADAA 的高頻損失）
    double aliasDb = 0.0;  // Σ alias 功率 / 基頻功率
};

// 所有 alias 都落在奇數 kHz（gcd(7000, 48000) = 1000，hard clip 是奇函式 → 只有奇次諧波）
Result analyse(const dawlab::AudioBuffer& b) {
    const int skip = 4800;
    const float* d = b.getReadPointer(0) + skip;
    const int n = b.getNumSamples() - skip;
    const double h1 = dawlab::goertzelAmplitude(d, n, kF0, kSampleRate);
    double p = 0.0;
    for (int k = 1; k <= 23; k += 2) {
        const double f = 1000.0 * k;
        if (f == kF0 || f == 3 * kF0)
            continue; // 7 kHz、21 kHz 是真正的諧波
        const double a = dawlab::goertzelAmplitude(d, n, f, kSampleRate);
        p += a * a;
    }
    return {dawlab::gainToDb(h1), 10.0 * std::log10(std::max(p / (h1 * h1), 1e-15))};
}

// mode：0 = naive、1 = ADAA；log2：oversampling（0 = 1x、1 = 2x）
Result run(int mode, int log2) {
    dawlab::AudioBuffer b = dawlab::makeSine(kF0, kSampleRate, 1.0, 0.5f, 1);
    dawlab::dsp::Oversampler os(log2);
    os.prepare(kSampleRate, kBlockSize, 1);
    HardClipAdaa adaa;
    dawlab::AudioBlock whole(b);
    for (int start = 0; start < b.getNumSamples(); start += kBlockSize) {
        dawlab::AudioBlock block = whole.getSubBlock(start, std::min(kBlockSize, b.getNumSamples() - start));
        dawlab::AudioBlock up = os.upsample(block);
        float* u = up.getChannelPointer(0);
        for (int i = 0; i < up.getNumSamples(); ++i) {
            const float x = kDrive * u[i];
            u[i] = (mode == 0) ? static_cast<float>(HardClipAdaa::f(x)) : adaa.processSample(x);
        }
        os.downsample(block);
    }
    return analyse(b);
}

} // namespace

int main() {
    // ---- 1. 反導函式的正確性：F' = f，且在 |x| = 1 連續 ----------------------------------------
    for (double x = -3.0; x <= 3.0; x += 0.01) {
        const double h = 1e-5;
        const double derivative = (HardClipAdaa::antiderivative(x + h) - HardClipAdaa::antiderivative(x - h)) / (2 * h);
        DAWLAB_CHECK(std::abs(derivative - HardClipAdaa::f(x)) < 1e-4);
    }
    DAWLAB_CHECK_NEAR(HardClipAdaa::antiderivative(1.0), 0.5, 1e-12);
    DAWLAB_CHECK_NEAR(HardClipAdaa::antiderivative(-2.0), 1.5, 1e-12);

    // ---- 2. 線性區：ADAA = 2-tap 平均 ------------------------------------------------------------
    {
        HardClipAdaa a;
        a.reset();
        a.processSample(0.2f);
        DAWLAB_CHECK_NEAR(a.processSample(0.6f), 0.4, 1e-6);  // (0.2 + 0.6) / 2
        DAWLAB_CHECK_NEAR(a.processSample(0.6f), 0.6, 1e-6);  // Δ = 0 → 中點
        DAWLAB_CHECK_NEAR(a.processSample(3.0f), (2.5 - 0.18) / 2.4, 1e-6); // (F(3) - F(0.6)) / 2.4
    }

    // ---- 3. aliasing 量測 --------------------------------------------------------------------------
    const Result naive1 = run(0, 0), adaa1 = run(1, 0), naive2 = run(0, 1), adaa2 = run(1, 1);
    std::printf("7 kHz 正弦（peak 0.5 × drive 4 = 2）→ hard clip，fs = 48 kHz\n");
    std::printf("%-16s %14s %16s\n", "方法", "基頻 (dBFS)", "alias 總和 (dB)");
    std::printf("%-16s %14.2f %16.1f\n", "naive 1x", naive1.h1Db, naive1.aliasDb);
    std::printf("%-16s %14.2f %16.1f\n", "ADAA 1x", adaa1.h1Db, adaa1.aliasDb);
    std::printf("%-16s %14.2f %16.1f\n", "naive 2x", naive2.h1Db, naive2.aliasDb);
    std::printf("%-16s %14.2f %16.1f\n", "ADAA + 2x", adaa2.h1Db, adaa2.aliasDb);
    // 對照：只靠 oversampling 的 naive hard clip。諧波只以 1/k 衰減，倍率每翻一倍只改善十幾 dB
    const Result naive4 = run(0, 2), naive8 = run(0, 3);
    std::printf("%-16s %14.2f %16.1f\n", "naive 4x", naive4.h1Db, naive4.aliasDb);
    std::printf("%-16s %14.2f %16.1f\n", "naive 8x", naive8.h1Db, naive8.aliasDb);
    std::printf("ADAA 1x 比 naive 1x 改善 %.1f dB；ADAA + 2x 比 naive 2x 改善 %.1f dB\n",
                naive1.aliasDb - adaa1.aliasDb, naive2.aliasDb - adaa2.aliasDb);

    DAWLAB_CHECK(adaa1.aliasDb < naive1.aliasDb - 6.0);
    DAWLAB_CHECK(adaa2.aliasDb < naive2.aliasDb - 6.0);
    DAWLAB_CHECK(naive2.aliasDb < naive1.aliasDb);
    DAWLAB_CHECK(naive4.aliasDb < naive2.aliasDb && naive8.aliasDb < naive4.aliasDb);
    // hard clip 即使 8x 也還有可量到的 alias（對照：練習 7 的 tanh 在 4x 就到 -85 dB）
    DAWLAB_CHECK(naive8.aliasDb > -75.0);
    // ADAA + 2x（運算量比 naive 8x 少很多）跟 naive 8x 在同一個量級
    DAWLAB_CHECK(std::abs(adaa2.aliasDb - naive8.aliasDb) < 10.0);
    // ADAA 的代價：基頻損失約 1 dB（2-tap 平均在 7 kHz 是 -0.94 dB；clip 區略有不同）
    DAWLAB_CHECK(naive1.h1Db - adaa1.h1Db > 0.3 && naive1.h1Db - adaa1.h1Db < 2.0);

    return dawlab::test::summary("l06_ex6");
}
