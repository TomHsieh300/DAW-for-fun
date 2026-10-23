#pragma once
// ParametricEq：6-band parametric EQ（第 03 章）。
//
// 每個 band 是一顆第 02 章的 Biquad（RBJ Audio EQ Cookbook 係數），六顆 band 串接（cascade）。
// 串接的意義：H(z) = H1(z)·H2(z)·…·H6(z)，所以 dB 響應會「相加」——這也是 getMagnitudeDb()
// 能把每個 band 的 magnitudeDb() 加總就得到整條曲線的原因。
//
// 參數即時改變時避免 click 的策略（README 第 2.9 節）：
//   1. 三個會影響係數的參數（frequency、Q、gain）各自用第 01 章的 SmoothedValue 做 linear ramp
//      （frequency 在 log2 域平滑，掃頻時聽起來每個 octave 速度一致；gain 在 dB 域平滑）。
//   2. 每 kDefaultUpdateInterval（預設 32）個 sample 檢查一次（tick）：平滑器只往前推「上次 tick 以來
//      實際經過的 sample 數」，參數值跟上次算係數時不同的 band 才重算 RBJ 係數。
//      係數不是逐 sample 重算（省 sin/cos/pow），也不是「插值係數」（為什麼不插值見 README）。
//   3. band 的 enabled 開關用 dry/wet mix 做 20 ms crossfade，所以 LowCut/HighCut/Notch 這些
//      沒有 gain 可以淡出的 band 也不會 click。
//   4. 「結構性」的改變（band type、LowCut/HighCut 的 slope）沒辦法平滑地內插——Peak 和 Notch 之間
//      沒有「一半」。做法是：先把這個 band 用 mix 淡出到 bypass（20 ms）、在完全 bypass 時換 type
//      並清掉 filter 記憶，再淡入新的 type（20 ms）。總共約 40 ms，聽起來是一次很短的 crossfade。
//
// 介面依 docs/authoring-guide.md 第 5 節的合約；額外提供 EqSlope（LowCut/HighCut 12 或 24 dB/oct）、
// getResponse / getPhaseDegrees / getGroupDelaySamples、Q↔bandwidth 換算等輔助函式。

#include <dawlab/AudioBuffer.h>
#include <dawlab/DspMath.h>
#include <dawlab/dsp/Biquad.h>
#include <dawlab/dsp/SmoothedValue.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <complex>

namespace dawlab::dsp {

enum class EqBandType { LowCut, LowShelf, Peak, HighShelf, HighCut, Notch };

// LowCut / HighCut 的斜率：Db12 = 一顆 2 階 Butterworth（用 band 的 q，預設 0.707）；
// Db24 = 兩顆 2 階串接成 4 階 Butterworth（Q = 0.5412 與 1.3066，此時 band 的 q 會被忽略）。
enum class EqSlope { Db12, Db24 };

struct EqBand {
    EqBandType type = EqBandType::Peak;
    float frequency = 1000.0f; // Hz（Peak 的中心、Shelf 的轉折「中點」、Cut 的 -3 dB 點）
    float q = 0.707f;          // Peak/Notch：頻寬；Shelf：轉折的陡度（0.707 = 最陡但不過衝）；Cut：共振
    float gainDb = 0.0f;       // 只對 Peak / LowShelf / HighShelf 有意義
    bool enabled = false;
};

class ParametricEq {
public:
    static constexpr int kNumBands = 6;
    static constexpr float kMinFrequency = 10.0f;
    // 目標值（getBand 回傳的值）只夾到這個「絕對」上限；跟 sample rate 有關的上限 0.49·fs
    // 只在算係數時套用（clampFrequency），所以 prepare 前設定的頻率不會被預設的 48 kHz 永久夾掉。
    static constexpr float kMaxFrequency = 100000.0f;
    static constexpr float kMinQ = 0.05f;
    static constexpr float kMaxQ = 40.0f;
    static constexpr float kMaxGainDb = 36.0f;
    static constexpr int kDefaultUpdateInterval = 32; // 每 32 個 sample 重算一次係數（48 kHz 下約 0.67 ms）
    static constexpr double kDefaultSmoothingSeconds = 0.02;
    // 4 階 Butterworth 拆成兩顆 2 階的 Q（同第 02 章 butterworthQ(4, k)）：
    //     Q_k = 1 / (2 cos((2k + 1) π / 8))，k = 0 → 0.5412，k = 1 → 1.3066
    // 兩顆各自的 -3 dB 點都不在 fc，但相乘後 |H(fc)| = 1/√2，整體仍是 -3 dB（README 2.3 節）。
    static constexpr double kButterworthQ1 = 0.54119610014619698;
    static constexpr double kButterworthQ2 = 1.30656296487637652;

