// 練習 6 解答：fuzz test——隨機訊號 + 隨機參數 + 隨機 block size，驗證 ceiling 保證
//
// 「證明」告訴我們演算法在數學上不會超過 ceiling，但程式碼可能有 off-by-one、浮點捨入、
// 環形緩衝繞回、block 邊界等問題。Fuzz test 的精神是：用大量隨機（但可重現）的輸入去轟它，
// 任何一次違反承諾都算 bug。
//
// 我們檢查三件事：
//   (a) 輸出 sample peak ≤ ceiling（線性值，精確比較、不留餘地）
//   (b) 輸出沒有 NaN / Inf
//   (c) getSafetyClipCount() == 0：演算法本身就沒超過，不是靠最後一道 clamp 救回來的
//
// 執行：l05_ex6_solution（約 400 個隨機 case，Debug 建置幾秒內跑完）
// 輸出：無 WAV（純數字檢查）

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

const char* kindName(SignalKind k) {
    switch (k) {
    case SignalKind::Noise: return "noise";
    case SignalKind::Sine: return "sine";
    case SignalKind::Bursts: return "bursts";
    case SignalKind::Impulses: return "impulses";
    case SignalKind::Dc: return "dc";
    case SignalKind::Square: return "square";
    case SignalKind::Mixed: return "mixed";
    default: return "?";
    }
}

// 產生一段隨機訊號：種類、長度、振幅（最大可到 +60 dB）都隨機
dawlab::AudioBuffer makeRandomSignal(Random& rng, double sampleRate, int numChannels, SignalKind kind) {
    const int n = rng.integer(1, dawlab::secondsToSamples(0.3, sampleRate));
    const float amp = dawlab::dbToGain(rng.uniform(-40.0f, 60.0f));
    const double freq = static_cast<double>(rng.uniform(20.0f, static_cast<float>(sampleRate * 0.45)));
    dawlab::AudioBuffer b(numChannels, n);
    for (int ch = 0; ch < numChannels; ++ch) {
        float* d = b.getWritePointer(ch);
        const int burstLen = std::max(1, rng.integer(1, 2000));
        const int impulseEvery = rng.integer(1, 500);
        for (int i = 0; i < n; ++i) {
            const double phase = dawlab::kTwoPi * freq * i / sampleRate;
            float v = 0.0f;
            switch (kind) {
            case SignalKind::Noise: v = amp * rng.next(); break;
            case SignalKind::Sine: v = amp * static_cast<float>(std::sin(phase)); break;
            case SignalKind::Bursts: v = ((i / burstLen) % 2 == 0) ? amp * static_cast<float>(std::sin(phase)) : 0.0f; break;
            case SignalKind::Impulses: v = (i % impulseEvery == 0) ? ((i / impulseEvery) % 2 ? -amp : amp) : 0.0f; break;
            case SignalKind::Dc: v = amp; break;
            case SignalKind::Square: v = std::sin(phase) >= 0.0 ? amp : -amp; break;
            case SignalKind::Mixed:
                v = amp * (0.5f * static_cast<float>(std::sin(phase)) + 0.5f * rng.next()) +
                    ((i % impulseEvery == 0) ? 10.0f * amp : 0.0f);
                break;
            default: break;
            }
            d[i] = v;
        }
    }
    return b;
}

// 用隨機長度（1..maxBlock）的 block 餵 limiter
void processRandomBlocks(dawlab::AudioBuffer& buffer, dawlab::dsp::Limiter& limiter, Random& rng, int maxBlock) {
    dawlab::AudioBlock whole(buffer);
    int start = 0;
    while (start < buffer.getNumSamples()) {
        const int len = std::min(rng.integer(1, maxBlock), buffer.getNumSamples() - start);
        limiter.process(whole.getSubBlock(start, len));
        start += len;
    }
}

struct Stats {
    int cases = 0;
    int failures = 0;
    float worstMarginDb = 1.0e9f; // ceiling - 輸出 peak 的最小值（dB）；越接近 0 表示 limiter 真的頂到 ceiling
};

void report(Stats& s, const char* label, bool pass, float ceilingDb, float outPeakDb) {
    ++s.cases;
    s.worstMarginDb = std::min(s.worstMarginDb, ceilingDb - outPeakDb);
    if (!pass) {
        ++s.failures;
        std::printf("  [FAIL] %s：ceiling %.3f dB，輸出 peak %.3f dB\n", label, ceilingDb, outPeakDb);
    }
}

} // namespace

