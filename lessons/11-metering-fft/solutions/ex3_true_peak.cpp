// 練習 3 解答：重用第 06 章的 Oversampler 做 true-peak meter，並與第 05 章練習 4 的 windowed-sinc 版比較
//
// 為什麼 sample peak 不夠？數位訊號的「真正波形」是 sample 之間用 sinc 內插出來的連續曲線，
// 峰值很可能落在兩個 sample 之間（inter-sample peak）。BS.1770-4 Annex 2：把訊號升到 4 倍取樣率後
// 取 |x| 的最大值，單位 dBTP。
//
// 第 05 章練習 4 已經從零做過一次（64 tap Hann windowed-sinc，4 個 phase 一次升 4 倍）。這一題換個角度：
//   (A) 重用：第 06 章的 Oversampler（兩級 2x halfband）已經是現成、測過的 4x 升頻器，
//       true-peak meter 只要「upsample → 找 |x| 最大值」，不需要 downsample。
//   (B) 比較：同一段訊號同時餵兩種 meter，跟「理想 sinc 內插」（FFT 補零）比誤差，再比 CPU 成本：
//       - 插值點落在哪裡（halfband 的 0、1/4、1/2、3/4 vs 偶數長度 FIR 的 1/8、3/8、5/8、7/8）
//       - 接近 Nyquist 的頻率響應（transition band 從哪裡開始）
//       - 每個輸入 sample 要幾次乘法、實際跑多快
//
// 執行：bin/Debug/l11_ex3_solution.exe（ctest label: solution）
// 輸出：dawlab_output/l11_ex3_true_peak_normalized.wav（clip 過的鼓組，normalize 到 -1 dBTP）

#include <dawlab/Common.h>
#include <dawlab/dsp/Fft.h>
#include <dawlab/dsp/Oversampler.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <memory>
#include <vector>

using namespace dawlab;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

// ============================================================================================
// (A) 本題的主角：用第 06 章 Oversampler 做的 true-peak meter
// ============================================================================================
class TruePeakMeter {
public:
    static constexpr int kOversamplingLog2 = 2; // 2^2 = 4x

    // 唯一可以 allocate 的地方
    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        numChannels_ = numChannels;
        oversampler_.prepare(sampleRate, maxBlockSize, numChannels);
        samplePeak_ = std::make_unique<std::atomic<float>[]>(static_cast<size_t>(numChannels));
        truePeak_ = std::make_unique<std::atomic<float>[]>(static_cast<size_t>(numChannels));
        reset();
    }

    void reset() noexcept {
        oversampler_.reset();
        resetPeaks();
    }

    // 只清掉「自上次以來的最大值」，不清 oversampler 的 filter 狀態
    void resetPeaks() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch) {
            samplePeak_[static_cast<size_t>(ch)].store(0.0f, std::memory_order_relaxed);
            truePeak_[static_cast<size_t>(ch)].store(0.0f, std::memory_order_relaxed);
        }
    }

    // audio thread：不 allocate、不鎖
    void process(const AudioBlock& block) noexcept {
        const int nch = std::min(numChannels_, block.getNumChannels());
        // 1. sample peak（原始取樣率）
        for (int ch = 0; ch < nch; ++ch) {
            const float p = peakAbs(block.getChannelPointer(ch), block.getNumSamples());
            auto& slot = samplePeak_[static_cast<size_t>(ch)];
            if (p > slot.load(std::memory_order_relaxed))
                slot.store(p, std::memory_order_relaxed);
        }
        // 2. 升到 4 倍取樣率，在高取樣率下找 |x| 的最大值 = true peak
        //    upsample() 回傳 Oversampler 內部 buffer 的 view（4 倍長）；只量測、不輸出，所以不呼叫 downsample()
        const AudioBlock up = oversampler_.upsample(block);
        for (int ch = 0; ch < nch; ++ch) {
            const float p = peakAbs(up.getChannelPointer(ch), up.getNumSamples());
            auto& slot = truePeak_[static_cast<size_t>(ch)];
            if (p > slot.load(std::memory_order_relaxed))
                slot.store(p, std::memory_order_relaxed);
        }
    }

    // 以下 getter thread-safe；都是「自 reset 以來的最大值」
    float getSamplePeakDb(int ch) const noexcept { return gainToDb(load(samplePeak_.get(), ch)); }
    float getTruePeakDb(int ch) const noexcept { return gainToDb(load(truePeak_.get(), ch)); } // dBTP
    float getTruePeakLinear(int ch) const noexcept { return load(truePeak_.get(), ch); }

    int getNumChannels() const noexcept { return numChannels_; }
    int getOversamplingFactor() const noexcept { return oversampler_.getFactor(); }
    const dsp::Oversampler& getOversampler() const noexcept { return oversampler_; }

