// Exercise 4 solution: VU meter ballistics (IEC 60268-17)
//
// A VU meter is a "needle with mass": it measures the rectified average (close to RMS, but not RMS), and its response is
// deliberately slow so that you see the "perceived loudness" rather than every transient. The standard requires:
//   - for a sudden-onset 1 kHz sine (0 VU), the needle reaches 99% of its final reading within 300 ms (±10%)
//   - overshoot of 1% ~ 1.5%
//   - the fall time equals the rise time
//   - 0 VU corresponds to +4 dBu; in a DAW it is usually calibrated so that a -18 dBFS (EBU) or -20 dBFS (K-20) sine = 0 VU
//
// It is modelled with a second-order system (a needle on a spring with damping):
//     p'' + 2ζω_n p' + ω_n² p = ω_n² u(t),  u = the rectified input
//   The damping ratio ζ sets the overshoot: overshoot = exp(-πζ / sqrt(1-ζ²)); ζ = 0.82 -> 1.11%
//   The natural frequency ω_n sets the speed: solve for "the normalized time u99 at which the step response first reaches 0.99",
//   then ω_n = u99 / 0.3 s
// Each sample is integrated with semi-implicit Euler (ω_n·T ≈ 3e-4, very stable).
//
// Run: bin/Debug/l11_ex4_solution.exe (ctest label: solution)

#include <dawlab/Common.h>
#include <dawlab/dsp/LevelMeter.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using namespace dawlab;

namespace {

constexpr double kSampleRate = 48000.0;

class VuMeter {
public:
    void prepare(double sampleRate, int numChannels) {
        sampleRate_ = sampleRate;
        numChannels_ = numChannels;
        state_.assign(static_cast<size_t>(numChannels), NeedleState{});
        readingVu_ = std::make_unique<std::atomic<float>[]>(static_cast<size_t>(numChannels));
        updateCoefficients();
        reset();
    }

    void reset() noexcept {
        for (NeedleState& s : state_)
            s = NeedleState{};
        for (int ch = 0; ch < numChannels_; ++ch)
            readingVu_[static_cast<size_t>(ch)].store(kMinusInfinityDb, std::memory_order_relaxed);
    }

    // Peak level (dBFS) of the sine that corresponds to 0 VU, default -18
    void setReferenceDbfs(float dbfs) noexcept {
        referenceDbfs_ = dbfs;
        updateCoefficients();
    }
    // Time to reach 99% (default 300 ms) and the damping ratio (default 0.82 -> 1.1% overshoot)
    void setRiseTimeMs(float ms) noexcept {
        riseTimeMs_ = std::max(1.0f, ms);
        updateCoefficients();
    }
    void setDamping(float zeta) noexcept {
        zeta_ = std::clamp(zeta, 0.3f, 0.999f);
        updateCoefficients();
    }

    // audio thread
    void process(const AudioBlock& block) noexcept {
        const int nch = std::min(numChannels_, block.getNumChannels());
        const int n = block.getNumSamples();
        for (int ch = 0; ch < nch; ++ch) {
            NeedleState& s = state_[static_cast<size_t>(ch)];
            const float* x = block.getChannelPointer(ch);
            for (int i = 0; i < n; ++i) {
                // Full-wave rectification + calibration: a reference-level sine has a rectified average of 2A/π; multiplying by π/(2A) makes it 1.0
                const double u = std::abs(x[i]) * inputScale_;
                // Second-order system, semi-implicit Euler: update the velocity first, then update the position with the new velocity
                const double acc = wn2_ * (u - s.position) - twoZetaWn_ * s.velocity;
                s.velocity += acc * dt_;
                s.position += s.velocity * dt_;
            }
            // The needle cannot go below 0 at the left end of the scale (a real needle hits the stop)
            const double needle = std::max(0.0, s.position);
            readingVu_[static_cast<size_t>(ch)].store(gainToDb(static_cast<float>(needle)), std::memory_order_relaxed);
        }
    }

    // thread-safe
    float getVu(int ch) const noexcept {
        return (ch >= 0 && ch < numChannels_) ? readingVu_[static_cast<size_t>(ch)].load(std::memory_order_relaxed)
                                              : kMinusInfinityDb;
    }
    // Linear needle position (1.0 = 0 VU)
    float getNeedle(int ch) const noexcept { return dbToGain(getVu(ch)); }

    double getNaturalFrequencyHz() const noexcept { return wn_ / kTwoPi; }
    double getOvershootPercent() const noexcept { return 100.0 * std::exp(-kPi * zeta_ / std::sqrt(1.0 - zeta_ * zeta_)); }

    // Normalized step response y(u) of the second-order system, u = ω_n t (underdamped, ζ < 1):
    //     y(u) = 1 − e^{−ζu} / sqrt(1−ζ²) · sin( sqrt(1−ζ²)·u + acos ζ )
    static double stepResponse(double zeta, double u) noexcept {
        const double s = std::sqrt(1.0 - zeta * zeta);
        return 1.0 - std::exp(-zeta * u) / s * std::sin(s * u + std::acos(zeta));
    }

