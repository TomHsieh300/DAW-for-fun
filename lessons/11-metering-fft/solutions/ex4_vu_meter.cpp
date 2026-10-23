// 練習 4 解答：VU meter 的 ballistics（IEC 60268-17）
//
// VU meter 是一支「有質量的指針」：它量的是整流後的平均（接近 RMS，但不是），而且反應刻意很慢，
// 讓人看到「感覺上的音量」而不是每個 transient。規範的要求：
//   - 給一個突然開始的 1 kHz 正弦（0 VU），指針在 300 ms（±10%）內到達最終讀數的 99%
//   - 過衝（overshoot）1% ~ 1.5%
//   - 掉下來的時間與上升相同
//   - 0 VU 對應 +4 dBu；在 DAW 裡通常校準成 -18 dBFS（EBU）或 -20 dBFS（K-20）的正弦 = 0 VU
//
// 用一個二階系統（彈簧 + 阻尼的指針）模擬：
//     p'' + 2ζω_n p' + ω_n² p = ω_n² u(t),  u = 整流後的輸入
//   阻尼比 ζ 決定過衝：overshoot = exp(-πζ / sqrt(1-ζ²))，ζ = 0.82 → 1.11%
//   自然頻率 ω_n 決定快慢：解出「步階響應第一次到 0.99 的正規化時間 u99」，ω_n = u99 / 0.3 s
// 每個 sample 用 semi-implicit Euler 積分（ω_n·T ≈ 3e-4，非常穩定）。
//
// 執行：bin/Debug/l11_ex4_solution.exe（ctest label: solution）

#include <dawlab/Common.h>
#include <dawlab/dsp/LevelMeter.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using namespace dawlab;

namespace {

constexpr double kSampleRate = 48000.0;

class VuMeter {
public:
    void prepare(double sampleRate, int numChannels) {
        sampleRate_ = sampleRate;
        numChannels_ = numChannels;
        state_.assign(static_cast<size_t>(numChannels), NeedleState{});
        readingVu_ = std::make_unique<std::atomic<float>[]>(static_cast<size_t>(numChannels));
        updateCoefficients();
        reset();
    }

    void reset() noexcept {
        for (NeedleState& s : state_)
            s = NeedleState{};
        for (int ch = 0; ch < numChannels_; ++ch)
            readingVu_[static_cast<size_t>(ch)].store(kMinusInfinityDb, std::memory_order_relaxed);
    }

    // 0 VU 對應的正弦 peak 位準（dBFS），預設 -18
    void setReferenceDbfs(float dbfs) noexcept {
        referenceDbfs_ = dbfs;
        updateCoefficients();
    }
    // 到達 99% 的時間（預設 300 ms）與阻尼比（預設 0.82 → 過衝 1.1%）
    void setRiseTimeMs(float ms) noexcept {
        riseTimeMs_ = std::max(1.0f, ms);
        updateCoefficients();
    }
    void setDamping(float zeta) noexcept {
        zeta_ = std::clamp(zeta, 0.3f, 0.999f);
        updateCoefficients();
    }

    // audio thread
    void process(const AudioBlock& block) noexcept {
        const int nch = std::min(numChannels_, block.getNumChannels());
        const int n = block.getNumSamples();
        for (int ch = 0; ch < nch; ++ch) {
            NeedleState& s = state_[static_cast<size_t>(ch)];
            const float* x = block.getChannelPointer(ch);
            for (int i = 0; i < n; ++i) {
                // 全波整流 + 校準：參考位準的正弦整流後平均 = 2A/π，乘 π/(2A) 讓它變成 1.0
                const double u = std::abs(x[i]) * inputScale_;
                // 二階系統，semi-implicit Euler：先更新速度、再用新速度更新位置
                const double acc = wn2_ * (u - s.position) - twoZetaWn_ * s.velocity;
                s.velocity += acc * dt_;
                s.position += s.velocity * dt_;
            }
            // 指針不能跑到刻度左邊的 0 以下（真的指針會撞到擋板）
            const double needle = std::max(0.0, s.position);
            readingVu_[static_cast<size_t>(ch)].store(gainToDb(static_cast<float>(needle)), std::memory_order_relaxed);
        }
    }

