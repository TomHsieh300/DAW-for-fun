// Exercise 6 solution: add lookahead to the Gate and verify that the head of a transient is not eaten by the attack
//
// Approach: the detector looks at the "current" signal, while the main signal goes through a delay line of length L
// and is then multiplied by the gain. This way the gate has already started opening (even finished opening) before
// the transient actually reaches the output. The price is an overall delay of L (the host compensates for it with
// latency compensation, so getLatencySamples() must report the correct value).
//
// Self-check (a 997 Hz, -6 dBFS tone burst starts abruptly out of complete silence, gate attack 10 ms, range -80 dB):
//   - No lookahead: the RMS of the first 2 ms of the burst is more than 10 dB below the input (the attack is still
//     climbing, so the head is eaten)
//   - Lookahead 10 ms: after delaying the output by 10 ms, the RMS of the first 2 ms of the burst is within 0.5 dB
//     of the input, and a sample-by-sample comparison output[burstStart + L + k] ~= input[burstStart + k] shows an
//     error < 0.01
//   - While the transient is still in the delay line: the output of [burstStart, burstStart + L) is still silent (< -60 dB)
//   - The delay amount is correct: in the steady-state region of the burst, output[i + L] equals input[i] sample by sample
// Why 997 Hz and not 1 kHz: the period of 1 kHz is 48 samples, which divides L = 480 exactly, so
// input[i + L] == input[i], and "delayed by L" looks identical to "not delayed at all" sample by sample, which makes
// the check meaningless. 997 Hz goes through 9.97 periods in 480 samples, a difference of 0.03 period; a delay error
// of even 1 sample would give a phase difference of 2*pi*997/48000 ~= 0.13 rad.
// Run: l08_ex6_solution -> writes dawlab_output/l08_ex6_input.wav, l08_ex6_no_lookahead.wav, l08_ex6_lookahead.wav.

#include <dawlab/Common.h>
#include <dawlab/dsp/Gate.h>

#include <cstdio>
#include <vector>

using namespace dawlab;
using dawlab::dsp::Gate;

namespace {

constexpr int kBlockSize = 512;
constexpr double kSampleRate = 48000.0;

class LookaheadGate {
public:
    // maxLookaheadMs determines the delay-line length (it can only be allocated in prepare)
    void prepare(double sampleRate, int maxBlockSize, int numChannels, float maxLookaheadMs = 20.0f) {
        sampleRate_ = sampleRate;
        gate_.prepare(sampleRate, maxBlockSize, numChannels);
        // Ring buffer length = max delay + 1: we write first and then read, so the read position for delay L must not
        // coincide with the write position (with L = length we would read back the value just written)
        maxDelay_ = std::max(0, msToSamples(maxLookaheadMs, sampleRate)) + 1;
        ring_.assign(static_cast<size_t>(numChannels) * static_cast<size_t>(maxDelay_), 0.0f);
        numChannels_ = numChannels;
        setLookaheadMs(lookaheadMs_);
        reset();
    }
    void reset() noexcept {
        gate_.reset();
        std::fill(ring_.begin(), ring_.end(), 0.0f);
        writePos_ = 0;
    }
    void setLookaheadMs(float ms) noexcept {
        lookaheadMs_ = std::max(0.0f, ms);
        delaySamples_ = std::min(maxDelay_ - 1, msToSamples(lookaheadMs_, sampleRate_));
    }
    int getLatencySamples() const noexcept { return delaySamples_; }
    Gate& gate() noexcept { return gate_; } // threshold / attack / ... are set directly on the internal Gate

    void process(AudioBlock block) noexcept {
        const int n = block.getNumSamples();
        const int nch = std::min(numChannels_, block.getNumChannels());
        for (int i = 0; i < n; ++i) {
            // 1. The detector uses the "current" sample
            float det = 0.0f;
            for (int ch = 0; ch < nch; ++ch)
                det = std::max(det, std::abs(block.getChannelPointer(ch)[i]));
            const float g = gate_.nextGain(det);
            // 2. Main signal: write into the ring buffer, read the value from delaySamples_ ago, then multiply by the gain
            const int readPos = (writePos_ - delaySamples_ + maxDelay_) % maxDelay_;
            for (int ch = 0; ch < nch; ++ch) {
                float* d = block.getChannelPointer(ch);
                float* ring = ring_.data() + static_cast<size_t>(ch) * static_cast<size_t>(maxDelay_);
                ring[writePos_] = d[i];
                d[i] = (delaySamples_ > 0 ? ring[readPos] : d[i]) * g;
            }
            writePos_ = (writePos_ + 1) % maxDelay_;
        }
    }

private:
    Gate gate_;
    double sampleRate_ = 48000.0;
    int numChannels_ = 0;
    int maxDelay_ = 1;
    int delaySamples_ = 0;
    int writePos_ = 0;
    float lookaheadMs_ = 0.0f;
    std::vector<float> ring_;
};

constexpr double kBurstHz = 997.0; // the period does not divide L = 480 (see the explanation at the top of the file)

// 0.5 s silence -> 0.3 s burst of 997 Hz at -6 dBFS -> 0.3 s silence
AudioBuffer makeBurst() {
    AudioBuffer b(1, secondsToSamples(1.1, kSampleRate));
    const int start = secondsToSamples(0.5, kSampleRate);
    const int len = secondsToSamples(0.3, kSampleRate);
    for (int i = 0; i < len; ++i)
        b.setSample(0, start + i, dbToGain(-6.0f) * static_cast<float>(std::sin(kTwoPi * kBurstHz * i / kSampleRate)));
    return b;
}

AudioBuffer runBlocks(const AudioBuffer& input, LookaheadGate& g) {
    AudioBuffer out = input;
    AudioBlock whole(out);
    for (int start = 0; start < out.getNumSamples(); start += kBlockSize)
        g.process(whole.getSubBlock(start, std::min(kBlockSize, out.getNumSamples() - start)));
    return out;
}

double rmsDb(const AudioBuffer& b, int start, int n) {
    const float r = rms(b.getReadPointer(0) + start, n);
    return r > 0.0f ? 20.0 * std::log10(r) : -120.0;
}

void setupGate(Gate& gate) {
    gate.setThresholdDb(-30.0f);
    gate.setHysteresisDb(6.0f);
    gate.setRangeDb(-80.0f);
    gate.setAttackMs(10.0f);
    gate.setHoldMs(20.0f);
    gate.setReleaseMs(50.0f);
}

} // namespace