    // Normalized time at which the response first reaches target (e.g. 0.99): y rises monotonically on [0, peak time π/sqrt(1−ζ²)], so bisect
    static double timeToReach(double zeta, double target) noexcept {
        double lo = 0.0, hi = kPi / std::sqrt(1.0 - zeta * zeta);
        for (int i = 0; i < 60; ++i) {
            const double mid = 0.5 * (lo + hi);
            (stepResponse(zeta, mid) < target ? lo : hi) = mid;
        }
        return 0.5 * (lo + hi);
    }

private:
    struct NeedleState {
        double position = 0.0;
        double velocity = 0.0;
    };

    void updateCoefficients() noexcept {
        if (sampleRate_ <= 0.0)
            return;
        dt_ = 1.0 / sampleRate_;
        const double u99 = timeToReach(zeta_, 0.99);
        wn_ = u99 / (riseTimeMs_ * 0.001); // so that y(0.3 s) = 0.99
        wn2_ = wn_ * wn_;
        twoZetaWn_ = 2.0 * zeta_ * wn_;
        inputScale_ = kPi / (2.0 * dbToGain(static_cast<double>(referenceDbfs_)));
    }

    double sampleRate_ = 0.0;
    int numChannels_ = 0;
    float referenceDbfs_ = -18.0f;
    float riseTimeMs_ = 300.0f;
    double zeta_ = 0.82;
    double dt_ = 0.0, wn_ = 0.0, wn2_ = 0.0, twoZetaWn_ = 0.0, inputScale_ = 1.0;
    std::vector<NeedleState> state_;
    std::unique_ptr<std::atomic<float>[]> readingVu_;
};

// Feed block by block; blockSize is adjustable (use 480 = 10 ms when measuring at 300 ms so the boundaries line up)
template <typename OnBlock>
void feed(VuMeter& meter, AudioBuffer& audio, int blockSize, OnBlock&& onBlock) {
    for (int start = 0; start < audio.getNumSamples(); start += blockSize) {
        const int len = std::min(blockSize, audio.getNumSamples() - start);
        meter.process(AudioBlock(audio).getSubBlock(start, len));
        onBlock(start + len);
    }
}

} // namespace

