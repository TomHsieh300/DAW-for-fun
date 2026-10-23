// Exercise 8 solution: write your own transient shaper and verify that it is "independent of level"
//
// What it shows:
//   1. mapToGainDb(): the d -> gain mapping from README section 2.6.2 (saturating at |d| = 6 dB)
//   2. nextGain(): fast / slow EnvelopeFollowers -> d -> mapping -> 0.5 ms one-pole smoothing
//   3. A sample-by-sample comparison against dawlab::dsp::TransientShaper (should be identical)
//   4. A level-independence test: the same drums are fed in with peaks of -6 dBFS and -26 dBFS
//        attack +6 dB: the peak increase is equal for both (+/- 0.3 dB)
//        sustain -12 dB: the tail RMS drops for both, and the drop is equal
//      Control: a compressor (threshold -20 dB, 4:1) changes the peak very differently at the two levels -> it
//      looks at the absolute level
// How to run: l08_ex8_solution (no arguments needed; it uses makeDrumLoop() to generate the drum loop)
// Output files: dawlab_output/l08_ex8_loud_dry.wav, l08_ex8_loud_attack6.wav, l08_ex8_loud_sustain-12.wav,
//          l08_ex8_quiet_dry.wav, l08_ex8_quiet_attack6.wav, l08_ex8_quiet_sustain-12.wav
// What to listen for: the quiet version is 20 dB quieter overall, but the "effect" of attack +6 / sustain -12 sounds
//            the same as in the loud version.

#include <dawlab/Common.h>
#include <dawlab/dsp/Compressor.h>
#include <dawlab/dsp/EnvelopeFollower.h>
#include <dawlab/dsp/TransientShaper.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

using namespace dawlab;
using dawlab::dsp::DetectorMode;
using dawlab::dsp::EnvelopeFollower;

namespace {

constexpr int kBlockSize = 512;
constexpr double kSampleRate = 48000.0;

class MyTransientShaper {
public:
    static constexpr float kFastAttackMs = 0.5f;
    static constexpr float kFastReleaseMs = 30.0f;
    static constexpr float kSlowAttackMs = 10.0f;
    static constexpr float kSlowReleaseMs = 150.0f;
    static constexpr float kFullScaleDb = 6.0f;
    static constexpr float kFloorDb = -80.0f;
    static constexpr float kGainSmoothMs = 0.5f;

    // README 2.6.2:
    //   d > 0:  gainDb = attackDb  * min(d / 6, 1)
    //   d <= 0: gainDb = sustainDb * min(-d / 6, 1)
    static float mapToGainDb(float d, float attackDb, float sustainDb) noexcept {
        if (d > 0.0f)
            return attackDb * std::min(d / kFullScaleDb, 1.0f);
        return sustainDb * std::min(-d / kFullScaleDb, 1.0f);
    }

    void prepare(double sampleRate, int /*maxBlockSize*/, int /*numChannels*/) {
        fast_.prepare(sampleRate);
        fast_.setMode(DetectorMode::Peak);
        fast_.setAttackMs(kFastAttackMs);
        fast_.setReleaseMs(kFastReleaseMs);
        slow_.prepare(sampleRate);
        slow_.setMode(DetectorMode::Peak);
        slow_.setAttackMs(kSlowAttackMs);
        slow_.setReleaseMs(kSlowReleaseMs);
        // one-pole smoothing coefficient alpha = e^{-1/(tau*fs)}
        smoothCoef_ = static_cast<float>(std::exp(-1.0 / (kGainSmoothMs * 0.001 * sampleRate)));
        reset();
    }
    void reset() noexcept {
        fast_.reset();
        slow_.reset();
        gainDb_ = 0.0f;
    }
    void setAttackDb(float db) noexcept { attackDb_ = std::clamp(db, -24.0f, 24.0f); }
    void setSustainDb(float db) noexcept { sustainDb_ = std::clamp(db, -24.0f, 24.0f); }

