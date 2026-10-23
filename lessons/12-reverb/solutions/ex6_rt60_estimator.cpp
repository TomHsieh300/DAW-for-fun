// 練習 6 解答：Schroeder backward integration 估 RT60
//
// Schroeder (1965)：對 impulse response h[n]，定義 energy decay curve（EDC）
//
//     EDC[n] = Σ_{k=n}^{N-1} h[k]²        （從「現在」到結尾的剩餘能量）
//     EDC_dB[n] = 10·log10(EDC[n] / EDC[0])
//
// 這等於把「無限多個不同起點的衰減曲線」平均起來，所以比直接看 h² 的 envelope 平滑得多
// （一次量測就能得到 ensemble average）。接著在 EDC_dB 的 -5 .. -25 dB 區段做線性回歸
// （T20），斜率 s（dB/s）外插：RT60 = -60 / s。
// 也可以用 -5 .. -35 dB（T30）；區段越長越準，但要求 IR 的 SNR 越好。
//
// 注意：真實錄音的 IR 尾巴有 noise floor，EDC 會在尾端「翹起來」變平；
// 實務上要先估 noise floor、把積分截在雜訊開始的地方（這裡的合成 IR 沒有雜訊，不需要）。

#include <dawlab/Common.h>
#include <dawlab/dsp/ConvolutionReverb.h>
#include <dawlab/dsp/Reverb.h>
#include <dawlab/dsp/ReverbBuildingBlocks.h>

#include <cmath>
#include <cstdio>
#include <vector>

using namespace dawlab;
using namespace dawlab::dsp;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

// EDC（dB，相對總能量）
std::vector<double> schroederDecayDb(const float* h, int n) {
    std::vector<double> edc(static_cast<size_t>(n));
    double acc = 0.0;
    for (int i = n - 1; i >= 0; --i) { // 從尾巴往回累加：EDC[n] = EDC[n+1] + h[n]²
        acc += static_cast<double>(h[i]) * h[i];
        edc[static_cast<size_t>(i)] = acc;
    }
    const double total = edc.empty() ? 1.0 : std::max(edc[0], 1e-30);
    for (double& v : edc)
        v = 10.0 * std::log10(std::max(v, 1e-30) / total);
    return edc;
}

struct Rt60Estimate {
    double rt60 = 0.0;
    double slopeDbPerSecond = 0.0;
    bool valid = false;
};

// 在 EDC_dB 介於 startDb 與 endDb 的區段做線性回歸，外插到 -60 dB
Rt60Estimate estimateRt60(const std::vector<double>& edcDb, double sampleRate, double startDb = -5.0,
                          double endDb = -25.0) {
    Rt60Estimate r;
    double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
    int count = 0;
    for (size_t i = 0; i < edcDb.size(); ++i) {
        const double y = edcDb[i];
        if (y > startDb)
            continue;
        if (y < endDb)
            break;
        const double t = static_cast<double>(i) / sampleRate;
        sx += t;
        sy += y;
        sxx += t * t;
        sxy += t * y;
        ++count;
    }
    if (count < 10)
        return r;
    const double denom = count * sxx - sx * sx;
    if (denom <= 0.0)
        return r;
    r.slopeDbPerSecond = (count * sxy - sx * sy) / denom;
    if (r.slopeDbPerSecond >= 0.0)
        return r;
    r.rt60 = -60.0 / r.slopeDbPerSecond;
    r.valid = true;
    return r;
}

// 理想的指數衰減正弦：envelope = exp(-ln(1000)·t / RT60)，正好 RT60 秒掉 60 dB
AudioBuffer makeDecayingSine(double freq, double rt60, double seconds) {
    AudioBuffer b(1, secondsToSamples(seconds, kSampleRate));
    const double k = std::log(1000.0) / rt60;
    for (int i = 0; i < b.getNumSamples(); ++i) {
        const double t = i / kSampleRate;
        b.setSample(0, i, static_cast<float>(std::exp(-k * t) * std::sin(kTwoPi * freq * t)));
    }
    return b;
}

AudioBuffer fdnImpulseResponse(float rt60, double seconds) {
    Reverb reverb;
    reverb.prepare(kSampleRate, kBlockSize, 2);
    reverb.setMix(1.0f);
    reverb.setDampingHz(20000.0f);
    reverb.setDecaySeconds(rt60);
    reverb.reset(); // 讓 mix = 1 立刻生效：否則第 0 個 sample 會漏出 dry impulse，污染 windowed-RMS 的 peak
    AudioBuffer ir = makeImpulse(secondsToSamples(seconds, kSampleRate), 2);
    for (int start = 0; start < ir.getNumSamples(); start += kBlockSize)
        reverb.process(ir.toBlock().getSubBlock(start, std::min(kBlockSize, ir.getNumSamples() - start)));
    return ir;
}

} // namespace

