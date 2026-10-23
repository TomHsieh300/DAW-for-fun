// 練習 6 解答：用 crossfade 切換 delay time（沒有 pitch shift、沒有 click）
//
// 第 09 章的 Delay 用「平滑過渡」策略：delay time 線性 ramp 到新值，過程中讀取指標的速度
// 不等於寫入指標 → 有 Doppler pitch shift（範例 3）。這在 modulation 效果裡是特色，
// 但 DAW 使用者按下「1/8 → 1/4」時通常不想聽到磁帶變速聲。
//
// 另一種策略：兩個讀取頭（A、B）。
//   - 平常只用 A。
//   - 收到新的 delay time 時，把 B 設到新位置（它的 delay 固定，所以沒有 Doppler），
//     然後在 crossfadeMs 內把輸出從 A 淡出、B 淡入（raised-cosine：gB = ½(1 − cos πt)，gA = 1 − gB）。
//   - 淡入淡出結束後交換角色：B 變成 A。
//   - crossfade 期間又收到新值 → 先記起來，等這次 fade 完再做下一次。
//
//   x ──►(+)──► delay line ──┬── head A (D_old) ──×gA──┐
//         ▲                  │                          (+)──► wet
//         │                  └── head B (D_new) ──×gB──┘
//         └──────────── ×fb ◄───────────────────────────┘
//
// 自我檢查（440 Hz 正弦，10 ms → 250 ms）：
//   1. crossfade 期間相鄰 sample 的最大差值不超過正弦本身的斜率太多（沒有 click）；
//   2. crossfade 前後 ±50 ms 用 zero-crossing 量到的頻率在 440 ± 6 Hz 內（沒有 Doppler；
//      剩下的幾 Hz 是兩個讀取頭相位不同造成的相位滑動，見 solutions/README.md）；
//   3. fade 結束後輸出與 x[n − 12000] 完全相同；
//   4. 對照組：crossfade = 0（直接跳）會出現遠大於正弦斜率的斷層；
//      Delay（平滑過渡、ramp 20 ms）量到的頻率會嚴重偏離 440 Hz；
//   5. 問題的答案：fade 中點的振幅 = |cos(φ/2)| = 0.309；白雜訊（兩頭內容不相關）經過 CrossfadeDelay，
//      fade 中點的音量 −3 dB。equal-power 中點 0 dB（相關素材 +3 dB）只印出算式對照，不算 check。
//
// 執行：l09_ex6_solution。輸出 dawlab_output/l09_ex6_crossfade.wav、l09_ex6_abrupt.wav、l09_ex6_ramp.wav。

#include <dawlab/Common.h>
#include <dawlab/dsp/Delay.h>
#include <dawlab/dsp/FractionalDelayLine.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using dawlab::dsp::Delay;
using dawlab::dsp::FractionalDelayLine;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr double kToneHz = 440.0;
constexpr float kDelayA = 10.0f;
constexpr float kDelayB = 250.0f;
constexpr double kSwitchAt = 1.0; // 秒

class CrossfadeDelay {
public:
    static constexpr int kMaxChannels = 2;

    void setMaxDelayMs(float ms) noexcept { maxDelayMs_ = std::max(1.0f, ms); }

    void prepare(double sampleRate, int /*maxBlockSize*/, int numChannels) {
        sampleRate_ = sampleRate;
        numChannels_ = std::clamp(numChannels, 1, kMaxChannels);
        maxDelaySamples_ = dawlab::msToSamples(maxDelayMs_, sampleRate) + 2;
        line_.prepare(maxDelaySamples_, numChannels_);
        prepared_ = true;
        setCrossfadeMs(crossfadeMs_);
        reset();
    }

    void reset() noexcept {
        line_.reset();
        // reset 不需要 crossfade（buffer 已清空）：兩個讀取頭直接對齊目前的目標值。
        // 否則 prepare → setDelayMs → reset 的順序會讓還沒做完的 fade 被丟掉、停在舊的 delay。
        if (prepared_)
            headDelay_[0] = headDelay_[1] = toSamples(delayMs_);
        active_ = 0;
        fadePos_ = -1;
        pending_ = false;
    }

    // 延遲時間改變：不是 ramp，而是把另一個讀取頭放到新位置、開始 crossfade
    void setDelayMs(float ms) noexcept {
        delayMs_ = std::clamp(ms, 0.0f, maxDelayMs_);
        if (!prepared_)
            return;
        if (fadePos_ >= 0) { // 正在 fade：先記下來，fade 完再處理
            pending_ = true;
            return;
        }
        startFade();
    }

