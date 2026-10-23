// 範例 3：壓縮鼓組 loop —— 壓縮前後的 peak / RMS / crest factor、attack 時間掃描，以及 gain reduction meter。
//
// 示範什麼：
//   - 在「像音樂」的素材上用 Compressor，逐 block（512 samples）處理，模擬 audio callback
//   - crest factor（peak 與 RMS 的距離）怎麼隨 attack 時間改變：
//       attack 極快（20 µs，類似 FET 硬體 compressor）→ 連單一 sample 的尖峰都壓到 → crest 下降；補 makeup 讓 peak 回到原本
//       大小後，RMS 變大（這就是「壓縮讓東西聽起來比較大聲」的來源）
//       attack 1 ms 以上 → 鼓的尖峰（snare 的雜訊、hi-hat 只有幾個 sample 寬）趁 GR 還沒升上來就溜過去，
//       但後面的「身體」被壓 → crest 反而上升，聽起來更「有打擊感」（punch）；也說明為什麼真正要「保證不超過」
//       需要第 05 章的 lookahead limiter
//   - getGainReductionDb() 的用法：每個 block 讀一次，畫成文字版 GR meter
// 怎麼執行：l04_drum_loop [input.wav]，不給參數就用 makeDrumLoop() 合成的鼓組。
// 輸出檔案：dawlab_output/l04_drums_dry.wav、l04_drums_compressed.wav（attack 20 µs，含 makeup，peak 與 dry 一樣）、
//          l04_drums_compressed_slow_attack.wav（attack 30 ms，含 makeup）。
// 該聽到什麼：compressed 版的 kick / snare「衝擊感」變小、整體更紮實也更大聲；slow attack 版保留鼓頭的打擊感，
//            但尾巴被壓得比較緊、整體反而比 dry 小聲一點。

#include <dawlab/Common.h>
#include <dawlab/dsp/Compressor.h>

#include <cstdio>
#include <string>
#include <vector>

using namespace dawlab;
using dawlab::dsp::Compressor;

namespace {

constexpr int kBlockSize = 512;

struct Stats {
    float peakDb, rmsDb, crestDb;
};

// peak 取所有 channel 的最大 |sample|；RMS 取所有 channel 中最大的
Stats measure(const AudioBuffer& b) {
    const float p = peakAbs(b);
    float r = 0.0f;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        r = std::max(r, rms(b, ch));
    return {gainToDb(p), gainToDb(r), gainToDb(p) - gainToDb(r)};
}

// 逐 block 壓縮；每個 block 記下 getGainReductionDb()
AudioBuffer compress(const AudioBuffer& input, Compressor& comp, std::vector<float>& grPerBlock) {
    AudioBuffer out = input;
    AudioBlock whole(out);
    const int n = out.getNumSamples();
    grPerBlock.clear();
    for (int start = 0; start < n; start += kBlockSize) {
        comp.process(whole.getSubBlock(start, std::min(kBlockSize, n - start)));
        grPerBlock.push_back(comp.getGainReductionDb());
    }
    return out;
}

void printMeter(const std::vector<float>& grPerBlock, double sampleRate, double seconds) {
    // 每 ~50 ms 印一行：時間、該段最大 GR、文字 bar（每格 0.5 dB）
    const double blockSec = kBlockSize / sampleRate;
    const int blocksPerRow = std::max(1, static_cast<int>(0.05 / blockSec));
    std::printf("    時間(s)  GR(dB)  meter（每格 0.5 dB）\n");
    for (size_t i = 0; i < grPerBlock.size() && static_cast<double>(i) * blockSec < seconds; i += blocksPerRow) {
        float maxGr = 0.0f;
        for (size_t j = i; j < std::min(grPerBlock.size(), i + blocksPerRow); ++j)
            maxGr = std::max(maxGr, grPerBlock[j]);
        const int bars = static_cast<int>(std::lround(maxGr / 0.5f));
        std::printf("    %6.2f   %5.2f   %s\n", static_cast<double>(i) * blockSec, maxGr,
                    std::string(static_cast<size_t>(bars), '|').c_str());
    }
}

struct Setting {
    const char* name;
    float thresholdDb, ratio, kneeDb, attackMs, releaseMs;
    const char* wavName;
};

Compressor makeCompressor(const Setting& s, double sampleRate, int numChannels) {
    Compressor comp;
    comp.prepare(sampleRate, kBlockSize, numChannels);
    comp.setThresholdDb(s.thresholdDb);
    comp.setRatio(s.ratio);
    comp.setKneeDb(s.kneeDb);
    comp.setAttackMs(s.attackMs);
    comp.setReleaseMs(s.releaseMs);
    comp.setStereoLink(true);
    return comp;
}

} // namespace

