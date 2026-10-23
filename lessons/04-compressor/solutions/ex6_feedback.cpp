// Exercise 6 solution: a feedback-topology compressor, compared with feedforward.
//
// Feedforward: the detector looks at the "input". Feedback: the detector looks at the "previous output sample".
//
// Derivation (dB, hard knee, steady state): let the gain computer's slope s = the number of dB of GR added for every
// 1 dB the level goes over the threshold (for feedforward, s = 1 - 1/R). With feedback, Y = X - GR and GR = s·(Y - T):
//   GR = s·(X - GR - T)  ⇒  GR·(1 + s) = s·(X - T)  ⇒  GR = s/(1+s)·(X - T)
//   Y - T = (X - T) - GR = (X - T)/(1 + s)  ⇒  effective ratio R_eff = 1 + s
// Therefore:
//   - moving a feedforward 4:1 setting (s = 0.75) straight to feedback gives only 1.75:1;
//   - to make feedback actually reach R_eff, set s = R_eff - 1 (4:1 → s = 3, which is greater than 1!).
// Behavioral difference: at first the feedback detector sees a loud output that has "not yet been squashed" → the GR
// target is initially much larger than at steady state → the attack is especially fast at the start and then slowly
// converges, so the attack becomes program-dependent (it depends on how far the signal is over the threshold); this
// is one reason classic feedback compressors "sound smoother".
// Note: a feedback loop with s > 1 is only stable thanks to the attack smoothing (attack = 0 oscillates / locks up);
// the attack is fixed at 10 ms in this solution.

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

// Mono feedback compressor (hard knee). The gain computer is parameterized by a "slope s", R_eff = 1 + s.
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
        // The detector looks at the previous "output" sample (feedback), not the input
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

// Recover the GR timeline from the peak ratio of the input / output over 1 ms windows
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
    std::printf("Exercise 6: feedback vs feedforward\n");
    const AudioBuffer input = makeToneBursts(1000.0, kSampleRate, {-30.0f, -10.0f, -30.0f}, kSegmentSeconds, 1);
    const float overDb = 10.0f; // -10 dB is 10 dB above the -20 dB threshold

    // Feedforward 4:1 (reference)
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
        std::printf("  feedforward R = 4 (s = 0.75): steady-state GR %.2f dB (theory 7.50), t63 = %.2f ms\n",
                    ff.settledGr, ff.t63Ms);
        DAWLAB_CHECK_NEAR(ff.settledGr, 7.5, 0.2);
    }

    // Feedback, with different slopes
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
        std::printf("  feedback s = %.2f (R_eff = %.2f): steady-state GR %.2f dB (theory s/(1+s)·10 = %.2f), t63 = %.2f ms\n",
                    s, effRatio, r.settledGr, expectedGr, r.t63Ms);
        DAWLAB_CHECK_NEAR(r.settledGr, expectedGr, 0.3);
        DAWLAB_CHECK(!hasNanOrInf(out));
        if (s == 3.0f) {
            // s = 3 ⇒ R_eff = 4: the steady-state GR should equal feedforward 4:1, but the attack is faster (program-dependent)
            DAWLAB_CHECK_NEAR(r.settledGr, ff.settledGr, 0.3);
            DAWLAB_CHECK(r.t63Ms < ff.t63Ms);
            writeWav(outputPath("l04_ex6_feedback.wav"), out, kSampleRate);
        }
    }
    std::printf("  Conclusion: the effective ratio of feedback = 1 + s; at the same 4:1, feedback attacks faster than feedforward (smaller t63).\n");
    std::printf("  Wrote dawlab_output/l04_ex6_feedback.wav\n");
    return dawlab::test::summary("l04_ex6");
}
