// 練習 6 解答：N 軌 mute / solo 解析邏輯（第 14 章 DAW 的 mixer 用的是去掉 soloSafe 的同一個式子：
//   track 沒有 solo-safe 旗標，只有 FX bus 與 master 隱含 solo-safe；見 solutions/README.md）
//
// 規則（本教材的定義，與 Pro Tools / Logic 的預設行為一致）：
//   1. 只要有任何一軌按了 solo，就進入「solo 模式」：只有 solo 的軌可以出聲。
//   2. mute 永遠有效：一軌同時 solo + mute → 不出聲（mute 優先）。
//   3. solo-safe（有些 DAW 叫 solo isolate）：這一軌在 solo 模式下仍然出聲（例如 FX return、
//      metronome），但它自己 mute 時一樣不出聲。
//
// 真值表（S = 有任何軌 solo）：
//   audible = !mute && (!S || solo || soloSafe)
//
// 實際 DAW 不會直接把 sample 設成 0，而是把每一軌的 Gain 目標設成 0 dB 或 −∞ dB，
// 由 SmoothedValue 用 ramp 走過去，mute / unmute 才不會 click。

#include <dawlab/Common.h>
#include <dawlab/dsp/Gain.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace dawlab;

struct TrackState {
    bool mute = false;
    bool solo = false;
    bool soloSafe = false;
};

// 回傳每一軌「現在應該出聲嗎」
std::vector<bool> resolveAudible(const std::vector<TrackState>& tracks) {
    bool anySolo = false;
    for (const auto& t : tracks)
        anySolo = anySolo || t.solo;

    std::vector<bool> audible(tracks.size());
    for (size_t i = 0; i < tracks.size(); ++i) {
        const TrackState& t = tracks[i];
        audible[i] = !t.mute && (!anySolo || t.solo || t.soloSafe);
    }
    return audible;
}

// 出聲與否 → 這一軌 Gain 的目標 dB
float audibleToGainDb(bool audible) { return audible ? 0.0f : kMinusInfinityDb; }

// ---- 把 mute 解析結果餵給 Gain（這段不是 TODO，直接用）-------------------------------
constexpr double kSr = 48000.0;
constexpr int kBlock = 512;
// mute / unmute 的時間點刻意「不」放在 1 kHz 正弦波的零交越：
//   15372 = 30·512 + 12，15372 / 48 = 320.25 個週期 → 正弦波的正峰值（+0.5）
//   30756 = 60·512 + 36，30756 / 48 = 640.75 個週期 → 正弦波的負峰值（−0.5）
// 如果放在 block 邊界 30·512 = 15360（剛好 320 個週期），該處 x = 0，就算完全不 ramp、
// 瞬間跳到 0 也不會產生陡坡（README 2.4 節的零交越例外），檢查就測不出 click。
constexpr int kMuteAt = 30 * kBlock + 12;   // ≈ 0.320 s 按下 mute
constexpr int kUnmuteAt = 60 * kBlock + 36; // ≈ 0.641 s 放開 mute

// 一軌 1 kHz 正弦波經過 Gain，在 kMuteAt 按下 mute、kUnmuteAt 放開。
// 事件落在 block 中間時把 block 切成兩個 sub-block（事件之前用舊設定、之後用新設定），
// 也就是 DAW 的 sample-accurate automation 的做法。rampSeconds = 0 代表完全不平滑（對照組）。
AudioBuffer runMuteScenario(double rampSeconds) {
    AudioBuffer audio = makeSine(1000.0, kSr, 1.0, 0.5f, 1);
    dsp::Gain gain;
    gain.setRampSeconds(rampSeconds);
    gain.prepare(kSr, kBlock, 1);
    std::vector<TrackState> one(1);
    AudioBlock ab(audio);
    const int n = audio.getNumSamples();
    int pos = 0;
    while (pos < n) {
        if (pos == kMuteAt)
            one[0].mute = true;
        if (pos == kUnmuteAt)
            one[0].mute = false;
        gain.setGainDb(audibleToGainDb(resolveAudible(one)[0]));
        int end = std::min(n, (pos / kBlock + 1) * kBlock); // 這個 audio block 的結尾
        if (kMuteAt > pos && kMuteAt < end)
            end = kMuteAt; // 事件落在 block 中間：切開
        if (kUnmuteAt > pos && kUnmuteAt < end)
            end = kUnmuteAt;
        gain.process(ab.getSubBlock(pos, end - pos));
        pos = end;
    }
    return audio;
}

