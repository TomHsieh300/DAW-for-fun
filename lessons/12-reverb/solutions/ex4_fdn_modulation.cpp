// 練習 4 解答：為 FDN 加 modulation，量化 metallic ringing
//
// 自己寫一個 4 線 FDN（比 Reverb.h 的 8 線版本小，方便看清楚）：
//
//   v_i = delay_i 的輸出（延遲 L_i + depth·sin(φ + i·π/2) 個 sample，用 readCubic 插值）
//   u   = A·v，A = I - (2/N)·1·1ᵀ（Householder；Aᵀ A = I → 無損）
//   delay_i ← g_i · lowpass_i(u_i) + x        g_i = 10^(-3·L_i/(RT60·fs))
//   y   = v_0 - v_1 + v_2 - v_3
//
// 「metallic ringing」的量化：4 條線的 FDN 模態密度只有 Σ L_i / fs ≈ 0.14 個 mode / Hz（每 7 Hz 一個），
// 尾巴的頻譜是一根根分開的尖峰 → 聽起來有音高、像敲金屬管。把 delay 長度緩慢調變，mode 的頻率
// 會隨時間漂移，長時間平均下來頻譜就變平滑。這裡用「尾巴頻譜（dB）在 100 Hz .. 6 kHz 的標準差」
// 當作 roughness 指標，modulation 開啟後應該明顯下降。
//
// 輸出：dawlab_output/l12_ex4_fdn_static.wav、l12_ex4_fdn_modulated.wav（都是 impulse response）

#include <dawlab/Common.h>
#include <dawlab/dsp/Fft.h>
#include <dawlab/dsp/FractionalDelayLine.h>
#include <dawlab/dsp/OnePole.h>
#include <dawlab/dsp/ReverbBuildingBlocks.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace dawlab;
using namespace dawlab::dsp;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

class Fdn4 {
public:
    static constexpr int kN = 4;
    static constexpr float kMaxDepthMs = 4.0f;

    void prepare(double sampleRate) {
        sampleRate_ = sampleRate;
        const int maxDepth = msToSamples(kMaxDepthMs, sampleRate);
        int maxDelay = 0;
        for (int i = 0; i < kN; ++i) {
            // 轉成 samples 後取下一個質數 → 互質
            delay_[static_cast<size_t>(i)] = static_cast<float>(nextPrime(msToSamples(kBaseMs[static_cast<size_t>(i)], sampleRate)));
            maxDelay = std::max(maxDelay, static_cast<int>(delay_[static_cast<size_t>(i)]));
        }
        lines_.prepare(maxDelay + maxDepth + 4, kN);
        damping_.prepare(sampleRate, 1, kN);
        damping_.setType(OnePole::Type::Lowpass);
        setDampingHz(dampingHz_);
        setDecaySeconds(decaySeconds_);
        setModulation(depthMs_, rateHz_);
        reset();
    }

    void reset() noexcept {
        lines_.reset();
        damping_.reset();
        phase_ = 0.0f;
    }

    void setDecaySeconds(float rt60) noexcept {
        decaySeconds_ = rt60;
        for (int i = 0; i < kN; ++i)
            gain_[static_cast<size_t>(i)] =
                lossless_ ? 1.0f : CombFilter::feedbackForRt60(static_cast<int>(delay_[static_cast<size_t>(i)]), rt60, sampleRate_);
    }
    // g_i = 1 且不 damping：用來驗證 Householder 矩陣真的無損
    void setLossless(bool on) noexcept {
        lossless_ = on;
        setDecaySeconds(decaySeconds_);
    }
    // >= 20 kHz 視為關閉 damping（就算 cutoff 20 kHz，one-pole 在 Nyquist 仍有 -1.3 dB，
    // 每繞一圈都乘一次，累積起來會讓高頻 RT60 明顯變短）
    void setDampingHz(float hz) noexcept {
        dampingHz_ = hz;
        dampingEnabled_ = hz < 20000.0f;
        damping_.setCutoff(std::min(hz, static_cast<float>(sampleRate_ * 0.45)));
    }
    void setModulation(float depthMs, float rateHz) noexcept {
        depthMs_ = std::clamp(depthMs, 0.0f, kMaxDepthMs);
        rateHz_ = rateHz;
        depth_ = static_cast<float>(depthMs_ * 0.001 * sampleRate_);
        increment_ = static_cast<float>(kTwoPi * rateHz / sampleRate_);
    }
    float getDelaySamples(int i) const noexcept { return delay_[static_cast<size_t>(i)]; }

