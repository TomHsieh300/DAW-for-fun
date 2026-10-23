// 練習 4 解答：tape-style delay —— feedback 迴路內的 lowpass + highpass + 輕微 saturation
//
// 磁帶 echo（Echoplex、Space Echo）每繞一圈，聲音都會：
//   1. 變暗（磁帶 / 磁頭的高頻損失）        → feedback 迴路內的 one-pole lowpass
//   2. 低頻被削掉一點（變壓器、耦合電容）    → feedback 迴路內的 one-pole highpass
//   3. 被磁帶輕微壓縮 / 飽和                 → feedback 迴路內的 tanh
//
//   x ──►(+)──► delay line ──┬──► wet ──(mix)──► y
//         ▲                  │
//         └── ×fb ◄── tanh ◄── LP ◄── HP ◄┘
//
// 第 3 點是 dub delay 的靈魂：feedback 轉超過 1 時，線性的 delay 會發散到無限大，
// 但 tanh 把每一圈的增益「壓」回來，結果是一個振幅受控、會自己唱歌的 self-oscillation。
// 本解答用數字驗證這兩件事：(a) 第二個 echo 的高頻比第一個少很多；(b) feedback = 1.1 時
// 線性版本發散、tape 版本停在一個固定振幅（可用 describing function 預測）。
//
// 執行：l09_ex4_solution。輸出 dawlab_output/l09_ex4_tape_dry.wav、l09_ex4_tape_wet.wav、l09_ex4_dub.wav。

#include <dawlab/Common.h>
#include <dawlab/dsp/FractionalDelayLine.h>
#include <dawlab/dsp/OnePole.h>
#include <dawlab/dsp/SmoothedValue.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

using dawlab::dsp::FractionalDelayLine;
using dawlab::dsp::OnePole;
using dawlab::dsp::SmoothedValue;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

class TapeDelay {
public:
    static constexpr int kMaxChannels = 2;

    void setMaxDelayMs(float ms) noexcept { maxDelayMs_ = std::max(1.0f, ms); }

    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        sampleRate_ = sampleRate;
        numChannels_ = std::clamp(numChannels, 1, kMaxChannels);
        maxDelaySamples_ = dawlab::msToSamples(maxDelayMs_, sampleRate) + 2;
        line_.prepare(maxDelaySamples_, numChannels_);
        lowpass_.setType(OnePole::Type::Lowpass);
        lowpass_.prepare(sampleRate, maxBlockSize, numChannels_);
        highpass_.setType(OnePole::Type::Highpass);
        highpass_.prepare(sampleRate, maxBlockSize, numChannels_);
        delaySamples_.reset(sampleRate, 0.05);
        prepared_ = true;
        setDelayMs(delayMs_);
        setLowpassHz(lowpassHz_);
        setHighpassHz(highpassHz_);
        reset();
    }

    void reset() noexcept {
        line_.reset();
        lowpass_.reset();
        highpass_.reset();
        delaySamples_.setCurrentAndTargetValue(targetDelaySamples());
    }

    void setDelayMs(float ms) noexcept {
        delayMs_ = std::clamp(ms, 0.0f, maxDelayMs_);
        if (prepared_)
            delaySamples_.setTargetValue(targetDelaySamples());
    }
    // 這裡刻意允許到 1.5：feedback > 1 是 dub 的玩法，由 saturation 負責把它關在籠子裡
    void setFeedback(float fb) noexcept { feedback_ = std::clamp(fb, 0.0f, 1.5f); }
    void setMix(float mix) noexcept { mix_ = std::clamp(mix, 0.0f, 1.0f); }
    void setLowpassHz(float hz) noexcept {
        lowpassHz_ = std::clamp(hz, 20.0f, 20000.0f);
        if (prepared_)
            lowpass_.setCutoff(lowpassHz_);
    }
    void setHighpassHz(float hz) noexcept {
        highpassHz_ = std::clamp(hz, 20.0f, 20000.0f);
        if (prepared_)
            highpass_.setCutoff(highpassHz_);
    }
    // drive：tanh 的輸入放大倍率。輸出除回 drive，小訊號增益保持 1，只有大訊號被壓。
    void setDrive(float drive) noexcept { drive_ = std::max(0.1f, drive); }
    void setSaturationEnabled(bool on) noexcept { saturate_ = on; }

    float getLoopGainAt(double freq) const {
        // 給 README / 自我檢查用：迴路一圈的線性增益 fb·|HP(f)|·|LP(f)|（不含 tanh；HP 永遠在迴路裡）
        return feedback_ * onePoleMag(freq, lowpassHz_, false) * onePoleMag(freq, highpassHz_, true);
    }

    void process(dawlab::AudioBlock block) noexcept {
        const int n = block.getNumSamples();
        const int nch = std::min(block.getNumChannels(), numChannels_);
        for (int i = 0; i < n; ++i) {
            const float d = delaySamples_.getNextValue();
            for (int ch = 0; ch < nch; ++ch) {
                float* data = block.getChannelPointer(ch);
                const float x = data[i];
                const float wet = line_.readCubic(ch, d - 1.0f); // 先讀再 push，所以 −1
                line_.push(ch, x + feedbackPath(ch, wet));
                data[i] = (1.0f - mix_) * x + mix_ * wet;
            }
        }
    }

