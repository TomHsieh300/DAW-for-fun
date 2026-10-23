// 練習 3 解答：allpass interpolation，並與 linear / cubic 比較
//
// 一階 allpass interpolator：
//     H(z) = (η + z⁻¹) / (1 + η z⁻¹),   η = (1 − Δ) / (1 + Δ)
//     y[n] = η·(v[n] − y[n−1]) + v[n−1]      v[n] = x[n − d]（整數延遲後的訊號）
// 特性：
//   - |H(e^{jω})| = 1 對所有頻率成立 → 完全沒有 linear / cubic 的高頻衰減。
//   - 代價是「相位延遲」只在低頻精準等於 Δ；頻率越高越偏離：
//         τ(ω) = 1 − (2/ω)·atan( η·sin ω / (1 + η·cos ω) )     （單位：sample）
//     ω → 0 時 τ → (1 − η)/(1 + η) = Δ。
//   - 它有記憶（y[n−1]），所以 delay time 改變時會留下一小段 transient，不適合快速調變。
// 實作技巧：讓 Δ 落在 [0.5, 1.5) 而不是 [0, 1)，η 就會落在 (−0.2, 1/3]，pole 離單位圓夠遠。
//
// 執行：l09_ex3_solution（不需要參數）。輸出 dawlab_output/l09_ex3_allpass_noise.wav。

#include <dawlab/Common.h>
#include <dawlab/dsp/FractionalDelayLine.h>

#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>

using dawlab::dsp::FractionalDelayLine;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

class AllpassInterpolator {
public:
    void prepare(int maxDelaySamples) {
        line_.prepare(maxDelaySamples + 2, 1); // 多 2 格：要讀 d 與 d+1
        reset();
    }

    void reset() noexcept {
        line_.reset();
        y1_ = 0.0f;
    }

    // delaySamples = d + Δ。把 Δ 安排在 [0.5, 1.5)，避免 η 接近 1（pole 貼近單位圓、transient 很長）。
    void setDelay(float delaySamples) noexcept {
        const float delay = std::max(0.5f, delaySamples);
        d_ = static_cast<int>(std::floor(delay - 0.5f));
        const float frac = delay - static_cast<float>(d_); // ∈ [0.5, 1.5)
        eta_ = (1.0f - frac) / (1.0f + frac);
    }

    float processSample(float x) noexcept {
        line_.push(0, x);
        const float v0 = line_.readInteger(0, d_);     // v[n]   = x[n − d]
        const float v1 = line_.readInteger(0, d_ + 1); // v[n−1] = x[n − d − 1]
        float y = eta_ * (v0 - y1_) + v1;              // y[n] = η(v[n] − y[n−1]) + v[n−1]
        if (std::abs(y) < 1.0e-20f)                    // 遞迴結構：flush denormal
            y = 0.0f;
        y1_ = y;
        return y;
    }

    float getEta() const noexcept { return eta_; }
    int getIntegerDelay() const noexcept { return d_; }

private:
    FractionalDelayLine line_;
    int d_ = 0;
    float eta_ = 0.0f;
    float y1_ = 0.0f;
};

// allpass 的理論相位延遲（總延遲，單位 sample）
double allpassTheoryDelay(int d, double eta, double freq) {
    const double w = dawlab::kTwoPi * freq / kSampleRate;
    return d + 1.0 - (2.0 / w) * std::atan(eta * std::sin(w) / (1.0 + eta * std::cos(w)));
}

