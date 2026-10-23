// Exercise 8 starter: Staggered phaser -- each allpass stage has its own fc; verify the prediction from Exercise 7 part 4
//
// Chapter 10's dawlab::dsp::Phaser makes all N allpass stages share the same fc, and the notches have a closed form:
//   tan(π f_k / fs) = tan(π fc / fs) · tan((2k+1)π / (2N))
// When each stage has a different fc there is no closed form, but "total phase = sum of the per-stage phases" still holds:
//   Φ(f) = Σ φ_i(f),  φ_i(f) = −2·arctan( tan(πf/fs) / tan(π fc_i/fs) )
// Every term falls monotonically from 0 (DC) to −π (Nyquist), so Φ falls monotonically from 0 to −Nπ;
// the notches are at Φ(f) = −(2k+1)π, which bisection solves just fine, and the number of notches is still N/2.
//
// Sweep: all fc_i are multiplied by the same ratio 2^(depth · octaves · lfo), so the ratios between the stages' fc stay the same,
// and the whole set of notches translates together along the logarithmic frequency axis.
//
// Goal: complete the three TODOs in StaggeredPhaser and pass the self-check:
//   1. All 4 stages at 1000 Hz: predictNotchHz() = Phaser::notchFrequencyHz() (414.7 / 2397.8 Hz);
//      the white-noise output is identical sample for sample to dawlab::dsp::Phaser (LFO frozen, mix 0.5, feedback 0)
//   2. 300 / 600 / 1200 / 2400 Hz: 2 notches are found, within 0.5% of predictNotchHz(), with depth < −30 dB
//   3. The prediction from Exercise 7 part 4: the first notch is lower than the 4 x 1000 Hz one, and the second is higher
//   4. depth 1, octaves 1, LFO parked at +1 -> every fc doubles, and the measured notches = predictNotchHz(k, 2)
// This program compiles; when run, the self-check FAILs because the TODOs are not done yet (expected).

#include <dawlab/Common.h>
#include <dawlab/dsp/Biquad.h>
#include <dawlab/dsp/Lfo.h>
#include <dawlab/dsp/Phaser.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

using dawlab::dsp::Biquad;
using dawlab::dsp::Lfo;
using dawlab::dsp::LfoShape;
using dawlab::dsp::Phaser;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

class StaggeredPhaser {
public:
    static constexpr int kMaxStages = 12;

    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        sampleRate_ = sampleRate;
        numChannels_ = std::max(1, numChannels);
        for (auto& s : stages_)
            s.prepare(sampleRate, maxBlockSize, numChannels_);
        lfo_.prepare(sampleRate);
        mixSm_.prepare(sampleRate, 20.0);
        reset();
    }

    void reset() noexcept {
        for (auto& s : stages_)
            s.reset();
        lfo_.reset();
        mixSm_.setCurrentAndTarget(mixSm_.getTarget());
    }

    // ---- Parameters ----------------------------------------------------------
    // Set the number of stages (an even number from 2 to 12) and each stage's base fc; fcs must have at least `stages` elements
    void setStageFrequencies(const std::vector<float>& fcs) noexcept {
        int n = std::clamp(static_cast<int>(fcs.size()), 2, kMaxStages);
        n -= n % 2;
        for (int s = 0; s < n; ++s)
            baseHz_[static_cast<size_t>(s)] = std::clamp(fcs[static_cast<size_t>(s)], 20.0f, 20000.0f);
        for (int s = numStages_; s < n; ++s)
            stages_[static_cast<size_t>(s)].reset();
        numStages_ = n;
    }
    void setRateHz(float hz) noexcept { lfo_.setRateHz(std::clamp(hz, 0.0f, 20.0f)); }
    void setDepth(float depth) noexcept { depth_ = std::clamp(depth, 0.0f, 1.0f); }
    void setSweepOctaves(float octaves) noexcept { sweepOctaves_ = std::clamp(octaves, 0.0f, 6.0f); }
    void setLfoPhase(float phase01) noexcept { lfo_.setPhase(phase01); }
    void setMix(float mix) noexcept { mixSm_.setTarget(std::clamp(mix, 0.0f, 1.0f)); }
    int getStages() const noexcept { return numStages_; }

    // ---- Theory --------------------------------------------------------------
    // Total phase (radians): Σ φ_i(f) after multiplying every fc by scale
    double theoryPhase(double fHz, double scale = 1.0) const noexcept {
        // TODO 1: add up the phase of every stage. Stage s has fc = scale · baseHz_[s];
        //         the phase of a first-order allpass is given directly by Phaser::allpassPhase(fHz, fc, sampleRate_).
        (void)fHz;
        (void)scale;
        return 0.0;
    }

    // The k-th notch (k = 0 .. N/2−1): solve theoryPhase(f) = −(2k+1)π.
    // theoryPhase falls monotonically from 0 to −Nπ, so use bisection: if the phase is "not negative enough yet", look at higher frequencies.
    double predictNotchHz(int k, double scale = 1.0) const noexcept {
        // TODO 2: bisect 60 times within [1 Hz, fs/2 − 1 Hz] to find the f with theoryPhase(f, scale) = −(2k+1)π.
        //         If the phase is "greater" than the target (not negative enough yet) -> the notch is at a higher frequency, so move lo to mid; otherwise move hi to mid.
        (void)k;
        (void)scale;
        return 0.0;
    }

    // ---- Processing ----------------------------------------------------------
    float processSample(int channel, float x) noexcept {
        if (channel == 0)
            advanceModulation();
        return renderSample(channel, x);
    }

    void process(dawlab::AudioBlock block) noexcept {
        const int n = block.getNumSamples();
        const int nch = std::min(block.getNumChannels(), numChannels_);
        for (int i = 0; i < n; ++i) {
            advanceModulation();
            for (int ch = 0; ch < nch; ++ch) {
                float* d = block.getChannelPointer(ch);
                d[i] = renderSample(ch, d[i]);
            }
        }
    }