    // thread-safe
    float getVu(int ch) const noexcept {
        return (ch >= 0 && ch < numChannels_) ? readingVu_[static_cast<size_t>(ch)].load(std::memory_order_relaxed)
                                              : kMinusInfinityDb;
    }
    // 指針的線性位置（1.0 = 0 VU）
    float getNeedle(int ch) const noexcept { return dbToGain(getVu(ch)); }

    double getNaturalFrequencyHz() const noexcept { return wn_ / kTwoPi; }
    double getOvershootPercent() const noexcept { return 100.0 * std::exp(-kPi * zeta_ / std::sqrt(1.0 - zeta_ * zeta_)); }

    // 二階系統正規化步階響應 y(u)，u = ω_n t（欠阻尼，ζ < 1）：
    //     y(u) = 1 − e^{−ζu} / sqrt(1−ζ²) · sin( sqrt(1−ζ²)·u + acos ζ )
    static double stepResponse(double zeta, double u) noexcept {
        const double s = std::sqrt(1.0 - zeta * zeta);
        return 1.0 - std::exp(-zeta * u) / s * std::sin(s * u + std::acos(zeta));
    }

    // 第一次到達 target（例如 0.99）的正規化時間：在 [0, 峰值時間 π/sqrt(1−ζ²)] 之間 y 單調上升，用二分法
    static double timeToReach(double zeta, double target) noexcept {
        double lo = 0.0, hi = kPi / std::sqrt(1.0 - zeta * zeta);
        for (int i = 0; i < 60; ++i) {
            const double mid = 0.5 * (lo + hi);
            (stepResponse(zeta, mid) < target ? lo : hi) = mid;
        }
        return 0.5 * (lo + hi);
    }

private:
    struct NeedleState {
        double position = 0.0;
        double velocity = 0.0;
    };

    void updateCoefficients() noexcept {
        if (sampleRate_ <= 0.0)
            return;
        dt_ = 1.0 / sampleRate_;
        const double u99 = timeToReach(zeta_, 0.99);
        wn_ = u99 / (riseTimeMs_ * 0.001); // 讓 y(0.3 s) = 0.99
        wn2_ = wn_ * wn_;
        twoZetaWn_ = 2.0 * zeta_ * wn_;
        inputScale_ = kPi / (2.0 * dbToGain(static_cast<double>(referenceDbfs_)));
    }

    double sampleRate_ = 0.0;
    int numChannels_ = 0;
    float referenceDbfs_ = -18.0f;
    float riseTimeMs_ = 300.0f;
    double zeta_ = 0.82;
    double dt_ = 0.0, wn_ = 0.0, wn2_ = 0.0, twoZetaWn_ = 0.0, inputScale_ = 1.0;
    std::vector<NeedleState> state_;
    std::unique_ptr<std::atomic<float>[]> readingVu_;
};

// 一塊一塊餵，blockSize 可調（量 300 ms 的點時用 480 = 10 ms 讓邊界對齊）
template <typename OnBlock>
void feed(VuMeter& meter, AudioBuffer& audio, int blockSize, OnBlock&& onBlock) {
    for (int start = 0; start < audio.getNumSamples(); start += blockSize) {
        const int len = std::min(blockSize, audio.getNumSamples() - start);
        meter.process(AudioBlock(audio).getSubBlock(start, len));
        onBlock(start + len);
    }
}

} // namespace

