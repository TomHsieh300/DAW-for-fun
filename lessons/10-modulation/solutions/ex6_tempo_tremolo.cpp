// 練習 6 解答：Tempo-synced tremolo —— rate 跟著 bpm 走，相位跟著 host 的歌曲位置走
//
// 在 DAW 裡，「八分音符的 tremolo」不只要 rate 對（120 bpm → 4 Hz），還要「對齊拍子」：
// 從第 37 小節開始播放時，音量的最大值也必須落在拍點上，而不是從按下 play 的那一刻才開始算。
// 所以 tempo sync 有兩件事：
//
//   1. rate：一個 LFO 週期 = beats 拍，一拍 = 60/bpm 秒
//        rate = bpm / 60 / beats                              （Lfo::tempoToRateHz）
//   2. phase：host 每個 block 給我們「block 第一個 sample 的歌曲位置」ppq（以四分音符計的拍數），
//        phase = frac(ppq / beats) + phaseOffset
//      每個 block 開頭都用這個公式重新對齊（而不是讓 LFO 自己一直累加），
//      這樣使用者跳到任何位置、或 tempo 改變後，相位都會立刻正確。
//
// self-check：
//   1. 120 bpm、八分音符（beats 0.5）→ rate 4 Hz；附點八分（0.75）→ 2.667 Hz；90 bpm 四分音符 → 1.5 Hz
//   2. 輸入 DC 1.0（輸出 = gain 曲線）、sine、depth 1、phaseOffset 0.25：
//      每個八分音符拍點（0.25 s 的整數倍）gain ≈ 1，兩拍點中間 gain ≈ 0；2 秒內剛好 8 個最小值
//   3. 從歌曲中間（ppq = 3.3 拍）開始播放，和從頭播放到同一位置的 gain 完全一致（最大差 < 1e-3）
//   4. tempo 從 120 改成 90 bpm 後，最小值的間距變成 60/90 × 0.5 = 0.3333 s
// 輸出：dawlab_output/l10_ex6_tempo_tremolo.wav（撥弦琶音 × 120 bpm 十六分音符 square tremolo）

#include <dawlab/Common.h>
#include <dawlab/dsp/Lfo.h>
#include <dawlab/dsp/Tremolo.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using dawlab::dsp::Lfo;
using dawlab::dsp::LfoShape;
using dawlab::dsp::Tremolo;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

class TempoSyncTremolo {
public:
    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        sampleRate_ = sampleRate;
        tremolo_.prepare(sampleRate, maxBlockSize, numChannels);
        reset();
    }

    void reset() noexcept { tremolo_.reset(); }

    void setTempoBpm(double bpm) noexcept { bpm_ = std::max(1.0, bpm); }
    // 一個 LFO 週期有幾拍：1 = 四分音符、0.5 = 八分、0.25 = 十六分、0.75 = 附點八分
    void setDivisionBeats(double beats) noexcept { beats_ = std::max(1.0 / 64.0, beats); }
    // 拍點落在 LFO 週期的哪裡。sine 時 0.25 → 拍點上 sin = +1 → gain 最大
    void setPhaseOffset(float offset01) noexcept { phaseOffset_ = offset01; }
    void setDepth(float depth) noexcept { tremolo_.setDepth(depth); }
    void setShape(LfoShape shape) noexcept { tremolo_.setShape(shape); }

    float getRateHz() const noexcept { return Lfo::tempoToRateHz(bpm_, beats_); }

    // ppqAtBlockStart：block 第一個 sample 的歌曲位置（四分音符拍數），由 host 提供
    void process(dawlab::AudioBlock block, double ppqAtBlockStart) noexcept {
        tremolo_.setRateHz(getRateHz());
        const double cycles = ppqAtBlockStart / beats_;          // 從歌曲開頭到現在經過幾個 LFO 週期
        const double phase = cycles - std::floor(cycles) + phaseOffset_;
        tremolo_.setLfoPhase(static_cast<float>(phase - std::floor(phase)));
        tremolo_.process(block);
    }

    // 一個 block 的長度是幾拍：n / fs 秒 × bpm / 60
    double blockLengthInBeats(int numSamples) const noexcept { return numSamples / sampleRate_ * bpm_ / 60.0; }

