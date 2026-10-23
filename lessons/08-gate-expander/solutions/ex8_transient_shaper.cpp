// 練習 8 解答：自己寫 transient shaper，並驗證它「與 level 無關」
//
// 示範什麼：
//   1. mapToGainDb()：README 2.6.2 節的 d → gain 映射（|d| = 6 dB 飽和）
//   2. nextGain()：fast / slow 兩個 EnvelopeFollower → d → 映射 → 0.5 ms one-pole 平滑
//   3. 和 dawlab::dsp::TransientShaper 逐 sample 比對（應該一模一樣）
//   4. level-independent 測試：同一段鼓分別以 -6 dBFS 與 -26 dBFS 的 peak 送進去
//        attack +6 dB：兩者的 peak 增量相等（± 0.3 dB）
//        sustain -12 dB：兩者的尾音 RMS 都下降，而且下降量相等
//      對照組：compressor（threshold -20 dB、4:1）對兩個 level 的 peak 變化差很多 → 它看的是絕對 level
// 怎麼執行：l08_ex8_solution（不需要參數，用 makeDrumLoop() 產生鼓組 loop）
// 輸出檔案：dawlab_output/l08_ex8_loud_dry.wav、l08_ex8_loud_attack6.wav、l08_ex8_loud_sustain-12.wav、
//          l08_ex8_quiet_dry.wav、l08_ex8_quiet_attack6.wav、l08_ex8_quiet_sustain-12.wav
// 該聽到什麼：quiet 版本整體小 20 dB，但 attack +6 / sustain -12 的「效果」和 loud 版本聽起來一樣。

#include <dawlab/Common.h>
#include <dawlab/dsp/Compressor.h>
#include <dawlab/dsp/EnvelopeFollower.h>
#include <dawlab/dsp/TransientShaper.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

using namespace dawlab;
using dawlab::dsp::DetectorMode;
using dawlab::dsp::EnvelopeFollower;

namespace {

constexpr int kBlockSize = 512;
constexpr double kSampleRate = 48000.0;

class MyTransientShaper {
public:
    static constexpr float kFastAttackMs = 0.5f;
    static constexpr float kFastReleaseMs = 30.0f;
    static constexpr float kSlowAttackMs = 10.0f;
    static constexpr float kSlowReleaseMs = 150.0f;
    static constexpr float kFullScaleDb = 6.0f;
    static constexpr float kFloorDb = -80.0f;
    static constexpr float kGainSmoothMs = 0.5f;

    // README 2.6.2：
    //   d > 0：gainDb = attackDb  · min(d / 6, 1)
    //   d ≤ 0：gainDb = sustainDb · min(−d / 6, 1)
    static float mapToGainDb(float d, float attackDb, float sustainDb) noexcept {
        if (d > 0.0f)
            return attackDb * std::min(d / kFullScaleDb, 1.0f);
        return sustainDb * std::min(-d / kFullScaleDb, 1.0f);
    }

    void prepare(double sampleRate, int /*maxBlockSize*/, int /*numChannels*/) {
        fast_.prepare(sampleRate);
        fast_.setMode(DetectorMode::Peak);
        fast_.setAttackMs(kFastAttackMs);
        fast_.setReleaseMs(kFastReleaseMs);
        slow_.prepare(sampleRate);
        slow_.setMode(DetectorMode::Peak);
        slow_.setAttackMs(kSlowAttackMs);
        slow_.setReleaseMs(kSlowReleaseMs);
        // one-pole 平滑係數 α = e^{−1/(τ·fs)}
        smoothCoef_ = static_cast<float>(std::exp(-1.0 / (kGainSmoothMs * 0.001 * sampleRate)));
        reset();
    }
    void reset() noexcept {
        fast_.reset();
        slow_.reset();
        gainDb_ = 0.0f;
    }
    void setAttackDb(float db) noexcept { attackDb_ = std::clamp(db, -24.0f, 24.0f); }
    void setSustainDb(float db) noexcept { sustainDb_ = std::clamp(db, -24.0f, 24.0f); }

    float nextGain(float detectorInput) noexcept {
        const float fast = fast_.processSample(detectorInput);
        const float slow = slow_.processSample(detectorInput);
        const float slowDb = gainToDb(slow);
        float target = 0.0f;
        if (slowDb > kFloorDb) {
            // d = 20·log10(fast / slow)：只看「比例」，所以整段訊號放大或縮小 d 都不變
            const float d = gainToDb(fast) - slowDb;
            target = mapToGainDb(d, attackDb_, sustainDb_);
        }
        gainDb_ = target + smoothCoef_ * (gainDb_ - target); // 平滑 gain（dB 域）
        return dbToGain(gainDb_);
    }

