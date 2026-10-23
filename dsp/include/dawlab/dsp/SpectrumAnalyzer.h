#pragma once
// 第 11 章：SpectrumAnalyzer —— audio thread 推 sample、UI thread 算頻譜，中間用 lock-free FIFO。
//
// 架構（single-producer / single-consumer）：
//
//   audio thread                      ring buffer (2 的冪次, = 4N)                  UI thread
//   pushSamples() ──store samples──▶ [ . . . . x x x x x x x x . . . ] ──copy 最近 N 個──▶ computeSpectrum()
//     1. writeEnd_ = pos + n（公告「要寫到哪」）+ release fence           1. writePos_ (acquire) → 決定 start
//     2. 寫 sample（relaxed）                                            2. 複製 [start, start + N)
//     3. writePos_ = pos + n（release，公告「寫完了」）                   3. acquire fence → 讀 writeEnd_
//
// - producer 只寫 ring、writeEnd_ 與 writePos_，consumer 只讀：不需要 mutex。
// - consumer 每次都取「最新的 N 個 sample」而不是依序消費，所以 UI 慢了也不會越落後越多，
//   只會跳過中間的 frame。
// - 覆寫偵測：consumer 複製完之後看 writeEnd_。只要 writeEnd_ − start > 容量，就表示 producer
//   （可能是一個還沒結束的 push）已經開始寫會繞回 [start, start + N) 的位置，這一 frame 作廢。
//   只看 writePos_ 不夠：進行中的 push 可能已經覆寫了一部分，writePos_ 卻還是舊值。
//   什麼時候會發生？一次 push 的長度 n 加上 N 超過容量 4N（例如 N = 64、block 512），
//   或 UI thread 在複製到一半時被作業系統暫停太久。
// - ring 的元素是 std::atomic<float>（relaxed）：兩個 thread 同時讀寫同一塊記憶體在 C++ 記憶體模型裡
//   必須是 atomic 才不是 data race；在 x86 / ARM 上 relaxed 的 float 讀寫就是普通的 load / store。
//
// computeSpectrum() 的頻譜後處理：
//   1. 乘上 window（預設 Hann）
//   2. forwardReal → N/2+1 個 bin
//   3. 正規化：振幅 A 的正弦（正好在 bin 中心）→ |X[k]| = A · N · CG / 2（CG = coherent gain）
//      所以 A = 2|X[k]| / (N·CG)；DC 與 Nyquist 沒有鏡像 bin，係數是 1 而不是 2。
//      如此 full-scale 正弦讀 0 dBFS。
//   4. exponential averaging（線性域）：s[k] = α·s[k] + (1-α)·m[k]，α = smoothing
//   5. 轉 dB

#include <dawlab/AudioBuffer.h>
#include <dawlab/DspMath.h>
#include <dawlab/dsp/Fft.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <complex>
#include <cstdint>
#include <memory>
#include <vector>

namespace dawlab::dsp {

class SpectrumAnalyzer {
public:
    // 會 allocate。fftOrder = 11 → N = 2048，48 kHz 時 bin 解析度 23.4 Hz。
    void prepare(double sampleRate, int fftOrder = 11) {
        assert(sampleRate > 0.0 && fftOrder >= 4 && fftOrder <= 16);
        sampleRate_ = sampleRate;
        fft_.setOrder(fftOrder);
        fftSize_ = fft_.getSize();
        ringCapacity_ = fftSize_ * 4;
        ringMask_ = static_cast<uint64_t>(ringCapacity_ - 1);
        ring_ = std::make_unique<std::atomic<float>[]>(static_cast<size_t>(ringCapacity_));
        for (int i = 0; i < ringCapacity_; ++i)
            ring_[static_cast<size_t>(i)].store(0.0f, std::memory_order_relaxed);
        frame_.assign(static_cast<size_t>(fftSize_), 0.0f);
        spectrum_.assign(static_cast<size_t>(fft_.getNumRealBins()), std::complex<float>(0.0f, 0.0f));
        smoothed_.assign(static_cast<size_t>(fft_.getNumRealBins()), 0.0f);
        rebuildWindow();
        writePos_.store(0, std::memory_order_relaxed);
        writeEnd_.store(0, std::memory_order_relaxed);
        lastFramePos_ = 0;
        primed_ = false;
        updateHop();
    }

