// Exercise 4 solution: add modulation to an FDN and quantify metallic ringing
//
// We write our own 4-line FDN (smaller than the 8-line version in Reverb.h, so it is easier to see):
//
//   v_i = output of delay_i (delay L_i + depth*sin(phi + i*pi/2) samples, interpolated with readCubic)
//   u   = A*v, A = I - (2/N)*1*1^T (Householder; A^T A = I -> lossless)
//   delay_i <- g_i * lowpass_i(u_i) + x        g_i = 10^(-3*L_i/(RT60*fs))
//   y   = v_0 - v_1 + v_2 - v_3
//
// Quantifying "metallic ringing": the modal density of a 4-line FDN is only sum(L_i)/fs ~ 0.14 modes/Hz
// (one mode every 7 Hz), so the spectrum of the tail is a set of separate sharp peaks -> it sounds pitched,
// like striking a metal pipe. If the delay lengths are modulated slowly, the mode frequencies drift over
// time and the long-term average spectrum becomes smooth. Here we use the "standard deviation of the tail
// spectrum (in dB) over 100 Hz .. 6 kHz" as the roughness metric; with modulation on it should drop clearly.
//
// Output: dawlab_output/l12_ex4_fdn_static.wav, l12_ex4_fdn_modulated.wav (both impulse responses)

#include <dawlab/Common.h>
#include <dawlab/dsp/Fft.h>
#include <dawlab/dsp/FractionalDelayLine.h>
#include <dawlab/dsp/OnePole.h>
#include <dawlab/dsp/ReverbBuildingBlocks.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace dawlab;
using namespace dawlab::dsp;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

class Fdn4 {
public:
    static constexpr int kN = 4;
    static constexpr float kMaxDepthMs = 4.0f;

    void prepare(double sampleRate) {
        sampleRate_ = sampleRate;
        const int maxDepth = msToSamples(kMaxDepthMs, sampleRate);
        int maxDelay = 0;
        for (int i = 0; i < kN; ++i) {
            // Convert to samples and take the next prime -> the lengths are mutually coprime
            delay_[static_cast<size_t>(i)] = static_cast<float>(nextPrime(msToSamples(kBaseMs[static_cast<size_t>(i)], sampleRate)));
            maxDelay = std::max(maxDelay, static_cast<int>(delay_[static_cast<size_t>(i)]));
        }
        lines_.prepare(maxDelay + maxDepth + 4, kN);
        damping_.prepare(sampleRate, 1, kN);
        damping_.setType(OnePole::Type::Lowpass);
        setDampingHz(dampingHz_);
        setDecaySeconds(decaySeconds_);
        setModulation(depthMs_, rateHz_);
        reset();
    }

    void reset() noexcept {
        lines_.reset();
        damping_.reset();
        phase_ = 0.0f;
    }

    void setDecaySeconds(float rt60) noexcept {
        decaySeconds_ = rt60;
        for (int i = 0; i < kN; ++i)
            gain_[static_cast<size_t>(i)] =
                lossless_ ? 1.0f : CombFilter::feedbackForRt60(static_cast<int>(delay_[static_cast<size_t>(i)]), rt60, sampleRate_);
    }
    // g_i = 1 and no damping: used to verify that the Householder matrix really is lossless
    void setLossless(bool on) noexcept {
        lossless_ = on;
        setDecaySeconds(decaySeconds_);
    }
    // >= 20 kHz is treated as damping off (even with a 20 kHz cutoff the one-pole is still -1.3 dB at Nyquist,
    // and that is applied once per trip around the loop, so it accumulates and clearly shortens the RT60 of the highs)
    void setDampingHz(float hz) noexcept {
        dampingHz_ = hz;
        dampingEnabled_ = hz < 20000.0f;
        damping_.setCutoff(std::min(hz, static_cast<float>(sampleRate_ * 0.45)));
    }
    void setModulation(float depthMs, float rateHz) noexcept {
        depthMs_ = std::clamp(depthMs, 0.0f, kMaxDepthMs);
        rateHz_ = rateHz;
        depth_ = static_cast<float>(depthMs_ * 0.001 * sampleRate_);
        increment_ = static_cast<float>(kTwoPi * rateHz / sampleRate_);
    }
    float getDelaySamples(int i) const noexcept { return delay_[static_cast<size_t>(i)]; }

