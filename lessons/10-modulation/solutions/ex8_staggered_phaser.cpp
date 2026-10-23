// 練習 8 解答：Staggered phaser —— 每一級 allpass 有自己的 fc，驗證練習 7 第 4 小題的預測
//
// 本章的 dawlab::dsp::Phaser 讓 N 級 allpass 共用同一個 fc，notch 有封閉解：
//   tan(π f_k / fs) = tan(π fc / fs) · tan((2k+1)π / (2N))
// 每一級 fc 不同時就沒有封閉解了，但「總相位 = 各級相位相加」仍然成立：
//   Φ(f) = Σ φ_i(f)，φ_i(f) = −2·arctan( tan(πf/fs) / tan(π fc_i/fs) )
// 每一項都從 0（DC）單調降到 −π（Nyquist），所以 Φ 從 0 單調降到 −Nπ；
// notch 在 Φ(f) = −(2k+1)π，用二分法（bisection）解就好，notch 數仍是 N/2。
//
// 掃動：所有 fc_i 乘上同一個比例 2^(depth · octaves · lfo)，各級 fc 的「比例關係」不變，
// 整組 notch 在對數頻率軸上一起平移。
//
// self-check：
//   1. 4 級都設 1000 Hz：predictNotchHz() 和 Phaser::notchFrequencyHz() 的封閉解一致（414.7 / 2397.8 Hz）；
//      處理白雜訊的輸出和 dawlab::dsp::Phaser（LFO 停住、mix 0.5、feedback 0）逐 sample 相同
//   2. 300 / 600 / 1200 / 2400 Hz：掃頻量到的 notch 仍是 2 個，位置與 predictNotchHz() 相差 < 0.5%，深度 < −30 dB
//   3. 練習 7 第 4 小題的預測：第一個 notch 比 4 × 1000 Hz 的低、第二個比它高（兩個 notch 分得更開）
//   4. depth 1、octaves 1、LFO 停在 +1（rate 0、相位 0.25）→ 所有 fc 加倍，量到的 notch = predictNotchHz(k, 2)
// 輸出：dawlab_output/l10_ex8_uniform.wav（4 × 1000 Hz 掃動）、l10_ex8_staggered.wav（300–2400 Hz 掃動），白雜訊

#include <dawlab/Common.h>
#include <dawlab/dsp/Biquad.h>
#include <dawlab/dsp/Lfo.h>
#include <dawlab/dsp/Phaser.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

using dawlab::dsp::Biquad;
using dawlab::dsp::Lfo;
using dawlab::dsp::LfoShape;
using dawlab::dsp::Phaser;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

