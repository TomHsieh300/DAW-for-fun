// 練習 6 解答：feedback 拓樸的 compressor，並與 feedforward 比較。
//
// Feedforward：detector 看「輸入」。  Feedback：detector 看「上一個輸出 sample」。
//
// 推導（dB，hard knee，穩態）：令 gain computer 的斜率 s = GR 每超過 threshold 1 dB 所增加的 dB
//（feedforward 的 s = 1 - 1/R）。feedback 時 Y = X - GR 且 GR = s·(Y - T)：
//   GR = s·(X - GR - T)  ⇒  GR·(1 + s) = s·(X - T)  ⇒  GR = s/(1+s)·(X - T)
//   Y - T = (X - T) - GR = (X - T)/(1 + s)  ⇒  有效 ratio R_eff = 1 + s
// 所以：
//   - 把 feedforward 的 4:1 設定（s = 0.75）直接搬到 feedback 只會得到 1.75:1；
//   - 想要 feedback 真的做到 R_eff，要設 s = R_eff - 1（4:1 → s = 3，比 1 還大！）。
// 行為差異：feedback 的 detector 一開始看到「還沒被壓」的大輸出 → GR 目標值一開始比穩態大很多 → attack 前段特別快、
// 之後慢慢收斂，attack 變成 program-dependent（跟訊號超過多少有關），這是古典 feedback 壓縮器「聽起來比較順」的原因之一。
// 注意：s > 1 的 feedback 迴路必須靠 attack 平滑才穩定（attack = 0 會震盪 / 鎖死），解答裡 attack 固定 10 ms。

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

// Mono feedback compressor（hard knee）。gain computer 用「斜率 s」參數化，R_eff = 1 + s。
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
    void setSlope(float s) noexcept { slope_ = std::max(0.0f, s); }        // s = R_eff - 1
    void setEffectiveRatio(float r) noexcept { setSlope(std::max(1.0f, r) - 1.0f); }
    void setAttackMs(float ms) noexcept { smoother_.setAttackMs(ms); }
    void setReleaseMs(float ms) noexcept { smoother_.setReleaseMs(ms); }
    float getGainReductionDb() const noexcept { return smoother_.getCurrentValue(); }

    float processSample(float x) noexcept {
        // detector 看的是上一個「輸出」sample（feedback），不是輸入
        const float levelDb = gainToDb(std::abs(lastOutput_));
        const float grTarget = std::max(0.0f, slope_ * (levelDb - thresholdDb_));
        const float gr = smoother_.process(grTarget);
        lastOutput_ = x * dbToGain(-gr);
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
    std::printf("練習 6：feedback vs feedforward\n");
    const AudioBuffer input = makeToneBursts(1000.0, kSampleRate, {-30.0f, -10.0f, -30.0f}, kSegmentSeconds, 1);
    const float overDb = 10.0f; // -10 dB 比 threshold -20 高 10 dB

    // Feedforward 4:1（參考）
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
        std::printf("  feedforward R = 4（s = 0.75）：穩態 GR %.2f dB（理論 7.50），t63 = %.2f ms\n", ff.settledGr,
                    ff.t63Ms);
        DAWLAB_CHECK_NEAR(ff.settledGr, 7.5, 0.2);
    }

    // Feedback，不同斜率
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
        const float expectedGr = s / (1.0f + s) * overDb;
        const float effRatio = 1.0f + s;
        std::printf("  feedback s = %.2f（R_eff = %.2f）：穩態 GR %.2f dB（理論 s/(1+s)·10 = %.2f），t63 = %.2f ms\n",
                    s, effRatio, r.settledGr, expectedGr, r.t63Ms);
        DAWLAB_CHECK_NEAR(r.settledGr, expectedGr, 0.3);
        DAWLAB_CHECK(!hasNanOrInf(out));
        if (s == 3.0f) {
            // s = 3 ⇒ R_eff = 4，穩態 GR 應該與 feedforward 4:1 相同，但 attack 更快（program-dependent）
            DAWLAB_CHECK_NEAR(r.settledGr, ff.settledGr, 0.3);
            DAWLAB_CHECK(r.t63Ms < ff.t63Ms);
            writeWav(outputPath("l04_ex6_feedback.wav"), out, kSampleRate);
        }
    }
    std::printf("  結論：feedback 的有效 ratio = 1 + s；同樣 4:1 時 feedback 的 attack 比 feedforward 快（t63 較小）。\n");
    std::printf("  已輸出 dawlab_output/l04_ex6_feedback.wav\n");
    return dawlab::test::summary("l04_ex6");
}
