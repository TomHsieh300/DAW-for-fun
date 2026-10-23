// 練習 4 解答：用 HPF + LPF 串接組成 bandpass，並與 RBJ 的二階 bandpass 比較。
//
// 把一個 Butterworth highpass（f_low）和一個 Butterworth lowpass（f_high）串起來，
// 就得到一個「兩邊都是 12 dB/oct」的帶通：H(z) = H_hp(z) · H_lp(z)，dB 直接相加。
// 和 RBJ 的二階 bandpass（兩邊各 6 dB/oct、只有一個 fc 與 Q）相比：
//   - 串接版可以獨立設定上下邊界，斜率更陡（遠離通帶時衰減快一倍）。
//   - 通帶中央不是精準 0 dB：兩個 filter 在對方的 cutoff 附近都還有一點衰減，
//     f_low 與 f_high 距離越近，中央凹得越多（本題 500 Hz ~ 2 kHz，中央 1 kHz 約 -0.5 dB）。
#include <dawlab/Common.h>
#include <dawlab/dsp/Biquad.h>

#include <cstdio>

using namespace dawlab;
using dawlab::dsp::Biquad;
using dawlab::dsp::BiquadCoefficients;

constexpr double kSampleRate = 48000.0;

class BandpassCascade {
public:
    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        sampleRate_ = sampleRate;
        hp_.prepare(sampleRate, maxBlockSize, numChannels);
        lp_.prepare(sampleRate, maxBlockSize, numChannels);
        setBand(lowHz_, highHz_);
    }
    void reset() noexcept {
        hp_.reset();
        lp_.reset();
    }
    // f_low 以下、f_high 以上各以 12 dB/oct 衰減（兩個 Butterworth 二階）
    void setBand(float lowHz, float highHz) noexcept {
        lowHz_ = lowHz;
        highHz_ = highHz;
        hp_.setCoefficients(BiquadCoefficients::makeHighpass(sampleRate_, lowHz, BiquadCoefficients::kButterworthQ));
        lp_.setCoefficients(BiquadCoefficients::makeLowpass(sampleRate_, highHz, BiquadCoefficients::kButterworthQ));
    }
    float processSample(int ch, float x) noexcept { return lp_.processSample(ch, hp_.processSample(ch, x)); }
    void process(AudioBlock block) noexcept {
        hp_.process(block);
        lp_.process(block);
    }
    // 串接 = 響應相乘 = dB 相加
    double magnitudeDb(double f) const {
        return hp_.getCoefficients().magnitudeDb(f, sampleRate_) + lp_.getCoefficients().magnitudeDb(f, sampleRate_);
    }

private:
    double sampleRate_ = 48000.0;
    float lowHz_ = 500.0f, highHz_ = 2000.0f;
    Biquad hp_, lp_;
};