int main(int argc, char** argv) {
    std::printf("第 04 章 範例 3：鼓組 loop 壓縮\n");

    double sampleRate = 48000.0;
    AudioBuffer dry;
    if (argc > 1) {
        WavData wav;
        std::string err;
        if (!readWav(argv[1], wav, &err)) {
            std::printf("讀取失敗：%s\n", err.c_str());
            return 1;
        }
        dry = wav.buffer;
        sampleRate = wav.sampleRate;
        std::printf("輸入：%s（%d ch, %d samples, %.0f Hz）\n", argv[1], dry.getNumChannels(), dry.getNumSamples(),
                    sampleRate);
    } else {
        dry = makeDrumLoop(sampleRate, 2, 120.0);
        std::printf("輸入：makeDrumLoop() 合成鼓組，2 小節 120 BPM（%.2f 秒）\n",
                    dry.getNumSamples() / sampleRate);
    }
    const int numChannels = dry.getNumChannels();
    const Stats in = measure(dry);
    std::printf("\n  壓縮前：peak %6.2f dBFS, RMS %6.2f dBFS, crest factor %5.2f dB\n", in.peakDb, in.rmsDb, in.crestDb);

    writeWav(outputPath("l04_drums_dry.wav"), dry, sampleRate);

    // ---------- 三組代表性設定 ----------
    const Setting settings[] = {
        {"極快 attack（壓 transient，類似 FET 硬體 compressor）", -20.0f, 4.0f, 6.0f, 0.02f, 80.0f, "l04_drums_compressed.wav"},
        {"慢 attack（放過 transient）", -20.0f, 4.0f, 6.0f, 30.0f, 80.0f, "l04_drums_compressed_slow_attack.wav"},
        {"重壓（limiter 風格）", -26.0f, 10.0f, 0.0f, 0.02f, 50.0f, nullptr},
    };

    for (const Setting& s : settings) {
        Compressor comp = makeCompressor(s, sampleRate, numChannels);
        std::vector<float> gr;
        AudioBuffer wet = compress(dry, comp, gr);
        const Stats raw = measure(wet);
        float maxGr = 0.0f;
        double meanGr = 0.0;
        for (float g : gr) {
            maxGr = std::max(maxGr, g);
            meanGr += g;
        }
        meanGr /= static_cast<double>(gr.size());

        // makeup：讓輸出 peak 回到跟 dry 一樣，才能公平比較 RMS（「一樣大的 peak 之下誰比較大聲」）
        const float makeupDb = in.peakDb - raw.peakDb;
        wet.applyGain(dbToGain(makeupDb));
        const Stats out = measure(wet);

        std::printf("\n=== %s：T %.0f dB, R %.0f:1, knee %.0f dB, attack %.2f ms, release %.0f ms ===\n", s.name,
                    s.thresholdDb, s.ratio, s.kneeDb, s.attackMs, s.releaseMs);
        std::printf("  GR：最大 %.2f dB，平均 %.2f dB\n", maxGr, meanGr);
        std::printf("  未補 makeup：peak %6.2f dBFS, RMS %6.2f dBFS, crest %5.2f dB（peak 降 %.2f dB、RMS 降 %.2f dB）\n",
                    raw.peakDb, raw.rmsDb, raw.crestDb, in.peakDb - raw.peakDb, in.rmsDb - raw.rmsDb);
        std::printf("  補 makeup %+.2f dB 後：peak %6.2f dBFS, RMS %6.2f dBFS, crest %5.2f dB（RMS 比 dry %s %.2f dB）\n",
                    makeupDb, out.peakDb, out.rmsDb, out.crestDb, out.rmsDb >= in.rmsDb ? "大" : "小",
                    std::abs(out.rmsDb - in.rmsDb));
        if (s.wavName) {
            writeWav(outputPath(s.wavName), wet, sampleRate);
            std::printf("  已輸出 dawlab_output/%s\n", s.wavName);
        }
        if (&s == &settings[0]) {
            std::printf("  前 1 秒的 gain reduction meter：\n");
            printMeter(gr, sampleRate, 1.0);
        }
    }

    // ---------- attack 時間掃描 ----------
    std::printf("\n=== attack 時間掃描（T -20 dB, R 4:1, knee 6 dB, release 80 ms；makeup 後 peak 與 dry 相同）===\n");
    std::printf("  attack(ms) | 最大GR | peak降 | RMS降 | crest(dB) | makeup後 RMS 相對 dry\n");
    std::printf("  -----------+--------+--------+-------+-----------+----------------------\n");
    for (float attackMs : {0.0f, 0.02f, 0.1f, 0.5f, 1.0f, 3.0f, 10.0f, 30.0f}) {
        const Setting s{"", -20.0f, 4.0f, 6.0f, attackMs, 80.0f, nullptr};
        Compressor comp = makeCompressor(s, sampleRate, numChannels);
        std::vector<float> gr;
        const AudioBuffer wet = compress(dry, comp, gr);
        const Stats raw = measure(wet);
        float maxGr = 0.0f;
        for (float g : gr)
            maxGr = std::max(maxGr, g);
        // makeup 後 RMS 相對 dry = (RMS_wet + makeup) - RMS_dry = crest_dry - crest_wet
        std::printf("  %10.2f | %6.2f | %6.2f | %5.2f | %9.2f | %+6.2f dB\n", attackMs, maxGr, in.peakDb - raw.peakDb,
                    in.rmsDb - raw.rmsDb, raw.crestDb, in.crestDb - raw.crestDb);
    }
    std::printf("  → attack 越快，peak 被壓得越多，crest 越低，補 makeup 後越大聲；\n"
                "    attack ≥ 1 ms 時幾個 sample 寬的尖峰幾乎原封不動溜過去，壓到的是「身體」，crest 反而上升。\n"
                "    想「保證」peak 不超過某個值，要靠第 05 章 limiter 的 lookahead。\n");

    std::printf("\n  有 NaN/Inf？ %s\n", hasNanOrInf(dry) ? "是" : "否");
    return 0;
}
