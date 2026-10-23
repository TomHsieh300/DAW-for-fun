// 練習 7 解答：stereo correlation meter（相位相關表）
//
// 混音師用它檢查 mono 相容性：左右聲道相位相反的成分，在手機喇叭、俱樂部的 mono PA 上會互相抵消而消失。
//
// 定義（Pearson correlation，不減平均值，因為音訊本來就沒有 DC）：
//
//              E[L·R]
//     r = ---------------------        −1 ≤ r ≤ +1
//          sqrt( E[L²] · E[R²] )
//
//   r = +1：左右完全相同（mono）              → mono 相容性最好
//   r ≈  0：左右無關（例如不同 seed 的雜訊、很寬的 reverb），或相差 90°
//   r = −1：左右相同但反相（L = −R）          → 加成 mono 會完全消失！
//
// E[·] 用 exponential moving average 估計（一階 lowpass，時間常數 τ）：
//     m[n] = m[n−1] + a · (v[n] − m[n−1]),   a = 1 − e^{−1/(τ·fs)}
// 分別對 L·R、L²、R² 做平均，再套公式。τ 常見 100 ~ 300 ms；太短指針會亂跳，太長反應慢。
// 硬體 correlation meter 的時間常數各家不同，這裡預設 300 ms。
//
// 同時提供 M/S 的能量比：M = (L+R)/2、S = (L−R)/2，sideDb = 10·log10(E[S²]/E[M²])。
// 兩者的關係（左右能量相同時）：E[S²]/E[M²] = (1 − r)/(1 + r)。
//
// 執行：bin/Debug/l11_ex7_solution.exe（ctest label: solution）
// 輸出：dawlab_output/l11_ex7_out_of_phase.wav（L = −R 的 pluck，耳機聽起來「在頭的兩側、很怪」；
//       用 mono 播放會幾乎完全消失）

#include <dawlab/Common.h>

#include <atomic>
#include <cmath>
#include <cstdio>

using namespace dawlab;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

class CorrelationMeter {
public:
    void prepare(double sampleRate) {
        sampleRate_ = sampleRate;
        updateCoefficient();
        reset();
    }

    void reset() noexcept {
        lr_ = ll_ = rr_ = 0.0;
        correlation_.store(0.0f, std::memory_order_relaxed);
        sideToMidDb_.store(kMinusInfinityDb, std::memory_order_relaxed);
    }

    void setTimeConstantMs(float ms) noexcept {
        timeConstantMs_ = std::max(1.0f, ms);
        updateCoefficient();
    }

    // audio thread：只看前兩個 channel
    void process(const AudioBlock& block) noexcept {
        if (block.getNumChannels() < 2)
            return;
        const float* l = block.getChannelPointer(0);
        const float* r = block.getChannelPointer(1);
        for (int i = 0; i < block.getNumSamples(); ++i) {
            const double x = l[i], y = r[i];
            lr_ += a_ * (x * y - lr_);
            ll_ += a_ * (x * x - ll_);
            rr_ += a_ * (y * y - rr_);
        }
        // 很小聲時分母接近 0：定義成 0（指針回到中間），避免除以 0 或被雜訊亂甩
        const double denom = std::sqrt(ll_ * rr_);
        const double corr = denom > 1e-10 ? std::clamp(lr_ / denom, -1.0, 1.0) : 0.0;
        correlation_.store(static_cast<float>(corr), std::memory_order_relaxed);

        // E[M²] = (E[L²] + 2E[LR] + E[R²]) / 4，E[S²] = (E[L²] − 2E[LR] + E[R²]) / 4
        const double mid = std::max(0.0, (ll_ + 2.0 * lr_ + rr_) * 0.25);
        const double side = std::max(0.0, (ll_ - 2.0 * lr_ + rr_) * 0.25);
        float sideDb = kMinusInfinityDb;
        if (side > 1e-20)
            sideDb = mid > 1e-20 ? static_cast<float>(10.0 * std::log10(side / mid)) : 100.0f;
        sideToMidDb_.store(sideDb, std::memory_order_relaxed);
    }