class StaggeredPhaser {
public:
    static constexpr int kMaxStages = 12;

    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        sampleRate_ = sampleRate;
        numChannels_ = std::max(1, numChannels);
        for (auto& s : stages_)
            s.prepare(sampleRate, maxBlockSize, numChannels_);
        lfo_.prepare(sampleRate);
        mixSm_.prepare(sampleRate, 20.0);
        reset();
    }

    void reset() noexcept {
        for (auto& s : stages_)
            s.reset();
        lfo_.reset();
        mixSm_.setCurrentAndTarget(mixSm_.getTarget());
    }

    // ---- 參數 ----------------------------------------------------------------
    // 設定級數（2..12 的偶數）與每一級的基準 fc；fcs 至少要有 stages 個元素
    void setStageFrequencies(const std::vector<float>& fcs) noexcept {
        int n = std::clamp(static_cast<int>(fcs.size()), 2, kMaxStages);
        n -= n % 2;
        for (int s = 0; s < n; ++s)
            baseHz_[static_cast<size_t>(s)] = std::clamp(fcs[static_cast<size_t>(s)], 20.0f, 20000.0f);
        for (int s = numStages_; s < n; ++s)
            stages_[static_cast<size_t>(s)].reset();
        numStages_ = n;
    }
    void setRateHz(float hz) noexcept { lfo_.setRateHz(std::clamp(hz, 0.0f, 20.0f)); }
    void setDepth(float depth) noexcept { depth_ = std::clamp(depth, 0.0f, 1.0f); }
    void setSweepOctaves(float octaves) noexcept { sweepOctaves_ = std::clamp(octaves, 0.0f, 6.0f); }
    void setLfoPhase(float phase01) noexcept { lfo_.setPhase(phase01); }
    void setMix(float mix) noexcept { mixSm_.setTarget(std::clamp(mix, 0.0f, 1.0f)); }
    int getStages() const noexcept { return numStages_; }

    // ---- 理論 ----------------------------------------------------------------
    // 總相位（弧度）：所有 fc 乘上 scale 之後，Σ φ_i(f)
    double theoryPhase(double fHz, double scale = 1.0) const noexcept {
        double sum = 0.0;
        for (int s = 0; s < numStages_; ++s)
            sum += Phaser::allpassPhase(fHz, scale * baseHz_[static_cast<size_t>(s)], sampleRate_);
        return sum;
    }

    // 第 k 個 notch（k = 0 .. N/2−1）：解 theoryPhase(f) = −(2k+1)π。
    // theoryPhase 從 0 單調降到 −Nπ，所以用二分法：相位還「不夠負」就往高頻找。
    double predictNotchHz(int k, double scale = 1.0) const noexcept {
        const double target = -(2.0 * k + 1.0) * dawlab::kPi;
        double lo = 1.0, hi = 0.5 * sampleRate_ - 1.0;
        for (int it = 0; it < 60; ++it) {
            const double mid = 0.5 * (lo + hi);
            if (theoryPhase(mid, scale) > target)
                lo = mid;
            else
                hi = mid;
        }
        return 0.5 * (lo + hi);
    }

    // ---- 處理 ----------------------------------------------------------------
    float processSample(int channel, float x) noexcept {
        if (channel == 0)
            advanceModulation();
        return renderSample(channel, x);
    }

    void process(dawlab::AudioBlock block) noexcept {
        const int n = block.getNumSamples();
        const int nch = std::min(block.getNumChannels(), numChannels_);
        for (int i = 0; i < n; ++i) {
            advanceModulation();
            for (int ch = 0; ch < nch; ++ch) {
                float* d = block.getChannelPointer(ch);
                d[i] = renderSample(ch, d[i]);
            }
        }
    }

private:
    void advanceModulation() noexcept {
        mix_ = mixSm_.next();
        // 指數刻度的共同比例：每一級的 fc 都乘上同一個 2^(depth · octaves · lfo)，維持各級之間的比例
        const float m = lfo_.getNextValue();
        const float scale = std::exp2(depth_ * sweepOctaves_ * m);
        const float maxHz = static_cast<float>(0.45 * sampleRate_);
        for (int s = 0; s < numStages_; ++s) {
            const float fc = std::clamp(baseHz_[static_cast<size_t>(s)] * scale, 20.0f, maxHz);
            stages_[static_cast<size_t>(s)].setCoefficients(Phaser::makeFirstOrderAllpass(fc, sampleRate_));
        }
    }

    float renderSample(int ch, float x) noexcept {
        float w = x;
        for (int s = 0; s < numStages_; ++s)
            w = stages_[static_cast<size_t>(s)].processSample(ch, w);
        return (1.0f - mix_) * x + mix_ * w;
    }

    double sampleRate_ = 48000.0;
    int numChannels_ = 2;
    int numStages_ = 4;
    float depth_ = 0.0f;
    float sweepOctaves_ = 2.0f;
    std::array<float, kMaxStages> baseHz_{1000.0f, 1000.0f, 1000.0f, 1000.0f, 1000.0f, 1000.0f,
                                          1000.0f, 1000.0f, 1000.0f, 1000.0f, 1000.0f, 1000.0f};
    std::array<Biquad, kMaxStages> stages_;
    Lfo lfo_;
    dawlab::dsp::detail::ModParamSmoother mixSm_{0.5f};
    float mix_ = 0.5f;
};

template <typename Effect>
void processInBlocks(Effect& fx, dawlab::AudioBuffer& buffer) {
    dawlab::AudioBlock whole(buffer);
    for (int start = 0; start < buffer.getNumSamples(); start += kBlockSize) {
        const int n = std::min(kBlockSize, buffer.getNumSamples() - start);
        fx.process(whole.getSubBlock(start, n));
    }
}

// LFO 停住（rate 0）的 staggered phaser，量測用
StaggeredPhaser makeFrozen(const std::vector<float>& fcs, float depth = 0.0f, float lfoPhase = 0.0f) {
    StaggeredPhaser p;
    p.prepare(kSampleRate, kBlockSize, 1);
    p.setStageFrequencies(fcs);
    p.setRateHz(0.0f);
    p.setDepth(depth);
    p.setSweepOctaves(1.0f);
    p.setLfoPhase(lfoPhase);
    p.setMix(0.5f);
    p.reset();
    return p;
}

