// 練習 5 起始碼：multi-tap delay
//
// 目標：一條 mono delay line、最多 8 個 tap，每個 tap 有自己的 delay / gain / pan，
// feedback 從最後一個 tap 回到輸入：
//
//   (L+R)/2 ──►(+)──► delay line ─┬─ tap0 (D0, g0, pan0) ─┐
//               ▲                 ├─ tap1 (D1, g1, pan1) ─┼─(Σ)─► wet L / wet R
//               │                 └─ tap2 (D2, g2, pan2) ─┘
//               └─── ×fb ◄────────── 最後一個 tap
//
// 完成 process() 與 updateTap() 的 TODO 之後執行 l09_ex5_starter，所有 check 應該通過。
// 提示：先讀再 push，所以讀取位置是 delaySamples − 1；pan 用 constant-power（cos / sin）。

#include <dawlab/Common.h>
#include <dawlab/dsp/Delay.h>
#include <dawlab/dsp/FractionalDelayLine.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

using dawlab::dsp::Delay;
using dawlab::dsp::FractionalDelayLine;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

class MultiTapDelay {
public:
    static constexpr int kMaxTaps = 8;

    struct Tap {
        float delayMs = 0.0f;
        float gain = 0.0f;
        float pan = 0.0f;
    };

    void setMaxDelayMs(float ms) noexcept { maxDelayMs_ = std::max(1.0f, ms); }

    void prepare(double sampleRate, int /*maxBlockSize*/, int numChannels) {
        sampleRate_ = sampleRate;
        numChannels_ = std::clamp(numChannels, 1, 2);
        maxDelaySamples_ = dawlab::msToSamples(maxDelayMs_, sampleRate) + 2;
        line_.prepare(maxDelaySamples_, 1);
        prepared_ = true;
        for (int i = 0; i < kMaxTaps; ++i)
            updateTap(i);
        reset();
    }

    void reset() noexcept { line_.reset(); }

    void setNumTaps(int n) noexcept { numTaps_ = std::clamp(n, 0, kMaxTaps); }
    int getNumTaps() const noexcept { return numTaps_; }

    void setTap(int index, float delayMs, float gain, float pan) noexcept {
        if (index < 0 || index >= kMaxTaps)
            return;
        taps_[index].delayMs = std::clamp(delayMs, 0.0f, maxDelayMs_);
        taps_[index].gain = gain;
        taps_[index].pan = std::clamp(pan, -1.0f, 1.0f);
        updateTap(index);
    }
    const Tap& getTap(int index) const noexcept { return taps_[index]; }

    void setFeedback(float fb) noexcept { feedback_ = std::clamp(fb, 0.0f, 0.99f); }
    void setMix(float mix) noexcept { mix_ = std::clamp(mix, 0.0f, 1.0f); }

    void process(dawlab::AudioBlock block) noexcept {
        const int n = block.getNumSamples();
        const int nch = std::min(block.getNumChannels(), numChannels_);
        if (nch <= 0)
            return;
        float* left = block.getChannelPointer(0);
        float* right = nch > 1 ? block.getChannelPointer(1) : nullptr;
        for (int i = 0; i < n; ++i) {
            const float xl = left[i];
            const float xr = right ? right[i] : xl;
            const float mono = right ? 0.5f * (xl + xr) : xl;

            float wetL = 0.0f, wetR = 0.0f, last = 0.0f;
            // TODO: 對每個 tap t < numTaps_：
            //   v = line_.readCubic(0, state_[t].delaySamples − 1)
            //   wetL += state_[t].gainL · v；wetR += state_[t].gainR · v；last = v
            line_.push(0, mono + feedback_ * last);

            if (right) {
                left[i] = (1.0f - mix_) * xl + mix_ * wetL;
                right[i] = (1.0f - mix_) * xr + mix_ * wetR;
            } else {
                left[i] = (1.0f - mix_) * xl + mix_ * (wetL + wetR);
            }
        }
    }

