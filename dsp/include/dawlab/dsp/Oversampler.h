#pragma once
// Oversampler：2x / 4x / 8x oversampling，用 FIR halfband filter 的 polyphase 實作。
//
// 為什麼需要它？非線性處理（saturation、distortion、hard clip……）會產生新的諧波，
// 其中超過 Nyquist（fs/2）的部分會「折返」（fold back / alias）到可聽頻帶，變成與原音不和諧的雜音。
// 解法是先把訊號升到 L 倍的 sample rate（Nyquist 變成 L·fs/2），在高取樣率下做非線性，
// 再用 lowpass 把 fs/2 以上的諧波濾掉、降回原本的 sample rate。
//
//   x[n] ──> upsample(×L) ──> 非線性 f() ──> downsample(÷L) ──> y[n]
//            （插零 + lowpass）              （lowpass + 丟棄）
//
// 每一級都是「×2」或「÷2」的 halfband 級（HalfbandStage）；4x = 兩級串接、8x = 三級串接。
//
// Halfband FIR 的特性（第 06 章 README 2.6 節有推導）：
//   - 截止頻率剛好在新 sample rate 的 1/4（也就是舊 sample rate 的 Nyquist）。
//   - 係數 h[n] 在 n 為偶數時全部是 0（只有中心 h[0] = 1/2 例外），奇數 n 時 h[n] = sin(πn/2)/(πn)。
//   - 所以把它寫成 polyphase 之後：
//       升頻時   even 輸出 = 奇數係數的 FIR（2K 個乘法）、odd 輸出 = 純延遲（0 個乘法）
//       降頻時   輸出 = 奇數係數 FIR（作用在偶數輸入）+ 1/2 × 奇數輸入的延遲
//     每個「原始取樣率的 sample」只要 2K 次乘加，而不是整條 FIR 在高取樣率下跑 2 次（≈ 8K 次）。
//   - 代價：線性相位 FIR 一定有延遲（latency），每級升頻 + 降頻共 2M 個「該級高取樣率」的 sample
//     （M = 2K - 1 是 FIR 的 group delay，也就是 FIR 的半長度；注意它和上面的倍率 L 是不同的東西）。
//     getLatencySamples() 會把所有級的延遲換算回原始取樣率，
//     並保證是整數（降頻時可以選擇取 filter 輸出的偶數或奇數 sample，奇數少延遲 1 個高取樣率 sample，
//     用這個 decimation phase 把零頭湊掉）。
//
// 用法：
//   dawlab::dsp::Oversampler os(2);              // 2^2 = 4x
//   os.prepare(48000.0, 512, 2);
//   ...
//   dawlab::AudioBlock up = os.upsample(block);   // up 的 sample 數 = block 的 4 倍（內部 buffer 的 view）
//   for (每個 sample) up[i] = std::tanh(up[i]);   // 在 192 kHz 做非線性
//   os.downsample(block);                         // 寫回 block（block 的長度必須與 upsample 時相同）
//
// 只依賴 common/（AudioBuffer、DspMath）；upsample / downsample 不 allocate。

#include <dawlab/AudioBuffer.h>
#include <dawlab/DspMath.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <vector>

namespace dawlab::dsp {

namespace detail {

// 0 階修正 Bessel 函式 I0(x)，Kaiser window 用。級數：I0(x) = Σ_k ((x/2)^k / k!)^2
inline double besselI0(double x) noexcept {
    const double y = 0.25 * x * x;
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 500; ++k) {
        term *= y / (static_cast<double>(k) * static_cast<double>(k));
        sum += term;
        if (term < 1.0e-14 * sum)
            break;
    }
    return sum;
}

} // namespace detail

// 一級 halfband：負責「×2 升頻」與「÷2 降頻」。Oversampler 會把 1~3 級串起來。
// 升頻與降頻各有自己的 FIR 歷史（同一級在一個 block 裡會先升頻、後降頻，兩條 FIR 看到的是不同的訊號）。
class HalfbandStage {
public:
    // 設計 halfband FIR 的完整係數（長度 2*numOddTaps - 1 = 4K - 1，numOddTaps = 2K 必須是偶數）。
    // 理想 halfband：h[0] = 1/2、h[n] = sin(πn/2)/(πn)（n 為奇數）、h[n] = 0（n 為非 0 偶數），
    // 乘上 Kaiser window（β 越大：stopband 衰減越深、但 transition band 越寬）。
    // 回傳的 h 以 index 0 = 最左邊的 tap（h[-M]）、中心在 index M = 2K - 1。Σh ≈ 1（DC gain ≈ 1）。
    static std::vector<double> designHalfband(int numOddTaps, double beta) {
        assert(numOddTaps >= 2 && numOddTaps % 2 == 0);
        const int K = numOddTaps / 2;
        const int M = 2 * K - 1;             // 半長度；完整長度 2M + 1 = 4K - 1
        std::vector<double> h(static_cast<size_t>(2 * M + 1), 0.0);
        const double i0beta = detail::besselI0(beta);
        for (int n = -M; n <= M; ++n) {
            double ideal;
            if (n == 0)
                ideal = 0.5;
            else if (n % 2 == 0)
                ideal = 0.0;                 // halfband 的關鍵：偶數 tap 全為 0
            else
                ideal = std::sin(0.5 * kPi * n) / (kPi * n);
            const double r = static_cast<double>(n) / static_cast<double>(M);
            const double window = detail::besselI0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0beta;
            h[static_cast<size_t>(n + M)] = ideal * window;
        }
        return h;
    }

