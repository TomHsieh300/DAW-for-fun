// 練習 4 起始碼：簡易 4x oversampling true-peak meter
//
// 目標：完成 TruePeakMeter 的 designFilter() 與 process()，讓它量出 inter-sample peak（dBTP）。
//
// 開始之前請先讀 README 2.8 節的「升頻 4 倍怎麼做？」小節（零插入 → image → lowpass 插值 → polyphase）。
// 那一節只講直覺；halfband、Kaiser window、latency 等完整理論在第 06 章 2.6 節，做這題不需要先讀。
//
// 四個想法串起來就是這題：
//   1. 零插入：每個 sample 後面插 3 個 0，sample rate 變 4 倍（48 kHz → 192 kHz）。
//      波形變成「原始 sample + 一堆 0」，還不是我們要的平滑曲線。
//   2. image：插 0 等於乘上「1,0,0,0,1,0,0,0…」這串脈衝，頻譜會在 fs、2fs 附近多出原訊號的複本（image），
//      而且原本那份（baseband）的振幅只剩 1/4。
//   3. lowpass 插值：用 cutoff = 原本 fs/2 的 lowpass 把 image 濾掉、增益乘 4 補回振幅，
//      0 的位置就被「補」成平滑曲線上的值。理想 lowpass 的 impulse response 是 sinc。
//   4. polyphase：插 0 之後跑 FIR，大部分乘法都是「× 0」。只保留非零項，每個輸入 sample 只算 4 個短內積。
//
// 執行：l05_ex4_starter → 完成前會印出 FAIL（預期）。先完成 TODO 1，self-check (0) 會先變成 PASS；
//       再完成 TODO 2，(1)～(4) 才會 PASS。

#include <dawlab/Common.h>
#include <dawlab/dsp/Limiter.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

constexpr int kOversample = 4;
constexpr int kTapsPerPhase = 16;
constexpr int kNumTaps = kOversample * kTapsPerPhase;

class TruePeakMeter {
public:
    void prepare(int numChannels) {
        numChannels_ = numChannels;
        designFilter();
        history_.assign(static_cast<size_t>(numChannels) * kTapsPerPhase, 0.0f);
        writeIndex_.assign(static_cast<size_t>(numChannels), 0);
        reset();
    }

    void reset() noexcept {
        std::fill(history_.begin(), history_.end(), 0.0f);
        std::fill(writeIndex_.begin(), writeIndex_.end(), 0);
        samplePeak_ = 0.0f;
        truePeak_ = 0.0f;
    }

    // 只清掉量到的 peak、保留濾波器的歷史：用在「濾波器暖機之後才開始量」（已完成）
    void resetPeaks() noexcept {
        samplePeak_ = 0.0f;
        truePeak_ = 0.0f;
    }

    void process(const dawlab::AudioBlock& block) noexcept {
        const int nch = std::min(block.getNumChannels(), numChannels_);
        for (int ch = 0; ch < nch; ++ch) {
            float* hist = history_.data() + static_cast<size_t>(ch) * kTapsPerPhase;
            int& w = writeIndex_[static_cast<size_t>(ch)];
            const float* x = block.getChannelPointer(ch);
            for (int i = 0; i < block.getNumSamples(); ++i) {
                samplePeak_ = std::max(samplePeak_, std::abs(x[i]));

                // TODO 2：polyphase 插值，每個輸入 sample 產生 4 個升頻後的 sample
                //
                //   先想清楚「為什麼只要 16 個輸入」：升頻後的 FIR 是 y[m] = Σ_k h[k]·u[m - k]，
                //   u 是零插入後的訊號，只有 m - k 是 4 的倍數時才不是 0。寫 m = 4n + p（p = 0..3），
                //   非零項只剩 k = 4j + p，而 u[4(n - j)] 就是原本的 x[n - j]，所以
                //       y[4n + p] = Σ_j h[4j + p] · x[n - j]，j = 0..kTapsPerPhase-1
                //   每個 phase p 是一個 16 tap 的子濾波器（coeffs_[p]），吃的都是最近 16 個「原始」輸入。
                //   全部 4 個 phase = 64 次乘法；若真的做出 4 倍長的零插入訊號再跑 64 tap FIR，要 256 次。
                //
                //   實作步驟：
                //   (a) 把 x[i] 存進環形緩衝 hist（長度 kTapsPerPhase，寫入位置 w）。
                //       之後 x[n - j] 在哪一格？（想想 w 往回數 j 格、小於 0 時怎麼繞回）
                //   (b) 對每個 phase p：用 coeffs_[p][j] 與 x[n - j] 做內積（建議用 double 累加），
                //       把 |內積| 與 truePeak_ 取 max。
                //   (c) w 往前推一格，到尾端繞回 0。
                //
                //   自我檢查：輸入是 DC（常數 c）時，每個 phase 的輸出應該都是 c——這正是 TODO 1 要求
                //   「每個 phase 的係數總和 ≈ 1」的原因。
                (void)hist;
                (void)w;
            }
        }
    }

