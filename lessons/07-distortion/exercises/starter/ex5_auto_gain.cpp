// 練習 5（起始碼）：auto gain compensation —— 讓 distortion 的輸出 RMS 跟輸入 RMS 一樣
//
// 問題：drive 轉大，音量跟著暴增，你根本分不清「變好聽」還是「變大聲」。
// 解法：量輸入與輸出的 RMS envelope（one-pole 平滑的 x²，時間常數約 100 ms），
//       gain = sqrt(inMeanSquare / outMeanSquare)，再把 gain 乘回輸出。
//
// 寫一個 AutoGain class：
//   prepare(sampleRate, maxBlockSize, numChannels) / reset() / setWindowMs(ms)
//   void process(const AudioBlock& dry, AudioBlock wet)：用 dry 當參考，就地修正 wet 的音量
//   float getGainDb() const：目前套用的增益（給 UI 看）
// 注意：
//   - dry 與 wet 必須對齊（Distortion 有 latency；這裡用 setOversamplingFactorLog2(0) 讓 latency = 0 即可）
//   - outMeanSquare 很小時不要除以 0；gain 限制在 ±40 dB
//   - 不可以在 process() 裡 allocate
//
// 執行：l07_ex5_starter → 目前會 FAIL，寫完後應該全部 PASS。

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
        // TODO:
        //   對每個 channel、每個 sample：
        //     inMs  = a * inMs  + (1 - a) * dry²
        //     outMs = a * outMs + (1 - a) * wet²
        //     gain  = sqrt(inMs / max(outMs, 1e-12))，clamp 到 [0.01, 100]
        //     wet  *= gain
        //   gain_ 存最後一個算出來的值（給 getGainDb）
        (void)dry;
        (void)wet;
    }

private:
    void updateCoefficient() noexcept {
        // one-pole 的係數：a = exp(-1 / (τ fs))，τ = window（秒）
        a_ = static_cast<float>(std::exp(-1.0 / (windowMs_ * 0.001 * sampleRate_)));
    }

    double sampleRate_ = 48000.0;
    float windowMs_ = 100.0f;
    float a_ = 0.999f;
    float gain_ = 1.0f;
    std::vector<float> inMs_, outMs_;
};

// Distortion + AutoGain 串起來
struct CompensatedDistortion {
    dawlab::dsp::Distortion distortion;
    AutoGain autoGain;
    dawlab::AudioBuffer dryScratch;

    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        distortion.setOversamplingFactorLog2(0); // latency = 0，dry / wet 才對得齊
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

    // (1) 穩態正弦：hard clip drive 30 dB 之後 RMS 應該回到輸入的 RMS（±0.2 dB）
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
        // 先自己估算：0.1 × 10^(30/20) = 3.16 的正弦被 clip 在 ±1 之後 RMS 是多少？補償應該是幾 dB？
        // （答案與推導在 solutions/README.md 練習 5）
        DAWLAB_CHECK_NEAR(cd.autoGain.getGainDb(), -22.38, 0.5);
    }

    // (2) pluck sequence（動態變化大）：整段 RMS 在 ±1 dB 內，沒有 NaN
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
        DAWLAB_CHECK(rawDb - inDb > 6.0);     // 不補償的話會大很多
        DAWLAB_CHECK_NEAR(compDb, inDb, 1.0); // 補償後接近
        DAWLAB_CHECK(!dawlab::hasNanOrInf(comp));
        dawlab::writeWav(dawlab::outputPath("l07_ex5_autogain.wav"), comp, kSampleRate);
    }

    // (3) 靜音輸入不可以爆掉（除以 0）
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
