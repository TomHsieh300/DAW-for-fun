// 練習 5 解答：Loudness Range（LRA，EBU Tech 3342）
//
// LRA 回答的問題是「這首歌大聲的段落和小聲的段落差多少 LU」——也就是 macro dynamics（宏觀動態），
// 跟 crest factor（峰值 / RMS，micro dynamics）是兩回事。演算法（EBU Tech 3342）：
//
//   1. 以 short-term loudness（3 s 視窗，K-weighting）為原料，每隔一小段時間取一個值
//      （我們每 100 ms 取一個，跟 LoudnessMeter 的子區塊同步；規範要求 overlap 至少 66%）。
//      只有「完整的 3 s 視窗」才算數。
//   2. absolute gate：丟掉 < -70 LUFS 的值。
//   3. relative gate：算剩下的值的「功率平均」再換回 LUFS，丟掉低於它 20 LU 的值
//      （注意：integrated loudness 用 -10 LU，LRA 用 -20 LU）。
//   4. LRA = 剩下的值的第 95 百分位數 − 第 10 百分位數（單位 LU）。
//      用 10% / 95% 而不是 min / max：避免一聲很短的爆炸聲或一段淡出把 LRA 撐大。
//
// 和 LoudnessMeter 一樣，我們不在 audio thread 上存一條越來越長的 vector，而是用 0.1 LU 一格的直方圖：
// 百分位數直接從累積次數找，誤差最多 0.1 LU，完全不 allocate。
//
// 限制：每次 process() 的 block 長度不可超過 100 ms（4800 samples @ 48 kHz），否則一次 process 會
// 跨過兩個子區塊、我們只看得到最後一個 short-term 值。實際的 audio callback 遠小於這個長度。
//
// 驗證（EBU Tech 3342 的測試訊號）：1 kHz 正弦，前 20 s 在位準 A、後 20 s 在位準 B，
//   -20 / -30 dBFS → LRA = 10 ± 1 LU；-20 / -15 → 5 ± 1；-40 / -20 → 20 ± 1；固定位準 → 0。
//
// 執行：bin/Debug/l11_ex5_solution.exe（ctest label: solution）

#include <dawlab/Common.h>
#include <dawlab/dsp/LoudnessMeter.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace dawlab;
using dawlab::dsp::LoudnessMeter;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

class LoudnessRangeMeter {
public:
    static constexpr float kAbsoluteGateLufs = -70.0f;
    static constexpr float kRelativeGateLu = -20.0f;
    static constexpr float kLowPercentile = 0.10f;
    static constexpr float kHighPercentile = 0.95f;

    void prepare(double sampleRate, int numChannels) {
        meter_.prepare(sampleRate, numChannels);
        histogramCount_.assign(kBins, 0u);
        reset();
    }

    void reset() noexcept {
        meter_.reset();
        std::fill(histogramCount_.begin(), histogramCount_.end(), 0u);
        lastUpdate_ = 0;
        lra_.store(0.0f, std::memory_order_relaxed);
    }

    // audio thread（block 長度 <= 100 ms）
    void process(const AudioBlock& block) noexcept {
        meter_.process(block);
        const uint32_t updates = meter_.getUpdateCount();
        if (updates == lastUpdate_)
            return; // 這個 block 沒有完成新的 100 ms 子區塊
        lastUpdate_ = updates;
        if (updates < static_cast<uint32_t>(kFullShortTermSteps))
            return; // 前 3 s：short-term 視窗還沒填滿，不算
        const float st = meter_.getShortTermLufs();
        if (st <= kAbsoluteGateLufs)
            return; // absolute gate
        const int bin = std::clamp(static_cast<int>(std::floor((st - kMinLufs) / kStepLu)), 0, kBins - 1);
        ++histogramCount_[static_cast<size_t>(bin)];
        lra_.store(computeLra(), std::memory_order_relaxed);
    }

    float getLoudnessRangeLu() const noexcept { return lra_.load(std::memory_order_relaxed); }
    float getIntegratedLufs() const noexcept { return meter_.getIntegratedLufs(); }

    // 除錯 / 教學用：最近一次計算的 10% 與 95% 位置
    float getLowLufs() const noexcept { return lowLufs_; }
    float getHighLufs() const noexcept { return highLufs_; }

private:
    static constexpr int kFullShortTermSteps = 30; // 3 s / 100 ms
    static constexpr float kMinLufs = -70.0f;
    static constexpr float kStepLu = 0.1f;
    static constexpr int kBins = 800; // [-70, +10) LUFS

    static float binCentre(int b) noexcept { return kMinLufs + (static_cast<float>(b) + 0.5f) * kStepLu; }

