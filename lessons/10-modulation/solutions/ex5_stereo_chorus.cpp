// 練習 5 解答：Stereo chorus —— 左右各一個 LFO，相位差 90°，量左右相關係數
//
// 單聲道 chorus 複製到左右兩邊，聲像仍然在正中間（左右完全相同，相關係數 = 1）。
// 讓左右聲道的 LFO 錯開 0.25 個週期（90°；右聲道的起始相位 = 0.25）：
//   左：d_L(t) = d0 + D·sin(2πrt)         右：d_R(t) = d0 + D·sin(2πrt + π/2) = d0 + D·cos(2πrt)
// 左右的延遲（因此走音的方向與量）不同步 → 兩耳聽到的訊號不再相同 → 聲像變寬。
//
// 為什麼常用 90°？對白雜訊這種寬頻素材，90° 和 180° 的相關係數都接近 0（下面會量到）；
// 差別在「動態」：180° 時左右的延遲永遠一個最長、一個最短，走音方向完全相反，聲像在左右之間
// 對稱地來回拉扯；90° 時左右輪流領先，聽起來是比較均勻、會旋轉的寬度。這是聽感的選擇，不是數學上的最佳解。
//
// 量測：輸入是「左右相同」的單聲道素材（白雜訊複製兩邊），輸出的 Pearson 相關係數
//   ρ = Σ L·R / sqrt(Σ L² · Σ R²)
// self-check：
//   1. phase offset 0 → ρ > 0.999（左右完全相同）
//   2. phase offset 0.25（90°）、mix 1 → ρ < 0.5（純濕訊號，左右明顯不同）
//   3. phase offset 0.25、mix 0.5 → ρ 介於上面兩者之間（乾訊號左右相同把相關係數拉高）
//   3b. 相位差真的是 90°：rate 0、impulse 輸入，左聲道延遲 = centre（15 ms）、右聲道 = centre + depth（18 ms）。
//       （白雜訊的 ρ 對相位差不敏感——任何非 0 的相位差都讓 ρ 接近 0，所以第 2、3、5 項分辨不出 0.25 和 0.5。）
//   4. mono 相容性：(L + R)/2 的 RMS 相對單一聲道的 RMS ≈ −1.25 dB（理論見下），且不低於 −3 dB
//   5. 和第 10 章 Chorus（setStereoSpread(1)、1 voice）的結果一致（相關係數差 < 0.05）
// 輸出：dawlab_output/l10_ex5_stereo_chorus.wav（撥弦琶音）

#include <dawlab/Common.h>
#include <dawlab/dsp/Chorus.h>
#include <dawlab/dsp/FractionalDelayLine.h>
#include <dawlab/dsp/Lfo.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

using dawlab::dsp::Chorus;
using dawlab::dsp::FractionalDelayLine;
using dawlab::dsp::Lfo;
using dawlab::dsp::LfoShape;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

class StereoChorus {
public:
    static constexpr float kMaxDelayMs = 50.0f;

    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        (void)maxBlockSize;
        (void)numChannels; // 永遠處理兩個聲道
        sampleRate_ = sampleRate;
        delay_.prepare(dawlab::msToSamples(kMaxDelayMs, sampleRate) + 8, 2);
        for (Lfo& l : lfo_)
            l.prepare(sampleRate);
        reset();
    }

    void reset() noexcept {
        delay_.reset();
        lfo_[0].setPhase(0.0f);
        lfo_[1].setPhase(phaseOffset_); // 右聲道的 LFO 從 offset 開始 → 兩個 LFO 永遠相差 offset 個週期
        for (Lfo& l : lfo_)
            l.reset();
    }

    void setRateHz(float hz) noexcept {
        for (Lfo& l : lfo_)
            l.setRateHz(hz);
    }
    void setDepthMs(float ms) noexcept { depthMs_ = std::clamp(ms, 0.0f, 20.0f); }
    void setCentreDelayMs(float ms) noexcept { centreMs_ = std::clamp(ms, 1.0f, kMaxDelayMs - 20.0f); }
    void setMix(float mix) noexcept { mix_ = std::clamp(mix, 0.0f, 1.0f); }
    // 左右 LFO 的相位差（0..1 個週期；0.25 = 90°）。改變後要 reset() 才生效。
    void setPhaseOffset(float offset01) noexcept { phaseOffset_ = std::clamp(offset01, 0.0f, 1.0f); }

    void process(dawlab::AudioBlock block) noexcept {
        const int n = block.getNumSamples();
        const int nch = std::min(block.getNumChannels(), 2);
        const float toSamples = static_cast<float>(sampleRate_ * 0.001);
        const float maxRead = static_cast<float>(delay_.getMaxDelaySamples() - 3);
        for (int i = 0; i < n; ++i) {
            for (int ch = 0; ch < nch; ++ch) {
                float* d = block.getChannelPointer(ch);
                const float x = d[i];
                delay_.push(ch, x);
                const float delayMs = centreMs_ + depthMs_ * lfo_[ch].getNextValue();
                const float wet = delay_.readCubic(ch, std::clamp(delayMs * toSamples, 2.0f, maxRead));
                d[i] = (1.0f - mix_) * x + mix_ * wet;
            }
        }
    }