    // UI thread：清掉平滑狀態（不清 ring）
    void reset() noexcept {
        std::fill(smoothed_.begin(), smoothed_.end(), 0.0f);
        primed_ = false;
    }

    // 以下三個 setter 在 UI thread 呼叫（computeSpectrum 的同一個 thread）
    void setWindowType(WindowType type) {
        windowType_ = type;
        rebuildWindow();
    }
    // 0 = 不平滑；0.8 左右看起來穩定又不至於太慢。α 是「舊值的比重」。
    void setSmoothing(float alpha) noexcept { smoothing_ = std::clamp(alpha, 0.0f, 0.99f); }
    // 相鄰 frame 的重疊比例（0 ~ 0.9），預設 0.5 → 每累積 N/2 個新 sample 才算下一個 frame
    void setOverlap(float fraction) noexcept {
        overlap_ = std::clamp(fraction, 0.0f, 0.9f);
        updateHop();
    }

    // ---- audio thread（lock-free、不 allocate）-------------------------------------------
    // 一次可以推任意長度（n 很大時 ring 只會留下最後 4N 個 sample，正好是 consumer 需要的）。
    void pushSamples(const float* x, int n) noexcept {
        if (!ring_ || n <= 0)
            return;
        const uint64_t pos = beginWrite(n);
        for (int i = 0; i < n; ++i)
            ring_[static_cast<size_t>((pos + static_cast<uint64_t>(i)) & ringMask_)].store(x[i], std::memory_order_relaxed);
        endWrite(pos, n);
    }

    // 多聲道 block：各 channel 平均成 mono 後推入
    void pushBlock(const AudioBlock& block) noexcept {
        const int nch = block.getNumChannels();
        const int n = block.getNumSamples();
        if (!ring_ || nch <= 0 || n <= 0)
            return;
        const float scale = 1.0f / static_cast<float>(nch);
        const uint64_t pos = beginWrite(n);
        for (int i = 0; i < n; ++i) {
            float sum = 0.0f;
            for (int ch = 0; ch < nch; ++ch)
                sum += block.getSample(ch, i);
            ring_[static_cast<size_t>((pos + static_cast<uint64_t>(i)) & ringMask_)].store(sum * scale, std::memory_order_relaxed);
        }
        endWrite(pos, n);
    }

    // ---- UI thread ----------------------------------------------------------------------
    // 回傳 true 並填入 N/2+1 個 bin 的 dBFS；沒有足夠的新資料時回傳 false（magnitudesDb 不變）。
    bool computeSpectrum(std::vector<float>& magnitudesDb) {
        if (!ring_)
            return false;
        const uint64_t written = writePos_.load(std::memory_order_acquire);
        const uint64_t n = static_cast<uint64_t>(fftSize_);
        if (written < n)
            return false; // 還沒累積到一個完整的 frame
        if (written - lastFramePos_ < static_cast<uint64_t>(hop_))
            return false; // 新 sample 不夠一個 hop，沒有新 frame

        // 複製最新的 N 個 sample 並乘 window
        const uint64_t start = written - n;
        for (uint64_t i = 0; i < n; ++i)
            frame_[static_cast<size_t>(i)] =
                ring_[static_cast<size_t>((start + i) & ringMask_)].load(std::memory_order_relaxed) *
                window_[static_cast<size_t>(i)];

        // 讀完再檢查 producer 有沒有在這段期間繞一圈覆寫掉我們讀的區域。
        // 要看的是 writeEnd_（「已經開始寫到哪裡」），不是 writePos_（「已經寫完到哪裡」）：
        // 一個還在進行中的 pushSamples() 可能已經覆寫了我們剛讀的格子，但它的 writePos_ 要等整個 push 結束才更新。
        // 為什麼讀到 writeEnd_ 就一定夠新？如果上面某個 relaxed load 讀到了某次 push 寫進來的新值，
        // 那次 push 的 release fence（在寫 sample 之前）就和下面的 acquire fence 同步（C++ 的 fence-fence synchronization），
        // 所以那次 push 對 writeEnd_ 的 store 一定看得到（seqlock 的標準寫法）。
        std::atomic_thread_fence(std::memory_order_acquire);
        const uint64_t writeEnd = writeEnd_.load(std::memory_order_relaxed);
        if (writeEnd - start > static_cast<uint64_t>(ringCapacity_))
            return false; // 讀的區域可能已被覆寫，這一 frame 作廢（下次再試）

        lastFramePos_ = written;

        fft_.forwardReal(frame_.data(), spectrum_.data());
        const int bins = fft_.getNumRealBins();
        magnitudesDb.resize(static_cast<size_t>(bins)); // UI thread，可以 allocate
        for (int k = 0; k < bins; ++k) {
            const float scale = (k == 0 || k == bins - 1) ? normalization_ : 2.0f * normalization_;
            const float magnitude = std::abs(spectrum_[static_cast<size_t>(k)]) * scale;
            float& s = smoothed_[static_cast<size_t>(k)];
            // 第一個 frame 直接採用（否則平滑狀態從 0 開始，頭幾個 frame 會讀得太低）
            s = primed_ ? smoothing_ * s + (1.0f - smoothing_) * magnitude : magnitude;
            magnitudesDb[static_cast<size_t>(k)] = gainToDb(s);
        }
        primed_ = true;
        return true;
    }