private:
    float load(const std::atomic<float>* v, int ch) const noexcept {
        return (v != nullptr && ch >= 0 && ch < numChannels_) ? v[ch].load(std::memory_order_relaxed) : 0.0f;
    }

    int numChannels_ = 0;
    dsp::Oversampler oversampler_{kOversamplingLog2};
    std::unique_ptr<std::atomic<float>[]> samplePeak_, truePeak_;
};

// 只升頻、不降頻時，每個「原始取樣率的 sample」要幾次乘法？
// 第 i 級（i = 1, 2, …）的輸入取樣率是 fs·2^(i-1)：每個原始 sample 會變成 2^(i-1) 個該級的輸入 sample，
// 每個輸入 sample 只算一次奇數 tap 的 FIR（getNumOddTaps() 次乘法）；另一個輸出是純延遲，0 次乘法。
int multipliesPerInputSample(const dsp::Oversampler& os) {
    int total = 0;
    for (int i = 0; i < os.getNumStages(); ++i)
        total += os.getStage(i).getNumOddTaps() << i;
    return total;
}

// ============================================================================================
// (B) 對照組：第 05 章練習 4 的 windowed-sinc 版（64 tap、Hann、4 個 phase；與該題解答相同）
// ============================================================================================
class WindowedSincTruePeak {
public:
    static constexpr int kOversample = 4;
    static constexpr int kTapsPerPhase = 16;
    static constexpr int kNumTaps = kOversample * kTapsPerPhase;

    void prepare(int numChannels) {
        numChannels_ = numChannels;
        // h[k] = sinc((k - M) / 4) · Hann[k]，正規化成 Σh = 4，再拆成 phase：coeffs_[p][j] = h[4j + p]
        std::array<double, kNumTaps> h{};
        const double M = (kNumTaps - 1) / 2.0;
        double sum = 0.0;
        for (int k = 0; k < kNumTaps; ++k) {
            const double t = (k - M) / kOversample;
            const double sinc = (t == 0.0) ? 1.0 : std::sin(kPi * t) / (kPi * t);
            h[static_cast<size_t>(k)] = sinc * (0.5 - 0.5 * std::cos(kTwoPi * k / (kNumTaps - 1)));
            sum += h[static_cast<size_t>(k)];
        }
        for (int k = 0; k < kNumTaps; ++k)
            coeffs_[static_cast<size_t>(k % kOversample)][static_cast<size_t>(k / kOversample)] =
                static_cast<float>(h[static_cast<size_t>(k)] * kOversample / sum);
        history_.assign(static_cast<size_t>(numChannels) * kTapsPerPhase, 0.0f);
        writeIndex_.assign(static_cast<size_t>(numChannels), 0);
        reset();
    }

    void reset() noexcept {
        std::fill(history_.begin(), history_.end(), 0.0f);
        std::fill(writeIndex_.begin(), writeIndex_.end(), 0);
        truePeak_ = 0.0f;
    }

