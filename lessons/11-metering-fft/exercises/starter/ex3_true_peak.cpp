// Exercise 3 starter: build a true-peak meter by reusing the chapter 06 Oversampler, and compare it with the
// chapter 05 exercise 4 windowed-sinc version
//
// Why is the sample peak not enough? The "true waveform" of a digital signal is the continuous curve obtained by sinc
// interpolation between the samples, and its peak may well fall between two samples (an inter-sample peak).
// BS.1770-4 Annex 2: upsample the signal to 4x the sample rate and take the maximum of |x|, in dBTP.
//
// Chapter 05 exercise 4 already built this from scratch (64-tap Hann windowed-sinc, 4 phases for 4x upsampling).
// This exercise takes a different angle:
//   (A) Reuse: the chapter 06 Oversampler (two 2x halfband stages) is already a ready-made, tested 4x upsampler;
//       a true-peak meter only needs "upsample -> find the maximum of |x|" and no downsampling.
//   (B) Compare: feed the same signal to both meters, compare their error against "ideal sinc interpolation"
//       (FFT zero-padding), and then compare CPU cost:
//       - where the interpolation points fall (halfband 0, 1/4, 1/2, 3/4 vs an even-length FIR's 1/8, 3/8, 5/8, 7/8)
//       - the frequency response near Nyquist (where the transition band starts)
//       - how many multiplies per input sample, and how fast it actually runs
//
// Your task (running it before you finish prints FAIL, which is expected):
//   TODO 1: the true-peak part of TruePeakMeter::process() (upsample with the Oversampler and find the maximum)
//   TODO 2: multipliesPerInputSample(): from the Oversampler's stage configuration, compute the number of multiplies
//           per input sample
//   Everything else (the reference WindowedSincTruePeak, the FFT ideal interpolation, all comparisons and timing)
//   is already done. After finishing the TODOs, read the printed tables and answer the exercise 3 questions in
//   exercises/README.md.
//
// Run: bin/Debug/l11_ex3_starter.exe (solution: solutions/ex3_true_peak.cpp)
// Output: dawlab_output/l11_ex3_true_peak_normalized.wav (the clipped drums, normalized to -1 dBTP)

#include <dawlab/Common.h>
#include <dawlab/dsp/Fft.h>
#include <dawlab/dsp/Oversampler.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <memory>
#include <vector>

using namespace dawlab;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

// ============================================================================================
// (A) The star of this exercise: a true-peak meter built on the chapter 06 Oversampler
// ============================================================================================
class TruePeakMeter {
public:
    static constexpr int kOversamplingLog2 = 2; // 2^2 = 4x

    // The only place that may allocate
    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        numChannels_ = numChannels;
        oversampler_.prepare(sampleRate, maxBlockSize, numChannels);
        samplePeak_ = std::make_unique<std::atomic<float>[]>(static_cast<size_t>(numChannels));
        truePeak_ = std::make_unique<std::atomic<float>[]>(static_cast<size_t>(numChannels));
        reset();
    }

    void reset() noexcept {
        oversampler_.reset();
        resetPeaks();
    }

    // Clear only the "maximum since last time"; the oversampler's filter state is left alone
    void resetPeaks() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch) {
            samplePeak_[static_cast<size_t>(ch)].store(0.0f, std::memory_order_relaxed);
            truePeak_[static_cast<size_t>(ch)].store(0.0f, std::memory_order_relaxed);
        }
    }

    // Audio thread: no allocation, no locks
    void process(const AudioBlock& block) noexcept {
        const int nch = std::min(numChannels_, block.getNumChannels());
        // 1. Sample peak (at the original sample rate)
        for (int ch = 0; ch < nch; ++ch) {
            const float p = peakAbs(block.getChannelPointer(ch), block.getNumSamples());
            auto& slot = samplePeak_[static_cast<size_t>(ch)];
            if (p > slot.load(std::memory_order_relaxed))
                slot.store(p, std::memory_order_relaxed);
        }
        // 2. TODO 1: true peak
        //    (a) Use oversampler_.upsample(block) to raise the block to 4x the sample rate. It returns a view of the
        //        Oversampler's internal buffer holding 4 times as many samples. Think about it: why is there no need to
        //        call downsample() here?
        //    (b) For each channel find the maximum of |x| (peakAbs) and store it into truePeak_[ch] if it is larger
        //        (relaxed, like step 1 above).
        //    The whole TODO is only a few lines -- that is the benefit of "reusing a component that is already tested".
    }

    // The getters below are thread-safe; all of them return "the maximum since reset"
    float getSamplePeakDb(int ch) const noexcept { return gainToDb(load(samplePeak_.get(), ch)); }
    float getTruePeakDb(int ch) const noexcept { return gainToDb(load(truePeak_.get(), ch)); } // dBTP
    float getTruePeakLinear(int ch) const noexcept { return load(truePeak_.get(), ch); }

    int getNumChannels() const noexcept { return numChannels_; }
    int getOversamplingFactor() const noexcept { return oversampler_.getFactor(); }
    const dsp::Oversampler& getOversampler() const noexcept { return oversampler_; }