// 量測「相位延遲」（單位 sample）：把 in / out 投影到 e^{−jωn}，取相位差再除以 ω。
// 相位差只有 2π 的解析度，所以用 expected 把結果展開（unwrap）到最接近的那個週期。
template <typename Fn>
double measurePhaseDelaySamples(Fn&& fn, double freq, double expected) {
    const int skip = static_cast<int>(0.2 * kSampleRate);
    const int n = static_cast<int>(0.5 * kSampleRate);
    const double w = dawlab::kTwoPi * freq / kSampleRate;
    std::complex<double> sumIn(0.0, 0.0), sumOut(0.0, 0.0);
    for (int i = 0; i < skip + n; ++i) {
        const auto x = static_cast<float>(0.25 * std::sin(w * i));
        const float y = fn(x);
        if (i >= skip) {
            const int k = i - skip;
            const double win = 0.5 - 0.5 * std::cos(dawlab::kTwoPi * k / (n - 1)); // Hann
            const std::complex<double> e(std::cos(w * i), -std::sin(w * i));
            sumIn += win * static_cast<double>(x) * e;
            sumOut += win * static_cast<double>(y) * e;
        }
    }
    const double phase = std::arg(sumOut * std::conj(sumIn)); // 負值 = 輸出落後
    double delay = -phase / w;
    const double period = dawlab::kTwoPi / w;
    delay += period * std::round((expected - delay) / period); // unwrap
    return delay;
}

double measureAllpassGainDb(float delay, double freq) {
    AllpassInterpolator ap;
    ap.prepare(64);
    ap.setDelay(delay);
    return dawlab::measureGainDb([&](float x) { return ap.processSample(x); }, freq, kSampleRate);
}

double measureLinearGainDb(float delay, double freq) {
    FractionalDelayLine line;
    line.prepare(64, 1);
    return dawlab::measureGainDb(
        [&](float x) {
            line.push(0, x);
            return line.readLinear(0, delay);
        },
        freq, kSampleRate);
}

double measureCubicGainDb(float delay, double freq) {
    FractionalDelayLine line;
    line.prepare(64, 1);
    return dawlab::measureGainDb(
        [&](float x) {
            line.push(0, x);
            return line.readCubic(0, delay);
        },
        freq, kSampleRate);
}

} // namespace