    double getBinFrequency(int bin) const noexcept { return fftSize_ > 0 ? bin * sampleRate_ / fftSize_ : 0.0; }
    double getBinWidthHz() const noexcept { return fftSize_ > 0 ? sampleRate_ / fftSize_ : 0.0; }
    int getFftSize() const noexcept { return fftSize_; }
    int getNumBins() const noexcept { return fftSize_ / 2 + 1; }
    int getHopSize() const noexcept { return hop_; }
    double getSampleRate() const noexcept { return sampleRate_; }
    WindowType getWindowType() const noexcept { return windowType_; }
    float getSmoothing() const noexcept { return smoothing_; }

    // 目前 ring 裡「還沒被任何 frame 用過」的新 sample 數（除錯 / 教學用）
    uint64_t getPendingSamples() const noexcept { return writePos_.load(std::memory_order_acquire) - lastFramePos_; }

private:
    // producer 的兩段式寫入（seqlock 風格）：
    //   beginWrite：先公告「我要寫到 pos + n」（writeEnd_），再放一個 release fence，之後才寫 sample；
    //   endWrite  ：sample 寫完，writePos_.store(release) 公告「資料已經可以讀到 pos + n」。
    // 只有 audio thread 會呼叫，所以讀自己寫的 writePos_ 用 relaxed 即可。
    uint64_t beginWrite(int n) noexcept {
        const uint64_t pos = writePos_.load(std::memory_order_relaxed);
        writeEnd_.store(pos + static_cast<uint64_t>(n), std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release); // 讓 writeEnd_ 排在之後所有 sample 寫入的前面
        return pos;
    }
    void endWrite(uint64_t pos, int n) noexcept {
        writePos_.store(pos + static_cast<uint64_t>(n), std::memory_order_release); // 之前的 sample 寫入對 acquire 端可見
    }

    void rebuildWindow() {
        Fft::makeWindow(window_, fftSize_, windowType_);
        const double cg = Fft::coherentGain(window_.data(), fftSize_);
        normalization_ = static_cast<float>(1.0 / (fftSize_ * cg)); // 乘 2 的部分在 computeSpectrum 處理
    }

    void updateHop() noexcept {
        hop_ = std::max(1, static_cast<int>(std::lround(fftSize_ * (1.0 - static_cast<double>(overlap_)))));
    }

    double sampleRate_ = 0.0;
    int fftSize_ = 0;
    int ringCapacity_ = 0;
    uint64_t ringMask_ = 0;
    int hop_ = 1;
    float overlap_ = 0.5f;
    float smoothing_ = 0.8f;
    float normalization_ = 1.0f;
    WindowType windowType_ = WindowType::Hann;

    Fft fft_;
    std::unique_ptr<std::atomic<float>[]> ring_;
    std::atomic<uint64_t> writePos_{0}; // 已經寫完（可以讀）的 sample 總數
    std::atomic<uint64_t> writeEnd_{0}; // 已經「開始寫」到的位置（≥ writePos_；push 進行中時 > writePos_）
    uint64_t lastFramePos_ = 0;
    bool primed_ = false;
    std::vector<float> window_, frame_, smoothed_;
    std::vector<std::complex<float>> spectrum_;
};

} // namespace dawlab::dsp
