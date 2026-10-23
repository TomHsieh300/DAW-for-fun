#pragma once
// Saturation：waveshaping 式的「飽和」效果（tape / tube / console 那種溫暖的失真）。
//
// 訊號流程（第 06 章 README 3.2 節）：
//
//   x ──> drive(dB) ──> [Oversampler ×L] ──> shape(curve) ──> [÷L] ──> DC blocker ──> output gain ──┐
//   │                                                                                                 mix ──> y
//   └──> 延遲 latency 個 sample（dry 要跟 wet 對齊，否則 mix 時會有 comb filtering）──────────────────┘
//
// - drive：進入曲線之前的增益。曲線都設計成「小訊號時斜率 = 1」，所以 drive 越大、訊號越深入曲線的彎曲區、諧波越多。
// - shape：四種 transfer curve（static float shape(curve, x)，純函式，練習與範例可以直接呼叫）：
//     Tanh        y = tanh(x)                         經典 soft clip，只產生奇次諧波
//     Cubic       y = x - x³/3（|x| ≤ 1），否則 ±2/3   多項式；只有 3 次諧波（小訊號時），|x| > 1 後是 hard clip
//     Arctan      y = (2/π) atan(πx/2)                 小訊號時 3 次項較大（x³ 係數 π²/12 ≈ 0.82 vs tanh 的 1/3），knee 較軟；
//                                                       但它的奇異點（x = ±2i/π ≈ ±0.64i）比 tanh 的（±iπ/2 ≈ ±1.57i）更靠近實軸，
//                                                       所以在常見的 drive 範圍（peak ≲ 4）高次諧波反而比 tanh 多、衰減較慢，
//                                                       對 aliasing 更不利。只有在極大 drive 時，因為趨近 ±1 很慢（~1/x），
//                                                       中低次諧波才會比 tanh 少一些（最高次諧波仍然衰減得比 tanh 慢）
//     Asymmetric  y = tanh(x)（x ≥ 0）、tanh(2x)/2（x < 0）  正負半波不對稱 → 偶次諧波 + DC（類 tube）
// - DC blocker：不對稱曲線會讓訊號的平均值不再是 0（DC offset）。DC 會吃掉 headroom、讓後面的
//   compressor 誤判、在 fade 時產生 click，所以用第 02 章的 OnePole highpass（預設 10 Hz）移除。
// - Oversampler：非線性產生的諧波超過 Nyquist 會 alias；prepare 前用 setOversamplingFactorLog2() 選 1x/2x/4x/8x。
// - mix：dry/wet 平行混合（parallel saturation）。dry 經過與 wet 相同的延遲。
//
// 參數變化都經過線性 ramp（20 ms），避免 zipper noise。process() 不 allocate。

#include <dawlab/AudioBuffer.h>
#include <dawlab/DspMath.h>
#include <dawlab/dsp/OnePole.h>
#include <dawlab/dsp/Oversampler.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace dawlab::dsp {

enum class SaturationCurve { Tanh, Cubic, Arctan, Asymmetric };

inline const char* saturationCurveName(SaturationCurve curve) noexcept {
    switch (curve) {
    case SaturationCurve::Tanh: return "Tanh";
    case SaturationCurve::Cubic: return "Cubic";
    case SaturationCurve::Arctan: return "Arctan";
    case SaturationCurve::Asymmetric: return "Asymmetric";
    }
    return "?";
}

class Saturation {
public:
    // ---- transfer curves（純函式）----------------------------------------------------------
    static float shape(SaturationCurve curve, float x) noexcept {
        switch (curve) {
        case SaturationCurve::Tanh:
            return std::tanh(x);
        case SaturationCurve::Cubic:
            // 在 |x| = 1 處 y = 2/3、斜率 1 - x² = 0：值與一階導數都連續，接到 hard clip 不會有折角
            if (x >= 1.0f) return 2.0f / 3.0f;
            if (x <= -1.0f) return -2.0f / 3.0f;
            return x - x * x * x * (1.0f / 3.0f);
        case SaturationCurve::Arctan:
            // (2/π) atan(πx/2)：原點斜率 (2/π)(π/2) = 1、漸近值 ±1（跟 tanh 一樣的正規化，方便比較）。
            // 級數 x - (π²/12) x³ + …：3 次項比 tanh（x - x³/3）大，所以同樣 drive 下 H3 較多、「knee」較軟
            return static_cast<float>((2.0 / kPi) * std::atan(0.5 * kPi * static_cast<double>(x)));
        case SaturationCurve::Asymmetric:
            // 正半波 tanh(x)（飽和在 +1）、負半波 tanh(2x)/2（提早飽和在 -0.5）。
            // 兩邊在原點的斜率都是 1，所以曲線本身是平滑的；不對稱 → 偶次諧波 + DC。
            return x >= 0.0f ? std::tanh(x) : 0.5f * std::tanh(2.0f * x);
        }
        return x;
    }

