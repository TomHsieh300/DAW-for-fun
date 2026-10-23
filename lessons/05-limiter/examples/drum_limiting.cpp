// 範例 2：把 drum loop 推進 limiter——「更大聲但不爆」
//
// 示範什麼：
//   鼓的 peak 很高、RMS 卻不高（crest factor 大）。把 input gain 推 +6、+12 dB 之後交給
//   limiter（ceiling -1 dBFS），peak 被壓回 -1 dBFS，RMS 卻真的變大了——這就是 mastering
//   limiter「增加 loudness」的原理。對照組是直接 hard clip（截掉超過 ±1 的部分）。
//
// 怎麼執行：l05_drum_limiting（不需要參數）；可選 argv[1] = 自己的 WAV 檔路徑
//
// 輸出檔案（在 dawlab_output/）：
//   l05_drums_dry.wav            原始 drum loop
//   l05_drums_limited_6dB.wav    +6 dB → limiter
//   l05_drums_limited_12dB.wav   +12 dB → limiter
//   l05_drums_clipped_12dB.wav   +12 dB → hard clip（對照：聽得到破音）
//
// 該聽到：
//   limited 版本明顯比 dry 大聲，+12 dB 的版本 kick 的「衝擊感」開始變鈍（gain reduction
//   會把 transient 壓扁），但沒有破音；clipped 版本的 kick / snare 則會有明顯的沙沙破音。

#include <dawlab/Common.h>
#include <dawlab/dsp/Limiter.h>

#include <cmath>
#include <cstdio>
#include <string>

namespace {

constexpr int kBlockSize = 512;

template <typename Processor>
void processInBlocks(dawlab::AudioBuffer& buffer, Processor& processor) {
    dawlab::AudioBlock whole(buffer);
    for (int start = 0; start < buffer.getNumSamples(); start += kBlockSize) {
        const int len = std::min(kBlockSize, buffer.getNumSamples() - start);
        processor.process(whole.getSubBlock(start, len));
    }
}

float rmsDb(const dawlab::AudioBuffer& b) { return dawlab::gainToDb(dawlab::rms(b, 0)); }
float peakDb(const dawlab::AudioBuffer& b) { return dawlab::gainToDb(dawlab::peakAbs(b)); }

} // namespace

int main(int argc, char** argv) {
    double sampleRate = 48000.0;
    dawlab::AudioBuffer dry;
    if (argc > 1) {
        dawlab::WavData wav;
        std::string error;
        if (!dawlab::readWav(argv[1], wav, &error)) {
            std::printf("%s\n", error.c_str());
            return 1;
        }
        dry = wav.buffer;
        sampleRate = wav.sampleRate;
    } else {
        dry = dawlab::makeDrumLoop(sampleRate, 2, 120.0, 0.8f);
    }

    const float ceilingDb = -1.0f;
    const float lookaheadMs = 5.0f;
    const float releaseMs = 80.0f;

    std::printf("Drum loop：%d samples @ %.0f Hz，peak %.2f dBFS，RMS %.2f dBFS，crest factor %.2f dB\n\n",
                dry.getNumSamples(), sampleRate, peakDb(dry), rmsDb(dry), peakDb(dry) - rmsDb(dry));
    std::printf("Limiter：ceiling %.1f dBFS, lookahead %.1f ms, release %.0f ms\n\n", ceilingDb, lookaheadMs,
                releaseMs);
    std::printf("%-10s %12s %12s %12s %12s %12s\n", "input gain", "in peak", "out peak", "out RMS", "RMS 增加",
                "max GR");

    bool ok = true;
    for (float gainDb : {0.0f, 6.0f, 12.0f}) {
        dawlab::AudioBuffer out = dry;
        dawlab::dsp::Limiter limiter;
        limiter.prepare(sampleRate, kBlockSize, out.getNumChannels());
        limiter.setCeilingDb(ceilingDb);
        limiter.setLookaheadMs(lookaheadMs);
        limiter.setReleaseMs(releaseMs);
        limiter.setInputGainDb(gainDb);
        limiter.reset(); // 讓 input gain 直接就定位（不要在開頭 ramp）

        float maxGr = 0.0f;
        dawlab::AudioBlock whole(out);
        for (int start = 0; start < out.getNumSamples(); start += kBlockSize) {
            const int len = std::min(kBlockSize, out.getNumSamples() - start);
            limiter.process(whole.getSubBlock(start, len));
            maxGr = std::max(maxGr, limiter.getGainReductionDb()); // UI 的 gain reduction meter 就是這樣讀
        }

        const float inPeak = peakDb(dry) + gainDb;
        std::printf("%+7.0f dB %10.2f dB %10.2f dB %10.2f dB %+10.2f dB %10.2f dB\n", gainDb, inPeak, peakDb(out),
                    rmsDb(out), rmsDb(out) - rmsDb(dry), maxGr);

        // 只看 peak 不夠：safety clamp 會把演算法的任何錯誤夾回 ceiling，所以也要求 clamp 從未觸發
        if (dawlab::peakAbs(out) > dawlab::dbToGain(ceilingDb) * (1.0f + 1.0e-6f) || dawlab::hasNanOrInf(out) ||
            limiter.getSafetyClipCount() != 0)
            ok = false;
        // lookahead 讓 gain「剛好」到位：最大 GR 應該 = max(0, 輸入 peak − ceiling)，不多不少。
        // （最大的峰值有最小的 g，它周圍 L+1 個視窗最小值都等於這個 g，平均出來剛好就是 g）
        const float expectedGr = std::max(0.0f, inPeak - ceilingDb);
        if (std::abs(maxGr - expectedGr) > 0.05f) {
            std::printf("  [FAIL] max GR %.2f dB，預期 %.2f dB\n", maxGr, expectedGr);
            ok = false;
        }

        if (gainDb == 0.0f)
            dawlab::writeWav(dawlab::outputPath("l05_drums_dry.wav"), dry, sampleRate);
        else
            dawlab::writeWav(dawlab::outputPath("l05_drums_limited_" + std::to_string(static_cast<int>(gainDb)) + "dB.wav"),
                             out, sampleRate);
    }

    // 對照組：+12 dB 之後直接 hard clip 到 ±1
    {
        dawlab::AudioBuffer clipped = dry;
        clipped.applyGain(dawlab::dbToGain(12.0f));
        for (int ch = 0; ch < clipped.getNumChannels(); ++ch) {
            float* d = clipped.getWritePointer(ch);
            for (int i = 0; i < clipped.getNumSamples(); ++i)
                d[i] = std::clamp(d[i], -1.0f, 1.0f);
        }
        std::printf("%-10s %10.2f dB %10.2f dB %10.2f dB %+10.2f dB %10s\n", "+12 dB clip", peakDb(dry) + 12.0f,
                    peakDb(clipped), rmsDb(clipped), rmsDb(clipped) - rmsDb(dry), "-");
        dawlab::writeWav(dawlab::outputPath("l05_drums_clipped_12dB.wav"), clipped, sampleRate);
    }

    std::printf("\n已輸出 dawlab_output/l05_drums_{dry,limited_6dB,limited_12dB,clipped_12dB}.wav\n");
    std::printf("%s\n", ok ? "所有 limited 輸出都 ≤ ceiling、safety clamp 未觸發，max GR = 輸入 peak − ceiling。"
                          : "[FAIL] 有輸出超過 ceiling、safety clamp 有觸發，或 max GR 不對！");
    return ok ? 0 : 1;
}