private:
    float load(const std::atomic<float>* v, int ch) const noexcept {
        return (v != nullptr && ch >= 0 && ch < numChannels_) ? v[ch].load(std::memory_order_relaxed) : 0.0f;
    }

    int numChannels_ = 0;
    dsp::Oversampler oversampler_{kOversamplingLog2};
    std::unique_ptr<std::atomic<float>[]> samplePeak_, truePeak_;
};

// Without downsampling (upsampling only), how many multiplies does each "sample at the original rate" need?
// The input rate of stage i (i = 1, 2, ...) is fs·2^(i-1): every original sample becomes 2^(i-1) input samples of that
// stage, and each input sample runs the odd-tap FIR only once (getNumOddTaps() multiplies); the other output is a pure
// delay, with 0 multiplies.
int multipliesPerInputSample(const dsp::Oversampler& os) {
    // TODO 2: use os.getNumStages() and os.getStage(i).getNumOddTaps() (i = 0 is stage 1) to compute the total number of multiplies.
    //   Hint: stage 1 runs once per original sample; the input of stage 2 is already at 2x the sample rate, so how many times
    //   does it run per original sample?
    //   (The answer should match the expected value in self-check 7; chapter 06 section 2.6 has a table of tap counts per stage.)
    (void)os;
    return 0;
}

// ============================================================================================
// (B) Reference: the chapter 05 exercise 4 windowed-sinc version (64 taps, Hann, 4 phases; identical to that
// exercise's solution, already complete)
// ============================================================================================
class WindowedSincTruePeak {
public:
    static constexpr int kOversample = 4;
    static constexpr int kTapsPerPhase = 16;
    static constexpr int kNumTaps = kOversample * kTapsPerPhase;

    void prepare(int numChannels) {
        numChannels_ = numChannels;
        // h[k] = sinc((k - M) / 4) · Hann[k], normalized so that Σh = 4, then split into phases: coeffs_[p][j] = h[4j + p]
        std::array<double, kNumTaps> h{};
        const double M = (kNumTaps - 1) / 2.0;
        double sum = 0.0;
        for (int k = 0; k < kNumTaps; ++k) {
            const double t = (k - M) / kOversample;
            const double sinc = (t == 0.0) ? 1.0 : std::sin(kPi * t) / (kPi * t);
            h[static_cast<size_t>(k)] = sinc * (0.5 - 0.5 * std::cos(kTwoPi * k / (kNumTaps - 1)));
            sum += h[static_cast<size_t>(k)];
        }
        for (int k = 0; k < kNumTaps; ++k)
            coeffs_[static_cast<size_t>(k % kOversample)][static_cast<size_t>(k / kOversample)] =
                static_cast<float>(h[static_cast<size_t>(k)] * kOversample / sum);
        history_.assign(static_cast<size_t>(numChannels) * kTapsPerPhase, 0.0f);
        writeIndex_.assign(static_cast<size_t>(numChannels), 0);
        reset();
    }

    void reset() noexcept {
        std::fill(history_.begin(), history_.end(), 0.0f);
        std::fill(writeIndex_.begin(), writeIndex_.end(), 0);
        truePeak_ = 0.0f;
    }

