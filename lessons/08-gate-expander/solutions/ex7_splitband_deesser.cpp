// 練習 7 解答：用 Linkwitz-Riley 4 階分頻做 2-band split-band de-esser，並驗證分頻相加為平坦
//
// Linkwitz-Riley 4 階（LR4）= 兩個相同的 2 階 Butterworth（Q = 1/√2）串聯。
// 在分頻點 fc：每個 Butterworth 是 -3 dB，串聯後 -6 dB；LP 與 HP 在 fc 同相位，
// 所以 LP + HP 在 fc = 2 × (-6 dB) = 0 dB，而且整個頻帶 |LP + HP| = 1（相加為 allpass：振幅平坦、相位不平）。
//
// de-esser：HP 段丟給第 04 章的 Compressor（threshold -30、ratio 8:1、attack 1 ms、release 40 ms），
// 偵測用 HP 段本身（齒音能量都在這裡），LP 段原封不動，最後相加。
//
// 自我檢查：
//   - 100 Hz ~ 15 kHz 之間 |LP + HP| 在 0 dB ± 0.1 dB
//   - fc 處 LP、HP 各 -6.02 dB ± 0.2 dB
//   - 齒音期間 220 Hz 母音的位準變化 < 0.3 dB；7 kHz 齒音被壓 ≥ 8 dB
// 執行：l08_ex7_solution → 輸出 dawlab_output/l08_ex7_input.wav、l08_ex7_output.wav。

#include <dawlab/Common.h>
#include <dawlab/dsp/Biquad.h>
#include <dawlab/dsp/Compressor.h>

#include <cstdio>
#include <vector>

using namespace dawlab;
using dawlab::dsp::Biquad;
using dawlab::dsp::BiquadCoefficients;
using dawlab::dsp::Compressor;

namespace {

constexpr int kBlockSize = 512;
constexpr double kSampleRate = 48000.0;
constexpr double kVowelHz = 220.0;
constexpr double kSibilanceHz = 7000.0;

// ---- LR4 分頻器 --------------------------------------------------------------
class LinkwitzRileyCrossover {
public:
    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        sampleRate_ = sampleRate;
        for (Biquad* f : {&lp1_, &lp2_, &hp1_, &hp2_})
            f->prepare(sampleRate, maxBlockSize, numChannels);
        setCrossoverHz(crossoverHz_);
        reset();
    }
    void reset() noexcept {
        for (Biquad* f : {&lp1_, &lp2_, &hp1_, &hp2_})
            f->reset();
    }
    void setCrossoverHz(float hz) noexcept {
        crossoverHz_ = hz;
        const auto lp = BiquadCoefficients::makeLowpass(sampleRate_, hz, BiquadCoefficients::kButterworthQ);
        const auto hp = BiquadCoefficients::makeHighpass(sampleRate_, hz, BiquadCoefficients::kButterworthQ);
        lp1_.setCoefficients(lp);
        lp2_.setCoefficients(lp);
        hp1_.setCoefficients(hp);
        hp2_.setCoefficients(hp);
    }
    // 一個 sample 進、兩個 band 出
    void processSample(int ch, float x, float& low, float& high) noexcept {
        low = lp2_.processSample(ch, lp1_.processSample(ch, x));
        high = hp2_.processSample(ch, hp1_.processSample(ch, x));
    }

private:
    double sampleRate_ = 48000.0;
    float crossoverHz_ = 5000.0f;
    Biquad lp1_, lp2_, hp1_, hp2_;
};

