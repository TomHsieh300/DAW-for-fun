// 範例 4：transient shaper 對鼓組 loop 做 attack ±12 dB、sustain ±12 dB
//
// 示範什麼：
//   - 先用兩個 EnvelopeFollower（fast：0.5 / 30 ms，slow：10 / 150 ms）追第一個 kick，
//     每 5 ms 印一次 fast、slow 與差值 d = 20·log10(fast/slow)：起音時 d > 0、延音時 d < 0
//   - TransientShaper 四種設定：attack +12 / -12 dB、sustain -12 / +12 dB，各自輸出 WAV
//   - 印出每個版本的 peak、RMS 與 crest factor（peak − RMS）：attack +12 讓 crest factor 變大、
//     attack -12 讓 crest factor 變小；sustain +12 會把 kick 的尾巴推得比起音還大聲（peak 反而上升）
// 怎麼執行：l08_transient_shaper_drums [input.wav]，不給參數就用 makeDrumLoop()。
// 輸出檔案：dawlab_output/l08_ts_dry.wav、l08_ts_attack_plus12.wav、l08_ts_attack_minus12.wav、
//          l08_ts_sustain_minus12.wav、l08_ts_sustain_plus12.wav。
// 該聽到什麼：attack +12 的鼓更「打」、更靠近；attack -12 變圓、變遠；sustain -12 的尾巴（kick 的低頻尾、
//            snare 的沙沙聲）被縮短，像在更乾的房間；sustain +12 則尾巴變長、整體更「滿」。

#include <dawlab/Common.h>
#include <dawlab/dsp/EnvelopeFollower.h>
#include <dawlab/dsp/TransientShaper.h>

#include <cstdio>
#include <string>

using namespace dawlab;
using dawlab::dsp::DetectorMode;
using dawlab::dsp::EnvelopeFollower;
using dawlab::dsp::TransientShaper;

namespace {

constexpr int kBlockSize = 512;

struct Stats {
    float peakDb, rmsDb, crestDb;
};

Stats measure(const AudioBuffer& b) {
    const float p = peakAbs(b);
    float r = 0.0f;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        r = std::max(r, rms(b, ch));
    return {gainToDb(p), gainToDb(r), gainToDb(p) - gainToDb(r)};
}

AudioBuffer runShaper(const AudioBuffer& input, TransientShaper& ts) {
    AudioBuffer out = input;
    AudioBlock whole(out);
    const int n = out.getNumSamples();
    for (int start = 0; start < n; start += kBlockSize)
        ts.process(whole.getSubBlock(start, std::min(kBlockSize, n - start)));
    return out;
}

} // namespace