    float processSample(float x) noexcept {
        // 1. Read the 4 lines (with modulation: the LFO phases of the lines are 90 degrees apart)
        phase_ += increment_;
        if (phase_ >= static_cast<float>(kTwoPi))
            phase_ -= static_cast<float>(kTwoPi);
        std::array<float, kN> v{};
        float sum = 0.0f;
        for (int i = 0; i < kN; ++i) {
            const float mod = depth_ > 0.0f ? depth_ * std::sin(phase_ + static_cast<float>(i) * 1.5707964f) : 0.0f;
            // Read before write: readCubic(d) returns the value written d+1 samples ago, so reading L_i - 1 makes the loop length exactly L_i
            v[static_cast<size_t>(i)] = lines_.readCubic(i, delay_[static_cast<size_t>(i)] - 1.0f + mod);
            sum += v[static_cast<size_t>(i)];
        }
        // 2. Householder: u_i = v_i - (2/N)*sum(v)
        const float scale = 2.0f / static_cast<float>(kN);
        for (int i = 0; i < kN; ++i) {
            float u = v[static_cast<size_t>(i)] - scale * sum;
            if (!lossless_ && dampingEnabled_)
                u = damping_.processSample(i, u);
            u = u * gain_[static_cast<size_t>(i)] + x;
            lines_.push(i, reverb_detail::flushDenormal(u));
        }
        // 3. Output: sum with alternating signs
        return v[0] - v[1] + v[2] - v[3];
    }

private:
    static constexpr std::array<double, kN> kBaseMs = {23.4, 32.5, 40.6, 46.9};

    double sampleRate_ = 48000.0;
    FractionalDelayLine lines_;
    OnePole damping_;
    std::array<float, kN> delay_{};
    std::array<float, kN> gain_{};
    float decaySeconds_ = 2.0f;
    float dampingHz_ = 20000.0f;
    bool lossless_ = false;
    bool dampingEnabled_ = false;
    float depthMs_ = 0.0f, rateHz_ = 1.0f, depth_ = 0.0f, increment_ = 0.0f, phase_ = 0.0f;
};

AudioBuffer impulseResponse(Fdn4& fdn, double seconds) {
    AudioBuffer ir = makeImpulse(secondsToSamples(seconds, kSampleRate), 1);
    fdn.reset();
    float* d = ir.getWritePointer(0);
    for (int start = 0; start < ir.getNumSamples(); start += kBlockSize) {
        const int len = std::min(kBlockSize, ir.getNumSamples() - start);
        for (int i = 0; i < len; ++i)
            d[start + i] = fdn.processSample(d[start + i]);
    }
    return ir;
}

// "Roughness" of the tail spectrum: Hann window + FFT, take the bins in fLo..fHi, and compute the standard deviation of the magnitude (dB)
double spectralRoughnessDb(const float* x, int fftOrder, double fLo, double fHi) {
    Fft fft(fftOrder);
    const int n = fft.getSize();
    std::vector<float> window;
    Fft::makeWindow(window, n, WindowType::Hann);
    std::vector<float> frame(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        frame[static_cast<size_t>(i)] = x[i] * window[static_cast<size_t>(i)];
    std::vector<std::complex<float>> spec(static_cast<size_t>(fft.getNumRealBins()));
    fft.forwardReal(frame.data(), spec.data());

    const double binHz = kSampleRate / n;
    std::vector<double> db;
    for (int k = static_cast<int>(fLo / binHz); k <= static_cast<int>(fHi / binHz); ++k)
        db.push_back(gainToDb(std::max(1e-12, static_cast<double>(std::abs(spec[static_cast<size_t>(k)])))));
    double mean = 0.0;
    for (double v : db)
        mean += v;
    mean /= static_cast<double>(db.size());
    double var = 0.0;
    for (double v : db)
        var += (v - mean) * (v - mean);
    return std::sqrt(var / static_cast<double>(db.size()));
}

} // namespace