private:
    void advanceModulation() noexcept {
        mix_ = mixSm_.next();
        // Common ratio on the exponential scale: every stage's fc is multiplied by the same 2^(depth · octaves · lfo), preserving the ratios between stages
        const float m = lfo_.getNextValue();
        // TODO 3: scale = 2^(depth_ · sweepOctaves_ · m); each stage's fc = baseHz_[s] · scale,
        //         clamped to [20 Hz, 0.45·fs]; then set stages_[s]'s coefficients with
        //         Phaser::makeFirstOrderAllpass(fc, sampleRate_) (only the first numStages_ stages).
        (void)m;
    }

    float renderSample(int ch, float x) noexcept {
        float w = x;
        for (int s = 0; s < numStages_; ++s)
            w = stages_[static_cast<size_t>(s)].processSample(ch, w);
        return (1.0f - mix_) * x + mix_ * w;
    }

    double sampleRate_ = 48000.0;
    int numChannels_ = 2;
    int numStages_ = 4;
    float depth_ = 0.0f;
    float sweepOctaves_ = 2.0f;
    std::array<float, kMaxStages> baseHz_{1000.0f, 1000.0f, 1000.0f, 1000.0f, 1000.0f, 1000.0f,
                                          1000.0f, 1000.0f, 1000.0f, 1000.0f, 1000.0f, 1000.0f};
    std::array<Biquad, kMaxStages> stages_;
    Lfo lfo_;
    dawlab::dsp::detail::ModParamSmoother mixSm_{0.5f};
    float mix_ = 0.5f;
};

template <typename Effect>
void processInBlocks(Effect& fx, dawlab::AudioBuffer& buffer) {
    dawlab::AudioBlock whole(buffer);
    for (int start = 0; start < buffer.getNumSamples(); start += kBlockSize) {
        const int n = std::min(kBlockSize, buffer.getNumSamples() - start);
        fx.process(whole.getSubBlock(start, n));
    }
}

// A staggered phaser with the LFO frozen (rate 0), for measurement
StaggeredPhaser makeFrozen(const std::vector<float>& fcs, float depth = 0.0f, float lfoPhase = 0.0f) {
    StaggeredPhaser p;
    p.prepare(kSampleRate, kBlockSize, 1);
    p.setStageFrequencies(fcs);
    p.setRateHz(0.0f);
    p.setDepth(depth);
    p.setSweepOctaves(1.0f);
    p.setLfoPhase(lfoPhase);
    p.setMix(0.5f);
    p.reset();
    return p;
}

double measureDb(StaggeredPhaser& p, double freq) {
    p.reset();
    return dawlab::measureGainDb([&](float x) { return p.processSample(0, x); }, freq, kSampleRate, 0.3, 0.1);
}

