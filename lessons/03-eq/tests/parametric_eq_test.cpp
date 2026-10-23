// ParametricEq 的回歸測試（self-test）
//
// 這支不是範例，也不是練習解答，而是本章 DSP 元件 dawlab::dsp::ParametricEq 的「合約測試」：
// 把 README 與 authoring guide 說的保證（理論曲線 = 實測、參數平滑的時間正確、
// 換 type / 開關不 click、smoothing = 0 時也會套用新參數……）全部用數字驗證。
// 後面的章節（第 13 章 plugin、第 14 章 DAW）直接使用 ParametricEq，改 header 之後先跑這支。
//
// 怎麼執行：l03_test_parametric_eq（不需參數；ctest label: solution）
// 輸出：只有 console 文字；全部通過時最後一行是 [PASS]。

#include <dawlab/Common.h>
#include <dawlab/dsp/ParametricEq.h>

#include <cstdio>
#include <vector>

using namespace dawlab;
using namespace dawlab::dsp;

namespace {

constexpr double kSr = 48000.0;

// ---- 1. 平滑時間：不論 block size、host 多常重送參數，ramp 長度都要 ≈ smoothingSeconds · sr ----
//
// 量法：同一組參數呼叫送進兩個一模一樣的 EQ，一個吃 sin、一個吃 cos（同頻率）。
// 對（慢慢變化的）filter 來說 sqrt(ys² + yc²) 就是「這一瞬間的增益」，不必等一個週期。
// Peak @ 1 kHz 的 gain 從 0 dB 線性（在 dB 域）走到 +12 dB，ramp 20 ms = 960 samples，
// 所以增益應在第 ~480 個 sample 越過 +6 dB（加上最多 32 個 sample 的係數更新延遲與 filter 本身的延遲）。
enum class Resend { None, SameValues, OtherBandChanges };

int samplesToHalfGain(int blockSize, Resend resend) {
    ParametricEq eqS, eqC;
    for (ParametricEq* eq : {&eqS, &eqC}) {
        eq->prepare(kSr, 512, 1);
        eq->setBand(0, {EqBandType::Peak, 1000.0f, 1.0f, 0.0f, true});
        eq->setBand(1, {EqBandType::Peak, 5000.0f, 1.0f, 0.0f, true});
        eq->reset();
    }
    const int total = 4800;
    std::vector<float> s(static_cast<size_t>(total)), c(static_cast<size_t>(total));
    for (int i = 0; i < total; ++i) {
        s[static_cast<size_t>(i)] = static_cast<float>(std::sin(kTwoPi * 1000.0 * i / kSr));
        c[static_cast<size_t>(i)] = static_cast<float>(std::cos(kTwoPi * 1000.0 * i / kSr));
    }
    // 先跑 100 個 sample 讓 tick 的相位不在 0（模擬播放到一半才改參數）
    const int startAt = 100;
    int crossed = -1;
    for (int start = 0, block = 0; start < total; start += blockSize, ++block) {
        const int len = std::min(blockSize, total - start);
        for (ParametricEq* eq : {&eqS, &eqC}) {
            if (start >= startAt && start - blockSize < startAt)
                eq->setBandGainDb(0, 12.0f); // 只在這一刻改一次目標
            else if (start > startAt && resend == Resend::SameValues)
                eq->setBand(0, eq->getBand(0)); // host 每個 block 開頭重送同一組參數
            else if (start > startAt && resend == Resend::OtherBandChanges)
                eq->setBandGainDb(1, (block % 2) ? 0.001f : 0.0f); // 別的 band 一直在變 → 每個 block 都強迫 tick
        }
        float* ps = s.data() + start;
        float* pc = c.data() + start;
        eqS.process(AudioBlock(&ps, 1, len));
        eqC.process(AudioBlock(&pc, 1, len));
    }
    int changeAt = -1;
    for (int start = 0; start < total; start += blockSize)
        if (start >= startAt) {
            changeAt = start;
            break;
        }
    for (int i = changeAt; i < total; ++i) {
        const double amp = std::sqrt(double(s[size_t(i)]) * s[size_t(i)] + double(c[size_t(i)]) * c[size_t(i)]);
        if (gainToDb(static_cast<float>(amp)) >= 6.0f) {
            crossed = i - changeAt;
            break;
        }
    }
    return crossed;
}

void testRampLength() {
    std::printf("[1] ramp 長度（Peak 1 kHz 0 -> +12 dB，20 ms = 960 samples；越過 +6 dB 應在 ~480 samples）\n");
    std::printf("    block   不重送   每 block 重送同值   別的 band 每 block 改值\n");
    for (int block : {1, 8, 16, 48, 512}) {
        const int a = samplesToHalfGain(block, Resend::None);
        const int b = samplesToHalfGain(block, Resend::SameValues);
        const int c = samplesToHalfGain(block, Resend::OtherBandChanges);
        std::printf("    %5d   %6d   %12d        %12d\n", block, a, b, c);
        for (int v : {a, b, c}) {
            DAWLAB_CHECK(v >= 470);
            DAWLAB_CHECK(v <= 540);
        }
    }
}

// ---- 2. smoothing = 0（或短到不到一個 sample）時，改參數也要真的套用到 filter ----
void testZeroSmoothing() {
    std::printf("[2] smoothing 0：改參數後，量到的響應要等於 getMagnitudeDb\n");
    for (double seconds : {0.0, 0.000005}) {
        ParametricEq eq;
        eq.setSmoothingSeconds(seconds);
        eq.prepare(kSr, 512, 1);
        eq.setBand(0, {EqBandType::Peak, 1000.0f, 1.0f, 12.0f, true});
        AudioBuffer buf = makeSine(440.0, kSr, 0.1, 0.25f, 1);
        eq.process(buf.toBlock());
        eq.setBandFrequency(0, 4000.0f);
        eq.setBandGainDb(0, -12.0f);
        eq.process(buf.toBlock()); // 不呼叫 reset()：reset 會順手重算係數，藏住 bug
        for (double f : {1000.0, 4000.0}) {
            const double m = measureGainDb([&](float x) { return eq.processSample(0, x); }, f, kSr);
            std::printf("    smoothing %.6f s  %5.0f Hz：實測 %+7.3f dB，getMagnitudeDb %+7.3f dB\n", seconds, f, m,
                        eq.getMagnitudeDb(f));
            DAWLAB_CHECK_NEAR(m, eq.getMagnitudeDb(f), 0.05);
        }
        // 換 type 也一樣（smoothing 0 時 mix 也是瞬間切換）
        eq.setBandType(0, EqBandType::LowShelf);
        eq.process(buf.toBlock());
        const double m = measureGainDb([&](float x) { return eq.processSample(0, x); }, 200.0, kSr);
        DAWLAB_CHECK_NEAR(m, eq.getMagnitudeDb(200.0), 0.05);
        DAWLAB_CHECK_NEAR(eq.getMagnitudeDb(200.0), -12.0, 0.2);
    }
}

// ---- 3. 理論曲線 = 實測（六個 band 全開，含 24 dB/oct） ----
void testCurveMatchesMeasurement() {
    std::printf("[3] 六個 band 全開：getMagnitudeDb 與正弦波實測\n");
    ParametricEq eq;
    eq.prepare(kSr, 512, 1);
    eq.setBand(0, {EqBandType::LowCut, 60.0f, 0.707f, 0.0f, true});
    eq.setSlope(0, EqSlope::Db24);
    eq.setBand(1, {EqBandType::LowShelf, 120.0f, 0.707f, 3.0f, true});
    eq.setBand(2, {EqBandType::Peak, 300.0f, 1.5f, -4.0f, true});
    eq.setBand(3, {EqBandType::Notch, 3000.0f, 4.0f, 0.0f, true});
    eq.setBand(4, {EqBandType::HighShelf, 10000.0f, 0.707f, 2.0f, true});
    eq.setBand(5, {EqBandType::HighCut, 18000.0f, 0.707f, 0.0f, true});
    eq.reset();
    double maxDiff = 0.0;
    for (double f : {30.0, 60.0, 120.0, 300.0, 1000.0, 2500.0, 5000.0, 10000.0, 16000.0, 18000.0}) {
        eq.reset();
        const double m = measureGainDb([&](float x) { return eq.processSample(0, x); }, f, kSr);
        maxDiff = std::max(maxDiff, std::abs(m - eq.getMagnitudeDb(f)));
    }
    std::printf("    最大差異 %.4f dB\n", maxDiff);
    DAWLAB_CHECK(maxDiff < 0.05);

    // 24 dB/oct 的 cut 在 fc 是 -3.01 dB（兩顆 Butterworth 相乘），12 dB/oct 也是
    ParametricEq cut;
    cut.prepare(kSr, 512, 1);
    for (EqSlope slope : {EqSlope::Db12, EqSlope::Db24}) {
        cut.setBand(0, {EqBandType::HighCut, 2000.0f, 0.707f, 0.0f, true});
        cut.setSlope(0, slope);
        cut.reset();
        const double m = measureGainDb([&](float x) { return cut.processSample(0, x); }, 2000.0, kSr);
        std::printf("    HighCut 2 kHz %s：fc 處實測 %+.3f dB\n", slope == EqSlope::Db24 ? "24 dB/oct" : "12 dB/oct", m);
        DAWLAB_CHECK_NEAR(m, -3.0103, 0.03);
        DAWLAB_CHECK_NEAR(cut.getMagnitudeDb(2000.0), -3.0103, 0.01);
    }
}

// ---- 4. 換 type 來回切換之後，穩態響應仍等於 getMagnitudeDb ----
void testTypeSwitching() {
    std::printf("[4] 播放中來回換 type / slope / 開關，最後的穩態響應 = getMagnitudeDb\n");
    ParametricEq eq;
    eq.prepare(kSr, 64, 1);
    AudioBuffer noise = makeWhiteNoise(kSr, 0.5, 0.1f, 1);
    const EqBandType seq[] = {EqBandType::Peak, EqBandType::Notch, EqBandType::Peak, EqBandType::LowShelf,
                              EqBandType::HighCut, EqBandType::LowCut};
    eq.setBand(2, {EqBandType::Peak, 800.0f, 2.0f, 9.0f, true});
    int k = 0;
    AudioBlock whole = noise.toBlock();
    for (int start = 0; start + 64 <= noise.getNumSamples(); start += 64, ++k) {
        if (k % 7 == 0) // 每 7 個 block（約 9 ms，比 40 ms 的切換短）就換一次：排隊中的切換也要正確收尾
            eq.setBandType(2, seq[(k / 7) % 6]);
        if (k % 11 == 0)
            eq.setSlope(2, (k / 11) % 2 ? EqSlope::Db24 : EqSlope::Db12);
        if (k % 13 == 0)
            eq.setBandEnabled(2, (k / 13) % 2 == 0);
        eq.process(whole.getSubBlock(start, 64));
    }
    DAWLAB_CHECK(!hasNanOrInf(noise));
    eq.setBand(2, {EqBandType::Peak, 800.0f, 2.0f, 9.0f, true});
    eq.process(whole.getSubBlock(0, 4096)); // 讓 40 ms 的切換走完
    for (double f : {200.0, 800.0, 3000.0}) {
        const double m = measureGainDb([&](float x) { return eq.processSample(0, x); }, f, kSr);
        std::printf("    %5.0f Hz：實測 %+7.3f dB，getMagnitudeDb %+7.3f dB\n", f, m, eq.getMagnitudeDb(f));
        DAWLAB_CHECK_NEAR(m, eq.getMagnitudeDb(f), 0.05);
    }
    DAWLAB_CHECK_NEAR(eq.getMagnitudeDb(800.0), 9.0, 0.01);
}

// ---- 5. 在 prepare() 之前設定的頻率不會被 48 kHz 的上限永久夾掉 ----
void testFrequencyBeforePrepare() {
    std::printf("[5] prepare 之前設定 30 kHz 的 HighCut，再 prepare(96 kHz)\n");
    ParametricEq eq;
    eq.setBand(5, {EqBandType::HighCut, 30000.0f, 0.707f, 0.0f, true}); // 例如 plugin 在 prepareToPlay 前還原狀態
    eq.prepare(96000.0, 512, 1);
    std::printf("    getBand(5).frequency = %.0f Hz\n", eq.getBand(5).frequency);
    DAWLAB_CHECK_NEAR(eq.getBand(5).frequency, 30000.0, 0.5);
    DAWLAB_CHECK_NEAR(eq.getMagnitudeDb(30000.0), -3.0103, 0.01);
    const double m = measureGainDb([&](float x) { return eq.processSample(0, x); }, 30000.0, 96000.0);
    DAWLAB_CHECK_NEAR(m, -3.0103, 0.05);
    // 換回 48 kHz：實際生效的頻率是 0.49·fs，但使用者的設定值保留
    eq.prepare(48000.0, 512, 1);
    DAWLAB_CHECK_NEAR(eq.getBand(5).frequency, 30000.0, 0.5);
    const double m48 = measureGainDb([&](float x) { return eq.processSample(0, x); }, 10000.0, 48000.0);
    DAWLAB_CHECK_NEAR(m48, eq.getMagnitudeDb(10000.0), 0.05);
    // NaN 欄位被忽略
    eq.setBandFrequency(5, std::nanf(""));
    DAWLAB_CHECK_NEAR(eq.getBand(5).frequency, 30000.0, 0.5);
}

// ---- 6. 開關 band、換 type 不 click：正弦波預測殘差 ----
float maxSineResidual(const AudioBuffer& y, double freq) {
    const float* d = y.getReadPointer(0);
    const double c = 2.0 * std::cos(kTwoPi * freq / kSr);
    double m = 0.0;
    for (int i = 2; i < y.getNumSamples(); ++i)
        m = std::max(m, std::abs(d[i] - c * d[i - 1] + d[i - 2]));
    return static_cast<float>(m);
}

void testNoClicks() {
    std::printf("[6] 開關 band、換 type 時的正弦波預測殘差（1 kHz、振幅 0.25）\n");
    AudioBuffer y = makeSine(1000.0, kSr, 0.6, 0.25f, 1);
    const float dry = maxSineResidual(y, 1000.0);
    ParametricEq eq;
    eq.prepare(kSr, 512, 1);
    AudioBlock whole = y.toBlock();
    for (int start = 0, block = 0; start < y.getNumSamples(); start += 128, ++block) {
        if (block == 20)
            eq.setBand(0, {EqBandType::LowCut, 2000.0f, 0.707f, 0.0f, true});
        if (block == 80)
            eq.setBandType(0, EqBandType::Peak);
        if (block == 150)
            eq.setBandEnabled(0, false);
        eq.process(whole.getSubBlock(start, std::min(128, y.getNumSamples() - start)));
    }
    const float wet = maxSineResidual(y, 1000.0);
    std::printf("    原始 %.6f，ParametricEq %.6f\n", dry, wet);
    DAWLAB_CHECK(wet < 0.004f); // naive 直接切換約 0.01–0.03（範例 3）
}

} // namespace

int main() {
    std::printf("=== 第 03 章 ParametricEq 回歸測試 ===\n\n");
    testRampLength();
    testZeroSmoothing();
    testCurveMatchesMeasurement();
    testTypeSwitching();
    testFrequencyBeforePrepare();
    testNoClicks();
    return dawlab::test::summary("l03_test_parametric_eq");
}
