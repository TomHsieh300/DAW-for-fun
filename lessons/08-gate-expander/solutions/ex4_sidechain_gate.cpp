// 練習 4 解答：sidechain —— 用 kick 軌控制 pad 軌（ducking 與 trance gate）
//
// (a) Ducker：偵測 sidechain（kick）的 envelope，高於 threshold 時把 main（pad）壓低 duckDb，
//     gain 在 dB 域用 attack / release 平滑（和第 04 章 compressor 的 log-domain 平滑同一個概念）。
// (b) Trance gate：直接用 Gate::process(main, sidechain)，pad 只在 kick 出現時通過。
//
// 自我檢查：
//   - ducking 期間（kick 後 15–60 ms）pad 的位準比原本低 duckDb ± 1 dB
//   - kick 之間（kick 後 350–490 ms）pad 回到原本位準 ± 1 dB
//   - trance gate：kick 後 20–100 ms pad 幾乎原封不動（± 1 dB），kick 後 350–490 ms pad RMS < -60 dBFS
// 執行：l08_ex4_solution → 輸出 dawlab_output/l08_ex4_pad.wav、l08_ex4_kick.wav、l08_ex4_ducked.wav、
//      l08_ex4_trance_gate.wav，並印出 PASS / FAIL。

#include <dawlab/Common.h>
#include <dawlab/dsp/EnvelopeFollower.h>
#include <dawlab/dsp/Gate.h>

#include <cstdio>

using namespace dawlab;
using dawlab::dsp::AttackReleaseFilter;
using dawlab::dsp::DetectorMode;
using dawlab::dsp::EnvelopeFollower;
using dawlab::dsp::Gate;

namespace {

constexpr int kBlockSize = 512;
constexpr double kSampleRate = 48000.0;
constexpr double kKickPeriodSec = 0.5; // 120 BPM，每拍一個 kick

// ---- (a) Ducker --------------------------------------------------------------
class Ducker {
public:
    void prepare(double sampleRate, int /*maxBlockSize*/, int /*numChannels*/) {
        detector_.prepare(sampleRate);
        detector_.setMode(DetectorMode::Peak);
        detector_.setAttackMs(1.0f);
        detector_.setReleaseMs(detectorReleaseMs_);
        smoother_.prepare(sampleRate);
        smoother_.setAttackMs(attackMs_);
        smoother_.setReleaseMs(releaseMs_);
        reset();
    }
    void reset() noexcept {
        detector_.reset();
        smoother_.reset(); // gain reduction 從 0 dB 開始
    }
    void setThresholdDb(float db) noexcept { thresholdDb_ = db; }
    void setDuckDb(float db) noexcept { duckDb_ = std::abs(db); }
    void setAttackMs(float ms) noexcept {
        attackMs_ = ms;
        smoother_.setAttackMs(ms);
    }
    void setReleaseMs(float ms) noexcept {
        releaseMs_ = ms;
        smoother_.setReleaseMs(ms);
    }

    // 餵入 sidechain 的偵測 sample，回傳要乘到 main 的線性 gain
    float nextGain(float sidechainSample) noexcept {
        const float envDb = gainToDb(detector_.processSample(sidechainSample));
        // 靜態曲線：高於 threshold → 壓 duckDb；否則 0 dB（這是「∞:1 的 gate 式 ducking」，也可以改成比例式）
        const float targetGr = envDb >= thresholdDb_ ? duckDb_ : 0.0f;
        // 在 dB 域平滑：GR 上升用 attack、下降用 release（AttackReleaseFilter 的 Branching 模式剛好是這個語意）
        const float gr = smoother_.process(targetGr);
        return dbToGain(-gr);
    }