    ParametricEq() { setDefaultBands(); }

    // ---- 標準介面 ----------------------------------------------------------------------

    // 唯一可以 allocate 的地方：配置每顆 Biquad 的 channel 狀態，並把所有平滑器對齊目前設定
    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        sampleRate_ = sampleRate;
        numChannels_ = numChannels;
        for (auto& b : bands_) {
            b.stage1.prepare(sampleRate, maxBlockSize, numChannels);
            b.stage2.prepare(sampleRate, maxBlockSize, numChannels);
            b.logFreq.reset(sampleRate, smoothingSeconds_);
            b.q.reset(sampleRate, smoothingSeconds_);
            b.gainDb.reset(sampleRate, smoothingSeconds_);
            b.mix.reset(sampleRate, smoothingSeconds_);
        }
        reset();
    }

    // 清掉 filter 記憶，並讓平滑器直接跳到目標值（不要在開頭就 ramp）。
    // 頻率會用目前的 sample rate 重新 clamp（prepare 換了 sample rate 時就在這裡生效）。
    void reset() noexcept {
        for (auto& b : bands_) {
            b.stage1.reset();
            b.stage2.reset();
            snapToTarget(b);
        }
        samplesUntilUpdate_ = 0;
        samplesSinceTick_ = 0;
    }

    // ---- 參數 -----------------------------------------------------------------------------
    // 所有 setter 都必須由呼叫 process() 的同一個 thread（audio thread）在 block 之間呼叫：
    // 它們會改動平滑器與 filter 狀態，跟 process() 同時執行就是 data race。
    // UI 的參數請先用 atomic / FIFO 送到 audio thread，再在 process() 之前套用（第 13、14 章）。

    void setBand(int index, const EqBand& band) noexcept {
        assert(index >= 0 && index < kNumBands);
        Band& b = bands_[static_cast<size_t>(index)];
        EqBand t = band;
        // NaN / inf 的欄位直接忽略（保留舊值）：std::clamp(NaN, lo, hi) 會原樣回傳 NaN，
        // 一旦 NaN 進了平滑器，log2 / exp2 之後整個 band 就壞了
        if (!std::isfinite(t.frequency))
            t.frequency = b.target.frequency;
        if (!std::isfinite(t.q))
            t.q = b.target.q;
        if (!std::isfinite(t.gainDb))
            t.gainDb = b.target.gainDb;
        // 目標值保留使用者的設定（只夾到與 sample rate 無關的範圍）；0.49·fs 的上限在
        // targetLogFreq() / makeCoefficients() 裡才套用
        t.frequency = std::clamp(t.frequency, kMinFrequency, kMaxFrequency);
        t.q = std::clamp(t.q, kMinQ, kMaxQ);
        t.gainDb = std::clamp(t.gainDb, -kMaxGainDb, kMaxGainDb);
        const bool paramsChanged =
            t.frequency != b.target.frequency || t.q != b.target.q || t.gainDb != b.target.gainDb;
        if (!paramsChanged && t.type == b.target.type && t.enabled == b.target.enabled)
            return; // 完全沒變（例如 host 每個 block 都重送同一組參數）：什麼都不做，ramp 照原速前進
        if (paramsChanged)
            catchUpSmoothers(); // 先把所有 ramp 推進到「現在」，新的 ramp 才會從正確的位置出發
        b.target = t;
        b.logFreq.setTargetValue(targetLogFreq(t));
        b.q.setTargetValue(t.q);
        b.gainDb.setTargetValue(t.gainDb);
        if (!b.active)
            snapParameters(b); // 完全 bypass 中：沒有人聽得到，參數直接跳到目標（淡入時就是最終的曲線）
        requestStructure(b);
        // 下一個 sample 立刻 tick，不必等到下一個 update 邊界。tick 只推進實際經過的 sample 數
        // （samplesSinceTick_），所以提早 tick 不會讓 ramp 變快。
        samplesUntilUpdate_ = 0;
        // 有結構性切換在排隊時，先淡出到 bypass；否則淡到 enabled 對應的 mix
        b.mix.setTargetValue((t.enabled && !b.pendingSwitch) ? 1.0f : 0.0f);
    }