    float getSamplePeak() const noexcept { return samplePeak_; }
    float getTruePeak() const noexcept { return truePeak_; }
    float getSamplePeakDb() const noexcept { return dawlab::gainToDb(samplePeak_); }
    float getTruePeakDb() const noexcept { return dawlab::gainToDb(truePeak_); }
    // coeffs[p][j] = h[4j + p]（給 self-check 檢查濾波器設計用，已完成）
    const std::array<std::array<float, kTapsPerPhase>, kOversample>& getCoefficients() const noexcept { return coeffs_; }

private:
    void designFilter() {
        // TODO 1：設計 64 tap 的 windowed-sinc lowpass 原型，再拆成 4 個 phase
        //
        //   (a) 理想 lowpass：在新的取樣率 4·fs 下，cutoff = 原本的 fs/2 = 新取樣率的 1/8。
        //       cutoff 為 fc 的理想 lowpass，impulse response 是 (2fc/fs')·sinc(2fc·n/fs')，fs' = 4·fs。
        //       代入 2fc/fs' = 1/4、再乘上補償零插入的增益 4，就得到 h[n] = sinc(n / 4)，
        //       其中 sinc(t) = sin(πt) / (πt)，t = 0 時為 1。
        //       觀察：sinc(n/4) 在 n = ±4, ±8, … 時是 0——也就是「原始 sample 的位置不被改動，只補中間的 0」。
        //   (b) 截斷與置中：只取 kNumTaps 個 tap（k = 0..kNumTaps-1），中心放在 M = (kNumTaps - 1) / 2，
        //       所以用 sinc((k - M) / 4)。注意 kNumTaps = 64 是偶數，M = 31.5 不是整數——
        //       self-check (2) 的 -6.19 dBTP 就是這個半格偏移造成的（solutions/README.md 有解釋）。
        //   (c) window：直接截斷會讓頻率響應出現漣波（第 11 章 2.9 節詳談）。乘上 Hann window
        //       w[k] = 0.5 - 0.5·cos(2πk / (kNumTaps - 1))，讓係數在兩端平滑地降到 0。
        //   (d) 正規化：window 會讓總增益偏離 4，把所有 h[k] 除以總和再乘 4，讓 Σh = 4
        //       （這樣 4 個 phase 加起來的 DC gain 剛好是 4，每個 phase 約為 1）。
        //   (e) polyphase 分解：phase p 用的是 h[p], h[p+4], h[p+8], …，也就是 coeffs_[p][j] = h[4j + p]
        //       （換個寫法：coeffs_[k % 4][k / 4] = h[k]）。為什麼是這樣分？看 TODO 2 的推導。
        //
        //   建議用 double 計算，最後再轉成 float 存進 coeffs_。可用 dawlab::kPi、dawlab::kTwoPi。
        //   完成後 self-check (0) 會檢查：Σh = 4、每個 phase 的總和 ≈ 1、h[k] = h[63 - k]（對稱 = 線性相位）。
    }

    int numChannels_ = 0;
    std::array<std::array<float, kTapsPerPhase>, kOversample> coeffs_{};
    std::vector<float> history_;
    std::vector<int> writeIndex_;
    float samplePeak_ = 0.0f;
    float truePeak_ = 0.0f;
};

// warmupSamples > 0：前面這幾個 sample 只用來填滿濾波器的歷史，不計入 peak（已完成）。
// 訊號「從 0 突然開始」時，插值濾波器會在開頭產生 onset 暫態（Gibbs ringing），量到的會是暫態而不是穩態。
void measure(const char* name, dawlab::AudioBuffer& buffer, float& samplePeakDb, float& truePeakDb,
             int warmupSamples = 0) {
    TruePeakMeter meter;
    meter.prepare(buffer.getNumChannels());
    dawlab::AudioBlock whole(buffer);
    const int warmup = std::min(warmupSamples, buffer.getNumSamples());
    if (warmup > 0) {
        meter.process(whole.getSubBlock(0, warmup));
        meter.resetPeaks();
    }
    for (int start = warmup; start < buffer.getNumSamples(); start += kBlockSize)
        meter.process(whole.getSubBlock(start, std::min(kBlockSize, buffer.getNumSamples() - start)));
    samplePeakDb = meter.getSamplePeakDb();
    truePeakDb = meter.getTruePeakDb();
    std::printf("%-34s sample peak %7.2f dBFS   true peak %7.2f dBTP   差 %+5.2f dB\n", name, samplePeakDb, truePeakDb,
                truePeakDb - samplePeakDb);
}

template <typename Processor>
void processInBlocks(dawlab::AudioBuffer& buffer, Processor& processor) {
    dawlab::AudioBlock whole(buffer);
    for (int start = 0; start < buffer.getNumSamples(); start += kBlockSize)
        processor.process(whole.getSubBlock(start, std::min(kBlockSize, buffer.getNumSamples() - start)));
}

} // namespace

