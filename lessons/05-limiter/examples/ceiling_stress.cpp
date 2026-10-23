// 範例 4：ceiling 保證的壓力測試——極端訊號與隨機 block size
//
// 示範什麼：
//   Limiter 的核心承諾是「任何輸入下，輸出 sample peak ≤ ceiling」。這支程式用各種
//   惡意訊號（+40 dB 的雜訊、±10（+20 dBFS）的 50 Hz 方波、DC、±1000 的脈衝、每個 sample 正負交替的 ±10⁴……）
//   搭配隨機的 block 長度與隨機參數去轟它，然後檢查每一種情況的輸出 peak 與 safety clamp 的觸發次數。
//   這也是練習 6（fuzz test）的縮小版。
//
// 怎麼執行：l05_ceiling_stress（不需要參數）
// 輸出檔案：無（純數字檢查）
// 該看到：每一列的 "out peak"（線性值）都 ≤ ceiling，而且 "clamp#" 都是 0；最後再驗證 block 512 / 1 / 隨機 block
//         的輸出逐 sample 相同，印出 "全部通過"。
//
// 為什麼要看 clamp#：Limiter 最後有一道 safety clamp，就算演算法寫錯（視窗長度、延遲差一個 sample……）
// 也會把輸出硬夾回 ceiling，所以「out peak ≤ ceiling」單獨看永遠會過。clamp# == 0 才證明是演算法本身沒超過，
// 這支程式因此可以當 regression test 用。

#include <dawlab/Common.h>
#include <dawlab/dsp/Limiter.h>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;

struct Case {
    std::string name;
    dawlab::AudioBuffer signal;
};

dawlab::AudioBuffer fill(int numSamples, const std::function<float(int, int)>& f, int numChannels = 2) {
    dawlab::AudioBuffer b(numChannels, numSamples);
    for (int ch = 0; ch < numChannels; ++ch)
        for (int i = 0; i < numSamples; ++i)
            b.setSample(ch, i, f(ch, i));
    return b;
}

// 用隨機長度（1..maxBlock）的 block 餵 limiter，模擬「host 給的 block 大小不固定」
void processRandomBlocks(dawlab::AudioBuffer& buffer, dawlab::dsp::Limiter& limiter, dawlab::NoiseGenerator& rng,
                         int maxBlock) {
    dawlab::AudioBlock whole(buffer);
    int start = 0;
    while (start < buffer.getNumSamples()) {
        const int len = std::min(1 + static_cast<int>((rng.next() * 0.5f + 0.5f) * static_cast<float>(maxBlock - 1)),
                                 buffer.getNumSamples() - start);
        limiter.process(whole.getSubBlock(start, len));
        start += len;
    }
}

} // namespace