    float getCorrelation() const noexcept { return correlation_.load(std::memory_order_relaxed); }
    float getSideToMidDb() const noexcept { return sideToMidDb_.load(std::memory_order_relaxed); }

private:
    void updateCoefficient() noexcept {
        if (sampleRate_ > 0.0)
            a_ = 1.0 - std::exp(-1.0 / (timeConstantMs_ * 0.001 * sampleRate_));
    }

    double sampleRate_ = 0.0;
    float timeConstantMs_ = 300.0f;
    double a_ = 1.0;
    double lr_ = 0.0, ll_ = 0.0, rr_ = 0.0;
    std::atomic<float> correlation_{0.0f};
    std::atomic<float> sideToMidDb_{kMinusInfinityDb};
};

float measure(CorrelationMeter& meter, AudioBuffer& audio) {
    meter.reset();
    for (int start = 0; start < audio.getNumSamples(); start += kBlockSize) {
        const int len = std::min(kBlockSize, audio.getNumSamples() - start);
        meter.process(AudioBlock(audio).getSubBlock(start, len));
    }
    return meter.getCorrelation();
}

// 左右聲道各自由一個函式產生
template <typename FnL, typename FnR>
AudioBuffer makeStereo(double seconds, FnL&& left, FnR&& right) {
    AudioBuffer b(2, secondsToSamples(seconds, kSampleRate));
    for (int i = 0; i < b.getNumSamples(); ++i) {
        const double t = i / kSampleRate;
        b.setSample(0, i, static_cast<float>(left(t)));
        b.setSample(1, i, static_cast<float>(right(t)));
    }
    return b;
}

} // namespace