int main() {
    std::printf("Chapter 08 Exercise 6: lookahead gate\n");
    const AudioBuffer input = makeBurst();
    const int burstStart = secondsToSamples(0.5, kSampleRate);
    const int head = msToSamples(2.0, kSampleRate); // measure the first 2 ms of the burst

    LookaheadGate noLa, la;
    noLa.prepare(kSampleRate, kBlockSize, 1);
    setupGate(noLa.gate());
    noLa.setLookaheadMs(0.0f);
    la.prepare(kSampleRate, kBlockSize, 1);
    setupGate(la.gate());
    la.setLookaheadMs(10.0f); // = the attack time: the gate has just finished opening when the transient arrives

    const AudioBuffer outNo = runBlocks(input, noLa);
    const AudioBuffer outLa = runBlocks(input, la);
    writeWav(outputPath("l08_ex6_input.wav"), input, kSampleRate);
    writeWav(outputPath("l08_ex6_no_lookahead.wav"), outNo, kSampleRate);
    writeWav(outputPath("l08_ex6_lookahead.wav"), outLa, kSampleRate);

    const int L = la.getLatencySamples();
    std::printf("\nlookahead 10 ms = %d samples (getLatencySamples)\n", L);
    DAWLAB_CHECK(L == msToSamples(10.0, kSampleRate));
    DAWLAB_CHECK(noLa.getLatencySamples() == 0);

    const double inHead = rmsDb(input, burstStart, head);
    const double noHead = rmsDb(outNo, burstStart, head);
    const double laHead = rmsDb(outLa, burstStart + L, head); // the output is delayed by L, so align by shifting later
    std::printf("RMS of the first 2 ms of the burst: input %.2f dB; no lookahead %.2f dB (head eaten by %.1f dB); lookahead %.2f dB\n", inHead,
                noHead, inHead - noHead, laHead);
    DAWLAB_CHECK(inHead - noHead > 10.0);
    DAWLAB_CHECK_NEAR(laHead, inHead, 0.5);

    // Sample-by-sample comparison of the head: RMS alone cannot distinguish "delayed by L" from "not delayed but the
    // gate happened to finish opening"; only the waveform itself can
    float headErr = 0.0f;
    for (int k = 0; k < head; ++k)
        headErr = std::max(headErr, std::abs(outLa.getSample(0, burstStart + L + k) - input.getSample(0, burstStart + k)));
    // During the L samples when the transient is still in the delay line, the output should still be silent
    const double preDb = rmsDb(outLa, burstStart, L);
    std::printf("First 2 ms of the burst, per sample: max |output[burstStart + L + k] - input[burstStart + k]| = %.2e; "
                "output RMS of [burstStart, burstStart + L) = %.1f dB\n",
                headErr, preDb);
    DAWLAB_CHECK(headErr < 0.01f);
    DAWLAB_CHECK(preDb < -60.0);

    // Steady-state region: output[i + L] == input[i] (gain = 1, only the delay differs)
    float maxErr = 0.0f;
    const int ssStart = burstStart + msToSamples(50.0, kSampleRate);
    const int ssLen = msToSamples(100.0, kSampleRate);
    for (int i = ssStart; i < ssStart + ssLen; ++i)
        maxErr = std::max(maxErr, std::abs(outLa.getSample(0, i + L) - input.getSample(0, i)));
    std::printf("Maximum error between output[i + L] and input[i] in the steady-state region: %.2e\n", maxErr);
    DAWLAB_CHECK(maxErr < 1.0e-6f);

    // The silent section stays closed (lookahead does not let noise leak out early -- there is no noise here, we just confirm 0)
    DAWLAB_CHECK(rmsDb(outLa, 0, secondsToSamples(0.45, kSampleRate)) < -100.0);
    DAWLAB_CHECK(!hasNanOrInf(outLa));
    return dawlab::test::summary("l08_ex6_lookahead_gate");
}
