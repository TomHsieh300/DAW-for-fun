// 練習 4 解答：sidechain high-pass filter（HPF）。
//
// 問題：低頻（kick、bass）能量大，很容易把 compressor 踩下去，連帶把高頻的東西一起壓小聲（pumping）。
// 解法：detector 不看原始訊號，而是看「經過 HPF 的副本」；gain 還是套在原始訊號上。
// 本解答自帶一個 one-pole HPF（y = x - lowpass(x)），然後用 Compressor::process(block, sidechain)。
//
// 測試訊號：50 Hz @ -6 dBFS（大聲的低音）+ 3 kHz @ -24 dBFS（小聲的高音）。
//   沒有 HPF：detector 看到 ≈ -5 dB 的 level → GR ≈ 11 dB，3 kHz 被低音「拖」下去。
//   HPF 400 Hz：50 Hz 在 sidechain 裡衰減約 18 dB → GR 只剩 1–2 dB，3 kHz 幾乎沒動。
// 輸出：dawlab_output/l04_ex4_no_hpf.wav、l04_ex4_with_hpf.wav。

#include <dawlab/Common.h>
#include <dawlab/dsp/Compressor.h>

#include <cstdio>
#include <vector>

using namespace dawlab;
using dawlab::dsp::Compressor;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

// 一階 high-pass：先算 one-pole lowpass，再用 x - lp。
//   lp[n] = lp[n-1] + b·(x[n] - lp[n-1])，b = 1 - exp(-2π·fc/fs)
//   hp[n] = x[n] - lp[n]
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
        b_ = 1.0f - static_cast<float>(std::exp(-kTwoPi * hz / sampleRate_));
    }
    float processSample(int ch, float x) noexcept {
        float& lp = lp_[static_cast<size_t>(ch)];
        lp += b_ * (x - lp);
        return x - lp;
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

// 逐 block：先把 block 複製到 sidechain buffer、過 HPF，再用它當 sidechain 壓主訊號
AudioBuffer compressWithSidechainHpf(const AudioBuffer& input, Compressor& comp, OnePoleHighpass* hpf) {
    AudioBuffer out = input;
    AudioBuffer scBuffer(input.getNumChannels(), kBlockSize); // 在 prepare 階段配置一次，process 內不 allocate
    AudioBlock whole(out);
    AudioBlock scWhole(scBuffer);
    for (int start = 0; start < out.getNumSamples(); start += kBlockSize) {
        const int len = std::min(kBlockSize, out.getNumSamples() - start);
        AudioBlock block = whole.getSubBlock(start, len);
        AudioBlock sc = scWhole.getSubBlock(0, len);
        sc.copyFrom(block);
        if (hpf)
            hpf->process(sc);
        comp.process(block, sc);
    }
    return out;
}

double steadyAmplitude(const AudioBuffer& b, double freq) {
    const int n = b.getNumSamples();
    return goertzelAmplitude(b.getReadPointer(0) + n / 2, n - n / 2, freq, kSampleRate);
}

} // namespace

int main() {
    std::printf("練習 4：sidechain HPF\n");

    // 測試訊號：50 Hz @ -6 dBFS + 3 kHz @ -24 dBFS，1 秒，mono
    AudioBuffer input = makeSine(50.0, kSampleRate, 1.0, dbToGain(-6.0f), 1);
    const AudioBuffer high = makeSine(3000.0, kSampleRate, 1.0, dbToGain(-24.0f), 1);
    input.addFrom(0, 0, high, 0, 0, input.getNumSamples());
    const double in3k = steadyAmplitude(input, 3000.0);
    std::printf("  輸入：peak %.2f dBFS，3 kHz 成分 %.2f dBFS\n", gainToDb(peakAbs(input)), gainToDb(in3k));

    // 先確認 HPF 本身的頻率響應（one-pole：fc 處 -3 dB，每 octave -6 dB）
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

    // 沒有 HPF
    Compressor compA = makeCompressor(1);
    AudioBuffer outA = compressWithSidechainHpf(input, compA, nullptr);
    const float grA = compA.getGainReductionDb();
    const double red3kA = gainToDb(in3k) - gainToDb(steadyAmplitude(outA, 3000.0));
    std::printf("  沒有 HPF：GR %.2f dB，3 kHz 成分被壓掉 %.2f dB\n", grA, red3kA);

    // 有 HPF
    Compressor compB = makeCompressor(1);
    OnePoleHighpass hpf;
    hpf.prepare(kSampleRate, 1);
    hpf.setCutoff(400.0f);
    AudioBuffer outB = compressWithSidechainHpf(input, compB, &hpf);
    const float grB = compB.getGainReductionDb();
    const double red3kB = gainToDb(in3k) - gainToDb(steadyAmplitude(outB, 3000.0));
    std::printf("  HPF 400 Hz：GR %.2f dB，3 kHz 成分被壓掉 %.2f dB\n", grB, red3kB);

    // self-check：靜態估計 —— 沒有 HPF 時 level ≈ 20log10(0.501 + 0.063) ≈ -5 dB → GR ≈ 11.2 dB
    const float expectedGrA = Compressor::computeGainReductionDb(gainToDb(dbToGain(-6.0f) + dbToGain(-24.0f)),
                                                                 -20.0f, 4.0f, 0.0f);
    std::printf("  靜態估計（無 HPF）GR = %.2f dB\n", expectedGrA);
    DAWLAB_CHECK_NEAR(grA, expectedGrA, 0.5);
    DAWLAB_CHECK(grA > 9.0f);
    DAWLAB_CHECK(grB < 2.5f);
    DAWLAB_CHECK(red3kA > 7.0);
    DAWLAB_CHECK(red3kB < 2.5);
    DAWLAB_CHECK(grA - grB > 7.0f);
    DAWLAB_CHECK(!hasNanOrInf(outA) && !hasNanOrInf(outB));

    writeWav(outputPath("l04_ex4_no_hpf.wav"), outA, kSampleRate);
    writeWav(outputPath("l04_ex4_with_hpf.wav"), outB, kSampleRate);
    std::printf("  已輸出 dawlab_output/l04_ex4_no_hpf.wav、l04_ex4_with_hpf.wav\n");
    return dawlab::test::summary("l04_ex4");
}
