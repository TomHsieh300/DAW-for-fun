// 練習 6（起始碼）：feedback 拓樸的 compressor，並與 feedforward 比較。
//
// 目標：
//   1. 完成 FeedbackCompressor::processSample()：detector 看「上一個輸出 sample」而不是輸入；
//      gain computer 用斜率 s 參數化：GR_target = max(0, s·(level_dB - T))，再經 AttackReleaseFilter 平滑；
//   2. 驗證穩態 GR = s/(1+s)·(X - T)，也就是有效 ratio R_eff = 1 + s；
//   3. 比較 s = 3（R_eff = 4）的 feedback 與 feedforward 4:1 的 attack 速度（t63）。
// 完成 TODO 之後，self-check 全部通過就算完成。起始碼執行時會印出 FAIL，這是預期的。

#include <dawlab/Common.h>
#include <dawlab/dsp/Compressor.h>
#include <dawlab/dsp/EnvelopeFollower.h>

#include <cstdio>
#include <vector>

using namespace dawlab;
using dawlab::dsp::AttackReleaseFilter;
using dawlab::dsp::Compressor;
using dawlab::dsp::SmoothingMode;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr double kSegmentSeconds = 0.5;
constexpr float kThresholdDb = -20.0f;

class FeedbackCompressor {
public:
    void prepare(double sampleRate) {
        smoother_.prepare(sampleRate);
        smoother_.setMode(SmoothingMode::Decoupled);
        reset();
    }
    void reset() noexcept {
        smoother_.reset();
        lastOutput_ = 0.0f;
    }
    void setThresholdDb(float db) noexcept { thresholdDb_ = db; }
    void setSlope(float s) noexcept { slope_ = std::max(0.0f, s); } // s = R_eff - 1
    void setAttackMs(float ms) noexcept { smoother_.setAttackMs(ms); }
    void setReleaseMs(float ms) noexcept { smoother_.setReleaseMs(ms); }
    float getGainReductionDb() const noexcept { return smoother_.getCurrentValue(); }

    float processSample(float x) noexcept {
        // TODO:
        //   1. levelDb = gainToDb(|lastOutput_|)
        //   2. grTarget = max(0, slope_ · (levelDb - thresholdDb_))
        //   3. gr = smoother_.process(grTarget)
        //   4. lastOutput_ = x · dbToGain(-gr)，回傳它
        lastOutput_ = x;
        return lastOutput_;
    }

private:
    AttackReleaseFilter smoother_;
    float thresholdDb_ = -20.0f;
    float slope_ = 0.75f;
    float lastOutput_ = 0.0f;
};

// 由輸入 / 輸出每 1 ms 視窗的 peak 反推 GR timeline
std::vector<float> grTimeline(const AudioBuffer& in, const AudioBuffer& out) {
    const int n = in.getNumSamples();
    const int win = msToSamples(1.0, kSampleRate);
    std::vector<float> gr(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        const int a = std::max(0, i - win + 1);
        const float px = peakAbs(in.getReadPointer(0) + a, i - a + 1);
        const float py = peakAbs(out.getReadPointer(0) + a, i - a + 1);
        gr[static_cast<size_t>(i)] = (px > 1e-6f && py > 1e-9f) ? gainToDb(px) - gainToDb(py) : 0.0f;
    }
    return gr;
}

struct Result {
    float settledGr;
    double t63Ms;
};

Result analyze(const AudioBuffer& in, const AudioBuffer& out) {
    const std::vector<float> gr = grTimeline(in, out);
    const int segLen = secondsToSamples(kSegmentSeconds, kSampleRate);
    const int stepStart = segLen, releaseStart = 2 * segLen;
    double sum = 0.0;
    const int tail = msToSamples(50.0, kSampleRate);
    for (int i = releaseStart - tail; i < releaseStart; ++i)
        sum += gr[static_cast<size_t>(i)];
    const float settled = static_cast<float>(sum / tail);
    double t63 = -1.0;
    for (int i = stepStart; i < releaseStart; ++i) {
        if (gr[static_cast<size_t>(i)] >= 0.632f * settled) {
            t63 = samplesToMs(i - stepStart, kSampleRate);
            break;
        }
    }
    return {settled, t63};
}

} // namespace

int main() {
    std::printf("練習 6（起始碼）：feedback vs feedforward\n");
    const AudioBuffer input = makeToneBursts(1000.0, kSampleRate, {-30.0f, -10.0f, -30.0f}, kSegmentSeconds, 1);
    const float overDb = 10.0f;

    Result ff;
    {
        Compressor comp;
        comp.prepare(kSampleRate, 512, 1);
        comp.setThresholdDb(kThresholdDb);
        comp.setRatio(4.0f);
        comp.setKneeDb(0.0f);
        comp.setAttackMs(10.0f);
        comp.setReleaseMs(100.0f);
        AudioBuffer out = input;
        AudioBlock whole(out);
        for (int start = 0; start < out.getNumSamples(); start += 512)
            comp.process(whole.getSubBlock(start, std::min(512, out.getNumSamples() - start)));
        ff = analyze(input, out);
        std::printf("  feedforward R = 4：穩態 GR %.2f dB（理論 7.50），t63 = %.2f ms\n", ff.settledGr, ff.t63Ms);
        DAWLAB_CHECK_NEAR(ff.settledGr, 7.5, 0.2);
    }

    const float slopes[] = {0.75f, 3.0f, 9.0f};
    for (float s : slopes) {
        FeedbackCompressor fb;
        fb.prepare(kSampleRate);
        fb.setThresholdDb(kThresholdDb);
        fb.setSlope(s);
        fb.setAttackMs(10.0f);
        fb.setReleaseMs(100.0f);
        AudioBuffer out = input;
        for (int i = 0; i < out.getNumSamples(); ++i)
            out.setSample(0, i, fb.processSample(out.getSample(0, i)));
        const Result r = analyze(input, out);
        // TODO: 把理論值換成 s/(1+s)·overDb
        const float expectedGr = 0.0f * overDb;
        std::printf("  feedback s = %.2f（R_eff = %.2f）：穩態 GR %.2f dB（理論 %.2f），t63 = %.2f ms\n", s, 1.0f + s,
                    r.settledGr, expectedGr, r.t63Ms);
        DAWLAB_CHECK_NEAR(r.settledGr, expectedGr, 0.3);
        DAWLAB_CHECK(!hasNanOrInf(out));
        if (s == 3.0f) {
            DAWLAB_CHECK_NEAR(r.settledGr, ff.settledGr, 0.3);
            DAWLAB_CHECK(r.t63Ms < ff.t63Ms);
        }
    }
    return dawlab::test::summary("l04_ex6_starter");
}
