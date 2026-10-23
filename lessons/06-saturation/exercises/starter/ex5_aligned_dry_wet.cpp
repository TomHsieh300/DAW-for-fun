// Exercise 5 (starter): latency-aligned dry/wet mix, and measuring the comb notches when misaligned.
//
//   x ──┬──> [4x oversampler: upsample -> tanh -> downsample] (delay L = 36 samples) ──> wet ──┐
//       └──> [dry delay: D samples] ─────────────────────────────────────────────────> dry ──┴─> (1-m)*dry + m*wet
//
// For small signals y[n] = (1 - m)*x[n - D] + m*x[n - L]; when D != L this is a comb filter.
// Once you have finished the TODOs, you are done when all the self-checks pass. The starter prints FAIL
// when run; that is expected.

#include <dawlab/Common.h>
#include <dawlab/dsp/Oversampler.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

using namespace dawlab;

constexpr double kSr = 48000.0;
constexpr int kBlock = 256;

// Integer-sample dry delay (ring buffer). With delay = 0 the output equals the input.
class DryDelay {
public:
    void prepare(int maxDelay) {
        buf_.assign(static_cast<size_t>(maxDelay + 1), 0.0f);
        pos_ = 0;
        delay_ = 0;
    }
    void setDelay(int d) noexcept { delay_ = std::clamp(d, 0, static_cast<int>(buf_.size()) - 1); }
    void reset() noexcept {
        std::fill(buf_.begin(), buf_.end(), 0.0f);
        pos_ = 0;
    }
    float processSample(float x) noexcept {
        const int size = static_cast<int>(buf_.size());
        // TODO 1: first write x into buf_[pos_], then read the value delay_ samples ago (the index must wrap
        //         around), and finally advance pos_. With delay_ = 0 the output must equal the input.
        (void)size;
        return x;
    }

private:
    std::vector<float> buf_;
    int pos_ = 0, delay_ = 0;
};

// Frequency of the k-th notch (k = 0, 1, 2, ...): deltaSamples = delay difference between the two paths (samples)
double predictedNotchHz(int k, int deltaSamples, double sampleRate) {
    // TODO 2: the two paths cancel when the phase difference 2*pi*f*delta/fs = (2k + 1)*pi; solve for f.
    (void)k;
    (void)deltaSamples;
    (void)sampleRate;
    return 0.0;
}

class ParallelSaturator {
public:
    explicit ParallelSaturator(int factorLog2) : os_(factorLog2) {}

    void prepare(double sampleRate, int maxBlock) {
        os_.prepare(sampleRate, maxBlock, 1);
        dry_.prepare(os_.getLatencySamples() + 1);
        dry_.setDelay(os_.getLatencySamples()); // default: aligned
        scratch_.setSize(1, maxBlock);
    }
    void reset() {
        os_.reset();
        dry_.reset();
    }
    int getLatencySamples() const noexcept { return os_.getLatencySamples(); }
    void setDryDelay(int d) noexcept { dry_.setDelay(d); }
    void setMix(float m) noexcept { mix_ = m; }
    void setDriveDb(float db) noexcept { drive_ = dbToGain(db); }

    void process(float* x, int n) {
        float* dry = scratch_.getWritePointer(0);
        for (int i = 0; i < n; ++i)
            dry[i] = dry_.processSample(x[i]); // dry delayed by D samples

        float* chans[1] = {x};
        AudioBlock block(chans, 1, n);
        AudioBlock up = os_.upsample(block);
        float* u = up.getChannelPointer(0);
        for (int i = 0; i < up.getNumSamples(); ++i)
            u[i] = std::tanh(drive_ * u[i]) / drive_; // divide by drive: small-signal gain = 1, so dry and wet are equally loud
        os_.downsample(block);                         // x is now the wet signal (delayed by L)

        for (int i = 0; i < n; ++i)
            x[i] = x[i] + 0.0f * dry[i]; // TODO 3: change to (1 - m)*dry + m*wet (mix_ = m)
    }

private:
    dsp::Oversampler os_;
    DryDelay dry_;
    AudioBuffer scratch_;
    float mix_ = 0.5f;
    float drive_ = 1.0f;
};

