// Exercise 4 starter: a simple 4x oversampling true-peak meter
//
// Goal: complete TruePeakMeter's designFilter() and process() so that it measures the inter-sample peak (dBTP).
//
// Before you start, read the "How do we upsample by 4?" subsection of README section 2.8
// (zero insertion -> image -> lowpass interpolation -> polyphase).
// That section covers only the intuition; the full theory of halfband filters, Kaiser windows, latency and so on
// is in Chapter 06 section 2.6, which you do not need to read first for this exercise.
//
// Four ideas chain together into this exercise:
//   1. Zero insertion: insert 3 zeros after every sample, so the sample rate becomes 4x (48 kHz -> 192 kHz).
//      The waveform becomes "the original samples plus a lot of zeros", which is not yet the smooth curve we want.
//   2. Image: inserting zeros is the same as multiplying by the pulse train "1,0,0,0,1,0,0,0...", so the spectrum
//      gains copies (images) of the original signal around fs, 2fs, and the original (baseband) copy has only 1/4 of its amplitude.
//   3. Lowpass interpolation: a lowpass with cutoff = the original fs/2 removes the images, and a gain of 4 restores
//      the amplitude, so the zero positions get "filled in" with values on the smooth curve. The impulse response of an
//      ideal lowpass is a sinc.
//   4. Polyphase: when an FIR runs after zero insertion, most of the multiplications are "x 0". Keep only the non-zero
//      terms and compute just 4 short dot products per input sample.
//
// Run: l05_ex4_starter -> it prints FAIL until you finish (expected). Complete TODO 1 and self-check (0) turns PASS first;
//      complete TODO 2 as well and (1)-(4) turn PASS.

#include <dawlab/Common.h>
#include <dawlab/dsp/Limiter.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

constexpr int kOversample = 4;
constexpr int kTapsPerPhase = 16;
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

    // Clear only the measured peaks and keep the filter history: used to "start measuring after the filter has warmed up" (already done)
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

                // TODO 2: polyphase interpolation; each input sample produces 4 upsampled samples
                //
                //   First work out "why only 16 inputs are needed": the upsampled FIR is y[m] = sum_k h[k]*u[m - k],
                //   where u is the zero-inserted signal, and u is non-zero only when m - k is a multiple of 4.
                //   Write m = 4n + p (p = 0..3); the only non-zero terms are k = 4j + p, and u[4(n - j)] is just
                //   the original x[n - j], so
                //       y[4n + p] = sum_j h[4j + p] * x[n - j],  j = 0..kTapsPerPhase-1
                //   Each phase p is a 16-tap sub-filter (coeffs_[p]) that consumes the most recent 16 "original" inputs.
                //   All 4 phases together = 64 multiplications; actually building the 4x-long zero-inserted signal and
                //   running a 64-tap FIR would take 256.
                //
                //   Steps:
                //   (a) Store x[i] in the circular buffer hist (length kTapsPerPhase, write position w).
                //       Afterwards, which slot holds x[n - j]? (Think about counting j slots back from w and wrapping when it goes below 0.)
                //   (b) For each phase p: take the dot product of coeffs_[p][j] and x[n - j] (accumulating in double is recommended),
                //       and take the max of |dot product| and truePeak_.
                //   (c) Advance w by one slot, wrapping back to 0 at the end.
                //
                //   Self-check: when the input is DC (a constant c), every phase's output should be c -- which is exactly why
                //   TODO 1 requires that "each phase's coefficients sum to about 1".
                (void)hist;
                (void)w;
            }
        }
    }

    float getSamplePeak() const noexcept { return samplePeak_; }
    float getTruePeak() const noexcept { return truePeak_; }
    float getSamplePeakDb() const noexcept { return dawlab::gainToDb(samplePeak_); }
    float getTruePeakDb() const noexcept { return dawlab::gainToDb(truePeak_); }
    // coeffs[p][j] = h[4j + p] (for the self-check that verifies the filter design; already done)
    const std::array<std::array<float, kTapsPerPhase>, kOversample>& getCoefficients() const noexcept { return coeffs_; }

