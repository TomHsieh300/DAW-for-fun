// 練習 6 起始碼：為 Gate 加上 lookahead
//
// 要做的事：完成 LookaheadGate::process()——偵測器看「現在」的 sample（gate_.nextGain），
// 主訊號走一條長 delaySamples_ 的環形緩衝再乘 gain。setLookaheadMs / getLatencySamples 已寫好。
// self-check 已經寫好；填完 TODO 後執行 l08_ex6_starter 應該印出 PASS。
// 提示：解答在 solutions/ex6_lookahead_gate.cpp。

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
    Gate& gate() noexcept { return gate_; }

    void process(AudioBlock block) noexcept {
        const int n = block.getNumSamples();
        const int nch = std::min(numChannels_, block.getNumChannels());
        for (int i = 0; i < n; ++i) {
            // TODO：
            //   1. det = max|x| across channels（現在的 sample）→ g = gate_.nextGain(det)
            //   2. readPos = (writePos_ − delaySamples_ + maxDelay_) % maxDelay_
            //   3. 每個 channel：ring[writePos_] = x；輸出 = ring[readPos] * g（delaySamples_ == 0 時輸出 x * g）
            //   4. writePos_ = (writePos_ + 1) % maxDelay_
            for (int ch = 0; ch < nch; ++ch)
                block.getChannelPointer(ch)[i] *= 1.0f;
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

// 997 Hz：週期不整除 L = 480，self-check 才分得出「有延遲」與「沒延遲」（解答檔頭有說明）
constexpr double kBurstHz = 997.0;

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
    std::printf("第 08 章 練習 6（起始碼）：lookahead gate\n");
    const AudioBuffer input = makeBurst();
    const int burstStart = secondsToSamples(0.5, kSampleRate);
    const int head = msToSamples(2.0, kSampleRate);

    LookaheadGate noLa, la;
    noLa.prepare(kSampleRate, kBlockSize, 1);
    setupGate(noLa.gate());
    noLa.setLookaheadMs(0.0f);
    la.prepare(kSampleRate, kBlockSize, 1);
    setupGate(la.gate());
    la.setLookaheadMs(10.0f);

    const AudioBuffer outNo = runBlocks(input, noLa);
    const AudioBuffer outLa = runBlocks(input, la);
    writeWav(outputPath("l08_ex6_input.wav"), input, kSampleRate);
    writeWav(outputPath("l08_ex6_no_lookahead.wav"), outNo, kSampleRate);
    writeWav(outputPath("l08_ex6_lookahead.wav"), outLa, kSampleRate);

    const int L = la.getLatencySamples();
    const double inHead = rmsDb(input, burstStart, head);
    const double noHead = rmsDb(outNo, burstStart, head);
    const double laHead = rmsDb(outLa, burstStart + L, head);
    std::printf("burst 前 2 ms 的 RMS：輸入 %.2f dB；無 lookahead %.2f dB；lookahead %.2f dB\n", inHead, noHead, laHead);
    DAWLAB_CHECK(inHead - noHead > 10.0);
    DAWLAB_CHECK_NEAR(laHead, inHead, 0.5);

    float headErr = 0.0f;
    for (int k = 0; k < head; ++k)
        headErr = std::max(headErr, std::abs(outLa.getSample(0, burstStart + L + k) - input.getSample(0, burstStart + k)));
    const double preDb = rmsDb(outLa, burstStart, L);
    std::printf("頭的逐 sample 最大誤差 %.2e；transient 到達輸出前的 RMS %.1f dB\n", headErr, preDb);
    DAWLAB_CHECK(headErr < 0.01f);
    DAWLAB_CHECK(preDb < -60.0);

    float maxErr = 0.0f;
    const int ssStart = burstStart + msToSamples(50.0, kSampleRate);
    const int ssLen = msToSamples(100.0, kSampleRate);
    for (int i = ssStart; i < ssStart + ssLen; ++i)
        maxErr = std::max(maxErr, std::abs(outLa.getSample(0, i + L) - input.getSample(0, i)));
    DAWLAB_CHECK(maxErr < 1.0e-6f);
    DAWLAB_CHECK(rmsDb(outLa, 0, secondsToSamples(0.45, kSampleRate)) < -100.0);
    return dawlab::test::summary("l08_ex6_starter");
}
