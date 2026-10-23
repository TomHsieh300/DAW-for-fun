// 練習 5 解答：multi-tap delay
//
// 一條 delay line、多個讀取點（tap）。每個 tap 有自己的延遲時間、增益與 pan，
// 輸入先混成 mono 寫進 delay line，各 tap 的輸出依 pan 分配到左右：
//
//   (L+R)/2 ──►(+)──► delay line ─┬─ tap0 (D0, g0, pan0) ─┐
//               ▲                 ├─ tap1 (D1, g1, pan1) ─┼─(Σ)─► wet L / wet R
//               │                 └─ tap2 (D2, g2, pan2) ─┘
//               └─── ×fb ◄────────── 最後一個 tap（未乘 gain / pan）
//
// 和「三台 delay 串接」不同：tap 之間互不影響，只有一條 feedback 路徑（從最後一個 tap 回到輸入），
// 所以 CPU 便宜、也不會因為串接而累積插值的高頻損失。
// pan 用 constant-power law：gL = cos θ、gR = sin θ，θ = (pan + 1)·π/4。
//
// 自我檢查：impulse 進去，每個 tap 都要在「正確的 sample」出現「正確的振幅」；
// 加上 feedback 之後，整組 tap 會在 D_last 之後再出現一次（乘上 fb）。
//
// 執行：l09_ex5_solution。輸出 dawlab_output/l09_ex5_multitap_dry.wav、l09_ex5_multitap_wet.wav。

#include <dawlab/Common.h>
#include <dawlab/dsp/Delay.h> // 只用 Delay::noteToMs 做 tempo sync
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
        float pan = 0.0f; // -1 = 左, 0 = 中, +1 = 右
    };

    void setMaxDelayMs(float ms) noexcept { maxDelayMs_ = std::max(1.0f, ms); }

    void prepare(double sampleRate, int /*maxBlockSize*/, int numChannels) {
        sampleRate_ = sampleRate;
        numChannels_ = std::clamp(numChannels, 1, 2);
        maxDelaySamples_ = dawlab::msToSamples(maxDelayMs_, sampleRate) + 2;
        line_.prepare(maxDelaySamples_, 1); // mono delay line
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
            for (int t = 0; t < numTaps_; ++t) {
                const TapState& s = state_[t];
                const float v = line_.readCubic(0, s.delaySamples - 1.0f); // 先讀再 push → −1
                wetL += s.gainL * v;
                wetR += s.gainR * v;
                last = v; // feedback 取最後一個 tap 的「原始」延遲訊號
            }
            line_.push(0, mono + feedback_ * last);

            if (right) {
                left[i] = (1.0f - mix_) * xl + mix_ * wetL;
                right[i] = (1.0f - mix_) * xr + mix_ * wetR;
            } else {
                left[i] = (1.0f - mix_) * xl + mix_ * (wetL + wetR); // mono：左右加總（pan 失去意義）
            }
        }
    }

    // constant-power pan law（第 01 章 Panner 的 ConstantPower）
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
        s.delaySamples = std::clamp(static_cast<float>(t.delayMs * 0.001 * sampleRate_), 2.0f,
                                    static_cast<float>(maxDelaySamples_));
        float gl, gr;
        panGains(t.pan, gl, gr);
        s.gainL = t.gain * gl;
        s.gainR = t.gain * gr;
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

// 120 BPM 的三個 tap：附點八分（左）、四分（右）、附點四分（中）
void setupRhythmicTaps(MultiTapDelay& d, double bpm) {
    d.setNumTaps(3);
    d.setTap(0, Delay::noteToMs(bpm, 0.75), 0.6f, -0.6f);
    d.setTap(1, Delay::noteToMs(bpm, 1.0), 0.45f, 0.6f);
    d.setTap(2, Delay::noteToMs(bpm, 1.5), 0.3f, 0.0f);
}

} // namespace