int main() {
    const int n = dawlab::secondsToSamples(1.0, kSampleRate);
    dawlab::NoiseGenerator rng(2024);

    std::vector<Case> cases;
    cases.push_back({"white noise +40 dB", dawlab::makeWhiteNoise(kSampleRate, 1.0, 100.0f)});
    cases.push_back({"50 Hz square x10", fill(n, [&](int, int i) {
                         return std::sin(dawlab::kTwoPi * 50.0 * i / kSampleRate) >= 0.0 ? 10.0f : -10.0f;
                     })});
    cases.push_back({"DC +5", fill(n, [](int, int) { return 5.0f; })});
    cases.push_back({"impulse train ±1000", fill(n, [](int, int i) {
                         return (i % 1000 == 0) ? ((i / 1000) % 2 ? -1000.0f : 1000.0f) : 0.0f;
                     })});
    cases.push_back({"alternating ±1e4", fill(n, [](int, int i) { return (i & 1) ? -1.0e4f : 1.0e4f; })});
    cases.push_back({"sine 1 kHz +60 dB", dawlab::makeSine(1000.0, kSampleRate, 1.0, 1000.0f)});
    cases.push_back({"drums +30 dB", [&] {
                         auto d = dawlab::makeDrumLoop(kSampleRate, 1);
                         d.applyGain(dawlab::dbToGain(30.0f));
                         return d;
                     }()});
    cases.push_back({"L loud / R silent", fill(n, [&](int ch, int) { return ch == 0 ? 3.0f * rng.next() : 0.0f; })});
    cases.push_back({"silence", fill(n, [](int, int) { return 0.0f; })});

    std::printf("%-22s %10s %10s %10s %10s %8s %10s %6s\n", "case", "ceiling", "lookahead", "release", "in peak",
                "out peak", "clamp#", "ok?");
    bool ok = true;
    int caseIndex = 0;
    for (auto& c : cases) {
        // 參數也隨機：ceiling -20..0 dB、lookahead 0..20 ms、release 1..500 ms
        const float ceilingDb = -20.0f * (rng.next() * 0.5f + 0.5f);
        const float lookaheadMs = 20.0f * (rng.next() * 0.5f + 0.5f);
        const float releaseMs = 1.0f + 499.0f * (rng.next() * 0.5f + 0.5f);

        dawlab::dsp::Limiter limiter;
        limiter.prepare(kSampleRate, 2048, c.signal.getNumChannels());
        limiter.setCeilingDb(ceilingDb);
        limiter.setLookaheadMs(lookaheadMs);
        limiter.setReleaseMs(releaseMs);
        limiter.setInputGainDb(caseIndex % 2 == 0 ? 0.0f : 12.0f);

        dawlab::AudioBuffer out = c.signal;
        processRandomBlocks(out, limiter, rng, 2048);

        const float ceiling = dawlab::dbToGain(ceilingDb);
        const float outPeak = dawlab::peakAbs(out);
        // safety clamp 會把任何錯誤的輸出夾回 ceiling，所以一定要同時檢查它沒有觸發
        const bool pass = outPeak <= ceiling && !dawlab::hasNanOrInf(out) && limiter.getSafetyClipCount() == 0;
        ok = ok && pass;
        std::printf("%-22s %7.2f dB %7.2f ms %7.0f ms %10.3g %10.6f %10llu %6s\n", c.name.c_str(), ceilingDb,
                    lookaheadMs, releaseMs, dawlab::peakAbs(c.signal), outPeak,
                    static_cast<unsigned long long>(limiter.getSafetyClipCount()), pass ? "yes" : "NO");
        ++caseIndex;
    }

    // ---- block 大小無關：同一段訊號用 block 512、block 1、隨機 block 處理，輸出必須逐 sample 完全相同 ----
    {
        auto run = [&](int mode) {
            dawlab::AudioBuffer d = dawlab::makeDrumLoop(kSampleRate, 1);
            dawlab::dsp::Limiter limiter;
            limiter.prepare(kSampleRate, 2048, d.getNumChannels());
            limiter.setCeilingDb(-1.0f);
            limiter.setLookaheadMs(5.0f);
            limiter.setReleaseMs(80.0f);
            limiter.setInputGainDb(18.0f); // 不 reset：讓 input gain 的 ramp 也跨越 block 邊界
            dawlab::AudioBlock whole(d);
            if (mode == 2) {
                dawlab::NoiseGenerator blockRng(7);
                processRandomBlocks(d, limiter, blockRng, 2048);
            } else {
                const int bs = (mode == 0) ? 512 : 1;
                for (int start = 0; start < d.getNumSamples(); start += bs)
                    limiter.process(whole.getSubBlock(start, std::min(bs, d.getNumSamples() - start)));
            }
            ok = ok && limiter.getSafetyClipCount() == 0;
            return d;
        };
        const dawlab::AudioBuffer a = run(0), b = run(1), c = run(2);
        float maxDiff = 0.0f;
        for (int ch = 0; ch < a.getNumChannels(); ++ch)
            for (int i = 0; i < a.getNumSamples(); ++i)
                maxDiff = std::max({maxDiff, std::abs(a.getSample(ch, i) - b.getSample(ch, i)),
                                    std::abs(a.getSample(ch, i) - c.getSample(ch, i))});
        std::printf("\nblock 大小無關：block 512 / block 1 / 隨機 block 的輸出最大差異 = %g（應為 0）\n", maxDiff);
        ok = ok && maxDiff == 0.0f;
    }

    std::printf("\n%s\n", ok ? "全部通過：每個 case 的輸出 peak 都 ≤ ceiling、safety clamp 從未觸發，且輸出與 block 大小無關。"
                              : "[FAIL] 有 case 超過 ceiling、safety clamp 有觸發，或輸出隨 block 大小改變！");
    return ok ? 0 : 1;
}