private:
    void designFilter() {
        // TODO 1: design a 64-tap windowed-sinc lowpass prototype, then split it into 4 phases
        //
        //   (a) Ideal lowpass: at the new sample rate 4*fs, cutoff = the original fs/2 = 1/8 of the new sample rate.
        //       An ideal lowpass with cutoff fc has impulse response (2fc/fs')*sinc(2fc*n/fs'), with fs' = 4*fs.
        //       Substituting 2fc/fs' = 1/4 and multiplying by the gain of 4 that compensates the zero insertion gives h[n] = sinc(n / 4),
        //       where sinc(t) = sin(pi*t) / (pi*t), equal to 1 at t = 0.
        //       Observe: sinc(n/4) is 0 at n = +/-4, +/-8, ... -- that is, "the original sample positions are left untouched and only the zeros in between are filled in".
        //   (b) Truncation and centering: keep only kNumTaps taps (k = 0..kNumTaps-1) with the center at M = (kNumTaps - 1) / 2,
        //       so use sinc((k - M) / 4). Note that kNumTaps = 64 is even, so M = 31.5 is not an integer --
        //       the -6.19 dBTP in self-check (2) is caused by exactly this half-slot offset (explained in solutions/README.md).
        //   (c) Window: plain truncation makes ripples in the frequency response (discussed in Chapter 11 section 2.9). Multiply by a Hann window
        //       w[k] = 0.5 - 0.5*cos(2*pi*k / (kNumTaps - 1)) so the coefficients taper smoothly to 0 at both ends.
        //   (d) Normalization: the window moves the total gain away from 4; divide all h[k] by the sum and multiply by 4 so that sum(h) = 4
        //       (then the DC gain of the 4 phases combined is exactly 4, and each phase is about 1).
        //   (e) Polyphase decomposition: phase p uses h[p], h[p+4], h[p+8], ..., that is, coeffs_[p][j] = h[4j + p]
        //       (written another way: coeffs_[k % 4][k / 4] = h[k]). Why split it this way? See the derivation in TODO 2.
        //
        //   Compute in double and convert to float only when storing into coeffs_. You can use dawlab::kPi and dawlab::kTwoPi.
        //   When done, self-check (0) verifies: sum(h) = 4, each phase sums to about 1, h[k] = h[63 - k] (symmetric = linear phase).
    }

    int numChannels_ = 0;
    std::array<std::array<float, kTapsPerPhase>, kOversample> coeffs_{};
    std::vector<float> history_;
    std::vector<int> writeIndex_;
    float samplePeak_ = 0.0f;
    float truePeak_ = 0.0f;
};

// warmupSamples > 0: these first samples only fill the filter history and do not count toward the peak (already done).
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

    // (0) First check the filter itself (no signal needed; only TODO 1 must be done) (already done):
    //     - all taps sum to 4: zero insertion cuts the baseband amplitude to 1/4, and the lowpass has to multiply by 4 to restore it
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

    // (1) 1 kHz sine at -6 dBFS: the two kinds of peak should be about equal
    {
        dawlab::AudioBuffer sine = dawlab::makeSine(1000.0, kSampleRate, 0.5, 0.5f, 1);
        measure("1 kHz sine, amp 0.5", sine, sp, tp);
        DAWLAB_CHECK_NEAR(sp, -6.02, 0.05);
        DAWLAB_CHECK_NEAR(tp, -6.02, 0.15);
    }

    // (2) A sine at fs/4 with a 45 degree phase offset: the sample peak underestimates by 3.01 dB.
    //     The interpolation points of a 64-tap (even-length) filter fall at 1/8, 3/8, 5/8 and 7/8 of a sample and never hit the
    //     peak exactly in the middle, so the measured true peak is about 0.17 dB below the real -6.02 dBTP (think about why; the answer is in solutions/README.md).
    //     The first kTapsPerPhase samples are filter warm-up and are not counted.
    {
        dawlab::AudioBuffer sine(1, dawlab::secondsToSamples(0.5, kSampleRate));
        for (int i = 0; i < sine.getNumSamples(); ++i)
            sine.setSample(0, i, static_cast<float>(0.5 * std::sin(dawlab::kTwoPi * 0.25 * i + dawlab::kPi / 4.0)));
        measure("12 kHz (fs/4) sine, phase 45deg", sine, sp, tp, kTapsPerPhase);
        DAWLAB_CHECK_NEAR(sp, -9.03, 0.05);
        DAWLAB_CHECK_NEAR(tp, -6.19, 0.05);
        DAWLAB_CHECK_NEAR(tp - sp, 2.84, 0.05);
        dawlab::writeWav(dawlab::outputPath("l05_ex4_intersample.wav"), sine, kSampleRate);
    }

    // (3) Hard-clipped drum loop: the true peak exceeds the sample peak
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
        DAWLAB_CHECK(tp > sp);
    }

    // (4) Limiter output: sample peak <= ceiling, but the true peak is clearly higher (above -1 dBTP)
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
        DAWLAB_CHECK(tp > sp);
        DAWLAB_CHECK(tp > ceilingDb);
    }

    return dawlab::test::summary("l05_ex4");
}