int main() {
    std::printf("=== Exercise 4: FDN modulation and metallic ringing ===\n");

    Fdn4 fdn;
    fdn.prepare(kSampleRate);
    std::printf("Delay-line lengths (samples): ");
    for (int i = 0; i < Fdn4::kN; ++i)
        std::printf("%s%.0f", i ? ", " : "", fdn.getDelaySamples(i));
    std::printf(" (all prime)\n");

    // ---- 1. Householder losslessness check: g = 1, no damping, no modulation -> the energy does not decay -----------
    {
        fdn.setLossless(true);
        fdn.setModulation(0.0f, 1.0f);
        AudioBuffer ir = impulseResponse(fdn, 3.0);
        const int w = secondsToSamples(0.5, kSampleRate);
        const float e1 = rms(ir.getReadPointer(0) + secondsToSamples(1.0, kSampleRate), w);
        const float e2 = rms(ir.getReadPointer(0) + secondsToSamples(2.5, kSampleRate), w);
        std::printf("lossless: RMS @1.0s = %.4f, @2.5s = %.4f (difference %+.2f dB, should be close to 0)\n", e1, e2,
                    gainToDb(e2 / e1));
        DAWLAB_CHECK_NEAR(gainToDb(e2 / e1), 0.0, 1.0);
        DAWLAB_CHECK(!hasNanOrInf(ir));
        fdn.setLossless(false);
    }

    // ---- 2. RT60 verification (no modulation) ----------------------------------------
    fdn.setDecaySeconds(2.0f);
    fdn.setDampingHz(20000.0f);
    AudioBuffer irStatic = impulseResponse(fdn, 2.6);
    {
        const DecayMeasurement m = measureRt60(irStatic.getReadPointer(0), irStatic.getNumSamples(), kSampleRate, 20.0);
        std::printf("RT60 set to 2.0 s -> measured %.3f s\n", m.rt60Seconds);
        DAWLAB_CHECK(m.valid);
        DAWLAB_CHECK_NEAR(m.rt60Seconds, 2.0, 0.3);
    }

    // ---- 3. Effect of modulation on the tail spectrum --------------------------------
    fdn.setModulation(1.0f, 1.3f);
    AudioBuffer irMod = impulseResponse(fdn, 2.6);
    {
        const int start = secondsToSamples(0.4, kSampleRate);
        const double rStatic = spectralRoughnessDb(irStatic.getReadPointer(0) + start, 14, 100.0, 6000.0);
        const double rMod = spectralRoughnessDb(irMod.getReadPointer(0) + start, 14, 100.0, 6000.0);
        std::printf("Std. dev. of the tail spectrum in dB (from 0.4 s, 16384-point FFT, 100 Hz..6 kHz): static %.2f dB, modulated %.2f dB\n",
                    rStatic, rMod);
        DAWLAB_CHECK(rMod < rStatic - 1.0);
        // Modulation should not noticeably change the overall decay
        const DecayMeasurement m = measureRt60(irMod.getReadPointer(0), irMod.getNumSamples(), kSampleRate, 20.0);
        std::printf("Measured RT60 of the modulated version: %.3f s\n", m.rt60Seconds);
        DAWLAB_CHECK_NEAR(m.rt60Seconds, 2.0, 0.4);
        DAWLAB_CHECK(!hasNanOrInf(irMod));
    }
    writeWav(outputPath("l12_ex4_fdn_static.wav"), irStatic, kSampleRate);
    writeWav(outputPath("l12_ex4_fdn_modulated.wav"), irMod, kSampleRate);

    // ---- 4. Too-deep modulation: pitch wobble (numbers only; listen for yourself) ---------
    {
        fdn.setModulation(4.0f, 1.3f);
        AudioBuffer deep = impulseResponse(fdn, 2.6);
        const int start = secondsToSamples(0.4, kSampleRate);
        // Maximum pitch deviation = 2*pi*f_LFO*depth: 1 ms -> 0.82% (about 14 cents); 4 ms -> 3.3% (about 56 cents)
        const double maxPitchDev = kTwoPi * 1.3 * 0.004;
        std::printf("depth 4 ms: spectrum std. dev. %.2f dB (flatter, but the maximum pitch deviation is %.1f%% = %.0f cents -> audible pitch wobble)\n",
                    spectralRoughnessDb(deep.getReadPointer(0) + start, 14, 100.0, 6000.0), maxPitchDev * 100.0,
                    1200.0 * std::log2(1.0 + maxPitchDev));
    }

    return dawlab::test::summary("l12_ex4");
}
