// Exercise 4 solution: a simple 4x oversampling true-peak meter (a conceptual version of ITU-R BS.1770)
//
// Idea:
//   The samples of a digital signal are just "sampling points" on a continuous waveform, and the truly reconstructed waveform
//   can be higher between two samples (inter-sample peak). The approach of BS.1770-4 Annex 2 is to upsample the signal by 4
//   (insert 3 zeros, then "fill them in" into a smooth curve with a lowpass) and then measure the sample peak of the upsampled
//   signal, in units called dBTP.
//
//   The cheapest way to compute the upsampling lowpass is polyphase:
//     upsampled y[4n + p] = sum_k h[k]*u[4n + p - k], where u is the zero-inserted signal (non-zero only at multiples of 4)
//                         = sum_j h[4j + p]*x[n - j]                      (keep only the non-zero terms)
//   So for each input sample we only need 4 "sub-filters" (phase p = 0..3, kTapsPerPhase taps each) taking a dot product
//   with the most recent kTapsPerPhase inputs to get the 4 interpolated points. There is no need to actually build a 4x-long buffer.
//
// Run: l05_ex4_solution
// Output: dawlab_output/l05_ex4_intersample.wav (an fs/4 sine wave, sample peak -9 dBFS, true peak -6 dBTP)

#include <dawlab/Common.h>
#include <dawlab/dsp/Limiter.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

constexpr int kOversample = 4;    // BS.1770 recommends 4x at 48 kHz
constexpr int kTapsPerPhase = 16; // 16 taps per phase -> 64 taps in the prototype filter
constexpr int kNumTaps = kOversample * kTapsPerPhase;

class TruePeakMeter {
public:
    void prepare(int numChannels) {
        numChannels_ = numChannels;
        designFilter();
        history_.assign(static_cast<size_t>(numChannels) * kTapsPerPhase, 0.0f);
        writeIndex_.assign(static_cast<size_t>(numChannels), 0);
        reset();
    }

    void reset() noexcept {
        std::fill(history_.begin(), history_.end(), 0.0f);
        std::fill(writeIndex_.begin(), writeIndex_.end(), 0);
        samplePeak_ = 0.0f;
        truePeak_ = 0.0f;
    }

    // Clear only the measured peaks and keep the filter history: used to "start measuring after the filter has warmed up"
    void resetPeaks() noexcept {
        samplePeak_ = 0.0f;
        truePeak_ = 0.0f;
    }