    void setCrossfadeMs(float ms) noexcept {
        crossfadeMs_ = std::max(0.0f, ms);
        if (prepared_)
            fadeLength_ = std::max(1, dawlab::msToSamples(crossfadeMs_, sampleRate_));
    }
    void setFeedback(float fb) noexcept { feedback_ = std::clamp(fb, 0.0f, 0.99f); }
    void setMix(float mix) noexcept { mix_ = std::clamp(mix, 0.0f, 1.0f); }
    bool isFading() const noexcept { return fadePos_ >= 0; }

    void process(dawlab::AudioBlock block) noexcept {
        const int n = block.getNumSamples();
        const int nch = std::min(block.getNumChannels(), numChannels_);
        for (int i = 0; i < n; ++i) {
            // 這個 sample 的 crossfade 權重（所有 channel 共用）
            float gA = 1.0f, gB = 0.0f;
            if (fadePos_ >= 0) {
                const float t = static_cast<float>(fadePos_) / static_cast<float>(fadeLength_);
                gB = 0.5f * (1.0f - std::cos(static_cast<float>(dawlab::kPi) * t)); // raised cosine
                gA = 1.0f - gB;
            }
            const int other = 1 - active_;
            for (int ch = 0; ch < nch; ++ch) {
                float* data = block.getChannelPointer(ch);
                const float x = data[i];
                const float a = line_.readCubic(ch, headDelay_[active_] - 1.0f);
                const float wet = fadePos_ >= 0 ? gA * a + gB * line_.readCubic(ch, headDelay_[other] - 1.0f) : a;
                line_.push(ch, x + feedback_ * wet);
                data[i] = (1.0f - mix_) * x + mix_ * wet;
            }
            if (fadePos_ >= 0 && ++fadePos_ >= fadeLength_) { // fade 結束：交換讀取頭
                active_ = other;
                fadePos_ = -1;
                if (pending_) {
                    pending_ = false;
                    if (toSamples(delayMs_) != headDelay_[active_])
                        startFade();
                }
            }
        }
    }

private:
    float toSamples(float ms) const noexcept {
        return std::clamp(static_cast<float>(ms * 0.001 * sampleRate_), 2.0f, static_cast<float>(maxDelaySamples_));
    }

    void startFade() noexcept {
        const float target = toSamples(delayMs_);
        if (target == headDelay_[active_])
            return;
        if (fadeLength_ <= 1 || crossfadeMs_ <= 0.0f) { // crossfade = 0：直接跳（會 click，對照用）
            headDelay_[active_] = target;
            return;
        }
        headDelay_[1 - active_] = target;
        fadePos_ = 0;
    }

    FractionalDelayLine line_;
    double sampleRate_ = 48000.0;
    int numChannels_ = 2;
    int maxDelaySamples_ = 0;
    float maxDelayMs_ = 2000.0f;
    float delayMs_ = 250.0f;
    float crossfadeMs_ = 20.0f;
    float feedback_ = 0.0f;
    float mix_ = 1.0f;
    float headDelay_[2] = {2.0f, 2.0f};
    int active_ = 0;
    int fadePos_ = -1; // -1 = 沒有在 fade
    int fadeLength_ = 1;
    bool pending_ = false;
    bool prepared_ = false;
};

// 用正向 zero-crossing 估計頻率
double estimateFrequency(const float* x, int n) {
    int first = -1, last = -1, count = 0;
    for (int i = 1; i < n; ++i) {
        if (x[i - 1] < 0.0f && x[i] >= 0.0f) {
            if (first < 0)
                first = i;
            last = i;
            ++count;
        }
    }
    return count < 2 ? 0.0 : (count - 1) * kSampleRate / static_cast<double>(last - first);
}

float maxSampleStep(const float* x, int n) {
    float m = 0.0f;
    for (int i = 1; i < n; ++i)
        m = std::max(m, std::abs(x[i] - x[i - 1]));
    return m;
}

dawlab::AudioBuffer makeTone() { return dawlab::makeSine(kToneHz, kSampleRate, 3.0, 0.5f, 1); }

// 兩種 delay 都在 t = kSwitchAt 把 delay 從 kDelayA 切到 kDelayB。
// 48000 不是 512 的倍數，所以把那個 block 在切換點切開（和 host 處理 automation 的做法一樣）。
template <typename Proc, typename Setter>
void renderInto(Proc& proc, Setter&& setDelay, dawlab::AudioBuffer& b) {
    const int switchAt = dawlab::secondsToSamples(kSwitchAt, kSampleRate);
    const int total = b.getNumSamples();
    dawlab::AudioBlock whole(b);
    int start = 0;
    while (start < total) {
        if (start == switchAt)
            setDelay(kDelayB);
        int n = std::min(kBlockSize, total - start);
        if (start < switchAt && switchAt < start + n)
            n = switchAt - start;
        proc.process(whole.getSubBlock(start, n));
        start += n;
    }
}