    void process(AudioBlock main, const AudioBlock& sidechain) noexcept {
        const int n = std::min(main.getNumSamples(), sidechain.getNumSamples());
        for (int i = 0; i < n; ++i) {
            float det = 0.0f;
            for (int ch = 0; ch < sidechain.getNumChannels(); ++ch)
                det = std::max(det, std::abs(sidechain.getChannelPointer(ch)[i]));
            const float g = nextGain(det);
            for (int ch = 0; ch < main.getNumChannels(); ++ch)
                main.getChannelPointer(ch)[i] *= g;
        }
    }

private:
    EnvelopeFollower detector_;
    AttackReleaseFilter smoother_;
    float thresholdDb_ = -20.0f;
    float duckDb_ = 12.0f;
    float attackMs_ = 5.0f;
    float releaseMs_ = 50.0f;
    float detectorReleaseMs_ = 30.0f;
};

// ---- 測試素材 ---------------------------------------------------------------
// pad：220 Hz + 330 Hz 兩個正弦（完全五度），持續整段
AudioBuffer makePad(double seconds) {
    AudioBuffer b(2, secondsToSamples(seconds, kSampleRate));
    for (int i = 0; i < b.getNumSamples(); ++i) {
        const auto v = static_cast<float>(0.2 * std::sin(kTwoPi * 220.0 * i / kSampleRate) +
                                          0.2 * std::sin(kTwoPi * 330.0 * i / kSampleRate));
        b.setSample(0, i, v);
        b.setSample(1, i, v);
    }
    return b;
}

// kick：每 0.5 秒一個，音高 120 → 45 Hz，振幅 e^(-14 t)，長 0.3 秒
AudioBuffer makeKicks(double seconds) {
    AudioBuffer b(1, secondsToSamples(seconds, kSampleRate));
    const int len = secondsToSamples(0.3, kSampleRate);
    for (double t0 = 0.0; t0 < seconds; t0 += kKickPeriodSec) {
        const int start = secondsToSamples(t0, kSampleRate);
        double phase = 0.0;
        for (int i = 0; i < len && start + i < b.getNumSamples(); ++i) {
            const double s = i / kSampleRate;
            const double f = 45.0 + 75.0 * std::exp(-s * 30.0);
            phase += kTwoPi * f / kSampleRate;
            b.addSample(0, start + i, static_cast<float>(0.9 * std::sin(phase) * std::exp(-s * 14.0)));
        }
    }
    return b;
}

template <typename Fn>
AudioBuffer runBlocks(const AudioBuffer& main, const AudioBuffer& sidechain, Fn&& processBlock) {
    AudioBuffer out = main;
    AudioBlock whole(out);
    AudioBlock sc(const_cast<AudioBuffer&>(sidechain)); // 只讀，const_cast 只是為了取得 view
    const int n = out.getNumSamples();
    for (int start = 0; start < n; start += kBlockSize) {
        const int len = std::min(kBlockSize, n - start);
        processBlock(whole.getSubBlock(start, len), sc.getSubBlock(start, len));
    }
    return out;
}

// 從 kick 起點算起 [fromMs, toMs) 的 RMS（dB），對第 2..5 個 kick 取平均
double windowRmsDb(const AudioBuffer& b, double fromMs, double toMs) {
    double sum = 0.0;
    int count = 0;
    for (int k = 1; k <= 4; ++k) {
        const int start = secondsToSamples(k * kKickPeriodSec + fromMs * 0.001, kSampleRate);
        const int n = secondsToSamples((toMs - fromMs) * 0.001, kSampleRate);
        const float r = rms(b.getReadPointer(0) + start, n);
        sum += r > 0.0f ? 20.0 * std::log10(r) : -120.0;
        ++count;
    }
    return sum / count;
}

} // namespace

int main() {
    std::printf("第 08 章 練習 4：sidechain ducking 與 trance gate\n");
    const double seconds = 3.0;
    const AudioBuffer pad = makePad(seconds);
    const AudioBuffer kicks = makeKicks(seconds);
    writeWav(outputPath("l08_ex4_pad.wav"), pad, kSampleRate);
    writeWav(outputPath("l08_ex4_kick.wav"), kicks, kSampleRate);

    // ---- (a) ducking ----
    Ducker ducker;
    ducker.prepare(kSampleRate, kBlockSize, 2);
    ducker.setThresholdDb(-20.0f);
    ducker.setDuckDb(12.0f);
    ducker.setAttackMs(5.0f);
    ducker.setReleaseMs(50.0f);
    const AudioBuffer ducked = runBlocks(pad, kicks, [&](AudioBlock m, AudioBlock s) { ducker.process(m, s); });
    writeWav(outputPath("l08_ex4_ducked.wav"), ducked, kSampleRate);

    const double padRef = windowRmsDb(pad, 15.0, 60.0);
    const double duckDuring = windowRmsDb(ducked, 15.0, 60.0);
    const double duckBetween = windowRmsDb(ducked, 350.0, 490.0);
    std::printf("\n[a] ducking：pad 原本 %.2f dBFS；kick 後 15–60 ms %.2f dBFS（應低 12 dB）；"
                "kick 後 350–490 ms %.2f dBFS（應回到原本）\n",
                padRef, duckDuring, duckBetween);
    DAWLAB_CHECK_NEAR(padRef - duckDuring, 12.0, 1.0);
    DAWLAB_CHECK_NEAR(duckBetween, padRef, 1.0);

    // ---- (b) trance gate ----
    Gate gate;
    gate.prepare(kSampleRate, kBlockSize, 2);
    gate.setThresholdDb(-20.0f);
    gate.setHysteresisDb(6.0f);
    gate.setRangeDb(-80.0f);
    gate.setAttackMs(1.0f);
    gate.setHoldMs(50.0f);
    gate.setReleaseMs(50.0f);
    const AudioBuffer gated = runBlocks(pad, kicks, [&](AudioBlock m, AudioBlock s) { gate.process(m, s); });
    writeWav(outputPath("l08_ex4_trance_gate.wav"), gated, kSampleRate);

    const double gateOpen = windowRmsDb(gated, 20.0, 100.0);
    const double gateClosed = windowRmsDb(gated, 350.0, 490.0);
    std::printf("[b] trance gate：kick 後 20–100 ms %.2f dBFS（應 ≈ %.2f）；kick 後 350–490 ms %.2f dBFS（應 < -60）\n",
                gateOpen, windowRmsDb(pad, 20.0, 100.0), gateClosed);
    DAWLAB_CHECK_NEAR(gateOpen, windowRmsDb(pad, 20.0, 100.0), 1.0);
    DAWLAB_CHECK(gateClosed < -60.0);
    DAWLAB_CHECK(!hasNanOrInf(ducked) && !hasNanOrInf(gated));

    return dawlab::test::summary("l08_ex4_sidechain_gate");
}
