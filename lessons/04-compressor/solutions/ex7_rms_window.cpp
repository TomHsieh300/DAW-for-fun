// 練習 7 解答：RMS detector 視窗長度對 pumping 的影響。
//
// RMS 視窗越短，detector 越像 peak detector：每一下鼓都會讓 GR 大幅上下跳動（pumping / breathing）；
// 視窗越長，level 估計越平滑，GR 幾乎固定，壓縮變成「整體拉平」而不是「跟著每一拍呼吸」。
// 量化：把每個 block 的 getGainReductionDb() 記下來，算「GR 的擺動幅度」= max - min（跳過前 0.5 秒暖機），
// self-check 要求擺動幅度隨視窗變長而單調下降。
// 輸出：dawlab_output/l04_ex7_rms_1ms.wav、l04_ex7_rms_10ms.wav、l04_ex7_rms_100ms.wav。

#include <dawlab/Common.h>
#include <dawlab/dsp/Compressor.h>

#include <cstdio>
#include <string>
#include <vector>

using namespace dawlab;
using dawlab::dsp::Compressor;
using dawlab::dsp::DetectorMode;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

struct Outcome {
    float grMax, grMin, grSwing, crestDb;
    AudioBuffer out;
};

Outcome run(const AudioBuffer& dry, DetectorMode mode, float rmsWindowMs) {
    Compressor comp;
    comp.prepare(kSampleRate, kBlockSize, dry.getNumChannels());
    comp.setThresholdDb(-24.0f);
    comp.setRatio(4.0f);
    comp.setKneeDb(3.0f);
    comp.setAttackMs(1.0f);
    comp.setReleaseMs(50.0f);
    comp.setDetectorMode(mode);
    comp.setRmsWindowMs(rmsWindowMs);

    Outcome o{0.0f, 1000.0f, 0.0f, 0.0f, dry};
    AudioBlock whole(o.out);
    const int warmup = secondsToSamples(0.5, kSampleRate);
    for (int start = 0; start < o.out.getNumSamples(); start += kBlockSize) {
        comp.process(whole.getSubBlock(start, std::min(kBlockSize, o.out.getNumSamples() - start)));
        if (start >= warmup) {
            o.grMax = std::max(o.grMax, comp.getGainReductionDb());
            o.grMin = std::min(o.grMin, comp.getGainReductionDb());
        }
    }
    o.grSwing = o.grMax - o.grMin;
    float r = 0.0f;
    for (int ch = 0; ch < o.out.getNumChannels(); ++ch)
        r = std::max(r, rms(o.out, ch));
    o.crestDb = gainToDb(peakAbs(o.out)) - gainToDb(r);
    return o;
}

} // namespace

int main() {
    std::printf("練習 7：RMS 視窗長度 vs pumping（T -24 dB, 4:1, attack 1 ms, release 50 ms）\n");
    const AudioBuffer dry = makeDrumLoop(kSampleRate, 2, 120.0);

    const Outcome peak = run(dry, DetectorMode::Peak, 10.0f);
    std::printf("  Peak detector（參考）：GR max %.2f / min %.2f，擺動 %.2f dB，輸出 crest %.2f dB\n", peak.grMax,
                peak.grMin, peak.grSwing, peak.crestDb);

    const float windows[] = {1.0f, 5.0f, 10.0f, 30.0f, 100.0f, 300.0f, 1000.0f};
    std::vector<float> swings;
    for (float w : windows) {
        const Outcome o = run(dry, DetectorMode::Rms, w);
        swings.push_back(o.grSwing);
        std::printf("  RMS 視窗 %5.0f ms：GR max %.2f / min %.2f，擺動 %.2f dB，輸出 crest %.2f dB\n", w, o.grMax,
                    o.grMin, o.grSwing, o.crestDb);
        DAWLAB_CHECK(!hasNanOrInf(o.out));
        if (w == 1.0f || w == 10.0f || w == 100.0f) {
            const std::string name = "l04_ex7_rms_" + std::to_string(static_cast<int>(w)) + "ms.wav";
            writeWav(outputPath(name), o.out, kSampleRate);
        }
    }
    // self-check：擺動幅度隨視窗變長單調下降；最短視窗的擺動至少是最長視窗的 3 倍
    for (size_t i = 1; i < swings.size(); ++i)
        DAWLAB_CHECK(swings[i] < swings[i - 1]);
    DAWLAB_CHECK(swings.front() > 3.0f * swings.back());
    // 1 ms 的 RMS 視窗行為接近 peak detector（擺動幅度差距在 3 dB 以內）
    DAWLAB_CHECK(std::abs(swings.front() - peak.grSwing) < 3.0f);
    std::printf("  已輸出 dawlab_output/l04_ex7_rms_1ms.wav、l04_ex7_rms_10ms.wav、l04_ex7_rms_100ms.wav\n");
    return dawlab::test::summary("l04_ex7");
}