    void process(AudioBlock block) noexcept {
        const int n = block.getNumSamples();
        const int nch = block.getNumChannels();
        for (int i = 0; i < n; ++i) {
            float det = 0.0f;
            for (int ch = 0; ch < nch; ++ch)
                det = std::max(det, std::abs(block.getChannelPointer(ch)[i]));
            const float g = nextGain(det);
            for (int ch = 0; ch < nch; ++ch)
                block.getChannelPointer(ch)[i] *= g;
        }
    }

private:
    EnvelopeFollower fast_, slow_;
    float attackDb_ = 0.0f;
    float sustainDb_ = 0.0f;
    float smoothCoef_ = 0.0f;
    float gainDb_ = 0.0f;
};

template <typename Processor>
AudioBuffer runBlocks(const AudioBuffer& input, Processor& p) {
    AudioBuffer out = input;
    AudioBlock whole(out);
    const int n = out.getNumSamples();
    for (int start = 0; start < n; start += kBlockSize)
        p.process(whole.getSubBlock(start, std::min(kBlockSize, n - start)));
    return out;
}

AudioBuffer runShaper(const AudioBuffer& input, float attackDb, float sustainDb) {
    MyTransientShaper ts;
    ts.prepare(kSampleRate, kBlockSize, input.getNumChannels());
    ts.setAttackDb(attackDb);
    ts.setSustainDb(sustainDb);
    return runBlocks(input, ts);
}

// 把鼓 loop 正規化成指定的 peak（dBFS）
AudioBuffer scaledTo(const AudioBuffer& b, float peakDb) {
    AudioBuffer out = b;
    const float g = dbToGain(peakDb) / peakAbs(b);
    for (int ch = 0; ch < out.getNumChannels(); ++ch)
        for (int i = 0; i < out.getNumSamples(); ++i)
            out.setSample(ch, i, out.getSample(ch, i) * g);
    return out;
}

// 「尾音」：makeDrumLoop 以八分音符（120 BPM → 250 ms）為一格，每一格的 120–240 ms 是前一下的尾巴
float tailRmsDb(const AudioBuffer& b) {
    const int step = secondsToSamples(0.25, kSampleRate);
    const int from = msToSamples(120.0, kSampleRate);
    const int to = msToSamples(240.0, kSampleRate);
    double sum = 0.0;
    long long count = 0;
    for (int s = 0; s + to <= b.getNumSamples(); s += step)
        for (int i = s + from; i < s + to; ++i) {
            const double x = b.getSample(0, i);
            sum += x * x;
            ++count;
        }
    return gainToDb(static_cast<float>(std::sqrt(sum / static_cast<double>(std::max(1LL, count)))));
}

float peakDb(const AudioBuffer& b) { return gainToDb(peakAbs(b)); }

} // namespace

