// 練習 6（解答）：可切換 12 / 24 dB/oct 的 high cut（lowpass），切換時不 click
//
// 12 dB/oct = 一顆 2 階 Butterworth（Q = 1/√2）。
// 24 dB/oct = 4 階 Butterworth = 兩顆 2 階串接，Q 分別是 0.5412 與 1.3066（第 02 章 butterworthQ(4, k)）。
//   為什麼不是兩顆 Q = 0.707 串接？那樣在 fc 會是 -6 dB，而且轉角不是 Butterworth 的「最平坦」。
//   4 階 Butterworth 的極點在 s 平面左半圓上每 45° 一對，拆成兩組共軛對就得到那兩個 Q；
//   每顆各自在 fc 不是 -3 dB（Q=0.5412 那顆 -5.33、Q=1.3066 那顆 +2.32），但相乘（dB 相加）剛好 -3.01 dB。
// 理論值（bilinear transform 後的數位 Butterworth）：
//   |H(f)|² = 1 / (1 + (tan(π f / fs) / tan(π fc / fs))^(2N))
//   類比的 (f/fc)^(2N) 被 tan 取代——這就是 frequency warping，遠離 fc 時數位版會比 6N dB/oct 更陡。
// 切換 slope 不 click 的做法：兩條鏈同時跑，用 SmoothedValue 做 20 ms 的 crossfade。
//   直接切換為什麼會 click？fc 處 12 dB/oct 的相位是 -90°、24 dB/oct 是 -180°，瞬間切換 = 波形跳一大步。

#include <dawlab/Common.h>
#include <dawlab/dsp/Biquad.h>
#include <dawlab/dsp/SmoothedValue.h>

#include <cstdio>

using namespace dawlab;
using namespace dawlab::dsp;

namespace {

class SwitchableHighCut {
public:
    enum class Slope { Db12, Db24 };

    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        sampleRate_ = sampleRate;
        numChannels_ = numChannels;
        lp12_.prepare(sampleRate, maxBlockSize, numChannels);
        lp24a_.prepare(sampleRate, maxBlockSize, numChannels);
        lp24b_.prepare(sampleRate, maxBlockSize, numChannels);
        mix_.reset(sampleRate, crossfadeSeconds_);
        mix_.setCurrentAndTargetValue(slope_ == Slope::Db24 ? 1.0f : 0.0f);
        updateCoefficients();
        reset();
    }

    void reset() noexcept {
        lp12_.reset();
        lp24a_.reset();
        lp24b_.reset();
    }

    void setCutoff(float hz) noexcept {
        cutoffHz_ = std::clamp(hz, 20.0f, static_cast<float>(0.45 * sampleRate_));
        updateCoefficients();
    }
    void setSlope(Slope slope) noexcept {
        slope_ = slope;
        mix_.setTargetValue(slope == Slope::Db24 ? 1.0f : 0.0f); // crossfade 到另一條鏈
    }
    // 在 prepare 之前呼叫；0 = 直接切換（會 click，拿來當對照組）
    void setCrossfadeSeconds(double seconds) noexcept { crossfadeSeconds_ = std::max(0.0, seconds); }
    Slope getSlope() const noexcept { return slope_; }

    // 目標 slope 的理論響應
    double getMagnitudeDb(double freq) const noexcept {
        if (slope_ == Slope::Db12)
            return lp12_.getCoefficients().magnitudeDb(freq, sampleRate_);
        return lp24a_.getCoefficients().magnitudeDb(freq, sampleRate_) +
               lp24b_.getCoefficients().magnitudeDb(freq, sampleRate_);
    }

    // 數位 Butterworth 的理論值：-10 log10(1 + (tan(πf/fs) / tan(πfc/fs))^(2N))
    static double butterworthDb(double freq, double cutoff, double sampleRate, int order) noexcept {
        const double ratio = std::tan(kPi * freq / sampleRate) / std::tan(kPi * cutoff / sampleRate);
        return -10.0 * std::log10(1.0 + std::pow(ratio, 2.0 * order));
    }

