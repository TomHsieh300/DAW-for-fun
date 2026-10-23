// 練習 6 起始碼：用 crossfade 切換 delay time
//
// 目標：兩個讀取頭 A / B。收到新的 delay time 時，把「沒在用的那個頭」放到新位置，
// 在 crossfadeMs 內用 raised-cosine 把輸出從舊頭淡出、新頭淡入，結束後交換角色。
// 這樣切換時既沒有 click（直接跳的問題）也沒有 Doppler pitch shift（平滑 ramp 的問題）。
//
//   x ──►(+)──► delay line ──┬── head A (D_old) ──×gA──┐
//         ▲                  │                          (+)──► wet
//         │                  └── head B (D_new) ──×gB──┘
//         └──────────── ×fb ◄───────────────────────────┘
//
// 完成 setDelayMs() / process() 的 TODO 之後執行 l09_ex6_starter，所有 check 應該通過。
// 目前的起始碼是「直接跳到新值」，所以會量到 click。

#include <dawlab/Common.h>
#include <dawlab/dsp/FractionalDelayLine.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

using dawlab::dsp::FractionalDelayLine;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr double kToneHz = 440.0;

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

    void setDelayMs(float ms) noexcept {
        delayMs_ = std::clamp(ms, 0.0f, maxDelayMs_);
        if (!prepared_)
            return;
        // TODO: 若正在 fade（fadePos_ >= 0）就設 pending_ = true 等它結束；
        //       否則把 headDelay_[1 - active_] 設成 toSamples(delayMs_)、fadePos_ = 0 開始 crossfade。
        headDelay_[active_] = toSamples(delayMs_); // 目前：直接跳（會 click）
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
            // TODO: 若 fadePos_ >= 0：t = fadePos_ / fadeLength_，gB = ½(1 − cos πt)，gA = 1 − gB，
            //       wet = gA · head[active_] + gB · head[1 − active_]；
            //       fadePos_ 走到 fadeLength_ 時交換 active_、fadePos_ = −1，並處理 pending_。
            for (int ch = 0; ch < nch; ++ch) {
                float* data = block.getChannelPointer(ch);
                const float x = data[i];
                const float wet = line_.readCubic(ch, headDelay_[active_] - 1.0f);
                line_.push(ch, x + feedback_ * wet);
                data[i] = (1.0f - mix_) * x + mix_ * wet;
            }
        }
    }

private:
    float toSamples(float ms) const noexcept {
        return std::clamp(static_cast<float>(ms * 0.001 * sampleRate_), 2.0f, static_cast<float>(maxDelaySamples_));
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
    int fadePos_ = -1;
    int fadeLength_ = 1;
    bool pending_ = false;
    bool prepared_ = false;
};

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

} // namespace

int main() {
    std::printf("=== 第 09 章 練習 6（起始碼）：crossfade 切換 delay time ===\n\n");
    const int switchAt = dawlab::secondsToSamples(1.0, kSampleRate);
    const double sineMaxStep = 0.5 * dawlab::kTwoPi * kToneHz / kSampleRate;

    dawlab::AudioBuffer tone = dawlab::makeSine(kToneHz, kSampleRate, 3.0, 0.5f, 1);
    dawlab::AudioBuffer out = tone;
    CrossfadeDelay d;
    d.setMaxDelayMs(1000.0f);
    d.prepare(kSampleRate, kBlockSize, 1);
    d.setCrossfadeMs(20.0f);
    d.setDelayMs(10.0f);
    d.setMix(1.0f);
    d.reset();
    dawlab::AudioBlock whole(out);
    // 48000 不是 512 的倍數：把 block 在切換點切開，前半段用舊值、後半段用新值
    int start = 0;
    while (start < out.getNumSamples()) {
        if (start == switchAt)
            d.setDelayMs(250.0f);
        int n = std::min(kBlockSize, out.getNumSamples() - start);
        if (start < switchAt && switchAt < start + n)
            n = switchAt - start;
        d.process(whole.getSubBlock(start, n));
        start += n;
    }

    const float step = maxSampleStep(out.getReadPointer(0) + switchAt - 2, dawlab::secondsToSamples(0.1, kSampleRate));
    const double f = estimateFrequency(out.getReadPointer(0) + switchAt - 2400, 4800);
    std::printf("切換後 100 ms 內相鄰 sample 最大差值 %.4f（正弦本身 %.4f）；切換前後量到 %.1f Hz\n", step,
                sineMaxStep, f);
    DAWLAB_CHECK(step < 1.5 * sineMaxStep);
    DAWLAB_CHECK_NEAR(f, kToneHz, 6.0); // 兩個頭相位不同，crossfade 時相位會滑動一點點（見解答說明）

    float maxErr = 0.0f;
    for (int n = switchAt + 960 + 8; n < out.getNumSamples(); ++n)
        maxErr = std::max(maxErr, std::abs(out.getSample(0, n) - tone.getSample(0, n - 12000)));
    std::printf("fade 結束後 y[n] 與 x[n − 12000] 的最大誤差：%.2e\n", maxErr);
    DAWLAB_CHECK(maxErr < 1.0e-5f);
    DAWLAB_CHECK(!dawlab::hasNanOrInf(out));

    // ---- 4. crossfade 期間收到第二次改變：要排隊，不能漏掉 -----------------------
    {
        CrossfadeDelay q;
        q.setMaxDelayMs(1000.0f);
        q.prepare(kSampleRate, kBlockSize, 1);
        q.setCrossfadeMs(20.0f);
        q.setDelayMs(10.0f);
        q.setMix(1.0f);
        q.reset();
        dawlab::AudioBuffer b = dawlab::makeSine(kToneHz, kSampleRate, 1.0, 0.5f, 1);
        dawlab::AudioBlock bw(b);
        q.process(bw.getSubBlock(0, 4800)); // 100 ms 暖身
        q.setDelayMs(100.0f);               // 開始 fade
        q.process(bw.getSubBlock(4800, 256));
        DAWLAB_CHECK(q.isFading());
        q.setDelayMs(300.0f); // fade 中途再改：排隊
        q.process(bw.getSubBlock(5056, 4800 * 3));
        DAWLAB_CHECK(!q.isFading());
        // 最後應該是 300 ms
        const int d300 = dawlab::msToSamples(300.0, kSampleRate);
        float err = 0.0f;
        for (int n = 4800 * 4; n < b.getNumSamples(); ++n) {
            const float expect = static_cast<float>(0.5 * std::sin(dawlab::kTwoPi * kToneHz * (n - d300) / kSampleRate));
            err = std::max(err, std::abs(b.getSample(0, n) - expect));
        }
        std::printf("fade 中途再次改變 delay（100 → 300 ms）：最後應該穩定在 300 ms，誤差 %.2e\n", err);
        DAWLAB_CHECK(err < 1.0e-4f);
        DAWLAB_CHECK(maxSampleStep(b.getReadPointer(0) + 4800, 4800 * 3) < 1.5 * sineMaxStep);
    }
    return dawlab::test::summary("l09_ex6_starter");
}