    void process(const AudioBlock& block) noexcept {
        const int nch = std::min(block.getNumChannels(), numChannels_);
        for (int ch = 0; ch < nch; ++ch) {
            float* hist = history_.data() + static_cast<size_t>(ch) * kTapsPerPhase;
            int& w = writeIndex_[static_cast<size_t>(ch)];
            const float* x = block.getChannelPointer(ch);
            for (int i = 0; i < block.getNumSamples(); ++i) {
                hist[w] = x[i];
                for (int p = 0; p < kOversample; ++p) { // y[4n+p] = Σ_j h[4j+p]·x[n-j]
                    double acc = 0.0;
                    int idx = w;
                    for (int j = 0; j < kTapsPerPhase; ++j) {
                        acc += static_cast<double>(coeffs_[static_cast<size_t>(p)][static_cast<size_t>(j)]) * hist[idx];
                        if (--idx < 0)
                            idx += kTapsPerPhase;
                    }
                    truePeak_ = std::max(truePeak_, static_cast<float>(std::abs(acc)));
                }
                if (++w >= kTapsPerPhase)
                    w = 0;
            }
        }
    }

    float getTruePeakDb() const noexcept { return gainToDb(truePeak_); } // maximum over all channels
    static constexpr int multipliesPerInputSample() { return kNumTaps; } // 4 phases × 16 taps

private:
    int numChannels_ = 0;
    std::array<std::array<float, kTapsPerPhase>, kOversample> coeffs_{};
    std::vector<float> history_;
    std::vector<int> writeIndex_;
    float truePeak_ = 0.0f;
};

// ============================================================================================
// Measurement utilities
// ============================================================================================

// Feed block by block (simulating the audio callback)
template <typename Meter>
void feed(Meter& meter, AudioBuffer& audio) {
    for (int start = 0; start < audio.getNumSamples(); start += kBlockSize) {
        const int len = std::min(kBlockSize, audio.getNumSamples() - start);
        meter.process(AudioBlock(audio).getSubBlock(start, len));
    }
}

float maxTruePeakDb(const TruePeakMeter& meter) {
    float m = kMinusInfinityDb;
    for (int ch = 0; ch < meter.getNumChannels(); ++ch)
        m = std::max(m, meter.getTruePeakDb(ch));
    return m;
}

float maxSamplePeakDb(const TruePeakMeter& meter) {
    float m = kMinusInfinityDb;
    for (int ch = 0; ch < meter.getNumChannels(); ++ch)
        m = std::max(m, meter.getSamplePeakDb(ch));
    return m;
}

// The windowed-sinc version's reading (it is re-prepared every time, so both meters see the same signal starting from silence)
float sincTruePeakDb(AudioBuffer& audio) {
    WindowedSincTruePeak meter;
    meter.prepare(audio.getNumChannels());
    feed(meter, audio);
    return meter.getTruePeakDb();
}

// The "ideal" true peak: run one big FFT over the whole signal, zero-pad the spectrum to 4x the length, then take the IFFT
// = samples of ideal sinc interpolation at the 4x sample rate (differing only by the circular boundary effect of the FFT,
// which can be ignored when the signal starts and ends with silence or fades). Returns the maximum over all channels.
float idealTruePeakDb(const AudioBuffer& audio) {
    int order = 1;
    while ((1 << order) < audio.getNumSamples())
        ++order;
    const int n = 1 << order;
    dsp::Fft fft(order), fft4(order + 2);
    std::vector<float> x(static_cast<size_t>(n));
    std::vector<std::complex<float>> spec(static_cast<size_t>(fft4.getNumRealBins()));
    std::vector<float> up(static_cast<size_t>(4 * n));
    float peak = 0.0f;
    for (int ch = 0; ch < audio.getNumChannels(); ++ch) {
        std::fill(x.begin(), x.end(), 0.0f);
        std::copy_n(audio.getReadPointer(ch), audio.getNumSamples(), x.begin());
        std::fill(spec.begin(), spec.end(), std::complex<float>(0.0f, 0.0f));
        fft.forwardReal(x.data(), spec.data());
        spec[static_cast<size_t>(n / 2)] *= 0.5f; // In the 4N spectrum the Nyquist bin is treated as two conjugate bins, so split it in half
        fft4.inverseReal(spec.data(), up.data());
        // inverseReal includes 1/(4N) and the original forward has no normalization -> multiply by 4 to restore the amplitude
        peak = std::max(peak, 4.0f * peakAbs(up.data(), 4 * n));
    }
    return gainToDb(peak);
}

// A sine at fs/4: x[n] = A·sin(π n/2 + φ)
AudioBuffer makeQuarterRateSine(double seconds, double phaseRadians, float amplitude = 1.0f) {
    AudioBuffer b(1, secondsToSamples(seconds, kSampleRate));
    for (int i = 0; i < b.getNumSamples(); ++i)
        b.setSample(0, i, static_cast<float>(amplitude * std::sin(kPi * 0.5 * i + phaseRadians)));
    return b;
}

