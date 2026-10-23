// 練習 5 解答：upward expander —— 把 threshold「以上」的訊號推得更大
//
// 靜態曲線（dB，R ≥ 1，T = threshold，W = knee）：
//   y = x                                    x ≤ T − W/2
//   y = x + (R − 1)(x − T + W/2)² / (2W)     T − W/2 < x < T + W/2
//   y = T + R (x − T)                        x ≥ T + W/2
// 增益 g = y − x ≥ 0，再用 range 限制最大 boost（例如 +12 dB），否則大聲的 transient 會被推到爆。
// 和 downward expander 對照：downward 作用在 T 以下、斜率 R；upward 作用在 T 以上、斜率 R。
//
// 自我檢查：
//   - knee 邊界處曲線連續、斜率連續（數值微分）
//   - tone bursts {-30, -20, -10, -40} dB、T = -30、R = 1.5、range +12 → 理論輸出 {-30, -15, 0, -40}
//     其中 -10 dB 段：T + R(x − T) = -30 + 1.5·20 = 0 dB → boost 10 dB < 12 dB 上限 → 輸出 0 dB
// 執行：l08_ex5_solution → 輸出 dawlab_output/l08_ex5_input.wav、l08_ex5_output.wav，印出 PASS / FAIL。

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
    static float computeOutputDb(float inputDb, float thresholdDb, float ratio, float kneeDb) noexcept {
        const float r = std::max(1.0f, ratio);
        const float w = std::max(0.0f, kneeDb);
        const float halfW = 0.5f * w;
        if (inputDb <= thresholdDb - halfW)
            return inputDb;
        if (w > 0.0f && inputDb < thresholdDb + halfW) {
            const float d = inputDb - thresholdDb + halfW; // 0 .. W
            return inputDb + (r - 1.0f) * d * d / (2.0f * w);
        }
        return thresholdDb + r * (inputDb - thresholdDb);
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
    // 額外：切換偵測器的平滑結構（只為了在 self-check 裡對照 Branching 的誤差）
    void setSmoothingMode(SmoothingMode mode) noexcept { detector_.setSmoothingMode(mode); }
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
        const float levelDb = gainToDb(detector_.processSample(detectorInput));
        const float boost = std::min(rangeDb_, computeOutputDb(levelDb, thresholdDb_, ratio_, kneeDb_) - levelDb);
        if (boost != gainDb_) {
            gainDb_ = boost;
            gain_ = dbToGain(gainDb_);
        }
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
    std::printf("第 08 章 練習 5：upward expander\n");

    // ---- 1. 靜態曲線：knee 邊界的連續性 ----
    const float T = -30.0f, R = 1.5f, W = 10.0f;
    const float lo = T - W / 2, hi = T + W / 2;
    std::printf("\n[1] 靜態曲線 T=%.0f R=%.1f knee=%.0f：knee 下緣 %.0f dB → %.3f；上緣 %.0f dB → %.3f\n", T, R, W, lo,
                UpwardExpander::computeOutputDb(lo, T, R, W), hi, UpwardExpander::computeOutputDb(hi, T, R, W));
    // 連續：knee 內外的公式在邊界給出同一個值
    DAWLAB_CHECK_NEAR(UpwardExpander::computeOutputDb(lo, T, R, W), lo, 1.0e-4);
    DAWLAB_CHECK_NEAR(UpwardExpander::computeOutputDb(hi, T, R, W), T + R * (hi - T), 1.0e-4);
    // 斜率連續：knee 下緣斜率 → 1，上緣斜率 → R（數值微分）
    const float eps = 0.01f;
    const float slopeLo = (UpwardExpander::computeOutputDb(lo + eps, T, R, W) - UpwardExpander::computeOutputDb(lo, T, R, W)) / eps;
    const float slopeHi = (UpwardExpander::computeOutputDb(hi, T, R, W) - UpwardExpander::computeOutputDb(hi - eps, T, R, W)) / eps;
    std::printf("    knee 下緣斜率 %.3f（應 ≈ 1）、上緣斜率 %.3f（應 ≈ %.1f）\n", slopeLo, slopeHi, R);
    DAWLAB_CHECK_NEAR(slopeLo, 1.0, 0.02);
    DAWLAB_CHECK_NEAR(slopeHi, R, 0.02);
    // hard knee 的幾個點
    DAWLAB_CHECK_NEAR(UpwardExpander::computeOutputDb(-40.0f, T, R, 0.0f), -40.0, 1.0e-5);
    DAWLAB_CHECK_NEAR(UpwardExpander::computeOutputDb(-20.0f, T, R, 0.0f), -15.0, 1.0e-5);
    DAWLAB_CHECK_NEAR(UpwardExpander::computeOutputDb(-10.0f, T, R, 0.0f), 0.0, 1.0e-5);

    // ---- 2. 動態實測 ----
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
    writeWav(outputPath("l08_ex5_input.wav"), input, kSampleRate, 32);
    writeWav(outputPath("l08_ex5_output.wav"), output, kSampleRate, 32);

    std::printf("\n[2] tone bursts（T = -30、R = 1.5、最大 boost 12 dB）\n    段落  輸入    輸出實測  理論\n");
    for (size_t s = 0; s < levels.size(); ++s) {
        const float theory = levels[s] + std::min(12.0f, UpwardExpander::computeOutputDb(levels[s], T, R, 0.0f) - levels[s]);
        const double measured = segmentLevelDb(output, static_cast<int>(s), segLen, 1000.0);
        std::printf("    %4zu  %6.1f  %8.2f  %6.1f\n", s + 1, levels[s], measured, theory);
        DAWLAB_CHECK_NEAR(measured, theory, 0.5);
    }
    DAWLAB_CHECK(!hasNanOrInf(output));

    // ---- 3. 對照：偵測器改用 Branching ----
    // Branching 在穩態正弦波上會低估 peak，upward expander 再把誤差乘上 R，輸出偏離靜態曲線
    UpwardExpander ueBranching;
    ueBranching.prepare(kSampleRate, kBlockSize, 2);
    ueBranching.setSmoothingMode(SmoothingMode::Branching);
    ueBranching.setThresholdDb(T);
    ueBranching.setRatio(R);
    ueBranching.setKneeDb(0.0f);
    ueBranching.setRangeDb(12.0f);
    AudioBuffer outBranching = input;
    {
        AudioBlock whole(outBranching);
        for (int start = 0; start < outBranching.getNumSamples(); start += kBlockSize)
            ueBranching.process(whole.getSubBlock(start, std::min(kBlockSize, outBranching.getNumSamples() - start)));
    }
    std::printf("\n[3] 對照：偵測器改用 Branching（其他設定相同）\n    段落  輸入    Decoupled  Branching\n");
    for (size_t s = 0; s < levels.size(); ++s)
        std::printf("    %4zu  %6.1f  %9.2f  %9.2f\n", s + 1, levels[s],
                    segmentLevelDb(output, static_cast<int>(s), segLen, 1000.0),
                    segmentLevelDb(outBranching, static_cast<int>(s), segLen, 1000.0));
    // Branching 的 -20 dB 段明顯偏低（超過 self-check 的 0.5 dB 容許範圍），這就是改用 Decoupled 的理由
    const double branchingErr = -15.0 - segmentLevelDb(outBranching, 1, segLen, 1000.0);
    std::printf("    Branching 在 -20 dB 段比理論低 %.2f dB\n", branchingErr);
    DAWLAB_CHECK(branchingErr > 0.5);

    return dawlab::test::summary("l08_ex5_upward_expander");
}
