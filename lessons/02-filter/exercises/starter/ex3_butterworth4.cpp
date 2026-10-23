// 練習 3：兩級 biquad 串接成 4 階 Butterworth lowpass（起始碼）。
//
// 完成 TODO：
//   1. 算出 4 階 Butterworth 兩級的 Q（提示：N 為偶數時 Q_k = 1 / (2 cos((2k+1)π/(2N)))，或用 dsp::butterworthQ）。
//   2. 在 setCutoff() 裡用同一個 fc、不同的 Q 設定兩級。
//   3. processSample() 把兩級串起來；magnitudeDb() 把兩級的 dB 相加。
// 驗證：cutoff 處 -3.01 dB、2 fc 處約 -24.1 dB。
#include <dawlab/Common.h>
#include <dawlab/dsp/Biquad.h>

#include <cstdio>

using namespace dawlab;
using dawlab::dsp::Biquad;
using dawlab::dsp::BiquadCoefficients;

constexpr double kSampleRate = 48000.0;

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
        // TODO: 兩級 Q 值不同（0.5412 與 1.3066），這裡暫時都用 0.707——這是錯的，請修正
        for (Biquad& s : stages_)
            s.setCoefficients(BiquadCoefficients::makeLowpass(sampleRate_, hz, 0.7071));
    }
    float processSample(int ch, float x) noexcept {
        // TODO: 串接兩級
        return stages_[0].processSample(ch, x);
    }
    void process(AudioBlock block) noexcept {
        for (Biquad& s : stages_)
            s.process(block);
    }
    double magnitudeDb(double f) const {
        // TODO: 回傳兩級 dB 相加
        return stages_[0].getCoefficients().magnitudeDb(f, sampleRate_);
    }

private:
    double sampleRate_ = 48000.0;
    float cutoff_ = 1000.0f;
    Biquad stages_[2];
};

int main() {
    const double fc = 1000.0;
    Butterworth4Lowpass lp;
    lp.prepare(kSampleRate, 512, 1);
    lp.setCutoff(static_cast<float>(fc));

    std::printf("%8s  %12s  %12s  %12s\n", "f (Hz)", "analog BW4", "2-stage dB", "measured");
    for (double f : {100.0, 500.0, 1000.0, 2000.0, 4000.0}) {
        const double analog = -10.0 * std::log10(1.0 + std::pow(f / fc, 8.0));
        lp.reset();
        const double meas = measureGainDb([&](float x) { return lp.processSample(0, x); }, f, kSampleRate, 0.5, 0.5);
        std::printf("%8.0f  %12.2f  %12.2f  %12.2f\n", f, analog, lp.magnitudeDb(f), meas);
        DAWLAB_CHECK_NEAR(meas, lp.magnitudeDb(f), 0.1);
        // 4 kHz 已經開始 cramping（digital 比 analog 陡約 0.8 dB），所以容許誤差放寬
        DAWLAB_CHECK_NEAR(lp.magnitudeDb(f), analog, f <= 2000.0 ? 0.3 : 1.0);
    }
    DAWLAB_CHECK_NEAR(lp.magnitudeDb(fc), -3.01, 0.05);
    DAWLAB_CHECK_NEAR(lp.magnitudeDb(2.0 * fc), -24.1, 0.3);

    return dawlab::test::summary("l02_ex3_starter");
}
