#pragma once
// Chapter 12: Convolution reverb -- uniform partitioned FFT convolution (overlap-save)
//
// Direct convolution y[n] = Σ_k h[k]·x[n-k] needs M multiply-adds per sample (M = IR length; a 4-second IR means 192000),
// which is far too expensive. The approach:
//
//   1. Cut the IR into K segments of P samples each (P = partition size, a power of 2, >= maxBlockSize)
//      h = [h_0 | h_1 | ... | h_{K-1}]; zero-pad each segment to N = 2P and FFT it → H_k (N/2+1 bins)
//   2. Every time P input samples have accumulated, process one "frame": frame = [previous P | this P] (overlap-save)
//      The FFT of the frame is pushed into a frequency-delay line (FDL): X_j, X_{j-1}, X_{j-2}...
//   3. Y = Σ_k H_k · X_{j-k}   (multiplication in the frequency domain = convolution in the time domain;
//      the spectrum delayed by k blocks is multiplied by the k-th IR segment)
//   4. The second half (P samples) of IFFT(Y) is the correct output for this block (the first half is circular wrap-around and is discarded)
//
// Latency = P: the n-th input sample has to wait until all P samples of its block have arrived before it can be
// processed, so the whole output is P samples late.
// The built-in dry/wet mix also delays the dry signal by P samples (by taking the value from P samples ago out of the input FIFO)
// so dry and wet line up: otherwise, when the IR contains a direct sound, dry summed with the direct sound that is P samples late
// would produce comb filtering with spacing fs/P.
// Cost per output sample ≈ K·(N/2+1)/P complex MACs + 2 FFTs/P ≈ K complex MACs,
// compared with K·P real MACs for direct convolution, a saving of about P/4 times (over 100 times for P = 512).
//
// Uses chapter 11's Fft (forwardReal / inverseReal).

#include <dawlab/AudioBuffer.h>
#include <dawlab/DspMath.h>
#include <dawlab/Signals.h>
#include <dawlab/dsp/Fft.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <memory>
#include <vector>

namespace dawlab::dsp {

class ConvolutionReverb {
public:
    // ---- Standard interface ------------------------------------------------------
    // partition size P = the smallest power of 2 that is >= maxBlockSize (minimum 64)
    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        sampleRate_ = sampleRate;
        numChannels_ = std::max(1, numChannels);
        partitionSize_ = 64;
        fftOrder_ = 7; // 2^7 = 128 = 2P
        while (partitionSize_ < maxBlockSize) {
            partitionSize_ *= 2;
            ++fftOrder_;
        }
        fftSize_ = 2 * partitionSize_;
        numBins_ = fftSize_ / 2 + 1;
        fft_ = std::make_unique<Fft>(fftOrder_);