int main() {
    std::printf("第 08 章 練習 8（解答）：transient shaper 與 level independence\n");

    // ---- 1. 映射函式 ---------------------------------------------------------------
    std::printf("\n[1] d → gain 映射（attack +12、sustain -12）\n");
    DAWLAB_CHECK_NEAR(MyTransientShaper::mapToGainDb(3.0f, 12.0f, -12.0f), 6.0, 1.0e-5);
    DAWLAB_CHECK_NEAR(MyTransientShaper::mapToGainDb(6.0f, 12.0f, -12.0f), 12.0, 1.0e-5);
    DAWLAB_CHECK_NEAR(MyTransientShaper::mapToGainDb(20.0f, 12.0f, -12.0f), 12.0, 1.0e-5); // 飽和
    DAWLAB_CHECK_NEAR(MyTransientShaper::mapToGainDb(-3.0f, 12.0f, -12.0f), -6.0, 1.0e-5);
    DAWLAB_CHECK_NEAR(MyTransientShaper::mapToGainDb(-20.0f, 12.0f, -12.0f), -12.0, 1.0e-5);
    DAWLAB_CHECK_NEAR(MyTransientShaper::mapToGainDb(0.0f, 12.0f, -12.0f), 0.0, 1.0e-6);
    DAWLAB_CHECK_NEAR(MyTransientShaper::mapToGainDb(-1.5f, 12.0f, 6.0f), 1.5, 1.0e-5); // sustain + 也可以
    for (const float d : {-12.0f, -6.0f, -3.0f, 0.0f, 3.0f, 6.0f, 12.0f})
        std::printf("    d = %+6.1f dB → %+6.2f dB\n", d, MyTransientShaper::mapToGainDb(d, 12.0f, -12.0f));

    const AudioBuffer drums = makeDrumLoop(kSampleRate);
    const AudioBuffer loud = scaledTo(drums, -6.0f);
    const AudioBuffer quiet = scaledTo(drums, -26.0f);

    // ---- 2. 和 dsp::TransientShaper 比對 -------------------------------------------
    {
        dawlab::dsp::TransientShaper ref;
        ref.prepare(kSampleRate, kBlockSize, loud.getNumChannels());
        ref.setAttackDb(6.0f);
        ref.setSustainDb(-12.0f);
        const AudioBuffer expected = runBlocks(loud, ref);
        const AudioBuffer mine = runShaper(loud, 6.0f, -12.0f);
        float maxErr = 0.0f;
        for (int ch = 0; ch < mine.getNumChannels(); ++ch)
            for (int i = 0; i < mine.getNumSamples(); ++i)
                maxErr = std::max(maxErr, std::abs(mine.getSample(ch, i) - expected.getSample(ch, i)));
        std::printf("\n[2] 與 dawlab::dsp::TransientShaper 逐 sample 比對：最大誤差 %.3g\n", maxErr);
        DAWLAB_CHECK(maxErr < 1.0e-5f);
    }

    // ---- 3. Level independence -----------------------------------------------------
    const AudioBuffer loudAtk = runShaper(loud, 6.0f, 0.0f);
    const AudioBuffer quietAtk = runShaper(quiet, 6.0f, 0.0f);
    const AudioBuffer loudSus = runShaper(loud, 0.0f, -12.0f);
    const AudioBuffer quietSus = runShaper(quiet, 0.0f, -12.0f);

    writeWav(outputPath("l08_ex8_loud_dry.wav"), loud, kSampleRate, 32);
    writeWav(outputPath("l08_ex8_loud_attack6.wav"), loudAtk, kSampleRate, 32);
    writeWav(outputPath("l08_ex8_loud_sustain-12.wav"), loudSus, kSampleRate, 32);
    writeWav(outputPath("l08_ex8_quiet_dry.wav"), quiet, kSampleRate, 32);
    writeWav(outputPath("l08_ex8_quiet_attack6.wav"), quietAtk, kSampleRate, 32);
    writeWav(outputPath("l08_ex8_quiet_sustain-12.wav"), quietSus, kSampleRate, 32);

    const float loudPeakGain = peakDb(loudAtk) - peakDb(loud);
    const float quietPeakGain = peakDb(quietAtk) - peakDb(quiet);
    const float loudTailDrop = tailRmsDb(loudSus) - tailRmsDb(loud);
    const float quietTailDrop = tailRmsDb(quietSus) - tailRmsDb(quiet);

    std::printf("\n[3] level independence（同一段鼓，peak -6 dBFS 與 -26 dBFS）\n");
    std::printf("                         loud(-6)   quiet(-26)\n");
    std::printf("    attack +6：peak 增量   %+6.2f     %+6.2f dB\n", loudPeakGain, quietPeakGain);
    std::printf("    sustain -12：尾音 RMS  %+6.2f     %+6.2f dB\n", loudTailDrop, quietTailDrop);
    DAWLAB_CHECK(loudPeakGain > 3.0f);                        // attack 真的有把 peak 推上去
    DAWLAB_CHECK_NEAR(loudPeakGain, quietPeakGain, 0.3);      // 而且與 level 無關
    DAWLAB_CHECK(loudTailDrop < -3.0f);                       // sustain -12 讓尾音變小
    DAWLAB_CHECK(quietTailDrop < -3.0f);
    DAWLAB_CHECK_NEAR(loudTailDrop, quietTailDrop, 0.3);

    // ---- 4. 對照組：compressor 看絕對 level ------------------------------------------
    auto compressPeakChange = [](const AudioBuffer& in) {
        dawlab::dsp::Compressor c;
        c.prepare(kSampleRate, kBlockSize, in.getNumChannels());
        c.setThresholdDb(-20.0f);
        c.setRatio(4.0f);
        c.setAttackMs(1.0f);
        c.setReleaseMs(100.0f);
        const AudioBuffer out = runBlocks(in, c);
        return peakDb(out) - peakDb(in);
    };
    const float loudComp = compressPeakChange(loud);
    const float quietComp = compressPeakChange(quiet);
    std::printf("\n[4] 對照組：compressor（threshold -20、4:1）的 peak 變化\n");
    std::printf("    loud %+6.2f dB、quiet %+6.2f dB → 差 %.2f dB：compressor 只壓得到大聲的那一份\n", loudComp,
                quietComp, std::abs(loudComp - quietComp));
    DAWLAB_CHECK(std::abs(loudComp - quietComp) > 3.0f);

    std::printf("\n輸出：dawlab_output/l08_ex8_{loud,quiet}_{dry,attack6,sustain-12}.wav\n");
    return dawlab::test::summary("l08_ex8_transient_shaper");
}