double measureDb(StaggeredPhaser& p, double freq) {
    p.reset();
    return dawlab::measureGainDb([&](float x) { return p.processSample(0, x); }, freq, kSampleRate, 0.3, 0.1);
}

struct Notch {
    double hz;
    double db;
};

// 50 Hz–16 kHz 對數掃頻找局部最小值，再用三分搜尋精修（notch 很尖，格點只能找到附近）
std::vector<Notch> findNotches(StaggeredPhaser& p) {
    constexpr int kPoints = 160;
    std::vector<double> freqs, gains;
    for (int i = 0; i <= kPoints; ++i) {
        freqs.push_back(50.0 * std::pow(16000.0 / 50.0, static_cast<double>(i) / kPoints));
        gains.push_back(measureDb(p, freqs.back()));
    }
    std::vector<Notch> notches;
    for (size_t i = 1; i + 1 < gains.size(); ++i) {
        if (gains[i] < gains[i - 1] && gains[i] <= gains[i + 1] && gains[i] < -6.0) {
            double lo = freqs[i - 1], hi = freqs[i + 1];
            for (int it = 0; it < 16; ++it) {
                const double m1 = lo + (hi - lo) / 3.0;
                const double m2 = hi - (hi - lo) / 3.0;
                if (measureDb(p, m1) < measureDb(p, m2))
                    hi = m2;
                else
                    lo = m1;
            }
            const double f = 0.5 * (lo + hi);
            notches.push_back({f, measureDb(p, f)});
        }
    }
    return notches;
}

// 印出量測 vs 預測，並檢查 notch 數、位置（相對誤差 < 0.5%）、深度（< −30 dB）
void checkNotches(const char* label, StaggeredPhaser& p, double scale) {
    const std::vector<Notch> notches = findNotches(p);
    const int expected = p.getStages() / 2;
    std::printf("%s：量到 %d 個 notch（預期 %d）\n", label, static_cast<int>(notches.size()), expected);
    std::printf("   k   量測(Hz)    預測(Hz)    誤差      深度\n");
    for (size_t k = 0; k < notches.size(); ++k) {
        const double pred = p.predictNotchHz(static_cast<int>(k), scale);
        const double err = (notches[k].hz - pred) / pred;
        std::printf("  %2d   %8.1f    %8.1f   %+6.3f%%   %+6.1f dB\n", static_cast<int>(k), notches[k].hz, pred,
                    100.0 * err, notches[k].db);
        DAWLAB_CHECK(std::abs(err) < 0.005);
        DAWLAB_CHECK(notches[k].db < -30.0);
    }
    std::printf("\n");
    DAWLAB_CHECK(static_cast<int>(notches.size()) == expected);
}

} // namespace