    float processSample(float x) noexcept {
        // 1. 讀 4 條線（含 modulation：每條線 LFO 相位差 90°）
        phase_ += increment_;
        if (phase_ >= static_cast<float>(kTwoPi))
            phase_ -= static_cast<float>(kTwoPi);
        std::array<float, kN> v{};
        float sum = 0.0f;
        for (int i = 0; i < kN; ++i) {
            const float mod = depth_ > 0.0f ? depth_ * std::sin(phase_ + static_cast<float>(i) * 1.5707964f) : 0.0f;
            // 先讀後寫：readCubic(d) 回傳 d+1 個 sample 前寫入的值，所以讀 L_i - 1 → 迴路長度剛好 L_i
            v[static_cast<size_t>(i)] = lines_.readCubic(i, delay_[static_cast<size_t>(i)] - 1.0f + mod);
            sum += v[static_cast<size_t>(i)];
        }
        // 2. Householder：u_i = v_i - (2/N)·Σv
        const float scale = 2.0f / static_cast<float>(kN);
        for (int i = 0; i < kN; ++i) {
            float u = v[static_cast<size_t>(i)] - scale * sum;
            if (!lossless_ && dampingEnabled_)
                u = damping_.processSample(i, u);
            u = u * gain_[static_cast<size_t>(i)] + x;
            lines_.push(i, reverb_detail::flushDenormal(u));
        }
        // 3. 輸出：正負交錯相加
        return v[0] - v[1] + v[2] - v[3];
    }

private:
    static constexpr std::array<double, kN> kBaseMs = {23.4, 32.5, 40.6, 46.9};

    double sampleRate_ = 48000.0;
    FractionalDelayLine lines_;
    OnePole damping_;
    std::array<float, kN> delay_{};
    std::array<float, kN> gain_{};
    float decaySeconds_ = 2.0f;
    float dampingHz_ = 20000.0f;
    bool lossless_ = false;
    bool dampingEnabled_ = false;
    float depthMs_ = 0.0f, rateHz_ = 1.0f, depth_ = 0.0f, increment_ = 0.0f, phase_ = 0.0f;
};

AudioBuffer impulseResponse(Fdn4& fdn, double seconds) {
    AudioBuffer ir = makeImpulse(secondsToSamples(seconds, kSampleRate), 1);
    fdn.reset();
    float* d = ir.getWritePointer(0);
    for (int start = 0; start < ir.getNumSamples(); start += kBlockSize) {
        const int len = std::min(kBlockSize, ir.getNumSamples() - start);
        for (int i = 0; i < len; ++i)
            d[start + i] = fdn.processSample(d[start + i]);
    }
    return ir;
}

// 尾巴頻譜的「粗糙度」：Hann window + FFT，取 fLo..fHi 的 bin，算 magnitude(dB) 的標準差
double spectralRoughnessDb(const float* x, int fftOrder, double fLo, double fHi) {
    Fft fft(fftOrder);
    const int n = fft.getSize();
    std::vector<float> window;
    Fft::makeWindow(window, n, WindowType::Hann);
    std::vector<float> frame(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        frame[static_cast<size_t>(i)] = x[i] * window[static_cast<size_t>(i)];
    std::vector<std::complex<float>> spec(static_cast<size_t>(fft.getNumRealBins()));
    fft.forwardReal(frame.data(), spec.data());

    const double binHz = kSampleRate / n;
    std::vector<double> db;
    for (int k = static_cast<int>(fLo / binHz); k <= static_cast<int>(fHi / binHz); ++k)
        db.push_back(gainToDb(std::max(1e-12, static_cast<double>(std::abs(spec[static_cast<size_t>(k)])))));
    double mean = 0.0;
    for (double v : db)
        mean += v;
    mean /= static_cast<double>(db.size());
    double var = 0.0;
    for (double v : db)
        var += (v - mean) * (v - mean);
    return std::sqrt(var / static_cast<double>(db.size()));
}

} // namespace