    // numOddTaps = 2K（奇數係數的個數，也是每個輸入 sample 的乘法次數）；beta = Kaiser β；
    // decimationPhase（0 或 1）：降頻時取 filter 輸出的偶數還是奇數 sample（phase 1 少延遲 1 個高取樣率 sample；
    // Oversampler 用它來把總 latency 湊成整數）。
    void prepare(int numOddTaps, double beta, int numChannels, int decimationPhase = 0) {
        K_ = numOddTaps / 2;
        phase_ = decimationPhase;
        const std::vector<double> h = designHalfband(numOddTaps, beta);
        const int M = 2 * K_ - 1;
        // polyphase 奇數分支：g[j] = 2 h[2(j - K) + 1]，j = 0 .. 2K-1（乘 2 是升頻（插零）的補償；降頻時再乘回 1/2）
        g_.resize(static_cast<size_t>(2 * K_));
        for (int j = 0; j < 2 * K_; ++j)
            g_[static_cast<size_t>(j)] = static_cast<float>(2.0 * h[static_cast<size_t>(2 * (j - K_) + 1 + M)]);
        state_.assign(static_cast<size_t>(numChannels), ChannelState{});
        for (auto& s : state_) {
            s.upHist.assign(static_cast<size_t>(4 * K_), 0.0f);        // 2N（N = 2K）鏡像 ring buffer
            s.downHist.assign(static_cast<size_t>(4 * K_), 0.0f);
            s.delayHist.assign(static_cast<size_t>(2 * (K_ + 1)), 0.0f);
        }
        reset();
    }

    void reset() noexcept {
        for (auto& s : state_) {
            std::fill(s.upHist.begin(), s.upHist.end(), 0.0f);
            std::fill(s.downHist.begin(), s.downHist.end(), 0.0f);
            std::fill(s.delayHist.begin(), s.delayHist.end(), 0.0f);
            s.upPos = 0;
            s.downPos = 0;
            s.delayPos = 0;
        }
    }

    int getNumOddTaps() const noexcept { return 2 * K_; }
    int getFilterLength() const noexcept { return 4 * K_ - 1; }
    // FIR 的 group delay M = 2K - 1（FIR 的半長度），以「這一級的高取樣率」計
    int getFilterDelay() const noexcept { return 2 * K_ - 1; }
    int getDecimationPhase() const noexcept { return phase_; }
    // 這一級升頻 + 降頻的總延遲，以「這一級的高取樣率」計：2M - phase
    int getRoundTripDelay() const noexcept { return 2 * getFilterDelay() - phase_; }
    const std::vector<float>& getPolyphaseCoefficients() const noexcept { return g_; }

    // 升頻：n 個輸入 → 2n 個輸出。
    //   out[2i]   = Σ_j g[j] x[i - j]      （內插出來的「中間點」）
    //   out[2i+1] = x[i - (K - 1)]          （原始 sample 本身，純延遲）
    // 整體相當於「插零 + 2·h 的 lowpass」再延遲 M 個高取樣率 sample。
    void upsample(int channel, const float* in, float* out, int n) noexcept {
        ChannelState& s = state_[static_cast<size_t>(channel)];
        const int N = 2 * K_;
        const float* g = g_.data();
        for (int i = 0; i < n; ++i) {
            pushMirror(s.upHist, s.upPos, N, in[i]);
            const float* hist = s.upHist.data() + s.upPos; // hist[j] = x[i - j]，連續記憶體
            float acc = 0.0f;
            for (int j = 0; j < N; ++j)
                acc += g[j] * hist[j];
            out[2 * i] = acc;
            out[2 * i + 1] = hist[K_ - 1];
        }
    }

