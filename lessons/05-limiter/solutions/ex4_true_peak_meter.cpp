// 練習 4 解答：簡易 4x oversampling true-peak meter（ITU-R BS.1770 的概念版）
//
// 想法：
//   數位訊號的 sample 只是連續波形上的「取樣點」，真正重建出來的波形在兩個 sample 之間
//   可能更高（inter-sample peak）。BS.1770-4 Annex 2 的做法是把訊號升頻 4 倍（插入 3 個 0、
//   再用 lowpass 把它們「補」成平滑的曲線），然後量升頻後的 sample peak，單位叫 dBTP。
//
//   升頻 lowpass 用 polyphase 的方式算最省：
//     升頻後的 y[4n + p] = Σ_k h[k]·u[4n + p - k]，u 是零插入後的訊號（只有 4 的倍數位置非 0）
//                        = Σ_j h[4j + p]·x[n - j]                      （只留非 0 的項）
//   所以每個輸入 sample 只要用 4 組「子濾波器」（phase p = 0..3，各 kTapsPerPhase 個 tap）
//   對最近 kTapsPerPhase 個輸入做內積，就得到 4 個插值點。不需要真的做出 4 倍長的 buffer。
//
// 執行：l05_ex4_solution
// 輸出：dawlab_output/l05_ex4_intersample.wav（fs/4 的正弦波，sample peak -9 dBFS、true peak -6 dBTP）

#include <dawlab/Common.h>
#include <dawlab/dsp/Limiter.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

constexpr int kOversample = 4;    // BS.1770 建議 48 kHz 時用 4x
constexpr int kTapsPerPhase = 16; // 每個 phase 16 個 tap → 原型濾波器共 64 個 tap
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

    // 只清掉量到的 peak、保留濾波器的歷史：用在「濾波器暖機之後才開始量」
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

                // 把新 sample 推進環形歷史：hist[w] = x[n]，x[n - j] 在 (w - j) mod T
                hist[w] = x[i];
                for (int p = 0; p < kOversample; ++p) {
                    double acc = 0.0;
                    int idx = w;
                    for (int j = 0; j < kTapsPerPhase; ++j) { // y[4n+p] = Σ_j h[4j+p]·x[n-j]
                        acc += static_cast<double>(coeffs_[static_cast<size_t>(p)][static_cast<size_t>(j)]) * hist[idx];
                        if (--idx < 0)
                            idx += kTapsPerPhase;
                    }
                    truePeak_ = std::max(truePeak_, static_cast<float>(std::abs(acc)));
                }
                if (++w >= kTapsPerPhase)
                    w = 0;
            }
        }
    }

    float getSamplePeak() const noexcept { return samplePeak_; }
    float getTruePeak() const noexcept { return truePeak_; }
    float getSamplePeakDb() const noexcept { return dawlab::gainToDb(samplePeak_); }
    float getTruePeakDb() const noexcept { return dawlab::gainToDb(truePeak_); } // 單位：dBTP
    // coeffs[p][j] = h[4j + p]（給 self-check 檢查濾波器設計用）
    const std::array<std::array<float, kTapsPerPhase>, kOversample>& getCoefficients() const noexcept { return coeffs_; }

private:
    // 原型：在 4x 取樣率下 cutoff = 原本的 fs/2（= 新取樣率的 1/8）的 windowed-sinc lowpass，
    // 增益 ×4 補償零插入（插 3 個 0 會讓平均能量掉成 1/4）。
    //   h[k] = sinc((k - M) / 4) · w[k]，M = (N - 1) / 2，w = Hann window
    // 最後把總和正規化成 4，確保 DC（= 所有 phase 的總和）精確等於 1。
    void designFilter() {
        std::array<double, kNumTaps> h{};
        const double M = (kNumTaps - 1) / 2.0;
        double sum = 0.0;
        for (int k = 0; k < kNumTaps; ++k) {
            const double t = (k - M) / kOversample;
            const double sinc = (t == 0.0) ? 1.0 : std::sin(dawlab::kPi * t) / (dawlab::kPi * t);
            const double window = 0.5 - 0.5 * std::cos(dawlab::kTwoPi * k / (kNumTaps - 1));
            h[static_cast<size_t>(k)] = sinc * window;
            sum += h[static_cast<size_t>(k)];
        }
        for (int k = 0; k < kNumTaps; ++k) {
            const double normalized = h[static_cast<size_t>(k)] * kOversample / sum;
            coeffs_[static_cast<size_t>(k % kOversample)][static_cast<size_t>(k / kOversample)] =
                static_cast<float>(normalized); // polyphase 分解：phase p 拿 h[p], h[p+4], h[p+8], ...
        }
    }

    int numChannels_ = 0;
    std::array<std::array<float, kTapsPerPhase>, kOversample> coeffs_{};
    std::vector<float> history_;
    std::vector<int> writeIndex_;
    float samplePeak_ = 0.0f;
    float truePeak_ = 0.0f;
};