private:
    // 數位 OnePole（第 02 章）的實際幅度響應，而不是類比 one-pole 的 1/√(1+(f/fc)²)：
    //   a = exp(−2π·fc/fs)，ω = 2π·f/fs，分母 |1 − a·e^{−jω}| = √(1 − 2a·cosω + a²)
    //   lowpass ：H = (1−a) / (1 − a z⁻¹)               → |H| = (1−a) / 分母
    //   highpass：H = ½(1+a)(1 − z⁻¹) / (1 − a z⁻¹)     → |H| = ½(1+a)·√(2 − 2cosω) / 分母
    // 類比公式在低頻幾乎一樣，但在 8 kHz（fs/6）附近會差好幾個百分點。
    float onePoleMag(double f, float cutoff, bool highpass) const noexcept {
        const double fc = std::clamp(static_cast<double>(cutoff), 1.0e-3, sampleRate_ * 0.49); // 同 OnePole 的 clamp
        const double a = dawlab::dsp::OnePole::coefficientForCutoff(fc, sampleRate_);
        const double c = std::cos(dawlab::kTwoPi * f / sampleRate_);
        const double den = std::sqrt(1.0 - 2.0 * a * c + a * a);
        const double mag = highpass ? 0.5 * (1.0 + a) * std::sqrt(2.0 - 2.0 * c) / den : (1.0 - a) / den;
        return static_cast<float>(mag);
    }

    float targetDelaySamples() const noexcept {
        return std::clamp(static_cast<float>(delayMs_ * 0.001 * sampleRate_), 2.0f,
                          static_cast<float>(maxDelaySamples_));
    }

    // feedback 路徑：HP → LP → tanh → ×fb
    float feedbackPath(int ch, float wet) noexcept {
        float v = highpass_.processSample(ch, wet);
        v = lowpass_.processSample(ch, v);
        if (saturate_)
            v = std::tanh(drive_ * v) / drive_; // 小訊號 ≈ v；|v| 大時被壓到 ±1/drive
        return feedback_ * v;
    }

    FractionalDelayLine line_;
    OnePole lowpass_, highpass_;
    SmoothedValue delaySamples_;
    double sampleRate_ = 48000.0;
    int numChannels_ = 2;
    int maxDelaySamples_ = 0;
    float maxDelayMs_ = 2000.0f;
    float delayMs_ = 375.0f;
    float feedback_ = 0.5f;
    float mix_ = 0.35f;
    float lowpassHz_ = 2500.0f;
    float highpassHz_ = 120.0f;
    float drive_ = 1.5f;
    bool saturate_ = true;
    bool prepared_ = false;
};

