// 練習 6 起始碼：fuzz test——隨機訊號 + 隨機參數 + 隨機 block size，驗證 ceiling 保證
//
// 目標：完成 makeRandomSignal() 與 main() 裡的 fuzz 迴圈，對 Limiter 做幾百次隨機測試，檢查：
//   (a) 輸出 sample peak ≤ ceiling（線性值，精確比較）
//   (b) 輸出沒有 NaN / Inf
//   (c) limiter.getSafetyClipCount() == 0（演算法本身沒超過，不是靠最後的 clamp）
//
// 執行：l05_ex6_starter → 完成前會印出 FAIL（預期）。

#include <dawlab/Common.h>
#include <dawlab/dsp/Limiter.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <cstdio>
#include <vector>

namespace {

class Random {
public:
    explicit Random(uint32_t seed) : gen_(seed) {}
    float uniform(float lo, float hi) { return lo + (hi - lo) * (gen_.next() * 0.5f + 0.5f); } // [lo, hi)
    int integer(int lo, int hi) { return lo + static_cast<int>(uniform(0.0f, static_cast<float>(hi - lo + 1))); } // [lo, hi]
    float next() { return gen_.next(); }

private:
    dawlab::NoiseGenerator gen_;
};

enum class SignalKind { Noise, Sine, Bursts, Impulses, Dc, Square, Mixed, Count };

// TODO 1：產生一段隨機訊號——長度 1 個 sample ～ 0.3 秒、振幅 -40..+60 dB、頻率 20 Hz..0.45 fs，
//         依 kind 產生 noise / sine / 開關的 bursts / 正負交替的 impulses / DC / 方波 / 混合。
dawlab::AudioBuffer makeRandomSignal(Random& rng, double sampleRate, int numChannels, SignalKind kind) {
    (void)kind;
    const int n = rng.integer(1, dawlab::secondsToSamples(0.3, sampleRate));
    dawlab::AudioBuffer b(numChannels, n);
    return b; // 目前是全 0：limiter 當然不會超過 ceiling，但也什麼都沒測到
}

// 用隨機長度（1..maxBlock）的 block 餵 limiter（已完成）
void processRandomBlocks(dawlab::AudioBuffer& buffer, dawlab::dsp::Limiter& limiter, Random& rng, int maxBlock) {
    dawlab::AudioBlock whole(buffer);
    int start = 0;
    while (start < buffer.getNumSamples()) {
        const int len = std::min(rng.integer(1, maxBlock), buffer.getNumSamples() - start);
        limiter.process(whole.getSubBlock(start, len));
        start += len;
    }
}

} // namespace

int main() {
    Random rng(20240521);
    int cases = 0, failures = 0;
    float worstMarginDb = 1.0e9f; // ceiling − 輸出 peak 的最小值（dB）

    const int kCases = 400;
    for (int c = 0; c < kCases; ++c) {
        // TODO 2：隨機參數——sample rate（44.1k / 48k / 96k）、channel 數 1..2、
        //         maxLookaheadMs 0..30、lookaheadMs（故意可以超出範圍）、ceiling -30..0 dB、
        //         release 0..1000 ms、input gain -10..+40 dB、maxBlock 1..1024、隨機 SignalKind
        const double sampleRate = 48000.0;
        const int numChannels = 2;
        const float ceilingDb = -1.0f;
        const float lookaheadMs = 5.0f;
        const float maxLookaheadMs = 20.0f;
        const auto kind = SignalKind::Noise;

        dawlab::dsp::Limiter limiter;
        limiter.setMaxLookaheadMs(maxLookaheadMs);
        limiter.prepare(sampleRate, 1024, numChannels);
        limiter.setCeilingDb(ceilingDb);
        limiter.setLookaheadMs(lookaheadMs);
        // TODO 3：setReleaseMs、setInputGainDb；一半的 case 呼叫 reset() 讓 input gain 直接就位

        dawlab::AudioBuffer signal = makeRandomSignal(rng, sampleRate, numChannels, kind);
        processRandomBlocks(signal, limiter, rng, 1024);

        // TODO 4：檢查 (a)(b)(c)，失敗時印出 case 的參數；更新 cases / failures / worstMarginDb
        //         並用 DAWLAB_CHECK 驗證 getLatencySamples() == clamp(msToSamples(lookaheadMs), 0, msToSamples(maxLookaheadMs))
        ++cases;
    }
    std::printf("固定參數：%d cases，%d 失敗；最小 margin = %.4f dB\n", cases, failures, worstMarginDb);
    DAWLAB_CHECK(failures == 0);
    DAWLAB_CHECK(worstMarginDb < 0.01f); // 至少要有 case 真的頂到 ceiling，否則測試沒測到東西

    // TODO 5（進階）：
    //   - 在 block 之間隨機改 setReleaseMs / setInputGainDb，保證應該仍然成立
    //   - 播放中把 ceiling 往下調：輸出仍 ≤ 新 ceiling，但 getSafetyClipCount() 會 > 0（為什麼？）
    //   - 在輸入裡塞 NaN / Inf：輸出必須仍然有限且 ≤ ceiling
    //   - 每個 block 都呼叫一次 setInputGainDb(同一個值)（模擬 plugin host），確認約 20 ms 後輸出達到目標增益

    return dawlab::test::summary("l05_ex6");
}
