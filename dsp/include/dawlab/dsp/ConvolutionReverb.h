#pragma once
// 第 12 章：Convolution reverb —— uniform partitioned FFT convolution（overlap-save）
//
// 直接卷積 y[n] = Σ_k h[k]·x[n-k] 每個 sample 要做 M 次乘加（M = IR 長度，4 秒的 IR 就是 192000 次），
// 太貴。做法：
//
//   1. 把 IR 切成 K 段、每段 P 個 sample（P = partition size，= 2 的次方、>= maxBlockSize）
//      h = [h_0 | h_1 | ... | h_{K-1}]，每段補零到 N = 2P 後做 FFT → H_k（N/2+1 個 bin）
//   2. 輸入每累積 P 個 sample 做一次「frame」：frame = [上一塊 P 個 | 這一塊 P 個]（overlap-save）
//      FFT(frame) 推進 frequency-delay line（FDL）：X_j、X_{j-1}、X_{j-2}...
//   3. Y = Σ_k H_k · X_{j-k}   （頻域相乘 = 時域卷積；延遲 k 塊的頻譜乘上第 k 段 IR）
//   4. IFFT(Y) 的後半 P 個 sample 就是這一塊的正確輸出（前半是 circular wrap，丟掉）
//
// Latency = P：第 n 個輸入要等到這一塊 P 個 sample 都收齊才能算，所以輸出整體晚 P 個 sample。
// 內建的 dry/wet mix 也把 dry 延遲 P 個 sample（直接取 input FIFO 裡 P 個 sample 前的值），
// 讓 dry 與 wet 對齊：否則 IR 有直達音時，dry 和晚 P 的直達音疊加會產生間距 fs/P 的 comb filtering。
// 每個輸出 sample 的成本 ≈ K·(N/2+1)/P 次 complex MAC + 2 次 FFT/P ≈ K 次 complex MAC，
// 相較直接卷積的 K·P 次實數 MAC，省下約 P/4 倍（P = 512 時約 100 倍以上）。
//
// 使用第 11 章的 Fft（forwardReal / inverseReal）。

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
    // ---- 標準介面 ---------------------------------------------------------------
    // partition size P = 大於等於 maxBlockSize 的最小 2 的次方（最小 64）
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
        currentMix_ = mix_; // 沒有聲音殘留時，mix 直接跳到目標值（不需要平滑）
        mixAtBlockStart_ = mix_;
    }

    // 載入 impulse response（會 allocate，不可在 audio thread 呼叫；呼叫時不可同時在 process）。
    // ir 的 channel 數可以是 1（所有輸出 channel 共用）或 >= numChannels（各用各的）。
    // 可以在 prepare 之前或之後呼叫。
    void setImpulseResponse(const AudioBuffer& ir) {
        ir_ = ir;
        rebuildPartitions();
        reset();
    }

    // 0 = 全 dry，1 = 全 wet（線性混合，有平滑）。dry 在內部也延遲 P 個 sample，
    // 所以任何 mix 下整個輸出的 latency 都是 getLatencySamples()（mix = 0 時就是單純延遲 P 的 dry）。
    void setMix(float mix) noexcept {
        if (std::isnan(mix)) // std::clamp(NaN) 會原樣回傳 NaN → 輸出永久變 NaN；保留上一個有效值
            return;
        mix_ = std::clamp(mix, 0.0f, 1.0f);
    }
    float getMix() const noexcept { return mix_; }

    int getLatencySamples() const noexcept { return partitionSize_; }
    int getPartitionSize() const noexcept { return partitionSize_; }
    int getFftSize() const noexcept { return fftSize_; }
    int getNumPartitions() const noexcept { return numPartitions_; }
    int getImpulseResponseLength() const noexcept { return ir_.getNumSamples(); }

    // 估計記憶體用量（bytes）：IR 頻譜 + 每個 channel 的 FDL
    size_t getSpectraMemoryBytes() const noexcept {
        const size_t perSpectrum = static_cast<size_t>(numBins_) * sizeof(std::complex<float>);
        size_t total = 0;
        for (const auto& chSpectra : irSpectra_)
            total += chSpectra.size() * perSpectrum;
        for (const ChannelState& c : channels_)
            total += c.fdl.size() * perSpectrum;
        return total;
    }

    // in-place；任何 <= maxBlockSize 的 block 長度都可以（內部用 FIFO 湊滿 P 個 sample 才做一次 FFT）
    void process(AudioBlock block) noexcept {
        const int nch = std::min(block.getNumChannels(), numChannels_);
        const int n = block.getNumSamples();
        for (int ch = 0; ch < nch; ++ch) {
            ChannelState& c = channels_[static_cast<size_t>(ch)];
            const std::vector<std::vector<std::complex<float>>>* spectra = irSpectraFor(ch);
            float* data = block.getChannelPointer(ch);
            float mix = ch == 0 ? currentMix_ : mixAtBlockStart_; // 每個 channel 從同一個起點平滑
            if (ch == 0)
                mixAtBlockStart_ = currentMix_;
            for (int i = 0; i < n; ++i) {
                const size_t pos = static_cast<size_t>(c.fifoPos);
                const float in = data[i];
                // dry 也要延遲 P 個 sample，才會和 wet 對齊（整個輸出的 latency 一致 = P）。
                // inputFifo[pos] 在被覆寫之前，存的正好是 P 個 sample 前同一位置的輸入 x[n-P]，
                // 所以不需要額外的 delay line。
                const float dryDelayed = c.inputFifo[pos];
                const float wet = c.outputFifo[pos]; // 晚 P 個 sample 的 wet
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

    // ---- 合成 IR ----------------------------------------------------------------
    // 指數衰減的白雜訊（RT60 = rt60Seconds）：每個 channel 用不同 seed（去相關 → stereo 感），
    // 經過固定 cutoff 的 one-pole lowpass 做一點「空氣吸收」的音色，最後正規化成單位能量（Σ h² = 1）。
    // 長度 = 1.1 × RT60（最後 10% 再加一段 fade，避免截斷產生 click）。
    static AudioBuffer makeSyntheticImpulseResponse(double sampleRate, double rt60Seconds, int numChannels,
                                                    uint32_t seed = 1) {
        rt60Seconds = std::max(0.05, rt60Seconds);
        const int length = std::max(16, static_cast<int>(std::lround(1.1 * rt60Seconds * sampleRate)));
        const int fadeStart = static_cast<int>(std::lround(rt60Seconds * sampleRate));
        AudioBuffer ir(std::max(1, numChannels), length);
        // -60 dB = e^(-6.9078) → envelope = exp(-ln(1000)·t / RT60)
        const double decayPerSample = std::exp(-std::log(1000.0) / (rt60Seconds * sampleRate));
        // 固定 8 kHz 的 lowpass，讓雜訊不要太「白」
        const double lpCoef = 1.0 - std::exp(-kTwoPi * 8000.0 / sampleRate);
        for (int ch = 0; ch < ir.getNumChannels(); ++ch) {
            NoiseGenerator noise(seed + 7919u * static_cast<uint32_t>(ch));
            double env = 1.0, lp = 0.0, energy = 0.0;
            float* h = ir.getWritePointer(ch);
            for (int i = 0; i < length; ++i) {
                lp += lpCoef * (noise.next() - lp);
                double v = lp * env;
                if (i >= fadeStart) // 線性 fade-out
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
        std::vector<float> inputFifo;  // 累積到 P 個就做一次 frame
        std::vector<float> outputFifo; // 上一個 frame 算出來的 P 個輸出
        std::vector<float> frame;      // [上一塊 | 這一塊]，長 N = 2P
        std::vector<float> ifftOut;    // IFFT 結果，長 N
        std::vector<std::vector<std::complex<float>>> fdl; // frequency-delay line：K 個頻譜
        std::vector<std::complex<float>> accumulator;      // Y = Σ H_k · X_{j-k}
        int fifoPos = 0;
        int fdlPos = 0; // 最新的頻譜放在 fdl[fdlPos]
    };

    const std::vector<std::vector<std::complex<float>>>* irSpectraFor(int ch) const noexcept {
        if (irSpectra_.empty())
            return nullptr;
        const size_t idx = static_cast<size_t>(ch) < irSpectra_.size() ? static_cast<size_t>(ch) : 0;
        return &irSpectra_[idx];
    }

    // 一個 frame：FFT 這一塊、更新 FDL、頻域乘加、IFFT、取後半
    void processPartition(ChannelState& c, const std::vector<std::vector<std::complex<float>>>* spectra) noexcept {
        const size_t P = static_cast<size_t>(partitionSize_);
        // overlap-save：frame 前半 = 上一塊，後半 = 這一塊
        std::copy(c.frame.begin() + static_cast<std::ptrdiff_t>(P), c.frame.end(), c.frame.begin());
        std::copy(c.inputFifo.begin(), c.inputFifo.end(), c.frame.begin() + static_cast<std::ptrdiff_t>(P));

        if (numPartitions_ == 0 || spectra == nullptr || c.fdl.empty()) {
            std::fill(c.outputFifo.begin(), c.outputFifo.end(), 0.0f);
            return;
        }

        // 新頻譜寫進 FDL（覆蓋最舊的那格）
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
        // 後半 P 個是正確的 linear convolution 輸出
        std::copy(c.ifftOut.begin() + static_cast<std::ptrdiff_t>(P), c.ifftOut.end(), c.outputFifo.begin());
    }

    // 把 IR 切段、補零、FFT。prepare 與 setImpulseResponse 都會呼叫（兩者都到齊才真的算）。
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
                std::copy(h + start, h + start + len, padded.begin()); // 前半 = IR 段，後半 = 0
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