void processInBlocks(TapeDelay& d, dawlab::AudioBuffer& b) {
    dawlab::AudioBlock whole(b);
    for (int start = 0; start < b.getNumSamples(); start += kBlockSize)
        d.process(whole.getSubBlock(start, std::min(kBlockSize, b.getNumSamples() - start)));
}

// 用 describing function預測 self-oscillation 的振幅。
// 迴路裡繞的近似是一個正弦。設 tanh 的輸入是 X·sin θ，它的輸出基頻振幅是
//     F1(X) = (2/π) ∫₀^π tanh(X sin θ) sin θ dθ        （小訊號時 F1 ≈ X，大訊號時 → 4/π）
// 繞一圈的「基頻」增益 = G · F1(X)/X，G = fb·|HP|·|LP| 是迴路的線性增益（drive 在 tanh 前乘、後除，互相抵消）。
// 穩定振盪 ⇔ 這個增益剛好 = 1：F1(X)/X = 1/G。F1(X)/X 從 1 單調遞減，所以用二分法解 X。
// 迴路訊號（= wet）就是 fb·tanh(·)/drive 的輸出，它的峰值 = fb·tanh(X)/drive。
double fundamentalOfTanh(double X) {
    const int steps = 2000;
    double sum = 0.0;
    for (int k = 0; k < steps; ++k) {
        const double th = dawlab::kPi * (k + 0.5) / steps; // midpoint rule
        sum += std::tanh(X * std::sin(th)) * std::sin(th);
    }
    return 2.0 / dawlab::kPi * sum * (dawlab::kPi / steps);
}

double selfOscillationPeak(double fb, double loopGain, double drive) {
    const double target = 1.0 / loopGain; // 需要 < 1（迴路線性增益 > 1 才會自激）
    double lo = 1.0e-6, hi = 50.0;
    for (int i = 0; i < 60; ++i) {
        const double mid = 0.5 * (lo + hi);
        if (fundamentalOfTanh(mid) / mid > target)
            lo = mid; // 增益還太大 → 振幅要更大
        else
            hi = mid;
    }
    const double X = 0.5 * (lo + hi);
    return fb * std::tanh(X) / drive;
}

} // namespace