    const EqBand& getBand(int index) const noexcept {
        assert(index >= 0 && index < kNumBands);
        return bands_[static_cast<size_t>(index)].target;
    }

    // 方便 UI 只改一個欄位的小幫手（都走 setBand，所以同樣沒有 click）
    void setBandEnabled(int index, bool enabled) noexcept {
        EqBand b = getBand(index);
        b.enabled = enabled;
        setBand(index, b);
    }
    void setBandType(int index, EqBandType type) noexcept {
        EqBand b = getBand(index);
        b.type = type;
        setBand(index, b);
    }
    void setBandFrequency(int index, float hz) noexcept {
        EqBand b = getBand(index);
        b.frequency = hz;
        setBand(index, b);
    }
    void setBandQ(int index, float q) noexcept {
        EqBand b = getBand(index);
        b.q = q;
        setBand(index, b);
    }
    void setBandGainDb(int index, float gainDb) noexcept {
        EqBand b = getBand(index);
        b.gainDb = gainDb;
        setBand(index, b);
    }

    // LowCut / HighCut 的斜率（其他 band 類型忽略此設定）
    void setSlope(int index, EqSlope slope) noexcept {
        assert(index >= 0 && index < kNumBands);
        Band& b = bands_[static_cast<size_t>(index)];
        if (slope == b.slope)
            return;
        b.slope = slope;
        requestStructure(b);
        samplesUntilUpdate_ = 0;
        b.mix.setTargetValue((b.target.enabled && !b.pendingSwitch) ? 1.0f : 0.0f);
    }
    EqSlope getSlope(int index) const noexcept {
        assert(index >= 0 && index < kNumBands);
        return bands_[static_cast<size_t>(index)].slope;
    }

    // 平滑時間（秒）。請在 prepare() 之前呼叫；預設 20 ms。0 = 不平滑（參數與開關都瞬間切換）。
    void setSmoothingSeconds(double seconds) noexcept { smoothingSeconds_ = std::max(0.0, seconds); }
    double getSmoothingSeconds() const noexcept { return smoothingSeconds_; }

    // 平滑期間每隔幾個 sample 重算一次係數（1 = 逐 sample，最平滑但最花 CPU）
    void setCoefficientUpdateInterval(int samples) noexcept { updateInterval_ = std::max(1, samples); }
    int getCoefficientUpdateInterval() const noexcept { return updateInterval_; }

    double getSampleRate() const noexcept { return sampleRate_; }

    // ---- 頻率響應（給 UI 畫圖；用「目標」參數，不受平滑進度影響）------------------------

    // 整條 EQ 曲線的 dB：enabled band 的 dB 相加（串接 = 複數相乘 = dB 相加）
    double getMagnitudeDb(double freq) const noexcept {
        double total = 0.0;
        for (int i = 0; i < kNumBands; ++i)
            total += getBandMagnitudeDb(i, freq);
        return total;
    }