// ---- 2-band de-esser ---------------------------------------------------------
class SplitBandDeEsser {
public:
    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        crossover_.prepare(sampleRate, maxBlockSize, numChannels);
        crossover_.setCrossoverHz(5000.0f);
        compressor_.prepare(sampleRate, maxBlockSize, numChannels);
        compressor_.setThresholdDb(-30.0f);
        compressor_.setRatio(8.0f);
        compressor_.setKneeDb(3.0f);
        compressor_.setAttackMs(1.0f);
        compressor_.setReleaseMs(40.0f);
        compressor_.setStereoLink(true);
        low_.setSize(numChannels, maxBlockSize);
        high_.setSize(numChannels, maxBlockSize);
        reset();
    }
    void reset() noexcept {
        crossover_.reset();
        compressor_.reset();
    }
    float getGainReductionDb() const noexcept { return compressor_.getGainReductionDb(); }

    void process(AudioBlock block) noexcept {
        const int n = std::min(block.getNumSamples(), low_.getNumSamples());
        const int nch = std::min(block.getNumChannels(), low_.getNumChannels());
        // 1. 分頻到暫存 buffer（prepare 時配好，process 內不 allocate）
        for (int ch = 0; ch < nch; ++ch) {
            const float* x = block.getChannelPointer(ch);
            float* lo = low_.getWritePointer(ch);
            float* hi = high_.getWritePointer(ch);
            for (int i = 0; i < n; ++i)
                crossover_.processSample(ch, x[i], lo[i], hi[i]);
        }
        // 2. 只壓高頻段（偵測也看高頻段自己）
        AudioBlock highBlock(high_.getArrayOfWritePointers(), nch, n);
        compressor_.process(highBlock);
        // 3. 相加
        for (int ch = 0; ch < nch; ++ch) {
            float* y = block.getChannelPointer(ch);
            const float* lo = low_.getReadPointer(ch);
            const float* hi = high_.getReadPointer(ch);
            for (int i = 0; i < n; ++i)
                y[i] = lo[i] + hi[i];
        }
    }

private:
    LinkwitzRileyCrossover crossover_;
    Compressor compressor_;
    AudioBuffer low_, high_;
};

// ---- 測試素材（與範例 3 相同）----------------------------------------------
AudioBuffer makeVowel(double seconds, float amplitude) {
    AudioBuffer b(2, secondsToSamples(seconds, kSampleRate));
    for (int i = 0; i < b.getNumSamples(); ++i) {
        double v = 0.0;
        for (int k = 1; k <= 12; ++k)
            v += std::sin(kTwoPi * kVowelHz * k * i / kSampleRate) / k;
        const auto s = static_cast<float>(amplitude * v / 1.6);
        b.setSample(0, i, s);
        b.setSample(1, i, s);
    }
    return b;
}

void addSibilance(AudioBuffer& target, double startSec, double lengthSec, float peakAmplitude) {
    const int start = secondsToSamples(startSec, kSampleRate);
    const int len = secondsToSamples(lengthSec, kSampleRate);
    Biquad bp;
    bp.prepare(kSampleRate, kBlockSize, 1);
    bp.setCoefficients(BiquadCoefficients::makeBandpass(kSampleRate, kSibilanceHz, 1.5));
    NoiseGenerator noise(777);
    std::vector<float> burst(static_cast<size_t>(len));
    float peak = 0.0f;
    for (int i = 0; i < len; ++i) {
        const double t = i / kSampleRate;
        const double env = std::min(1.0, t / 0.010) * std::min(1.0, (lengthSec - t) / 0.030);
        burst[static_cast<size_t>(i)] = static_cast<float>(env * bp.processSample(0, noise.next()));
        peak = std::max(peak, std::abs(burst[static_cast<size_t>(i)]));
    }
    for (int i = 0; i < len && start + i < target.getNumSamples(); ++i)
        for (int ch = 0; ch < target.getNumChannels(); ++ch)
            target.addSample(ch, start + i, burst[static_cast<size_t>(i)] * peakAmplitude / peak);
}

double levelDb(const AudioBuffer& b, double startSec, double lengthSec, double freq) {
    const int start = secondsToSamples(startSec, kSampleRate);
    const int n = secondsToSamples(lengthSec, kSampleRate);
    return gainToDb(goertzelAmplitude(b.getReadPointer(0) + start, n, freq, kSampleRate));
}

} // namespace