// warmupSamples > 0：前面這幾個 sample 只用來填滿濾波器的歷史，不計入 peak。
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

    // (0) 先檢查濾波器本身（不用跑訊號）：
    //     - 所有 tap 總和 = 4：零插入讓 baseband 振幅變成 1/4，lowpass 要乘 4 補回來（README 2.8 節）
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

    // (1) 1 kHz 正弦波 -6 dBFS：sample 幾乎會落在峰值上，兩種 peak 應該差不多
    {
        dawlab::AudioBuffer sine = dawlab::makeSine(1000.0, kSampleRate, 0.5, 0.5f, 1);
        measure("1 kHz sine, amp 0.5", sine, sp, tp);
        DAWLAB_CHECK_NEAR(sp, -6.02, 0.05);
        DAWLAB_CHECK_NEAR(tp, -6.02, 0.15);
    }

    // (2) fs/4 = 12 kHz 的正弦波、相位偏 45°：每個 sample 都落在 ±0.707·A，
    //     sample peak 低估 3.01 dB；4x oversampling 之後插值點會落到峰值附近。
    //
    //     為什麼不是剛好 -6.02 dBTP？64 tap 是偶數長度，原型的中心 M = 31.5 不在整數上，
    //     所以 4 個 phase 的插值點落在兩個原始 sample 之間的 1/8、3/8、5/8、7/8 處（不是 0、1/4、1/2、3/4）。
    //     這個訊號的峰值剛好在兩個 sample 正中間（1/2），最近的插值點差 1/8 sample = 11.25°，
    //     量到 0.5·cos(11.25°) = 0.4904 → -6.19 dBTP，最多低估 0.17 dB（BS.1770 的 48-tap 濾波器也是偶數長度，有同樣的性質）。
    //     前 kTapsPerPhase 個 sample 是濾波器暖機（訊號從 0 突然開始的 onset 暫態），不計入。
    {
        dawlab::AudioBuffer sine(1, dawlab::secondsToSamples(0.5, kSampleRate));
        for (int i = 0; i < sine.getNumSamples(); ++i)
            sine.setSample(0, i, static_cast<float>(0.5 * std::sin(dawlab::kTwoPi * 0.25 * i + dawlab::kPi / 4.0)));
        measure("12 kHz (fs/4) sine, phase 45deg", sine, sp, tp, kTapsPerPhase);
        DAWLAB_CHECK_NEAR(sp, -9.03, 0.05); // 0.5 / √2 = 0.3536 → -9.03 dBFS
        DAWLAB_CHECK_NEAR(tp, -6.19, 0.05); // 真正的峰值 0.5 = -6.02 dBTP；插值點差 1/8 sample → -6.19
        DAWLAB_CHECK_NEAR(tp - sp, 2.84, 0.05);
        dawlab::writeWav(dawlab::outputPath("l05_ex4_intersample.wav"), sine, kSampleRate);
    }

    // (3) hard clip 到 ±1 的 drum loop：sample peak 剛好 0 dBFS，但平頂的邊緣重建後會超過 0 dBTP
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
        DAWLAB_CHECK(tp > sp); // inter-sample peak 一定比 sample peak 高
    }

    // (4) 本章的 Limiter 保證 sample peak ≤ ceiling，但 true peak 明顯更高（drum loop 約高 2 dB）：
    //     這就是為什麼 sample-peak limiter 的 ceiling 實務上要比 -1 dBTP 再往下留 headroom
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
        DAWLAB_CHECK(tp > sp);        // limiter 壓住的是 sample，inter-sample peak 仍然超過 sample peak
        DAWLAB_CHECK(tp > ceilingDb); // 也就是：輸出 sample peak ≤ -1 dBFS，true peak 卻超過 -1 dBTP
        std::printf("  → 若要保證 %.0f dBTP，sample-peak limiter 的 ceiling 大約要設 %.2f dBFS（或改用 true-peak 偵測）\n",
                    ceilingDb, ceilingDb - std::max(0.0f, tp - sp));
    }

    return dawlab::test::summary("l05_ex4");
}