int main() {
    Random rng(20240521);
    Stats stats;

    // ---- (1) 主要 fuzz：每個 case 的參數在處理前固定 ----
    const int kCases = 400;
    for (int c = 0; c < kCases; ++c) {
        const double sampleRate = (c % 3 == 0) ? 44100.0 : (c % 3 == 1) ? 48000.0 : 96000.0;
        const int numChannels = rng.integer(1, 2);
        const float maxLookaheadMs = rng.uniform(0.0f, 30.0f);
        const float lookaheadMs = rng.uniform(-5.0f, maxLookaheadMs + 5.0f); // 故意超出範圍，看 clamp
        const float ceilingDb = rng.uniform(-30.0f, 0.0f);
        const float releaseMs = rng.uniform(0.0f, 1000.0f);
        const float inputGainDb = rng.uniform(-10.0f, 40.0f);
        const int maxBlock = rng.integer(1, 1024);
        const auto kind = static_cast<SignalKind>(rng.integer(0, static_cast<int>(SignalKind::Count) - 1));

        dawlab::dsp::Limiter limiter;
        limiter.setMaxLookaheadMs(maxLookaheadMs);
        limiter.prepare(sampleRate, 1024, numChannels);
        limiter.setCeilingDb(ceilingDb);
        limiter.setLookaheadMs(lookaheadMs);
        limiter.setReleaseMs(releaseMs);
        limiter.setInputGainDb(inputGainDb);
        if (c % 2 == 0)
            limiter.reset(); // 一半的 case 讓 input gain 直接就位，一半讓它 ramp

        dawlab::AudioBuffer signal = makeRandomSignal(rng, sampleRate, numChannels, kind);
        processRandomBlocks(signal, limiter, rng, maxBlock);

        const float ceiling = dawlab::dbToGain(ceilingDb);
        const float outPeak = dawlab::peakAbs(signal);
        const bool pass = outPeak <= ceiling && !dawlab::hasNanOrInf(signal) && limiter.getSafetyClipCount() == 0;
        char label[160];
        std::snprintf(label, sizeof(label), "case %3d %-8s sr=%.0f ch=%d la=%.2f/%.2f ms rel=%.0f gain=%+.1f blk=%d", c,
                      kindName(kind), sampleRate, numChannels, lookaheadMs, maxLookaheadMs, releaseMs, inputGainDb, maxBlock);
        report(stats, label, pass, ceilingDb, dawlab::gainToDb(outPeak));

        // lookahead 的 clamp 與 latency 回報要一致
        const int expected = std::clamp(dawlab::msToSamples(lookaheadMs, sampleRate), 0,
                                        dawlab::msToSamples(maxLookaheadMs, sampleRate));
        DAWLAB_CHECK(limiter.getLatencySamples() == expected);
    }
    std::printf("(1) 固定參數：%d cases，%d 失敗；最小 margin（ceiling − 輸出 peak）= %.4f dB\n", stats.cases, stats.failures,
                stats.worstMarginDb);
    DAWLAB_CHECK(stats.failures == 0);
    DAWLAB_CHECK(stats.worstMarginDb < 0.01f); // 至少有 case 真的頂到 ceiling（否則測試沒測到東西）

    // ---- (2) 播放中改 release / input gain：證明仍然成立（這兩個參數不影響推導）----
    {
        Stats s2;
        for (int c = 0; c < 50; ++c) {
            const double sampleRate = 48000.0;
            const float ceilingDb = rng.uniform(-20.0f, 0.0f);
            dawlab::dsp::Limiter limiter;
            limiter.prepare(sampleRate, 512, 2);
            limiter.setCeilingDb(ceilingDb);
            limiter.setLookaheadMs(rng.uniform(0.0f, 20.0f));

            dawlab::AudioBuffer signal = makeRandomSignal(rng, sampleRate, 2, SignalKind::Mixed);
            dawlab::AudioBlock whole(signal);
            int start = 0;
            while (start < signal.getNumSamples()) {
                limiter.setReleaseMs(rng.uniform(0.0f, 500.0f));
                limiter.setInputGainDb(rng.uniform(-20.0f, 30.0f));
                const int len = std::min(rng.integer(1, 512), signal.getNumSamples() - start);
                limiter.process(whole.getSubBlock(start, len));
                start += len;
            }
            const bool pass = dawlab::peakAbs(signal) <= dawlab::dbToGain(ceilingDb) && !dawlab::hasNanOrInf(signal) &&
                              limiter.getSafetyClipCount() == 0;
            report(s2, "release/gain 隨 block 變化", pass, ceilingDb, dawlab::gainToDb(dawlab::peakAbs(signal)));
        }
        std::printf("(2) 播放中改 release / input gain：%d cases，%d 失敗\n", s2.cases, s2.failures);
        DAWLAB_CHECK(s2.failures == 0);
    }

    // ---- (3) 播放中把 ceiling 往下調：pipeline 裡已經算好 gain 的 sample 會超過新 ceiling，
    //          這時 safety clamp 會接手（clip count > 0），輸出仍然 ≤ 當下的 ceiling ----
    {
        const double sampleRate = 48000.0;
        dawlab::dsp::Limiter limiter;
        limiter.prepare(sampleRate, 512, 2);
        limiter.setCeilingDb(-1.0f);
        limiter.setLookaheadMs(10.0f);
        dawlab::AudioBuffer signal = dawlab::makeWhiteNoise(sampleRate, 0.5, 4.0f);
        dawlab::AudioBlock whole(signal);
        const int half = signal.getNumSamples() / 2;
        limiter.process(whole.getSubBlock(0, half));
        limiter.setCeilingDb(-12.0f);
        limiter.process(whole.getSubBlock(half, signal.getNumSamples() - half));
        const float secondHalfPeak = dawlab::peakAbs(signal.getReadPointer(0) + half, signal.getNumSamples() - half);
        std::printf("(3) ceiling -1 → -12 dB 中途切換：後半段 peak %.2f dBFS，safety clamp 觸發 %llu 次（預期 > 0）\n",
                    dawlab::gainToDb(secondHalfPeak), static_cast<unsigned long long>(limiter.getSafetyClipCount()));
        DAWLAB_CHECK(secondHalfPeak <= dawlab::dbToGain(-12.0f));
        DAWLAB_CHECK(limiter.getSafetyClipCount() > 0);
        DAWLAB_CHECK(!dawlab::hasNanOrInf(signal));
    }

    // ---- (4) 惡意輸入：NaN / Inf 混進來，輸出必須仍然有限且 ≤ ceiling ----
    {
        const double sampleRate = 48000.0;
        dawlab::dsp::Limiter limiter;
        limiter.prepare(sampleRate, 512, 2);
        limiter.setCeilingDb(-3.0f);
        limiter.setLookaheadMs(5.0f);
        dawlab::AudioBuffer signal = dawlab::makeWhiteNoise(sampleRate, 0.2, 0.5f);
        signal.setSample(0, 1000, std::numeric_limits<float>::quiet_NaN());
        signal.setSample(1, 2000, std::numeric_limits<float>::infinity());
        signal.setSample(0, 3000, -std::numeric_limits<float>::infinity());
        processRandomBlocks(signal, limiter, rng, 512);
        std::printf("(4) NaN / Inf 注入：輸出 peak %.2f dBFS，有 NaN/Inf？%s\n", dawlab::gainToDb(dawlab::peakAbs(signal)),
                    dawlab::hasNanOrInf(signal) ? "有" : "無");
        DAWLAB_CHECK(!dawlab::hasNanOrInf(signal));
        DAWLAB_CHECK(dawlab::peakAbs(signal) <= dawlab::dbToGain(-3.0f));
    }

    // ---- (5) host 每個 block 都重設同一個 input gain（第 13 章的 plugin 就是這樣做）：
    //          ramp 仍然要在約 20 ms 內走完，不能因為「重設」而越走越慢 ----
    {
        const double sampleRate = 48000.0;
        dawlab::dsp::Limiter limiter;
        limiter.prepare(sampleRate, 64, 1);
        limiter.setCeilingDb(0.0f);
        limiter.setLookaheadMs(0.0f);
        dawlab::AudioBuffer signal(1, dawlab::secondsToSamples(0.05, sampleRate));
        for (int i = 0; i < signal.getNumSamples(); ++i)
            signal.setSample(0, i, 0.1f); // DC 0.1，+6 dB 之後 ≈ 0.2，遠低於 ceiling
        dawlab::AudioBlock whole(signal);
        for (int start = 0; start < signal.getNumSamples(); start += 64) {
            limiter.setInputGainDb(6.0f); // 每個 block 都設一次
            limiter.process(whole.getSubBlock(start, std::min(64, signal.getNumSamples() - start)));
        }
        const int after = dawlab::secondsToSamples(0.025, sampleRate); // 20 ms ramp + 餘裕
        const float expected = 0.1f * dawlab::dbToGain(6.0f);
        std::printf("(5) 每個 block 重設 input gain +6 dB：25 ms 後輸出 %.6f（目標 %.6f）\n", signal.getSample(0, after),
                    expected);
        DAWLAB_CHECK_NEAR(signal.getSample(0, after), expected, 1.0e-6);
    }

    return dawlab::test::summary("l05_ex6");
}
