// 練習 6 解答：為 Gate 加上 lookahead，驗證 transient 的頭不會被 attack 削掉
//
// 做法：偵測器看「現在」的訊號，主訊號走一條長 L 的延遲線再乘 gain。
// 這樣門在 transient 真正到達輸出之前就已經開始開（甚至開完），代價是整體延遲 L（host 會用 latency
// compensation 把它補回來，所以 getLatencySamples() 要回報正確的值）。
//
// 自我檢查（997 Hz、-6 dBFS 的 tone burst 從完全靜音中突然開始，gate attack 10 ms、range -80 dB）：
//   - 無 lookahead：burst 前 2 ms 的 RMS 比輸入低超過 10 dB（attack 還在爬，頭被削掉）
//   - lookahead 10 ms：輸出延遲 10 ms 後，burst 前 2 ms 的 RMS 與輸入相差 < 0.5 dB，
//     而且逐 sample 比對 output[burstStart + L + k] ≈ input[burstStart + k]（誤差 < 0.01）
//   - transient 還在延遲線裡：[burstStart, burstStart + L) 的輸出仍是靜音（< -60 dB）
//   - 延遲量正確：burst 穩態區間，output[i + L] 與 input[i] 逐 sample 相等
// 為什麼用 997 Hz 而不是 1 kHz：1 kHz 的週期是 48 samples，剛好整除 L = 480，
// input[i + L] == input[i]，「延遲 L」和「完全沒延遲」逐 sample 比起來一模一樣，檢查就失去意義。
// 997 Hz 在 480 samples 內走 9.97 個週期，差 0.03 週期；延遲差 1 sample 也會差 2π·997/48000 ≈ 0.13 rad。
// 執行：l08_ex6_solution → 輸出 dawlab_output/l08_ex6_input.wav、l08_ex6_no_lookahead.wav、l08_ex6_lookahead.wav。

#include <dawlab/Common.h>
#include <dawlab/dsp/Gate.h>

#include <cstdio>
#include <vector>

using namespace dawlab;
using dawlab::dsp::Gate;

namespace {

constexpr int kBlockSize = 512;
constexpr double kSampleRate = 48000.0;

class LookaheadGate {
public:
    // maxLookaheadMs 決定延遲線長度（只能在 prepare 配置）
    void prepare(double sampleRate, int maxBlockSize, int numChannels, float maxLookaheadMs = 20.0f) {
        sampleRate_ = sampleRate;
        gate_.prepare(sampleRate, maxBlockSize, numChannels);
        // 環形緩衝長度 = 最大延遲 + 1：先寫後讀，延遲 L 的讀取位置不能和寫入位置重疊（L = 長度時會讀到剛寫入的值）
        maxDelay_ = std::max(0, msToSamples(maxLookaheadMs, sampleRate)) + 1;
        ring_.assign(static_cast<size_t>(numChannels) * static_cast<size_t>(maxDelay_), 0.0f);
        numChannels_ = numChannels;
        setLookaheadMs(lookaheadMs_);
        reset();
    }
    void reset() noexcept {
        gate_.reset();
        std::fill(ring_.begin(), ring_.end(), 0.0f);
        writePos_ = 0;
    }
    void setLookaheadMs(float ms) noexcept {
        lookaheadMs_ = std::max(0.0f, ms);
        delaySamples_ = std::min(maxDelay_ - 1, msToSamples(lookaheadMs_, sampleRate_));
    }
    int getLatencySamples() const noexcept { return delaySamples_; }
    Gate& gate() noexcept { return gate_; } // threshold / attack / … 直接設在內部的 Gate 上

    void process(AudioBlock block) noexcept {
        const int n = block.getNumSamples();
        const int nch = std::min(numChannels_, block.getNumChannels());
        for (int i = 0; i < n; ++i) {
            // 1. 偵測用「現在」的 sample
            float det = 0.0f;
            for (int ch = 0; ch < nch; ++ch)
                det = std::max(det, std::abs(block.getChannelPointer(ch)[i]));
            const float g = gate_.nextGain(det);
            // 2. 主訊號：寫進環形緩衝，讀出 delaySamples_ 之前的值，再乘 gain
            const int readPos = (writePos_ - delaySamples_ + maxDelay_) % maxDelay_;
            for (int ch = 0; ch < nch; ++ch) {
                float* d = block.getChannelPointer(ch);
                float* ring = ring_.data() + static_cast<size_t>(ch) * static_cast<size_t>(maxDelay_);
                ring[writePos_] = d[i];
                d[i] = (delaySamples_ > 0 ? ring[readPos] : d[i]) * g;
            }
            writePos_ = (writePos_ + 1) % maxDelay_;
        }
    }

private:
    Gate gate_;
    double sampleRate_ = 48000.0;
    int numChannels_ = 0;
    int maxDelay_ = 1;
    int delaySamples_ = 0;
    int writePos_ = 0;
    float lookaheadMs_ = 0.0f;
    std::vector<float> ring_;
};

constexpr double kBurstHz = 997.0; // 週期不整除 L = 480（見檔頭說明）

// 0.5 s 靜音 → 997 Hz -6 dBFS 的 burst 0.3 s → 0.3 s 靜音
AudioBuffer makeBurst() {
    AudioBuffer b(1, secondsToSamples(1.1, kSampleRate));
    const int start = secondsToSamples(0.5, kSampleRate);
    const int len = secondsToSamples(0.3, kSampleRate);
    for (int i = 0; i < len; ++i)
        b.setSample(0, start + i, dbToGain(-6.0f) * static_cast<float>(std::sin(kTwoPi * kBurstHz * i / kSampleRate)));
    return b;
}

AudioBuffer runBlocks(const AudioBuffer& input, LookaheadGate& g) {
    AudioBuffer out = input;
    AudioBlock whole(out);
    for (int start = 0; start < out.getNumSamples(); start += kBlockSize)
        g.process(whole.getSubBlock(start, std::min(kBlockSize, out.getNumSamples() - start)));
    return out;
}

double rmsDb(const AudioBuffer& b, int start, int n) {
    const float r = rms(b.getReadPointer(0) + start, n);
    return r > 0.0f ? 20.0 * std::log10(r) : -120.0;
}

void setupGate(Gate& gate) {
    gate.setThresholdDb(-30.0f);
    gate.setHysteresisDb(6.0f);
    gate.setRangeDb(-80.0f);
    gate.setAttackMs(10.0f);
    gate.setHoldMs(20.0f);
    gate.setReleaseMs(50.0f);
}

} // namespace