struct Notch {
    double hz;
    double db;
};

// Sweep 50 Hz-16 kHz logarithmically to find local minima, then refine with ternary search (a notch is sharp, so a grid only finds its neighbourhood)
std::vector<Notch> findNotches(StaggeredPhaser& p) {
    constexpr int kPoints = 160;
    std::vector<double> freqs, gains;
    for (int i = 0; i <= kPoints; ++i) {
        freqs.push_back(50.0 * std::pow(16000.0 / 50.0, static_cast<double>(i) / kPoints));
        gains.push_back(measureDb(p, freqs.back()));
    }
    std::vector<Notch> notches;
    for (size_t i = 1; i + 1 < gains.size(); ++i) {
        if (gains[i] < gains[i - 1] && gains[i] <= gains[i + 1] && gains[i] < -6.0) {
            double lo = freqs[i - 1], hi = freqs[i + 1];
            for (int it = 0; it < 16; ++it) {
                const double m1 = lo + (hi - lo) / 3.0;
                const double m2 = hi - (hi - lo) / 3.0;
                if (measureDb(p, m1) < measureDb(p, m2))
                    hi = m2;
                else
                    lo = m1;
            }
            const double f = 0.5 * (lo + hi);
            notches.push_back({f, measureDb(p, f)});
        }
    }
    return notches;
}

// Print measured vs predicted, and check the number of notches, their positions (relative error < 0.5%), and their depth (< −30 dB)
void checkNotches(const char* label, StaggeredPhaser& p, double scale) {
    const std::vector<Notch> notches = findNotches(p);
    const int expected = p.getStages() / 2;
    std::printf("%s: found %d notches (expected %d)\n", label, static_cast<int>(notches.size()), expected);
    std::printf("   k   measured(Hz)  predicted(Hz)   error      depth\n");
    for (size_t k = 0; k < notches.size(); ++k) {
        const double pred = p.predictNotchHz(static_cast<int>(k), scale);
        // pred is 0 while TODO 2 is not done: avoid dividing by 0 and treat it as a 100% error
        const double err = pred > 0.0 ? (notches[k].hz - pred) / pred : 1.0;
        std::printf("  %2d   %10.1f    %10.1f   %+6.3f%%   %+6.1f dB\n", static_cast<int>(k), notches[k].hz, pred,
                    100.0 * err, notches[k].db);
        DAWLAB_CHECK(std::abs(err) < 0.005);
        DAWLAB_CHECK(notches[k].db < -30.0);
    }
    std::printf("\n");
    DAWLAB_CHECK(static_cast<int>(notches.size()) == expected);
}

} // namespace

