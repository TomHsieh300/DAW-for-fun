// 練習 3 解答：用兩級 biquad 串接成 4 階 Butterworth lowpass（24 dB/oct）。
//
// N 階 Butterworth 的 |H(jΩ)|² = 1 / (1 + (Ω/Ωc)^(2N))：通帶最平坦，cutoff 處永遠 -3 dB。
// 拆成二階段時每一級的 Q 來自 pole 的角度：N 為偶數時 Q_k = 1 / (2 cos((2k+1)π/(2N)))，
// N = 4 → Q = 0.5412（k=0）與 1.3066（k=1）。兩級都用同一個 fc。
// （N 為奇數時角度改成 (k+1)π/N，另加一級 one-pole；最後也順便驗證 3 階的情況。）
//
// 注意：兩級都用 Q = 0.707 串起來「不是」Butterworth——cutoff 處會變成 -6 dB，通帶也不最平坦。
#include <dawlab/Common.h>
#include <dawlab/dsp/Biquad.h>

#include <cstdio>

using namespace dawlab;
using dawlab::dsp::Biquad;
using dawlab::dsp::BiquadCoefficients;
using dawlab::dsp::butterworthQ;

constexpr double kSampleRate = 48000.0;

// 兩級 biquad 串接
class Butterworth4Lowpass {
public:
    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        sampleRate_ = sampleRate;
        for (Biquad& s : stages_)
            s.prepare(sampleRate, maxBlockSize, numChannels);
        setCutoff(cutoff_);
    }
    void reset() noexcept {
        for (Biquad& s : stages_)
            s.reset();
    }
    void setCutoff(float hz) noexcept {
        cutoff_ = hz;
        for (int k = 0; k < 2; ++k)
            stages_[k].setCoefficients(BiquadCoefficients::makeLowpass(sampleRate_, hz, butterworthQ(4, k)));
    }
    float processSample(int ch, float x) noexcept {
        return stages_[1].processSample(ch, stages_[0].processSample(ch, x));
    }
    void process(AudioBlock block) noexcept {
        for (Biquad& s : stages_)
            s.process(block);
    }
    // 整體響應 = 各級響應相乘（dB 相加）
    double magnitudeDb(double f) const {
        return stages_[0].getCoefficients().magnitudeDb(f, sampleRate_) +
               stages_[1].getCoefficients().magnitudeDb(f, sampleRate_);
    }

private:
    double sampleRate_ = 48000.0;
    float cutoff_ = 1000.0f;
    Biquad stages_[2];
};