int main() {
    std::printf("=== 第 09 章 練習 5：multi-tap delay ===\n\n");
    const double bpm = 120.0;

    // ---- 1. impulse：每個 tap 的位置與振幅 --------------------------------------
    {
        MultiTapDelay mt;
        mt.setMaxDelayMs(2000.0f);
        mt.prepare(kSampleRate, kBlockSize, 2);
        setupRhythmicTaps(mt, bpm);
        mt.setFeedback(0.0f);
        mt.setMix(1.0f);
        mt.reset();

        dawlab::AudioBuffer ir = dawlab::makeImpulse(dawlab::secondsToSamples(2.0, kSampleRate), 2); // L = R = 1
        processInBlocks(mt, ir);
        std::printf("impulse（L = R = 1）經過三個 tap（fb = 0）：\n");
        std::printf("   tap   delay(ms)  sample   gain   pan    L 預期 / 量到      R 預期 / 量到\n");
        for (int t = 0; t < mt.getNumTaps(); ++t) {
            const auto& tap = mt.getTap(t);
            const int n = dawlab::msToSamples(tap.delayMs, kSampleRate);
            float gl, gr;
            MultiTapDelay::panGains(tap.pan, gl, gr);
            const float expL = tap.gain * gl, expR = tap.gain * gr;
            const float gotL = ir.getSample(0, n), gotR = ir.getSample(1, n);
            std::printf("   %d   %8.1f  %6d   %.2f  %+.1f   %.4f / %.4f    %.4f / %.4f\n", t, tap.delayMs, n,
                        tap.gain, tap.pan, expL, gotL, expR, gotR);
            DAWLAB_CHECK_NEAR(gotL, expL, 1.0e-4);
            DAWLAB_CHECK_NEAR(gotR, expR, 1.0e-4);
            DAWLAB_CHECK_NEAR(ir.getSample(0, n - 1), 0.0, 1.0e-6); // 前一個 sample 必須是 0（位置精準）
            DAWLAB_CHECK_NEAR(ir.getSample(0, n + 1), 0.0, 1.0e-6);
        }
        // 整段 impulse response 的總能量 = Σ gain²（constant-power pan 的 gL² + gR² = 1）
        double energy = 0.0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < ir.getNumSamples(); ++i)
                energy += static_cast<double>(ir.getSample(ch, i)) * ir.getSample(ch, i);
        DAWLAB_CHECK_NEAR(energy, 0.6 * 0.6 + 0.45 * 0.45 + 0.3 * 0.3, 1.0e-4);
        std::printf("  總能量 %.4f = 0.6² + 0.45² + 0.3²（constant-power pan 不改變能量）\n\n", energy);
    }

    // ---- 2. feedback：整組 tap 在最後一個 tap 之後重複 -------------------------
    {
        MultiTapDelay mt;
        mt.setMaxDelayMs(2000.0f);
        mt.prepare(kSampleRate, kBlockSize, 2);
        setupRhythmicTaps(mt, bpm);
        mt.setFeedback(0.5f);
        mt.setMix(1.0f);
        mt.reset();
        dawlab::AudioBuffer ir = dawlab::makeImpulse(dawlab::secondsToSamples(3.0, kSampleRate), 2);
        processInBlocks(mt, ir);
        const int dLast = dawlab::msToSamples(mt.getTap(2).delayMs, kSampleRate); // 750 ms = 36000
        const int d0 = dawlab::msToSamples(mt.getTap(0).delayMs, kSampleRate);    // 375 ms = 18000
        float gl, gr;
        MultiTapDelay::panGains(mt.getTap(0).pan, gl, gr);
        const float expL = 0.5f * mt.getTap(0).gain * gl; // fb × tap0 的增益
        std::printf("feedback 0.5：tap0 的第二次出現應該在 %d + %d = %d，振幅 %.4f，量到 %.4f\n", dLast, d0,
                    dLast + d0, expL, ir.getSample(0, dLast + d0));
        DAWLAB_CHECK_NEAR(ir.getSample(0, dLast + d0), expL, 1.0e-4);
        DAWLAB_CHECK_NEAR(ir.getSample(0, 2 * dLast), 0.5f * mt.getTap(2).gain * 0.70710678f, 1.0e-4);
        DAWLAB_CHECK(!dawlab::hasNanOrInf(ir));
        std::printf("\n");
    }

    // ---- 3. 音樂素材 ------------------------------------------------------------
    {
        dawlab::AudioBuffer seq = dawlab::makePluckSequence(kSampleRate, 4.0, 0.4f);
        dawlab::AudioBuffer dry(2, dawlab::secondsToSamples(8.0, kSampleRate));
        for (int ch = 0; ch < 2; ++ch)
            dry.copyFrom(ch, 0, seq, ch, 0, seq.getNumSamples());
        dawlab::AudioBuffer wet = dry;
        MultiTapDelay mt;
        mt.setMaxDelayMs(2000.0f);
        mt.prepare(kSampleRate, kBlockSize, 2);
        setupRhythmicTaps(mt, bpm);
        mt.setFeedback(0.3f);
        mt.setMix(0.4f);
        mt.reset();
        processInBlocks(mt, wet);
        dawlab::writeWav(dawlab::outputPath("l09_ex5_multitap_dry.wav"), dry, kSampleRate);
        dawlab::writeWav(dawlab::outputPath("l09_ex5_multitap_wet.wav"), wet, kSampleRate);
        std::printf("撥弦琶音：peak dry %.3f / multi-tap %.3f\n", dawlab::peakAbs(dry), dawlab::peakAbs(wet));
        DAWLAB_CHECK(!dawlab::hasNanOrInf(wet));
        DAWLAB_CHECK(dawlab::peakAbs(wet) < 1.0f);
        std::printf("已輸出 dawlab_output/l09_ex5_multitap_dry.wav, l09_ex5_multitap_wet.wav\n\n");
    }

    // ---- 4. 問題的數字：串接三台 delay 時，插值的高頻損失會累積 ----------------------
    {
        // 每台都是 Δ = 0.5 的 cubic（最壞情況）。單台在 12 kHz 是 −1.07 dB，三台串接就是三倍。
        FractionalDelayLine a, b, c;
        a.prepare(64, 1);
        b.prepare(64, 1);
        c.prepare(64, 1);
        auto once = [&](FractionalDelayLine& line, float x) {
            line.push(0, x);
            return line.readCubic(0, 10.5f);
        };
        const double one = dawlab::measureGainDb([&](float x) { return once(a, x); }, 12000.0, kSampleRate);
        a.reset();
        const double three = dawlab::measureGainDb([&](float x) { return once(c, once(b, once(a, x))); }, 12000.0,
                                                   kSampleRate);
        std::printf("cubic（Δ = 0.5）在 12 kHz：一次 %+.2f dB，三次串接 %+.2f dB（multi-tap 每個 echo 只插值一次）\n",
                    one, three);
        DAWLAB_CHECK_NEAR(one, -1.07, 0.02);
        DAWLAB_CHECK_NEAR(three, 3.0 * one, 0.02);
    }

    return dawlab::test::summary("l09_ex5");
}