    void process(const AudioBlock& block) noexcept {
        const int nch = std::min(block.getNumChannels(), numChannels_);
        for (int ch = 0; ch < nch; ++ch) {
            float* hist = history_.data() + static_cast<size_t>(ch) * kTapsPerPhase;
            int& w = writeIndex_[static_cast<size_t>(ch)];
            const float* x = block.getChannelPointer(ch);
            for (int i = 0; i < block.getNumSamples(); ++i) {
                hist[w] = x[i];
                for (int p = 0; p < kOversample; ++p) { // y[4n+p] = Σ_j h[4j+p]·x[n-j]
                    double acc = 0.0;
                    int idx = w;
                    for (int j = 0; j < kTapsPerPhase; ++j) {
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

    float getTruePeakDb() const noexcept { return gainToDb(truePeak_); } // 所有 channel 的最大值
    static constexpr int multipliesPerInputSample() { return kNumTaps; } // 4 個 phase × 16 tap

private:
    int numChannels_ = 0;
    std::array<std::array<float, kTapsPerPhase>, kOversample> coeffs_{};
    std::vector<float> history_;
    std::vector<int> writeIndex_;
    float truePeak_ = 0.0f;
};

// ============================================================================================
// 量測工具
// ============================================================================================

// 一塊一塊餵（模擬 audio callback）
template <typename Meter>
void feed(Meter& meter, AudioBuffer& audio) {
    for (int start = 0; start < audio.getNumSamples(); start += kBlockSize) {
        const int len = std::min(kBlockSize, audio.getNumSamples() - start);
        meter.process(AudioBlock(audio).getSubBlock(start, len));
    }
}

float maxTruePeakDb(const TruePeakMeter& meter) {
    float m = kMinusInfinityDb;
    for (int ch = 0; ch < meter.getNumChannels(); ++ch)
        m = std::max(m, meter.getTruePeakDb(ch));
    return m;
}

float maxSamplePeakDb(const TruePeakMeter& meter) {
    float m = kMinusInfinityDb;
    for (int ch = 0; ch < meter.getNumChannels(); ++ch)
        m = std::max(m, meter.getSamplePeakDb(ch));
    return m;
}

// windowed-sinc 版的讀數（每次重新 prepare，兩種 meter 看到的都是同一段、從靜音開始的訊號）
float sincTruePeakDb(AudioBuffer& audio) {
    WindowedSincTruePeak meter;
    meter.prepare(audio.getNumChannels());
    feed(meter, audio);
    return meter.getTruePeakDb();
}

// 「理想」true peak：整段做一次大 FFT，頻譜補 0 到 4 倍長再 IFFT = 理想 sinc 內插在 4x 取樣率上的取樣
// （只差 FFT 的循環邊界效應；訊號頭尾是靜音或淡入淡出時可以忽略）。回傳所有 channel 的最大值。
float idealTruePeakDb(const AudioBuffer& audio) {
    int order = 1;
    while ((1 << order) < audio.getNumSamples())
        ++order;
    const int n = 1 << order;
    dsp::Fft fft(order), fft4(order + 2);
    std::vector<float> x(static_cast<size_t>(n));
    std::vector<std::complex<float>> spec(static_cast<size_t>(fft4.getNumRealBins()));
    std::vector<float> up(static_cast<size_t>(4 * n));
    float peak = 0.0f;
    for (int ch = 0; ch < audio.getNumChannels(); ++ch) {
        std::fill(x.begin(), x.end(), 0.0f);
        std::copy_n(audio.getReadPointer(ch), audio.getNumSamples(), x.begin());
        std::fill(spec.begin(), spec.end(), std::complex<float>(0.0f, 0.0f));
        fft.forwardReal(x.data(), spec.data());
        spec[static_cast<size_t>(n / 2)] *= 0.5f; // Nyquist bin 在 4N 的頻譜裡會被當成兩個共軛 bin，平分
        fft4.inverseReal(spec.data(), up.data());
        // inverseReal 含 1/(4N)，原本的 forward 沒有正規化 → 乘 4 補回振幅
        peak = std::max(peak, 4.0f * peakAbs(up.data(), 4 * n));
    }
    return gainToDb(peak);
}

// fs/4 的正弦：x[n] = A·sin(π n/2 + φ)
AudioBuffer makeQuarterRateSine(double seconds, double phaseRadians, float amplitude = 1.0f) {
    AudioBuffer b(1, secondsToSamples(seconds, kSampleRate));
    for (int i = 0; i < b.getNumSamples(); ++i)
        b.setSample(0, i, static_cast<float>(amplitude * std::sin(kPi * 0.5 * i + phaseRadians)));
    return b;
}

// 頭尾加 raised-cosine 淡入淡出，避免訊號「突然開始」本身的步階產生額外的 inter-sample 過衝
void fadeInOut(AudioBuffer& b, double ms) {
    const int n = std::min(b.getNumSamples() / 2, msToSamples(ms, kSampleRate));
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < n; ++i) {
            const float g = static_cast<float>(0.5 - 0.5 * std::cos(kPi * i / n));
            b.getWritePointer(ch)[i] *= g;
            b.getWritePointer(ch)[b.getNumSamples() - 1 - i] *= g;
        }
}

} // namespace

int main() {
    TruePeakMeter meter;
    meter.prepare(kSampleRate, kBlockSize, 2);
    std::printf("Oversampler：%dx，%d 級 halfband；對照組：第 05 章練習 4 的 64 tap windowed-sinc\n\n",
                meter.getOversamplingFactor(), meter.getOversampler().getNumStages());

    // ---- 1. 1 kHz、0 dBFS：sample 剛好踩到峰值（48 個 sample 一週期，第 12 個 = sin(π/2)）--------
    {
        AudioBuffer sine = makeSine(1000.0, kSampleRate, 1.0, 1.0f, 1);
        meter.reset();
        feed(meter, sine);
        const float sinc = sincTruePeakDb(sine);
        std::printf("1 kHz 0 dBFS：sample peak = %.3f dBFS，true peak：Oversampler %.3f / windowed-sinc %.3f dBTP\n",
                    meter.getSamplePeakDb(0), meter.getTruePeakDb(0), sinc);
        DAWLAB_CHECK_NEAR(meter.getSamplePeakDb(0), 0.0, 0.01);
        DAWLAB_CHECK_NEAR(meter.getTruePeakDb(0), 0.0, 0.1);
        DAWLAB_CHECK_NEAR(sinc, 0.0, 0.1);
    }

    // ---- 2. fs/4、相位 45°：sample peak -3.01 dBFS，真正的峰值 0 dBTP ------------------------------
    {
        AudioBuffer q = makeQuarterRateSine(1.0, kPi / 4.0);
        fadeInOut(q, 10.0);
        meter.reset();
        feed(meter, q);
        const float sinc = sincTruePeakDb(q);
        std::printf("12 kHz 相位 45°：sample peak = %.3f dBFS，true peak：Oversampler %.3f / windowed-sinc %.3f dBTP\n",
                    meter.getSamplePeakDb(0), meter.getTruePeakDb(0), sinc);
        DAWLAB_CHECK_NEAR(meter.getSamplePeakDb(0), -3.01, 0.02);
        DAWLAB_CHECK_NEAR(meter.getTruePeakDb(0), 0.0, 0.15);
    }

    // ---- 3. 相位掃描：同一個 fs/4 正弦，峰值落在原始 sample 之間的不同位置 -------------------------
    //   Oversampler 的 4x 點在 0、1/4、1/2、3/4；64 tap（偶數長度）的點在 1/8、3/8、5/8、7/8。
    //   峰值離最近的點最遠 1/8 sample = 11.25°（fs/4 時），兩者最差都低估 20·log10(cos 11.25°) = -0.17 dB，
    //   只是發生在不同的相位：一個量得準的地方，另一個剛好最差。
    {
        std::printf("\n相位掃描（fs/4 正弦，振幅 1；誤差 = 讀數 − 0 dBTP）\n");
        std::printf("  相位     峰值位置    Oversampler    windowed-sinc\n");
        float worstOs = 0.0f, worstSinc = 0.0f;
        double worstOsPhase = 0.0, worstSincPhase = 0.0;
        constexpr int kSteps = 32;
        for (int s = 0; s <= kSteps; ++s) {
            const double phaseDeg = 90.0 * s / kSteps;
            AudioBuffer q = makeQuarterRateSine(0.25, phaseDeg * kPi / 180.0);
            fadeInOut(q, 10.0);
            meter.reset();
            feed(meter, q);
            const float os = meter.getTruePeakDb(0);
            const float sinc = sincTruePeakDb(q);
            if (os < worstOs) {
                worstOs = os;
                worstOsPhase = phaseDeg;
            }
            if (sinc < worstSinc) {
                worstSinc = sinc;
                worstSincPhase = phaseDeg;
            }
            // 峰值在 n = 1 − 2φ/π（取小數部分）：φ = 0 → 0，φ = 45° → 1/2，φ = 90° → 0
            double where = 1.0 - phaseDeg / 90.0;
            where -= std::floor(where);
            if (s % 4 == 0)
                std::printf("  %5.2f°     %5.3f       %+7.3f dB      %+7.3f dB\n", phaseDeg, where, os, sinc);
        }
        std::printf("  最差：Oversampler %+.3f dB（相位 %.2f°），windowed-sinc %+.3f dB（相位 %.2f°）\n", worstOs,
                    worstOsPhase, worstSinc, worstSincPhase);
        DAWLAB_CHECK_NEAR(worstOs, -0.17, 0.03);
        DAWLAB_CHECK_NEAR(worstSinc, -0.17, 0.03);
        DAWLAB_CHECK(std::abs(worstOsPhase - worstSincPhase) > 5.0); // 最差的位置不一樣
    }

    // ---- 4. 接近 Nyquist 的頻率響應：振幅 0.5（-6.02 dBFS）的正弦，頻率稍微偏離整數比 -----------------
    //   讓峰值在 1 秒內掃過 sample 之間的所有位置，讀數就幾乎只反映插值 filter 在這個頻率的增益。
    {
        std::printf("\n頻率響應（振幅 0.5 的正弦，誤差 = 讀數 − (-6.02 dBTP)）\n");
        std::printf("  頻率          Oversampler    windowed-sinc\n");
        const double freqs[] = {1000.3, 10000.3, 16000.3, 18000.3, 20000.3, 21000.3, 22000.3};
        const float ref = gainToDb(0.5f);
        float osErr[7] = {}, sincErr[7] = {};
        for (int i = 0; i < 7; ++i) {
            AudioBuffer s = makeSine(freqs[i], kSampleRate, 1.0, 0.5f, 1);
            fadeInOut(s, 10.0);
            meter.reset();
            feed(meter, s);
            osErr[i] = meter.getTruePeakDb(0) - ref;
            sincErr[i] = sincTruePeakDb(s) - ref;
            std::printf("  %7.1f Hz     %+7.3f dB      %+7.3f dB\n", freqs[i], osErr[i], sincErr[i]);
        }
        // 低頻到 16 kHz 兩者都準（< 0.02 dB）
        for (int i = 0; i <= 2; ++i)
            DAWLAB_CHECK(std::abs(osErr[i]) < 0.02f && std::abs(sincErr[i]) < 0.02f);
        // Oversampler 一路到 22 kHz 都在 0.02 dB 內：第 1 級 halfband 在 22 kHz 其實衰減 0.65 dB，
        // 但它沒濾乾淨的 image（26 kHz）剛好補回來——halfband 的 |H(f)| + |H(fs − f)| = 1（第 06 章 2.6 節），
        // 兩者同相疊加時峰值正好是原本的振幅
        for (int i = 0; i < 7; ++i)
            DAWLAB_CHECK(std::abs(osErr[i]) < 0.02f);
        // 64 tap Hann windowed-sinc 的 transition band 寬得多：18 kHz 起就有 ±0.1 dB 的誤差，22 kHz 低估約 0.4 dB
        DAWLAB_CHECK(sincErr[6] < osErr[6] - 0.3f);
    }

    // ---- 5. hard clip 過的鼓組：同一段訊號、三種讀數；再 normalize 到 -1 dBTP ------------------------
    {
        AudioBuffer drums = makeDrumLoop(kSampleRate, 2, 120.0, 0.8f, 2);
        drums.applyGain(dbToGain(12.0f)); // 推 +12 dB 再 clip
        for (int ch = 0; ch < drums.getNumChannels(); ++ch)
            for (int i = 0; i < drums.getNumSamples(); ++i)
                drums.setSample(ch, i, std::clamp(drums.getSample(ch, i), -1.0f, 1.0f));
        meter.reset();
        feed(meter, drums);
        const float sp = maxSamplePeakDb(meter), tp = maxTruePeakDb(meter);
        const float sinc = sincTruePeakDb(drums);
        const float ideal = idealTruePeakDb(drums);
        std::printf("\nclip 過的鼓組：sample peak = %.3f dBFS，true peak：Oversampler %.3f / windowed-sinc %.3f / "
                    "FFT 理想內插 %.3f dBTP\n",
                    sp, tp, sinc, ideal);
        DAWLAB_CHECK_NEAR(sp, 0.0, 0.01);
        DAWLAB_CHECK(tp >= sp - 1e-3f); // halfband 的純延遲 phase 就是原始 sample，所以不可能更低
        DAWLAB_CHECK(tp > sp + 0.05f);  // clip 出來的平頂一定會有 inter-sample 過衝
        DAWLAB_CHECK(std::abs(tp - ideal) < std::abs(sinc - ideal)); // 高頻很多時，halfband 較接近理想值

        // 母帶流程：normalize 到 -1 dBTP（而不是 -1 dBFS）
        const float gainDb = -1.0f - tp;
        drums.applyGain(dbToGain(gainDb));
        meter.reset();
        feed(meter, drums);
        std::printf("套用 %+.2f dB 後：sample peak = %.3f dBFS，true peak = %.3f dBTP\n", gainDb,
                    maxSamplePeakDb(meter), maxTruePeakDb(meter));
        DAWLAB_CHECK_NEAR(maxTruePeakDb(meter), -1.0, 0.05);
        DAWLAB_CHECK(maxSamplePeakDb(meter) <= -1.0f + 0.01f);
        writeWav(outputPath("l11_ex3_true_peak_normalized.wav"), drums, kSampleRate);
    }

    // ---- 6. 撥弦素材：每個音都從一段白雜訊開始，接近 Nyquist 的成分很多 ------------------------------
    {
        AudioBuffer plucks = makePluckSequence(kSampleRate, 2.0, 0.4f, 1);
        meter.reset();
        feed(meter, plucks);
        const float sp = meter.getSamplePeakDb(0), tp = meter.getTruePeakDb(0);
        const float sinc = sincTruePeakDb(plucks);
        const float ideal = idealTruePeakDb(plucks);
        std::printf("pluck sequence：sample peak = %.3f dBFS，true peak：Oversampler %.3f / windowed-sinc %.3f / "
                    "FFT 理想內插 %.3f dBTP\n",
                    sp, tp, sinc, ideal);
        DAWLAB_CHECK(tp >= sp - 1e-3f);
        DAWLAB_CHECK(ideal >= tp - 0.1f);   // 4x 只會低估（少量 filter ripple 容許 0.1 dB）
        DAWLAB_CHECK(ideal >= sinc - 0.1f);
        DAWLAB_CHECK(std::abs(ideal - tp) < 1.0f);
        DAWLAB_CHECK(std::abs(ideal - sinc) < 1.5f);
        DAWLAB_CHECK(std::abs(ideal - tp) < std::abs(ideal - sinc)); // 同樣是 Nyquist 附近的能量多，halfband 較接近
    }

    // ---- 7. CPU：每個輸入 sample 的乘法數 + 實際計時 --------------------------------------------------
    {
        const int osMul = multipliesPerInputSample(meter.getOversampler());
        const int sincMul = WindowedSincTruePeak::multipliesPerInputSample();
        std::printf("\n每個輸入 sample 的乘法數：Oversampler %d（第 1 級 %d + 第 2 級 %d × 2），windowed-sinc %d（4 × 16）\n",
                    osMul, meter.getOversampler().getStage(0).getNumOddTaps(),
                    meter.getOversampler().getStage(1).getNumOddTaps(), sincMul);
        DAWLAB_CHECK(osMul == 32 + 2 * 12);
        DAWLAB_CHECK(sincMul == 64);

        // 10 秒 stereo 白雜訊，各跑 3 次取最快的一次（排除第一次的 cache / page fault 等干擾）
        AudioBuffer noise = makeWhiteNoise(kSampleRate, 10.0, 0.25f, 2);
        const double numSamples = static_cast<double>(noise.getNumSamples()) * noise.getNumChannels();
        auto timeIt = [&](auto&& run) {
            double best = 1.0e30;
            for (int r = 0; r < 3; ++r) {
                const auto t0 = std::chrono::steady_clock::now();
                run();
                const auto t1 = std::chrono::steady_clock::now();
                best = std::min(best, std::chrono::duration<double, std::nano>(t1 - t0).count());
            }
            return best / numSamples; // ns / sample
        };
        const double osNs = timeIt([&] {
            meter.reset();
            feed(meter, noise);
        });
        WindowedSincTruePeak sincMeter;
        sincMeter.prepare(2);
        const double sincNs = timeIt([&] {
            sincMeter.reset();
            feed(sincMeter, noise);
        });
        std::printf("實際計時（10 s stereo，每個 channel-sample）：Oversampler %.1f ns，windowed-sinc %.1f ns，"
                    "比值 %.2f（依建置設定與機器而異）\n",
                    osNs, sincNs, sincNs / osNs);
        DAWLAB_CHECK(osNs > 0.0 && sincNs > 0.0);
    }

    return dawlab::test::summary("l11_ex3");
}
