// Exercise 4 starter: add modulation to an FDN and quantify metallic ringing
//
// Goals:
//   1. Complete Fdn4::processSample():
//        v_i = output of delay line i (delay L_i + depth*sin(phi + i*pi/2), read with readCubic)
//        u   = Householder matrix A = I - (2/N)*1*1^T applied to v, i.e. u_i = v_i - (2/N)*sum_j v_j
//        delay_i <- g_i * damping_i(u_i) + x
//        y   = v_0 - v_1 + v_2 - v_3
//   2. Complete spectralRoughnessDb(): the standard deviation of the tail's spectrum (in dB) over fLo..fHi.
//   3. Observe: with modulation on, the standard deviation drops clearly (the modes get smoothed out) while
//      the RT60 stays the same; too large a depth causes pitch wobble.
//
// At run time the self-checks print FAIL; after you complete the TODOs they should all PASS.

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
        // TODO:
        //   1. phase_ += increment_ (wrap to 2*pi)
        //   2. read the 4 lines v_i (with modulation: lines_.readCubic(i, delay_[i] - 1 + depth_*sin(phase_ + i*pi/2)))
        //      the "-1" is there because we read before we write: after the previous push, readCubic(d) returns
        //      the value from d+1 samples ago
        //   3. Householder: u_i = v_i - (2/N)*sum v_j; when lossless_ is false and dampingEnabled_ is true,
        //      also pass it through damping_.processSample(i, u_i)
        //   4. lines_.push(i, u_i*gain_[i] + x) (remember reverb_detail::flushDenormal)
        //   5. return v_0 - v_1 + v_2 - v_3
        (void)x;
        return 0.0f;
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

double spectralRoughnessDb(const float* x, int fftOrder, double fLo, double fHi) {
    // TODO: Hann window -> Fft::forwardReal -> take the magnitude in dB of the bins in fLo..fHi -> return the standard deviation
    (void)x;
    (void)fftOrder;
    (void)fLo;
    (void)fHi;
    return 0.0;
}

} // namespace

int main() {
    std::printf("=== Exercise 4: FDN modulation and metallic ringing (starter) ===\n");

    Fdn4 fdn;
    fdn.prepare(kSampleRate);

    // 1. Lossless (g = 1, no damping): the energy does not decay
    {
        fdn.setLossless(true);
        fdn.setModulation(0.0f, 1.0f);
        AudioBuffer ir = impulseResponse(fdn, 3.0);
        const int w = secondsToSamples(0.5, kSampleRate);
        const float e1 = rms(ir.getReadPointer(0) + secondsToSamples(1.0, kSampleRate), w);
        const float e2 = rms(ir.getReadPointer(0) + secondsToSamples(2.5, kSampleRate), w);
        std::printf("lossless: RMS @1.0s = %.4f, @2.5s = %.4f\n", e1, e2);
        DAWLAB_CHECK(e1 > 0.0f);
        DAWLAB_CHECK_NEAR(gainToDb(e2 / e1), 0.0, 1.0);
        fdn.setLossless(false);
    }

    // 2. RT60
    fdn.setDecaySeconds(2.0f);
    AudioBuffer irStatic = impulseResponse(fdn, 2.6);
    {
        const DecayMeasurement m = measureRt60(irStatic.getReadPointer(0), irStatic.getNumSamples(), kSampleRate, 20.0);
        std::printf("RT60 set to 2.0 s -> measured %.3f s\n", m.rt60Seconds);
        DAWLAB_CHECK(m.valid);
        DAWLAB_CHECK_NEAR(m.rt60Seconds, 2.0, 0.3);
    }

    // 3. Modulation flattens the tail's spectrum
    fdn.setModulation(1.0f, 1.3f);
    AudioBuffer irMod = impulseResponse(fdn, 2.6);
    {
        const int start = secondsToSamples(0.4, kSampleRate);
        const double rStatic = spectralRoughnessDb(irStatic.getReadPointer(0) + start, 14, 100.0, 6000.0);
        const double rMod = spectralRoughnessDb(irMod.getReadPointer(0) + start, 14, 100.0, 6000.0);
        std::printf("Standard deviation of the tail spectrum in dB: static %.2f dB, modulated %.2f dB\n", rStatic, rMod);
        DAWLAB_CHECK(rStatic > 0.0);
        DAWLAB_CHECK(rMod < rStatic - 1.0);
    }
    writeWav(outputPath("l12_ex4_fdn_static.wav"), irStatic, kSampleRate);
    writeWav(outputPath("l12_ex4_fdn_modulated.wav"), irMod, kSampleRate);

    return dawlab::test::summary("l12_ex4_starter");
}
