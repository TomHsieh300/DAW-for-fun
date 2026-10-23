// 練習 5（起始碼）：Tilt EQ
//
// 目標：用一顆 low shelf + 一顆 high shelf（都在 pivot 頻率、Q = 0.707）做 tilt EQ：
//   - setTiltDb(t)：低架 gain = −t/2、高架 gain = +t/2 → pivot 處 0 dB、兩端 ∓t/2。
//   - getMagnitudeDb：兩顆的 dB 相加。
//   - 加分：tilt 參數用 SmoothedValue 平滑，避免 click。
// 執行時 self-check 會印出 FAIL，直到你把 TODO 補完。

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
        updateCoefficients();
        reset();
    }

    void reset() noexcept {
        low_.reset();
        high_.reset();
    }

    void setPivotHz(float hz) noexcept {
        pivotHz_ = hz;
        updateCoefficients();
    }
    void setTiltDb(float tiltDb) noexcept {
        tiltDb_ = std::clamp(tiltDb, -12.0f, 12.0f);
        updateCoefficients();
    }

    double getMagnitudeDb(double freq) const noexcept {
        (void)freq;
        return 0.0; // TODO: low_ 與 high_ 的 magnitudeDb 相加
    }

    void process(AudioBlock block) noexcept {
        low_.process(block);
        high_.process(block);
    }

    float processSample(int ch, float x) noexcept { return high_.processSample(ch, low_.processSample(ch, x)); }

private:
    void updateCoefficients() noexcept {
        // TODO: low_ = makeLowShelf(sr, pivot, kShelfQ, -tilt/2)；high_ = makeHighShelf(sr, pivot, kShelfQ, +tilt/2)
    }

    double sampleRate_ = 48000.0;
    float pivotHz_ = 1000.0f;
    float tiltDb_ = 0.0f;
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

    std::printf("  pivot 1 kHz, tilt +6 dB：\n");
    for (double f : {30.0, 250.0, 1000.0, 4000.0, 16000.0}) {
        tilt.reset();
        const double m = measureGainDb([&](float x) { return tilt.processSample(0, x); }, f, sr);
        std::printf("    %7.0f    %+6.2f     %+6.2f\n", f, tilt.getMagnitudeDb(f), m);
        DAWLAB_CHECK_NEAR(m, tilt.getMagnitudeDb(f), 0.05);
    }
    DAWLAB_CHECK_NEAR(tilt.getMagnitudeDb(1000.0), 0.0, 0.01);
    DAWLAB_CHECK_NEAR(tilt.getMagnitudeDb(30.0), -3.0, 0.1);
    DAWLAB_CHECK_NEAR(tilt.getMagnitudeDb(16000.0), 3.0, 0.1);
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
    dark.prepare(sr, 512, 1);
    dark.setTiltDb(-6.0f);
    for (double f : {100.0, 1000.0, 5000.0})
        DAWLAB_CHECK_NEAR(dark.getMagnitudeDb(f), -tilt.getMagnitudeDb(f), 1e-6);

    // TODO（加分）：
    //   1. setTiltDb() 改成只設定 SmoothedValue 的目標（20 ms ramp），process() 每 32 個 sample
    //      skip(32) 並用目前值重算一次係數（跟 ParametricEq 同一招）。
    //   2. 試聽：pluck sequence（makePluckSequence(sr, 4.0)）以 512 sample 為一個 block 處理，
    //      前 2 秒 tilt +6 dB、2 秒處切到 −6 dB（不可以 click），
    //      writeWav(outputPath("l03_ex5_tilt.wav"), wet, sr)。

    return dawlab::test::summary("l03_ex5_starter");
}