int main() {
    const double fLow = 500.0, fHigh = 2000.0;
    BandpassCascade bp;
    bp.prepare(kSampleRate, 512, 1);
    bp.setBand(static_cast<float>(fLow), static_cast<float>(fHigh));

    // 對照組：RBJ bandpass，中心 = 幾何平均 sqrt(500·2000) = 1000 Hz，Q = fc / BW = 1000 / 1500
    const double fc = std::sqrt(fLow * fHigh);
    const double q = fc / (fHigh - fLow);
    const BiquadCoefficients rbj = BiquadCoefficients::makeBandpass(kSampleRate, fc, q);
    std::printf("HPF %.0f Hz + LPF %.0f Hz（各 Q=0.707） vs RBJ bandpass fc=%.0f Hz, Q=%.3f\n\n", fLow, fHigh, fc, q);

    std::printf("%8s  %12s  %12s  %12s\n", "f (Hz)", "cascade dB", "measured", "RBJ BPF dB");
    for (double f : {62.5, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0}) {
        bp.reset();
        const double meas = measureGainDb([&](float x) { return bp.processSample(0, x); }, f, kSampleRate, 0.5, 0.5);
        std::printf("%8.1f  %12.2f  %12.2f  %12.2f\n", f, bp.magnitudeDb(f), meas, rbj.magnitudeDb(f, kSampleRate));
        DAWLAB_CHECK_NEAR(meas, bp.magnitudeDb(f), 0.1);
    }

    // 1. 兩個邊界各約 -3 dB（另一個 filter 在這裡只貢獻 -0.02 dB：-10 log10(1 + 0.25^4)）
    const double otherSide = -10.0 * std::log10(1.0 + std::pow(fLow / fHigh, 4.0));
    std::printf("\n另一個 filter 在邊界處的衰減 = %.3f dB\n", otherSide);
    DAWLAB_CHECK_NEAR(bp.magnitudeDb(fLow), -3.01 + otherSide, 0.05);
    DAWLAB_CHECK_NEAR(bp.magnitudeDb(fHigh), -3.01 + otherSide, 0.05);

    // 2. 通帶中央（1 kHz）：HPF 與 LPF 各掉 -10 log10(1 + 0.5^4) = -0.26 dB，合計約 -0.53 dB
    const double centreTheory = -20.0 * std::log10(1.0 + std::pow(0.5, 4.0));
    std::printf("通帶中央 %.0f Hz：%.2f dB（analog 理論 %.2f dB）；RBJ bandpass 在 fc 永遠 0 dB\n", fc,
                bp.magnitudeDb(fc), centreTheory);
    DAWLAB_CHECK_NEAR(bp.magnitudeDb(fc), centreTheory, 0.05);
    DAWLAB_CHECK_NEAR(rbj.magnitudeDb(fc, kSampleRate), 0.0, 0.01);

    // 3. 斜率：離邊界 2 個 octave（125 Hz、8 kHz），串接版 ≈ -24 dB（12 dB/oct）。
    //    RBJ 二階 bandpass 的 analog 原型 |H| = 1 / sqrt(1 + Q² (f/fc - fc/f)²)，
    //    在 fc/8 與 8 fc 處 = -14.55 dB（遠離 fc 後每 octave 只掉 6 dB）。
    //    8 kHz 那一側兩者都因為 cramping 比 analog 再陡 1~2 dB。
    const double cascadeLow = bp.magnitudeDb(fLow / 4.0), cascadeHigh = bp.magnitudeDb(fHigh * 4.0);
    const double rbjLow = rbj.magnitudeDb(fLow / 4.0, kSampleRate), rbjHigh = rbj.magnitudeDb(fHigh * 4.0, kSampleRate);
    const double rbjAnalog = -10.0 * std::log10(1.0 + q * q * std::pow(8.0 - 1.0 / 8.0, 2.0));
    std::printf("邊界外 2 octaves：串接 %.1f / %.1f dB，RBJ %.1f / %.1f dB（RBJ analog 理論 %.2f dB）\n", cascadeLow,
                cascadeHigh, rbjLow, rbjHigh, rbjAnalog);
    DAWLAB_CHECK_NEAR(cascadeLow, -24.1, 0.5);
    DAWLAB_CHECK_NEAR(cascadeHigh, -24.1, 2.0); // 8 kHz 已經受 cramping 影響（約 -25.7 dB）
    DAWLAB_CHECK_NEAR(rbjLow, rbjAnalog, 0.2);
    DAWLAB_CHECK_NEAR(rbjHigh, rbjAnalog, 1.5);
    DAWLAB_CHECK(rbjLow > cascadeLow + 6.0 && rbjHigh > cascadeHigh + 6.0); // 串接版明顯陡得多

    // 聽一下：白雜訊過兩種 bandpass
    AudioBuffer noise = makeWhiteNoise(kSampleRate, 2.0, 0.25f, 2);
    AudioBuffer outCascade = noise, outRbj = noise;
    BandpassCascade bp2;
    bp2.prepare(kSampleRate, 512, 2);
    bp2.setBand(static_cast<float>(fLow), static_cast<float>(fHigh));
    Biquad bq;
    bq.prepare(kSampleRate, 512, 2);
    bq.setCoefficients(rbj);
    AudioBlock wc(outCascade), wr(outRbj);
    for (int start = 0; start < noise.getNumSamples(); start += 512) {
        const int len = std::min(512, noise.getNumSamples() - start);
        bp2.process(wc.getSubBlock(start, len));
        bq.process(wr.getSubBlock(start, len));
    }
    writeWav(outputPath("l02_ex4_noise_cascade.wav"), outCascade, kSampleRate);
    writeWav(outputPath("l02_ex4_noise_rbj_bpf.wav"), outRbj, kSampleRate);
    std::printf("雜訊 RMS：dry %.2f dB → 串接 %.2f dB，RBJ %.2f dB（斜率陡的版本留下更少能量）\n",
                gainToDb(rms(noise, 0)), gainToDb(rms(outCascade, 0)), gainToDb(rms(outRbj, 0)));
    DAWLAB_CHECK(rms(outCascade, 0) < rms(outRbj, 0));
    DAWLAB_CHECK(!hasNanOrInf(outCascade) && !hasNanOrInf(outRbj));

    return dawlab::test::summary("l02_ex4");
}