int main() {
    std::printf("=== 練習 4：FDN 的 modulation 與 metallic ringing ===\n");

    Fdn4 fdn;
    fdn.prepare(kSampleRate);
    std::printf("delay 線長度（samples）：");
    for (int i = 0; i < Fdn4::kN; ++i)
        std::printf("%s%.0f", i ? ", " : "", fdn.getDelaySamples(i));
    std::printf("（都是質數）\n");

    // ---- 1. Householder 無損驗證：g = 1、無 damping、無 modulation → 能量不衰減 -----------
    {
        fdn.setLossless(true);
        fdn.setModulation(0.0f, 1.0f);
        AudioBuffer ir = impulseResponse(fdn, 3.0);
        const int w = secondsToSamples(0.5, kSampleRate);
        const float e1 = rms(ir.getReadPointer(0) + secondsToSamples(1.0, kSampleRate), w);
        const float e2 = rms(ir.getReadPointer(0) + secondsToSamples(2.5, kSampleRate), w);
        std::printf("lossless：RMS @1.0s = %.4f, @2.5s = %.4f（差 %+.2f dB，應接近 0）\n", e1, e2, gainToDb(e2 / e1));
        DAWLAB_CHECK_NEAR(gainToDb(e2 / e1), 0.0, 1.0);
        DAWLAB_CHECK(!hasNanOrInf(ir));
        fdn.setLossless(false);
    }

    // ---- 2. RT60 驗證（無 modulation）-------------------------------------------------
    fdn.setDecaySeconds(2.0f);
    fdn.setDampingHz(20000.0f);
    AudioBuffer irStatic = impulseResponse(fdn, 2.6);
    {
        const DecayMeasurement m = measureRt60(irStatic.getReadPointer(0), irStatic.getNumSamples(), kSampleRate, 20.0);
        std::printf("RT60 設定 2.0 s → 量測 %.3f s\n", m.rt60Seconds);
        DAWLAB_CHECK(m.valid);
        DAWLAB_CHECK_NEAR(m.rt60Seconds, 2.0, 0.3);
    }

    // ---- 3. modulation 對尾巴頻譜的影響 ------------------------------------------------
    fdn.setModulation(1.0f, 1.3f);
    AudioBuffer irMod = impulseResponse(fdn, 2.6);
    {
        const int start = secondsToSamples(0.4, kSampleRate);
        const double rStatic = spectralRoughnessDb(irStatic.getReadPointer(0) + start, 14, 100.0, 6000.0);
        const double rMod = spectralRoughnessDb(irMod.getReadPointer(0) + start, 14, 100.0, 6000.0);
        std::printf("尾巴頻譜 dB 標準差（0.4 s 起、16384 點 FFT、100 Hz..6 kHz）：static %.2f dB, modulated %.2f dB\n", rStatic,
                    rMod);
        DAWLAB_CHECK(rMod < rStatic - 1.0);
        // modulation 不應明顯改變整體衰減
        const DecayMeasurement m = measureRt60(irMod.getReadPointer(0), irMod.getNumSamples(), kSampleRate, 20.0);
        std::printf("modulated 的 RT60 量測 %.3f s\n", m.rt60Seconds);
        DAWLAB_CHECK_NEAR(m.rt60Seconds, 2.0, 0.4);
        DAWLAB_CHECK(!hasNanOrInf(irMod));
    }
    writeWav(outputPath("l12_ex4_fdn_static.wav"), irStatic, kSampleRate);
    writeWav(outputPath("l12_ex4_fdn_modulated.wav"), irMod, kSampleRate);

    // ---- 4. 太深的 modulation：pitch wobble（只印數字，自己聽聽看）------------------------
    {
        fdn.setModulation(4.0f, 1.3f);
        AudioBuffer deep = impulseResponse(fdn, 2.6);
        const int start = secondsToSamples(0.4, kSampleRate);
        // 最大 pitch 偏移 = 2π·f_LFO·depth：1 ms → 0.82%（約 14 cents）；4 ms → 3.3%（約 56 cents）
        const double maxPitchDev = kTwoPi * 1.3 * 0.004;
        std::printf("depth 4 ms：頻譜標準差 %.2f dB（更平，但最大 pitch 偏移 %.1f%% ≈ %.0f cents → 聽得到音高晃動）\n",
                    spectralRoughnessDb(deep.getReadPointer(0) + start, 14, 100.0, 6000.0), maxPitchDev * 100.0,
                    1200.0 * std::log2(1.0 + maxPitchDev));
    }

    return dawlab::test::summary("l12_ex4");
}
