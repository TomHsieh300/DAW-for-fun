// Exercise 5 solution: Stereo chorus -- one LFO per side with a 90° phase offset; measure the left/right correlation
//
// A mono chorus copied to both sides still has the image dead center (left and right identical,
// correlation = 1). Offset the left and right LFOs by 0.25 cycle (90°; the right channel's start phase = 0.25):
//   left: d_L(t) = d0 + D·sin(2πrt)         right: d_R(t) = d0 + D·sin(2πrt + π/2) = d0 + D·cos(2πrt)
// The left and right delays (and therefore the direction and amount of detuning) are out of sync
// -> the two ears no longer hear the same signal -> the image widens.
//
// Why is 90° so common? For broadband material like white noise, the correlation at both 90° and
// 180° is close to 0 (measured below); the difference is in the "dynamics": at 180° one side always
// has the longest delay while the other has the shortest, the detuning directions are exactly
// opposite, and the image is pulled symmetrically back and forth between left and right; at 90°
// left and right take turns leading, which sounds like a more even, rotating width. This is an
// aesthetic choice, not a mathematical optimum.
//
// Measurement: the input is mono material with identical left and right (white noise copied to
// both sides), and the output's Pearson correlation coefficient is
//   ρ = Σ L·R / sqrt(Σ L² · Σ R²)
// self-check:
//   1. phase offset 0 -> ρ > 0.999 (left and right identical)
//   2. phase offset 0.25 (90°), mix 1 -> ρ < 0.5 (purely wet signal, clearly different left and right)
//   3. phase offset 0.25, mix 0.5 -> ρ between the two above (the dry signal, identical on both sides, pulls the correlation up)
//   3b. The offset really is 90°: rate 0, impulse input; left delay = centre (15 ms), right = centre + depth (18 ms).
//       (The white-noise ρ is insensitive to the phase offset -- any non-zero offset drives ρ close to 0, so items 2, 3 and 5 cannot tell 0.25 from 0.5.)
//   4. Mono compatibility: the RMS of (L + R)/2 relative to a single channel's RMS ≈ −1.25 dB (theory below), and not below −3 dB
//   5. Agrees with the Chapter 10 Chorus (setStereoSpread(1), 1 voice) (correlation difference < 0.05)
// Output: dawlab_output/l10_ex5_stereo_chorus.wav (plucked arpeggio)

#include <dawlab/Common.h>
#include <dawlab/dsp/Chorus.h>
#include <dawlab/dsp/FractionalDelayLine.h>
#include <dawlab/dsp/Lfo.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

using dawlab::dsp::Chorus;
using dawlab::dsp::FractionalDelayLine;
using dawlab::dsp::Lfo;
using dawlab::dsp::LfoShape;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

class StereoChorus {
public:
    static constexpr float kMaxDelayMs = 50.0f;

    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        (void)maxBlockSize;
        (void)numChannels; // always processes two channels
        sampleRate_ = sampleRate;
        delay_.prepare(dawlab::msToSamples(kMaxDelayMs, sampleRate) + 8, 2);
        for (Lfo& l : lfo_)
            l.prepare(sampleRate);
        reset();
    }

    void reset() noexcept {
        delay_.reset();
        lfo_[0].setPhase(0.0f);
        lfo_[1].setPhase(phaseOffset_); // the right channel's LFO starts at the offset -> the two LFOs always differ by `offset` cycles
        for (Lfo& l : lfo_)
            l.reset();
    }

    void setRateHz(float hz) noexcept {
        for (Lfo& l : lfo_)
            l.setRateHz(hz);
    }
    void setDepthMs(float ms) noexcept { depthMs_ = std::clamp(ms, 0.0f, 20.0f); }
    void setCentreDelayMs(float ms) noexcept { centreMs_ = std::clamp(ms, 1.0f, kMaxDelayMs - 20.0f); }
    void setMix(float mix) noexcept { mix_ = std::clamp(mix, 0.0f, 1.0f); }
    // Phase offset between the left and right LFOs (0..1 cycle; 0.25 = 90°). Takes effect only after reset().
    void setPhaseOffset(float offset01) noexcept { phaseOffset_ = std::clamp(offset01, 0.0f, 1.0f); }

    void process(dawlab::AudioBlock block) noexcept {
        const int n = block.getNumSamples();
        const int nch = std::min(block.getNumChannels(), 2);
        const float toSamples = static_cast<float>(sampleRate_ * 0.001);
        const float maxRead = static_cast<float>(delay_.getMaxDelaySamples() - 3);
        for (int i = 0; i < n; ++i) {
            for (int ch = 0; ch < nch; ++ch) {
                float* d = block.getChannelPointer(ch);
                const float x = d[i];
                delay_.push(ch, x);
                const float delayMs = centreMs_ + depthMs_ * lfo_[ch].getNextValue();
                const float wet = delay_.readCubic(ch, std::clamp(delayMs * toSamples, 2.0f, maxRead));
                d[i] = (1.0f - mix_) * x + mix_ * wet;
            }
        }
    }