    // 單一 band 的 dB（未啟用 = 0 dB）
    double getBandMagnitudeDb(int index, double freq) const noexcept {
        const Band& b = bands_[static_cast<size_t>(index)];
        if (!b.target.enabled)
            return 0.0;
        double db = makeCoefficients(sampleRate_, b.target, b.slope).magnitudeDb(freq, sampleRate_);
        if (usesTwoStages(b.target.type, b.slope))
            db += makeSecondStageCoefficients(sampleRate_, b.target).magnitudeDb(freq, sampleRate_);
        return db;
    }

    // 整條 EQ 的複數響應 H(e^{jω})（所有 enabled band 相乘）
    std::complex<double> getResponse(double freq) const noexcept {
        std::complex<double> h(1.0, 0.0);
        for (const auto& b : bands_) {
            if (!b.target.enabled)
                continue;
            h *= makeCoefficients(sampleRate_, b.target, b.slope).response(freq, sampleRate_);
            if (usesTwoStages(b.target.type, b.slope))
                h *= makeSecondStageCoefficients(sampleRate_, b.target).response(freq, sampleRate_);
        }
        return h;
    }

    // 相位（度，-180..180）
    double getPhaseDegrees(double freq) const noexcept { return std::arg(getResponse(freq)) * 180.0 / kPi; }

    // 群延遲 τ(ω) = -dφ/dω，用數值微分估計，單位 sample
    double getGroupDelaySamples(double freq) const noexcept {
        const double df = std::max(0.5, freq * 1e-3);
        const double p1 = std::arg(getResponse(freq - df));
        const double p2 = std::arg(getResponse(freq + df));
        double dphi = p2 - p1; // unwrap 到 (-π, π]
        while (dphi > kPi)
            dphi -= kTwoPi;
        while (dphi <= -kPi)
            dphi += kTwoPi;
        const double dOmega = kTwoPi * (2.0 * df) / sampleRate_;
        return -dphi / dOmega;
    }

    // ---- 靜態輔助：公式對照 README 第 2.4–2.6 節 ----------------------------------------------

    // Q -> bandwidth（octave）：BW = (2 / ln 2) · asinh(1 / (2Q))
    static double qToBandwidthOctaves(double q) noexcept { return 2.0 / std::log(2.0) * std::asinh(1.0 / (2.0 * q)); }

    // bandwidth（octave）-> Q：Q = 1 / (2 sinh(ln 2 · BW / 2)) = 2^(BW/2) / (2^BW - 1)
    static double bandwidthOctavesToQ(double bw) noexcept { return 1.0 / (2.0 * std::sinh(std::log(2.0) * bw / 2.0)); }

    // RBJ shelf slope 參數 S -> Q：Q = 1 / sqrt((A + 1/A)(1/S - 1) + 2)，S = 1 時對任何 gain 都是 0.707
    static double shelfSlopeToQ(double slope, double gainDb) noexcept {
        const double A = std::pow(10.0, gainDb / 40.0);
        return 1.0 / std::sqrt((A + 1.0 / A) * (1.0 / slope - 1.0) + 2.0);
    }

    // peak 的「中點增益」頻寬邊緣：f_{1,2} = f0 ( sqrt(1 + 1/(4Q²)) ∓ 1/(2Q) )
    static void bandEdges(double f0, double q, double& fLow, double& fHigh) noexcept {
        const double r = std::sqrt(1.0 + 1.0 / (4.0 * q * q));
        fLow = f0 * (r - 1.0 / (2.0 * q));
        fHigh = f0 * (r + 1.0 / (2.0 * q));
    }

    static float clampFrequency(float hz, double sampleRate) noexcept {
        return std::clamp(hz, kMinFrequency, static_cast<float>(0.49 * sampleRate));
    }

    static bool usesTwoStages(EqBandType type, EqSlope slope) noexcept {
        return slope == EqSlope::Db24 && (type == EqBandType::LowCut || type == EqBandType::HighCut);
    }