int main() {
    VuMeter vu;
    vu.prepare(kSampleRate, 2);
    std::printf("ζ = 0.82 → 過衝 %.2f%%；u99 = %.3f → ω_n = %.2f rad/s（%.2f Hz）\n\n", vu.getOvershootPercent(),
                VuMeter::timeToReach(0.82, 0.99), vu.getNaturalFrequencyHz() * kTwoPi, vu.getNaturalFrequencyHz());
    DAWLAB_CHECK(vu.getOvershootPercent() >= 1.0 && vu.getOvershootPercent() <= 1.5);

    // ---- 1. 步階響應：-18 dBFS 的 1 kHz 正弦（= 0 VU）突然開始，1 s 後突然停止 -------------------------
    {
        AudioBuffer step = makeToneBursts(1000.0, kSampleRate, {-18.0f, -200.0f}, 1.0, 1);
        const int blockSize = 480; // 10 ms
        float needleAt300 = 0.0f, maxNeedle = 0.0f, needleAfterStop300 = 1.0f, finalNeedle = 0.0f;
        std::printf("%8s %10s %10s\n", "time(s)", "needle", "VU");
        feed(vu, step, blockSize, [&](int samplesDone) {
            const double t = samplesDone / kSampleRate;
            const float needle = vu.getNeedle(0);
            if (samplesDone == 14400)
                needleAt300 = needle; // 剛好 300 ms
            if (t <= 1.0) {
                maxNeedle = std::max(maxNeedle, needle);
                finalNeedle = needle;
            }
            if (samplesDone == 48000 + 14400)
                needleAfterStop300 = needle; // 停止後 300 ms
            if (samplesDone % 4800 == 0 && t <= 1.6)
                std::printf("%8.2f %10.4f %10.2f\n", t, needle, vu.getVu(0));
        });
        // 規範的 99% 與過衝都是相對於「最終讀數」。最終讀數不是精確的 1.0：1 kHz 在 48 kHz 下一週期只有
        // 48 個 sample，取樣後的 |sin| 平均 = cot(π/48)/24 = 0.63571，比連續的 2/π = 0.63662 低 0.14%。
        const float settle = needleAt300 / finalNeedle;
        const float overshoot = maxNeedle / finalNeedle - 1.0f;
        std::printf("1 s 時（最終讀數）= %.4f（%.3f VU）；300 ms 時 = 最終讀數的 %.2f%%（規範 99%%）；"
                    "過衝 %.2f%%（規範 1~1.5%%）\n",
                    finalNeedle, gainToDb(finalNeedle), 100.0 * settle, 100.0 * overshoot);
        std::printf("停止後 300 ms 指針 = %.4f（≤ 0.015：掉下來和升上去一樣快）\n\n", needleAfterStop300);
        DAWLAB_CHECK_NEAR(settle, 0.99, 0.005);
        DAWLAB_CHECK(overshoot >= 0.01f && overshoot <= 0.015f);
        DAWLAB_CHECK_NEAR(gainToDb(finalNeedle), 0.0, 0.05);
        DAWLAB_CHECK(needleAfterStop300 <= 0.015f);
    }

    // ---- 2. 校準：-24 dBFS 的正弦 → -6 VU；0 dBFS → +18 VU ----------------------------------------
    {
        AudioBuffer quiet = makeSine(1000.0, kSampleRate, 1.0, dbToGain(-24.0f), 1);
        vu.reset();
        feed(vu, quiet, 512, [](int) {});
        std::printf("-24 dBFS 正弦 → %.2f VU（期望 -6）\n", vu.getVu(0));
        DAWLAB_CHECK_NEAR(vu.getVu(0), -6.0, 0.05);

        AudioBuffer loud = makeSine(1000.0, kSampleRate, 1.0, 1.0f, 1);
        vu.reset();
        feed(vu, loud, 512, [](int) {});
        std::printf("0 dBFS 正弦 → %.2f VU（期望 +18）\n", vu.getVu(0));
        DAWLAB_CHECK_NEAR(vu.getVu(0), 18.0, 0.05);

        vu.setReferenceDbfs(-20.0f); // K-20 校準
        vu.reset();
        feed(vu, quiet, 512, [](int) {});
        std::printf("改成 -20 dBFS = 0 VU 後，-24 dBFS 正弦 → %.2f VU（期望 -4）\n\n", vu.getVu(0));
        DAWLAB_CHECK_NEAR(vu.getVu(0), -4.0, 0.05);
        vu.setReferenceDbfs(-18.0f);
    }

    // ---- 3. 鼓組：VU（平均）遠低於 sample peak，而且幾乎不跟著每一下鼓點跳 ----------------------------
    {
        AudioBuffer drums = makeDrumLoop(kSampleRate, 2, 120.0, 0.8f, 2);
        dsp::LevelMeter peakMeter;
        peakMeter.prepare(kSampleRate, 2);
        peakMeter.setPeakHoldMs(-1.0f); // 無限 hold
        vu.reset();
        float minVu = 1000.0f, maxVu = -1000.0f;
        std::printf("%8s %10s %10s\n", "time(s)", "VU", "peak dBFS");
        feed(vu, drums, 512, [&](int samplesDone) {
            peakMeter.process(AudioBlock(drums).getSubBlock(samplesDone - std::min(512, samplesDone),
                                                            std::min(512, samplesDone)));
            const double t = samplesDone / kSampleRate;
            if (t > 1.0) {
                minVu = std::min(minVu, vu.getVu(0));
                maxVu = std::max(maxVu, vu.getVu(0));
            }
            if (samplesDone % (512 * 20) == 0)
                std::printf("%8.2f %10.2f %10.2f\n", t, vu.getVu(0), peakMeter.getPeakDb(0));
        });
        const float peakDbfs = peakMeter.getPeakHoldDb(0);
        const float vuAsDbfs = maxVu - 18.0f; // VU 讀數換回 dBFS（以 -18 dBFS = 0 VU）
        std::printf("鼓組：VU 在 %.2f ~ %.2f 之間擺動（換算 %.2f dBFS），sample peak = %.2f dBFS\n", minVu, maxVu,
                    vuAsDbfs, peakDbfs);
        DAWLAB_CHECK(vuAsDbfs < peakDbfs - 6.0f); // 平均值比峰值低很多（crest factor）
        // 注意：VU 在鼓點之間的空檔會掉很多（minVu 很低）—— 300 ms 的 ballistics 是「上升與下降一樣慢」，
        // 而這個鼓組的空檔有好幾百 ms，所以指針確實會落下來。這是正常的 VU 行為。
    }

    // ---- 4. 短促的 transient：10 ms 的 0 VU 正弦，VU 指針幾乎不動 -------------------------------------
    // 這才是「VU 看不到 transient」的正確量法：同樣的位準，持續 10 ms 與持續 1 s 的讀數差很多。
    {
        // 10 ms 的正弦之後接 290 ms 靜音：指針有慣性，burst 結束後還會再往上走一小段（約 80 ms 後才到最高點），
        // 所以要多觀察一段時間才抓得到真正的最大值。
        std::vector<float> levels(30, -200.0f);
        levels[0] = -18.0f;
        AudioBuffer click = makeToneBursts(1000.0, kSampleRate, levels, 0.01, 1);
        vu.reset();
        float maxNeedle = 0.0f;
        feed(vu, click, 48, [&](int) { maxNeedle = std::max(maxNeedle, vu.getNeedle(0)); }); // 1 ms 一塊，抓得到最大值
        std::printf("\n10 ms 的 0 VU 正弦：指針最高 = %.4f（%.2f VU）；持續的同位準正弦會到 0 VU\n", maxNeedle,
                    gainToDb(maxNeedle));
        DAWLAB_CHECK(maxNeedle < 0.25f); // 低於 -12 VU：短促的 transient 幾乎推不動指針
        DAWLAB_CHECK(maxNeedle > 0.01f); // 但也不是完全沒反應
    }

    return dawlab::test::summary("l11_ex4");
}