int main() {
    std::printf("第 08 章 練習 6：lookahead gate\n");
    const AudioBuffer input = makeBurst();
    const int burstStart = secondsToSamples(0.5, kSampleRate);
    const int head = msToSamples(2.0, kSampleRate); // 量 burst 的前 2 ms

    LookaheadGate noLa, la;
    noLa.prepare(kSampleRate, kBlockSize, 1);
    setupGate(noLa.gate());
    noLa.setLookaheadMs(0.0f);
    la.prepare(kSampleRate, kBlockSize, 1);
    setupGate(la.gate());
    la.setLookaheadMs(10.0f); // = attack 時間：門剛好在 transient 到達時開完

    const AudioBuffer outNo = runBlocks(input, noLa);
    const AudioBuffer outLa = runBlocks(input, la);
    writeWav(outputPath("l08_ex6_input.wav"), input, kSampleRate);
    writeWav(outputPath("l08_ex6_no_lookahead.wav"), outNo, kSampleRate);
    writeWav(outputPath("l08_ex6_lookahead.wav"), outLa, kSampleRate);

    const int L = la.getLatencySamples();
    std::printf("\nlookahead 10 ms = %d samples（getLatencySamples）\n", L);
    DAWLAB_CHECK(L == msToSamples(10.0, kSampleRate));
    DAWLAB_CHECK(noLa.getLatencySamples() == 0);

    const double inHead = rmsDb(input, burstStart, head);
    const double noHead = rmsDb(outNo, burstStart, head);
    const double laHead = rmsDb(outLa, burstStart + L, head); // 輸出延遲了 L，要往後對齊
    std::printf("burst 前 2 ms 的 RMS：輸入 %.2f dB；無 lookahead %.2f dB（頭被削掉 %.1f dB）；lookahead %.2f dB\n", inHead,
                noHead, inHead - noHead, laHead);
    DAWLAB_CHECK(inHead - noHead > 10.0);
    DAWLAB_CHECK_NEAR(laHead, inHead, 0.5);

    // 頭的逐 sample 比對：只比 RMS 分不出「延遲 L」與「沒有延遲但門剛好開完」，波形本身才分得出來
    float headErr = 0.0f;
    for (int k = 0; k < head; ++k)
        headErr = std::max(headErr, std::abs(outLa.getSample(0, burstStart + L + k) - input.getSample(0, burstStart + k)));
    // transient 還在延遲線裡的那 L 個 sample，輸出應該仍是靜音
    const double preDb = rmsDb(outLa, burstStart, L);
    std::printf("burst 前 2 ms 逐 sample：|output[burstStart + L + k] − input[burstStart + k]| 最大 %.2e；"
                "[burstStart, burstStart + L) 的輸出 RMS %.1f dB\n",
                headErr, preDb);
    DAWLAB_CHECK(headErr < 0.01f);
    DAWLAB_CHECK(preDb < -60.0);

    // 穩態區間：output[i + L] == input[i]（gain = 1、只差延遲）
    float maxErr = 0.0f;
    const int ssStart = burstStart + msToSamples(50.0, kSampleRate);
    const int ssLen = msToSamples(100.0, kSampleRate);
    for (int i = ssStart; i < ssStart + ssLen; ++i)
        maxErr = std::max(maxErr, std::abs(outLa.getSample(0, i + L) - input.getSample(0, i)));
    std::printf("穩態區間 output[i + L] 與 input[i] 的最大誤差：%.2e\n", maxErr);
    DAWLAB_CHECK(maxErr < 1.0e-6f);

    // 靜音段仍然關著（lookahead 不會讓雜訊提早漏出來——這裡沒有雜訊，只確認 0）
    DAWLAB_CHECK(rmsDb(outLa, 0, secondsToSamples(0.45, kSampleRate)) < -100.0);
    DAWLAB_CHECK(!hasNanOrInf(outLa));
    return dawlab::test::summary("l08_ex6_lookahead_gate");
}