int main() {
    CorrelationMeter meter;
    meter.prepare(kSampleRate);
    const double w = kTwoPi * 440.0;

    // ---- 1. 四種教科書情況 -----------------------------------------------------------------------------
    AudioBuffer mono = makeSine(440.0, kSampleRate, 2.0, 0.5f, 2);
    const float rMono = measure(meter, mono);
    const float sideMono = meter.getSideToMidDb();

    AudioBuffer inverted = makeStereo(2.0, [&](double t) { return 0.5 * std::sin(w * t); },
                                      [&](double t) { return -0.5 * std::sin(w * t); });
    const float rInverted = measure(meter, inverted);

    AudioBuffer quadrature = makeStereo(2.0, [&](double t) { return 0.5 * std::sin(w * t); },
                                        [&](double t) { return 0.5 * std::cos(w * t); });
    const float rQuad = measure(meter, quadrature);
    const float sideQuad = meter.getSideToMidDb();

    AudioBuffer noise = makeWhiteNoise(kSampleRate, 2.0, 0.25f, 2, 5); // 左右不同 seed → 不相關
    const float rNoise = measure(meter, noise);
    const float sideNoise = meter.getSideToMidDb();

    std::printf("%-34s %12s %14s\n", "訊號", "correlation", "S/M 能量 (dB)");
    std::printf("%-34s %12.3f %14.1f\n", "L = R（mono）", rMono, sideMono);
    std::printf("%-34s %12.3f %14s\n", "L = −R（反相）", rInverted, "+inf");
    std::printf("%-34s %12.3f %14.1f\n", "L = sin、R = cos（差 90°）", rQuad, sideQuad);
    std::printf("%-34s %12.3f %14.1f\n", "左右獨立的白雜訊", rNoise, sideNoise);
    DAWLAB_CHECK_NEAR(rMono, 1.0, 1e-3);
    DAWLAB_CHECK(sideMono <= kMinusInfinityDb + 1.0f); // 完全沒有 side
    DAWLAB_CHECK_NEAR(rInverted, -1.0, 1e-3);
    DAWLAB_CHECK_NEAR(rQuad, 0.0, 0.02);
    DAWLAB_CHECK_NEAR(sideQuad, 0.0, 0.2); // r = 0 → S/M = (1−r)/(1+r) = 1 → 0 dB
    DAWLAB_CHECK_NEAR(rNoise, 0.0, 0.1); // 300 ms 的估計會在 0 附近小幅擺動
    DAWLAB_CHECK_NEAR(sideNoise, 0.0, 1.0);

    // ---- 2. 部分相關：L = s + n1、R = s + n2，s 與 n 能量相同 → r = 1/2 -----------------------------------
    {
        AudioBuffer common = makeWhiteNoise(kSampleRate, 3.0, 0.25f, 1, 101);
        AudioBuffer indep = makeWhiteNoise(kSampleRate, 3.0, 0.25f, 2, 202);
        AudioBuffer mix(2, common.getNumSamples());
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < mix.getNumSamples(); ++i)
                mix.setSample(ch, i, common.getSample(0, i) + indep.getSample(ch, i));
        const float r = measure(meter, mix);
        const float side = meter.getSideToMidDb();
        // 理論：r = E[s²] / (E[s²] + E[n²]) = 0.5；S/M = (1 − r)/(1 + r) = 1/3 → −4.77 dB
        std::printf("%-34s %12.3f %14.1f（理論 0.5 / −4.8 dB）\n", "共同成分 + 獨立成分（1:1）", r, side);
        DAWLAB_CHECK_NEAR(r, 0.5, 0.1);
        DAWLAB_CHECK_NEAR(side, -4.77, 1.0);
    }

    // ---- 3. 反應時間：從 mono 突然切到反相，指針要多久從 +1 擺到 0 以下？------------------------------------
    {
        AudioBuffer flip = makeStereo(2.0, [&](double t) { return 0.5 * std::sin(w * t); },
                                      [&](double t) { return (t < 1.0 ? 0.5 : -0.5) * std::sin(w * t); });
        meter.reset();
        double crossTime = -1.0;
        for (int start = 0; start < flip.getNumSamples(); start += kBlockSize) {
            const int len = std::min(kBlockSize, flip.getNumSamples() - start);
            meter.process(AudioBlock(flip).getSubBlock(start, len));
            if (crossTime < 0.0 && start / kSampleRate > 1.0 && meter.getCorrelation() < 0.0f)
                crossTime = (start + len) / kSampleRate - 1.0;
        }
        // E[LR] 從 +P 指數地走向 −P：越過 0 的時間 = τ·ln 2 ≈ 208 ms（τ = 300 ms）
        std::printf("\nmono → 反相：%.0f ms 後 correlation 越過 0（理論 τ·ln2 = %.0f ms，block 粒度 ±11 ms）\n",
                    crossTime * 1000.0, 300.0 * std::log(2.0));
        DAWLAB_CHECK_NEAR(crossTime * 1000.0, 300.0 * std::log(2.0), 15.0);
    }

    // ---- 4. 反相的 pluck：寫出 WAV，並驗證加成 mono 會消失 ----------------------------------------------
    {
        AudioBuffer plucks = makePluckSequence(kSampleRate, 3.0, 0.4f, 2);
        for (int i = 0; i < plucks.getNumSamples(); ++i)
            plucks.setSample(1, i, -plucks.getSample(0, i));
        const float r = measure(meter, plucks);
        float monoPeak = 0.0f;
        for (int i = 0; i < plucks.getNumSamples(); ++i)
            monoPeak = std::max(monoPeak, std::abs(0.5f * (plucks.getSample(0, i) + plucks.getSample(1, i))));
        std::printf("反相的 pluck：correlation = %.3f，(L+R)/2 的 peak = %.3g（完全抵消）\n", r, monoPeak);
        DAWLAB_CHECK_NEAR(r, -1.0, 1e-3);
        DAWLAB_CHECK(monoPeak < 1e-6f);
        writeWav(outputPath("l11_ex7_out_of_phase.wav"), plucks, kSampleRate);
        std::printf("→ dawlab_output/l11_ex7_out_of_phase.wav（請用耳機聽，再試試切成 mono）\n");
    }

    return dawlab::test::summary("l11_ex7");
}