template <typename Proc, typename Setter>
dawlab::AudioBuffer render(Proc& proc, Setter&& setDelay) {
    dawlab::AudioBuffer b = makeTone();
    renderInto(proc, setDelay, b);
    return b;
}

dawlab::AudioBuffer renderCrossfade(float crossfadeMs) {
    CrossfadeDelay d;
    d.setMaxDelayMs(1000.0f);
    d.prepare(kSampleRate, kBlockSize, 1);
    d.setCrossfadeMs(crossfadeMs);
    d.setDelayMs(kDelayA);
    d.setFeedback(0.0f);
    d.setMix(1.0f);
    d.reset();
    return render(d, [&](float ms) { d.setDelayMs(ms); });
}

// 白雜訊經過 CrossfadeDelay（10 → 250 ms、crossfade 1 秒），fade 中點 ±20 ms 的輸出能量相對輸入的 dB。
// 兩個讀取頭讀到的是相差 240 ms 的雜訊（不相關），中點 gA = gB = 0.5 → 能量 0.5² + 0.5² = 0.5 → −3.01 dB。
double measureMidFadeNoiseDb() {
    const float fadeMs = 1000.0f;
    const int switchAt = dawlab::secondsToSamples(kSwitchAt, kSampleRate);
    const int mid = switchAt + dawlab::msToSamples(fadeMs, kSampleRate) / 2;
    const int half = dawlab::msToSamples(20.0, kSampleRate);
    double outEnergy = 0.0, inEnergy = 0.0;
    long long outCount = 0, inCount = 0;
    for (uint32_t seed = 1; seed <= 8; ++seed) {
        CrossfadeDelay d;
        d.setMaxDelayMs(1000.0f);
        d.prepare(kSampleRate, kBlockSize, 1);
        d.setCrossfadeMs(fadeMs);
        d.setDelayMs(kDelayA);
        d.setMix(1.0f);
        d.reset();
        dawlab::AudioBuffer b = dawlab::makeWhiteNoise(kSampleRate, 2.5, 0.25f, 1, seed);
        const dawlab::AudioBuffer input = b;
        renderInto(d, [&](float ms) { d.setDelayMs(ms); }, b);
        for (int n = mid - half; n < mid + half; ++n, ++outCount)
            outEnergy += static_cast<double>(b.getSample(0, n)) * b.getSample(0, n);
        for (int n = 0; n < input.getNumSamples(); ++n, ++inCount)
            inEnergy += static_cast<double>(input.getSample(0, n)) * input.getSample(0, n);
    }
    return 10.0 * std::log10((outEnergy / static_cast<double>(outCount)) / (inEnergy / static_cast<double>(inCount)));
}

dawlab::AudioBuffer renderRamp(double rampSeconds) {
    Delay d;
    d.setMaxDelayMs(1000.0f);
    d.prepare(kSampleRate, kBlockSize, 1);
    d.setDelayRampSeconds(rampSeconds);
    d.setDelayMs(kDelayA);
    d.setFeedback(0.0f);
    d.setMix(1.0f);
    d.reset();
    return render(d, [&](float ms) { d.setDelayMs(ms); });
}

} // namespace