int main() {
    std::printf("第 08 章 練習 7：LR4 分頻 + split-band de-esser\n");

    // ---- 1. 分頻相加為平坦 ----
    LinkwitzRileyCrossover xo;
    xo.prepare(kSampleRate, kBlockSize, 1);
    xo.setCrossoverHz(5000.0f);
    std::printf("\n[1] LR4 @ 5 kHz：|LP + HP|、|LP|、|HP|（dB）\n    頻率(Hz)   LP+HP      LP       HP\n");
    for (double f : {100.0, 500.0, 1000.0, 2000.0, 3500.0, 5000.0, 7000.0, 10000.0, 15000.0}) {
        xo.reset();
        const double sum = measureGainDb([&](float x) { float lo, hi; xo.processSample(0, x, lo, hi); return lo + hi; }, f, kSampleRate);
        xo.reset();
        const double lo = measureGainDb([&](float x) { float l, h; xo.processSample(0, x, l, h); return l; }, f, kSampleRate);
        xo.reset();
        const double hi = measureGainDb([&](float x) { float l, h; xo.processSample(0, x, l, h); return h; }, f, kSampleRate);
        std::printf("    %8.0f  %7.3f  %7.2f  %7.2f\n", f, sum, lo, hi);
        DAWLAB_CHECK_NEAR(sum, 0.0, 0.1);
        if (f == 5000.0) {
            DAWLAB_CHECK_NEAR(lo, -6.02, 0.2);
            DAWLAB_CHECK_NEAR(hi, -6.02, 0.2);
        }
    }

    // ---- 2. de-esser ----
    AudioBuffer input = makeVowel(3.0, dbToGain(-14.0f));
    for (double t : {0.5, 1.5, 2.5})
        addSibilance(input, t, 0.120, dbToGain(-12.0f));

    SplitBandDeEsser de;
    de.prepare(kSampleRate, kBlockSize, 2);
    AudioBuffer output = input;
    float maxGr = 0.0f;
    {
        AudioBlock whole(output);
        for (int start = 0; start < output.getNumSamples(); start += kBlockSize) {
            de.process(whole.getSubBlock(start, std::min(kBlockSize, output.getNumSamples() - start)));
            maxGr = std::max(maxGr, de.getGainReductionDb());
        }
    }
    writeWav(outputPath("l08_ex7_input.wav"), input, kSampleRate);
    writeWav(outputPath("l08_ex7_output.wav"), output, kSampleRate);

    const double vowelInQuiet = levelDb(input, 1.0, 0.4, kVowelHz);
    const double vowelOutQuiet = levelDb(output, 1.0, 0.4, kVowelHz);
    const double vowelInBurst = levelDb(input, 0.52, 0.08, kVowelHz);
    const double vowelOutBurst = levelDb(output, 0.52, 0.08, kVowelHz);
    const double sibIn = levelDb(input, 0.52, 0.08, kSibilanceHz);
    const double sibOut = levelDb(output, 0.52, 0.08, kSibilanceHz);
    std::printf("\n[2] de-esser：最大 GR %.2f dB\n", maxGr);
    std::printf("    220 Hz 母音：純母音段 輸入 %.2f / 輸出 %.2f；齒音期間 輸入 %.2f / 輸出 %.2f dB\n", vowelInQuiet,
                vowelOutQuiet, vowelInBurst, vowelOutBurst);
    std::printf("    7 kHz 齒音：輸入 %.2f / 輸出 %.2f dB（壓了 %.2f dB）\n", sibIn, sibOut, sibIn - sibOut);
    DAWLAB_CHECK_NEAR(vowelOutQuiet, vowelInQuiet, 0.1);
    DAWLAB_CHECK_NEAR(vowelOutBurst, vowelInBurst, 0.3);
    DAWLAB_CHECK(sibIn - sibOut >= 8.0);
    DAWLAB_CHECK(!hasNanOrInf(output));
    return dawlab::test::summary("l08_ex7_splitband_deesser");
}