    // 逐 sample：每個 frame 依序呼叫 channel 0, 1, …；crossfade 的 ramp 在 channel 0 推進
    float processSample(int ch, float x) noexcept {
        if (ch == 0)
            mixCurrent_ = mix_.getNextValue();
        const float y12 = lp12_.processSample(ch, x);
        const float y24 = lp24b_.processSample(ch, lp24a_.processSample(ch, x));
        return y12 + mixCurrent_ * (y24 - y12);
    }

    void process(AudioBlock block) noexcept {
        const int nch = std::min(block.getNumChannels(), numChannels_);
        for (int i = 0; i < block.getNumSamples(); ++i)
            for (int ch = 0; ch < nch; ++ch)
                block.getChannelPointer(ch)[i] = processSample(ch, block.getChannelPointer(ch)[i]);
    }

private:
    void updateCoefficients() noexcept {
        lp12_.setCoefficients(BiquadCoefficients::makeLowpass(sampleRate_, cutoffHz_, BiquadCoefficients::kButterworthQ));
        lp24a_.setCoefficients(BiquadCoefficients::makeLowpass(sampleRate_, cutoffHz_, butterworthQ(4, 0))); // 0.5412
        lp24b_.setCoefficients(BiquadCoefficients::makeLowpass(sampleRate_, cutoffHz_, butterworthQ(4, 1))); // 1.3066
    }

    double sampleRate_ = 48000.0;
    int numChannels_ = 0;
    float cutoffHz_ = 1000.0f;
    Slope slope_ = Slope::Db12;
    double crossfadeSeconds_ = 0.02;
    SmoothedValue mix_;
    float mixCurrent_ = 0.0f;
    Biquad lp12_, lp24a_, lp24b_;
};

// 切換 slope 的 click 測試：fc 處的正弦波，中途切換，量最大的 sample 間跳變
float maxJumpWhenSwitching(double crossfadeSeconds, double sr) {
    SwitchableHighCut hc;
    hc.setCrossfadeSeconds(crossfadeSeconds);
    hc.prepare(sr, 512, 1);
    hc.setCutoff(1000.0f);
    AudioBuffer y = makeSine(1000.0, sr, 1.0, 0.5f, 1);
    AudioBlock whole(y);
    for (int start = 0; start < y.getNumSamples(); start += 512) {
        // 在第 48 個 block 開頭（0.512 秒）切換：此時 1 kHz 正弦波相位剛好是 0°，
        // 12 與 24 dB/oct 兩條鏈的輸出差 y24 − y12 = 0.5·cos(θ + 45°)，θ = 0° 時 ≈ 0.35，直接切換一定看得到大跳變
        if (start == 48 * 512)
            hc.setSlope(SwitchableHighCut::Slope::Db24);
        hc.process(whole.getSubBlock(start, std::min(512, y.getNumSamples() - start)));
    }
    float maxJump = 0.0f;
    const float* d = y.getReadPointer(0);
    for (int i = secondsToSamples(0.3, sr); i < y.getNumSamples(); ++i)
        maxJump = std::max(maxJump, std::abs(d[i] - d[i - 1]));
    return maxJump;
}

} // namespace