private:
    double sampleRate_ = 48000.0;
    float depthMs_ = 3.0f, centreMs_ = 15.0f, mix_ = 0.5f, phaseOffset_ = 0.25f;
    FractionalDelayLine delay_;
    Lfo lfo_[2];
};

template <typename Effect>
void processInBlocks(Effect& fx, dawlab::AudioBuffer& buffer) {
    dawlab::AudioBlock whole(buffer);
    for (int start = 0; start < buffer.getNumSamples(); start += kBlockSize) {
        const int n = std::min(kBlockSize, buffer.getNumSamples() - start);
        fx.process(whole.getSubBlock(start, n));
    }
}

double correlation(const dawlab::AudioBuffer& b, int skip) {
    const float* l = b.getReadPointer(0);
    const float* r = b.getReadPointer(1);
    double sll = 0.0, srr = 0.0, slr = 0.0;
    for (int i = skip; i < b.getNumSamples(); ++i) {
        sll += double(l[i]) * l[i];
        srr += double(r[i]) * r[i];
        slr += double(l[i]) * r[i];
    }
    return (sll > 0.0 && srr > 0.0) ? slr / std::sqrt(sll * srr) : 0.0;
}

dawlab::AudioBuffer dualMono(double seconds, uint32_t seed) {
    dawlab::AudioBuffer mono = dawlab::makeWhiteNoise(kSampleRate, seconds, 0.25f, 1, seed);
    dawlab::AudioBuffer b(2, mono.getNumSamples());
    b.copyFrom(0, 0, mono, 0, 0, mono.getNumSamples());
    b.copyFrom(1, 0, mono, 0, 0, mono.getNumSamples());
    return b;
}

double runAndCorrelate(float phaseOffset, float mix, dawlab::AudioBuffer* keep = nullptr) {
    dawlab::AudioBuffer in = dualMono(3.0, 21u);
    StereoChorus c;
    c.prepare(kSampleRate, kBlockSize, 2);
    c.setRateHz(0.8f);
    c.setDepthMs(3.0f);
    c.setCentreDelayMs(15.0f);
    c.setMix(mix);
    c.setPhaseOffset(phaseOffset);
    c.reset();
    processInBlocks(c, in);
    if (keep)
        *keep = in;
    return correlation(in, dawlab::secondsToSamples(0.5, kSampleRate));
}

// 相位差的直接驗證：rate 0 讓 LFO 停在起始相位，送進一個 impulse，
// 輸出 impulse 的位置就是那個聲道的延遲：d = centre + depth·sin(2π·phase)。
// （白雜訊的 ρ 對「相位差是多少」不敏感：任何非 0 的相位差都讓 ρ 接近 0，所以要另外量。）
void impulsePeakDelaysMs(float phaseOffset, double& leftMs, double& rightMs) {
    dawlab::AudioBuffer b(2, dawlab::secondsToSamples(0.1, kSampleRate));
    b.setSample(0, 0, 1.0f);
    b.setSample(1, 0, 1.0f);
    StereoChorus c;
    c.prepare(kSampleRate, kBlockSize, 2);
    c.setRateHz(0.0f); // LFO 停住
    c.setDepthMs(3.0f);
    c.setCentreDelayMs(15.0f);
    c.setMix(1.0f);
    c.setPhaseOffset(phaseOffset);
    c.reset();
    processInBlocks(c, b);
    double ms[2] = {0.0, 0.0};
    for (int ch = 0; ch < 2; ++ch) {
        const float* d = b.getReadPointer(ch);
        int best = 0;
        for (int i = 1; i < b.getNumSamples(); ++i)
            if (std::abs(d[i]) > std::abs(d[best]))
                best = i;
        ms[ch] = best * 1000.0 / kSampleRate;
    }
    leftMs = ms[0];
    rightMs = ms[1];
}

} // namespace