    // 降頻：2n 個輸入 → n 個輸出。先濾波、再每兩個取一個（polyphase 讓我們只算需要的那一個）：
    //   phase 0：out[i] = 1/2 ( Σ_j g[j] u_even[i - j] + u_odd[i - K] )        = w[2i - M]
    //   phase 1：out[i] = 1/2 ( Σ_j g[j] u_odd[i - j]  + u_even[i - (K - 1)] ) = w[2i - (M - 1)]
    // 其中 u_even[i] = in[2i]、u_odd[i] = in[2i+1]，w = 用完整 h 濾波（零相位）的結果。
    void downsample(int channel, const float* in, float* out, int n) noexcept {
        ChannelState& s = state_[static_cast<size_t>(channel)];
        const int N = 2 * K_;
        const int D = K_ + 1;                      // delayHist 的長度
        const int centreDelay = (phase_ == 0) ? K_ : K_ - 1;
        const float* g = g_.data();
        for (int i = 0; i < n; ++i) {
            const float firIn = (phase_ == 0) ? in[2 * i] : in[2 * i + 1];
            const float delayIn = (phase_ == 0) ? in[2 * i + 1] : in[2 * i];
            pushMirror(s.downHist, s.downPos, N, firIn);
            pushMirror(s.delayHist, s.delayPos, D, delayIn);
            const float* hist = s.downHist.data() + s.downPos;
            float acc = 0.0f;
            for (int j = 0; j < N; ++j)
                acc += g[j] * hist[j];
            out[i] = 0.5f * (acc + s.delayHist[static_cast<size_t>(s.delayPos + centreDelay)]);
        }
    }

private:
    struct ChannelState {
        std::vector<float> upHist;    // 升頻 FIR 的歷史，長度 2N：每個 sample 寫兩份，讓最近 N 個 sample 永遠連續
        std::vector<float> downHist;  // 降頻 FIR 的歷史（同樣技巧）
        std::vector<float> delayHist; // 降頻「純延遲」分支的歷史，長度 2D
        int upPos = 0;
        int downPos = 0;
        int delayPos = 0;
    };

    // 「鏡像 ring buffer」：buffer 長度 2N，寫入位置往下走，同一個值寫在 pos 與 pos + N。
    // 之後 buf[pos + j]（j = 0..N-1）就是 x[n - j]，不需要 modulo，FIR 內迴圈可以直接跑。
    static void pushMirror(std::vector<float>& buf, int& pos, int N, float x) noexcept {
        pos = (pos == 0) ? N - 1 : pos - 1;
        buf[static_cast<size_t>(pos)] = x;
        buf[static_cast<size_t>(pos + N)] = x;
    }

    int K_ = 0;
    int phase_ = 0;
    std::vector<float> g_;
    std::vector<ChannelState> state_;
};

class Oversampler {
public:
    // 預設的 filter 設計（第 06 章 README 2.6 節有數字；範例 l06_oversampler_response 會實際量）：
    //   第 1 級（×2，在 2fs 下）  32 個奇數 tap（63 taps），Kaiser β = 8 → stopband 約 -80 dB、
    //                              transition 約 20.3 kHz → 28 kHz（fs = 48 kHz 時）
    //   第 2 級（×4，在 4fs 下）  12 個奇數 tap（23 taps）：transition band 可以寬很多，所以短很多
    //   第 3 級（×8，在 8fs 下）   8 個奇數 tap（15 taps）
    static constexpr int kMaxFactorLog2 = 3;
    static constexpr int kOddTapsStage1 = 32;
    static constexpr int kOddTapsStage2 = 12;
    static constexpr int kOddTapsStage3 = 8;
    static constexpr double kKaiserBeta = 8.0;

    // 倍率 = 2^factorLog2：0 = 不 oversampling（1x，純複製）、1 = 2x、2 = 4x、3 = 8x
    explicit Oversampler(int factorLog2 = 1) noexcept : factorLog2_(std::clamp(factorLog2, 0, kMaxFactorLog2)) {}

    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        sampleRate_ = sampleRate;
        maxBlockSize_ = std::max(1, maxBlockSize); // ≤ 0 是不合理的輸入；至少配置 1，避免 buffer 大小為 0
        numChannels_ = numChannels;
        stages_.clear();
        stages_.resize(static_cast<size_t>(factorLog2_));

        // 總延遲（以 1/2^S 個原始 sample 為單位）= Σ_i (2 M_i - φ_i) · 2^(S-i)
        //   M_i：第 i 級 FIR 的 group delay（升頻與降頻各一次）；φ_i ∈ {0, 1}：第 i 級的 decimation phase。
        // 先用 φ = 0 算出 delayUnits，零頭 r = delayUnits mod 2^S 用各級的 φ 扣掉（r 的二進位第 (S-i) 位 → 第 i 級），
        // 剩下的就是整數個原始 sample。
        const int S = factorLog2_;
        int delayUnits = 0;
        for (int i = 1; i <= S; ++i) {
            const int K = oddTapsForStage(i) / 2;
            delayUnits += 2 * (2 * K - 1) * (1 << (S - i));
        }
        const int remainder = (S > 0) ? delayUnits % (1 << S) : 0;
        for (int i = 1; i <= S; ++i) {
            const int phase = (remainder >> (S - i)) & 1;
            stages_[static_cast<size_t>(i - 1)].prepare(oddTapsForStage(i), kKaiserBeta, numChannels, phase);
        }
        latencySamples_ = (S > 0) ? (delayUnits - remainder) >> S : 0;

