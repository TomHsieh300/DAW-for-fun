// 練習 5 解答：latency 對齊的 dry/wet mix，並量測沒對齊時的 comb notch
//
// Parallel saturation 的訊號流：
//
//   x ──┬──> [4x oversampler: 升頻 → tanh → 降頻]（延遲 L = 36 samples）──> wet ──┐
//       └──> [dry delay: D samples] ────────────────────────────────────> dry ──┴─> (1-m)·dry + m·wet
//
// 小訊號時 tanh(x) ≈ x，wet 只是「延遲 L 個 sample 的 x」，整個系統就是
//     y[n] = (1 - m)·x[n - D] + m·x[n - L]
//     |H(f)| = |(1 - m) + m·e^{-j2πf(L-D)/fs}|
// D = L（對齊）時 |H| = 1，完全平坦；D ≠ L 時是一個 feedforward comb filter，
// 相位差 2πfΔ/fs = π 的奇數倍處出現 notch：f_k = (2k + 1)·fs / (2Δ)，Δ = L - D。
// m = 0.5 時兩路振幅相同、notch 理論上無限深；m ≠ 0.5 時 notch 深度 = |1 - 2m|。
//
// DC blocker 不在這一題：Asymmetric 曲線的 DC 與 DC blocker 的設計見第 02 章練習 2，
// dawlab::dsp::Saturation 內部用的是第 02 章的 OnePole highpass（README 2.4 節）。
//
// 執行：l06_ex5_solution

#include <dawlab/Common.h>
#include <dawlab/dsp/Oversampler.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

using namespace dawlab;

constexpr double kSr = 48000.0;
constexpr int kBlock = 256;

// 整數 sample 的 dry 延遲（ring buffer）。delay = 0 時輸出 = 輸入。
class DryDelay {
public:
    void prepare(int maxDelay) {
        buf_.assign(static_cast<size_t>(maxDelay + 1), 0.0f);
        pos_ = 0;
        delay_ = 0;
    }
    void setDelay(int d) noexcept { delay_ = std::clamp(d, 0, static_cast<int>(buf_.size()) - 1); }
    void reset() noexcept {
        std::fill(buf_.begin(), buf_.end(), 0.0f);
        pos_ = 0;
    }
    float processSample(float x) noexcept {
        const int size = static_cast<int>(buf_.size());
        buf_[static_cast<size_t>(pos_)] = x;                      // 先寫入 x[n]
        int r = pos_ - delay_;                                    // 再讀 x[n - delay]
        if (r < 0)
            r += size;
        const float y = buf_[static_cast<size_t>(r)];
        pos_ = (pos_ + 1 == size) ? 0 : pos_ + 1;
        return y;
    }

private:
    std::vector<float> buf_;
    int pos_ = 0, delay_ = 0;
};

// 第 k 個 notch（k = 0, 1, 2, ...）的頻率：Δ = 兩路延遲差（samples）
double predictedNotchHz(int k, int deltaSamples, double sampleRate) {
    return (2.0 * k + 1.0) * sampleRate / (2.0 * deltaSamples);
}

class ParallelSaturator {
public:
    explicit ParallelSaturator(int factorLog2) : os_(factorLog2) {}

    void prepare(double sampleRate, int maxBlock) {
        os_.prepare(sampleRate, maxBlock, 1);
        dry_.prepare(os_.getLatencySamples() + 1);
        dry_.setDelay(os_.getLatencySamples()); // 預設：對齊
        scratch_.setSize(1, maxBlock);
    }
    void reset() {
        os_.reset();
        dry_.reset();
    }
    int getLatencySamples() const noexcept { return os_.getLatencySamples(); }
    void setDryDelay(int d) noexcept { dry_.setDelay(d); }
    void setMix(float m) noexcept { mix_ = m; }
    void setDriveDb(float db) noexcept { drive_ = dbToGain(db); }

    void process(float* x, int n) {
        float* dry = scratch_.getWritePointer(0);
        for (int i = 0; i < n; ++i)
            dry[i] = dry_.processSample(x[i]); // dry 延遲 D 個 sample

        float* chans[1] = {x};
        AudioBlock block(chans, 1, n);
        AudioBlock up = os_.upsample(block);
        float* u = up.getChannelPointer(0);
        for (int i = 0; i < up.getNumSamples(); ++i)
            u[i] = std::tanh(drive_ * u[i]) / drive_; // 除以 drive：小訊號增益 = 1，dry / wet 一樣大
        os_.downsample(block);                         // x 現在是 wet（延遲 L）

        for (int i = 0; i < n; ++i)
            x[i] = dry[i] + (x[i] - dry[i]) * mix_; // (1 - m)·dry + m·wet
    }

private:
    dsp::Oversampler os_;
    DryDelay dry_;
    AudioBuffer scratch_;
    float mix_ = 0.5f;
    float drive_ = 1.0f;
};

