// 練習 6 解答：FIR moving average vs one-pole lowpass。
//
// N 點 moving average：y[n] = (1/N) Σ_{k=0}^{N-1} x[n-k]
//   - FIR：impulse response 只有 N 個 0 以外的值，N 個 sample 後完全結束（step response 剛好 N 個 sample 到位）。
//   - 頻率響應 H(e^{jω}) = (1/N) · sin(Nω/2) / sin(ω/2) · e^{-jω(N-1)/2}：
//       * 在 f = k · fs / N（k = 1, 2, …）完全為 0（null）
//       * 第一個 null 之後還有「旁瓣」（sidelobe），第一個旁瓣約 -13 dB，不像 one-pole 持續往下掉
//       * 相位是線性的：固定延遲 (N-1)/2 個 sample（linear phase）
//   - -3 dB 點 ≈ 0.443 · fs / N
// 用一個 running sum 實作：每個 sample 只要一次加法、一次減法，和 N 無關（O(1)）。
#include <dawlab/Common.h>
#include <dawlab/dsp/OnePole.h>

#include <complex>
#include <cstdio>
#include <vector>

using namespace dawlab;
using dawlab::dsp::OnePole;

constexpr double kSampleRate = 48000.0;

class MovingAverage {
public:
    void prepare(int length, int numChannels) {
        length_ = std::max(1, length);
        history_.assign(static_cast<size_t>(numChannels), std::vector<double>(static_cast<size_t>(length_), 0.0));
        sum_.assign(static_cast<size_t>(numChannels), 0.0);
        writeIndex_.assign(static_cast<size_t>(numChannels), 0);
    }
    void reset() noexcept {
        for (auto& h : history_)
            std::fill(h.begin(), h.end(), 0.0);
        std::fill(sum_.begin(), sum_.end(), 0.0);
        std::fill(writeIndex_.begin(), writeIndex_.end(), 0);
    }
    int getLength() const noexcept { return length_; }

    float processSample(int channel, float x) noexcept {
        const auto ch = static_cast<size_t>(channel);
        auto& h = history_[ch];
        auto& idx = writeIndex_[ch];
        // running sum：加新的、減掉 N 個 sample 前的那一個
        sum_[ch] += x - h[static_cast<size_t>(idx)];
        h[static_cast<size_t>(idx)] = x;
        idx = (idx + 1) % length_;
        return static_cast<float>(sum_[ch] / length_);
    }

    void process(AudioBlock block) noexcept {
        const int numChannels = std::min(block.getNumChannels(), static_cast<int>(history_.size()));
        for (int ch = 0; ch < numChannels; ++ch) {
            float* d = block.getChannelPointer(ch);
            for (int i = 0; i < block.getNumSamples(); ++i)
                d[i] = processSample(ch, d[i]);
        }
    }

    // |H(e^{jω})| = |sin(Nω/2) / (N sin(ω/2))|
    double magnitudeDb(double f, double sampleRate) const {
        const double w = kTwoPi * f / sampleRate;
        if (std::abs(w) < 1e-12)
            return 0.0;
        return gainToDb(std::abs(std::sin(length_ * w * 0.5) / (length_ * std::sin(w * 0.5))));
    }

private:
    int length_ = 1;
    std::vector<std::vector<double>> history_;
    std::vector<double> sum_;
    std::vector<int> writeIndex_;
};

