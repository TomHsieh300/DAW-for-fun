// 練習 2 解答：DC blocker。
//
//     y[n] = x[n] - x[n-1] + R y[n-1]        H(z) = (1 - z^-1) / (1 - R z^-1)
//
// zero 在 z = 1（DC 完全消失），pole 在 z = R（R 越接近 1，cutoff 越低、但 transient 越長）。
// -3 dB 點 ≈ fs (1 - R) / (2π)。R = 0.995, fs = 48 kHz → 約 38 Hz。
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
        const double y = x - s.x1 + r_ * s.y1;
        s.x1 = x;
        s.y1 = y;
        return static_cast<float>(y);
    }

    void process(AudioBlock block) noexcept {
        for (int ch = 0; ch < block.getNumChannels(); ++ch) {
            float* d = block.getChannelPointer(ch);
            for (int i = 0; i < block.getNumSamples(); ++i)
                d[i] = processSample(ch, d[i]);
        }
    }

    // 理論響應：把 z = e^{jω} 代入
    double magnitudeDb(double f, double sampleRate) const {
        const std::complex<double> z1 = std::polar(1.0, -kTwoPi * f / sampleRate);
        return gainToDb(std::abs((1.0 - z1) / (1.0 - r_ * z1)));
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

    double meanIn = 0.0, meanOut = 0.0;
    for (int i = n / 2; i < n; ++i) { // 跳過前半段的 transient
        meanIn += x.getSample(0, i);
        meanOut += y.getSample(0, i);
    }
    meanIn /= n / 2;
    meanOut /= n / 2;
    const double sineIn = goertzelAmplitude(x.getReadPointer(0) + n / 2, n / 2, 1000.0, kSampleRate);
    const double sineOut = goertzelAmplitude(y.getReadPointer(0) + n / 2, n / 2, 1000.0, kSampleRate);
    std::printf("DC：輸入平均 %.4f → 輸出平均 %.6f\n", meanIn, meanOut);
    std::printf("1 kHz：輸入振幅 %.4f → 輸出振幅 %.4f（%.3f dB）\n", sineIn, sineOut, gainToDb(sineOut / sineIn));
    DAWLAB_CHECK(std::abs(meanOut) < 1e-3);
    DAWLAB_CHECK_NEAR(gainToDb(sineOut / sineIn), 0.0, 0.05);

    // 2. -3 dB 點：理論近似 fs (1 - R) / (2π)
    const double f3Approx = kSampleRate * (1.0 - R) / kTwoPi;
    double f3 = 1.0;
    while (dc.magnitudeDb(f3, kSampleRate) < -3.0103)
        f3 *= 1.001;
    std::printf("-3 dB 點：理論近似 %.1f Hz，由 H(z) 算出 %.1f Hz\n", f3Approx, f3);
    DAWLAB_CHECK_NEAR(f3, f3Approx, 1.0);

    // 3. 實測：用 measureGainDb 在 -3 dB 點附近與 20 Hz 量
    dc.reset();
    const double measured3 = measureGainDb([&](float v) { return dc.processSample(0, v); }, f3, kSampleRate, 1.0, 0.5);
    dc.reset();
    const double measured20 = measureGainDb([&](float v) { return dc.processSample(0, v); }, 20.0, kSampleRate, 1.0, 0.5);
    std::printf("實測：%.1f Hz = %.2f dB，20 Hz = %.2f dB（理論 %.2f dB）\n", f3, measured3, measured20,
                dc.magnitudeDb(20.0, kSampleRate));
    DAWLAB_CHECK_NEAR(measured3, -3.01, 0.1);
    DAWLAB_CHECK_NEAR(measured20, dc.magnitudeDb(20.0, kSampleRate), 0.1);

    // 4. 一個 step（直流突然出現）要多久才消失？時間常數 τ = -1/ln(R) samples
    const double tauSamples = -1.0 / std::log(static_cast<double>(R));
    dc.reset();
    int settle = 0;
    for (int i = 0; i < n; ++i) {
        const float v = dc.processSample(0, 1.0f);
        if (std::abs(v) > 0.01f)
            settle = i;
    }
    std::printf("step response：τ = %.0f samples（%.1f ms），降到 1%% 需要 %d samples（理論 ≈ 4.6 τ = %.0f）\n",
                tauSamples, samplesToMs(tauSamples, kSampleRate), settle, 4.605 * tauSamples);
    DAWLAB_CHECK_NEAR(settle, 4.605 * tauSamples, 5.0);

    return dawlab::test::summary("l02_ex2");
}