int main() {
    std::printf("=== 第 10 章 練習 5：Stereo chorus ===\n\n");
    const int skip = dawlab::secondsToSamples(0.5, kSampleRate);

    // ---- 1–3. 相位差 vs 相關係數 ------------------------------------------------
    std::printf("白雜訊（左右相同）→ StereoChorus，rate 0.8 Hz、depth 3 ms、centre 15 ms：\n");
    std::printf("   phase offset   mix   L/R 相關係數\n");
    const double rho0 = runAndCorrelate(0.0f, 1.0f);
    const double rho90wet = runAndCorrelate(0.25f, 1.0f);
    const double rho90half = runAndCorrelate(0.25f, 0.5f);
    const double rho180wet = runAndCorrelate(0.5f, 1.0f);
    std::printf("   0°             1.0   %.4f\n", rho0);
    std::printf("   90°            1.0   %.4f\n", rho90wet);
    std::printf("   90°            0.5   %.4f\n", rho90half);
    std::printf("   180°           1.0   %.4f\n", rho180wet);
    DAWLAB_CHECK(rho0 > 0.999);
    DAWLAB_CHECK(rho90wet < 0.5);
    DAWLAB_CHECK(rho90half > rho90wet && rho90half < rho0);
    std::printf("  -> 白雜訊在不同延遲下幾乎不相關，所以純濕訊號的 ρ 很低；加回乾訊號後 ρ 回升。\n\n");

    // ---- 3b. 相位差真的是 90°？（impulse 量延遲） -----------------------------------
    {
        double lMs = 0.0, rMs = 0.0;
        impulsePeakDelaysMs(0.25f, lMs, rMs);
        std::printf("rate 0、phase offset 0.25 的 impulse response：左 %.3f ms、右 %.3f ms\n", lMs, rMs);
        std::printf("  理論：左 centre + depth·sin(0) = 15.000 ms、右 centre + depth·sin(π/2) = 18.000 ms\n\n");
        DAWLAB_CHECK_NEAR(lMs, 15.0, 0.05);
        DAWLAB_CHECK_NEAR(rMs, 18.0, 0.05); // offset 0.5 → 15 ms、0.05 → 15.93 ms，都會 FAIL
    }

    // ---- 4. mono 相容性 -----------------------------------------------------------
    {
        dawlab::AudioBuffer out;
        runAndCorrelate(0.25f, 0.5f, &out);
        dawlab::AudioBuffer monoSum(1, out.getNumSamples());
        for (int i = 0; i < out.getNumSamples(); ++i)
            monoSum.setSample(0, i, 0.5f * (out.getSample(0, i) + out.getSample(1, i)));
        const float sumRms = dawlab::rms(monoSum.getReadPointer(0) + skip, monoSum.getNumSamples() - skip);
        const float lrRms = 0.5f * (dawlab::rms(out.getReadPointer(0) + skip, out.getNumSamples() - skip) +
                                    dawlab::rms(out.getReadPointer(1) + skip, out.getNumSamples() - skip));
        // 理論：L = 0.5x + 0.5w_L，x、w_L、w_R 互不相關且功率相同（σ²）
        //   P(L) = 0.25σ² + 0.25σ² = 0.5σ²
        //   P((L+R)/2) = P(0.5x + 0.25w_L + 0.25w_R) = 0.25σ² + 0.0625σ² · 2 = 0.375σ²
        //   比值 = 0.375 / 0.5 = 0.75 → 10·log10(0.75) = −1.25 dB
        const float lossDb = dawlab::gainToDb(sumRms / lrRms);
        std::printf("mono 相容性（90°、mix 0.5）：(L+R)/2 的 RMS 相對單一聲道 %+.2f dB（理論 −1.25 dB）\n\n", lossDb);
        DAWLAB_CHECK_NEAR(lossDb, -1.25, 0.5);
        DAWLAB_CHECK(lossDb > -3.0f);
    }

    // ---- 5. 和 dsp::Chorus 的 stereo spread 對照 -----------------------------------
    {
        dawlab::AudioBuffer in = dualMono(3.0, 21u);
        Chorus c;
        c.prepare(kSampleRate, kBlockSize, 2);
        c.setRateHz(0.8f);
        c.setDepthMs(3.0f);
        c.setCentreDelayMs(15.0f);
        c.setVoices(1);
        c.setMix(1.0f);
        c.setStereoSpread(1.0f); // 右聲道 LFO 和左聲道相差 90°
        c.reset();
        processInBlocks(c, in);
        const double rhoLib = correlation(in, skip);
        std::printf("dsp::Chorus（1 voice、mix 1、spread 1）：ρ = %.4f（自己寫的 90° 版 %.4f）\n\n", rhoLib, rho90wet);
        DAWLAB_CHECK_NEAR(rhoLib, rho90wet, 0.05);
    }

    // ---- 聽 ----------------------------------------------------------------------
    {
        dawlab::AudioBuffer pluck = dawlab::makePluckSequence(kSampleRate, 4.0, 0.4f, 2);
        StereoChorus c;
        c.prepare(kSampleRate, kBlockSize, 2);
        c.setRateHz(0.8f);
        c.setDepthMs(3.0f);
        c.setCentreDelayMs(15.0f);
        c.setMix(0.5f);
        c.setPhaseOffset(0.25f);
        c.reset();
        processInBlocks(c, pluck);
        dawlab::writeWav(dawlab::outputPath("l10_ex5_stereo_chorus.wav"), pluck, kSampleRate);
        std::printf("輸出 l10_ex5_stereo_chorus.wav：peak %.3f，L/R 相關係數 %.3f\n", dawlab::peakAbs(pluck),
                    correlation(pluck, 0));
        DAWLAB_CHECK(!dawlab::hasNanOrInf(pluck));
    }

    return dawlab::test::summary("l10_ex5");
}