        buffers_.clear();
        buffers_.resize(static_cast<size_t>(S + 1));
        for (int i = 0; i <= S; ++i)
            buffers_[static_cast<size_t>(i)].setSize(numChannels, maxBlockSize_ << i);
        currentNumSamples_ = 0;
    }

    void reset() noexcept {
        for (auto& s : stages_)
            s.reset();
        for (auto& b : buffers_)
            b.clear();
    }

    int getFactorLog2() const noexcept { return factorLog2_; }
    int getFactor() const noexcept { return 1 << factorLog2_; }
    double getOversampledRate() const noexcept { return sampleRate_ * getFactor(); }
    int getNumStages() const noexcept { return factorLog2_; }
    const HalfbandStage& getStage(int index) const noexcept { return stages_[static_cast<size_t>(index)]; }

    // 升頻 + 降頻來回的總延遲，以「原始取樣率」的 sample 計（保證是整數）
    int getLatencySamples() const noexcept { return latencySamples_; }

    // 升頻：回傳內部最高取樣率 buffer 的 view，長度 = input 長度 × factor。
    // 使用者直接在這個 view 上做非線性處理，再呼叫 downsample()。
    AudioBlock upsample(const AudioBlock& input) noexcept {
        const int n = input.getNumSamples();
        const int numChannels = std::min(input.getNumChannels(), numChannels_);
        assert(n <= maxBlockSize_ && "block 比 prepare 時的 maxBlockSize 大");
        currentNumSamples_ = n;
        if (factorLog2_ == 0) {
            AudioBlock view = viewOf(0, n);
            view.copyFrom(input);
            return view;
        }
        for (int s = 1; s <= factorLog2_; ++s) {
            const int inLen = n << (s - 1);
            AudioBuffer& dst = buffers_[static_cast<size_t>(s)];
            for (int ch = 0; ch < numChannels; ++ch) {
                const float* src = (s == 1) ? input.getChannelPointer(ch)
                                            : buffers_[static_cast<size_t>(s - 1)].getReadPointer(ch);
                stages_[static_cast<size_t>(s - 1)].upsample(ch, src, dst.getWritePointer(ch), inLen);
            }
        }
        return viewOf(factorLog2_, n << factorLog2_);
    }

    // 降頻：把內部高取樣率 buffer 的內容濾波、降回原始取樣率，寫進 output（長度必須與 upsample 的 input 相同）
    void downsample(AudioBlock output) noexcept {
        const int n = output.getNumSamples();
        const int numChannels = std::min(output.getNumChannels(), numChannels_);
        assert(n == currentNumSamples_ && "downsample 的長度必須與 upsample 時相同");
        if (factorLog2_ == 0) {
            output.copyFrom(viewOf(0, n));
            return;
        }
        for (int s = factorLog2_; s >= 1; --s) {
            const int outLen = n << (s - 1);
            const AudioBuffer& src = buffers_[static_cast<size_t>(s)];
            for (int ch = 0; ch < numChannels; ++ch) {
                float* dst = (s == 1) ? output.getChannelPointer(ch)
                                      : buffers_[static_cast<size_t>(s - 1)].getWritePointer(ch);
                stages_[static_cast<size_t>(s - 1)].downsample(ch, src.getReadPointer(ch), dst, outLen);
            }
        }
    }

private:
    static int oddTapsForStage(int stage) noexcept {
        return stage == 1 ? kOddTapsStage1 : (stage == 2 ? kOddTapsStage2 : kOddTapsStage3);
    }

    AudioBlock viewOf(int level, int numSamples) noexcept {
        AudioBuffer& b = buffers_[static_cast<size_t>(level)];
        return AudioBlock(b.getArrayOfWritePointers(), b.getNumChannels(), numSamples);
    }

    int factorLog2_ = 1;
    double sampleRate_ = 48000.0;
    int maxBlockSize_ = 0;
    int numChannels_ = 0;
    int latencySamples_ = 0;
    int currentNumSamples_ = 0;
    std::vector<HalfbandStage> stages_;
    std::vector<AudioBuffer> buffers_; // buffers_[i] 的取樣率 = fs × 2^i；[0] 只在 1x 時用
};

} // namespace dawlab::dsp