private:
    double sampleRate_ = 48000.0;
    float depthMs_ = 3.0f, centreMs_ = 15.0f, mix_ = 0.5f, phaseOffset_ = 0.25f;
    FractionalDelayLine delay_;
    Lfo lfo_[2];
};

template <typename Effect>
void processInBlocks(Effect& fx, dawlab::AudioBuffer& buffer) {
    dawlab::AudioBlock whole(buffer);
    for (int start = 0; start < buffer.getNumSamples(); start += kBlockSize) {
        const int n = std::min(kBlockSize, buffer.getNumSamples() - start);
        fx.process(whole.getSubBlock(start, n));
    }
}

double correlation(const dawlab::AudioBuffer& b, int skip) {
    const float* l = b.getReadPointer(0);
    const float* r = b.getReadPointer(1);
    double sll = 0.0, srr = 0.0, slr = 0.0;
    for (int i = skip; i < b.getNumSamples(); ++i) {
        sll += double(l[i]) * l[i];
        srr += double(r[i]) * r[i];
        slr += double(l[i]) * r[i];
    }
    return (sll > 0.0 && srr > 0.0) ? slr / std::sqrt(sll * srr) : 0.0;
}

dawlab::AudioBuffer dualMono(double seconds, uint32_t seed) {
    dawlab::AudioBuffer mono = dawlab::makeWhiteNoise(kSampleRate, seconds, 0.25f, 1, seed);
    dawlab::AudioBuffer b(2, mono.getNumSamples());
    b.copyFrom(0, 0, mono, 0, 0, mono.getNumSamples());
    b.copyFrom(1, 0, mono, 0, 0, mono.getNumSamples());
    return b;
}

double runAndCorrelate(float phaseOffset, float mix, dawlab::AudioBuffer* keep = nullptr) {
    dawlab::AudioBuffer in = dualMono(3.0, 21u);
    StereoChorus c;
    c.prepare(kSampleRate, kBlockSize, 2);
    c.setRateHz(0.8f);
    c.setDepthMs(3.0f);
    c.setCentreDelayMs(15.0f);
    c.setMix(mix);
    c.setPhaseOffset(phaseOffset);
    c.reset();
    processInBlocks(c, in);
    if (keep)
        *keep = in;
    return correlation(in, dawlab::secondsToSamples(0.5, kSampleRate));
}

// Direct verification of the phase offset: rate 0 parks the LFOs at their start phases, and an
// impulse is sent in. The position of the output impulse is that channel's delay: d = centre + depth·sin(2π·phase).
// (The white-noise ρ is insensitive to the size of the phase offset: any non-zero offset drives ρ
// close to 0, so it has to be measured separately.)
void impulsePeakDelaysMs(float phaseOffset, double& leftMs, double& rightMs) {
    dawlab::AudioBuffer b(2, dawlab::secondsToSamples(0.1, kSampleRate));
    b.setSample(0, 0, 1.0f);
    b.setSample(1, 0, 1.0f);
    StereoChorus c;
    c.prepare(kSampleRate, kBlockSize, 2);
    c.setRateHz(0.0f); // LFO frozen
    c.setDepthMs(3.0f);
    c.setCentreDelayMs(15.0f);
    c.setMix(1.0f);
    c.setPhaseOffset(phaseOffset);
    c.reset();
    processInBlocks(c, b);
    double ms[2] = {0.0, 0.0};
    for (int ch = 0; ch < 2; ++ch) {
        const float* d = b.getReadPointer(ch);
        int best = 0;
        for (int i = 1; i < b.getNumSamples(); ++i)
            if (std::abs(d[i]) > std::abs(d[best]))
                best = i;
        ms[ch] = best * 1000.0 / kSampleRate;
    }
    leftMs = ms[0];
    rightMs = ms[1];
}

} // namespace

