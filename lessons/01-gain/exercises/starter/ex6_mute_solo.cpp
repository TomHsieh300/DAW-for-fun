// 練習 6（起始碼）：N 軌 mute / solo 解析邏輯
//
// 規則：
//   1. 只要有任何一軌 solo，就只有 solo 的軌出聲。
//   2. mute 永遠有效（solo + mute → 不出聲）。
//   3. soloSafe 的軌在 solo 模式下仍然出聲（但自己 mute 時一樣不出聲）。
//   audible = !mute && (!anySolo || solo || soloSafe)
// 完成 TODO 之後，self-check 全部通過就算完成。起始碼執行時會印出 FAIL，這是預期的。

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
    // TODO: 先找出是否有任何一軌 solo，再依規則逐軌判斷
    return std::vector<bool>(tracks.size(), true);
}

// 出聲與否 → 這一軌 Gain 的目標 dB（出聲 = 0 dB，靜音 = kMinusInfinityDb）
float audibleToGainDb(bool audible) {
    (void)audible;
    // TODO
    return 0.0f;
}

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

} // namespace

int main() {
    std::vector<TrackState> t(4);
    DAWLAB_CHECK(sameAs(resolveAudible(t), {true, true, true, true}));

    t[1].mute = true;
    DAWLAB_CHECK(sameAs(resolveAudible(t), {true, false, true, true}));

    t = std::vector<TrackState>(4);
    t[2].solo = true;
    DAWLAB_CHECK(sameAs(resolveAudible(t), {false, false, true, false}));

    t[0].solo = true;
    DAWLAB_CHECK(sameAs(resolveAudible(t), {true, false, true, false}));

    t[2].mute = true;
    DAWLAB_CHECK(sameAs(resolveAudible(t), {true, false, false, false}));

    t = std::vector<TrackState>(4);
    t[3].soloSafe = true;
    t[1].solo = true;
    DAWLAB_CHECK(sameAs(resolveAudible(t), {false, true, false, true}));

    t[3].mute = true;
    DAWLAB_CHECK(sameAs(resolveAudible(t), {false, true, false, false}));

    t = std::vector<TrackState>(4);
    t[3].soloSafe = true;
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
    DAWLAB_CHECK(maxDiff < naturalDiff * 1.05f);
    DAWLAB_CHECK(rawDiff > naturalDiff * 2.0f);
    DAWLAB_CHECK(quietPeak == 0.0f);
    DAWLAB_CHECK(peakAbs(audio.getReadPointer(0) + 70 * kBlock, 10 * kBlock) > 0.49f);

    return dawlab::test::summary("l01_ex6_starter");
}