int main() {
    std::printf("=== 第 09 章 練習 6：crossfade 切換 delay time ===\n\n");
    const float crossfadeMs = 20.0f;
    const int switchAt = dawlab::secondsToSamples(kSwitchAt, kSampleRate);
    const int winLen = dawlab::secondsToSamples(0.1, kSampleRate);
    const double sineMaxStep = 0.5 * dawlab::kTwoPi * kToneHz / kSampleRate; // 正弦每個 sample 最大的變化量

    dawlab::AudioBuffer crossfade = renderCrossfade(crossfadeMs);
    dawlab::AudioBuffer abrupt = renderCrossfade(0.0f);
    dawlab::AudioBuffer ramp = renderRamp(0.02);
    dawlab::AudioBuffer tone = makeTone();

    std::printf("440 Hz 正弦，t = %.1f s 時 delay 從 %g ms 切到 %g ms\n", kSwitchAt, kDelayA, kDelayB);
    std::printf("正弦本身相鄰 sample 的最大差值：%.4f\n\n", sineMaxStep);

    // ---- 1. 沒有 click ---------------------------------------------------------
    const float stepXf = maxSampleStep(crossfade.getReadPointer(0) + switchAt - 2, winLen);
    const float stepAb = maxSampleStep(abrupt.getReadPointer(0) + switchAt - 2, winLen);
    std::printf("切換後 100 ms 內相鄰 sample 最大差值：crossfade %.4f、直接跳 %.4f\n", stepXf, stepAb);
    DAWLAB_CHECK(stepXf < 1.5 * sineMaxStep); // crossfade：和正弦本身差不多
    DAWLAB_CHECK(stepAb > 3.0 * sineMaxStep); // 直接跳：斷層 = click

    // ---- 2. 沒有 Doppler -------------------------------------------------------
    auto freqIn = [&](const dawlab::AudioBuffer& b, double t0, double t1) {
        const int s = dawlab::secondsToSamples(t0, kSampleRate);
        return estimateFrequency(b.getReadPointer(0) + s, dawlab::secondsToSamples(t1 - t0, kSampleRate));
    };
    const double fXf = freqIn(crossfade, kSwitchAt - 0.05, kSwitchAt + 0.05);
    const double fRamp = freqIn(ramp, kSwitchAt, kSwitchAt + 0.02);
    const double rampSlope = (kDelayB - kDelayA) * 0.001 / 0.02; // dτ/dt
    std::printf("切換前後 ±50 ms 量到的頻率：crossfade %.1f Hz（預期 %.0f）\n", fXf, kToneHz);
    std::printf("Delay 平滑過渡（ramp 20 ms）在 ramp 期間量到：%.1f Hz（dτ/dt = %.0f → 理論 |f_out| = %.0f Hz，"
                "負號代表倒放）\n",
                fRamp, rampSlope, std::abs(kToneHz * (1.0 - rampSlope)));
    // 容許幾 Hz 的誤差：兩個讀取頭的相位不同（240 ms × 440 Hz = 105.6 週期 → 相差 0.6 週期），
    // crossfade 時合成訊號的相位會在 20 ms 內從 A「滑」到 B（最多半個週期），
    // 攤在 100 ms 的量測窗裡約是 ±5 Hz。對照 ramp 版本的 4840 Hz，這可以說是「沒有 Doppler」。
    DAWLAB_CHECK_NEAR(fXf, kToneHz, 6.0);
    DAWLAB_CHECK(std::abs(fRamp - kToneHz) > 100.0); // 平滑過渡在這麼快的變化下 pitch 完全走樣

    // ---- 3. fade 結束後 = 純粹的 x[n − D_new] -------------------------------------
    {
        const int dNew = dawlab::msToSamples(kDelayB, kSampleRate); // 12000
        const int from = switchAt + dawlab::msToSamples(crossfadeMs, kSampleRate) + 8;
        float maxErr = 0.0f;
        for (int n = from; n < crossfade.getNumSamples(); ++n)
            maxErr = std::max(maxErr, std::abs(crossfade.getSample(0, n) - tone.getSample(0, n - dNew)));
        std::printf("fade 結束後 y[n] 與 x[n − %d] 的最大誤差：%.2e\n", dNew, maxErr);
        DAWLAB_CHECK(maxErr < 1.0e-5f);
        // 切換前則是 x[n − 480]
        const int dOld = dawlab::msToSamples(kDelayA, kSampleRate);
        float maxErrBefore = 0.0f;
        for (int n = dOld + 8; n < switchAt; ++n)
            maxErrBefore = std::max(maxErrBefore, std::abs(crossfade.getSample(0, n) - tone.getSample(0, n - dOld)));
        DAWLAB_CHECK(maxErrBefore < 1.0e-5f);
    }

    // ---- 4. crossfade 期間收到第二次改變：要排隊，不能漏掉 -----------------------
    {
        CrossfadeDelay d;
        d.setMaxDelayMs(1000.0f);
        d.prepare(kSampleRate, kBlockSize, 1);
        d.setCrossfadeMs(20.0f);
        d.setDelayMs(10.0f);
        d.setMix(1.0f);
        d.reset();
        dawlab::AudioBuffer b = dawlab::makeSine(kToneHz, kSampleRate, 1.0, 0.5f, 1);
        dawlab::AudioBlock whole(b);
        d.process(whole.getSubBlock(0, 4800)); // 100 ms 暖身
        d.setDelayMs(100.0f);                  // 開始 fade
        d.process(whole.getSubBlock(4800, 256));
        DAWLAB_CHECK(d.isFading());
        d.setDelayMs(300.0f); // fade 中途再改：排隊
        d.process(whole.getSubBlock(5056, 4800 * 3));
        DAWLAB_CHECK(!d.isFading());
        // 最後應該是 300 ms
        const int d300 = dawlab::msToSamples(300.0, kSampleRate);
        float err = 0.0f;
        for (int n = 4800 * 4; n < b.getNumSamples(); ++n) {
            const float expect = static_cast<float>(0.5 * std::sin(dawlab::kTwoPi * kToneHz * (n - d300) / kSampleRate));
            err = std::max(err, std::abs(b.getSample(0, n) - expect));
        }
        std::printf("fade 中途再次改變 delay（100 → 300 ms）：最後穩定在 300 ms，誤差 %.2e\n", err);
        DAWLAB_CHECK(err < 1.0e-4f);
        DAWLAB_CHECK(maxSampleStep(b.getReadPointer(0) + 4800, 4800 * 3) < 1.5 * sineMaxStep);
    }

    // ---- 5. 問題的答案：fade 中點的振幅、不相關素材與 equal-power 的比較 ------------
    {
        // 兩個頭相差 240 ms = 105.6 週期 → 相位差 φ = 0.6 週期；中點 gA = gB = 0.5，
        // 合成振幅 = 0.5·|1 + e^{jφ}| = |cos(φ/2)|（相對於原本的振幅）
        const double phi = dawlab::kTwoPi * kToneHz * (kDelayB - kDelayA) * 0.001;
        const double expectRatio = std::abs(std::cos(phi / 2.0));
        const int mid = switchAt + dawlab::msToSamples(crossfadeMs, kSampleRate) / 2;
        // 半個週期的視窗裡一定有一個 |sin| 的極值；視窗越短，gA、gB 在視窗內的變化越小（約 ±0.02）
        const int halfPeriod = static_cast<int>(kSampleRate / kToneHz / 2.0) + 1;
        const float midPeak = dawlab::peakAbs(crossfade.getReadPointer(0) + mid - halfPeriod / 2, halfPeriod);
        std::printf("\nfade 中點（gA = gB = 0.5）半個週期內的 peak：%.4f，相對原振幅 %.3f（理論 |cos(φ/2)| = %.3f，%.1f dB）\n",
                    midPeak, midPeak / 0.5, expectRatio, dawlab::gainToDb(expectRatio));
        DAWLAB_CHECK_NEAR(midPeak / 0.5, expectRatio, 0.01);

        // 真實素材：兩個頭讀到的內容不相關。白雜訊在不同的延遲下彼此不相關，
        // 所以把白雜訊送進「真正的」CrossfadeDelay（10 → 250 ms），在 fade 中點量輸出的 RMS。
        // 為了讓量測窗內 gA、gB 幾乎固定在 0.5，這裡把 crossfade 拉長到 1 秒、只看中點 ±20 ms；
        // 再用 8 個不同 seed 平均，壓低白雜訊 RMS 本身的統計誤差。
        const double midFadeDb = measureMidFadeNoiseDb();
        std::printf("crossfade 中點的音量（相對輸入，CrossfadeDelay 實測）：\n");
        std::printf("  raised-cosine（0.5 + 0.5），不相關素材：%+.2f dB（理論 10·log10(0.5² + 0.5²) = −3.01）\n",
                    midFadeDb);
        DAWLAB_CHECK_NEAR(midFadeDb, -3.01, 0.15);
        // equal-power crossfade 只是對照的算式（本練習的 CrossfadeDelay 沒有實作它，所以不算 check）：
        // 中點 gA = gB = cos45°，不相關素材 10·log10(2·0.5) = 0 dB；相關素材 20·log10(2·cos45°) = +3.01 dB。
        const double c45 = std::cos(dawlab::kPi / 4.0);
        std::printf("  （算式對照）equal-power 中點 gA = gB = cos45°：不相關 %+.2f dB、相關 %+.2f dB\n",
                    10.0 * std::log10(2.0 * c45 * c45), dawlab::gainToDb(2.0 * c45));
    }

    DAWLAB_CHECK(!dawlab::hasNanOrInf(crossfade));
    dawlab::writeWav(dawlab::outputPath("l09_ex6_crossfade.wav"), crossfade, kSampleRate);
    dawlab::writeWav(dawlab::outputPath("l09_ex6_abrupt.wav"), abrupt, kSampleRate);
    dawlab::writeWav(dawlab::outputPath("l09_ex6_ramp.wav"), ramp, kSampleRate);
    std::printf("\n已輸出 dawlab_output/l09_ex6_crossfade.wav（乾淨切換）、l09_ex6_abrupt.wav（click）、"
                "l09_ex6_ramp.wav（短促的倒放 / 變速聲）\n\n");

    return dawlab::test::summary("l09_ex6");
}