int main(int argc, char** argv) {
    std::printf("第 08 章 範例 4：transient shaper 對鼓組 loop\n");

    const double sampleRate = 48000.0;
    AudioBuffer dry;
    if (argc > 1) {
        WavData wav;
        std::string err;
        if (!readWav(argv[1], wav, &err)) {
            std::printf("讀取失敗：%s\n", err.c_str());
            return 1;
        }
        dry = wav.buffer;
    } else {
        dry = makeDrumLoop(sampleRate);
    }

    // ---- 1. 快慢 envelope 的差值 ------------------------------------------------
    std::printf("\n[1] 第一個 kick（0 ms 起）附近的 fast / slow envelope 與差值 d（每 5 ms 一列）\n");
    EnvelopeFollower fast, slow;
    fast.prepare(sampleRate);
    fast.setMode(DetectorMode::Peak);
    fast.setAttackMs(TransientShaper::kFastAttackMs);
    fast.setReleaseMs(TransientShaper::kFastReleaseMs);
    slow.prepare(sampleRate);
    slow.setMode(DetectorMode::Peak);
    slow.setAttackMs(TransientShaper::kSlowAttackMs);
    slow.setReleaseMs(TransientShaper::kSlowReleaseMs);
    std::printf("    時間(ms)  fast(dB)  slow(dB)    d(dB)   → gain（attack +12 / sustain -12）\n");
    const int probeSamples = secondsToSamples(0.20, sampleRate);
    const int printEvery = msToSamples(5.0, sampleRate);
    for (int i = 0; i < probeSamples; ++i) {
        float det = 0.0f;
        for (int ch = 0; ch < dry.getNumChannels(); ++ch)
            det = std::max(det, std::abs(dry.getSample(ch, i)));
        const float f = fast.processSample(det);
        const float s = slow.processSample(det);
        if (i % printEvery == 0) {
            const float d = gainToDb(f) - gainToDb(s);
            const float g = d > 0.0f ? 12.0f * std::min(d / TransientShaper::kFullScaleDb, 1.0f)
                                     : -12.0f * std::min(-d / TransientShaper::kFullScaleDb, 1.0f);
            std::printf("    %7.1f   %7.1f   %7.1f   %+6.1f   %+6.1f dB\n", samplesToMs(i, sampleRate), gainToDb(f),
                        gainToDb(s), d, g);
        }
    }
    std::printf("    （起音：fast 先衝上去、slow 還在爬 → d > 0；之後 fast 跟著訊號掉、slow 撐著 → d < 0）\n");

    // ---- 2. 四種設定 ------------------------------------------------------------
    struct Setting {
        const char* name;
        const char* file;
        float attackDb, sustainDb;
    };
    const Setting settings[] = {
        {"dry", "l08_ts_dry.wav", 0.0f, 0.0f},
        {"attack +12", "l08_ts_attack_plus12.wav", 12.0f, 0.0f},
        {"attack -12", "l08_ts_attack_minus12.wav", -12.0f, 0.0f},
        {"sustain -12", "l08_ts_sustain_minus12.wav", 0.0f, -12.0f},
        {"sustain +12", "l08_ts_sustain_plus12.wav", 0.0f, 12.0f},
    };

    std::printf("\n[2] 四種設定的 peak / RMS / crest factor（dB）\n");
    std::printf("    設定          peak     RMS    crest   Δcrest vs dry\n");
    float dryCrest = 0.0f;
    for (const auto& s : settings) {
        TransientShaper ts;
        ts.prepare(sampleRate, kBlockSize, dry.getNumChannels());
        ts.setAttackDb(s.attackDb);
        ts.setSustainDb(s.sustainDb);
        const AudioBuffer out = runShaper(dry, ts);
        writeWav(outputPath(s.file), out, sampleRate, 32); // attack +12 可能超過 0 dBFS，用 float 存
        const Stats st = measure(out);
        if (s.attackDb == 0.0f && s.sustainDb == 0.0f)
            dryCrest = st.crestDb;
        std::printf("    %-12s %6.2f  %6.2f  %6.2f   %+6.2f%s\n", s.name, st.peakDb, st.rmsDb, st.crestDb,
                    st.crestDb - dryCrest, st.peakDb > 0.0f ? "   （peak 超過 0 dBFS：WAV 以 32-bit float 存）" : "");
    }
    std::printf("    → attack +12：peak 幾乎 +12 dB、RMS 升得少 → crest factor 變大（更「打」）\n");
    std::printf("    → attack -12：peak 下降比 RMS 多 → crest factor 變小（更圓、更遠）\n");
    std::printf("    → sustain -12：尾巴變短、RMS 下降、peak 幾乎不動 → crest factor 變大（更乾）\n");
    std::printf("    → sustain +12：kick 尾巴整段 +12 dB，比原本的起音還大聲 → peak 跟著升高；\n"
                "      實務上 sustain 加強多半只用 +3～+6 dB，後面再接 limiter\n");

    std::printf("\n輸出：dawlab_output/l08_ts_dry.wav、l08_ts_attack_plus12.wav、l08_ts_attack_minus12.wav、"
                "l08_ts_sustain_minus12.wav、l08_ts_sustain_plus12.wav\n");
    return 0;
}