    void process(const dawlab::AudioBlock& block) noexcept {
        const int nch = std::min(block.getNumChannels(), numChannels_);
        for (int ch = 0; ch < nch; ++ch) {
            float* hist = history_.data() + static_cast<size_t>(ch) * kTapsPerPhase;
            int& w = writeIndex_[static_cast<size_t>(ch)];
            const float* x = block.getChannelPointer(ch);
            for (int i = 0; i < block.getNumSamples(); ++i) {
                samplePeak_ = std::max(samplePeak_, std::abs(x[i]));

                // Push the new sample into the circular history: hist[w] = x[n], and x[n - j] is at (w - j) mod T
                hist[w] = x[i];
                for (int p = 0; p < kOversample; ++p) {
                    double acc = 0.0;
                    int idx = w;
                    for (int j = 0; j < kTapsPerPhase; ++j) { // y[4n+p] = sum_j h[4j+p]*x[n-j]
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

    float getSamplePeak() const noexcept { return samplePeak_; }
    float getTruePeak() const noexcept { return truePeak_; }
    float getSamplePeakDb() const noexcept { return dawlab::gainToDb(samplePeak_); }
    float getTruePeakDb() const noexcept { return dawlab::gainToDb(truePeak_); } // unit: dBTP
    // coeffs[p][j] = h[4j + p] (for the self-check that verifies the filter design)
    const std::array<std::array<float, kTapsPerPhase>, kOversample>& getCoefficients() const noexcept { return coeffs_; }

private:
    // Prototype: a windowed-sinc lowpass whose cutoff at the 4x sample rate is the original fs/2 (= 1/8 of the new sample rate),
    // with a gain of x4 to compensate for the zero insertion (inserting 3 zeros drops the average level to 1/4).
    //   h[k] = sinc((k - M) / 4) * w[k],  M = (N - 1) / 2,  w = Hann window
    // Finally the sum is normalized to 4, which makes the DC gain (= the sum over all phases) exactly 1.
    void designFilter() {
        std::array<double, kNumTaps> h{};
        const double M = (kNumTaps - 1) / 2.0;
        double sum = 0.0;
        for (int k = 0; k < kNumTaps; ++k) {
            const double t = (k - M) / kOversample;
            const double sinc = (t == 0.0) ? 1.0 : std::sin(dawlab::kPi * t) / (dawlab::kPi * t);
            const double window = 0.5 - 0.5 * std::cos(dawlab::kTwoPi * k / (kNumTaps - 1));
            h[static_cast<size_t>(k)] = sinc * window;
            sum += h[static_cast<size_t>(k)];
        }
        for (int k = 0; k < kNumTaps; ++k) {
            const double normalized = h[static_cast<size_t>(k)] * kOversample / sum;
            coeffs_[static_cast<size_t>(k % kOversample)][static_cast<size_t>(k / kOversample)] =
                static_cast<float>(normalized); // polyphase decomposition: phase p takes h[p], h[p+4], h[p+8], ...
        }
    }

    int numChannels_ = 0;
    std::array<std::array<float, kTapsPerPhase>, kOversample> coeffs_{};
    std::vector<float> history_;
    std::vector<int> writeIndex_;
    float samplePeak_ = 0.0f;
    float truePeak_ = 0.0f;
};

// warmupSamples > 0: these first samples only fill the filter history and do not count toward the peak.
// When the signal "starts suddenly from 0", the interpolation filter produces an onset transient (Gibbs ringing) at the beginning,
// and what gets measured would be the transient instead of the steady state.
void measure(const char* name, dawlab::AudioBuffer& buffer, float& samplePeakDb, float& truePeakDb,
             int warmupSamples = 0) {
    TruePeakMeter meter;
    meter.prepare(buffer.getNumChannels());
    dawlab::AudioBlock whole(buffer);
    const int warmup = std::min(warmupSamples, buffer.getNumSamples());
    if (warmup > 0) {
        meter.process(whole.getSubBlock(0, warmup));
        meter.resetPeaks();
    }
    for (int start = warmup; start < buffer.getNumSamples(); start += kBlockSize)
        meter.process(whole.getSubBlock(start, std::min(kBlockSize, buffer.getNumSamples() - start)));
    samplePeakDb = meter.getSamplePeakDb();
    truePeakDb = meter.getTruePeakDb();
    std::printf("%-34s sample peak %7.2f dBFS   true peak %7.2f dBTP   diff %+5.2f dB\n", name, samplePeakDb, truePeakDb,
                truePeakDb - samplePeakDb);
}

template <typename Processor>
void processInBlocks(dawlab::AudioBuffer& buffer, Processor& processor) {
    dawlab::AudioBlock whole(buffer);
    for (int start = 0; start < buffer.getNumSamples(); start += kBlockSize)
        processor.process(whole.getSubBlock(start, std::min(kBlockSize, buffer.getNumSamples() - start)));
}

} // namespace

int main() {
    float sp = 0.0f, tp = 0.0f;

    // (0) First check the filter itself (no signal needed):
    //     - all taps sum to 4: zero insertion cuts the baseband amplitude to 1/4, and the lowpass has to multiply by 4 to restore it (README section 2.8)
    //     - each phase sums to about 1: for a DC (constant) input, all 4 interpolated points should equal that constant
    //     - left-right symmetry h[k] = h[63 - k]: linear phase, so the interpolation does not "skew" the waveform
    {
        TruePeakMeter meter;
        meter.prepare(1);
        const auto& c = meter.getCoefficients();
        double total = 0.0, maxAsym = 0.0;
        double phaseSum[kOversample] = {};
        for (int k = 0; k < kNumTaps; ++k) {
            const double hk = c[static_cast<size_t>(k % kOversample)][static_cast<size_t>(k / kOversample)];
            const int mirror = kNumTaps - 1 - k;
            const double hm = c[static_cast<size_t>(mirror % kOversample)][static_cast<size_t>(mirror / kOversample)];
            total += hk;
            phaseSum[k % kOversample] += hk;
            maxAsym = std::max(maxAsym, std::abs(hk - hm));
        }
        std::printf("filter: sum(h) = %.4f, phase sums = %.4f %.4f %.4f %.4f, max |h[k] - h[63-k]| = %.1e\n",
                    total, phaseSum[0], phaseSum[1], phaseSum[2], phaseSum[3], maxAsym);
        DAWLAB_CHECK_NEAR(total, 4.0, 1.0e-4);
        for (int p = 0; p < kOversample; ++p)
            DAWLAB_CHECK_NEAR(phaseSum[p], 1.0, 1.0e-3);
        DAWLAB_CHECK(maxAsym < 1.0e-6);
    }

    // (1) 1 kHz sine at -6 dBFS: samples land almost exactly on the peak, so the two kinds of peak should be about equal
    {
        dawlab::AudioBuffer sine = dawlab::makeSine(1000.0, kSampleRate, 0.5, 0.5f, 1);
        measure("1 kHz sine, amp 0.5", sine, sp, tp);
        DAWLAB_CHECK_NEAR(sp, -6.02, 0.05);
        DAWLAB_CHECK_NEAR(tp, -6.02, 0.15);
    }

    // (2) A 12 kHz (fs/4) sine with a 45 degree phase offset: every sample lands at +/-0.707*A,
    //     so the sample peak underestimates by 3.01 dB; after 4x oversampling the interpolated points land near the peak.
    //
    //     Why isn't it exactly -6.02 dBTP? 64 taps is an even length, so the prototype's center M = 31.5 is not on an integer,
    //     and the interpolated points of the 4 phases land at 1/8, 3/8, 5/8 and 7/8 between two original samples (not at 0, 1/4, 1/2, 3/4).
    //     This signal's peak lies exactly midway between two samples (1/2), and the nearest interpolated point is 1/8 sample = 11.25 degrees away,
    //     so we measure 0.5*cos(11.25 deg) = 0.4904 -> -6.19 dBTP, an underestimate of at most 0.17 dB (the 48-tap filter in BS.1770 is also of even length and has the same property).
    //     The first kTapsPerPhase samples are filter warm-up (the onset transient of a signal starting suddenly from 0) and are not counted.
    {
        dawlab::AudioBuffer sine(1, dawlab::secondsToSamples(0.5, kSampleRate));
        for (int i = 0; i < sine.getNumSamples(); ++i)
            sine.setSample(0, i, static_cast<float>(0.5 * std::sin(dawlab::kTwoPi * 0.25 * i + dawlab::kPi / 4.0)));
        measure("12 kHz (fs/4) sine, phase 45deg", sine, sp, tp, kTapsPerPhase);
        DAWLAB_CHECK_NEAR(sp, -9.03, 0.05); // 0.5 / sqrt(2) = 0.3536 -> -9.03 dBFS
        DAWLAB_CHECK_NEAR(tp, -6.19, 0.05); // the real peak 0.5 = -6.02 dBTP; the interpolated point is 1/8 sample off -> -6.19
        DAWLAB_CHECK_NEAR(tp - sp, 2.84, 0.05);
        dawlab::writeWav(dawlab::outputPath("l05_ex4_intersample.wav"), sine, kSampleRate);
    }

    // (3) Drum loop hard clipped to +/-1: the sample peak is exactly 0 dBFS, but after reconstruction the edges of the flat tops exceed 0 dBTP
    {
        dawlab::AudioBuffer drums = dawlab::makeDrumLoop(kSampleRate, 1);
        drums.applyGain(dawlab::dbToGain(12.0f));
        for (int ch = 0; ch < drums.getNumChannels(); ++ch) {
            float* d = drums.getWritePointer(ch);
            for (int i = 0; i < drums.getNumSamples(); ++i)
                d[i] = std::clamp(d[i], -1.0f, 1.0f);
        }
        measure("drums +12 dB, hard clip to 0 dBFS", drums, sp, tp);
        DAWLAB_CHECK_NEAR(sp, 0.0, 0.01);
        DAWLAB_CHECK(tp > sp); // the inter-sample peak is always higher than the sample peak
    }

    // (4) This chapter's Limiter guarantees sample peak <= ceiling, but the true peak is clearly higher (about 2 dB higher for the drum loop):
    //     this is why, in practice, the ceiling of a sample-peak limiter has to leave extra headroom below -1 dBTP
    {
        const float ceilingDb = -1.0f;
        dawlab::AudioBuffer drums = dawlab::makeDrumLoop(kSampleRate, 1);
        dawlab::dsp::Limiter limiter;
        limiter.prepare(kSampleRate, kBlockSize, drums.getNumChannels());
        limiter.setCeilingDb(ceilingDb);
        limiter.setInputGainDb(12.0f);
        limiter.setLookaheadMs(5.0f);
        limiter.setReleaseMs(80.0f);
        limiter.reset();
        processInBlocks(drums, limiter);
        measure("drums +12 dB -> Limiter (ceiling -1)", drums, sp, tp);
        DAWLAB_CHECK(sp <= ceilingDb + 1.0e-3f);
        DAWLAB_CHECK(tp > sp);        // the limiter holds down the samples; the inter-sample peak still exceeds the sample peak
        DAWLAB_CHECK(tp > ceilingDb); // that is: the output sample peak is <= -1 dBFS, yet the true peak exceeds -1 dBTP
        std::printf("  -> To guarantee %.0f dBTP, a sample-peak limiter's ceiling needs to be set to about %.2f dBFS (or switch to true-peak detection)\n",
                    ceilingDb, ceilingDb - std::max(0.0f, tp - sp));
    }

    return dawlab::test::summary("l05_ex4");
}