private:
    double sampleRate_ = 48000.0;
    double bpm_ = 120.0;
    double beats_ = 0.5;
    float phaseOffset_ = 0.25f;
    Tremolo tremolo_;
};

// 模擬 host：一塊一塊處理，並且每塊告訴 tremolo 目前的 ppq。
// tempoChangeAtSample ≥ 0 時，在那個 sample 之後的 block 改用 bpm2（為了簡化，只在 block 邊界改）
void runHost(TempoSyncTremolo& t, dawlab::AudioBuffer& buffer, double startPpq, double bpm1,
             int tempoChangeAtSample = -1, double bpm2 = 120.0) {
    dawlab::AudioBlock whole(buffer);
    double ppq = startPpq;
    for (int start = 0; start < buffer.getNumSamples(); start += kBlockSize) {
        const int n = std::min(kBlockSize, buffer.getNumSamples() - start);
        t.setTempoBpm((tempoChangeAtSample >= 0 && start >= tempoChangeAtSample) ? bpm2 : bpm1);
        t.process(whole.getSubBlock(start, n), ppq);
        ppq += t.blockLengthInBeats(n);
    }
}

dawlab::AudioBuffer makeDc(double seconds) {
    dawlab::AudioBuffer b(1, dawlab::secondsToSamples(seconds, kSampleRate));
    for (int i = 0; i < b.getNumSamples(); ++i)
        b.setSample(0, i, 1.0f);
    return b;
}

// 找 gain 曲線的谷底：每一段連續「gain < 0.05」的區間算一個谷，取區間中點當位置。
// （不用「比左右鄰居小」來判斷：谷底附近 float 的量化會造成好幾個假的局部最小值）
std::vector<int> findMinima(const float* g, int n, int from, int to) {
    std::vector<int> idx;
    int regionStart = -1;
    for (int i = std::max(0, from); i < std::min(n, to); ++i) {
        const bool low = g[i] < 0.05f;
        if (low && regionStart < 0)
            regionStart = i;
        if (!low && regionStart >= 0) {
            if (regionStart > from) // 從區間一開始就在谷裡的那段不完整，不算
                idx.push_back((regionStart + i - 1) / 2);
            regionStart = -1;
        }
    }
    return idx;
}

TempoSyncTremolo makeTremolo(double beats, LfoShape shape = LfoShape::Sine) {
    TempoSyncTremolo t;
    t.prepare(kSampleRate, kBlockSize, 2);
    t.setDivisionBeats(beats);
    t.setDepth(1.0f);
    t.setShape(shape);
    t.setPhaseOffset(0.25f);
    t.reset();
    return t;
}

} // namespace

