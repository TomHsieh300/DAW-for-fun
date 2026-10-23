// 練習 5 起始碼：upward expander —— 把 threshold「以上」的訊號推得更大
//
// 要做的事：
//   1. 完成 UpwardExpander::computeOutputDb()（含 soft knee，公式見 exercises/README.md）
//   2. 完成 nextGain()：envelope → dB → 靜態曲線 → boost（上限 range）→ 線性 gain
// self-check 已經寫好；填完 TODO 後執行 l08_ex5_starter 應該印出 PASS。
// 提示：解答在 solutions/ex5_upward_expander.cpp。

#include <dawlab/Common.h>
#include <dawlab/dsp/EnvelopeFollower.h>

#include <cstdio>
#include <vector>

using namespace dawlab;
using dawlab::dsp::DetectorMode;
using dawlab::dsp::SmoothingMode;
using dawlab::dsp::EnvelopeFollower;

namespace {

constexpr int kBlockSize = 512;
constexpr double kSampleRate = 48000.0;

class UpwardExpander {
public:
    // 回傳輸出位準（dB）。ratio ≥ 1；kneeDb ≤ 0 視為 hard knee。
    static float computeOutputDb(float inputDb, float thresholdDb, float ratio, float kneeDb) noexcept {
        // TODO：
        //   x ≤ T − W/2            → y = x
        //   T − W/2 < x < T + W/2  → y = x + (R − 1)(x − T + W/2)² / (2W)
        //   x ≥ T + W/2            → y = T + R (x − T)
        (void)thresholdDb;
        (void)ratio;
        (void)kneeDb;
        return inputDb;
    }

    void prepare(double sampleRate, int /*maxBlockSize*/, int /*numChannels*/) {
        detector_.prepare(sampleRate);
        detector_.setMode(DetectorMode::Peak);
        // Decoupled（peak-hold + attack 平滑）：對正弦波不會因 release 期間的漣漪而低估 peak，
        // 穩態 level 才會等於真正的 peak（Branching 在 1 kHz、attack 5 ms 時約低估 1 dB）
        detector_.setSmoothingMode(SmoothingMode::Decoupled);
        detector_.setAttackMs(attackMs_);
        detector_.setReleaseMs(releaseMs_);
        reset();
    }
    void reset() noexcept {
        detector_.reset();
        gainDb_ = 0.0f;
        gain_ = 1.0f;
    }
    void setThresholdDb(float db) noexcept { thresholdDb_ = db; }
    void setRatio(float r) noexcept { ratio_ = std::max(1.0f, r); }
    void setKneeDb(float db) noexcept { kneeDb_ = std::max(0.0f, db); }
    void setRangeDb(float db) noexcept { rangeDb_ = std::abs(db); } // 最大 boost（正值）
    void setAttackMs(float ms) noexcept {
        attackMs_ = ms;
        detector_.setAttackMs(ms);
    }
    void setReleaseMs(float ms) noexcept {
        releaseMs_ = ms;
        detector_.setReleaseMs(ms);
    }

    float nextGain(float detectorInput) noexcept {
        // TODO：
        //   levelDb = gainToDb(detector_.processSample(detectorInput))
        //   boost   = min(rangeDb_, computeOutputDb(levelDb, ...) − levelDb)
        //   gain_   = dbToGain(boost)（只有 boost 改變時才算 pow）
        (void)detectorInput;
        return gain_;
    }

    void process(AudioBlock block) noexcept {
        const int n = block.getNumSamples();
        const int nch = block.getNumChannels();
        for (int i = 0; i < n; ++i) {
            float det = 0.0f;
            for (int ch = 0; ch < nch; ++ch)
                det = std::max(det, std::abs(block.getChannelPointer(ch)[i]));
            const float g = nextGain(det);
            for (int ch = 0; ch < nch; ++ch)
                block.getChannelPointer(ch)[i] *= g;
        }
    }

private:
    EnvelopeFollower detector_;
    float thresholdDb_ = -30.0f;
    float ratio_ = 1.5f;
    float kneeDb_ = 0.0f;
    float rangeDb_ = 12.0f;
    float attackMs_ = 5.0f;
    float releaseMs_ = 50.0f;
    float gainDb_ = 0.0f;
    float gain_ = 1.0f;
};

double segmentLevelDb(const AudioBuffer& b, int segment, int segLen, double freq) {
    const int start = segment * segLen + segLen / 2;
    return gainToDb(goertzelAmplitude(b.getReadPointer(0) + start, segLen / 2, freq, kSampleRate));
}

} // namespace

int main() {
    std::printf("第 08 章 練習 5（起始碼）：upward expander\n");

    const float T = -30.0f, R = 1.5f, W = 10.0f;
    const float lo = T - W / 2, hi = T + W / 2;
    DAWLAB_CHECK_NEAR(UpwardExpander::computeOutputDb(lo, T, R, W), lo, 1.0e-4);
    DAWLAB_CHECK_NEAR(UpwardExpander::computeOutputDb(hi, T, R, W), T + R * (hi - T), 1.0e-4);
    const float eps = 0.01f;
    const float slopeLo = (UpwardExpander::computeOutputDb(lo + eps, T, R, W) - UpwardExpander::computeOutputDb(lo, T, R, W)) / eps;
    const float slopeHi = (UpwardExpander::computeOutputDb(hi, T, R, W) - UpwardExpander::computeOutputDb(hi - eps, T, R, W)) / eps;
    DAWLAB_CHECK_NEAR(slopeLo, 1.0, 0.02);
    DAWLAB_CHECK_NEAR(slopeHi, R, 0.02);
    DAWLAB_CHECK_NEAR(UpwardExpander::computeOutputDb(-20.0f, T, R, 0.0f), -15.0, 1.0e-5);
    DAWLAB_CHECK_NEAR(UpwardExpander::computeOutputDb(-10.0f, T, R, 0.0f), 0.0, 1.0e-5);

    const std::vector<float> levels = {-30.0f, -20.0f, -10.0f, -40.0f};
    const double segSec = 0.5;
    const AudioBuffer input = makeToneBursts(1000.0, kSampleRate, levels, segSec);
    const int segLen = secondsToSamples(segSec, kSampleRate);

    UpwardExpander ue;
    ue.prepare(kSampleRate, kBlockSize, 2);
    ue.setThresholdDb(T);
    ue.setRatio(R);
    ue.setKneeDb(0.0f);
    ue.setRangeDb(12.0f);
    AudioBuffer output = input;
    {
        AudioBlock whole(output);
        for (int start = 0; start < output.getNumSamples(); start += kBlockSize)
            ue.process(whole.getSubBlock(start, std::min(kBlockSize, output.getNumSamples() - start)));
    }
    writeWav(outputPath("l08_ex5_output.wav"), output, kSampleRate, 32);

    std::printf("    段落  輸入    輸出實測  理論\n");
    for (size_t s = 0; s < levels.size(); ++s) {
        const float theory = levels[s] + std::min(12.0f, UpwardExpander::computeOutputDb(levels[s], T, R, 0.0f) - levels[s]);
        const double measured = segmentLevelDb(output, static_cast<int>(s), segLen, 1000.0);
        std::printf("    %4zu  %6.1f  %8.2f  %6.1f\n", s + 1, levels[s], measured, theory);
        DAWLAB_CHECK_NEAR(measured, theory, 0.5);
    }
    return dawlab::test::summary("l08_ex5_starter");
}