int main() {
    std::printf("=== 第 09 章 練習 3：allpass interpolation ===\n\n");

    // ---- 1. 幅度響應（delay = 10.5）----------------------------------------
    {
        const float delay = 10.5f;
        AllpassInterpolator ap;
        ap.prepare(64);
        ap.setDelay(delay);
        std::printf("delay = %.1f：d = %d, Δ = %.2f, η = %.4f\n", delay, ap.getIntegerDelay(),
                    delay - static_cast<float>(ap.getIntegerDelay()), ap.getEta());
        std::printf("幅度響應（dB）：\n   freq(Hz)   linear    cubic   allpass\n");
        const double freqs[] = {100, 1000, 4000, 8000, 12000, 16000, 20000};
        for (double f : freqs) {
            const double gl = measureLinearGainDb(delay, f);
            const double gc = measureCubicGainDb(delay, f);
            const double ga = measureAllpassGainDb(delay, f);
            std::printf("  %8.0f   %+6.2f   %+6.2f   %+6.2f\n", f, gl, gc, ga);
            DAWLAB_CHECK_NEAR(ga, 0.0, 0.05); // allpass：所有頻率都是 0 dB
        }
        DAWLAB_CHECK_NEAR(measureLinearGainDb(delay, 12000.0), -3.01, 0.1); // |cos(π/4)|
        DAWLAB_CHECK_NEAR(measureCubicGainDb(delay, 12000.0), -1.07, 0.1);  // |9/8·cos(π/4) − 1/8·cos(3π/4)|
        std::printf("  -> allpass 全頻 0 dB；linear 在 12 kHz 掉 3 dB、cubic 掉 1.07 dB。\n\n");
    }

    // ---- 2. 相位延遲 vs 頻率（delay = 10.5）---------------------------------
    {
        const float delay = 10.5f;
        std::printf("相位延遲（sample），delay = %.1f：\n   freq(Hz)   linear   allpass   allpass 理論\n", delay);
        const double freqs[] = {100, 1000, 2000, 4000, 8000};
        double allpassError8k = 0.0; // 8 kHz 時 allpass 的相位延遲與目標 delay 的差
        for (double f : freqs) {
            FractionalDelayLine line;
            line.prepare(64, 1);
            const double tl = measurePhaseDelaySamples(
                [&](float x) {
                    line.push(0, x);
                    return line.readLinear(0, delay);
                },
                f, delay);
            AllpassInterpolator ap;
            ap.prepare(64);
            ap.setDelay(delay);
            const double ta = measurePhaseDelaySamples([&](float x) { return ap.processSample(x); }, f, delay);
            const double theory = allpassTheoryDelay(ap.getIntegerDelay(), ap.getEta(), f);
            std::printf("  %8.0f   %7.3f   %7.3f   %7.3f\n", f, tl, ta, theory);
            DAWLAB_CHECK_NEAR(ta, theory, 0.02);
            if (f == 8000.0)
                allpassError8k = ta - delay;
        }
        // 低頻時 allpass 的延遲就是我們要的 10.5；linear（Δ = 0.5 是對稱 FIR）則是精準的線性相位
        AllpassInterpolator ap;
        ap.prepare(64);
        ap.setDelay(delay);
        DAWLAB_CHECK_NEAR(measurePhaseDelaySamples([&](float x) { return ap.processSample(x); }, 100.0, delay),
                          delay, 0.01);
        std::printf("  -> allpass 的延遲在低頻精準、頻率越高偏離越多（8 kHz 已偏離 %+.3f sample）；\n",
                    allpassError8k);
        std::printf("     linear 在 Δ = 0.5 時是對稱 FIR，相位延遲全頻精準但幅度衰減。\n\n");
    }

    // ---- 3. 不同的小數部分：低頻延遲是否都正確 --------------------------------
    {
        std::printf("各種小數延遲在 100 Hz 的相位延遲（allpass）：\n");
        const float delays[] = {10.0f, 10.1f, 10.3f, 10.5f, 10.7f, 10.9f, 11.0f};
        for (float d : delays) {
            AllpassInterpolator ap;
            ap.prepare(64);
            ap.setDelay(d);
            const double t = measurePhaseDelaySamples([&](float x) { return ap.processSample(x); }, 100.0, d);
            std::printf("  delay %.1f -> d = %2d, η = %+.4f, 量到 %.3f\n", d, ap.getIntegerDelay(), ap.getEta(), t);
            DAWLAB_CHECK_NEAR(t, d, 0.01);
        }
        std::printf("\n");
    }

    // ---- 4. 聽聽看：白雜訊經過 allpass 10.5 sample ----------------------------
    {
        dawlab::AudioBuffer noise = dawlab::makeWhiteNoise(kSampleRate, 3.0, 0.25f, 1);
        dawlab::AudioBuffer out(1, noise.getNumSamples());
        AllpassInterpolator ap;
        ap.prepare(64);
        ap.setDelay(10.5f);
        const float* src = noise.getReadPointer(0);
        float* dst = out.getWritePointer(0);
        for (int start = 0; start < noise.getNumSamples(); start += kBlockSize) {
            const int n = std::min(kBlockSize, noise.getNumSamples() - start);
            for (int i = 0; i < n; ++i)
                dst[start + i] = ap.processSample(src[start + i]);
        }
        dawlab::writeWav(dawlab::outputPath("l09_ex3_allpass_noise.wav"), out, kSampleRate);
        std::printf("白雜訊 RMS：dry %.4f, allpass %.4f（allpass 不改變能量）\n", dawlab::rms(noise, 0),
                    dawlab::rms(out, 0));
        DAWLAB_CHECK_NEAR(dawlab::rms(out, 0), dawlab::rms(noise, 0), 0.002);
        DAWLAB_CHECK(!dawlab::hasNanOrInf(out));
        std::printf("已輸出 dawlab_output/l09_ex3_allpass_noise.wav\n\n");
    }

    return dawlab::test::summary("l09_ex3");
}
