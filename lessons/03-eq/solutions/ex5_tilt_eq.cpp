// 練習 5（解答）：Tilt EQ
//
// Tilt EQ 只有兩個旋鈕：pivot（樞軸頻率）與 tilt（dB）。曲線像翹翹板：pivot 處 0 dB，
// 往低頻一端漸漸變成 −tilt/2，往高頻一端變成 +tilt/2（tilt > 0 = 更亮；tilt < 0 = 更暗）。
// 用兩顆 RBJ shelf 就能做：
//   low shelf  @ pivot，gain = −tilt/2
//   high shelf @ pivot，gain = +tilt/2
// 為什麼 pivot 剛好是 0 dB？RBJ shelf 的 f0 是「中點頻率」：|H(f0)| = A = 10^(gainDb/40)，
// 也就是 gainDb / 2（README 2.6 節證明）。低架在 pivot 貢獻 −tilt/4、高架貢獻 +tilt/4，相加 = 0。
// 兩顆的 Q 都用 0.707（S = 1）：最陡但不過衝，所以整條曲線單調、沒有駝峰。

#include <dawlab/Common.h>
#include <dawlab/dsp/Biquad.h>
#include <dawlab/dsp/SmoothedValue.h>

#include <cstdio>

using namespace dawlab;
using namespace dawlab::dsp;

namespace {

class TiltEq {
public:
    static constexpr double kShelfQ = 0.70710678118654752;

    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        sampleRate_ = sampleRate;
        low_.prepare(sampleRate, maxBlockSize, numChannels);
        high_.prepare(sampleRate, maxBlockSize, numChannels);
        tiltSmoother_.reset(sampleRate, 0.02);
        tiltSmoother_.setCurrentAndTargetValue(tiltDb_);
        updateCoefficients(tiltDb_);
        reset();
    }

    void reset() noexcept {
        low_.reset();
        high_.reset();
    }

    void setPivotHz(float hz) noexcept {
        pivotHz_ = hz;
        updateCoefficients(tiltSmoother_.getCurrentValue());
    }
    // tilt：正 = 高頻多、低頻少（更亮）；負 = 相反
    void setTiltDb(float tiltDb) noexcept {
        tiltDb_ = std::clamp(tiltDb, -12.0f, 12.0f);
        tiltSmoother_.setTargetValue(tiltDb_);
    }

    double getMagnitudeDb(double freq) const noexcept {
        return low_.getCoefficients().magnitudeDb(freq, sampleRate_) + high_.getCoefficients().magnitudeDb(freq, sampleRate_);
    }

    // 參數平滑：tilt 以 20 ms linear ramp 走到目標，每 32 個 sample 重算一次係數（跟 ParametricEq 同一招）
    void process(AudioBlock block) noexcept {
        const int n = block.getNumSamples();
        for (int start = 0; start < n; start += kUpdateInterval) {
            const int len = std::min(kUpdateInterval, n - start);
            if (tiltSmoother_.isSmoothing()) {
                tiltSmoother_.skip(len);
                updateCoefficients(tiltSmoother_.getCurrentValue());
            }
            AudioBlock sub = block.getSubBlock(start, len);
            low_.process(sub);
            high_.process(sub);
        }
    }

    float processSample(int ch, float x) noexcept { return high_.processSample(ch, low_.processSample(ch, x)); }

private:
    static constexpr int kUpdateInterval = 32;

    void updateCoefficients(float tiltDb) noexcept {
        low_.setCoefficients(BiquadCoefficients::makeLowShelf(sampleRate_, pivotHz_, kShelfQ, -0.5 * tiltDb));
        high_.setCoefficients(BiquadCoefficients::makeHighShelf(sampleRate_, pivotHz_, kShelfQ, 0.5 * tiltDb));
    }

    double sampleRate_ = 48000.0;
    float pivotHz_ = 1000.0f;
    float tiltDb_ = 0.0f;
    SmoothedValue tiltSmoother_;
    Biquad low_, high_;
};

} // namespace

