// l02_cutoff_modulation：快速調變 cutoff 時，biquad vs TPT state-variable filter。
//
// 示範什麼：
//   - 用 6 Hz 的 LFO 讓 lowpass 的 cutoff 在 200 Hz ~ 8 kHz 之間快速來回（像 auto-wah）。
//   - Biquad 的係數只能在 block 之間換（每 512 samples ≈ 10.7 ms 一次），cutoff 是階梯狀前進：
//     resonance 峰停在錯的頻率、一格一格跳（[B2] 量化它和理想平滑調變的差異），聽起來有顆粒感。
//     [B3] 另外實測 cutoff 瞬間大跳時的過衝：TDF-II 與 SVF 其實都不大（TDF-II 的狀態和輸出同量級）。
//   - StateVariableFilter（TPT）的 cutoff 可以每個 sample 更新（一次 tan），而且狀態是物理量，
//     調變時平順得多。
//   - 兩者的靜態頻率響應完全一樣（最後用 measureGainDb 驗證），差別只在「動起來」的時候。
//
// 怎麼執行：不帶參數直接執行。輸出 WAV（dawlab_output/）：
//   l02_mod_dry.wav、l02_mod_biquad_block.wav、l02_mod_svf_sample.wav
// 該聽到什麼：兩者都是 wah-wah；biquad_block 的 resonance 峰是「一格一格跳」的（每 10.7 ms 跳一次，
//   LFO 最快時一步接近一個 octave），聽起來有顆粒感；svf_sample 則是連續的滑動。
//   peak / RMS 兩者差不多，差異要用 [B2] 的「與理想平滑調變的差」才量得出來。
#include <dawlab/Common.h>
#include <dawlab/dsp/Biquad.h>
#include <dawlab/dsp/StateVariableFilter.h>

#include <cstdio>
#include <vector>

using namespace dawlab;
using dawlab::dsp::Biquad;
using dawlab::dsp::BiquadCoefficients;
using dawlab::dsp::StateVariableFilter;

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr double kLfoHz = 6.0;
constexpr double kQ = 6.0;

// LFO：在 log 頻率軸上以正弦擺動的 cutoff
static double cutoffAt(int sampleIndex) {
    const double lfo = std::sin(kTwoPi * kLfoHz * sampleIndex / kSampleRate); // -1..1
    return 200.0 * std::pow(40.0, 0.5 + 0.5 * lfo);                             // 200 Hz..8 kHz
}

