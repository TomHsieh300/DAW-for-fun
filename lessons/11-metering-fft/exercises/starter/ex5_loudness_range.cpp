// Exercise 5 starter: Loudness Range (LRA, EBU Tech 3342)
//
// LRA answers the question "how many LU separate the loud passages of the song from the quiet ones" -- that is,
// macro dynamics, which is a different thing from the crest factor (peak / RMS, micro dynamics). The algorithm
// (EBU Tech 3342):
//
//   1. Take short-term loudness (3 s window, K-weighting) as the raw material and take one value every so often
//      (we take one every 100 ms, in sync with LoudnessMeter's sub-blocks; the standard requires at least 66% overlap).
//      Only "complete 3 s windows" count.
//   2. Absolute gate: discard values below -70 LUFS.
//   3. Relative gate: compute the "power average" of the remaining values, convert it back to LUFS, and discard values
//      more than 20 LU below it (note: integrated loudness uses -10 LU, while LRA uses -20 LU).
//   4. LRA = the 95th percentile of the remaining values minus the 10th percentile (in LU).
//      10% / 95% are used instead of min / max so that a very short explosion or a fade-out does not inflate the LRA.
//
// As with LoudnessMeter, we do not keep an ever-growing vector on the audio thread; we use a histogram with one bin
// per 0.1 LU: percentiles are found directly from the cumulative counts, with an error of at most 0.1 LU and no
// allocation at all.
//
// Limitation: the block length of each process() call must not exceed 100 ms (4800 samples @ 48 kHz); otherwise one
// process() would cross two sub-blocks and we would only see the last short-term value. Real audio callbacks are far
// shorter than this.
//
// Verification (the EBU Tech 3342 test signals): a 1 kHz sine, level A for the first 20 s and level B for the last 20 s,
//   -20 / -30 dBFS -> LRA = 10 ± 1 LU; -20 / -15 -> 5 ± 1; -40 / -20 -> 20 ± 1; constant level -> 0.
//
// Your task: complete computeLra() (the relative gate + percentiles). process() already puts the short-term values that
// pass the absolute gate into the histogram. Running it before you finish prints FAIL (this is expected).
// Run: bin/Debug/l11_ex5_starter.exe (solution: solutions/ex5_loudness_range.cpp)

#include <dawlab/Common.h>
#include <dawlab/dsp/LoudnessMeter.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace dawlab;
using dawlab::dsp::LoudnessMeter;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

class LoudnessRangeMeter {
public:
    static constexpr float kAbsoluteGateLufs = -70.0f;
    static constexpr float kRelativeGateLu = -20.0f;
    static constexpr float kLowPercentile = 0.10f;
    static constexpr float kHighPercentile = 0.95f;

    void prepare(double sampleRate, int numChannels) {
        meter_.prepare(sampleRate, numChannels);
        histogramCount_.assign(kBins, 0u);
        reset();
    }

    void reset() noexcept {
        meter_.reset();
        std::fill(histogramCount_.begin(), histogramCount_.end(), 0u);
        lastUpdate_ = 0;
        lra_.store(0.0f, std::memory_order_relaxed);
    }

    // Audio thread (block length <= 100 ms)
    void process(const AudioBlock& block) noexcept {
        meter_.process(block);
        const uint32_t updates = meter_.getUpdateCount();
        if (updates == lastUpdate_)
            return; // this block did not complete a new 100 ms sub-block
        lastUpdate_ = updates;
        if (updates < static_cast<uint32_t>(kFullShortTermSteps))
            return; // first 3 s: the short-term window is not full yet, so it doesn't count
        const float st = meter_.getShortTermLufs();
        if (st <= kAbsoluteGateLufs)
            return; // absolute gate
        const int bin = std::clamp(static_cast<int>(std::floor((st - kMinLufs) / kStepLu)), 0, kBins - 1);
        ++histogramCount_[static_cast<size_t>(bin)];
        lra_.store(computeLra(), std::memory_order_relaxed);
    }

    float getLoudnessRangeLu() const noexcept { return lra_.load(std::memory_order_relaxed); }
    float getIntegratedLufs() const noexcept { return meter_.getIntegratedLufs(); }

    // For debugging / teaching: the 10% and 95% positions from the most recent computation
    float getLowLufs() const noexcept { return lowLufs_; }
    float getHighLufs() const noexcept { return highLufs_; }

private:
    static constexpr int kFullShortTermSteps = 30; // 3 s / 100 ms
    static constexpr float kMinLufs = -70.0f;
    static constexpr float kStepLu = 0.1f;
    static constexpr int kBins = 800; // [-70, +10) LUFS

