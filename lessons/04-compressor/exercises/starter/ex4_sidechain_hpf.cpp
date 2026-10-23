// 練習 4（起始碼）：sidechain high-pass filter（HPF）。
//
// 目標：
//   1. 實作一個 one-pole HPF（lp[n] = lp[n-1] + b·(x[n] - lp[n-1])，hp = x - lp，b = 1 - exp(-2π·fc/fs)）；
//   2. 逐 block：把 block 複製到 sidechain buffer → 過 HPF → 用 Compressor::process(block, sidechain) 壓主訊號；
//   3. 用「50 Hz 大聲 + 3 kHz 小聲」的測試訊號驗證：有 HPF 時 3 kHz 幾乎不被低音拖下去。
// 完成 TODO 之後，self-check 全部通過就算完成。起始碼執行時會印出 FAIL，這是預期的。

#include <dawlab/Common.h>
#include <dawlab/dsp/Compressor.h>

#include <cstdio>
#include <vector>

using namespace dawlab;
using dawlab::dsp::Compressor;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

class OnePoleHighpass {
public:
    void prepare(double sampleRate, int numChannels) {
        sampleRate_ = sampleRate;
        lp_.assign(static_cast<size_t>(numChannels), 0.0f);
        setCutoff(cutoffHz_);
    }
    void reset() noexcept { std::fill(lp_.begin(), lp_.end(), 0.0f); }
    void setCutoff(float hz) noexcept {
        cutoffHz_ = hz;
        // TODO: b_ = 1 - exp(-2π·fc/fs)（用 dawlab::kTwoPi，注意 double→float 轉換）
        b_ = 0.0f;
    }
    float processSample(int ch, float x) noexcept {
        float& lp = lp_[static_cast<size_t>(ch)];
        (void)lp;
        // TODO: 更新 lp，回傳 x - lp
        return x;
    }
    void process(AudioBlock block) noexcept {
        for (int ch = 0; ch < block.getNumChannels(); ++ch) {
            float* d = block.getChannelPointer(ch);
            for (int i = 0; i < block.getNumSamples(); ++i)
                d[i] = processSample(ch, d[i]);
        }
    }

private:
    double sampleRate_ = 48000.0;
    float cutoffHz_ = 400.0f;
    float b_ = 0.0f;
    std::vector<float> lp_;
};

Compressor makeCompressor(int numChannels) {
    Compressor c;
    c.prepare(kSampleRate, kBlockSize, numChannels);
    c.setThresholdDb(-20.0f);
    c.setRatio(4.0f);
    c.setKneeDb(0.0f);
    c.setAttackMs(1.0f);
    c.setReleaseMs(100.0f);
    return c;
}

// hpf == nullptr 代表「不加 HPF」（sidechain = 原始訊號）
AudioBuffer compressWithSidechainHpf(const AudioBuffer& input, Compressor& comp, OnePoleHighpass* hpf) {
    AudioBuffer out = input;
    AudioBuffer scBuffer(input.getNumChannels(), kBlockSize); // 在 prepare 階段配置一次，process 內不 allocate
    (void)comp;
    (void)hpf;
    (void)scBuffer;
    // TODO: 逐 block：sc.copyFrom(block) → 若 hpf 非空則 hpf->process(sc) → comp.process(block, sc)
    return out;
}

double steadyAmplitude(const AudioBuffer& b, double freq) {
    const int n = b.getNumSamples();
    return goertzelAmplitude(b.getReadPointer(0) + n / 2, n - n / 2, freq, kSampleRate);
}

} // namespace

int main() {
    std::printf("練習 4（起始碼）：sidechain HPF\n");

    AudioBuffer input = makeSine(50.0, kSampleRate, 1.0, dbToGain(-6.0f), 1);
    const AudioBuffer high = makeSine(3000.0, kSampleRate, 1.0, dbToGain(-24.0f), 1);
    input.addFrom(0, 0, high, 0, 0, input.getNumSamples());
    const double in3k = steadyAmplitude(input, 3000.0);
    std::printf("  輸入：peak %.2f dBFS，3 kHz 成分 %.2f dBFS\n", gainToDb(peakAbs(input)), gainToDb(in3k));

    OnePoleHighpass probe;
    probe.prepare(kSampleRate, 1);
    probe.setCutoff(400.0f);
    const double g50 = measureGainDb([&](float x) { return probe.processSample(0, x); }, 50.0, kSampleRate);
    const double g400 = measureGainDb([&](float x) { return probe.processSample(0, x); }, 400.0, kSampleRate);
    const double g3k = measureGainDb([&](float x) { return probe.processSample(0, x); }, 3000.0, kSampleRate);
    std::printf("  HPF 400 Hz 響應：50 Hz %.2f dB, 400 Hz %.2f dB, 3 kHz %.2f dB\n", g50, g400, g3k);
    DAWLAB_CHECK_NEAR(g400, -3.0, 0.5);
    DAWLAB_CHECK(g50 < -15.0);
    DAWLAB_CHECK(g3k > -0.5);

    Compressor compA = makeCompressor(1);
    AudioBuffer outA = compressWithSidechainHpf(input, compA, nullptr);
    const float grA = compA.getGainReductionDb();
    const double red3kA = gainToDb(in3k) - gainToDb(steadyAmplitude(outA, 3000.0));
    std::printf("  沒有 HPF：GR %.2f dB，3 kHz 成分被壓掉 %.2f dB\n", grA, red3kA);

    Compressor compB = makeCompressor(1);
    OnePoleHighpass hpf;
    hpf.prepare(kSampleRate, 1);
    hpf.setCutoff(400.0f);
    AudioBuffer outB = compressWithSidechainHpf(input, compB, &hpf);
    const float grB = compB.getGainReductionDb();
    const double red3kB = gainToDb(in3k) - gainToDb(steadyAmplitude(outB, 3000.0));
    std::printf("  HPF 400 Hz：GR %.2f dB，3 kHz 成分被壓掉 %.2f dB\n", grB, red3kB);

    const float expectedGrA = Compressor::computeGainReductionDb(gainToDb(dbToGain(-6.0f) + dbToGain(-24.0f)),
                                                                 -20.0f, 4.0f, 0.0f);
    DAWLAB_CHECK_NEAR(grA, expectedGrA, 0.5);
    DAWLAB_CHECK(grA > 9.0f);
    DAWLAB_CHECK(grB < 2.5f);
    DAWLAB_CHECK(red3kA > 7.0);
    DAWLAB_CHECK(red3kB < 2.5);
    DAWLAB_CHECK(grA - grB > 7.0f);
    DAWLAB_CHECK(!hasNanOrInf(outA) && !hasNanOrInf(outB));
    return dawlab::test::summary("l04_ex4_starter");
}