int main() {
    VuMeter vu;
    vu.prepare(kSampleRate, 2);
    std::printf("ζ = 0.82 -> overshoot %.2f%%; u99 = %.3f -> ω_n = %.2f rad/s (%.2f Hz)\n\n", vu.getOvershootPercent(),
                VuMeter::timeToReach(0.82, 0.99), vu.getNaturalFrequencyHz() * kTwoPi, vu.getNaturalFrequencyHz());
    DAWLAB_CHECK(vu.getOvershootPercent() >= 1.0 && vu.getOvershootPercent() <= 1.5);

    // ---- 1. Step response: a -18 dBFS 1 kHz sine (= 0 VU) starts abruptly and stops abruptly after 1 s ----------
    {
        AudioBuffer step = makeToneBursts(1000.0, kSampleRate, {-18.0f, -200.0f}, 1.0, 1);
        const int blockSize = 480; // 10 ms
        float needleAt300 = 0.0f, maxNeedle = 0.0f, needleAfterStop300 = 1.0f, finalNeedle = 0.0f;
        std::printf("%8s %10s %10s\n", "time(s)", "needle", "VU");
        feed(vu, step, blockSize, [&](int samplesDone) {
            const double t = samplesDone / kSampleRate;
            const float needle = vu.getNeedle(0);
            if (samplesDone == 14400)
                needleAt300 = needle; // exactly 300 ms
            if (t <= 1.0) {
                maxNeedle = std::max(maxNeedle, needle);
                finalNeedle = needle;
            }
            if (samplesDone == 48000 + 14400)
                needleAfterStop300 = needle; // 300 ms after the stop
            if (samplesDone % 4800 == 0 && t <= 1.6)
                std::printf("%8.2f %10.4f %10.2f\n", t, needle, vu.getVu(0));
        });
        // The standard's 99% and overshoot are both relative to the "final reading". The final reading is not exactly 1.0: at 48 kHz a 1 kHz period has only
        // 48 samples, and the average of the sampled |sin| = cot(π/48)/24 = 0.63571, 0.14% below the continuous 2/π = 0.63662.
        const float settle = needleAt300 / finalNeedle;
        const float overshoot = maxNeedle / finalNeedle - 1.0f;
        std::printf("At 1 s (final reading) = %.4f (%.3f VU); at 300 ms = %.2f%% of the final reading (standard: 99%%); "
                    "overshoot %.2f%% (standard: 1~1.5%%)\n",
                    finalNeedle, gainToDb(finalNeedle), 100.0 * settle, 100.0 * overshoot);
        std::printf("Needle 300 ms after the stop = %.4f (<= 0.015: it falls as fast as it rises)\n\n", needleAfterStop300);
        DAWLAB_CHECK_NEAR(settle, 0.99, 0.005);
        DAWLAB_CHECK(overshoot >= 0.01f && overshoot <= 0.015f);
        DAWLAB_CHECK_NEAR(gainToDb(finalNeedle), 0.0, 0.05);
        DAWLAB_CHECK(needleAfterStop300 <= 0.015f);
    }

    // ---- 2. Calibration: a -24 dBFS sine -> -6 VU; 0 dBFS -> +18 VU -------------------------------
    {
        AudioBuffer quiet = makeSine(1000.0, kSampleRate, 1.0, dbToGain(-24.0f), 1);
        vu.reset();
        feed(vu, quiet, 512, [](int) {});
        std::printf("-24 dBFS sine -> %.2f VU (expected -6)\n", vu.getVu(0));
        DAWLAB_CHECK_NEAR(vu.getVu(0), -6.0, 0.05);

        AudioBuffer loud = makeSine(1000.0, kSampleRate, 1.0, 1.0f, 1);
        vu.reset();
        feed(vu, loud, 512, [](int) {});
        std::printf("0 dBFS sine -> %.2f VU (expected +18)\n", vu.getVu(0));
        DAWLAB_CHECK_NEAR(vu.getVu(0), 18.0, 0.05);

        vu.setReferenceDbfs(-20.0f); // K-20 calibration
        vu.reset();
        feed(vu, quiet, 512, [](int) {});
        std::printf("After changing to -20 dBFS = 0 VU, a -24 dBFS sine -> %.2f VU (expected -4)\n\n", vu.getVu(0));
        DAWLAB_CHECK_NEAR(vu.getVu(0), -4.0, 0.05);
        vu.setReferenceDbfs(-18.0f);
    }

    // ---- 3. Drums: the VU (average) is far below the sample peak and barely follows each drum hit ------------
    {
        AudioBuffer drums = makeDrumLoop(kSampleRate, 2, 120.0, 0.8f, 2);
        dsp::LevelMeter peakMeter;
        peakMeter.prepare(kSampleRate, 2);
        peakMeter.setPeakHoldMs(-1.0f); // infinite hold
        vu.reset();
        float minVu = 1000.0f, maxVu = -1000.0f;
        std::printf("%8s %10s %10s\n", "time(s)", "VU", "peak dBFS");
        feed(vu, drums, 512, [&](int samplesDone) {
            peakMeter.process(AudioBlock(drums).getSubBlock(samplesDone - std::min(512, samplesDone),
                                                            std::min(512, samplesDone)));
            const double t = samplesDone / kSampleRate;
            if (t > 1.0) {
                minVu = std::min(minVu, vu.getVu(0));
                maxVu = std::max(maxVu, vu.getVu(0));
            }
            if (samplesDone % (512 * 20) == 0)
                std::printf("%8.2f %10.2f %10.2f\n", t, vu.getVu(0), peakMeter.getPeakDb(0));
        });
        const float peakDbfs = peakMeter.getPeakHoldDb(0);
        const float vuAsDbfs = maxVu - 18.0f; // convert the VU reading back to dBFS (with -18 dBFS = 0 VU)
        std::printf("Drums: the VU swings between %.2f and %.2f (equivalent to %.2f dBFS), sample peak = %.2f dBFS\n", minVu, maxVu,
                    vuAsDbfs, peakDbfs);
        DAWLAB_CHECK(vuAsDbfs < peakDbfs - 6.0f); // the average is far below the peak (crest factor)
        // Note: the VU falls a lot in the gaps between drum hits (minVu is very low) -- the 300 ms ballistics are "as slow to fall
        // as to rise", and the gaps in this drum pattern last several hundred ms, so the needle really does drop.
        // This is normal VU behaviour.
    }

    // ---- 4. A short transient: a 10 ms 0 VU sine barely moves the VU needle ------------------------
    // This is the right way to show that "the VU cannot see transients": at the same level, lasting 10 ms vs lasting 1 s gives very different readings.
    {
        // A 10 ms sine followed by 290 ms of silence: the needle has inertia and keeps climbing a little after the burst ends
        // (peaking about 80 ms later), so we need to observe for a while longer to catch the true maximum.
        std::vector<float> levels(30, -200.0f);
        levels[0] = -18.0f;
        AudioBuffer click = makeToneBursts(1000.0, kSampleRate, levels, 0.01, 1);
        vu.reset();
        float maxNeedle = 0.0f;
        feed(vu, click, 48, [&](int) { maxNeedle = std::max(maxNeedle, vu.getNeedle(0)); }); // 1 ms per block, so the maximum is caught
        std::printf("\n10 ms 0 VU sine: needle maximum = %.4f (%.2f VU); a sustained sine at the same level reaches 0 VU\n", maxNeedle,
                    gainToDb(maxNeedle));
        DAWLAB_CHECK(maxNeedle < 0.25f); // below -12 VU: a short transient hardly moves the needle
        DAWLAB_CHECK(maxNeedle > 0.01f); // but it is not completely unresponsive either
    }

    return dawlab::test::summary("l11_ex4");
}
