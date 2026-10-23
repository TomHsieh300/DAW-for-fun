// 練習 7（起始碼）：RMS detector 視窗長度對 pumping 的影響。
//
// 目標：
//   1. 完成 run()：用 Compressor（RMS 模式、指定視窗長度）逐 block 壓縮鼓組 loop，
//      記錄每個 block 的 getGainReductionDb()（跳過前 0.5 秒暖機），算出 GR 的擺動幅度 = max - min；
//   2. 觀察：視窗越長，擺動越小（pumping 越少）；1 ms 視窗的行為接近 peak detector；
//   3. 通過 self-check（擺動幅度隨視窗長度單調下降）。
// 完成 TODO 之後，self-check 全部通過就算完成。起始碼執行時會印出 FAIL，這是預期的。

#include <dawlab/Common.h>
#include <dawlab/dsp/Compressor.h>

#include <cstdio>
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
    // TODO: comp.setDetectorMode(mode)、comp.setRmsWindowMs(rmsWindowMs)
    (void)mode;
    (void)rmsWindowMs;

    Outcome o{0.0f, 1000.0f, 0.0f, 0.0f, dry};
    // TODO: 逐 block 呼叫 comp.process()；start >= 0.5 秒之後更新 o.grMax / o.grMin
    o.grSwing = o.grMax - o.grMin;
    float r = 0.0f;
    for (int ch = 0; ch < o.out.getNumChannels(); ++ch)
        r = std::max(r, rms(o.out, ch));
    o.crestDb = gainToDb(peakAbs(o.out)) - gainToDb(r);
    return o;
}

} // namespace

int main() {
    std::printf("練習 7（起始碼）：RMS 視窗長度 vs pumping\n");
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
    }
    for (size_t i = 1; i < swings.size(); ++i)
        DAWLAB_CHECK(swings[i] < swings[i - 1]);
    DAWLAB_CHECK(swings.front() > 3.0f * swings.back());
    DAWLAB_CHECK(std::abs(swings.front() - peak.grSwing) < 3.0f);
    return dawlab::test::summary("l04_ex7_starter");
}
