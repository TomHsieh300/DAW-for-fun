// 練習 4 解答：用 biquad 做簡易 cabinet simulator
//
// 真正的吉他喇叭（12 吋單體 + 密閉 / 開放式箱體）的頻率響應大致是：
//   - 70~100 Hz 以下快速掉（箱體擋不住、單體推不動）
//   - 100 Hz 附近有一個箱體 / 單體共振峰
//   - 中頻大致平坦（有很多小起伏，這裡省略）
//   - 4~5 kHz 以上急遽下滑（紙盆的質量讓它跟不上高頻），斜率常超過 24 dB/oct
// 我們用四顆 biquad 近似：highpass（70 Hz）× peak（100 Hz，+4 dB，Q 1.5）× lowpass × lowpass（4.5 kHz，Q = 1/√2）。
//
// 為什麼兩顆 Q = 1/√2 的 lowpass 串接叫 Linkwitz-Riley 4 階（LR4）？
//   一顆 2 階 Butterworth 在 cutoff 處是 -3.01 dB；兩顆串接就是 -6.02 dB，遠離 cutoff 後斜率 24 dB/oct。
//   這個結構在第 03 章的 crossover 也用過（LR 的 lowpass + highpass 在 cutoff 處各 -6 dB，相加剛好 0 dB）。
//
// 線性級串接的總響應 = 各級響應相乘 = 各級 dB 相加，所以 magnitudeDb() 直接把四個 magnitudeDb 加起來即可。
//
// 執行：l07_ex4_solution

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
        lowCut_.prepare(sampleRate, maxBlockSize, numChannels);
        resonance_.prepare(sampleRate, maxBlockSize, numChannels);
        lp1_.prepare(sampleRate, maxBlockSize, numChannels);
        lp2_.prepare(sampleRate, maxBlockSize, numChannels);
        updateCoefficients();
    }
    void reset() noexcept {
        lowCut_.reset();
        resonance_.reset();
        lp1_.reset();
        lp2_.reset();
    }
    void setLowCutHz(float hz) noexcept { lowCutHz_ = hz; updateCoefficients(); }
    void setResonanceHz(float hz) noexcept { resHz_ = hz; updateCoefficients(); }
    void setResonanceDb(float db) noexcept { resDb_ = db; updateCoefficients(); }
    void setCutoffHz(float hz) noexcept { cutoffHz_ = hz; updateCoefficients(); }

    float processSample(int ch, float x) noexcept {
        return lp2_.processSample(ch, lp1_.processSample(ch, resonance_.processSample(ch, lowCut_.processSample(ch, x))));
    }
    void process(dawlab::AudioBlock block) noexcept {
        for (int ch = 0; ch < block.getNumChannels(); ++ch) {
            float* d = block.getChannelPointer(ch);
            for (int i = 0; i < block.getNumSamples(); ++i)
                d[i] = processSample(ch, d[i]);
        }
    }

    double magnitudeDb(double freq) const noexcept {
        return lowCut_.getCoefficients().magnitudeDb(freq, sampleRate_) +
               resonance_.getCoefficients().magnitudeDb(freq, sampleRate_) +
               lp1_.getCoefficients().magnitudeDb(freq, sampleRate_) + lp2_.getCoefficients().magnitudeDb(freq, sampleRate_);
    }

private:
    void updateCoefficients() noexcept {
        if (sampleRate_ <= 0.0)
            return;
        using dawlab::dsp::BiquadCoefficients;
        lowCut_.setCoefficients(BiquadCoefficients::makeHighpass(sampleRate_, lowCutHz_, kButterworthQ));
        resonance_.setCoefficients(BiquadCoefficients::makePeak(sampleRate_, resHz_, 1.5, resDb_));
        lp1_.setCoefficients(BiquadCoefficients::makeLowpass(sampleRate_, cutoffHz_, kButterworthQ));
        lp2_.setCoefficients(BiquadCoefficients::makeLowpass(sampleRate_, cutoffHz_, kButterworthQ));
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

    std::printf("%8s %12s %12s\n", "freq", "analytic", "measured");
    bool consistent = true;
    for (double f : {30.0, 70.0, 100.0, 200.0, 1000.0, 2250.0, 4500.0, 9000.0, 15000.0}) {
        cab.reset();
        const double analytic = cab.magnitudeDb(f);
        const double measured = dawlab::measureGainDb([&](float x) { return cab.processSample(0, x); }, f, kSampleRate);
        std::printf("%8.0f %12.2f %12.2f\n", f, analytic, measured);
        consistent = consistent && std::abs(analytic - measured) < 0.2;
    }
    DAWLAB_CHECK(consistent);
    DAWLAB_CHECK_NEAR(cab.magnitudeDb(4500.0), -6.02, 0.3);
    // 一個 octave 上去：類比 LR4 應為 -24.6 dB，但 bilinear transform 的 frequency warping 讓數位 filter
    // 在高頻掉得更快：等效的類比頻率比 = tan(π f/fs) / tan(π fc/fs)（9 kHz / 4.5 kHz @ 48 kHz → 2.20 而不是 2）
    {
        const double r = std::tan(dawlab::kPi * 9000.0 / kSampleRate) / std::tan(dawlab::kPi * 4500.0 / kSampleRate);
        const double expectedDb = -2.0 * 10.0 * std::log10(1.0 + r * r * r * r); // 兩顆 2 階 Butterworth
        std::printf("9 kHz：warped ratio %.3f → 預期 %.2f dB（未 warp 的類比值 -24.6 dB）\n", r, expectedDb);
        DAWLAB_CHECK_NEAR(cab.magnitudeDb(9000.0), expectedDb, 0.5);
    }
    DAWLAB_CHECK(cab.magnitudeDb(1000.0) > -0.5 && cab.magnitudeDb(1000.0) < 0.5);
    DAWLAB_CHECK(cab.magnitudeDb(100.0) > 2.5 && cab.magnitudeDb(100.0) < 4.5);
    DAWLAB_CHECK(cab.magnitudeDb(30.0) < -12.0);
    DAWLAB_CHECK(cab.magnitudeDb(15000.0) < -40.0);

    // 聽感用：白雜訊（white noise）經過 cab sim（聽起來像「對著音箱錄的雜訊」）
    {
        dawlab::AudioBuffer noise = dawlab::makeWhiteNoise(kSampleRate, 2.0, 0.25f, 2);
        CabSim cab2;
        cab2.prepare(kSampleRate, kBlockSize, 2);
        dawlab::AudioBlock whole(noise);
        for (int start = 0; start < noise.getNumSamples(); start += kBlockSize)
            cab2.process(whole.getSubBlock(start, std::min(kBlockSize, noise.getNumSamples() - start)));
        dawlab::writeWav(dawlab::outputPath("l07_ex4_cab_noise.wav"), noise, kSampleRate);
        std::printf("已輸出 dawlab_output/l07_ex4_cab_noise.wav\n");
    }

    return dawlab::test::summary("l07_ex4");
}
