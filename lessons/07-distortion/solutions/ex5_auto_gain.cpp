// 練習 5 解答：auto gain compensation（輸出 RMS = 輸入 RMS）
//
// 做法：兩個 one-pole 平滑的 mean-square 偵測器（跟第 04 章 EnvelopeFollower 的 RMS 模式同一個概念）：
//     ms[n] = a · ms[n-1] + (1 - a) · x[n]²，a = exp(-1 / (τ fs))
// 然後 gain[n] = sqrt(ms_in[n] / ms_out[n])。
//
// 幾個設計細節：
//   - 時間常數 100 ms：太短會跟著波形的週期起伏（變成 tremolo / 失真），太長會讓音頭「衝」出去。
//   - 分母加下限（1e-12）避免除以 0；gain 限制在 ±40 dB，避免靜音 → 有訊號的瞬間 gain 爆衝。
//   - 兩個偵測器的時間常數一樣，所以穩態時 gain 會收斂到「輸入 RMS / 輸出 RMS」。
//   - 因為 gain 是每個 sample 連續變化，不需要另外做 ramp。
//
// 執行：l07_ex5_solution

#include <dawlab/Common.h>
#include <dawlab/dsp/Distortion.h>

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

class AutoGain {
public:
    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        sampleRate_ = sampleRate;
        (void)maxBlockSize;
        inMs_.assign(static_cast<size_t>(numChannels), 0.0f);
        outMs_.assign(static_cast<size_t>(numChannels), 0.0f);
        updateCoefficient();
    }
    void reset() noexcept {
        std::fill(inMs_.begin(), inMs_.end(), 0.0f);
        std::fill(outMs_.begin(), outMs_.end(), 0.0f);
        gain_ = 1.0f;
    }
    void setWindowMs(float ms) noexcept {
        windowMs_ = std::max(1.0f, ms);
        updateCoefficient();
    }
    float getGainDb() const noexcept { return dawlab::gainToDb(gain_); }

    void process(const dawlab::AudioBlock& dry, dawlab::AudioBlock wet) noexcept {
        const int nch = std::min({dry.getNumChannels(), wet.getNumChannels(), static_cast<int>(inMs_.size())});
        const int n = std::min(dry.getNumSamples(), wet.getNumSamples());
        const float a = a_, b = 1.0f - a_;
        for (int ch = 0; ch < nch; ++ch) {
            const float* x = dry.getChannelPointer(ch);
            float* y = wet.getChannelPointer(ch);
            float inMs = inMs_[static_cast<size_t>(ch)];
            float outMs = outMs_[static_cast<size_t>(ch)];
            for (int i = 0; i < n; ++i) {
                inMs = a * inMs + b * x[i] * x[i];
                outMs = a * outMs + b * y[i] * y[i];
                const float g = std::clamp(std::sqrt(inMs / std::max(outMs, 1.0e-12f)), 0.01f, 100.0f);
                y[i] *= g;
                gain_ = g;
            }
            // 把極小值清成 0，避免 denormal
            inMs_[static_cast<size_t>(ch)] = inMs < 1.0e-30f ? 0.0f : inMs;
            outMs_[static_cast<size_t>(ch)] = outMs < 1.0e-30f ? 0.0f : outMs;
        }
    }

private:
    void updateCoefficient() noexcept {
        a_ = static_cast<float>(std::exp(-1.0 / (windowMs_ * 0.001 * sampleRate_)));
    }

    double sampleRate_ = 48000.0;
    float windowMs_ = 100.0f;
    float a_ = 0.999f;
    float gain_ = 1.0f;
    std::vector<float> inMs_, outMs_;
};

struct CompensatedDistortion {
    dawlab::dsp::Distortion distortion;
    AutoGain autoGain;
    dawlab::AudioBuffer dryScratch;

    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        distortion.setOversamplingFactorLog2(0);
        distortion.prepare(sampleRate, maxBlockSize, numChannels);
        autoGain.prepare(sampleRate, maxBlockSize, numChannels);
        dryScratch.setSize(numChannels, maxBlockSize);
    }
    void process(dawlab::AudioBlock block) noexcept {
        dawlab::AudioBlock dry(dryScratch.getArrayOfWritePointers(), block.getNumChannels(), block.getNumSamples());
        dry.copyFrom(block);
        distortion.process(block);
        autoGain.process(dry, block);
    }
};

