// 練習 6（起始碼）：可切換 12 / 24 dB/oct 的 high cut（lowpass），切換時不 click
//
// 目標：
//   1. 12 dB/oct = 一顆 Butterworth lowpass（Q = 1/√2）。
//      24 dB/oct = 兩顆 lowpass 串接，Q 用 butterworthQ(4, 0) 與 butterworthQ(4, 1)（第 02 章 Biquad.h）。
//   2. 兩種 slope 在 fc 都要是 -3 dB；在其他頻率要符合數位 Butterworth 理論值
//      |H|² = 1 / (1 + (tan(πf/fs) / tan(πfc/fs))^(2N))。
//   3. 切換 slope 時用 SmoothedValue 做 crossfade，fc 處的正弦波不可以出現大跳變。
// 執行時 self-check 會印出 FAIL，直到你把 TODO 補完。

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
        // TODO: 不要直接切，改成把 mix_ 的目標設為 0（12）或 1（24）做 crossfade
        mix_.setCurrentAndTargetValue(slope == Slope::Db24 ? 1.0f : 0.0f);
    }
    void setCrossfadeSeconds(double seconds) noexcept { crossfadeSeconds_ = std::max(0.0, seconds); }
    Slope getSlope() const noexcept { return slope_; }

    double getMagnitudeDb(double freq) const noexcept {
        (void)freq;
        return 0.0; // TODO: 依目前 slope 回傳 lp12 或 lp24a + lp24b 的 dB
    }

    // TODO: 數位 Butterworth 的理論值：-10 log10(1 + (tan(πf/fs) / tan(πfc/fs))^(2N))
    static double butterworthDb(double freq, double cutoff, double sampleRate, int order) noexcept {
        (void)freq;
        (void)cutoff;
        (void)sampleRate;
        (void)order;
        return 0.0;
    }

    float processSample(int ch, float x) noexcept {
        if (ch == 0)
            mixCurrent_ = mix_.getNextValue();
        // TODO: y12 = lp12_(x)；y24 = lp24b_(lp24a_(x))；回傳 y12 + mix * (y24 - y12)
        return lp12_.processSample(ch, x);
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
        // TODO: lp24a_ / lp24b_ 用 butterworthQ(4, 0)、butterworthQ(4, 1)
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
        hc.prepare(sr, 512, 1);
        std::printf("  %d dB/oct（%d 階 Butterworth），fc = %.0f Hz\n", 6 * order, order, fc);
        for (double f : {500.0, 1000.0, 2000.0, 4000.0}) {
            hc.reset();
            const double theory = SwitchableHighCut::butterworthDb(f, fc, sr, order);
            const double m = measureGainDb([&](float x) { return hc.processSample(0, x); }, f, sr);
            std::printf("    %7.0f    理論 %+7.2f    實測 %+7.2f\n", f, theory, m);
            DAWLAB_CHECK_NEAR(m, theory, 0.1);
            DAWLAB_CHECK_NEAR(hc.getMagnitudeDb(f), theory, 1e-6);
        }
        DAWLAB_CHECK_NEAR(hc.getMagnitudeDb(fc), -3.0103, 0.01);
    }
    DAWLAB_CHECK_NEAR(SwitchableHighCut::butterworthDb(2000.0, fc, sr, 2), -12.38, 0.05);
    DAWLAB_CHECK_NEAR(SwitchableHighCut::butterworthDb(2000.0, fc, sr, 4), -24.25, 0.05);

    const float sineSlope = static_cast<float>(0.5 * kTwoPi * 1000.0 / sr);
    const float jumpHard = maxJumpWhenSwitching(0.0, sr);
    const float jumpSoft = maxJumpWhenSwitching(0.02, sr);
    std::printf("\n  切換時最大跳變：直接 %.4f / crossfade %.4f（正弦波本身 %.4f）\n", jumpHard, jumpSoft, sineSlope);
    DAWLAB_CHECK(jumpHard > 3.0f * sineSlope);
    DAWLAB_CHECK(jumpSoft < 1.2f * sineSlope);

    // TODO（第 4 小題，試聽）：drum loop（makeDrumLoop(sr, 2)）過 high cut 2 kHz，
    //   以 512 sample 為一個 block 處理：前 2 秒 12 dB/oct，在包含 2 秒那個 block 的開頭 setSlope(Db24)，
    //   最後 writeWav(outputPath("l03_ex6_highcut_switch.wav"), wet, sr)。切換處不應該聽到 click。

    return dawlab::test::summary("l03_ex6_starter");
}