// 小訊號（−40 dBFS）正弦波，以 256-sample block 處理，回傳 freq 處的增益（dB）
double measureGainDb(ParallelSaturator& s, double freq) {
    s.reset();
    const int n = secondsToSamples(0.25, kSr), skip = secondsToSamples(0.05, kSr);
    std::vector<float> in(static_cast<size_t>(n)), out(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        in[static_cast<size_t>(i)] = static_cast<float>(0.01 * std::sin(kTwoPi * freq * i / kSr));
    out = in;
    for (int start = 0; start < n; start += kBlock)
        s.process(out.data() + start, std::min(kBlock, n - start));
    const double a = goertzelAmplitude(in.data() + skip, n - skip, freq, kSr);
    const double b = goertzelAmplitude(out.data() + skip, n - skip, freq, kSr);
    return gainToDb(b / a, -200.0);
}

} // namespace

int main() {
    ParallelSaturator sat(2); // 4x
    sat.prepare(kSr, kBlock);
    const int L = sat.getLatencySamples();
    std::printf("4x oversampler latency L = %d samples（%.3f ms）\n", L, 1000.0 * L / kSr);
    DAWLAB_CHECK(L == 36);

    // ---- 1. 沒對齊（D = 0，Δ = 36）：comb filter ---------------------------------------------------
    sat.setDryDelay(0);
    sat.setMix(0.5f);
    std::printf("\n[沒對齊] dry 不延遲，mix 50%%：\n");
    for (int k = 0; k < 3; ++k) {
        const double f = predictedNotchHz(k, L, kSr);
        const double g = measureGainDb(sat, f);
        std::printf("  notch %d：預測 %7.1f Hz，量到 %7.1f dB\n", k, f, g);
        DAWLAB_CHECK(f > 0.0 && g < -40.0);
    }
    DAWLAB_CHECK_NEAR(predictedNotchHz(0, L, kSr), 666.667, 0.001);
    const double peak = measureGainDb(sat, kSr / L); // 相位差 2π → 兩路同相
    std::printf("  兩個 notch 中間的峰值（%.1f Hz）：%+.3f dB\n", kSr / L, peak);
    DAWLAB_CHECK_NEAR(peak, 0.0, 0.05);

    // 掃頻找第一個 notch：500～850 Hz，每 2 Hz
    double minF = 0.0, minG = 1e9;
    for (double f = 500.0; f <= 850.0; f += 2.0) {
        const double g = measureGainDb(sat, f);
        if (g < minG) {
            minG = g;
            minF = f;
        }
    }
    std::printf("  掃頻 500～850 Hz：最深處 %.0f Hz（%.1f dB）\n", minF, minG);
    DAWLAB_CHECK_NEAR(minF, 666.7, 2.0);

    // mix 30%：notch 變淺，深度 = |1 - 2m| = 0.4 → −7.96 dB
    sat.setMix(0.3f);
    const double shallow = measureGainDb(sat, predictedNotchHz(0, L, kSr));
    std::printf("  mix 30%%：667 Hz %.2f dB（理論 20·log10|1 - 2m| = %.2f dB）\n", shallow,
                20.0 * std::log10(0.4));
    DAWLAB_CHECK_NEAR(shallow, 20.0 * std::log10(0.4), 0.05);

    // ---- 2. 對齊（D = L）：完全平坦 ----------------------------------------------------------------
    sat.setDryDelay(L);
    sat.setMix(0.5f);
    std::printf("\n[對齊] dry 延遲 %d samples，mix 50%%：\n", L);
    for (double f : {666.667, 2000.0, 3333.333, 10000.0}) {
        const double g = measureGainDb(sat, f);
        std::printf("  %8.1f Hz：%+.4f dB\n", f, g);
        DAWLAB_CHECK_NEAR(g, 0.0, 0.02);
    }

    // ---- 3. 差一個 sample（D = L - 1，Δ = 1）：第一個 notch 在 24 kHz，但高頻已經在掉 ------------------
    sat.setDryDelay(L - 1);
    std::printf("\n[差 1 sample] dry 延遲 %d samples：\n", L - 1);
    for (double f : {1000.0, 10000.0}) {
        const double g = measureGainDb(sat, f);
        const double theory = 20.0 * std::log10(std::cos(kPi * f / kSr)); // |0.5 + 0.5 e^{-jω}| = |cos(ω/2)|
        std::printf("  %7.0f Hz：%+.3f dB（理論 %+.3f dB）\n", f, g, theory);
        DAWLAB_CHECK_NEAR(g, theory, 0.05);
    }

    return dawlab::test::summary("l06_ex5");
}