template <typename Processor>
void processInBlocks(dawlab::AudioBuffer& buffer, Processor& processor) {
    dawlab::AudioBlock whole(buffer);
    for (int start = 0; start < buffer.getNumSamples(); start += kBlockSize) {
        const int len = std::min(kBlockSize, buffer.getNumSamples() - start);
        processor.process(whole.getSubBlock(start, len));
    }
}

} // namespace

int main() {
    using dawlab::dsp::DistortionType;

    // (1) 穩態正弦
    {
        dawlab::AudioBuffer in = dawlab::makeSine(220.0, kSampleRate, 2.0, 0.1f, 1);
        dawlab::AudioBuffer out = in;
        CompensatedDistortion cd;
        cd.prepare(kSampleRate, kBlockSize, 1);
        cd.distortion.setType(DistortionType::HardClip);
        cd.distortion.setDriveDb(30.0f);
        processInBlocks(out, cd);
        const int skip = dawlab::secondsToSamples(1.0, kSampleRate), n = in.getNumSamples() - skip;
        const double inDb = dawlab::gainToDb(dawlab::rms(in.getReadPointer(0) + skip, n));
        const double outDb = dawlab::gainToDb(dawlab::rms(out.getReadPointer(0) + skip, n));
        std::printf("(1) 正弦 -20 dBFS、hard clip 30 dB：輸入 RMS %.2f dBFS，輸出 RMS %.2f dBFS，gain = %.2f dB\n", inDb,
                    outDb, cd.autoGain.getGainDb());
        DAWLAB_CHECK_NEAR(outDb, inDb, 0.2);
        // 0.1 × 10^(30/20) = 3.16 → |u| < 1 只佔 asin(1/3.16)/(π/2) ≈ 20% 的時間，其餘 clip 在 ±1
        //   → 輸出 mean square ≈ (2/π)[10·(θ/2 - sin2θ/4) + (π/2 - θ)]，θ = 0.3217 → 0.864，RMS ≈ 0.930（-0.63 dBFS）
        //   輸入 RMS = 0.0707（-23.01 dBFS）→ gain ≈ -23.01 - (-0.63) = -22.38 dB（純方波近似會得到 -23.0 dB）
        DAWLAB_CHECK_NEAR(cd.autoGain.getGainDb(), -22.38, 0.5);
    }

    // (2) pluck sequence
    {
        dawlab::AudioBuffer in = dawlab::makePluckSequence(kSampleRate, 4.0, 0.4f, 2);
        dawlab::AudioBuffer raw = in, comp = in;
        dawlab::dsp::Distortion plain;
        plain.setOversamplingFactorLog2(0);
        plain.prepare(kSampleRate, kBlockSize, 2);
        plain.setType(DistortionType::Fuzz);
        plain.setDriveDb(30.0f);
        processInBlocks(raw, plain);

        CompensatedDistortion cd;
        cd.prepare(kSampleRate, kBlockSize, 2);
        cd.distortion.setType(DistortionType::Fuzz);
        cd.distortion.setDriveDb(30.0f);
        processInBlocks(comp, cd);

        const double inDb = dawlab::gainToDb(dawlab::rms(in, 0));
        const double rawDb = dawlab::gainToDb(dawlab::rms(raw, 0));
        const double compDb = dawlab::gainToDb(dawlab::rms(comp, 0));
        std::printf("(2) pluck、fuzz 30 dB：輸入 RMS %.2f，無補償 %.2f，有補償 %.2f dBFS\n", inDb, rawDb, compDb);
        DAWLAB_CHECK(rawDb - inDb > 6.0);
        DAWLAB_CHECK_NEAR(compDb, inDb, 1.0);
        DAWLAB_CHECK(!dawlab::hasNanOrInf(comp));
        dawlab::writeWav(dawlab::outputPath("l07_ex5_autogain.wav"), comp, kSampleRate);
        std::printf("    已輸出 dawlab_output/l07_ex5_autogain.wav\n");
    }

    // (3) 靜音
    {
        dawlab::AudioBuffer silence(1, 4096);
        CompensatedDistortion cd;
        cd.prepare(kSampleRate, kBlockSize, 1);
        cd.distortion.setDriveDb(40.0f);
        processInBlocks(silence, cd);
        DAWLAB_CHECK(!dawlab::hasNanOrInf(silence));
        DAWLAB_CHECK(dawlab::peakAbs(silence) == 0.0f);
    }

    return dawlab::test::summary("l07_ex5");
}