    static void panGains(float pan, float& gl, float& gr) noexcept {
        const float theta = (pan + 1.0f) * static_cast<float>(dawlab::kPi) * 0.25f;
        gl = std::cos(theta);
        gr = std::sin(theta);
    }

private:
    struct TapState {
        float delaySamples = 2.0f;
        float gainL = 0.0f, gainR = 0.0f;
    };

    void updateTap(int i) noexcept {
        if (!prepared_)
            return;
        const Tap& t = taps_[i];
        TapState& s = state_[i];
        // TODO: 把 t.delayMs 換成 sample（clamp 到 [2, maxDelaySamples_]），
        //       用 panGains() 算 gainL / gainR（記得乘上 t.gain）。
        (void)t;
        s.delaySamples = 2.0f;
        s.gainL = s.gainR = 0.0f;
    }

    FractionalDelayLine line_;
    Tap taps_[kMaxTaps];
    TapState state_[kMaxTaps];
    double sampleRate_ = 48000.0;
    int numChannels_ = 2;
    int numTaps_ = 0;
    int maxDelaySamples_ = 0;
    float maxDelayMs_ = 2000.0f;
    float feedback_ = 0.0f;
    float mix_ = 0.4f;
    bool prepared_ = false;
};

void processInBlocks(MultiTapDelay& d, dawlab::AudioBuffer& b) {
    dawlab::AudioBlock whole(b);
    for (int start = 0; start < b.getNumSamples(); start += kBlockSize)
        d.process(whole.getSubBlock(start, std::min(kBlockSize, b.getNumSamples() - start)));
}

void setupRhythmicTaps(MultiTapDelay& d, double bpm) {
    d.setNumTaps(3);
    d.setTap(0, Delay::noteToMs(bpm, 0.75), 0.6f, -0.6f);
    d.setTap(1, Delay::noteToMs(bpm, 1.0), 0.45f, 0.6f);
    d.setTap(2, Delay::noteToMs(bpm, 1.5), 0.3f, 0.0f);
}

} // namespace

int main() {
    std::printf("=== 第 09 章 練習 5（起始碼）：multi-tap delay ===\n\n");
    const double bpm = 120.0;

    MultiTapDelay mt;
    mt.setMaxDelayMs(2000.0f);
    mt.prepare(kSampleRate, kBlockSize, 2);
    setupRhythmicTaps(mt, bpm);
    mt.setFeedback(0.5f);
    mt.setMix(1.0f);
    mt.reset();
    dawlab::AudioBuffer ir = dawlab::makeImpulse(dawlab::secondsToSamples(3.0, kSampleRate), 2);
    processInBlocks(mt, ir);

    for (int t = 0; t < mt.getNumTaps(); ++t) {
        const auto& tap = mt.getTap(t);
        const int n = dawlab::msToSamples(tap.delayMs, kSampleRate);
        float gl, gr;
        MultiTapDelay::panGains(tap.pan, gl, gr);
        std::printf("tap %d @ %6d：L 預期 %.4f 量到 %.4f，R 預期 %.4f 量到 %.4f\n", t, n, tap.gain * gl,
                    ir.getSample(0, n), tap.gain * gr, ir.getSample(1, n));
        DAWLAB_CHECK_NEAR(ir.getSample(0, n), tap.gain * gl, 1.0e-4);
        DAWLAB_CHECK_NEAR(ir.getSample(1, n), tap.gain * gr, 1.0e-4);
        DAWLAB_CHECK_NEAR(ir.getSample(0, n - 1), 0.0, 1.0e-6);
    }
    const int dLast = dawlab::msToSamples(mt.getTap(2).delayMs, kSampleRate);
    const int d0 = dawlab::msToSamples(mt.getTap(0).delayMs, kSampleRate);
    float gl, gr;
    MultiTapDelay::panGains(mt.getTap(0).pan, gl, gr);
    DAWLAB_CHECK_NEAR(ir.getSample(0, dLast + d0), 0.5f * mt.getTap(0).gain * gl, 1.0e-4);
    DAWLAB_CHECK(!dawlab::hasNanOrInf(ir));
    return dawlab::test::summary("l09_ex5_starter");
}