        channels_.assign(static_cast<size_t>(numChannels_), ChannelState{});
        for (ChannelState& c : channels_) {
            c.inputFifo.assign(static_cast<size_t>(partitionSize_), 0.0f);
            c.outputFifo.assign(static_cast<size_t>(partitionSize_), 0.0f);
            c.frame.assign(static_cast<size_t>(fftSize_), 0.0f);
            c.ifftOut.assign(static_cast<size_t>(fftSize_), 0.0f);
            c.accumulator.assign(static_cast<size_t>(numBins_), {0.0f, 0.0f});
        }
        rebuildPartitions();
        smoothCoef_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.02 * sampleRate)));
        reset();
    }

    void reset() noexcept {
        for (ChannelState& c : channels_) {
            std::fill(c.inputFifo.begin(), c.inputFifo.end(), 0.0f);
            std::fill(c.outputFifo.begin(), c.outputFifo.end(), 0.0f);
            std::fill(c.frame.begin(), c.frame.end(), 0.0f);
            for (auto& spectrum : c.fdl)
                std::fill(spectrum.begin(), spectrum.end(), std::complex<float>{0.0f, 0.0f});
            c.fifoPos = 0;
            c.fdlPos = 0;
        }
        currentMix_ = mix_; // with no sound left over, mix jumps straight to its target (no smoothing needed)
        mixAtBlockStart_ = mix_;
    }

    // Loads an impulse response (allocates, so it must not be called on the audio thread, and not while process is running).
    // The IR may have 1 channel (shared by all output channels) or >= numChannels channels (one per output channel).
    // May be called before or after prepare.
    void setImpulseResponse(const AudioBuffer& ir) {
        ir_ = ir;
        rebuildPartitions();
        reset();
    }

    // 0 = all dry, 1 = all wet (linear mix, smoothed). The dry signal is also delayed by P samples internally,
    // so at any mix the latency of the whole output is getLatencySamples() (at mix = 0 it is simply the dry signal delayed by P).
    void setMix(float mix) noexcept {
        if (std::isnan(mix)) // std::clamp(NaN) returns the NaN unchanged → the output would turn permanently into NaN; keep the previous valid value
            return;
        mix_ = std::clamp(mix, 0.0f, 1.0f);
    }
    float getMix() const noexcept { return mix_; }

    int getLatencySamples() const noexcept { return partitionSize_; }
    int getPartitionSize() const noexcept { return partitionSize_; }
    int getFftSize() const noexcept { return fftSize_; }
    int getNumPartitions() const noexcept { return numPartitions_; }
    int getImpulseResponseLength() const noexcept { return ir_.getNumSamples(); }

    // Estimated memory use (bytes): the IR spectra + the FDL of every channel
    size_t getSpectraMemoryBytes() const noexcept {
        const size_t perSpectrum = static_cast<size_t>(numBins_) * sizeof(std::complex<float>);
        size_t total = 0;
        for (const auto& chSpectra : irSpectra_)
            total += chSpectra.size() * perSpectrum;
        for (const ChannelState& c : channels_)
            total += c.fdl.size() * perSpectrum;
        return total;
    }

    // In place; any block length <= maxBlockSize works (internally a FIFO collects P samples before each FFT is done)
    void process(AudioBlock block) noexcept {
        const int nch = std::min(block.getNumChannels(), numChannels_);
        const int n = block.getNumSamples();
        for (int ch = 0; ch < nch; ++ch) {
            ChannelState& c = channels_[static_cast<size_t>(ch)];
            const std::vector<std::vector<std::complex<float>>>* spectra = irSpectraFor(ch);
            float* data = block.getChannelPointer(ch);
            float mix = ch == 0 ? currentMix_ : mixAtBlockStart_; // every channel is smoothed from the same starting point
            if (ch == 0)
                mixAtBlockStart_ = currentMix_;
            for (int i = 0; i < n; ++i) {
                const size_t pos = static_cast<size_t>(c.fifoPos);
                const float in = data[i];
                // The dry signal must also be delayed by P samples to line up with wet (the latency of the whole output is then uniformly P).
                // Before it is overwritten, inputFifo[pos] holds exactly the input at the same position P samples ago, x[n-P],
                // so no extra delay line is needed.
                const float dryDelayed = c.inputFifo[pos];
                const float wet = c.outputFifo[pos]; // the wet signal, P samples late
                c.inputFifo[pos] = in;
                ++c.fifoPos;
                if (c.fifoPos == partitionSize_) {
                    processPartition(c, spectra);
                    c.fifoPos = 0;
                }
                mix += smoothCoef_ * (mix_ - mix);
                data[i] = dryDelayed * (1.0f - mix) + wet * mix;
            }
            if (ch == 0)
                currentMix_ = mix;
        }
    }

    // ---- Synthetic IR ------------------------------------------------------------
    // Exponentially decaying white noise (RT60 = rt60Seconds): each channel uses a different seed (decorrelated → a stereo feel),
    // passed through a fixed-cutoff one-pole lowpass to give a little "air absorption" coloration, and finally normalized to unit energy (Σ h² = 1).
    // Length = 1.1 × RT60 (the last 10% gets an extra fade so that truncation does not cause a click).
    static AudioBuffer makeSyntheticImpulseResponse(double sampleRate, double rt60Seconds, int numChannels,
                                                    uint32_t seed = 1) {
        rt60Seconds = std::max(0.05, rt60Seconds);
        const int length = std::max(16, static_cast<int>(std::lround(1.1 * rt60Seconds * sampleRate)));
        const int fadeStart = static_cast<int>(std::lround(rt60Seconds * sampleRate));
        AudioBuffer ir(std::max(1, numChannels), length);
        // -60 dB = e^(-6.9078) → envelope = exp(-ln(1000)·t / RT60)
        const double decayPerSample = std::exp(-std::log(1000.0) / (rt60Seconds * sampleRate));
        // A fixed 8 kHz lowpass, so the noise is not too "white"
        const double lpCoef = 1.0 - std::exp(-kTwoPi * 8000.0 / sampleRate);
        for (int ch = 0; ch < ir.getNumChannels(); ++ch) {
            NoiseGenerator noise(seed + 7919u * static_cast<uint32_t>(ch));
            double env = 1.0, lp = 0.0, energy = 0.0;
            float* h = ir.getWritePointer(ch);
            for (int i = 0; i < length; ++i) {
                lp += lpCoef * (noise.next() - lp);
                double v = lp * env;
                if (i >= fadeStart) // linear fade-out
                    v *= 1.0 - static_cast<double>(i - fadeStart) / static_cast<double>(length - fadeStart);
                h[i] = static_cast<float>(v);
                energy += v * v;
                env *= decayPerSample;
            }
            const float norm = energy > 0.0 ? static_cast<float>(1.0 / std::sqrt(energy)) : 1.0f;
            for (int i = 0; i < length; ++i)
                h[i] *= norm;
        }
        return ir;
    }

