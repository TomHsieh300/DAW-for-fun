// 練習 2：DC blocker（起始碼）。
//
// 實作一階的 DC blocker：
//     y[n] = x[n] - x[n-1] + R y[n-1]        H(z) = (1 - z^-1) / (1 - R z^-1)
// 並驗證：直流被移除、1 kHz 幾乎不受影響、-3 dB 點 ≈ fs (1 - R) / (2π)。
//
// 完成所有 TODO 後，執行時 self-check 應該全部通過。
#include <dawlab/Common.h>

#include <complex>
#include <cstdio>
#include <vector>

using namespace dawlab;

constexpr double kSampleRate = 48000.0;

class DcBlocker {
public:
    void prepare(double /*sampleRate*/, int /*maxBlockSize*/, int numChannels) {
        state_.assign(static_cast<size_t>(numChannels), State{});
    }
    void reset() noexcept {
        for (auto& s : state_)
            s = State{};
    }
    void setR(float r) noexcept { r_ = r; }

    float processSample(int channel, float x) noexcept {
        State& s = state_[static_cast<size_t>(channel)];
        // TODO: 依 difference equation 計算 y，並更新 s.x1 / s.y1
        (void)s;
        return x;
    }

    void process(AudioBlock block) noexcept {
        for (int ch = 0; ch < block.getNumChannels(); ++ch) {
            float* d = block.getChannelPointer(ch);
            for (int i = 0; i < block.getNumSamples(); ++i)
                d[i] = processSample(ch, d[i]);
        }
    }

    // 理論響應：把 z = e^{jω} 代入 H(z)
    double magnitudeDb(double f, double sampleRate) const {
        // TODO: 回傳 20 log10 |H(e^{jω})|，ω = 2π f / fs
        (void)f;
        (void)sampleRate;
        return 0.0;
    }

private:
    struct State {
        double x1 = 0.0, y1 = 0.0;
    };
    double r_ = 0.995;
    std::vector<State> state_;
};

int main() {
    const float R = 0.995f;
    DcBlocker dc;
    dc.prepare(kSampleRate, 512, 1);
    dc.setR(R);

    // 1. 輸入 = 0.5 的直流 + 0.25 的 1 kHz 正弦；輸出的平均值（DC）應該趨近 0，1 kHz 幾乎不變
    const int n = 48000;
    AudioBuffer x(1, n);
    for (int i = 0; i < n; ++i)
        x.setSample(0, i, 0.5f + static_cast<float>(0.25 * std::sin(kTwoPi * 1000.0 * i / kSampleRate)));
    AudioBuffer y = x;
    dc.process(y);

    double meanOut = 0.0;
    for (int i = n / 2; i < n; ++i)
        meanOut += y.getSample(0, i);
    meanOut /= n / 2;
    const double sineIn = goertzelAmplitude(x.getReadPointer(0) + n / 2, n / 2, 1000.0, kSampleRate);
    const double sineOut = goertzelAmplitude(y.getReadPointer(0) + n / 2, n / 2, 1000.0, kSampleRate);
    std::printf("DC：輸出平均 %.6f；1 kHz 增益 %.3f dB\n", meanOut, gainToDb(sineOut / sineIn));
    DAWLAB_CHECK(std::abs(meanOut) < 1e-3);
    DAWLAB_CHECK_NEAR(gainToDb(sineOut / sineIn), 0.0, 0.05);

    // 2. -3 dB 點：理論近似 fs (1 - R) / (2π)
    const double f3Approx = kSampleRate * (1.0 - R) / kTwoPi;
    double f3 = 1.0;
    while (f3 < 1000.0 && dc.magnitudeDb(f3, kSampleRate) < -3.0103)
        f3 *= 1.001;
    std::printf("-3 dB 點：理論近似 %.1f Hz，由 H(z) 算出 %.1f Hz\n", f3Approx, f3);
    DAWLAB_CHECK_NEAR(f3, f3Approx, 1.0);

    // 3. 實測
    dc.reset();
    const double measured3 = measureGainDb([&](float v) { return dc.processSample(0, v); }, f3, kSampleRate, 1.0, 0.5);
    std::printf("實測：%.1f Hz = %.2f dB\n", f3, measured3);
    DAWLAB_CHECK_NEAR(measured3, -3.01, 0.1);

    return dawlab::test::summary("l02_ex2_starter");
}