int main() {
    std::printf("=== 第 09 章 練習 4：tape-style delay ===\n\n");

    // ---- (a) 每繞一圈都更暗：比較第 1 與第 2 個 echo 的頻譜 ----------------------
    {
        const float delayMs = 200.0f;
        const int D = dawlab::msToSamples(delayMs, kSampleRate);
        TapeDelay tape;
        tape.setMaxDelayMs(500.0f);
        tape.prepare(kSampleRate, kBlockSize, 1);
        tape.setDelayMs(delayMs);
        tape.setFeedback(0.7f);
        tape.setLowpassHz(2000.0f);
        tape.setHighpassHz(100.0f);
        tape.setDrive(0.5f); // 小 drive：這段先看濾波器的效果
        tape.setMix(1.0f);
        tape.reset();

        dawlab::AudioBuffer ir = dawlab::makeImpulse(dawlab::secondsToSamples(1.0, kSampleRate), 1);
        processInBlocks(tape, ir);
        const float* y = ir.getReadPointer(0);
        // echo k 出現在 n = kD。goertzelAmplitude 內部有 Hann window（兩端為 0），
        // 所以把分析視窗的中心對準 echo：[kD − D/2, kD + D/2)
        const double lo1 = dawlab::goertzelAmplitude(y + D / 2, D, 500.0, kSampleRate);
        const double lo2 = dawlab::goertzelAmplitude(y + 3 * D / 2, D, 500.0, kSampleRate);
        const double hi1 = dawlab::goertzelAmplitude(y + D / 2, D, 8000.0, kSampleRate);
        const double hi2 = dawlab::goertzelAmplitude(y + 3 * D / 2, D, 8000.0, kSampleRate);
        const double rLo = lo2 / lo1, rHi = hi2 / hi1;
        std::printf("impulse → echo 1 vs echo 2（delay %.0f ms, fb 0.7, LP 2 kHz, HP 100 Hz）：\n", delayMs);
        std::printf("   500 Hz：echo2/echo1 = %.3f（理論 fb·|HP|·|LP| ≈ %.3f）\n", rLo, tape.getLoopGainAt(500.0));
        std::printf("  8000 Hz：echo2/echo1 = %.3f（理論 ≈ %.3f）\n", rHi, tape.getLoopGainAt(8000.0));
        std::printf("  第一個 echo 的峰值 %.3f 在 n = %d\n", y[D], D);
        DAWLAB_CHECK_NEAR(y[D], 1.0, 1.0e-3);          // 第一個 echo 是未經濾波的原始 impulse
        DAWLAB_CHECK_NEAR(rLo, tape.getLoopGainAt(500.0), 0.05);
        DAWLAB_CHECK_NEAR(rHi, tape.getLoopGainAt(8000.0), 0.05);
        DAWLAB_CHECK(rHi < 0.5 * rLo); // 高頻每圈掉得比低頻快得多
        std::printf("  -> 高頻每圈只剩 %.0f%%，低頻還有 %.0f%%：echo 一次比一次暗。\n\n", 100.0 * rHi, 100.0 * rLo);
    }

    // ---- (b) feedback > 1：線性版本發散，tanh 版本停在固定振幅 ------------------
    {
        const float fb = 1.1f, drive = 2.0f;
        const double toneHz = 200.0;
        auto makeInput = [&] {
            // 200 Hz 正弦只響 0.3 秒，之後 2.7 秒讓迴路自己跑
            dawlab::AudioBuffer b(1, dawlab::secondsToSamples(3.0, kSampleRate));
            const int burst = dawlab::secondsToSamples(0.3, kSampleRate);
            for (int i = 0; i < burst; ++i)
                b.setSample(0, i, static_cast<float>(0.5 * std::sin(dawlab::kTwoPi * toneHz * i / kSampleRate)));
            return b;
        };
        auto run = [&](bool saturate, float driveValue) {
            TapeDelay tape;
            tape.setMaxDelayMs(500.0f);
            tape.prepare(kSampleRate, kBlockSize, 1);
            tape.setDelayMs(100.0f);
            tape.setFeedback(fb);
            tape.setLowpassHz(2000.0f);
            tape.setHighpassHz(20.0f); // HP 降到最低的 20 Hz（仍在迴路裡，200 Hz 處 |HP| ≈ 0.995，影響很小）
            tape.setDrive(driveValue);
            tape.setSaturationEnabled(saturate);
            tape.setMix(1.0f);
            tape.reset();
            dawlab::AudioBuffer b = makeInput();
            processInBlocks(tape, b);
            return std::make_pair(b, tape.getLoopGainAt(toneHz));
        };
        auto [linear, loopGain] = run(false, drive);
        auto [tape, loopGain2] = run(true, drive);
        auto [tape1, loopGain3] = run(true, 1.0f); // drive 1：振幅應該是 drive 2 的兩倍
        (void)loopGain2;
        (void)loopGain3;

        const int tailStart = dawlab::secondsToSamples(2.5, kSampleRate);
        const int tailLen = dawlab::secondsToSamples(0.5, kSampleRate);
        const float linearTailPeak = dawlab::peakAbs(linear.getReadPointer(0) + tailStart, tailLen);
        const float tapeTailPeak = dawlab::peakAbs(tape.getReadPointer(0) + tailStart, tailLen);
        const double predicted = selfOscillationPeak(fb, loopGain, drive);
        std::printf("feedback = %.1f（迴路線性增益 %.3f > 1）、200 Hz 正弦只響 0.3 s：\n", fb, loopGain);
        std::printf("  線性版本：整體 peak %.2f，2.5–3.0 s 尾巴 peak %.2f（每 100 ms 乘 %.3f，持續成長）\n",
                    dawlab::peakAbs(linear), linearTailPeak, loopGain);
        std::printf("  tanh 版本：整體 peak %.2f，2.5–3.0 s 尾巴 peak %.3f（describing function 預測 %.3f）\n",
                    dawlab::peakAbs(tape), tapeTailPeak, predicted);
        DAWLAB_CHECK(loopGain > 1.0f);
        DAWLAB_CHECK(linearTailPeak > 3.0f);                 // 線性：發散
        DAWLAB_CHECK(dawlab::peakAbs(tape) < 1.2f);          // tanh：有界
        DAWLAB_CHECK_NEAR(tapeTailPeak, predicted, 0.03);    // 而且落在 describing function 預測的振幅
        DAWLAB_CHECK(!dawlab::hasNanOrInf(tape));
        DAWLAB_CHECK(!dawlab::hasNanOrInf(linear));

        // drive 對自激振幅的影響：令 u = drive·v，迴路變成 u ← G·tanh(u)，和 drive 無關，
        // 所以 tanh 的輸入振幅 X 只由 G 決定，而 wet = fb·tanh(X)/drive ∝ 1/drive。
        const float tape1TailPeak = dawlab::peakAbs(tape1.getReadPointer(0) + tailStart, tailLen);
        const double predicted1 = selfOscillationPeak(fb, loopGain, 1.0);
        std::printf("  drive 1 的 tanh 版本：尾巴 peak %.3f（預測 %.3f）；drive 1 / drive 2 = %.2f（理論 2）\n",
                    tape1TailPeak, predicted1, tape1TailPeak / tapeTailPeak);
        DAWLAB_CHECK_NEAR(tape1TailPeak, predicted1, 0.06);
        DAWLAB_CHECK_NEAR(tape1TailPeak / tapeTailPeak, 2.0, 0.1);
        tape.applyGain(0.8f);
        dawlab::writeWav(dawlab::outputPath("l09_ex4_dub.wav"), tape, kSampleRate);
        std::printf("  -> 線性 feedback ≥ 1 一定爆；tanh 讓它變成振幅固定的 self-oscillation（dub delay）。\n");
        std::printf("已輸出 dawlab_output/l09_ex4_dub.wav\n\n");
    }

    // ---- (c) 音樂素材 ------------------------------------------------------------
    {
        dawlab::AudioBuffer seq = dawlab::makePluckSequence(kSampleRate, 4.0, 0.4f);
        dawlab::AudioBuffer dry(2, dawlab::secondsToSamples(8.0, kSampleRate));
        for (int ch = 0; ch < 2; ++ch)
            dry.copyFrom(ch, 0, seq, ch, 0, seq.getNumSamples());
        dawlab::AudioBuffer wet = dry;
        TapeDelay tape;
        tape.setMaxDelayMs(2000.0f);
        tape.prepare(kSampleRate, kBlockSize, 2);
        tape.setDelayMs(375.0f); // 120 BPM 附點八分
        tape.setFeedback(0.6f);
        tape.setLowpassHz(2500.0f);
        tape.setHighpassHz(120.0f);
        tape.setDrive(1.5f);
        tape.setMix(0.35f);
        tape.reset();
        processInBlocks(tape, wet);
        dawlab::writeWav(dawlab::outputPath("l09_ex4_tape_dry.wav"), dry, kSampleRate);
        dawlab::writeWav(dawlab::outputPath("l09_ex4_tape_wet.wav"), wet, kSampleRate);
        std::printf("撥弦琶音：peak dry %.3f / tape %.3f\n", dawlab::peakAbs(dry), dawlab::peakAbs(wet));
        DAWLAB_CHECK(!dawlab::hasNanOrInf(wet));
        DAWLAB_CHECK(dawlab::peakAbs(wet) < 1.0f);
        std::printf("已輸出 dawlab_output/l09_ex4_tape_dry.wav, l09_ex4_tape_wet.wav\n\n");
    }

    return dawlab::test::summary("l09_ex4");
}
