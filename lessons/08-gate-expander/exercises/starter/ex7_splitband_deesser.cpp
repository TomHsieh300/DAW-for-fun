// 練習 7 起始碼：用 Linkwitz-Riley 4 階分頻做 2-band split-band de-esser
//
// 要做的事：
//   1. 完成 LinkwitzRileyCrossover::setCrossoverHz() 與 processSample()：
//      LR4 = 兩個相同的 2 階 Butterworth（Q = 1/√2）串聯，LP 與 HP 各一組
//   2. 完成 SplitBandDeEsser::process()：分頻 → 只用 Compressor 壓高頻段 → 相加
// self-check 已經寫好（分頻相加平坦、fc 處 -6 dB、母音不動、齒音被壓）；填完 TODO 後 l08_ex7_starter 應印出 PASS。
// 提示：解答在 solutions/ex7_splitband_deesser.cpp。

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
        // TODO：用 BiquadCoefficients::makeLowpass / makeHighpass(sampleRate_, hz, BiquadCoefficients::kButterworthQ)
        //       設定 lp1_、lp2_、hp1_、hp2_ 的係數
    }
    void processSample(int ch, float x, float& low, float& high) noexcept {
        // TODO：low = lp2(lp1(x))、high = hp2(hp1(x))
        (void)ch;
        low = x;
        high = 0.0f;
    }

private:
    double sampleRate_ = 48000.0;
    float crossoverHz_ = 5000.0f;
    Biquad lp1_, lp2_, hp1_, hp2_;
};

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
        // TODO：
        //   1. 每個 channel 逐 sample 呼叫 crossover_.processSample() 寫進 low_ / high_
        //   2. AudioBlock highBlock(high_.getArrayOfWritePointers(), nch, n); compressor_.process(highBlock);
        //   3. block = low_ + high_
        (void)block;
    }

private:
    LinkwitzRileyCrossover crossover_;
    Compressor compressor_;
    AudioBuffer low_, high_;
};

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
    std::printf("第 08 章 練習 7（起始碼）：LR4 分頻 + split-band de-esser\n");

    LinkwitzRileyCrossover xo;
    xo.prepare(kSampleRate, kBlockSize, 1);
    xo.setCrossoverHz(5000.0f);
    for (double f : {100.0, 500.0, 1000.0, 2000.0, 3500.0, 5000.0, 7000.0, 10000.0, 15000.0}) {
        xo.reset();
        const double sum = measureGainDb([&](float x) { float lo, hi; xo.processSample(0, x, lo, hi); return lo + hi; }, f, kSampleRate);
        xo.reset();
        const double lo = measureGainDb([&](float x) { float l, h; xo.processSample(0, x, l, h); return l; }, f, kSampleRate);
        xo.reset();
        const double hi = measureGainDb([&](float x) { float l, h; xo.processSample(0, x, l, h); return h; }, f, kSampleRate);
        std::printf("    %8.0f Hz  LP+HP %7.3f  LP %7.2f  HP %7.2f dB\n", f, sum, lo, hi);
        DAWLAB_CHECK_NEAR(sum, 0.0, 0.1);
        if (f == 5000.0) {
            DAWLAB_CHECK_NEAR(lo, -6.02, 0.2);
            DAWLAB_CHECK_NEAR(hi, -6.02, 0.2);
        }
    }

    AudioBuffer input = makeVowel(3.0, dbToGain(-14.0f));
    for (double t : {0.5, 1.5, 2.5})
        addSibilance(input, t, 0.120, dbToGain(-12.0f));

    SplitBandDeEsser de;
    de.prepare(kSampleRate, kBlockSize, 2);
    AudioBuffer output = input;
    {
        AudioBlock whole(output);
        for (int start = 0; start < output.getNumSamples(); start += kBlockSize)
            de.process(whole.getSubBlock(start, std::min(kBlockSize, output.getNumSamples() - start)));
    }
    writeWav(outputPath("l08_ex7_input.wav"), input, kSampleRate);
    writeWav(outputPath("l08_ex7_output.wav"), output, kSampleRate);

    const double vowelInBurst = levelDb(input, 0.52, 0.08, kVowelHz);
    const double vowelOutBurst = levelDb(output, 0.52, 0.08, kVowelHz);
    const double sibIn = levelDb(input, 0.52, 0.08, kSibilanceHz);
    const double sibOut = levelDb(output, 0.52, 0.08, kSibilanceHz);
    std::printf("    母音@齒音期間 輸入 %.2f / 輸出 %.2f dB；7 kHz 輸入 %.2f / 輸出 %.2f dB\n", vowelInBurst, vowelOutBurst,
                sibIn, sibOut);
    DAWLAB_CHECK_NEAR(levelDb(output, 1.0, 0.4, kVowelHz), levelDb(input, 1.0, 0.4, kVowelHz), 0.1);
    DAWLAB_CHECK_NEAR(vowelOutBurst, vowelInBurst, 0.3);
    DAWLAB_CHECK(sibIn - sibOut >= 8.0);
    return dawlab::test::summary("l08_ex7_starter");
}
