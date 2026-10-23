// 練習 6 起始碼：用 FFT 做 overlap-add 快速卷積（為第 12 章 convolution reverb 鋪路）
//
// 直接卷積（direct convolution）：y[n] = Σ_{k=0}^{L-1} h[k] · x[n-k]
//   每個輸出 sample 要 L 次乘加。L = 48000（1 秒的 IR）時，每秒要 48000 × 48000 ≈ 23 億次乘加 —— 太慢。
//
// 卷積定理（convolution theorem）：時域的卷積 = 頻域的相乘
//   y = x * h   ⇔   Y[k] = X[k] · H[k]
// 但 DFT 的乘積對應的是「循環卷積」（circular convolution）：超出長度 N 的部分會繞回開頭疊加。
// 解法：把 x 的一段（長 B）和 h（長 L）都補 0 到 N ≥ B + L − 1，循環卷積就等於線性卷積（沒有東西會繞回來）。
//
// Overlap-add：把輸入切成長 B 的 block，每塊單獨做「補 0 → FFT → 乘 H → IFFT」，
// 得到長 B + L − 1 的結果；前 B 個 sample 加上「上一塊留下的尾巴」就是這一塊的輸出，
// 剩下的 L − 1 個 sample 存起來（tail）疊加到後面的 block。
//
//   block 0: [ x0 x0 x0 x0 | 0 0 0 0 0 0 ] ─FFT·H·IFFT─▶ [ y y y y | t t t t t t ]
//   block 1:               [ x1 x1 x1 x1 | 0 0 0 0 0 0 ] ─▶       [ y y y y | t t t t t t ]
//                                         ↑ 前一塊的尾巴疊加在這裡（overlap-add）
//
// 每個 block 的成本：一次 forwardReal + 一次 inverseReal（各 O(N log N)）+ N/2+1 次複數乘法。
// 換算到每個 sample 約 O((N/B) log N)，L 很長時比直接卷積的 O(L) 快非常多。
//
// 這個單一 partition 的版本 latency 是 0（每來一個 block 就輸出一個 block），但 IR 很長時 N 很大，
// 每個 block 的運算量很不平均（第 12 章的 uniform partitioned convolution 會把 IR 切成很多段來解決）。
//
// 你的任務：完成 OverlapAddConvolver::process()。prepare() 已經算好 FFT 大小與 IR 的頻譜 irSpectrum_。
// 完成前執行會印出 FAIL（這是預期的）。
// 執行：bin/Debug/l11_ex6_starter.exe（解答：solutions/ex6_fft_convolution.cpp）
// 輸出：dawlab_output/l11_ex6_convolved.wav（pluck sequence 經過一段合成的 0.25 s 「房間」IR）

#include <dawlab/Common.h>
#include <dawlab/dsp/Fft.h>

#include <algorithm>
#include <chrono>
#include <complex>
#include <cstdio>
#include <vector>

using namespace dawlab;
using dawlab::dsp::Fft;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

class OverlapAddConvolver {
public:
    // 會 allocate（非 audio thread）：決定 FFT 大小、預先算好 IR 的頻譜
    void prepare(const std::vector<float>& impulseResponse, int maxBlockSize) {
        irLength_ = static_cast<int>(impulseResponse.size());
        blockSize_ = maxBlockSize;
        // N = 大於等於 B + L − 1 的 2 的冪次
        int order = 1;
        while ((1 << order) < blockSize_ + irLength_ - 1)
            ++order;
        fft_.setOrder(order);
        const int n = fft_.getSize();
        const int bins = fft_.getNumRealBins();

        std::vector<float> padded(static_cast<size_t>(n), 0.0f);
        std::copy(impulseResponse.begin(), impulseResponse.end(), padded.begin());
        irSpectrum_.assign(static_cast<size_t>(bins), {0.0f, 0.0f});
        fft_.forwardReal(padded.data(), irSpectrum_.data()); // H[k]：只算一次

        timeBuffer_.assign(static_cast<size_t>(n), 0.0f);
        spectrum_.assign(static_cast<size_t>(bins), {0.0f, 0.0f});
        overlap_.assign(static_cast<size_t>(n), 0.0f);
    }

    void reset() noexcept { std::fill(overlap_.begin(), overlap_.end(), 0.0f); }

    int getFftSize() const noexcept { return fft_.getSize(); }