// Add raised-cosine fades at both ends so the step of the signal "starting abruptly" does not produce extra inter-sample overshoot
void fadeInOut(AudioBuffer& b, double ms) {
    const int n = std::min(b.getNumSamples() / 2, msToSamples(ms, kSampleRate));
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < n; ++i) {
            const float g = static_cast<float>(0.5 - 0.5 * std::cos(kPi * i / n));
            b.getWritePointer(ch)[i] *= g;
            b.getWritePointer(ch)[b.getNumSamples() - 1 - i] *= g;
        }
}

} // namespace

int main() {
    TruePeakMeter meter;
    meter.prepare(kSampleRate, kBlockSize, 2);
    std::printf("Oversampler: %dx, %d halfband stages; reference: the 64-tap windowed-sinc from chapter 05 exercise 4\n\n",
                meter.getOversamplingFactor(), meter.getOversampler().getNumStages());

    // ---- 1. 1 kHz, 0 dBFS: a sample lands exactly on the peak (48 samples per period, the 12th = sin(π/2)) ----
    {
        AudioBuffer sine = makeSine(1000.0, kSampleRate, 1.0, 1.0f, 1);
        meter.reset();
        feed(meter, sine);
        const float sinc = sincTruePeakDb(sine);
        std::printf("1 kHz 0 dBFS: sample peak = %.3f dBFS, true peak: Oversampler %.3f / windowed-sinc %.3f dBTP\n",
                    meter.getSamplePeakDb(0), meter.getTruePeakDb(0), sinc);
        DAWLAB_CHECK_NEAR(meter.getSamplePeakDb(0), 0.0, 0.01);
        DAWLAB_CHECK_NEAR(meter.getTruePeakDb(0), 0.0, 0.1);
        DAWLAB_CHECK_NEAR(sinc, 0.0, 0.1);
    }

    // ---- 2. fs/4, phase 45°: sample peak -3.01 dBFS, true peak 0 dBTP ----------------------------
    {
        AudioBuffer q = makeQuarterRateSine(1.0, kPi / 4.0);
        fadeInOut(q, 10.0);
        meter.reset();
        feed(meter, q);
        const float sinc = sincTruePeakDb(q);
        std::printf("12 kHz, phase 45°: sample peak = %.3f dBFS, true peak: Oversampler %.3f / windowed-sinc %.3f dBTP\n",
                    meter.getSamplePeakDb(0), meter.getTruePeakDb(0), sinc);
        DAWLAB_CHECK_NEAR(meter.getSamplePeakDb(0), -3.01, 0.02);
        DAWLAB_CHECK_NEAR(meter.getTruePeakDb(0), 0.0, 0.15);
    }

    // ---- 3. Phase sweep: the same fs/4 sine, with the peak falling at different positions between the original samples ----
    //   The Oversampler's 4x points are at 0, 1/4, 1/2, 3/4; the 64-tap (even-length) points are at 1/8, 3/8, 5/8, 7/8.
    //   The peak is at most 1/8 sample = 11.25° (at fs/4) from the nearest point, and both underestimate by at worst
    //   20·log10(cos 11.25°) = -0.17 dB, just at different phases: where one is accurate, the other happens to be worst.
    {
        std::printf("\nPhase sweep (fs/4 sine, amplitude 1; error = reading - 0 dBTP)\n");
        std::printf("  phase    peak pos    Oversampler    windowed-sinc\n");
        float worstOs = 0.0f, worstSinc = 0.0f;
        double worstOsPhase = 0.0, worstSincPhase = 0.0;
        constexpr int kSteps = 32;
        for (int s = 0; s <= kSteps; ++s) {
            const double phaseDeg = 90.0 * s / kSteps;
            AudioBuffer q = makeQuarterRateSine(0.25, phaseDeg * kPi / 180.0);
            fadeInOut(q, 10.0);
            meter.reset();
            feed(meter, q);
            const float os = meter.getTruePeakDb(0);
            const float sinc = sincTruePeakDb(q);
            if (os < worstOs) {
                worstOs = os;
                worstOsPhase = phaseDeg;
            }
            if (sinc < worstSinc) {
                worstSinc = sinc;
                worstSincPhase = phaseDeg;
            }
            // The peak is at n = 1 − 2φ/π (fractional part): φ = 0 -> 0, φ = 45° -> 1/2, φ = 90° -> 0
            double where = 1.0 - phaseDeg / 90.0;
            where -= std::floor(where);
            if (s % 4 == 0)
                std::printf("  %5.2f°     %5.3f       %+7.3f dB      %+7.3f dB\n", phaseDeg, where, os, sinc);
        }
        std::printf("  Worst: Oversampler %+.3f dB (phase %.2f°), windowed-sinc %+.3f dB (phase %.2f°)\n", worstOs,
                    worstOsPhase, worstSinc, worstSincPhase);
        DAWLAB_CHECK_NEAR(worstOs, -0.17, 0.03);
        DAWLAB_CHECK_NEAR(worstSinc, -0.17, 0.03);
        DAWLAB_CHECK(std::abs(worstOsPhase - worstSincPhase) > 5.0); // the worst positions differ
    }

    // ---- 4. Frequency response near Nyquist: a sine of amplitude 0.5 (-6.02 dBFS) with the frequency slightly off an integer ratio ----
    //   Letting the peak sweep through every position between samples within 1 second makes the reading reflect almost
    //   purely the gain of the interpolation filter at that frequency.
    {
        std::printf("\nFrequency response (sine of amplitude 0.5, error = reading - (-6.02 dBTP))\n");
        std::printf("  frequency     Oversampler    windowed-sinc\n");
        const double freqs[] = {1000.3, 10000.3, 16000.3, 18000.3, 20000.3, 21000.3, 22000.3};
        const float ref = gainToDb(0.5f);
        float osErr[7] = {}, sincErr[7] = {};
        for (int i = 0; i < 7; ++i) {
            AudioBuffer s = makeSine(freqs[i], kSampleRate, 1.0, 0.5f, 1);
            fadeInOut(s, 10.0);
            meter.reset();
            feed(meter, s);
            osErr[i] = meter.getTruePeakDb(0) - ref;
            sincErr[i] = sincTruePeakDb(s) - ref;
            std::printf("  %7.1f Hz     %+7.3f dB      %+7.3f dB\n", freqs[i], osErr[i], sincErr[i]);
        }
        // Both are accurate from low frequencies up to 16 kHz (< 0.02 dB)
        for (int i = 0; i <= 2; ++i)
            DAWLAB_CHECK(std::abs(osErr[i]) < 0.02f && std::abs(sincErr[i]) < 0.02f);
        // The Oversampler stays within 0.02 dB all the way to 22 kHz: stage 1's halfband actually attenuates 0.65 dB at 22 kHz,
        // but the image it fails to filter out completely (26 kHz) happens to make up for it -- for a halfband
        // |H(f)| + |H(fs − f)| = 1 (chapter 06 section 2.6), and when the two add in phase the peak is exactly the
        // original amplitude
        for (int i = 0; i < 7; ++i)
            DAWLAB_CHECK(std::abs(osErr[i]) < 0.02f);
        // The transition band of the 64-tap Hann windowed-sinc is much wider: errors of ±0.1 dB from 18 kHz, and about 0.4 dB underestimate at 22 kHz
        DAWLAB_CHECK(sincErr[6] < osErr[6] - 0.3f);
    }

    // ---- 5. Hard-clipped drums: one signal, three readings; then normalize to -1 dBTP ------------
    {
        AudioBuffer drums = makeDrumLoop(kSampleRate, 2, 120.0, 0.8f, 2);
        drums.applyGain(dbToGain(12.0f)); // push +12 dB, then clip
        for (int ch = 0; ch < drums.getNumChannels(); ++ch)
            for (int i = 0; i < drums.getNumSamples(); ++i)
                drums.setSample(ch, i, std::clamp(drums.getSample(ch, i), -1.0f, 1.0f));
        meter.reset();
        feed(meter, drums);
        const float sp = maxSamplePeakDb(meter), tp = maxTruePeakDb(meter);
        const float sinc = sincTruePeakDb(drums);
        const float ideal = idealTruePeakDb(drums);
        std::printf("\nClipped drums: sample peak = %.3f dBFS, true peak: Oversampler %.3f / windowed-sinc %.3f / "
                    "FFT ideal interpolation %.3f dBTP\n",
                    sp, tp, sinc, ideal);
        DAWLAB_CHECK_NEAR(sp, 0.0, 0.01);
        DAWLAB_CHECK(tp >= sp - 1e-3f); // the halfband's pure-delay phase is the original sample, so it cannot read lower
        DAWLAB_CHECK(tp > sp + 0.05f);  // the flat tops produced by clipping always have inter-sample overshoot
        DAWLAB_CHECK(std::abs(tp - ideal) < std::abs(sinc - ideal)); // with a lot of high-frequency content, the halfband is closer to the ideal value

        // Mastering workflow: normalize to -1 dBTP (not -1 dBFS)
        const float gainDb = -1.0f - tp;
        drums.applyGain(dbToGain(gainDb));
        meter.reset();
        feed(meter, drums);
        std::printf("After applying %+.2f dB: sample peak = %.3f dBFS, true peak = %.3f dBTP\n", gainDb,
                    maxSamplePeakDb(meter), maxTruePeakDb(meter));
        DAWLAB_CHECK_NEAR(maxTruePeakDb(meter), -1.0, 0.05);
        DAWLAB_CHECK(maxSamplePeakDb(meter) <= -1.0f + 0.01f);
        writeWav(outputPath("l11_ex3_true_peak_normalized.wav"), drums, kSampleRate);
    }

    // ---- 6. Plucked material: every note starts from a burst of white noise, with a lot of content near Nyquist ----
    {
        AudioBuffer plucks = makePluckSequence(kSampleRate, 2.0, 0.4f, 1);
        meter.reset();
        feed(meter, plucks);
        const float sp = meter.getSamplePeakDb(0), tp = meter.getTruePeakDb(0);
        const float sinc = sincTruePeakDb(plucks);
        const float ideal = idealTruePeakDb(plucks);
        std::printf("pluck sequence: sample peak = %.3f dBFS, true peak: Oversampler %.3f / windowed-sinc %.3f / "
                    "FFT ideal interpolation %.3f dBTP\n",
                    sp, tp, sinc, ideal);
        DAWLAB_CHECK(tp >= sp - 1e-3f);
        DAWLAB_CHECK(ideal >= tp - 0.1f);   // 4x can only underestimate (0.1 dB tolerance for a little filter ripple)
        DAWLAB_CHECK(ideal >= sinc - 0.1f);
        DAWLAB_CHECK(std::abs(ideal - tp) < 1.0f);
        DAWLAB_CHECK(std::abs(ideal - sinc) < 1.5f);
        DAWLAB_CHECK(std::abs(ideal - tp) < std::abs(ideal - sinc)); // again lots of energy near Nyquist, so the halfband is closer
    }

    // ---- 7. CPU: multiplies per input sample + actual timing -----------------------------------
    {
        const int osMul = multipliesPerInputSample(meter.getOversampler());
        const int sincMul = WindowedSincTruePeak::multipliesPerInputSample();
        std::printf("\nMultiplies per input sample: Oversampler %d (stage 1: %d + stage 2: %d × 2), windowed-sinc %d (4 × 16)\n",
                    osMul, meter.getOversampler().getStage(0).getNumOddTaps(),
                    meter.getOversampler().getStage(1).getNumOddTaps(), sincMul);
        DAWLAB_CHECK(osMul == 32 + 2 * 12);
        DAWLAB_CHECK(sincMul == 64);

        // 10 s of stereo white noise, run 3 times each and keep the fastest (to exclude first-run cache / page-fault interference)
        AudioBuffer noise = makeWhiteNoise(kSampleRate, 10.0, 0.25f, 2);
        const double numSamples = static_cast<double>(noise.getNumSamples()) * noise.getNumChannels();
        auto timeIt = [&](auto&& run) {
            double best = 1.0e30;
            for (int r = 0; r < 3; ++r) {
                const auto t0 = std::chrono::steady_clock::now();
                run();
                const auto t1 = std::chrono::steady_clock::now();
                best = std::min(best, std::chrono::duration<double, std::nano>(t1 - t0).count());
            }
            return best / numSamples; // ns / sample
        };
        const double osNs = timeIt([&] {
            meter.reset();
            feed(meter, noise);
        });
        WindowedSincTruePeak sincMeter;
        sincMeter.prepare(2);
        const double sincNs = timeIt([&] {
            sincMeter.reset();
            feed(sincMeter, noise);
        });
        std::printf("Measured timing (10 s stereo, per channel-sample): Oversampler %.1f ns, windowed-sinc %.1f ns, "
                    "ratio %.2f (varies with build settings and machine)\n",
                    osNs, sincNs, sincNs / osNs);
        DAWLAB_CHECK(osNs > 0.0 && sincNs > 0.0);
    }

    return dawlab::test::summary("l11_ex3");
}