// Process a small-signal (-40 dBFS) sine in 256-sample blocks and return the gain at freq (dB)
double measureGainDb(ParallelSaturator& s, double freq) {
    s.reset();
    const int n = secondsToSamples(0.25, kSr), skip = secondsToSamples(0.05, kSr);
    std::vector<float> in(static_cast<size_t>(n)), out(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        in[static_cast<size_t>(i)] = static_cast<float>(0.01 * std::sin(kTwoPi * freq * i / kSr));
    out = in;
    for (int start = 0; start < n; start += kBlock)
        s.process(out.data() + start, std::min(kBlock, n - start));
    const double a = goertzelAmplitude(in.data() + skip, n - skip, freq, kSr);
    const double b = goertzelAmplitude(out.data() + skip, n - skip, freq, kSr);
    return gainToDb(b / a, -200.0);
}

} // namespace

int main() {
    ParallelSaturator sat(2); // 4x
    sat.prepare(kSr, kBlock);
    const int L = sat.getLatencySamples();
    std::printf("4x oversampler latency L = %d samples (%.3f ms)\n", L, 1000.0 * L / kSr);
    DAWLAB_CHECK(L == 36);

    // ---- 1. Misaligned (D = 0, delta = 36): comb filter --------------------------------------
    sat.setDryDelay(0);
    sat.setMix(0.5f);
    std::printf("\n[Misaligned] dry not delayed, mix 50%%:\n");
    for (int k = 0; k < 3; ++k) {
        const double f = predictedNotchHz(k, L, kSr);
        const double g = measureGainDb(sat, f);
        std::printf("  notch %d: predicted %7.1f Hz, measured %7.1f dB\n", k, f, g);
        DAWLAB_CHECK(f > 0.0 && g < -40.0);
    }
    DAWLAB_CHECK_NEAR(predictedNotchHz(0, L, kSr), 666.667, 0.001);
    const double peak = measureGainDb(sat, kSr / L); // phase difference 2*pi -> the two paths are in phase
    std::printf("  Peak between two notches (%.1f Hz): %+.3f dB\n", kSr / L, peak);
    DAWLAB_CHECK_NEAR(peak, 0.0, 0.05);

    // Sweep to find the first notch: 500-850 Hz in 2 Hz steps
    double minF = 0.0, minG = 1e9;
    for (double f = 500.0; f <= 850.0; f += 2.0) {
        const double g = measureGainDb(sat, f);
        if (g < minG) {
            minG = g;
            minF = f;
        }
    }
    std::printf("  Sweep 500-850 Hz: deepest at %.0f Hz (%.1f dB)\n", minF, minG);
    DAWLAB_CHECK_NEAR(minF, 666.7, 2.0);

    // mix 30%: the notch gets shallower, depth = |1 - 2m| = 0.4 -> -7.96 dB
    sat.setMix(0.3f);
    const double shallow = measureGainDb(sat, predictedNotchHz(0, L, kSr));
    std::printf("  mix 30%%: 667 Hz %.2f dB (theory 20*log10|1 - 2m| = %.2f dB)\n", shallow,
                20.0 * std::log10(0.4));
    DAWLAB_CHECK_NEAR(shallow, 20.0 * std::log10(0.4), 0.05);

    // ---- 2. Aligned (D = L): perfectly flat --------------------------------------------------
    sat.setDryDelay(L);
    sat.setMix(0.5f);
    std::printf("\n[Aligned] dry delayed by %d samples, mix 50%%:\n", L);
    for (double f : {666.667, 2000.0, 3333.333, 10000.0}) {
        const double g = measureGainDb(sat, f);
        std::printf("  %8.1f Hz: %+.4f dB\n", f, g);
        DAWLAB_CHECK_NEAR(g, 0.0, 0.02);
    }

    // ---- 3. Off by one sample (D = L - 1, delta = 1): the first notch is at 24 kHz, but the highs are already falling
    sat.setDryDelay(L - 1);
    std::printf("\n[Off by 1 sample] dry delayed by %d samples:\n", L - 1);
    for (double f : {1000.0, 10000.0}) {
        const double g = measureGainDb(sat, f);
        const double theory = 20.0 * std::log10(std::cos(kPi * f / kSr)); // |0.5 + 0.5 e^{-jw}| = |cos(w/2)|
        std::printf("  %7.0f Hz: %+.3f dB (theory %+.3f dB)\n", f, g, theory);
        DAWLAB_CHECK_NEAR(g, theory, 0.05);
    }

    return dawlab::test::summary("l06_ex5_starter");
}