int main() {
    std::printf("=== 練習 6：Schroeder backward integration 估 RT60 ===\n");

    auto report = [](const char* name, const AudioBuffer& ir, double truth) {
        const std::vector<double> edc = schroederDecayDb(ir.getReadPointer(0), ir.getNumSamples());
        const Rt60Estimate t20 = estimateRt60(edc, kSampleRate, -5.0, -25.0);
        const Rt60Estimate t30 = estimateRt60(edc, kSampleRate, -5.0, -35.0);
        const DecayMeasurement rmsFit = measureRt60(ir.getReadPointer(0), ir.getNumSamples(), kSampleRate, 20.0);
        std::printf("%-28s %10.3f %12.3f %12.3f %14.3f\n", name, truth, t20.rt60, t30.rt60, rmsFit.rt60Seconds);
        return t20;
    };

    // ---- 1. 理想指數衰減：EDC 必須是直線，RT60 幾乎精準 ----------------------------------
    {
        AudioBuffer sine = makeDecayingSine(440.0, 1.0, 1.3);
        const std::vector<double> edc = schroederDecayDb(sine.getReadPointer(0), sine.getNumSamples());
        // 對 e^(-kt) 的 envelope，能量 ∝ e^(-2kt)，EDC_dB(t) = -60·t/RT60：t = 0.5 s 時 -30 dB
        const double at05 = edc[static_cast<size_t>(secondsToSamples(0.5, kSampleRate))];
        const double at025 = edc[static_cast<size_t>(secondsToSamples(0.25, kSampleRate))];
        std::printf("指數衰減正弦的 EDC：t=0.25 s → %.2f dB（理論 -15），t=0.5 s → %.2f dB（理論 -30）\n", at025, at05);
        DAWLAB_CHECK_NEAR(at025, -15.0, 0.5);
        DAWLAB_CHECK_NEAR(at05, -30.0, 0.5);
        // 表頭在 EDC 那一行之後才印，讓表格連在一起
        std::printf("\n%-28s %10s %12s %12s %14s\n", "IR", "真值(s)", "T20(s)", "T30(s)", "windowed-RMS(s)");
        const Rt60Estimate e = report("decaying sine (RT60 1.0)", sine, 1.0);
        DAWLAB_CHECK(e.valid);
        DAWLAB_CHECK_NEAR(e.rt60, 1.0, 0.03);
    }

    // ---- 2. 合成雜訊 IR（ConvolutionReverb::makeSyntheticImpulseResponse）-----------------
    for (double truth : {0.5, 1.5}) {
        AudioBuffer ir = ConvolutionReverb::makeSyntheticImpulseResponse(kSampleRate, truth, 1, 11);
        const Rt60Estimate e = report("synthetic noise IR", ir, truth);
        DAWLAB_CHECK(e.valid);
        DAWLAB_CHECK_NEAR(e.rt60 / truth, 1.0, 0.1); // 10% 以內
    }

    // ---- 3. FDN 的 IR：Schroeder 與 windowed-RMS 回歸應該大致一致 -----------------------
    {
        AudioBuffer ir = fdnImpulseResponse(2.0f, 2.8);
        const Rt60Estimate e = report("FDN Reverb (RT60 2.0)", ir, 2.0);
        const DecayMeasurement rmsFit = measureRt60(ir.getReadPointer(0), ir.getNumSamples(), kSampleRate, 20.0);
        DAWLAB_CHECK(e.valid);
        DAWLAB_CHECK_NEAR(e.rt60 / 2.0, 1.0, 0.15);
        DAWLAB_CHECK_NEAR(e.rt60 / rmsFit.rt60Seconds, 1.0, 0.2);

        // 印 EDC 曲線給人看
        const std::vector<double> edc = schroederDecayDb(ir.getReadPointer(0), ir.getNumSamples());
        std::printf("\nFDN IR 的 EDC（每 0.25 s）：\n");
        for (double t = 0.0; t <= 2.0; t += 0.25)
            std::printf("  t = %.2f s : %7.2f dB\n", t, edc[static_cast<size_t>(secondsToSamples(t, kSampleRate))]);
    }

    return dawlab::test::summary("l12_ex6");
}
