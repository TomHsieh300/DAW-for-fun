// l02_filter_sweep：把白雜訊與 log sweep 送過 LPF / HPF / BPF / Notch，輸出 WAV 試聽。
//
// 示範什麼：
//   - 用 Biquad 一塊一塊（block size 512）處理整段訊號，模擬 audio callback。
//   - 同一個 1 kHz 的 cutoff / center，四種 filter 聽起來差多少。
//   - 用 RMS 數字對照聽感：白雜訊能量均勻分布在 0..24 kHz，LPF 1 kHz 只留下約 1/24 的頻寬，
//     RMS 應該大幅下降；HPF 幾乎不變；BPF 更少；Notch 幾乎不變。
//
// 怎麼執行：不帶參數直接執行；或 l02_filter_sweep 你的.wav（會改用你的檔案當輸入）。
// 輸出 WAV（dawlab_output/）：
//   l02_noise_dry.wav, l02_noise_lpf.wav, l02_noise_hpf.wav, l02_noise_bpf.wav, l02_noise_notch.wav
//   l02_sweep_dry.wav, l02_sweep_lpf.wav, l02_sweep_hpf.wav, l02_sweep_bpf.wav, l02_sweep_notch.wav
// 該聽到什麼：
//   - noise_lpf 像隔著牆聽到的「悶」聲；noise_hpf 只剩「嘶」聲；noise_bpf 像老電話的窄頻；
//     noise_notch 跟 dry 幾乎一樣（只挖掉 1 kHz 附近一小塊）。
//   - sweep 從 20 Hz 掃到 20 kHz：lpf 版在 1 kHz 之後越來越小聲；hpf 相反；bpf 只有經過 1 kHz
//     時最大聲；notch 在經過 1 kHz 的瞬間有個「凹陷」。
#include <dawlab/Common.h>
#include <dawlab/dsp/Biquad.h>

#include <cstdio>
#include <string>
#include <vector>

using namespace dawlab;
using dawlab::dsp::Biquad;
using dawlab::dsp::BiquadCoefficients;

constexpr int kBlockSize = 512;

// 一塊一塊處理（模擬 audio callback），回傳處理後的新 buffer
static AudioBuffer processInBlocks(const AudioBuffer& input, const BiquadCoefficients& coeffs, double sampleRate) {
    AudioBuffer out = input; // 複製一份，在複製上 in-place 處理
    Biquad filter;
    filter.prepare(sampleRate, kBlockSize, out.getNumChannels());
    filter.setCoefficients(coeffs);
    AudioBlock whole(out);
    for (int start = 0; start < out.getNumSamples(); start += kBlockSize) {
        const int len = std::min(kBlockSize, out.getNumSamples() - start);
        filter.process(whole.getSubBlock(start, len));
    }
    return out;
}

int main(int argc, char** argv) {
    double sampleRate = 48000.0;
    AudioBuffer noise, sweep;
    bool haveUserFile = false;

    if (argc > 1) {
        WavData wav;
        std::string err;
        if (readWav(argv[1], wav, &err)) {
            noise = wav.buffer;
            sampleRate = wav.sampleRate;
            haveUserFile = true;
            std::printf("使用輸入檔 %s（%.0f Hz, %d ch, %.2f s）\n", argv[1], sampleRate, noise.getNumChannels(),
                        noise.getNumSamples() / sampleRate);
        } else {
            std::printf("無法讀取 %s：%s，改用合成訊號\n", argv[1], err.c_str());
        }
    }
    if (!haveUserFile)
        noise = makeWhiteNoise(sampleRate, 3.0, 0.25f, 2);
    sweep = makeLogSweep(20.0, 20000.0, sampleRate, 4.0, 0.5f, 2);

    struct Variant {
        const char* name;
        BiquadCoefficients c;
    };
    const double fc = 1000.0;
    const std::vector<Variant> variants = {
        {"lpf", BiquadCoefficients::makeLowpass(sampleRate, fc, BiquadCoefficients::kButterworthQ)},
        {"hpf", BiquadCoefficients::makeHighpass(sampleRate, fc, BiquadCoefficients::kButterworthQ)},
        {"bpf", BiquadCoefficients::makeBandpass(sampleRate, fc, 2.0)},
        {"notch", BiquadCoefficients::makeNotch(sampleRate, fc, 2.0)},
    };

    std::printf("=== 第 02 章：雜訊 / 掃頻通過四種 biquad（fc = %.0f Hz, block = %d）===\n\n", fc, kBlockSize);
    std::printf("    %-8s  %10s  %10s\n", "", "noise RMS", "sweep RMS");
    const float noiseDryRms = rms(noise, 0);
    const float sweepDryRms = rms(sweep, 0);
    std::printf("    %-8s  %7.2f dB  %7.2f dB\n", "dry", gainToDb(noiseDryRms), gainToDb(sweepDryRms));
    writeWav(outputPath("l02_noise_dry.wav"), noise, sampleRate);
    writeWav(outputPath("l02_sweep_dry.wav"), sweep, sampleRate);

    for (const Variant& v : variants) {
        AudioBuffer n = processInBlocks(noise, v.c, sampleRate);
        AudioBuffer s = processInBlocks(sweep, v.c, sampleRate);
        const float nRms = rms(n, 0), sRms = rms(s, 0);
        std::printf("    %-8s  %7.2f dB  %7.2f dB   peak %.2f dBFS\n", v.name, gainToDb(nRms), gainToDb(sRms),
                    gainToDb(std::max(peakAbs(n), peakAbs(s))));
        writeWav(outputPath(std::string("l02_noise_") + v.name + ".wav"), n, sampleRate);
        writeWav(outputPath(std::string("l02_sweep_") + v.name + ".wav"), s, sampleRate);
        DAWLAB_CHECK(!hasNanOrInf(n) && !hasNanOrInf(s));

        if (!haveUserFile) {
            // 白雜訊的功率均勻分布，所以輸出 / 輸入的功率比 = 平均 |H|²。
            // 用理論響應在 0..Nyquist 積分一次做對照（數值積分 2000 點）。
            double power = 0.0;
            const int steps = 2000;
            for (int i = 0; i < steps; ++i) {
                const double f = (i + 0.5) * (sampleRate * 0.5) / steps;
                const double mag = std::abs(v.c.response(f, sampleRate));
                power += mag * mag / steps;
            }
            const double expectedDb = 10.0 * std::log10(power);
            const double measuredDb = gainToDb(nRms / noiseDryRms);
            std::printf("              雜訊 RMS 變化：實測 %.2f dB，理論 ∫|H|² = %.2f dB\n", measuredDb, expectedDb);
            DAWLAB_CHECK_NEAR(measuredDb, expectedDb, 0.3);
        }
    }
    std::printf("\nWAV 已寫到 dawlab_output/l02_noise_*.wav 與 l02_sweep_*.wav\n\n");
    return dawlab::test::summary("l02_filter_sweep");
}