    // 一個 band 的（第一顆）Biquad 係數
    static BiquadCoefficients makeCoefficients(double sr, const EqBand& band, EqSlope slope = EqSlope::Db12) noexcept {
        const double f = clampFrequency(band.frequency, sr);
        const double q = std::clamp(band.q, kMinQ, kMaxQ);
        const double g = std::clamp(band.gainDb, -kMaxGainDb, kMaxGainDb);
        const bool two = usesTwoStages(band.type, slope);
        switch (band.type) {
        case EqBandType::LowCut: return BiquadCoefficients::makeHighpass(sr, f, two ? kButterworthQ1 : q);
        case EqBandType::HighCut: return BiquadCoefficients::makeLowpass(sr, f, two ? kButterworthQ1 : q);
        case EqBandType::LowShelf: return BiquadCoefficients::makeLowShelf(sr, f, q, g);
        case EqBandType::HighShelf: return BiquadCoefficients::makeHighShelf(sr, f, q, g);
        case EqBandType::Notch: return BiquadCoefficients::makeNotch(sr, f, q);
        case EqBandType::Peak:
        default: return BiquadCoefficients::makePeak(sr, f, q, g);
        }
    }

    // Db24 的第二顆係數（只有 LowCut / HighCut 會用到；其他類型回傳恆等）
    static BiquadCoefficients makeSecondStageCoefficients(double sr, const EqBand& band) noexcept {
        const double f = clampFrequency(band.frequency, sr);
        if (band.type == EqBandType::LowCut)
            return BiquadCoefficients::makeHighpass(sr, f, kButterworthQ2);
        if (band.type == EqBandType::HighCut)
            return BiquadCoefficients::makeLowpass(sr, f, kButterworthQ2);
        return BiquadCoefficients{};
    }

    // ---- 處理 ---------------------------------------------------------------------------

    // 逐 sample：每個 frame 請依序呼叫 channel 0, 1, …；參數平滑的計數器在 channel 0 推進
    float processSample(int channel, float x) noexcept {
        if (channel == 0)
            advanceFrame();
        return processChain(channel, x);
    }

    void process(AudioBlock block) noexcept {
        const int n = block.getNumSamples();
        const int nch = std::min(block.getNumChannels(), numChannels_);
        for (int i = 0; i < n; ++i) {
            advanceFrame();
            for (int ch = 0; ch < nch; ++ch) {
                float* d = block.getChannelPointer(ch);
                d[i] = processChain(ch, d[i]);
            }
        }
    }

private:
    struct Band {
        EqBand target;                   // UI 設定的目標值
        EqSlope slope = EqSlope::Db12;   // UI 設定的 slope
        EqBandType runningType = EqBandType::Peak; // filter 目前「真正在跑」的 type / slope
        EqSlope runningSlope = EqSlope::Db12;      // （結構性切換淡出期間仍是舊的）
        bool pendingSwitch = false;      // 等淡出到 bypass 後要換 type / slope
        SmoothedValue logFreq, q, gainDb; // 平滑中的參數（frequency 以 log2(Hz) 平滑）
        SmoothedValue mix;               // 0 = bypass，1 = 全開；enabled 切換時 crossfade
        Biquad stage1, stage2;
        float mixCurrent = 0.0f;
        bool active = false;             // 目前需不需要跑這個 band 的 filter
        bool twoStages = false;
        bool dirty = false;              // type / slope 改了，下次 tick 一定要重算係數
        float coeffLogFreq = 0.0f, coeffQ = 0.0f, coeffGainDb = 0.0f; // 目前的係數是用哪組參數值算的
    };
    // 注意：整個 class 刻意保持可複製 / 可移動（沒有 atomic、沒有指標），
    // 第 14 章的 DAW 才能把它放進 std::vector<Track> 之類的容器。

    void setDefaultBands() noexcept {
        const EqBand defaults[kNumBands] = {
            {EqBandType::LowCut, 80.0f, 0.707f, 0.0f, false},     {EqBandType::LowShelf, 150.0f, 0.707f, 0.0f, false},
            {EqBandType::Peak, 400.0f, 1.0f, 0.0f, false},        {EqBandType::Peak, 2500.0f, 1.0f, 0.0f, false},
            {EqBandType::HighShelf, 8000.0f, 0.707f, 0.0f, false}, {EqBandType::HighCut, 16000.0f, 0.707f, 0.0f, false},
        };
        for (int i = 0; i < kNumBands; ++i) {
            bands_[static_cast<size_t>(i)].target = defaults[i];
            snapToTarget(bands_[static_cast<size_t>(i)]);
        }
    }

