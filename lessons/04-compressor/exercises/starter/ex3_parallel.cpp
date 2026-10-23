// 練習 3（起始碼）：parallel（New York）compression。
//
// 目標：
//   1. 把 dry 訊號複製一份，用「重壓」設定（低 threshold、高 ratio、快 attack）逐 block 壓縮成 wet；
//   2. 補 makeup gain，讓 wet 的 peak 回到 dry 的 peak；
//   3. 實作 parallelMix()：y = (1 - mix)·dry + mix·wet；
//   4. 印出不同 mix 的 crest factor，並通過 self-check。
// 完成 TODO 之後，self-check 全部通過就算完成。起始碼執行時會印出 FAIL，這是預期的。

#include <dawlab/Common.h>
#include <dawlab/dsp/Compressor.h>

#include <cstdio>

using namespace dawlab;
using dawlab::dsp::Compressor;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

// crest factor = peak(dB) - RMS(dB)，取所有 channel 中最大的 RMS
float crestFactorDb(const AudioBuffer& b) {
    float r = 0.0f;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        r = std::max(r, rms(b, ch));
    return gainToDb(peakAbs(b)) - gainToDb(r);
}

// 逐 block 壓縮整段 buffer（模擬 audio callback），回傳壓縮後的新 buffer，並記下整段最大的 gain reduction
AudioBuffer compressBlocks(const AudioBuffer& input, Compressor& comp, float& maxGrDb) {
    AudioBuffer out = input;
    (void)comp;
    maxGrDb = 0.0f;
    // TODO: 用 AudioBlock whole(out) 與 getSubBlock() 每 kBlockSize 個 sample 呼叫一次 comp.process()，
    //       每個 block 之後用 comp.getGainReductionDb() 更新 maxGrDb
    return out;
}

// y = (1 - mix)·dry + mix·wet，逐 sample 計算（dry 與 wet 必須同長度、同 channel 數）
AudioBuffer parallelMix(const AudioBuffer& dry, const AudioBuffer& wet, float mix) {
    (void)wet;
    (void)mix;
    AudioBuffer out = dry;
    // TODO: 把 mix clamp 到 0..1，然後對每個 channel、每個 sample 做線性混合
    return out;
}

float maxAbsDifference(const AudioBuffer& a, const AudioBuffer& b) {
    float m = 0.0f;
    for (int ch = 0; ch < a.getNumChannels(); ++ch)
        for (int i = 0; i < a.getNumSamples(); ++i)
            m = std::max(m, std::abs(a.getSample(ch, i) - b.getSample(ch, i)));
    return m;
}

} // namespace

int main() {
    std::printf("練習 3（起始碼）：parallel compression\n");
    const AudioBuffer dry = makeDrumLoop(kSampleRate, 2, 120.0);

    // 重壓：T -30 dB, 10:1, knee 3 dB, attack 0.02 ms（極快，≈ 1 sample，鼓的尖峰才壓得到）, release 60 ms
    Compressor comp;
    comp.prepare(kSampleRate, kBlockSize, dry.getNumChannels());
    comp.setThresholdDb(-30.0f);
    comp.setRatio(10.0f);
    comp.setKneeDb(3.0f);
    comp.setAttackMs(0.02f);
    comp.setReleaseMs(60.0f);
    float maxGr = 0.0f;
    AudioBuffer wet = compressBlocks(dry, comp, maxGr);

    // TODO: 算出 makeupDb = dry 的 peak(dB) - wet 的 peak(dB)，並用 wet.applyGain() 補上
    const float makeupDb = 0.0f;
    std::printf("  wet 的 makeup = %+.2f dB（最大 GR %.2f dB）\n", makeupDb, maxGr);

    const float crestDry = crestFactorDb(dry);
    const float crestWet = crestFactorDb(wet);
    std::printf("  crest factor：dry %.2f dB, wet %.2f dB\n", crestDry, crestWet);
    DAWLAB_CHECK(crestWet < crestDry - 3.0f); // 重壓後 crest 明顯下降

    const float mixes[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
    for (float mix : mixes) {
        AudioBuffer y = parallelMix(dry, wet, mix);
        std::printf("  mix %.2f：peak %6.2f dBFS, crest %5.2f dB\n", mix, gainToDb(peakAbs(y)), crestFactorDb(y));
        DAWLAB_CHECK(!hasNanOrInf(y));
    }

    // self-check
    DAWLAB_CHECK(maxAbsDifference(parallelMix(dry, wet, 0.0f), dry) < 1e-6f);
    DAWLAB_CHECK(maxAbsDifference(parallelMix(dry, wet, 1.0f), wet) < 1e-6f);
    const AudioBuffer half = parallelMix(dry, wet, 0.5f);
    const float crestHalf = crestFactorDb(half);
    DAWLAB_CHECK(crestHalf < crestDry && crestHalf > crestWet);
    DAWLAB_CHECK_NEAR(half.getSample(0, 1000), 0.5f * (dry.getSample(0, 1000) + wet.getSample(0, 1000)), 1e-6);

    writeWav(outputPath("l04_ex3_parallel_50.wav"), half, kSampleRate);
    return dawlab::test::summary("l04_ex3_starter");
}