int main() {
    std::printf("=== 第 10 章 練習 6：Tempo-synced tremolo ===\n\n");

    // ---- 1. rate 換算 ---------------------------------------------------------
    {
        // 用 TempoSyncTremolo::getRateHz()（TODO 1）本身驗證，而不是直接呼叫 Lfo::tempoToRateHz
        auto rateOf = [](double bpm, double beats) {
            TempoSyncTremolo t;
            t.prepare(kSampleRate, kBlockSize, 2);
            t.setTempoBpm(bpm);
            t.setDivisionBeats(beats);
            return static_cast<double>(t.getRateHz());
        };
        const double r8 = rateOf(120.0, 0.5);
        const double r8d = rateOf(120.0, 0.75);
        const double r4at90 = rateOf(90.0, 1.0);
        std::printf("rate 換算：120 bpm 八分 %.4f Hz、附點八分 %.4f Hz、90 bpm 四分 %.4f Hz\n\n", r8, r8d, r4at90);
        DAWLAB_CHECK_NEAR(r8, 4.0, 1e-4);
        DAWLAB_CHECK_NEAR(r8d, 8.0 / 3.0, 1e-4);
        DAWLAB_CHECK_NEAR(r4at90, 1.5, 1e-4);
    }

    // ---- 2. 拍點對齊 -------------------------------------------------------------
    {
        dawlab::AudioBuffer g = makeDc(2.0);
        TempoSyncTremolo t = makeTremolo(0.5);
        runHost(t, g, 0.0, 120.0);
        const float* d = g.getReadPointer(0);
        const int eighth = dawlab::secondsToSamples(0.25, kSampleRate); // 12000 samples
        std::printf("120 bpm 八分音符、sine、depth 1（輸入 DC 1.0 → 輸出 = gain）：\n");
        std::printf("   拍點   時間(s)   拍點上的 gain   兩拍中間的 gain\n");
        float worstOnBeat = 1.0f, worstMid = 0.0f;
        for (int k = 0; k < 8; ++k) {
            const float onBeat = d[k * eighth];
            const float mid = d[k * eighth + eighth / 2];
            worstOnBeat = std::min(worstOnBeat, onBeat);
            worstMid = std::max(worstMid, mid);
            std::printf("   %4d   %6.3f      %8.5f        %8.5f\n", k, k * 0.25, onBeat, mid);
        }
        const std::vector<int> minima = findMinima(d, g.getNumSamples(), 0, g.getNumSamples());
        std::printf("  -> 2 秒內找到 %zu 個最小值（理論 8）\n\n", minima.size());
        DAWLAB_CHECK(worstOnBeat > 0.999f);
        DAWLAB_CHECK(worstMid < 0.001f);
        DAWLAB_CHECK(minima.size() == 8);
    }

    // ---- 3. 從歌曲中間開始播放 ---------------------------------------------------
    {
        const double startBeat = 3.3;                       // 120 bpm 下 = 1.65 秒
        const int offset = dawlab::secondsToSamples(startBeat * 0.5, kSampleRate);
        dawlab::AudioBuffer full = makeDc(4.0);
        TempoSyncTremolo a = makeTremolo(0.5);
        runHost(a, full, 0.0, 120.0);

        dawlab::AudioBuffer late = makeDc(4.0 - startBeat * 0.5);
        TempoSyncTremolo b = makeTremolo(0.5);
        runHost(b, late, startBeat, 120.0);

        float maxDiff = 0.0f;
        for (int i = 0; i < late.getNumSamples(); ++i)
            maxDiff = std::max(maxDiff, std::abs(late.getSample(0, i) - full.getSample(0, offset + i)));
        std::printf("從 ppq = %.1f 開始播放 vs 從頭播放：同一歌曲位置的 gain 最大差 = %.2e\n\n", startBeat, maxDiff);
        DAWLAB_CHECK(maxDiff < 1.0e-3f);
    }

    // ---- 4. tempo 改變 -------------------------------------------------------------
    {
        dawlab::AudioBuffer g = makeDc(4.0);
        TempoSyncTremolo t = makeTremolo(0.5);
        const int change = dawlab::secondsToSamples(2.0, kSampleRate);
        runHost(t, g, 0.0, 120.0, change, 90.0);
        const float* d = g.getReadPointer(0);
        const std::vector<int> after = findMinima(d, g.getNumSamples(), change + kBlockSize, g.getNumSamples());
        double meanGap = 0.0;
        for (size_t i = 1; i < after.size(); ++i)
            meanGap += (after[i] - after[i - 1]) / kSampleRate;
        meanGap = after.size() > 1 ? meanGap / static_cast<double>(after.size() - 1) : 0.0;
        std::printf("tempo 120 → 90 bpm 之後：最小值平均間距 %.4f s（理論 60/90 × 0.5 = %.4f s）\n\n", meanGap,
                    60.0 / 90.0 * 0.5);
        DAWLAB_CHECK(after.size() >= 4);
        DAWLAB_CHECK_NEAR(meanGap, 60.0 / 90.0 * 0.5, 0.002);
    }

    // ---- 聽 ----------------------------------------------------------------------
    {
        dawlab::AudioBuffer pluck = dawlab::makePluckSequence(kSampleRate, 4.0, 0.4f, 2);
        TempoSyncTremolo t = makeTremolo(0.25, LfoShape::Square); // 十六分音符的「切音」
        t.setPhaseOffset(0.0f); // square 在相位 0..0.5 為 +1：拍點上是「開」
        runHost(t, pluck, 0.0, 120.0);
        dawlab::writeWav(dawlab::outputPath("l10_ex6_tempo_tremolo.wav"), pluck, kSampleRate);
        std::printf("輸出 l10_ex6_tempo_tremolo.wav（120 bpm、十六分音符、square、depth 1）：peak %.3f\n",
                    dawlab::peakAbs(pluck));
        DAWLAB_CHECK(!dawlab::hasNanOrInf(pluck));
    }

    return dawlab::test::summary("l10_ex6");
}