    static float binCentre(int b) noexcept { return kMinLufs + (static_cast<float>(b) + 0.5f) * kStepLu; }

    float computeLra() noexcept {
        // TODO:
        //   1. Relative gate: sum the "power" of every histogram bin (count × LoudnessMeter::lufsToPower(binCentre(b))),
        //      divide by the total count to get the average power, convert back to LUFS with powerToLufs(), and add
        //      kRelativeGateLu (-20). Note it is a "power average", not a dB average.
        //   2. Find the first bin whose binCentre >= gate and compute the total count that passes the gate, gated.
        //   3. Percentile: accumulate counts starting from that bin; the centre of the first bin where the running count
        //      reaches >= ceil(p · gated) is the p-th percentile.
        //      lowLufs_ = 10%, highLufs_ = 95%; return highLufs_ − lowLufs_.
        lowLufs_ = 0.0f;
        highLufs_ = 0.0f;
        return 0.0f;
    }

    LoudnessMeter meter_;
    std::vector<uint32_t> histogramCount_;
    uint32_t lastUpdate_ = 0;
    float lowLufs_ = 0.0f, highLufs_ = 0.0f;
    std::atomic<float> lra_{0.0f};
};

float measureLra(LoudnessRangeMeter& meter, AudioBuffer& audio, float* integrated = nullptr) {
    meter.reset();
    for (int start = 0; start < audio.getNumSamples(); start += kBlockSize) {
        const int len = std::min(kBlockSize, audio.getNumSamples() - start);
        meter.process(AudioBlock(audio).getSubBlock(start, len));
    }
    if (integrated)
        *integrated = meter.getIntegratedLufs();
    return meter.getLoudnessRangeLu();
}

} // namespace

int main() {
    LoudnessRangeMeter meter;
    meter.prepare(kSampleRate, 2);

    // ---- EBU Tech 3342-style test: two 20 s sections of a stereo 1 kHz sine ----------------------------
    struct Case {
        float levelA, levelB, expectedLra;
    };
    const Case cases[] = {{-20.0f, -30.0f, 10.0f}, {-20.0f, -15.0f, 5.0f}, {-40.0f, -20.0f, 20.0f}};
    std::printf("%10s %10s %10s %12s %12s %12s\n", "A (dBFS)", "B (dBFS)", "LRA (LU)", "10% (LUFS)", "95% (LUFS)",
                "integrated");
    for (const Case& c : cases) {
        AudioBuffer audio = makeToneBursts(1000.0, kSampleRate, {c.levelA, c.levelB}, 20.0, 2);
        float integrated = 0.0f;
        const float lra = measureLra(meter, audio, &integrated);
        std::printf("%10.1f %10.1f %10.2f %12.2f %12.2f %12.2f\n", c.levelA, c.levelB, lra, meter.getLowLufs(),
                    meter.getHighLufs(), integrated);
        DAWLAB_CHECK_NEAR(lra, c.expectedLra, 1.0);
    }

    // ---- Constant level -> LRA ≈ 0 ----------------------------------------------------------------
    {
        AudioBuffer steady = makeSine(1000.0, kSampleRate, 10.0, dbToGain(-20.0f), 2);
        const float lra = measureLra(meter, steady);
        std::printf("\nConstant -20 dBFS sine for 10 s -> LRA = %.2f LU (expected 0)\n", lra);
        DAWLAB_CHECK_NEAR(lra, 0.0, 0.2);
    }

    // ---- A long stretch of silence in the middle: the absolute gate keeps it from affecting the LRA --------
    {
        AudioBuffer withSilence = makeToneBursts(1000.0, kSampleRate, {-20.0f, -200.0f, -30.0f}, 20.0, 2);
        const float lra = measureLra(meter, withSilence);
        std::printf("-20 dBFS 20 s -> silence 20 s -> -30 dBFS 20 s: LRA = %.2f LU (the silence is blocked by the absolute gate, still about 10)\n",
                    lra);
        DAWLAB_CHECK_NEAR(lra, 10.0, 1.0);
    }

    // ---- A very quiet section (30 LU below the main body): the relative gate excludes it --------------------
    {
        AudioBuffer quietTail = makeToneBursts(1000.0, kSampleRate, {-20.0f, -20.0f, -50.0f}, 10.0, 2);
        const float lra = measureLra(meter, quietTail);
        std::printf("-20 dBFS 20 s -> -50 dBFS 10 s: LRA = %.2f LU (the -50 section is more than 20 LU below the relative gate and is excluded; "
                    "only a few values in the transition region remain)\n",
                    lra);
        DAWLAB_CHECK(lra < 5.0f);
    }

    return dawlab::test::summary("l11_ex5");
}