int main() {
    std::printf("4 階 Butterworth 的 Q：%.4f, %.4f\n", butterworthQ(4, 0), butterworthQ(4, 1));
    DAWLAB_CHECK_NEAR(butterworthQ(4, 0), 0.5412, 1e-4);
    DAWLAB_CHECK_NEAR(butterworthQ(4, 1), 1.3066, 1e-4);
    DAWLAB_CHECK_NEAR(butterworthQ(2, 0), 0.70711, 1e-4);

    // 奇數階：pole 角度（從負實軸量起）是 kπ/N，不是 (2k+1)π/(2N)。
    // 3 階 = 一級 one-pole（φ = 0 的實數 pole）+ 一級 Q = 1 的 biquad（φ = π/3）；
    // 5 階 = one-pole + Q = 0.618、1.618 兩級。
    std::printf("奇數階的 Q：N=3 → %.4f；N=5 → %.4f, %.4f\n", butterworthQ(3, 0), butterworthQ(5, 0),
                butterworthQ(5, 1));
    DAWLAB_CHECK_NEAR(butterworthQ(3, 0), 1.0, 1e-9);
    DAWLAB_CHECK_NEAR(butterworthQ(5, 0), 0.6180, 1e-4);
    DAWLAB_CHECK_NEAR(butterworthQ(5, 1), 1.6180, 1e-4);
    {
        // 驗證：bilinear one-pole（-3 dB 精準在 fc）串接 makeLowpass(fc, butterworthQ(3, 0)) = 3 階 Butterworth
        const double fc3 = 1000.0;
        const double g = std::tan(kPi * fc3 / kSampleRate);
        const BiquadCoefficients bq3 = BiquadCoefficients::makeLowpass(kSampleRate, fc3, butterworthQ(3, 0));
        const auto order3Db = [&](double f) {
            const std::complex<double> z1 = std::polar(1.0, -kTwoPi * f / kSampleRate);
            const double onePoleDb = gainToDb(std::abs(g * (1.0 + z1) / ((1.0 + g) - (1.0 - g) * z1)));
            return onePoleDb + bq3.magnitudeDb(f, kSampleRate);
        };
        const double analog500 = -10.0 * std::log10(1.0 + std::pow(500.0 / fc3, 6.0)); // |H|² = 1/(1 + (f/fc)^6)
        std::printf("3 階 Butterworth（one-pole + Q=1 biquad）：fc 處 %.2f dB，500 Hz 處 %.3f dB（analog %.3f dB）\n",
                    order3Db(fc3), order3Db(500.0), analog500);
        DAWLAB_CHECK_NEAR(order3Db(fc3), -3.01, 0.01);
        DAWLAB_CHECK_NEAR(order3Db(500.0), analog500, 0.01);
    }

    const double fc = 1000.0;
    Butterworth4Lowpass lp;
    lp.prepare(kSampleRate, 512, 1);
    lp.setCutoff(static_cast<float>(fc));

    // 對照組：單一 biquad（12 dB/oct）、兩級 Q=0.707（錯誤的串法）
    const BiquadCoefficients single = BiquadCoefficients::makeLowpass(kSampleRate, fc, BiquadCoefficients::kButterworthQ);

    std::printf("\n%8s  %12s  %12s  %12s  %14s\n", "f (Hz)", "analog BW4", "2-stage dB", "measured", "single biquad");
    for (double f : {100.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0}) {
        // analog 4 階 Butterworth：-10 log10(1 + (f/fc)^8)
        const double analog = -10.0 * std::log10(1.0 + std::pow(f / fc, 8.0));
        lp.reset();
        const double meas = measureGainDb([&](float x) { return lp.processSample(0, x); }, f, kSampleRate, 0.5, 0.5);
        std::printf("%8.0f  %12.2f  %12.2f  %12.2f  %14.2f\n", f, analog, lp.magnitudeDb(f), meas,
                    single.magnitudeDb(f, kSampleRate));
        DAWLAB_CHECK_NEAR(meas, lp.magnitudeDb(f), 0.1);
        // 遠離 Nyquist 時 digital ≈ analog；4 kHz（fs/12）已經開始 cramping，digital 比 analog 陡約 0.8 dB
        if (f <= 2000.0)
            DAWLAB_CHECK_NEAR(lp.magnitudeDb(f), analog, 0.3);
        else if (f <= 4000.0)
            DAWLAB_CHECK_NEAR(lp.magnitudeDb(f), analog, 1.0);
    }

    // 關鍵數字：cutoff -3.01 dB；1 octave 外 ≈ -24.1 dB（-10 log10(1 + 2^8) = -24.10）；
    // 2 octaves 外 analog 是 -48.2 dB，digital 因 cramping 約 -48.9 dB
    DAWLAB_CHECK_NEAR(lp.magnitudeDb(fc), -3.01, 0.05);
    DAWLAB_CHECK_NEAR(lp.magnitudeDb(2.0 * fc), -24.1, 0.3);
    DAWLAB_CHECK_NEAR(lp.magnitudeDb(4.0 * fc), -48.2, 1.0);
    // 通帶最平坦：500 Hz 處只掉 0.017 dB
    DAWLAB_CHECK_NEAR(lp.magnitudeDb(500.0), -0.017, 0.02);

    // 錯誤示範：兩級 Q = 0.707 串接，cutoff 處是 -6 dB
    const double wrong = 2.0 * single.magnitudeDb(fc, kSampleRate);
    std::printf("\n兩級都用 Q=0.707 串接時，cutoff 處 = %.2f dB（不是 Butterworth）\n", wrong);
    DAWLAB_CHECK_NEAR(wrong, -6.02, 0.1);

    // 聽一下：白雜訊過 4 階 vs 2 階
    AudioBuffer noise = makeWhiteNoise(kSampleRate, 2.0, 0.25f, 2);
    AudioBuffer out4 = noise, out2 = noise;
    Butterworth4Lowpass lp2;
    lp2.prepare(kSampleRate, 512, 2);
    lp2.setCutoff(static_cast<float>(fc));
    Biquad bq2;
    bq2.prepare(kSampleRate, 512, 2);
    bq2.setCoefficients(single);
    AudioBlock w4(out4), w2(out2);
    for (int start = 0; start < noise.getNumSamples(); start += 512) {
        const int len = std::min(512, noise.getNumSamples() - start);
        lp2.process(w4.getSubBlock(start, len));
        bq2.process(w2.getSubBlock(start, len));
    }
    writeWav(outputPath("l02_ex3_noise_butterworth4.wav"), out4, kSampleRate);
    writeWav(outputPath("l02_ex3_noise_biquad2.wav"), out2, kSampleRate);
    std::printf("雜訊 RMS：dry %.2f dB → 2 階 %.2f dB → 4 階 %.2f dB（更陡 = 留下更少高頻能量）\n",
                gainToDb(rms(noise, 0)), gainToDb(rms(out2, 0)), gainToDb(rms(out4, 0)));
    DAWLAB_CHECK(rms(out4, 0) < rms(out2, 0));

    return dawlab::test::summary("l02_ex3");
}