    float computeLra() noexcept {
        // relative gate：功率平均（不是 dB 平均！）
        double sumPower = 0.0;
        uint64_t count = 0;
        for (int b = 0; b < kBins; ++b) {
            const uint32_t c = histogramCount_[static_cast<size_t>(b)];
            if (c == 0)
                continue;
            sumPower += c * LoudnessMeter::lufsToPower(binCentre(b));
            count += c;
        }
        if (count == 0)
            return 0.0f;
        const float gate = LoudnessMeter::powerToLufs(sumPower / static_cast<double>(count)) + kRelativeGateLu;

        // 通過 relative gate 的總數
        const int firstBin = std::clamp(static_cast<int>(std::ceil((gate - kMinLufs) / kStepLu - 0.5f)), 0, kBins);
        uint64_t gated = 0;
        for (int b = firstBin; b < kBins; ++b)
            gated += histogramCount_[static_cast<size_t>(b)];
        if (gated == 0)
            return 0.0f;

        // 百分位數：累積次數第一次 >= p·total 的那一格
        auto percentile = [&](float p) {
            const double target = std::max(1.0, std::ceil(p * static_cast<double>(gated)));
            uint64_t acc = 0;
            for (int b = firstBin; b < kBins; ++b) {
                acc += histogramCount_[static_cast<size_t>(b)];
                if (static_cast<double>(acc) >= target)
                    return binCentre(b);
            }
            return binCentre(kBins - 1);
        };
        lowLufs_ = percentile(kLowPercentile);
        highLufs_ = percentile(kHighPercentile);
        return highLufs_ - lowLufs_;
    }

    LoudnessMeter meter_;
    std::vector<uint32_t> histogramCount_;
    uint32_t lastUpdate_ = 0;
    float lowLufs_ = 0.0f, highLufs_ = 0.0f;
    std::atomic<float> lra_{0.0f};
};

float measureLra(LoudnessRangeMeter& meter, AudioBuffer& audio, float* integrated = nullptr) {
    meter.reset();
    for (int start = 0; start < audio.getNumSamples(); start += kBlockSize) {
        const int len = std::min(kBlockSize, audio.getNumSamples() - start);
        meter.process(AudioBlock(audio).getSubBlock(start, len));
    }
    if (integrated)
        *integrated = meter.getIntegratedLufs();
    return meter.getLoudnessRangeLu();
}

} // namespace

int main() {
    LoudnessRangeMeter meter;
    meter.prepare(kSampleRate, 2);

    // ---- EBU Tech 3342 風格的測試：兩段 20 s 的立體聲 1 kHz 正弦 ------------------------------------
    struct Case {
        float levelA, levelB, expectedLra;
    };
    const Case cases[] = {{-20.0f, -30.0f, 10.0f}, {-20.0f, -15.0f, 5.0f}, {-40.0f, -20.0f, 20.0f}};
    std::printf("%10s %10s %10s %12s %12s %12s\n", "A (dBFS)", "B (dBFS)", "LRA (LU)", "10% (LUFS)", "95% (LUFS)",
                "integrated");
    for (const Case& c : cases) {
        AudioBuffer audio = makeToneBursts(1000.0, kSampleRate, {c.levelA, c.levelB}, 20.0, 2);
        float integrated = 0.0f;
        const float lra = measureLra(meter, audio, &integrated);
        std::printf("%10.1f %10.1f %10.2f %12.2f %12.2f %12.2f\n", c.levelA, c.levelB, lra, meter.getLowLufs(),
                    meter.getHighLufs(), integrated);
        DAWLAB_CHECK_NEAR(lra, c.expectedLra, 1.0);
    }

    // ---- 固定位準 → LRA ≈ 0 -----------------------------------------------------------------------
    {
        AudioBuffer steady = makeSine(1000.0, kSampleRate, 10.0, dbToGain(-20.0f), 2);
        const float lra = measureLra(meter, steady);
        std::printf("\n固定 -20 dBFS 的正弦 10 s → LRA = %.2f LU（期望 0）\n", lra);
        DAWLAB_CHECK_NEAR(lra, 0.0, 0.2);
    }

    // ---- 中間夾一段很長的靜音：absolute gate 讓靜音不影響 LRA -----------------------------------------
    {
        AudioBuffer withSilence = makeToneBursts(1000.0, kSampleRate, {-20.0f, -200.0f, -30.0f}, 20.0, 2);
        const float lra = measureLra(meter, withSilence);
        std::printf("-20 dBFS 20 s → 靜音 20 s → -30 dBFS 20 s：LRA = %.2f LU（靜音被 absolute gate 擋掉，仍約 10）\n",
                    lra);
        DAWLAB_CHECK_NEAR(lra, 10.0, 1.0);
    }

    // ---- 很小聲的一段（比主體低 30 LU）：relative gate 把它排除 ----------------------------------------
    {
        AudioBuffer quietTail = makeToneBursts(1000.0, kSampleRate, {-20.0f, -20.0f, -50.0f}, 10.0, 2);
        const float lra = measureLra(meter, quietTail);
        std::printf("-20 dBFS 20 s → -50 dBFS 10 s：LRA = %.2f LU（-50 那段低於 relative gate 20 LU，被排除；"
                    "只剩過渡區的少數值）\n",
                    lra);
        DAWLAB_CHECK(lra < 5.0f);
    }

    return dawlab::test::summary("l11_ex5");
}