    // ---- 設定（prepare 之前）-------------------------------------------------------------
    // 1x = 0、2x = 1、4x = 2、8x = 3。在 prepare() 之前呼叫（prepare 會依此配置 Oversampler）。
    void setOversamplingFactorLog2(int factorLog2) noexcept {
        oversamplingLog2_ = std::clamp(factorLog2, 0, Oversampler::kMaxFactorLog2);
    }

    // ---- 標準介面 -------------------------------------------------------------------------
    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        sampleRate_ = sampleRate;
        numChannels_ = numChannels;
        // maxBlockSize ≤ 0 是不合理的輸入，但 process() 一定會切出長度 ≥ 1 的塊，
        // 所以底下所有 buffer 都要用「夾過的」maxBlockSize_ 配置，否則會寫進大小為 0 的 buffer。
        maxBlockSize_ = std::max(1, maxBlockSize);
        oversampler_ = Oversampler(oversamplingLog2_);
        oversampler_.prepare(sampleRate, maxBlockSize_, numChannels);
        latency_ = oversampler_.getLatencySamples();

        dcBlocker_.prepare(sampleRate, maxBlockSize_, numChannels);
        dcBlocker_.setType(OnePole::Type::Highpass);
        dcBlocker_.setCutoff(dcBlockerHz_);

        // dry 延遲線：長度 latency + 1 的 ring buffer（latency = 0 時就是「原樣讀回」）
        dryDelay_.assign(static_cast<size_t>(numChannels), std::vector<float>(static_cast<size_t>(latency_ + 1), 0.0f));
        dryPos_.assign(static_cast<size_t>(numChannels), 0);
        dryScratch_.setSize(numChannels, maxBlockSize_);

        const int rampSamples = std::max(1, static_cast<int>(std::lround(kRampSeconds * sampleRate)));
        driveGain_.prepare(rampSamples, dbToGain(driveDb_));
        outputGain_.prepare(rampSamples, dbToGain(outputGainDb_));
        mix_.prepare(rampSamples, mixTarget_);
        prepared_ = true;
        reset();
    }

    void reset() noexcept {
        oversampler_.reset();
        dcBlocker_.reset();
        for (auto& d : dryDelay_)
            std::fill(d.begin(), d.end(), 0.0f);
        std::fill(dryPos_.begin(), dryPos_.end(), 0);
        driveGain_.snap();
        outputGain_.snap();
        mix_.snap();
    }

    void setCurve(SaturationCurve curve) noexcept { curve_ = curve; }
    SaturationCurve getCurve() const noexcept { return curve_; }

    // drive：0 dB = 訊號原樣進入曲線；+20 dB = 乘 10 倍再進曲線
    void setDriveDb(float db) noexcept {
        driveDb_ = db;
        driveGain_.setTarget(dbToGain(db));
    }
    float getDriveDb() const noexcept { return driveDb_; }

    // 0 = 全 dry、1 = 全 wet
    void setMix(float mix) noexcept {
        mixTarget_ = std::clamp(mix, 0.0f, 1.0f);
        mix_.setTarget(mixTarget_);
    }
    float getMix() const noexcept { return mixTarget_; }

    // wet 路徑最後的補償增益（drive 推高後通常要降回來）
    void setOutputGainDb(float db) noexcept {
        outputGainDb_ = db;
        outputGain_.setTarget(dbToGain(db));
    }
    float getOutputGainDb() const noexcept { return outputGainDb_; }

    // DC blocker（預設開、10 Hz）。對稱曲線其實不需要，但開著沒有壞處
    void setDcBlockerEnabled(bool enabled) noexcept { dcBlockerEnabled_ = enabled; }
    void setDcBlockerHz(float hz) noexcept {
        dcBlockerHz_ = hz;
        if (prepared_)
            dcBlocker_.setCutoff(hz);
    }

    // 目前實際生效的倍率（與 getLatencySamples() 一致）。prepare 之後再呼叫 setOversamplingFactorLog2()
    // 不會改變它，要等下一次 prepare()；prepare 之前回傳的是「prepare 時將會使用」的倍率。
    int getOversamplingFactor() const noexcept {
        return prepared_ ? oversampler_.getFactor() : (1 << oversamplingLog2_);
    }
    // setOversamplingFactorLog2() 存下、等下一次 prepare() 才生效的值（0..3）
    int getPendingOversamplingFactorLog2() const noexcept { return oversamplingLog2_; }
    // 以原始取樣率計的延遲（= Oversampler 的延遲；1x 時為 0）
    int getLatencySamples() const noexcept { return latency_; }

    // host 偶爾會給比 prepare 時更大的 block（不合規，但真的會發生）：內部 buffer 都是依 maxBlockSize 配置的，
    // 所以這裡切成 maxBlockSize 的小塊處理。ramp、FIR 與 DC blocker 的狀態都會延續，結果與一次處理完全相同。
    void process(AudioBlock block) noexcept {
        const int total = block.getNumSamples();
        for (int start = 0; start < total; start += maxBlockSize_)
            processChunk(block.getSubBlock(start, std::min(maxBlockSize_, total - start)));
    }