int main() {
    float sp = 0.0f, tp = 0.0f;

    // (0) 先檢查濾波器本身（不用跑訊號；只需要完成 TODO 1）（已完成）：
    //     - 所有 tap 總和 = 4：零插入讓 baseband 振幅變成 1/4，lowpass 要乘 4 補回來
    //     - 每個 phase 的總和 ≈ 1：輸入是 DC（常數）時，4 個插值點都應該等於那個常數
    //     - 左右對稱 h[k] = h[63 - k]：線性相位，插值不會把波形「拉歪」
    {
        TruePeakMeter meter;
        meter.prepare(1);
        const auto& c = meter.getCoefficients();
        double total = 0.0, maxAsym = 0.0;
        double phaseSum[kOversample] = {};
        for (int k = 0; k < kNumTaps; ++k) {
            const double hk = c[static_cast<size_t>(k % kOversample)][static_cast<size_t>(k / kOversample)];
            const int mirror = kNumTaps - 1 - k;
            const double hm = c[static_cast<size_t>(mirror % kOversample)][static_cast<size_t>(mirror / kOversample)];
            total += hk;
            phaseSum[k % kOversample] += hk;
            maxAsym = std::max(maxAsym, std::abs(hk - hm));
        }
        std::printf("filter: sum(h) = %.4f, phase sums = %.4f %.4f %.4f %.4f, max |h[k] - h[63-k]| = %.1e\n",
                    total, phaseSum[0], phaseSum[1], phaseSum[2], phaseSum[3], maxAsym);
        DAWLAB_CHECK_NEAR(total, 4.0, 1.0e-4);
        for (int p = 0; p < kOversample; ++p)
            DAWLAB_CHECK_NEAR(phaseSum[p], 1.0, 1.0e-3);
        DAWLAB_CHECK(maxAsym < 1.0e-6);
    }

    // (1) 1 kHz 正弦波 -6 dBFS：兩種 peak 應該差不多
    {
        dawlab::AudioBuffer sine = dawlab::makeSine(1000.0, kSampleRate, 0.5, 0.5f, 1);
        measure("1 kHz sine, amp 0.5", sine, sp, tp);
        DAWLAB_CHECK_NEAR(sp, -6.02, 0.05);
        DAWLAB_CHECK_NEAR(tp, -6.02, 0.15);
    }

    // (2) fs/4 的正弦波、相位偏 45°：sample peak 低估 3.01 dB。
    //     64 tap（偶數長度）的插值點落在 1/8、3/8、5/8、7/8 sample 處，碰不到正中間的峰值，
    //     所以量到的 true peak 會比真正的 -6.02 dBTP 低約 0.17 dB（想想為什麼；答案在 solutions/README.md）。
    //     前 kTapsPerPhase 個 sample 是濾波器暖機，不計入。
    {
        dawlab::AudioBuffer sine(1, dawlab::secondsToSamples(0.5, kSampleRate));
        for (int i = 0; i < sine.getNumSamples(); ++i)
            sine.setSample(0, i, static_cast<float>(0.5 * std::sin(dawlab::kTwoPi * 0.25 * i + dawlab::kPi / 4.0)));
        measure("12 kHz (fs/4) sine, phase 45deg", sine, sp, tp, kTapsPerPhase);
        DAWLAB_CHECK_NEAR(sp, -9.03, 0.05);
        DAWLAB_CHECK_NEAR(tp, -6.19, 0.05);
        DAWLAB_CHECK_NEAR(tp - sp, 2.84, 0.05);
        dawlab::writeWav(dawlab::outputPath("l05_ex4_intersample.wav"), sine, kSampleRate);
    }

    // (3) hard clip 的 drum loop：true peak 會超過 sample peak
    {
        dawlab::AudioBuffer drums = dawlab::makeDrumLoop(kSampleRate, 1);
        drums.applyGain(dawlab::dbToGain(12.0f));
        for (int ch = 0; ch < drums.getNumChannels(); ++ch) {
            float* d = drums.getWritePointer(ch);
            for (int i = 0; i < drums.getNumSamples(); ++i)
                d[i] = std::clamp(d[i], -1.0f, 1.0f);
        }
        measure("drums +12 dB, hard clip to 0 dBFS", drums, sp, tp);
        DAWLAB_CHECK_NEAR(sp, 0.0, 0.01);
        DAWLAB_CHECK(tp > sp);
    }

    // (4) Limiter 輸出：sample peak ≤ ceiling，但 true peak 明顯更高（超過 -1 dBTP）
    {
        const float ceilingDb = -1.0f;
        dawlab::AudioBuffer drums = dawlab::makeDrumLoop(kSampleRate, 1);
        dawlab::dsp::Limiter limiter;
        limiter.prepare(kSampleRate, kBlockSize, drums.getNumChannels());
        limiter.setCeilingDb(ceilingDb);
        limiter.setInputGainDb(12.0f);
        limiter.setLookaheadMs(5.0f);
        limiter.setReleaseMs(80.0f);
        limiter.reset();
        processInBlocks(drums, limiter);
        measure("drums +12 dB -> Limiter (ceiling -1)", drums, sp, tp);
        DAWLAB_CHECK(sp <= ceilingDb + 1.0e-3f);
        DAWLAB_CHECK(tp > sp);
        DAWLAB_CHECK(tp > ceilingDb);
    }

    return dawlab::test::summary("l05_ex4");
}