int main() {
    constexpr double sr = 48000.0;
    std::printf("=== 第 03 章 練習 5：Tilt EQ ===\n\n");

    TiltEq tilt;
    tilt.prepare(sr, 512, 1);
    tilt.setPivotHz(1000.0f);
    tilt.setTiltDb(6.0f);
    tilt.prepare(sr, 512, 1); // 重新 prepare：平滑器直接對齊，量測不用等 ramp

    std::printf("  pivot 1 kHz, tilt +6 dB：\n");
    std::printf("    freq(Hz)   理論(dB)   實測(dB)\n");
    for (double f : {30.0, 60.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0}) {
        tilt.reset();
        const double m = measureGainDb([&](float x) { return tilt.processSample(0, x); }, f, sr);
        std::printf("    %7.0f    %+6.2f     %+6.2f\n", f, tilt.getMagnitudeDb(f), m);
        DAWLAB_CHECK_NEAR(m, tilt.getMagnitudeDb(f), 0.05);
    }
    DAWLAB_CHECK_NEAR(tilt.getMagnitudeDb(1000.0), 0.0, 0.01);   // pivot 恰為 0 dB（兩顆 shelf 的中點互相抵消）
    DAWLAB_CHECK_NEAR(tilt.getMagnitudeDb(30.0), -3.0, 0.1);     // 低頻端 → −tilt/2
    DAWLAB_CHECK_NEAR(tilt.getMagnitudeDb(16000.0), 3.0, 0.1);   // 高頻端 → +tilt/2
    DAWLAB_CHECK_NEAR(tilt.getMagnitudeDb(2000.0), -tilt.getMagnitudeDb(500.0), 0.1); // 以 pivot 為中心反對稱（1 octave）
    DAWLAB_CHECK_NEAR(tilt.getMagnitudeDb(4000.0), -tilt.getMagnitudeDb(250.0), 0.15); // 2 octave（高頻端有一點 warp）
    // 單調：從低到高每一步都要上升（沒有駝峰）
    bool monotonic = true;
    double prev = tilt.getMagnitudeDb(20.0);
    for (double f = 25.0; f <= 20000.0; f *= 1.05) {
        const double db = tilt.getMagnitudeDb(f);
        if (db < prev - 1e-6)
            monotonic = false;
        prev = db;
    }
    DAWLAB_CHECK(monotonic);

    // 負 tilt 是正 tilt 的鏡像：H(-t) = 1 / H(+t)
    TiltEq dark;
    dark.setTiltDb(-6.0f);
    dark.prepare(sr, 512, 1);
    for (double f : {100.0, 1000.0, 5000.0})
        DAWLAB_CHECK_NEAR(dark.getMagnitudeDb(f), -tilt.getMagnitudeDb(f), 1e-6);

    // 試聽：pluck sequence 套 +6 dB（亮）與 −6 dB（暗），用 block 處理且中途改 tilt（平滑、沒有 click）
    AudioBuffer dry = makePluckSequence(sr, 4.0);
    AudioBuffer wet = dry;
    TiltEq live;
    live.prepare(sr, 512, wet.getNumChannels());
    live.setTiltDb(6.0f);
    live.prepare(sr, 512, wet.getNumChannels());
    AudioBlock whole(wet);
    for (int start = 0; start < wet.getNumSamples(); start += 512) {
        if (start <= secondsToSamples(2.0, sr) && secondsToSamples(2.0, sr) < start + 512) // 包含該時間點的那個 block
            live.setTiltDb(-6.0f); // 2 秒處從「亮」切到「暗」
        live.process(whole.getSubBlock(start, std::min(512, wet.getNumSamples() - start)));
    }
    DAWLAB_CHECK(!hasNanOrInf(wet));
    writeWav(outputPath("l03_ex5_tilt.wav"), wet, sr);
    std::printf("\n  pluck sequence：前 2 秒 tilt +6 dB，後 2 秒 −6 dB -> dawlab_output/l03_ex5_tilt.wav\n");

    return dawlab::test::summary("l03_ex5");
}
