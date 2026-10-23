// 範例 4：reverb 的 send / return（aux bus）用法 —— pre-fader vs post-fader
//
// 示範什麼：
//   混音時 reverb 通常不是「插在每一軌上」，而是放在一條 aux / return bus 上：
//
//     drum track ──► fader ──────────────────────┐
//            │                                   ├─► master
//            └─(send level)─► reverb bus (100% wet) ─► return fader ┘
//
//   1. reverb 設 mix = 1（純 wet），送多少進去由 send level 決定，混多少回來由 return fader 決定。
//   2. post-fader send（預設）：send 在 fader 之後 → 拉掉 track fader，reverb 也跟著變小。
//      pre-fader send：send 在 fader 之前 → fader 拉到底，reverb 還在（「只剩殘響」的效果）。
//   3. 一條 reverb bus 可以給多軌共用（這裡用 drum loop 與 pluck 兩軌），聽起來像在同一個空間。
//
// 怎麼執行：不帶參數直接執行（ctest 也會跑）。
// 輸出檔案（build/dawlab_output/）：
//   l12_send_dry.wav          兩軌直接相加（沒有 reverb）
//   l12_send_mix.wav          dry + reverb return（post-fader send，send -6 dB，return 0 dB，RT60 1.6 s）
//   l12_send_prefader.wav     track fader 拉到 -inf、但 pre-fader send 仍送進 reverb → 只剩殘響
// 該聽到什麼：mix 版本的鼓與 pluck 多了空間感但 transient 還在；prefader 版本只有「房間的回聲」沒有直達音。

#include <dawlab/Common.h>
#include <dawlab/dsp/Reverb.h>

#include <cstdio>
#include <vector>

using namespace dawlab;
using namespace dawlab::dsp;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

struct Track {
    AudioBuffer audio;
    float faderDb = 0.0f;
    float sendDb = -6.0f;
    bool preFader = false;
};

// 混音：回傳 (master, returnOnly)。reverb 在 aux bus 上，mix = 1。
void mixdown(const std::vector<Track>& tracks, Reverb& reverb, float returnDb, AudioBuffer& master,
             AudioBuffer& wetOnly) {
    const int n = tracks.front().audio.getNumSamples();
    master.setSize(2, n);
    wetOnly.setSize(2, n);
    AudioBuffer bus(2, n); // reverb bus 的輸入（所有 track 的 send 相加）
    for (const Track& t : tracks) {
        const float fader = dbToGain(t.faderDb);
        const float send = dbToGain(t.sendDb) * (t.preFader ? 1.0f : fader); // post-fader：send 在 fader 之後
        for (int ch = 0; ch < 2; ++ch) {
            master.addFrom(ch, 0, t.audio, ch, 0, n, fader);
            bus.addFrom(ch, 0, t.audio, ch, 0, n, send);
        }
    }
    // reverb bus：逐 block 處理（mix = 1 → 純 wet）
    reverb.reset();
    for (int start = 0; start < n; start += kBlockSize) {
        const int len = std::min(kBlockSize, n - start);
        reverb.process(bus.toBlock().getSubBlock(start, len));
    }
    const float ret = dbToGain(returnDb);
    for (int ch = 0; ch < 2; ++ch) {
        wetOnly.addFrom(ch, 0, bus, ch, 0, n, ret);
        master.addFrom(ch, 0, bus, ch, 0, n, ret);
    }
}

} // namespace

int main() {
    std::printf("=== 第 12 章 範例 4：send / return ===\n\n");

    // 兩軌素材：drum loop（2 小節 @120 BPM = 4 s）與 pluck sequence（4 s）
    std::vector<Track> tracks(2);
    tracks[0].audio = makeDrumLoop(kSampleRate, 2, 120.0, 0.55f, 2);
    tracks[1].audio = makePluckSequence(kSampleRate, 4.0, 0.25f, 2);
    tracks[0].sendDb = -6.0f;
    tracks[1].sendDb = -3.0f; // pluck 多送一點（旋律樂器通常比鼓更需要空間感）

    Reverb reverb;
    reverb.prepare(kSampleRate, kBlockSize, 2);
    reverb.setMix(1.0f); // bus 上的 reverb 一律 100% wet
    reverb.setDecaySeconds(1.6f);
    reverb.setPreDelayMs(20.0f); // 20 ms pre-delay：讓 transient 先出來，殘響稍後才跟上
    reverb.setDampingHz(5000.0f);
    reverb.setSize(0.6f);
    reverb.setWidth(1.0f);

    AudioBuffer master, wet;

    // ---- 1. 沒有 reverb -----------------------------------------------------------
    {
        AudioBuffer dry(2, tracks[0].audio.getNumSamples());
        for (const Track& t : tracks)
            for (int ch = 0; ch < 2; ++ch)
                dry.addFrom(ch, 0, t.audio, ch, 0, dry.getNumSamples(), dbToGain(t.faderDb));
        writeWav(outputPath("l12_send_dry.wav"), dry, kSampleRate);
        std::printf("dry 兩軌相加           : RMS %.4f, peak %.3f\n", rms(dry, 0), peakAbs(dry));
    }

    // ---- 2. post-fader send，fader 0 dB -------------------------------------------
    mixdown(tracks, reverb, 0.0f, master, wet);
    const float wetRmsFader0 = rms(wet, 0);
    writeWav(outputPath("l12_send_mix.wav"), master, kSampleRate);
    std::printf("post-fader, fader 0 dB : master RMS %.4f, peak %.3f, return(wet) RMS %.4f\n", rms(master, 0),
                peakAbs(master), wetRmsFader0);

    // ---- 3. post-fader send，fader -12 dB：return 也跟著掉 12 dB ----------------------
    for (Track& t : tracks)
        t.faderDb = -12.0f;
    mixdown(tracks, reverb, 0.0f, master, wet);
    std::printf("post-fader, fader -12dB: return RMS %.4f（相對 0 dB 時 %+.2f dB → 跟著 fader 掉）\n", rms(wet, 0),
                gainToDb(rms(wet, 0) / wetRmsFader0));

    // ---- 4. pre-fader send，fader -inf：return 不變，master 只剩殘響 -------------------
    for (Track& t : tracks) {
        t.faderDb = -120.0f; // dbToGain 會回傳 0（低於 kMinusInfinityDb）
        t.preFader = true;
    }
    mixdown(tracks, reverb, 0.0f, master, wet);
    writeWav(outputPath("l12_send_prefader.wav"), master, kSampleRate);
    std::printf("pre-fader,  fader -inf : return RMS %.4f（相對 0 dB 時 %+.2f dB → 不受 fader 影響）, master = 純殘響\n\n",
                rms(wet, 0), gainToDb(rms(wet, 0) / wetRmsFader0));

    std::printf("觀念整理：\n");
    std::printf("  - reverb bus 上的 reverb 永遠 mix = 1，「多濕」由 send 與 return 的 fader 決定。\n");
    std::printf("  - 多軌共用一條 reverb bus：省 CPU，而且所有樂器聽起來在同一個空間。\n");
    std::printf("  - pre-fader send 常用在耳機監聽混音、或「fader 拉掉只剩殘響」的特效。\n");
    std::printf("\n輸出：dawlab_output/l12_send_dry.wav, l12_send_mix.wav, l12_send_prefader.wav\n");
    return 0;
}