int main() {
    std::printf("=== Chapter 10 Exercise 8: Staggered phaser (starter) ===\n\n");

    const std::vector<float> uniform{1000.0f, 1000.0f, 1000.0f, 1000.0f};
    const std::vector<float> staggered{300.0f, 600.0f, 1200.0f, 2400.0f};

    // ---- 1. All at 1000 Hz: agrees with the closed form and with dawlab::dsp::Phaser ----
    {
        StaggeredPhaser sp = makeFrozen(uniform);
        std::printf("4 x 1000 Hz: predictNotchHz vs Phaser::notchFrequencyHz (closed form)\n");
        for (int k = 0; k < 2; ++k) {
            const double pred = sp.predictNotchHz(k);
            const double closed = Phaser::notchFrequencyHz(k, 4, 1000.0, kSampleRate);
            std::printf("  k = %d: %.4f Hz vs %.4f Hz\n", k, pred, closed);
            DAWLAB_CHECK_NEAR(pred, closed, 0.01);
        }

        Phaser ref;
        ref.prepare(kSampleRate, kBlockSize, 1);
        ref.setRateHz(0.0f);
        ref.setDepth(0.0f);
        ref.setCentreHz(1000.0f);
        ref.setFeedback(0.0f);
        ref.setStages(4);
        ref.setMix(0.5f);
        ref.reset();
        dawlab::AudioBuffer a = dawlab::makeWhiteNoise(kSampleRate, 1.0, 0.25f, 1, 5u);
        dawlab::AudioBuffer b = a;
        processInBlocks(sp, a);
        processInBlocks(ref, b);
        float maxDiff = 0.0f;
        for (int i = 0; i < a.getNumSamples(); ++i)
            maxDiff = std::max(maxDiff, std::abs(a.getSample(0, i) - b.getSample(0, i)));
        std::printf("  White noise through StaggeredPhaser vs dawlab::dsp::Phaser: largest difference %.2e (should be 0)\n\n", maxDiff);
        DAWLAB_CHECK(maxDiff < 1.0e-5f);
    }

    // ---- 2. 300 / 600 / 1200 / 2400 Hz: measured notches vs the bisection prediction ----
    StaggeredPhaser sp = makeFrozen(staggered);
    checkNotches("300 / 600 / 1200 / 2400 Hz", sp, 1.0);

    // ---- 3. The prediction from Exercise 7 part 4: the notch count is unchanged and the two notches spread further apart ----
    {
        const double u0 = Phaser::notchFrequencyHz(0, 4, 1000.0, kSampleRate);
        const double u1 = Phaser::notchFrequencyHz(1, 4, 1000.0, kSampleRate);
        const double s0 = sp.predictNotchHz(0), s1 = sp.predictNotchHz(1);
        const double sRatio = s0 > 0.0 ? s1 / s0 : 0.0; // s0 is 0 while TODO 2 is not done
        std::printf("Compared with 4 x 1000 Hz:\n");
        std::printf("  4 x 1000 Hz:         %7.1f / %7.1f Hz, ratio %.2fx (%.2f octaves)\n", u0, u1, u1 / u0,
                    std::log2(u1 / u0));
        std::printf("  300-2400 Hz spread:  %7.1f / %7.1f Hz, ratio %.2fx (%.2f octaves)\n", s0, s1, sRatio,
                    sRatio > 0.0 ? std::log2(sRatio) : 0.0);
        std::printf("  -> The first notch moves down, the second moves up, and there are still 2 notches.\n");
        const double toDeg = 180.0 / dawlab::kPi;
        const StaggeredPhaser up = makeFrozen(uniform);
        std::printf("     total phase   4 x 1000 Hz    300-2400 Hz\n");
        for (double f : {u0, 1000.0, u1})
            std::printf("     %7.1f Hz   %+8.1f°      %+8.1f°\n", f, up.theoryPhase(f) * toDeg, sp.theoryPhase(f) * toDeg);
        std::printf("  -> After spreading, the total-phase curve is flatter (the low-frequency stage starts turning earlier and the high-frequency stage finishes later):\n"
                    "     at %.1f Hz it has already passed −180°, and at %.1f Hz it has not yet reached −540°,\n"
                    "     so the first notch appears earlier (toward low frequencies) and the second later (toward high frequencies).\n\n",
                    u0, u1);
        DAWLAB_CHECK(s0 < u0);
        DAWLAB_CHECK(s1 > u1);
    }

    // ---- 4. Sweep: LFO parked at +1, depth 1, octaves 1 -> every fc doubles ----------------
    {
        StaggeredPhaser swept = makeFrozen(staggered, 1.0f, 0.25f); // sine LFO phase 0.25 -> value = +1
        checkNotches("LFO = +1 (fc x 2 = 600 / 1200 / 2400 / 4800 Hz)", swept, 2.0);
    }

    // ---- Listening -----------------------------------------------------------
    {
        std::printf("Output WAVs (white noise, rate 0.2 Hz, depth 1, ±1.5 octaves, mix 0.5):\n");
        for (int which = 0; which < 2; ++which) {
            dawlab::AudioBuffer noise = dawlab::makeWhiteNoise(kSampleRate, 6.0, 0.2f, 2, 11u);
            StaggeredPhaser p;
            p.prepare(kSampleRate, kBlockSize, 2);
            p.setStageFrequencies(which == 0 ? uniform : staggered);
            p.setRateHz(0.2f);
            p.setDepth(1.0f);
            p.setSweepOctaves(1.5f);
            p.setMix(0.5f);
            p.reset();
            processInBlocks(p, noise);
            const char* name = which == 0 ? "l10_ex8_uniform.wav" : "l10_ex8_staggered.wav";
            dawlab::writeWav(dawlab::outputPath(name), noise, kSampleRate);
            std::printf("  %-24s peak %.3f\n", name, dawlab::peakAbs(noise));
            DAWLAB_CHECK(!dawlab::hasNanOrInf(noise));
        }
        std::printf("  -> The staggered version's two notches are further apart and each is broader, so the sweep sounds more \"open\".\n");
    }

    return dawlab::test::summary("l10_ex8");
}