int main() {
    std::printf("=== 第 10 章 練習 8：Staggered phaser ===\n\n");

    const std::vector<float> uniform{1000.0f, 1000.0f, 1000.0f, 1000.0f};
    const std::vector<float> staggered{300.0f, 600.0f, 1200.0f, 2400.0f};

    // ---- 1. 全部 1000 Hz：和封閉解、和 dawlab::dsp::Phaser 一致 -----------------------
    {
        StaggeredPhaser sp = makeFrozen(uniform);
        std::printf("4 × 1000 Hz：predictNotchHz vs Phaser::notchFrequencyHz（封閉解）\n");
        for (int k = 0; k < 2; ++k) {
            const double pred = sp.predictNotchHz(k);
            const double closed = Phaser::notchFrequencyHz(k, 4, 1000.0, kSampleRate);
            std::printf("  k = %d：%.4f Hz vs %.4f Hz\n", k, pred, closed);
            DAWLAB_CHECK_NEAR(pred, closed, 0.01);
        }

        Phaser ref;
        ref.prepare(kSampleRate, kBlockSize, 1);
        ref.setRateHz(0.0f);
        ref.setDepth(0.0f);
        ref.setCentreHz(1000.0f);
        ref.setFeedback(0.0f);
        ref.setStages(4);
        ref.setMix(0.5f);
        ref.reset();
        dawlab::AudioBuffer a = dawlab::makeWhiteNoise(kSampleRate, 1.0, 0.25f, 1, 5u);
        dawlab::AudioBuffer b = a;
        processInBlocks(sp, a);
        processInBlocks(ref, b);
        float maxDiff = 0.0f;
        for (int i = 0; i < a.getNumSamples(); ++i)
            maxDiff = std::max(maxDiff, std::abs(a.getSample(0, i) - b.getSample(0, i)));
        std::printf("  白雜訊經過 StaggeredPhaser 與 dawlab::dsp::Phaser：最大差 %.2e（應為 0）\n\n", maxDiff);
        DAWLAB_CHECK(maxDiff < 1.0e-5f);
    }

    // ---- 2. 300 / 600 / 1200 / 2400 Hz：量測 notch vs 二分法預測 ----------------------
    StaggeredPhaser sp = makeFrozen(staggered);
    checkNotches("300 / 600 / 1200 / 2400 Hz", sp, 1.0);

    // ---- 3. 練習 7 第 4 小題的預測：notch 數不變、兩個 notch 分得更開 -----------------------
    {
        const double u0 = Phaser::notchFrequencyHz(0, 4, 1000.0, kSampleRate);
        const double u1 = Phaser::notchFrequencyHz(1, 4, 1000.0, kSampleRate);
        const double s0 = sp.predictNotchHz(0), s1 = sp.predictNotchHz(1);
        std::printf("和 4 × 1000 Hz 比較：\n");
        std::printf("  4 × 1000 Hz：       %7.1f / %7.1f Hz，比值 %.2f 倍（%.2f 個八度）\n", u0, u1, u1 / u0,
                    std::log2(u1 / u0));
        std::printf("  300–2400 Hz 散開：  %7.1f / %7.1f Hz，比值 %.2f 倍（%.2f 個八度）\n", s0, s1, s1 / s0,
                    std::log2(s1 / s0));
        std::printf("  -> 第一個 notch 往低頻移、第二個往高頻移，notch 數仍是 2。\n");
        const double toDeg = 180.0 / dawlab::kPi;
        const StaggeredPhaser up = makeFrozen(uniform);
        std::printf("     總相位        4 × 1000 Hz    300–2400 Hz\n");
        for (double f : {u0, 1000.0, u1})
            std::printf("     %7.1f Hz   %+8.1f°      %+8.1f°\n", f, up.theoryPhase(f) * toDeg, sp.theoryPhase(f) * toDeg);
        std::printf("  -> 散開之後總相位的曲線比較平緩（低頻那級較早開始轉、高頻那級較晚轉完）：\n"
                    "     在 %.1f Hz 已經超過 −180°、在 %.1f Hz 還沒轉到 −540°，\n"
                    "     所以第一個 notch 提早出現（往低頻）、第二個延後出現（往高頻）。\n\n",
                    u0, u1);
        DAWLAB_CHECK(s0 < u0);
        DAWLAB_CHECK(s1 > u1);
    }

    // ---- 4. 掃動：LFO 停在 +1、depth 1、octaves 1 → 所有 fc 加倍 ---------------------------
    {
        StaggeredPhaser swept = makeFrozen(staggered, 1.0f, 0.25f); // sine LFO 相位 0.25 → 值 = +1
        checkNotches("LFO = +1（fc × 2 = 600 / 1200 / 2400 / 4800 Hz）", swept, 2.0);
    }

    // ---- 聽 -----------------------------------------------------------------
    {
        std::printf("輸出 WAV（白雜訊、rate 0.2 Hz、depth 1、±1.5 個八度、mix 0.5）：\n");
        for (int which = 0; which < 2; ++which) {
            dawlab::AudioBuffer noise = dawlab::makeWhiteNoise(kSampleRate, 6.0, 0.2f, 2, 11u);
            StaggeredPhaser p;
            p.prepare(kSampleRate, kBlockSize, 2);
            p.setStageFrequencies(which == 0 ? uniform : staggered);
            p.setRateHz(0.2f);
            p.setDepth(1.0f);
            p.setSweepOctaves(1.5f);
            p.setMix(0.5f);
            p.reset();
            processInBlocks(p, noise);
            const char* name = which == 0 ? "l10_ex8_uniform.wav" : "l10_ex8_staggered.wav";
            dawlab::writeWav(dawlab::outputPath(name), noise, kSampleRate);
            std::printf("  %-24s peak %.3f\n", name, dawlab::peakAbs(noise));
            DAWLAB_CHECK(!dawlab::hasNanOrInf(noise));
        }
        std::printf("  -> staggered 的兩個 notch 距離較遠、各自比較寬，掃動聽起來比較「開」。\n");
    }

    return dawlab::test::summary("l10_ex8");
}