private:
    struct ChannelState {
        std::vector<float> inputFifo;  // once P samples have accumulated, one frame is processed
        std::vector<float> outputFifo; // the P outputs computed from the previous frame
        std::vector<float> frame;      // [previous block | this block], length N = 2P
        std::vector<float> ifftOut;    // IFFT result, length N
        std::vector<std::vector<std::complex<float>>> fdl; // frequency-delay line: K spectra
        std::vector<std::complex<float>> accumulator;      // Y = Σ H_k · X_{j-k}
        int fifoPos = 0;
        int fdlPos = 0; // the newest spectrum is stored in fdl[fdlPos]
    };

    const std::vector<std::vector<std::complex<float>>>* irSpectraFor(int ch) const noexcept {
        if (irSpectra_.empty())
            return nullptr;
        const size_t idx = static_cast<size_t>(ch) < irSpectra_.size() ? static_cast<size_t>(ch) : 0;
        return &irSpectra_[idx];
    }

    // One frame: FFT this block, update the FDL, multiply-accumulate in the frequency domain, IFFT, take the second half
    void processPartition(ChannelState& c, const std::vector<std::vector<std::complex<float>>>* spectra) noexcept {
        const size_t P = static_cast<size_t>(partitionSize_);
        // overlap-save: the first half of the frame = the previous block, the second half = this block
        std::copy(c.frame.begin() + static_cast<std::ptrdiff_t>(P), c.frame.end(), c.frame.begin());
        std::copy(c.inputFifo.begin(), c.inputFifo.end(), c.frame.begin() + static_cast<std::ptrdiff_t>(P));

        if (numPartitions_ == 0 || spectra == nullptr || c.fdl.empty()) {
            std::fill(c.outputFifo.begin(), c.outputFifo.end(), 0.0f);
            return;
        }

        // Write the new spectrum into the FDL (overwriting the oldest slot)
        c.fdlPos = (c.fdlPos + 1) % numPartitions_;
        fft_->forwardReal(c.frame.data(), c.fdl[static_cast<size_t>(c.fdlPos)].data());

        // Y = Σ_k H_k · X_{j-k}
        std::fill(c.accumulator.begin(), c.accumulator.end(), std::complex<float>{0.0f, 0.0f});
        for (int k = 0; k < numPartitions_; ++k) {
            int idx = c.fdlPos - k;
            if (idx < 0)
                idx += numPartitions_;
            const std::complex<float>* X = c.fdl[static_cast<size_t>(idx)].data();
            const std::complex<float>* H = (*spectra)[static_cast<size_t>(k)].data();
            std::complex<float>* Y = c.accumulator.data();
            for (int b = 0; b < numBins_; ++b)
                Y[b] += H[b] * X[b];
        }

        fft_->inverseReal(c.accumulator.data(), c.ifftOut.data());
        // The second half (P samples) is the correct linear-convolution output
        std::copy(c.ifftOut.begin() + static_cast<std::ptrdiff_t>(P), c.ifftOut.end(), c.outputFifo.begin());
    }

    // Cut the IR into segments, zero-pad and FFT them. Both prepare and setImpulseResponse call this (the work is only done once both have happened).
    void rebuildPartitions() {
        irSpectra_.clear();
        numPartitions_ = 0;
        if (!fft_ || ir_.getNumSamples() == 0)
            return;
        numPartitions_ = (ir_.getNumSamples() + partitionSize_ - 1) / partitionSize_;
        std::vector<float> padded(static_cast<size_t>(fftSize_));
        irSpectra_.resize(static_cast<size_t>(ir_.getNumChannels()));
        for (int ch = 0; ch < ir_.getNumChannels(); ++ch) {
            auto& chSpectra = irSpectra_[static_cast<size_t>(ch)];
            chSpectra.assign(static_cast<size_t>(numPartitions_),
                             std::vector<std::complex<float>>(static_cast<size_t>(numBins_)));
            const float* h = ir_.getReadPointer(ch);
            for (int k = 0; k < numPartitions_; ++k) {
                std::fill(padded.begin(), padded.end(), 0.0f);
                const int start = k * partitionSize_;
                const int len = std::min(partitionSize_, ir_.getNumSamples() - start);
                std::copy(h + start, h + start + len, padded.begin()); // first half = IR segment, second half = 0
                fft_->forwardReal(padded.data(), chSpectra[static_cast<size_t>(k)].data());
            }
        }
        for (ChannelState& c : channels_) {
            c.fdl.assign(static_cast<size_t>(numPartitions_),
                         std::vector<std::complex<float>>(static_cast<size_t>(numBins_), {0.0f, 0.0f}));
            c.fdlPos = 0;
        }
    }

    double sampleRate_ = 48000.0;
    int numChannels_ = 0;
    int partitionSize_ = 0;
    int fftOrder_ = 0;
    int fftSize_ = 0;
    int numBins_ = 0;
    int numPartitions_ = 0;
    std::unique_ptr<Fft> fft_;
    AudioBuffer ir_;
    std::vector<std::vector<std::vector<std::complex<float>>>> irSpectra_; // [irChannel][partition][bin]
    std::vector<ChannelState> channels_;
    float mix_ = 1.0f;
    float currentMix_ = 1.0f;
    float mixAtBlockStart_ = 1.0f;
    float smoothCoef_ = 0.001f;
};

} // namespace dawlab::dsp