    // audio thread：in-place 處理一個 block（長度 <= maxBlockSize），不 allocate
    void process(float* data, int numSamples) noexcept {
        // TODO：
        //   1. 把 data 的 numSamples 個 sample 複製到 timeBuffer_ 開頭，其餘補 0
        //   2. fft_.forwardReal(timeBuffer_ → spectrum_)，逐 bin 乘上 irSpectrum_，再 fft_.inverseReal 回 timeBuffer_
        //   3. overlap-add：把 timeBuffer_ 整段（N 個）加到 overlap_
        //   4. overlap_ 的前 numSamples 個就是輸出，複製回 data
        //   5. overlap_ 往前移 numSamples 格，尾端補 0
        (void)data;
        (void)numSamples;
    }

private:
    int irLength_ = 0;
    int blockSize_ = 0;
    Fft fft_;
    std::vector<std::complex<float>> irSpectrum_, spectrum_;
    std::vector<float> timeBuffer_, overlap_;
};

// 直接卷積（標準答案）：y[n] = Σ h[k] x[n−k]，輸出長度與輸入相同（尾巴截掉）
std::vector<float> directConvolve(const float* x, int n, const std::vector<float>& h) {
    std::vector<float> y(static_cast<size_t>(n), 0.0f);
    const int l = static_cast<int>(h.size());
    for (int i = 0; i < n; ++i) {
        double acc = 0.0;
        const int kMax = std::min(l - 1, i);
        for (int k = 0; k <= kMax; ++k)
            acc += static_cast<double>(h[static_cast<size_t>(k)]) * x[i - k];
        y[static_cast<size_t>(i)] = static_cast<float>(acc);
    }
    return y;
}

// 合成的 IR：指數衰減的白雜訊（像一個很小的房間），能量正規化成 1
std::vector<float> makeNoiseIr(int length, double rt60Seconds, uint32_t seed) {
    std::vector<float> h(static_cast<size_t>(length));
    NoiseGenerator noise(seed);
    // RT60：衰減 60 dB 的時間 → 振幅每秒乘 10^(−3/rt60)
    const double decayPerSample = std::pow(10.0, -3.0 / (rt60Seconds * kSampleRate));
    double g = 1.0, energy = 0.0;
    for (float& v : h) {
        v = static_cast<float>(noise.next() * g);
        g *= decayPerSample;
        energy += static_cast<double>(v) * v;
    }
    const auto norm = static_cast<float>(1.0 / std::sqrt(energy));
    for (float& v : h)
        v *= norm;
    return h;
}

float maxAbsDiff(const std::vector<float>& a, const float* b, int n) {
    float e = 0.0f;
    for (int i = 0; i < n; ++i)
        e = std::max(e, std::abs(a[static_cast<size_t>(i)] - b[i]));
    return e;
}

} // namespace

