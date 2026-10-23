// 練習 3 解答：parallel（New York）compression。
//
// 作法：把 dry 訊號複製一份，重壓（低 threshold、高 ratio、極快 attack），補 makeup 讓 wet 的 peak 回到 dry 的 peak，
// 再用 mix 把兩者混在一起：y = (1 - mix)·dry + mix·wet。
// 注意 attack 要「極快」（這裡 0.02 ms ≈ 1 個 sample，類似 FET 硬體 compressor 最快約 20 µs 的 attack）：鼓組的 peak 是幾個 sample 寬的雜訊尖峰，
// attack 1 ms 會讓尖峰溜過去、只壓到身體，crest factor 反而上升（見範例 3 的 attack 掃描）。
// self-check：mix = 0 → 完全等於 dry；mix = 1 → 完全等於 wet；mix = 0.5 的 crest factor 介於兩者之間。
// 輸出：dawlab_output/l04_ex3_parallel_50.wav（mix 0.5）、l04_ex3_parallel_wet.wav（mix 1）。

#include <dawlab/Common.h>
#include <dawlab/dsp/Compressor.h>

#include <cstdio>

using namespace dawlab;
using dawlab::dsp::Compressor;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

float crestFactorDb(const AudioBuffer& b) {
    float r = 0.0f;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        r = std::max(r, rms(b, ch));
    return gainToDb(peakAbs(b)) - gainToDb(r);
}

// 逐 block 壓縮整段 buffer（模擬 audio callback），順便記下整段的最大 gain reduction
AudioBuffer compressBlocks(const AudioBuffer& input, Compressor& comp, float& maxGrDb) {
    AudioBuffer out = input;
    AudioBlock whole(out);
    maxGrDb = 0.0f;
    for (int start = 0; start < out.getNumSamples(); start += kBlockSize) {
        comp.process(whole.getSubBlock(start, std::min(kBlockSize, out.getNumSamples() - start)));
        maxGrDb = std::max(maxGrDb, comp.getGainReductionDb());
    }
    return out;
}

// y = (1 - mix)·dry + mix·wet，逐 sample 計算（dry 與 wet 必須同長度、同 channel 數）
AudioBuffer parallelMix(const AudioBuffer& dry, const AudioBuffer& wet, float mix) {
    mix = std::clamp(mix, 0.0f, 1.0f);
    AudioBuffer out(dry.getNumChannels(), dry.getNumSamples());
    for (int ch = 0; ch < dry.getNumChannels(); ++ch) {
        const float* d = dry.getReadPointer(ch);
        const float* w = wet.getReadPointer(ch);
        float* o = out.getWritePointer(ch);
        for (int i = 0; i < dry.getNumSamples(); ++i)
            o[i] = (1.0f - mix) * d[i] + mix * w[i];
    }
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
    std::printf("練習 3：parallel compression\n");
    const AudioBuffer dry = makeDrumLoop(kSampleRate, 2, 120.0);

    // 重壓：T -30 dB, 10:1, attack 0.02 ms（≈ 1 sample）, release 60 ms
    Compressor comp;
    comp.prepare(kSampleRate, kBlockSize, dry.getNumChannels());
    comp.setThresholdDb(-30.0f);
    comp.setRatio(10.0f);
    comp.setKneeDb(3.0f);
    comp.setAttackMs(0.02f);
    comp.setReleaseMs(60.0f);
    float maxGr = 0.0f;
    AudioBuffer wet = compressBlocks(dry, comp, maxGr);

    // makeup：讓 wet 的 peak 等於 dry 的 peak
    const float makeupDb = gainToDb(peakAbs(dry)) - gainToDb(peakAbs(wet));
    wet.applyGain(dbToGain(makeupDb));
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
    // 線性混合：mix 0.5 的每個 sample 都應該是 dry、wet 的平均
    DAWLAB_CHECK_NEAR(half.getSample(0, 1000), 0.5f * (dry.getSample(0, 1000) + wet.getSample(0, 1000)), 1e-6);

    writeWav(outputPath("l04_ex3_parallel_50.wav"), half, kSampleRate);
    writeWav(outputPath("l04_ex3_parallel_wet.wav"), wet, kSampleRate);
    std::printf("  已輸出 dawlab_output/l04_ex3_parallel_50.wav、l04_ex3_parallel_wet.wav\n");
    return dawlab::test::summary("l04_ex3");
}
