// 練習 4（起始碼）：用 biquad 做簡易 cabinet simulator
//
// 目標：寫一個 CabSim class，頻率響應 =
//     low cut（2 階 Butterworth highpass，預設 70 Hz）
//   × 共振 peak（makePeak，預設 100 Hz、Q 1.5、+4 dB，模擬單體 / 箱體共振）
//   × 4 階 Linkwitz-Riley lowpass（兩顆 Q = 1/√2 的 Butterworth lowpass 串接，預設 4.5 kHz）
//
// 必須提供：prepare / reset / setLowCutHz / setResonanceHz / setResonanceDb / setCutoffHz /
//          processSample(ch, x) / process(block) / double magnitudeDb(freq)（分析用：把每一級的 magnitudeDb 相加）
//
// 執行：l07_ex4_starter → 目前會 FAIL，寫完後應該全部 PASS。

#include <dawlab/Common.h>
#include <dawlab/dsp/Biquad.h>

#include <cmath>
#include <cstdio>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr double kButterworthQ = 0.70710678118654752; // 1/√2：二階 Butterworth 的 Q

class CabSim {
public:
    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        sampleRate_ = sampleRate;
        // TODO: prepare 四顆 biquad，然後 updateCoefficients()
        (void)maxBlockSize;
        (void)numChannels;
    }
    void reset() noexcept {
        // TODO
    }
    void setLowCutHz(float hz) noexcept { lowCutHz_ = hz; updateCoefficients(); }
    void setResonanceHz(float hz) noexcept { resHz_ = hz; updateCoefficients(); }
    void setResonanceDb(float db) noexcept { resDb_ = db; updateCoefficients(); }
    void setCutoffHz(float hz) noexcept { cutoffHz_ = hz; updateCoefficients(); }

    float processSample(int ch, float x) noexcept {
        // TODO: lowCut → resonance → lp1 → lp2
        (void)ch;
        return x;
    }
    void process(dawlab::AudioBlock block) noexcept {
        for (int ch = 0; ch < block.getNumChannels(); ++ch) {
            float* d = block.getChannelPointer(ch);
            for (int i = 0; i < block.getNumSamples(); ++i)
                d[i] = processSample(ch, d[i]);
        }
    }

    // 線性級串接 → 各級 dB 相加
    double magnitudeDb(double freq) const noexcept {
        // TODO
        (void)freq;
        return 0.0;
    }

private:
    void updateCoefficients() noexcept {
        if (sampleRate_ <= 0.0)
            return;
        // TODO: 用 BiquadCoefficients::makeHighpass / makePeak / makeLowpass 算係數
    }

    double sampleRate_ = 0.0;
    float lowCutHz_ = 70.0f, resHz_ = 100.0f, resDb_ = 4.0f, cutoffHz_ = 4500.0f;
    dawlab::dsp::Biquad lowCut_, resonance_, lp1_, lp2_;
};

} // namespace

int main() {
    CabSim cab;
    cab.prepare(kSampleRate, kBlockSize, 1);
    cab.setLowCutHz(70.0f);
    cab.setResonanceHz(100.0f);
    cab.setResonanceDb(4.0f);
    cab.setCutoffHz(4500.0f);

    // 分析式響應（magnitudeDb）的關鍵點
    std::printf("%8s %12s %12s\n", "freq", "analytic", "measured");
    bool consistent = true;
    for (double f : {30.0, 70.0, 100.0, 200.0, 1000.0, 2250.0, 4500.0, 9000.0, 15000.0}) {
        cab.reset();
        const double analytic = cab.magnitudeDb(f);
        const double measured = dawlab::measureGainDb([&](float x) { return cab.processSample(0, x); }, f, kSampleRate);
        std::printf("%8.0f %12.2f %12.2f\n", f, analytic, measured);
        consistent = consistent && std::abs(analytic - measured) < 0.2;
    }
    DAWLAB_CHECK(consistent);                                        // 實際處理 = 分析式
    DAWLAB_CHECK_NEAR(cab.magnitudeDb(4500.0), -6.02, 0.3);          // LR4：cutoff 處 -6 dB
    // 一個 octave 上去：類比 LR4 應為 -24.6 dB，但 bilinear transform 的 frequency warping 讓數位 filter
    // 在高頻掉得更快：等效的類比頻率比 = tan(π f/fs) / tan(π fc/fs)（9 kHz / 4.5 kHz @ 48 kHz → 2.20 而不是 2）
    {
        const double r = std::tan(dawlab::kPi * 9000.0 / kSampleRate) / std::tan(dawlab::kPi * 4500.0 / kSampleRate);
        const double expectedDb = -2.0 * 10.0 * std::log10(1.0 + r * r * r * r); // 兩顆 2 階 Butterworth
        std::printf("9 kHz：warped ratio %.3f → 預期 %.2f dB（未 warp 的類比值 -24.6 dB）\n", r, expectedDb);
        DAWLAB_CHECK_NEAR(cab.magnitudeDb(9000.0), expectedDb, 0.5);
    }
    DAWLAB_CHECK(cab.magnitudeDb(1000.0) > -0.5 && cab.magnitudeDb(1000.0) < 0.5); // 中頻透明
    DAWLAB_CHECK(cab.magnitudeDb(100.0) > 2.5 && cab.magnitudeDb(100.0) < 4.5);    // 共振（扣掉 low cut 的一點損失）
    DAWLAB_CHECK(cab.magnitudeDb(30.0) < -12.0);                                     // low cut
    DAWLAB_CHECK(cab.magnitudeDb(15000.0) < -40.0);

    return dawlab::test::summary("l07_ex4");
}