int main() {
    std::printf("=== 第 02 章：cutoff 快速調變（LFO %.0f Hz, 200 Hz..8 kHz, Q = %.0f）===\n\n", kLfoHz, kQ);

    AudioBuffer dry = makePluckSequence(kSampleRate, 4.0, 0.35f, 2);
    writeWav(outputPath("l02_mod_dry.wav"), dry, kSampleRate);
    const int total = dry.getNumSamples();

    // ---- A. biquad：每個 block 更新一次係數 ------------------------------------------
    AudioBuffer outBiquad = dry;
    {
        Biquad bq;
        bq.prepare(kSampleRate, kBlockSize, 2);
        AudioBlock whole(outBiquad);
        for (int start = 0; start < total; start += kBlockSize) {
            const int len = std::min(kBlockSize, total - start);
            bq.setCoefficients(BiquadCoefficients::makeLowpass(kSampleRate, cutoffAt(start), kQ));
            bq.process(whole.getSubBlock(start, len));
        }
    }

    // ---- B. SVF：每個 sample 更新 cutoff ---------------------------------------------
    AudioBuffer outSvf = dry;
    {
        StateVariableFilter svf;
        svf.prepare(kSampleRate, kBlockSize, 2);
        svf.setType(StateVariableFilter::Type::Lowpass);
        svf.setQ(static_cast<float>(kQ));
        for (int i = 0; i < total; ++i) {
            svf.setCutoff(static_cast<float>(cutoffAt(i)));
            for (int ch = 0; ch < 2; ++ch)
                outSvf.setSample(ch, i, svf.processSample(ch, outSvf.getSample(ch, i)));
        }
    }

    std::printf("    %-22s  peak %7.2f dBFS   RMS %7.2f dB\n", "dry", gainToDb(peakAbs(dry)), gainToDb(rms(dry, 0)));
    std::printf("    %-22s  peak %7.2f dBFS   RMS %7.2f dB\n", "biquad (per-block)", gainToDb(peakAbs(outBiquad)),
                gainToDb(rms(outBiquad, 0)));
    std::printf("    %-22s  peak %7.2f dBFS   RMS %7.2f dB\n", "SVF (per-sample)", gainToDb(peakAbs(outSvf)),
                gainToDb(rms(outSvf, 0)));
    DAWLAB_CHECK(!hasNanOrInf(outBiquad) && !hasNanOrInf(outSvf));
    writeWav(outputPath("l02_mod_biquad_block.wav"), outBiquad, kSampleRate, 32);
    writeWav(outputPath("l02_mod_svf_sample.wav"), outSvf, kSampleRate, 32);

    // 「block 之間跳係數」的 transient 有多大？量輸出的「一階差分」峰值（sample 之間的最大跳動）。
    // 兩者都是同樣的 LFO，平滑度差異會直接反映在這個數字上。
    auto maxStep = [](const AudioBuffer& b) {
        float m = 0.0f;
        for (int i = 1; i < b.getNumSamples(); ++i)
            m = std::max(m, std::abs(b.getSample(0, i) - b.getSample(0, i - 1)));
        return m;
    };
    std::printf("\n    sample 間最大跳動：biquad %.4f，SVF %.4f（pluck 本身的 transient 主導了這個數字，看不出差別）\n",
                maxStep(outBiquad), maxStep(outSvf));

    // ---- B2. 用穩態訊號量「階梯式 cutoff」帶來的差異 ---------------------------------------
    // 輸入：110 Hz 的 band-limited saw（諧波到 8 kHz），沒有自己的 transient。
    // 以「每個 sample 都重算 RBJ 係數的 biquad」當作理想的平滑調變，量：
    //   - per-block biquad 與它的差（= 階梯式 cutoff 造成的 zipper 成分，RMS dB re. 訊號）
    //   - per-sample SVF 與它的差（兩種結構在調變中的狀態語意不同，但都平滑）
    std::printf("\n[B2] 穩態 saw 110 Hz，以 per-sample biquad 為基準的差異（RMS，dB re. 輸出）\n");
    std::vector<float> saw(static_cast<size_t>(48000 * 2));
    for (size_t i = 0; i < saw.size(); ++i) {
        double v = 0.0;
        for (int h = 1; h * 110.0 < 8000.0; ++h)
            v += std::sin(kTwoPi * 110.0 * h * static_cast<double>(i) / kSampleRate) / h;
        saw[i] = static_cast<float>(0.3 * v);
    }
    {
        Biquad perBlock, perSample;
        perBlock.prepare(kSampleRate, kBlockSize, 1);
        perSample.prepare(kSampleRate, kBlockSize, 1);
        StateVariableFilter svfMod;
        svfMod.prepare(kSampleRate, kBlockSize, 1);
        svfMod.setQ(static_cast<float>(kQ));
        double refPow = 0.0, errBlock = 0.0, errSvf = 0.0;
        for (size_t i = 0; i < saw.size(); ++i) {
            const int n = static_cast<int>(i);
            if (n % kBlockSize == 0)
                perBlock.setCoefficients(BiquadCoefficients::makeLowpass(kSampleRate, cutoffAt(n), kQ));
            perSample.setCoefficients(BiquadCoefficients::makeLowpass(kSampleRate, cutoffAt(n), kQ));
            svfMod.setCutoff(static_cast<float>(cutoffAt(n)));
            const double yBlock = perBlock.processSample(0, saw[i]);
            const double yRef = perSample.processSample(0, saw[i]);
            const double ySvf = svfMod.processSample(0, saw[i]);
            if (n >= 4800) { // 跳過開頭 0.1 s
                refPow += yRef * yRef;
                errBlock += (yBlock - yRef) * (yBlock - yRef);
                errSvf += (ySvf - yRef) * (ySvf - yRef);
            }
        }
        const double blockDb = 10.0 * std::log10(errBlock / refPow);
        const double svfDb = 10.0 * std::log10(errSvf / refPow);
        std::printf("    biquad per-block（每 %d samples 換一次係數）   %7.2f dB\n", kBlockSize, blockDb);
        std::printf("    SVF per-sample                                %7.2f dB\n", svfDb);
        std::printf("    → block 階梯造成的差異幾乎和訊號本身一樣大（resonance 峰停在錯的頻率上）；\n"
                    "      per-sample SVF 與 per-sample biquad 接近得多（剩下的差異來自調變中兩種結構的狀態語意不同）。\n");
        DAWLAB_CHECK(blockDb > -30.0);
        DAWLAB_CHECK(svfDb < blockDb - 10.0);
    }

    // ---- B3. cutoff 瞬間大跳（200 Hz → 8 kHz）：換係數後的過衝 ---------------------------
    // 穩態 saw 先以 cutoff 200 Hz 跑 0.5 s，然後一個 sample 內跳到 8 kHz。
    // 比較「跳之後 20 ms 內的峰值」與「8 kHz 穩態的峰值」：越接近 1 代表狀態與新係數越相容。
    std::printf("\n[B3] cutoff 200 Hz → 8 kHz 瞬間跳：跳後 20 ms 內的峰值 / 8 kHz 穩態峰值\n");
    {
        auto overshoot = [&](auto&& setCutoff, auto&& processOne) {
            const int jumpAt = 24000, window = 960;
            float peakAfter = 0.0f, peakSteady = 0.0f;
            setCutoff(200.0);
            for (size_t i = 0; i < saw.size(); ++i) {
                const int n = static_cast<int>(i);
                if (n == jumpAt)
                    setCutoff(8000.0);
                const float y = std::abs(processOne(saw[i]));
                if (n >= jumpAt && n < jumpAt + window)
                    peakAfter = std::max(peakAfter, y);
                if (n >= jumpAt + 24000)
                    peakSteady = std::max(peakSteady, y);
            }
            return peakAfter / peakSteady;
        };
        Biquad bqJump;
        bqJump.prepare(kSampleRate, kBlockSize, 1);
        StateVariableFilter svfJump;
        svfJump.prepare(kSampleRate, kBlockSize, 1);
        svfJump.setQ(static_cast<float>(kQ));
        const double rBq = overshoot(
            [&](double fc) { bqJump.setCoefficients(BiquadCoefficients::makeLowpass(kSampleRate, fc, kQ)); },
            [&](float x) { return bqJump.processSample(0, x); });
        const double rSvf = overshoot([&](double fc) { svfJump.setCutoff(static_cast<float>(fc)); },
                                      [&](float x) { return svfJump.processSample(0, x); });
        std::printf("    biquad (TDF-II)  %.3f\n    SVF (TPT)        %.3f\n", rBq, rSvf);
        std::printf("    → 兩者都沒有爆衝（TDF-II 的狀態與輸出同量級，見 l02_direct_forms）；\n"
                    "      SVF 的優勢在於每個 sample 更新只要一次 tan，而且同一組狀態同時給 LP/BP/HP/Notch。\n");
        DAWLAB_CHECK(rBq < 2.0 && rSvf < 2.0);
    }

    // ---- C. 靜態響應一致 ----------------------------------------------------------------
    std::printf("\n[C] 靜態響應（cutoff 固定 1 kHz, Q = %.0f）：biquad 與 SVF 應該完全一樣\n", kQ);
    Biquad bq;
    bq.prepare(kSampleRate, kBlockSize, 1);
    bq.setCoefficients(BiquadCoefficients::makeLowpass(kSampleRate, 1000.0, kQ));
    StateVariableFilter svf;
    svf.prepare(kSampleRate, kBlockSize, 1);
    svf.setQ(static_cast<float>(kQ));
    svf.setCutoff(1000.0f);
    std::printf("    %8s  %10s  %10s\n", "f (Hz)", "biquad dB", "SVF dB");
    for (double f : {100.0, 500.0, 900.0, 1000.0, 1100.0, 2000.0, 8000.0}) {
        bq.reset();
        svf.reset();
        const double a = measureGainDb([&](float x) { return bq.processSample(0, x); }, f, kSampleRate, 0.5, 0.5);
        const double b = measureGainDb([&](float x) { return svf.processSample(0, x); }, f, kSampleRate, 0.5, 0.5);
        std::printf("    %8.0f  %10.2f  %10.2f\n", f, a, b);
        DAWLAB_CHECK_NEAR(a, b, 0.05);
    }
    // SVF 的 highpass / bandpass / notch 也對應 RBJ 的同名 filter
    for (auto [type, coeffs] : {
             std::pair{StateVariableFilter::Type::Highpass, BiquadCoefficients::makeHighpass(kSampleRate, 1000.0, 2.0)},
             std::pair{StateVariableFilter::Type::Bandpass, BiquadCoefficients::makeBandpass(kSampleRate, 1000.0, 2.0)},
             std::pair{StateVariableFilter::Type::Notch, BiquadCoefficients::makeNotch(kSampleRate, 1000.0, 2.0)}}) {
        svf.setType(type);
        svf.setQ(2.0f);
        for (double f : {300.0, 1000.0, 3000.0}) {
            svf.reset();
            const double meas = measureGainDb([&](float x) { return svf.processSample(0, x); }, f, kSampleRate, 0.5, 0.5);
            const double theory = coeffs.magnitudeDb(f, kSampleRate);
            if (theory > -50.0)
                DAWLAB_CHECK_NEAR(meas, theory, 0.1);
            else
                DAWLAB_CHECK(meas < -50.0);
        }
    }
    std::printf("    （SVF 的 Highpass / Bandpass / Notch 也與 RBJ 同名 filter 一致，見 self-check）\n\n");

    return dawlab::test::summary("l02_cutoff_modulation");
}