// 一階差分最大值 max |x[n] − x[n−1]|：click 會讓它遠大於正弦波的自然值 A·2πf/fs
float maxFirstDifference(const AudioBuffer& audio) {
    float maxDiff = 0.0f;
    for (int i = 1; i < audio.getNumSamples(); ++i)
        maxDiff = std::max(maxDiff, std::abs(audio.getSample(0, i) - audio.getSample(0, i - 1)));
    return maxDiff;
}

namespace {

bool sameAs(const std::vector<bool>& actual, std::initializer_list<bool> expected) {
    if (actual.size() != expected.size())
        return false;
    size_t i = 0;
    for (bool e : expected)
        if (actual[i++] != e)
            return false;
    return true;
}

void printCase(const char* name, const std::vector<TrackState>& tracks) {
    const auto a = resolveAudible(tracks);
    std::printf("%-34s", name);
    for (size_t i = 0; i < tracks.size(); ++i)
        std::printf(" [%zu:%s%s%s→%s]", i, tracks[i].mute ? "M" : "-", tracks[i].solo ? "S" : "-",
                    tracks[i].soloSafe ? "F" : "-", a[i] ? "on " : "off");
    std::printf("\n");
}

} // namespace

int main() {
    std::vector<TrackState> t(4);

    printCase("全部正常", t);
    DAWLAB_CHECK(sameAs(resolveAudible(t), {true, true, true, true}));

    t[1].mute = true;
    printCase("mute 1", t);
    DAWLAB_CHECK(sameAs(resolveAudible(t), {true, false, true, true}));

    t = std::vector<TrackState>(4);
    t[2].solo = true;
    printCase("solo 2", t);
    DAWLAB_CHECK(sameAs(resolveAudible(t), {false, false, true, false}));

    t[0].solo = true;
    printCase("solo 0 + solo 2", t);
    DAWLAB_CHECK(sameAs(resolveAudible(t), {true, false, true, false}));

    t[2].mute = true;
    printCase("solo 0 + solo 2 + mute 2（mute 優先）", t);
    DAWLAB_CHECK(sameAs(resolveAudible(t), {true, false, false, false}));

    t = std::vector<TrackState>(4);
    t[3].soloSafe = true;
    t[1].solo = true;
    printCase("solo 1 + 3 是 solo-safe", t);
    DAWLAB_CHECK(sameAs(resolveAudible(t), {false, true, false, true}));

    t[3].mute = true;
    printCase("solo 1 + 3 solo-safe 但 mute", t);
    DAWLAB_CHECK(sameAs(resolveAudible(t), {false, true, false, false}));

    t = std::vector<TrackState>(4);
    t[3].soloSafe = true;
    printCase("沒有人 solo，3 是 solo-safe", t);
    DAWLAB_CHECK(sameAs(resolveAudible(t), {true, true, true, true}));

    DAWLAB_CHECK(resolveAudible({}).empty());

    // 把結果餵給 Gain：mute 要經過 ramp，不能有 click
    const float naturalDiff = 0.5f * static_cast<float>(kTwoPi * 1000.0 / kSr);
    const AudioBuffer audio = runMuteScenario(0.05); // Gain 預設的 50 ms ramp
    const AudioBuffer raw = runMuteScenario(0.0);    // 對照組：完全不平滑
    const float maxDiff = maxFirstDifference(audio);
    const float rawDiff = maxFirstDifference(raw);
    const int quietStart = kMuteAt + msToSamples(60.0, kSr);
    const float quietPeak = peakAbs(audio.getReadPointer(0) + quietStart, kUnmuteAt - quietStart);
    std::printf("mute/unmute 經過 Gain（50 ms ramp）：一階差分最大值 %.4f（正弦波自然值 %.4f），mute 期間 peak = %.1f dBFS\n",
                maxDiff, naturalDiff, gainToDb(quietPeak));
    std::printf("對照組（ramp = 0，瞬間跳）：一階差分最大值 %.4f（自然值的 %.1f 倍）→ 證明這個檢查抓得到 click\n",
                rawDiff, rawDiff / naturalDiff);
    DAWLAB_CHECK(maxDiff < naturalDiff * 1.05f); // 沒有 click
    DAWLAB_CHECK(rawDiff > naturalDiff * 2.0f);  // 對照組：不平滑就有 click（檢查有鑑別力）
    DAWLAB_CHECK(quietPeak == 0.0f);             // mute 期間真的是 0
    DAWLAB_CHECK(peakAbs(audio.getReadPointer(0) + 70 * kBlock, 10 * kBlock) > 0.49f); // unmute 後回來了
    writeWav(outputPath("l01_ex6_mute_solo.wav"), audio, kSr);

    return dawlab::test::summary("l01_ex6");
}