int main() {
    constexpr double sr = 48000.0;
    constexpr double fc = 1000.0;
    std::printf("=== 第 03 章 練習 6：可切換 12 / 24 dB/oct 的 high cut ===\n\n");

    SwitchableHighCut hc;
    hc.prepare(sr, 512, 1);
    hc.setCutoff(static_cast<float>(fc));

    for (int order : {2, 4}) {
        hc.setSlope(order == 2 ? SwitchableHighCut::Slope::Db12 : SwitchableHighCut::Slope::Db24);
        hc.prepare(sr, 512, 1); // 讓 crossfade 直接對齊（量測不用等）
        std::printf("  %d dB/oct（%d 階 Butterworth），fc = %.0f Hz\n", 6 * order, order, fc);
        std::printf("    freq(Hz)   理論(dB)   實測(dB)   類比 Butterworth(dB)\n");
        for (double f : {250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0}) {
            hc.reset();
            const double theory = SwitchableHighCut::butterworthDb(f, fc, sr, order);
            const double m = measureGainDb([&](float x) { return hc.processSample(0, x); }, f, sr);
            const double ideal = -10.0 * std::log10(1.0 + std::pow(f / fc, 2.0 * order)); // 類比（無 warp）
            std::printf("    %7.0f    %+7.2f    %+7.2f      %+7.2f\n", f, theory, m, ideal);
            DAWLAB_CHECK_NEAR(m, theory, 0.1);
            DAWLAB_CHECK_NEAR(hc.getMagnitudeDb(f), theory, 1e-6);
        }
        DAWLAB_CHECK_NEAR(hc.getMagnitudeDb(fc), -3.0103, 0.01); // 兩種 slope 在 fc 都是 -3 dB
    }
    // 2 fc 處：12 dB/oct 約 -12.4 dB、24 dB/oct 約 -24.3 dB（比類比的 12.3 / 24.1 稍多：warping）
    DAWLAB_CHECK_NEAR(SwitchableHighCut::butterworthDb(2000.0, fc, sr, 2), -12.38, 0.05);
    DAWLAB_CHECK_NEAR(SwitchableHighCut::butterworthDb(2000.0, fc, sr, 4), -24.25, 0.05);
    // 4 階拆成兩顆的 Q 各自在 fc 的增益：相加才是 -3 dB
    const double qa = BiquadCoefficients::makeLowpass(sr, fc, butterworthQ(4, 0)).magnitudeDb(fc, sr);
    const double qb = BiquadCoefficients::makeLowpass(sr, fc, butterworthQ(4, 1)).magnitudeDb(fc, sr);
    std::printf("\n  4 階的兩顆在 fc：Q=0.5412 -> %+.2f dB，Q=1.3066 -> %+.2f dB，相加 %+.2f dB\n", qa, qb, qa + qb);
    DAWLAB_CHECK_NEAR(qa + qb, -3.0103, 0.01);

    // ---- 切換時的 click ----
    const float sineSlope = static_cast<float>(0.5 * kTwoPi * 1000.0 / sr); // 正弦波自己每個 sample 最多變這麼多
    const float jumpHard = maxJumpWhenSwitching(0.0, sr);
    const float jumpSoft = maxJumpWhenSwitching(0.02, sr);
    std::printf("\n  1 kHz 正弦波（= fc）中途 12 -> 24 dB/oct：最大 sample 跳變\n");
    std::printf("    直接切換      %.4f（正弦波本身最多 %.4f）\n", jumpHard, sineSlope);
    std::printf("    20 ms crossfade %.4f\n", jumpSoft);
    DAWLAB_CHECK(jumpHard > 3.0f * sineSlope); // 直接切換：-90° 跳到 -180°，波形跳一大步
    DAWLAB_CHECK(jumpSoft < 1.2f * sineSlope); // crossfade：跟正弦波自己的變化量差不多

    // 試聽：drum loop，前半 12、後半 24 dB/oct，fc = 2 kHz
    AudioBuffer wet = makeDrumLoop(sr, 2);
    SwitchableHighCut live;
    live.prepare(sr, 512, wet.getNumChannels());
    live.setCutoff(2000.0f);
    AudioBlock whole(wet);
    for (int start = 0; start < wet.getNumSamples(); start += 512) {
        if (start <= secondsToSamples(2.0, sr) && secondsToSamples(2.0, sr) < start + 512) // 包含該時間點的那個 block
            live.setSlope(SwitchableHighCut::Slope::Db24);
        live.process(whole.getSubBlock(start, std::min(512, wet.getNumSamples() - start)));
    }
    DAWLAB_CHECK(!hasNanOrInf(wet));
    writeWav(outputPath("l03_ex6_highcut_switch.wav"), wet, sr);
    std::printf("\n  drum loop high cut 2 kHz：前 2 秒 12 dB/oct、後 2 秒 24 dB/oct -> dawlab_output/l03_ex6_highcut_switch.wav\n");

    return dawlab::test::summary("l03_ex6");
}