    float nextGain(float detectorInput) noexcept {
        const float fast = fast_.processSample(detectorInput);
        const float slow = slow_.processSample(detectorInput);
        const float slowDb = gainToDb(slow);
        float target = 0.0f;
        if (slowDb > kFloorDb) {
            // d = 20*log10(fast / slow): it only looks at the ratio, so scaling the whole signal up or down leaves d unchanged
            const float d = gainToDb(fast) - slowDb;
            target = mapToGainDb(d, attackDb_, sustainDb_);
        }
        gainDb_ = target + smoothCoef_ * (gainDb_ - target); // smooth the gain (in the dB domain)
        return dbToGain(gainDb_);
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
    EnvelopeFollower fast_, slow_;
    float attackDb_ = 0.0f;
    float sustainDb_ = 0.0f;
    float smoothCoef_ = 0.0f;
    float gainDb_ = 0.0f;
};

template <typename Processor>
AudioBuffer runBlocks(const AudioBuffer& input, Processor& p) {
    AudioBuffer out = input;
    AudioBlock whole(out);
    const int n = out.getNumSamples();
    for (int start = 0; start < n; start += kBlockSize)
        p.process(whole.getSubBlock(start, std::min(kBlockSize, n - start)));
    return out;
}

AudioBuffer runShaper(const AudioBuffer& input, float attackDb, float sustainDb) {
    MyTransientShaper ts;
    ts.prepare(kSampleRate, kBlockSize, input.getNumChannels());
    ts.setAttackDb(attackDb);
    ts.setSustainDb(sustainDb);
    return runBlocks(input, ts);
}

// Normalize the drum loop to the given peak (dBFS)
AudioBuffer scaledTo(const AudioBuffer& b, float peakDb) {
    AudioBuffer out = b;
    const float g = dbToGain(peakDb) / peakAbs(b);
    for (int ch = 0; ch < out.getNumChannels(); ++ch)
        for (int i = 0; i < out.getNumSamples(); ++i)
            out.setSample(ch, i, out.getSample(ch, i) * g);
    return out;
}

// "Tail": makeDrumLoop uses eighth notes (120 BPM -> 250 ms) as its grid; 120-240 ms of each cell is the tail of the previous hit
float tailRmsDb(const AudioBuffer& b) {
    const int step = secondsToSamples(0.25, kSampleRate);
    const int from = msToSamples(120.0, kSampleRate);
    const int to = msToSamples(240.0, kSampleRate);
    double sum = 0.0;
    long long count = 0;
    for (int s = 0; s + to <= b.getNumSamples(); s += step)
        for (int i = s + from; i < s + to; ++i) {
            const double x = b.getSample(0, i);
            sum += x * x;
            ++count;
        }
    return gainToDb(static_cast<float>(std::sqrt(sum / static_cast<double>(std::max(1LL, count)))));
}

float peakDb(const AudioBuffer& b) { return gainToDb(peakAbs(b)); }

} // namespace

int main() {
    std::printf("Chapter 08 Exercise 8 (solution): transient shaper and level independence\n");

    // ---- 1. Mapping function ---------------------------------------------------------------
    std::printf("\n[1] d -> gain mapping (attack +12, sustain -12)\n");
    DAWLAB_CHECK_NEAR(MyTransientShaper::mapToGainDb(3.0f, 12.0f, -12.0f), 6.0, 1.0e-5);
    DAWLAB_CHECK_NEAR(MyTransientShaper::mapToGainDb(6.0f, 12.0f, -12.0f), 12.0, 1.0e-5);
    DAWLAB_CHECK_NEAR(MyTransientShaper::mapToGainDb(20.0f, 12.0f, -12.0f), 12.0, 1.0e-5); // saturation
    DAWLAB_CHECK_NEAR(MyTransientShaper::mapToGainDb(-3.0f, 12.0f, -12.0f), -6.0, 1.0e-5);
    DAWLAB_CHECK_NEAR(MyTransientShaper::mapToGainDb(-20.0f, 12.0f, -12.0f), -12.0, 1.0e-5);
    DAWLAB_CHECK_NEAR(MyTransientShaper::mapToGainDb(0.0f, 12.0f, -12.0f), 0.0, 1.0e-6);
    DAWLAB_CHECK_NEAR(MyTransientShaper::mapToGainDb(-1.5f, 12.0f, 6.0f), 1.5, 1.0e-5); // positive sustain works too
    for (const float d : {-12.0f, -6.0f, -3.0f, 0.0f, 3.0f, 6.0f, 12.0f})
        std::printf("    d = %+6.1f dB -> %+6.2f dB\n", d, MyTransientShaper::mapToGainDb(d, 12.0f, -12.0f));

    const AudioBuffer drums = makeDrumLoop(kSampleRate);
    const AudioBuffer loud = scaledTo(drums, -6.0f);
    const AudioBuffer quiet = scaledTo(drums, -26.0f);

    // ---- 2. Compare against dsp::TransientShaper -------------------------------------------
    {
        dawlab::dsp::TransientShaper ref;
        ref.prepare(kSampleRate, kBlockSize, loud.getNumChannels());
        ref.setAttackDb(6.0f);
        ref.setSustainDb(-12.0f);
        const AudioBuffer expected = runBlocks(loud, ref);
        const AudioBuffer mine = runShaper(loud, 6.0f, -12.0f);
        float maxErr = 0.0f;
        for (int ch = 0; ch < mine.getNumChannels(); ++ch)
            for (int i = 0; i < mine.getNumSamples(); ++i)
                maxErr = std::max(maxErr, std::abs(mine.getSample(ch, i) - expected.getSample(ch, i)));
        std::printf("\n[2] Sample-by-sample comparison with dawlab::dsp::TransientShaper: maximum error %.3g\n", maxErr);
        DAWLAB_CHECK(maxErr < 1.0e-5f);
    }

    // ---- 3. Level independence -----------------------------------------------------
    const AudioBuffer loudAtk = runShaper(loud, 6.0f, 0.0f);
    const AudioBuffer quietAtk = runShaper(quiet, 6.0f, 0.0f);
    const AudioBuffer loudSus = runShaper(loud, 0.0f, -12.0f);
    const AudioBuffer quietSus = runShaper(quiet, 0.0f, -12.0f);

    writeWav(outputPath("l08_ex8_loud_dry.wav"), loud, kSampleRate, 32);
    writeWav(outputPath("l08_ex8_loud_attack6.wav"), loudAtk, kSampleRate, 32);
    writeWav(outputPath("l08_ex8_loud_sustain-12.wav"), loudSus, kSampleRate, 32);
    writeWav(outputPath("l08_ex8_quiet_dry.wav"), quiet, kSampleRate, 32);
    writeWav(outputPath("l08_ex8_quiet_attack6.wav"), quietAtk, kSampleRate, 32);
    writeWav(outputPath("l08_ex8_quiet_sustain-12.wav"), quietSus, kSampleRate, 32);

    const float loudPeakGain = peakDb(loudAtk) - peakDb(loud);
    const float quietPeakGain = peakDb(quietAtk) - peakDb(quiet);
    const float loudTailDrop = tailRmsDb(loudSus) - tailRmsDb(loud);
    const float quietTailDrop = tailRmsDb(quietSus) - tailRmsDb(quiet);

    std::printf("\n[3] Level independence (the same drums, with peaks at -6 dBFS and -26 dBFS)\n");
    std::printf("                         loud(-6)   quiet(-26)\n");
    std::printf("    attack +6: peak increase   %+6.2f     %+6.2f dB\n", loudPeakGain, quietPeakGain);
    std::printf("    sustain -12: tail RMS      %+6.2f     %+6.2f dB\n", loudTailDrop, quietTailDrop);
    DAWLAB_CHECK(loudPeakGain > 3.0f);                        // attack really does push the peak up
    DAWLAB_CHECK_NEAR(loudPeakGain, quietPeakGain, 0.3);      // and it is independent of level
    DAWLAB_CHECK(loudTailDrop < -3.0f);                       // sustain -12 makes the tail smaller
    DAWLAB_CHECK(quietTailDrop < -3.0f);
    DAWLAB_CHECK_NEAR(loudTailDrop, quietTailDrop, 0.3);

    // ---- 4. Control: a compressor looks at the absolute level ------------------------------------------
    auto compressPeakChange = [](const AudioBuffer& in) {
        dawlab::dsp::Compressor c;
        c.prepare(kSampleRate, kBlockSize, in.getNumChannels());
        c.setThresholdDb(-20.0f);
        c.setRatio(4.0f);
        c.setAttackMs(1.0f);
        c.setReleaseMs(100.0f);
        const AudioBuffer out = runBlocks(in, c);
        return peakDb(out) - peakDb(in);
    };
    const float loudComp = compressPeakChange(loud);
    const float quietComp = compressPeakChange(quiet);
    std::printf("\n[4] Control: peak change of a compressor (threshold -20, 4:1)\n");
    std::printf("    loud %+6.2f dB, quiet %+6.2f dB -> difference %.2f dB: the compressor only affects the loud version\n", loudComp,
                quietComp, std::abs(loudComp - quietComp));
    DAWLAB_CHECK(std::abs(loudComp - quietComp) > 3.0f);

    std::printf("\nOutput: dawlab_output/l08_ex8_{loud,quiet}_{dry,attack6,sustain-12}.wav\n");
    return dawlab::test::summary("l08_ex8_transient_shaper");
}