int main() {
    const int N = 8;
    MovingAverage ma;
    ma.prepare(N, 1);
    const double firstNull = kSampleRate / N;        // 6 kHz
    const double f3Approx = 0.443 * kSampleRate / N; // ≈ 2658 Hz

    // 找 moving average 真正的 -3 dB 點，然後用同樣的 cutoff 設一個 one-pole 做對照
    double f3 = 10.0;
    while (ma.magnitudeDb(f3, kSampleRate) > -3.0103)
        f3 *= 1.0005;
    OnePole op;
    op.prepare(kSampleRate, 512, 1);
    op.setCutoff(static_cast<float>(f3));
    std::printf("N = %d 的 moving average：第一個 null 在 fs/N = %.0f Hz，-3 dB 點 = %.0f Hz（近似 0.443 fs/N = %.0f）\n\n",
                N, firstNull, f3, f3Approx);
    DAWLAB_CHECK_NEAR(f3, f3Approx, f3Approx * 0.02);

    std::printf("%9s  %12s  %12s  %12s\n", "f (Hz)", "MA theory", "MA measured", "one-pole");
    for (double f : {100.0, 1000.0, f3, 4000.0, firstNull, 1.5 * firstNull, 2.0 * firstNull, 20000.0}) {
        ma.reset();
        op.reset();
        const double theory = ma.magnitudeDb(f, kSampleRate);
        const double meas = measureGainDb([&](float x) { return ma.processSample(0, x); }, f, kSampleRate, 0.5, 0.1);
        const double onePole = measureGainDb([&](float x) { return op.processSample(0, x); }, f, kSampleRate, 0.5, 0.2);
        std::printf("%9.0f  %12.2f  %12.2f  %12.2f\n", f, theory, meas, onePole);
        if (theory > -50.0)
            DAWLAB_CHECK_NEAR(meas, theory, 0.1);
        else
            DAWLAB_CHECK(meas < -50.0);
    }
    // 1. null：fs/N 與 2 fs/N 完全消失；one-pole 在那裡只有幾 dB 的衰減
    // gainToDb() 的下限是 -100 dB（kMinusInfinityDb），null 處會印 -100.00
    DAWLAB_CHECK(ma.magnitudeDb(firstNull, kSampleRate) <= -99.9);
    DAWLAB_CHECK(ma.magnitudeDb(2.0 * firstNull, kSampleRate) <= -99.9);
    // 2. 旁瓣：第一個 null 之後在 1.5 fs/N 又「回升」到約 -13 dB（不是持續下降）
    //    精確值：|sin(1.5π)/(8 sin(1.5π/8))| = 1/(8 sin(0.589)) = 0.2250 → -12.96 dB
    const double sidelobe = ma.magnitudeDb(1.5 * firstNull, kSampleRate);
    std::printf("\n第一個旁瓣（1.5 fs/N = %.0f Hz）= %.2f dB；one-pole 在同一頻率 = %.2f dB\n", 1.5 * firstNull, sidelobe,
                measureGainDb([&](float x) { return op.processSample(0, x); }, 1.5 * firstNull, kSampleRate));
    DAWLAB_CHECK_NEAR(sidelobe, -12.96, 0.05);
    // 3. 兩者在 DC 都是 0 dB
    DAWLAB_CHECK_NEAR(ma.magnitudeDb(0.0, kSampleRate), 0.0, 1e-9);

    // 4. step response：moving average 剛好 N 個 sample 到位；one-pole 永遠只是「趨近」
    ma.reset();
    op.reset();
    int maSettle = -1, opSettle = -1;
    for (int i = 0; i < 4096; ++i) {
        const float a = ma.processSample(0, 1.0f);
        const float b = op.processSample(0, 1.0f);
        if (maSettle < 0 && std::abs(a - 1.0f) < 1e-6f)
            maSettle = i + 1;
        if (opSettle < 0 && std::abs(b - 1.0f) < 0.01f)
            opSettle = i + 1;
    }
    const double tau = -1.0 / std::log(op.getCoefficient());
    std::printf("step response：moving average 第 %d 個 sample 就剛好等於 1；one-pole 到 99%% 要 %d samples（τ = %.1f，4.6τ ≈ %.0f）\n",
                maSettle, opSettle, tau, 4.605 * tau);
    DAWLAB_CHECK(maSettle == N);
    DAWLAB_CHECK_NEAR(opSettle, 4.605 * tau, 2.0);

    // 5. impulse response：FIR 有限長（第 N 個 sample 後全 0），IIR 無限長
    ma.reset();
    op.reset();
    std::printf("impulse response：MA  = ");
    for (int i = 0; i < N + 2; ++i)
        std::printf("%.3f ", ma.processSample(0, i == 0 ? 1.0f : 0.0f));
    std::printf("\n                  1-p = ");
    float opTail = 0.0f;
    for (int i = 0; i < N + 2; ++i)
        std::printf("%.3f ", opTail = op.processSample(0, i == 0 ? 1.0f : 0.0f));
    std::printf("...\n");
    DAWLAB_CHECK(opTail > 0.0f);

    // 6. linear phase：moving average 的群延遲固定 (N-1)/2 個 sample。
    //    驗證：把 1 kHz 正弦送進去，量輸出相對輸入的相位 → 延遲 = -phase / ω
    {
        const double f = 1000.0, w = kTwoPi * f / kSampleRate;
        ma.reset();
        const int n = 4800;
        std::vector<float> in(static_cast<size_t>(n)), out(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            in[static_cast<size_t>(i)] = static_cast<float>(std::sin(w * i));
            out[static_cast<size_t>(i)] = ma.processSample(0, in[static_cast<size_t>(i)]);
        }
        // 用正交解調估相位：Σ y · e^{-jωn}（跳過前 N 個 sample 的 transient）
        std::complex<double> accIn = 0.0, accOut = 0.0;
        for (int i = N; i < n; ++i) {
            const std::complex<double> e = std::polar(1.0, -w * i);
            accIn += static_cast<double>(in[static_cast<size_t>(i)]) * e;
            accOut += static_cast<double>(out[static_cast<size_t>(i)]) * e;
        }
        const double phaseShift = std::arg(accOut / accIn); // 負值 = 延遲
        const double delaySamples = -phaseShift / w;
        std::printf("1 kHz 的群延遲：%.3f samples（理論 (N-1)/2 = %.1f）\n", delaySamples, (N - 1) / 2.0);
        DAWLAB_CHECK_NEAR(delaySamples, (N - 1) / 2.0, 0.01);
    }

    // 聽一下：白雜訊過 moving average（有梳狀 null）vs one-pole
    AudioBuffer noise = makeWhiteNoise(kSampleRate, 2.0, 0.25f, 2);
    AudioBuffer outMa = noise, outOp = noise;
    MovingAverage ma2;
    ma2.prepare(N, 2);
    OnePole op2;
    op2.prepare(kSampleRate, 512, 2);
    op2.setCutoff(static_cast<float>(f3));
    AudioBlock wm(outMa), wo(outOp);
    for (int start = 0; start < noise.getNumSamples(); start += 512) {
        const int len = std::min(512, noise.getNumSamples() - start);
        ma2.process(wm.getSubBlock(start, len));
        op2.process(wo.getSubBlock(start, len));
    }
    writeWav(outputPath("l02_ex6_noise_moving_average.wav"), outMa, kSampleRate);
    writeWav(outputPath("l02_ex6_noise_one_pole.wav"), outOp, kSampleRate);
    std::printf("雜訊 RMS：dry %.2f dB → moving average %.2f dB，one-pole %.2f dB\n", gainToDb(rms(noise, 0)),
                gainToDb(rms(outMa, 0)), gainToDb(rms(outOp, 0)));
    DAWLAB_CHECK(!hasNanOrInf(outMa) && !hasNanOrInf(outOp));

    return dawlab::test::summary("l02_ex6");
}