int main() {
    using Clock = std::chrono::steady_clock;

    // ---- 1. 正確性：小 IR、各種 block 長度（含不足 maxBlockSize 的最後一塊）----------------------------
    {
        const std::vector<float> h = {0.5f, -0.25f, 0.125f, 0.0f, 0.1f};
        AudioBuffer x = makeWhiteNoise(kSampleRate, 0.05, 0.5f, 1, 3);
        const int n = x.getNumSamples();
        const std::vector<float> reference = directConvolve(x.getReadPointer(0), n, h);
        OverlapAddConvolver conv;
        conv.prepare(h, 64);
        std::vector<float> y(x.getReadPointer(0), x.getReadPointer(0) + n);
        // 故意用長短不一的 block：37、64、1、64、…（host 的 callback 長度本來就可能變）
        const int pattern[] = {37, 64, 1, 64, 50};
        int pos = 0, p = 0;
        while (pos < n) {
            const int len = std::min(pattern[p++ % 5], n - pos);
            conv.process(y.data() + pos, len);
            pos += len;
        }
        const float err = maxAbsDiff(reference, y.data(), n);
        std::printf("5-tap IR、block 長度不固定：FFT 大小 = %d，與直接卷積最大誤差 = %.3e\n", conv.getFftSize(), err);
        DAWLAB_CHECK(err < 1e-5f);
    }

    // ---- 2. impulse 的響應 = IR 本身 -----------------------------------------------------------------
    {
        const std::vector<float> h = makeNoiseIr(1000, 0.05, 11);
        OverlapAddConvolver conv;
        conv.prepare(h, kBlockSize);
        AudioBuffer imp = makeImpulse(4096, 1, 0, 1.0f);
        for (int start = 0; start < imp.getNumSamples(); start += kBlockSize)
            conv.process(imp.getWritePointer(0) + start, std::min(kBlockSize, imp.getNumSamples() - start));
        float err = 0.0f;
        for (int i = 0; i < 4096; ++i) {
            const float expected = i < 1000 ? h[static_cast<size_t>(i)] : 0.0f;
            err = std::max(err, std::abs(imp.getSample(0, i) - expected));
        }
        std::printf("impulse → 輸出等於 IR：最大誤差 = %.3e\n", err);
        DAWLAB_CHECK(err < 1e-5f);
    }

    // ---- 3. 實際素材 + 計時：0.25 s 的 IR（12000 taps）------------------------------------------------
    {
        const int irLen = 12000;
        const std::vector<float> h = makeNoiseIr(irLen, 0.2, 7);
        AudioBuffer dry = makePluckSequence(kSampleRate, 2.0, 0.4f, 1);
        const int n = dry.getNumSamples();

        auto t0 = Clock::now();
        const std::vector<float> reference = directConvolve(dry.getReadPointer(0), n, h);
        const double directMs = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();

        // 用兩種 block 長度跑同一個 IR：
        //   B = 512  ：N = 16384，每 512 個 sample 就要做一次 16384 點的 FFT + IFFT → 大部分運算浪費在補的 0 上
        //   B = 4096 ：N 一樣是 16384，但每 4096 個 sample 才做一次 → 每個 sample 的成本少 8 倍
        // 結論：單一 partition 的 overlap-add 要 B 與 L 同一個數量級才划算；但 audio callback 的 B 很小，
        // 而且 B 大 = latency 大。第 12 章的 uniform partitioned convolution 把 IR 切成很多段長 B 的小段，
        // 兩個問題一起解決。
        AudioBuffer wet = dry;
        double fftMs[2] = {0.0, 0.0};
        float err[2] = {0.0f, 0.0f};
        int fftSize[2] = {0, 0};
        const int blockSizes[2] = {kBlockSize, 4096};
        for (int v = 0; v < 2; ++v) {
            OverlapAddConvolver conv;
            conv.prepare(h, blockSizes[v]);
            fftSize[v] = conv.getFftSize();
            wet = dry;
            t0 = Clock::now();
            for (int start = 0; start < n; start += blockSizes[v])
                conv.process(wet.getWritePointer(0) + start, std::min(blockSizes[v], n - start));
            fftMs[v] = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
            err[v] = maxAbsDiff(reference, wet.getReadPointer(0), n);
        }

        const float refPeak = peakAbs(reference.data(), n);
        std::printf("IR %d taps，%d 個 sample（Debug 組態只看相對快慢）：\n", irLen, n);
        std::printf("  直接卷積                        %8.1f ms\n", directMs);
        for (int v = 0; v < 2; ++v)
            std::printf("  overlap-add B = %4d、N = %5d   %8.1f ms（直接卷積的 %.2f 倍時間），最大誤差 %.2e（%.0f dB）\n",
                        blockSizes[v], fftSize[v], fftMs[v], fftMs[v] / directMs, err[v],
                        gainToDb(err[v] / refPeak));
        DAWLAB_CHECK(err[0] / refPeak < 1e-4f); // float 運算，誤差比訊號低 80 dB 以上
        DAWLAB_CHECK(err[1] / refPeak < 1e-4f);
        DAWLAB_CHECK(fftMs[1] < directMs * 0.5); // 大 block 時明顯比直接卷積快
        DAWLAB_CHECK(fftMs[1] < fftMs[0]);       // 同一個 N，block 越大每個 sample 越便宜

        // 乾濕混合後輸出，聽起來像在小房間裡撥弦
        AudioBuffer out(2, n);
        for (int i = 0; i < n; ++i) {
            const float v = 0.7f * dry.getSample(0, i) + 0.5f * wet.getSample(0, i);
            out.setSample(0, i, v);
            out.setSample(1, i, v);
        }
        writeWav(outputPath("l11_ex6_convolved.wav"), out, kSampleRate);
        std::printf("→ dawlab_output/l11_ex6_convolved.wav\n");
    }

    return dawlab::test::summary("l11_ex6");
}