    // 平滑器的頻率目標：log2(Hz)，並夾在目前 sample rate 的合法範圍內
    float targetLogFreq(const EqBand& t) const noexcept { return std::log2(clampFrequency(t.frequency, sampleRate_)); }

    void snapParameters(Band& b) const noexcept {
        b.logFreq.setCurrentAndTargetValue(targetLogFreq(b.target));
        b.q.setCurrentAndTargetValue(b.target.q);
        b.gainDb.setCurrentAndTargetValue(b.target.gainDb);
    }

    // 平滑器直接跳到目標，並重算係數
    void snapToTarget(Band& b) noexcept {
        snapParameters(b);
        b.mix.setCurrentAndTargetValue(b.target.enabled ? 1.0f : 0.0f);
        b.mixCurrent = b.mix.getCurrentValue();
        b.active = b.target.enabled;
        b.runningType = b.target.type;
        b.runningSlope = b.slope;
        b.pendingSwitch = false;
        updateCoefficients(b);
        b.dirty = false;
    }

    // type 或 slope 跟「正在跑的」不同 → 結構性切換。
    // band 正在發聲（active）時要先淡出；完全 bypass 時直接換，不會有人聽到。
    void requestStructure(Band& b) noexcept {
        const bool isCut = b.target.type == EqBandType::LowCut || b.target.type == EqBandType::HighCut;
        const bool changed = b.target.type != b.runningType || (isCut && b.slope != b.runningSlope);
        if (!changed) {
            // 淡出途中使用者又把 type 改回原本的：取消排隊中的切換，mix 直接淡回去即可
            b.pendingSwitch = false;
            return;
        }
        if (b.active) {
            b.pendingSwitch = true;
        } else {
            b.runningType = b.target.type;
            b.runningSlope = b.slope;
            b.dirty = true;
        }
    }

    void updateCoefficients(Band& b) noexcept {
        // 記下這次用的參數值：tick 時只要目前值跟它們不同就重算（不管是不是「正在平滑」）
        b.coeffLogFreq = b.logFreq.getCurrentValue();
        b.coeffQ = b.q.getCurrentValue();
        b.coeffGainDb = b.gainDb.getCurrentValue();
        EqBand cur = b.target;
        cur.type = b.runningType;
        cur.frequency = std::exp2(b.coeffLogFreq);
        cur.q = b.coeffQ;
        cur.gainDb = b.coeffGainDb;
        b.stage1.setCoefficients(makeCoefficients(sampleRate_, cur, b.runningSlope));
        b.twoStages = usesTwoStages(cur.type, b.runningSlope);
        if (b.twoStages)
            b.stage2.setCoefficients(makeSecondStageCoefficients(sampleRate_, cur));
    }

    // 每個 frame（所有 channel 的同一個 sample 時間點）呼叫一次：
    //   - 係數：每 updateInterval_ 個 sample 才重算（tick）。tick 把平滑器推進「上次 tick 以來
    //     實際經過的 sample 數」，所以係數最多落後 updateInterval_ 個 sample、永遠不會超前；
    //     setBand() 強迫提早 tick 時也只推進已經經過的部分，ramp 的總長度不受影響。
    //   - mix（dry/wet crossfade）：逐 sample 前進。mix 只是乘法，逐 sample 很便宜；
    //     如果也每 32 個 sample 才跳一階，crossfade 本身就會變成小小的 zipper noise。
    void advanceFrame() noexcept {
        if (samplesUntilUpdate_ == 0) {
            tick(samplesSinceTick_);
            samplesSinceTick_ = 0;
            samplesUntilUpdate_ = updateInterval_;
        }
        --samplesUntilUpdate_;
        ++samplesSinceTick_;
        for (auto& b : bands_)
            if (b.mix.isSmoothing())
                b.mixCurrent = b.mix.getNextValue();
    }