int main() {
    std::printf("=== Chapter 10 Exercise 5: Stereo chorus ===\n\n");
    const int skip = dawlab::secondsToSamples(0.5, kSampleRate);

    // ---- 1-3. Phase offset vs correlation ---------------------------------------
    std::printf("White noise (left = right) -> StereoChorus, rate 0.8 Hz, depth 3 ms, centre 15 ms:\n");
    std::printf("   phase offset   mix   L/R correlation\n");
    const double rho0 = runAndCorrelate(0.0f, 1.0f);
    const double rho90wet = runAndCorrelate(0.25f, 1.0f);
    const double rho90half = runAndCorrelate(0.25f, 0.5f);
    const double rho180wet = runAndCorrelate(0.5f, 1.0f);
    std::printf("   0°             1.0   %.4f\n", rho0);
    std::printf("   90°            1.0   %.4f\n", rho90wet);
    std::printf("   90°            0.5   %.4f\n", rho90half);
    std::printf("   180°           1.0   %.4f\n", rho180wet);
    DAWLAB_CHECK(rho0 > 0.999);
    DAWLAB_CHECK(rho90wet < 0.5);
    DAWLAB_CHECK(rho90half > rho90wet && rho90half < rho0);
    std::printf("  -> White noise is almost uncorrelated at different delays, so the purely wet signal has a very low ρ; adding the dry signal back raises ρ again.\n\n");

    // ---- 3b. Is the phase offset really 90°? (measure the delay with an impulse) --
    {
        double lMs = 0.0, rMs = 0.0;
        impulsePeakDelaysMs(0.25f, lMs, rMs);
        std::printf("Impulse response with rate 0, phase offset 0.25: left %.3f ms, right %.3f ms\n", lMs, rMs);
        std::printf("  Theory: left centre + depth·sin(0) = 15.000 ms, right centre + depth·sin(π/2) = 18.000 ms\n\n");
        DAWLAB_CHECK_NEAR(lMs, 15.0, 0.05);
        DAWLAB_CHECK_NEAR(rMs, 18.0, 0.05); // offset 0.5 -> 15 ms, 0.05 -> 15.93 ms; both would FAIL
    }

    // ---- 4. Mono compatibility --------------------------------------------------
    {
        dawlab::AudioBuffer out;
        runAndCorrelate(0.25f, 0.5f, &out);
        dawlab::AudioBuffer monoSum(1, out.getNumSamples());
        for (int i = 0; i < out.getNumSamples(); ++i)
            monoSum.setSample(0, i, 0.5f * (out.getSample(0, i) + out.getSample(1, i)));
        const float sumRms = dawlab::rms(monoSum.getReadPointer(0) + skip, monoSum.getNumSamples() - skip);
        const float lrRms = 0.5f * (dawlab::rms(out.getReadPointer(0) + skip, out.getNumSamples() - skip) +
                                    dawlab::rms(out.getReadPointer(1) + skip, out.getNumSamples() - skip));
        // Theory: L = 0.5x + 0.5w_L, where x, w_L, w_R are mutually uncorrelated with equal power (σ²)
        //   P(L) = 0.25σ² + 0.25σ² = 0.5σ²
        //   P((L+R)/2) = P(0.5x + 0.25w_L + 0.25w_R) = 0.25σ² + 0.0625σ² · 2 = 0.375σ²
        //   ratio = 0.375 / 0.5 = 0.75 -> 10·log10(0.75) = −1.25 dB
        const float lossDb = dawlab::gainToDb(sumRms / lrRms);
        std::printf("Mono compatibility (90°, mix 0.5): RMS of (L+R)/2 relative to a single channel %+.2f dB (theory −1.25 dB)\n\n", lossDb);
        DAWLAB_CHECK_NEAR(lossDb, -1.25, 0.5);
        DAWLAB_CHECK(lossDb > -3.0f);
    }

    // ---- 5. Comparison with dsp::Chorus's stereo spread ---------------------------
    {
        dawlab::AudioBuffer in = dualMono(3.0, 21u);
        Chorus c;
        c.prepare(kSampleRate, kBlockSize, 2);
        c.setRateHz(0.8f);
        c.setDepthMs(3.0f);
        c.setCentreDelayMs(15.0f);
        c.setVoices(1);
        c.setMix(1.0f);
        c.setStereoSpread(1.0f); // the right channel's LFO is 90° off the left
        c.reset();
        processInBlocks(c, in);
        const double rhoLib = correlation(in, skip);
        std::printf("dsp::Chorus (1 voice, mix 1, spread 1): ρ = %.4f (your own 90° version: %.4f)\n\n", rhoLib, rho90wet);
        DAWLAB_CHECK_NEAR(rhoLib, rho90wet, 0.05);
    }

    // ---- Listening ---------------------------------------------------------------
    {
        dawlab::AudioBuffer pluck = dawlab::makePluckSequence(kSampleRate, 4.0, 0.4f, 2);
        StereoChorus c;
        c.prepare(kSampleRate, kBlockSize, 2);
        c.setRateHz(0.8f);
        c.setDepthMs(3.0f);
        c.setCentreDelayMs(15.0f);
        c.setMix(0.5f);
        c.setPhaseOffset(0.25f);
        c.reset();
        processInBlocks(c, pluck);
        dawlab::writeWav(dawlab::outputPath("l10_ex5_stereo_chorus.wav"), pluck, kSampleRate);
        std::printf("Output l10_ex5_stereo_chorus.wav: peak %.3f, L/R correlation %.3f\n", dawlab::peakAbs(pluck),
                    correlation(pluck, 0));
        DAWLAB_CHECK(!dawlab::hasNanOrInf(pluck));
    }

    return dawlab::test::summary("l10_ex5");
}