private:
    void processChunk(AudioBlock block) noexcept {
        const int n = block.getNumSamples();
        const int numChannels = std::min(block.getNumChannels(), numChannels_);
        if (n <= 0 || numChannels <= 0)
            return;

        // 1. 把 dry 複製到 scratch（之後 mix 要用），wet 路徑套 drive（ramp 每個 sample 前進一次）
        for (int ch = 0; ch < numChannels; ++ch)
            std::copy_n(block.getChannelPointer(ch), n, dryScratch_.getWritePointer(ch));
        for (int i = 0; i < n; ++i) {
            const float drive = driveGain_.next();
            for (int ch = 0; ch < numChannels; ++ch)
                block.getChannelPointer(ch)[i] *= drive;
        }

        // 2. 升頻 → 非線性 → 降頻（1x 時 Oversampler 只是複製）
        AudioBlock up = oversampler_.upsample(block);
        const int upLen = up.getNumSamples();
        for (int ch = 0; ch < numChannels; ++ch) {
            float* u = up.getChannelPointer(ch);
            switch (curve_) { // switch 放在迴圈外，避免每個 sample 都判斷一次
            case SaturationCurve::Tanh:
                for (int i = 0; i < upLen; ++i) u[i] = std::tanh(u[i]);
                break;
            default:
                for (int i = 0; i < upLen; ++i) u[i] = shape(curve_, u[i]);
                break;
            }
        }
        oversampler_.downsample(block);

        // 3. DC blocker → output gain → 與延遲後的 dry 混合
        for (int i = 0; i < n; ++i) {
            const float outGain = outputGain_.next();
            const float mix = mix_.next();
            for (int ch = 0; ch < numChannels; ++ch) {
                float* d = block.getChannelPointer(ch);
                float wet = d[i];
                // 上游送進 NaN 時，NaN 會穿過 FIR（幾十個 sample 後自己離開），但一進入 DC blocker 的
                // 遞迴狀態就永遠出不來（NaN 乘任何數都是 NaN）→ 之後的輸出全部是 NaN。
                // 所以在進入遞迴結構之前把非有限值換成 0：只會有一小段靜音，之後自動恢復。
                if (!std::isfinite(wet))
                    wet = 0.0f;
                if (dcBlockerEnabled_)
                    wet = dcBlocker_.processSample(ch, wet);
                wet *= outGain;
                const float dry = pushAndReadDry(ch, dryScratch_.getSample(ch, i)); // 延遲 latency 個 sample 的 dry
                d[i] = dry + (wet - dry) * mix; // = dry (1 - mix) + wet mix
            }
        }
    }

    // 線性 ramp（與第 01 章 SmoothedValue 同概念，這裡用最小實作避免跨章依賴）
    struct LinearRamp {
        float current = 1.0f, target = 1.0f, step = 0.0f;
        int remaining = 0, rampSamples = 1;
        void prepare(int samples, float value) noexcept {
            rampSamples = std::max(1, samples);
            current = target = value;
            remaining = 0;
        }
        void setTarget(float v) noexcept {
            target = v;
            remaining = rampSamples;
            step = (target - current) / static_cast<float>(rampSamples);
        }
        void snap() noexcept { current = target; remaining = 0; }
        float next() noexcept {
            if (remaining > 0) {
                current += step;
                if (--remaining == 0)
                    current = target;
            }
            return current;
        }
    };

    // ring buffer 長度 latency + 1：先把 x[n] 寫在 pos，「下一格」就是 latency 個 sample 前寫進去的 x[n - latency]。
    // latency = 0 時長度為 1，寫完立刻讀回同一格 → 原樣輸出。
    float pushAndReadDry(int ch, float x) noexcept {
        std::vector<float>& d = dryDelay_[static_cast<size_t>(ch)];
        int& pos = dryPos_[static_cast<size_t>(ch)];
        const int size = static_cast<int>(d.size());
        d[static_cast<size_t>(pos)] = x;
        pos = (pos + 1 == size) ? 0 : pos + 1;
        return d[static_cast<size_t>(pos)];
    }

    static constexpr double kRampSeconds = 0.02;

    SaturationCurve curve_ = SaturationCurve::Tanh;
    float driveDb_ = 0.0f;
    float outputGainDb_ = 0.0f;
    float mixTarget_ = 1.0f;
    float dcBlockerHz_ = 10.0f;
    bool dcBlockerEnabled_ = true;
    int oversamplingLog2_ = 1;
    bool prepared_ = false;

    double sampleRate_ = 48000.0;
    int numChannels_ = 0;
    int maxBlockSize_ = 1;
    int latency_ = 0;

    Oversampler oversampler_{1};
    OnePole dcBlocker_;
    std::vector<std::vector<float>> dryDelay_;
    std::vector<int> dryPos_;
    AudioBuffer dryScratch_;
    LinearRamp driveGain_, outputGain_, mix_;
};

} // namespace dawlab::dsp