    // setBand() 改參數之前：把所有平滑器推進到「現在」（上次 tick 以來經過的 sample），
    // 新的 ramp 才會從現在的值出發，而且下一次 tick 不會再推進同一段。係數由緊接著的 tick 重算。
    void catchUpSmoothers() noexcept {
        if (samplesSinceTick_ <= 0)
            return;
        for (auto& b : bands_) {
            b.logFreq.skip(samplesSinceTick_);
            b.q.skip(samplesSinceTick_);
            b.gainDb.skip(samplesSinceTick_);
        }
        samplesSinceTick_ = 0;
    }

    // 每 updateInterval_ 個 sample（或 setBand 之後的下一個 sample）執行一次：
    // 把平滑器推進 numSamples（上次 tick 以來經過的 sample 數），必要時重算係數
    void tick(int numSamples) noexcept {
        for (auto& b : bands_) {
            // 結構性切換：已經淡出到 bypass → 換 type / slope、清 filter 記憶、再淡入
            if (b.pendingSwitch && !b.mix.isSmoothing() && b.mix.getCurrentValue() <= 0.0f) {
                b.pendingSwitch = false;
                b.runningType = b.target.type;
                b.runningSlope = b.slope;
                snapParameters(b); // 此刻 mix = 0：新 type 直接用最終參數淡入，不必邊淡入邊掃頻
                b.dirty = true;
                b.active = false; // 讓下面的 !wasActive 分支清掉舊的 filter 記憶
                b.mix.setTargetValue(b.target.enabled ? 1.0f : 0.0f);
            }
            const bool wasActive = b.active;
            b.logFreq.skip(numSamples); // 沒在平滑時 skip 什麼都不做
            b.q.skip(numSamples);
            b.gainDb.skip(numSamples);
            b.mixCurrent = b.mix.getCurrentValue();
            b.active = b.mixCurrent > 0.0f || b.mix.isSmoothing();
            if (!b.active)
                continue;
            if (!wasActive) { // 剛從完全 bypass 變成啟用：filter 記憶是舊的，清掉再 crossfade 進來
                b.stage1.reset();
                b.stage2.reset();
            }
            // 「參數值跟上次算係數時不同」才重算：平滑中的 band 每次 tick 都會成立；
            // smoothing = 0 時 setTargetValue 讓值瞬間跳到目標，也是靠這個條件套用到 filter
            const bool paramsMoved = b.logFreq.getCurrentValue() != b.coeffLogFreq ||
                                     b.q.getCurrentValue() != b.coeffQ || b.gainDb.getCurrentValue() != b.coeffGainDb;
            if (paramsMoved || !wasActive || b.dirty) {
                updateCoefficients(b);
                b.dirty = false;
            }
        }
    }

    float processChain(int ch, float x) noexcept {
        float y = x;
        for (auto& b : bands_) {
            if (!b.active)
                continue;
            float wet = b.stage1.processSample(ch, y);
            if (b.twoStages)
                wet = b.stage2.processSample(ch, wet);
            // dry/wet crossfade：mix = 1 時就是 wet；切換 enabled 時 mix 以 20 ms ramp 過去
            y = (b.mixCurrent >= 1.0f) ? wet : y + b.mixCurrent * (wet - y);
        }
        return y;
    }

    std::array<Band, kNumBands> bands_;
    double sampleRate_ = 48000.0;
    double smoothingSeconds_ = kDefaultSmoothingSeconds;
    int numChannels_ = 0;
    int updateInterval_ = kDefaultUpdateInterval;
    int samplesUntilUpdate_ = 0; // 距離下一次 tick 還有幾個 sample
    int samplesSinceTick_ = 0;   // 上次 tick 以來實際經過了幾個 sample（下一次 tick 推進平滑器的量）
};

} // namespace dawlab::dsp
