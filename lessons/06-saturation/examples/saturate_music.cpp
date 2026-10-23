// 範例 4：把 Saturation 用在音樂素材上——drum loop 與 pluck sequence
//
// 示範什麼：
//   用 Saturation（4x oversampling）處理 drum loop 與撥弦琶音，試幾組常見設定：
//     glue      Tanh，drive +6 dB，100% wet，output -3 dB        → 鼓更「黏」、peak 變小 RMS 變大
//     tube      Asymmetric，drive +12 dB，100% wet，output -6 dB → 偶次諧波，較厚；同時示範 DC blocker
//     parallel  Tanh，drive +18 dB，30% wet                      → 「New York」式平行飽和：保留 transient、加厚
//     cubic     Cubic，drive +12 dB，100% wet，output -3 dB      → 多項式曲線，推大時有 hard clip 的粗糙感
//   印出每個版本的 peak、RMS、crest factor，以及 Asymmetric 在「關掉 / 打開 DC blocker」時的 DC offset，
//   並印出 Saturation 回報的 latency（host 要用它做 latency compensation）。
//   另外對 Cubic 印出「曲線本身的上限（2/3 × output gain）」與「實際輸出 peak」：實際 peak 會比較高，
//   因為降頻的 lowpass 與 DC blocker 都會改變波形（README 第 5 節「saturation 不是 limiter」）。
//   為了分辨兩者各占多少，再用「1x 關 DC blocker」「1x 開」「4x 關」三種組合重跑 cubic。
//   最後驗證：host 給的 block 比 prepare 時的 maxBlockSize 大時，Saturation 會自動切塊，結果與正常處理完全相同。
//
// 怎麼執行：l06_saturate_music（不需要參數）；可選 argv[1] = 自己的 WAV 檔路徑（會取代 drum loop）
//
// 輸出檔案（在 dawlab_output/）：
//   l06_drums_dry.wav   l06_drums_glue.wav   l06_drums_tube.wav   l06_drums_parallel.wav   l06_drums_cubic.wav
//   l06_pluck_dry.wav   l06_pluck_glue.wav   l06_pluck_tube.wav   l06_pluck_parallel.wav   l06_pluck_cubic.wav
//
// 該聽到：
//   glue：鼓組變緊、變大聲，kick 的 transient 稍微變鈍。tube：snare / hi-hat 更有「砂質感」，低音更厚。
//   parallel：跟 dry 很像，但更飽滿、更有存在感。cubic：pluck 在音頭處有一點粗糙的破音。

#include <dawlab/Common.h>
#include <dawlab/dsp/Saturation.h>

#include <cstdio>
#include <string>

namespace {

constexpr int kBlockSize = 512;

using dawlab::dsp::Saturation;
using dawlab::dsp::SaturationCurve;

struct Preset {
    const char* name;
    SaturationCurve curve;
    float driveDb;
    float mix;
    float outputGainDb;
};

const Preset kPresets[] = {
    {"glue", SaturationCurve::Tanh, 6.0f, 1.0f, -3.0f},
    {"tube", SaturationCurve::Asymmetric, 12.0f, 1.0f, -6.0f},
    {"parallel", SaturationCurve::Tanh, 18.0f, 0.3f, -6.0f},
    {"cubic", SaturationCurve::Cubic, 12.0f, 1.0f, -3.0f},
};

double meanOf(const dawlab::AudioBuffer& b) {
    double s = 0.0;
    for (int i = 0; i < b.getNumSamples(); ++i)
        s += b.getSample(0, i);
    return b.getNumSamples() > 0 ? s / b.getNumSamples() : 0.0;
}

float rmsDb(const dawlab::AudioBuffer& b) { return dawlab::gainToDb(dawlab::rms(b, 0)); }
float peakDb(const dawlab::AudioBuffer& b) { return dawlab::gainToDb(dawlab::peakAbs(b)); }

dawlab::AudioBuffer applyPreset(const dawlab::AudioBuffer& dry, double sampleRate, const Preset& p, bool dcBlocker,
                                int* latencyOut = nullptr, int oversamplingLog2 = 2, int hostBlockSize = kBlockSize) {
    dawlab::AudioBuffer out = dry;
    Saturation sat;
    sat.setOversamplingFactorLog2(oversamplingLog2); // 預設 2 = 4x
    sat.prepare(sampleRate, kBlockSize, out.getNumChannels());
    sat.setCurve(p.curve);
    sat.setDriveDb(p.driveDb);
    sat.setMix(p.mix);
    sat.setOutputGainDb(p.outputGainDb);
    sat.setDcBlockerEnabled(dcBlocker);
    sat.reset(); // 參數直接就定位（不要在開頭 ramp）
    if (latencyOut)
        *latencyOut = sat.getLatencySamples();
    // hostBlockSize 可以故意比 prepare 的 kBlockSize 大（測試 Saturation 的自動切塊）
    dawlab::AudioBlock whole(out);
    for (int start = 0; start < out.getNumSamples(); start += hostBlockSize) {
        const int len = std::min(hostBlockSize, out.getNumSamples() - start);
        sat.process(whole.getSubBlock(start, len));
    }
    return out;
}

bool runMaterial(const char* label, const dawlab::AudioBuffer& dry, double sampleRate) {
    bool ok = true;
    std::printf("=== %s：%d samples @ %.0f Hz，peak %.2f dBFS，RMS %.2f dBFS，crest %.2f dB ===\n", label,
                dry.getNumSamples(), sampleRate, peakDb(dry), rmsDb(dry), peakDb(dry) - rmsDb(dry));
    dawlab::writeWav(dawlab::outputPath(std::string("l06_") + label + "_dry.wav"), dry, sampleRate);
    std::printf("%-10s %-12s %7s %6s %10s %10s %10s %10s\n", "preset", "curve", "drive", "mix", "peak", "RMS", "crest",
                "ΔRMS");
    for (const Preset& p : kPresets) {
        int latency = 0;
        const dawlab::AudioBuffer out = applyPreset(dry, sampleRate, p, true, &latency);
        std::printf("%-10s %-12s %+5.0f dB %5.0f%% %7.2f dB %7.2f dB %7.2f dB %+7.2f dB\n", p.name,
                    dawlab::dsp::saturationCurveName(p.curve), p.driveDb, p.mix * 100.0f, peakDb(out), rmsDb(out),
                    peakDb(out) - rmsDb(out), rmsDb(out) - rmsDb(dry));
        ok = ok && !dawlab::hasNanOrInf(out) && dawlab::peakAbs(out) <= 1.0f;
        dawlab::writeWav(dawlab::outputPath(std::string("l06_") + label + "_" + p.name + ".wav"), out, sampleRate);

        if (p.curve == SaturationCurve::Asymmetric) {
            const dawlab::AudioBuffer noBlocker = applyPreset(dry, sampleRate, p, false);
            std::printf("           DC offset：dry %+.5f → 關 DC blocker %+.5f → 開 DC blocker %+.5f\n", meanOf(dry),
                        meanOf(noBlocker), meanOf(out));
            ok = ok && std::abs(meanOf(out)) < std::abs(meanOf(noBlocker)) * 0.2 + 1.0e-4;
        }
        if (p.curve == SaturationCurve::Cubic) {
            const float ceiling = (2.0f / 3.0f) * dawlab::dbToGain(p.outputGainDb);
            std::printf("           曲線上限 2/3 × output = %.2f dBFS，實際 peak %.2f dBFS（多出 %.2f dB）\n",
                        dawlab::gainToDb(ceiling), peakDb(out), peakDb(out) - dawlab::gainToDb(ceiling));
            // 拆解：1x 且關掉 DC blocker 時，輸出就是「曲線 × output gain」，peak 不可能超過上限
            const float p1xOff = peakDb(applyPreset(dry, sampleRate, p, false, nullptr, 0));
            const float p1xOn = peakDb(applyPreset(dry, sampleRate, p, true, nullptr, 0));
            const float p4xOff = peakDb(applyPreset(dry, sampleRate, p, false, nullptr, 2));
            std::printf("           拆解 peak：1x 關 DC blocker %.2f、1x 開 %.2f、4x 關 %.2f、4x 開 %.2f dBFS\n", p1xOff,
                        p1xOn, p4xOff, peakDb(out));
            ok = ok && std::abs(p1xOff - dawlab::gainToDb(ceiling)) < 0.01f; // 沒有 filter → 正好碰到上限
        }
        if (&p == &kPresets[0])
            std::printf("           （4x oversampling 的 latency = %d samples = %.2f ms；host 要據此補償）\n", latency,
                        dawlab::samplesToMs(latency, sampleRate));
    }
    std::printf("\n");
    return ok;
}

} // namespace

int main(int argc, char** argv) {
    double sampleRate = 48000.0;
    dawlab::AudioBuffer drums;
    if (argc > 1) {
        dawlab::WavData wav;
        std::string error;
        if (!dawlab::readWav(argv[1], wav, &error)) {
            std::printf("%s\n", error.c_str());
            return 1;
        }
        drums = wav.buffer;
        sampleRate = wav.sampleRate;
    } else {
        drums = dawlab::makeDrumLoop(sampleRate, 2, 120.0, 0.8f);
    }
    const dawlab::AudioBuffer pluck = dawlab::makePluckSequence(sampleRate, 4.0, 0.5f);

    bool ok = runMaterial("drums", drums, sampleRate);
    ok = runMaterial("pluck", pluck, sampleRate) && ok;

    // host 給 2000 samples 的 block（大於 prepare 時的 512）：Saturation 內部切塊，結果必須與 512 一塊完全相同
    {
        const dawlab::AudioBuffer a = applyPreset(drums, sampleRate, kPresets[1], true, nullptr, 2, kBlockSize);
        const dawlab::AudioBuffer b = applyPreset(drums, sampleRate, kPresets[1], true, nullptr, 2, 2000);
        float maxDiff = 0.0f;
        for (int ch = 0; ch < a.getNumChannels(); ++ch)
            for (int i = 0; i < a.getNumSamples(); ++i)
                maxDiff = std::max(maxDiff, std::abs(a.getSample(ch, i) - b.getSample(ch, i)));
        std::printf("block 2000 > maxBlockSize 512：與正常處理的最大差異 %.3g（應為 0）\n\n", maxDiff);
        ok = ok && maxDiff == 0.0f;
    }

    std::printf("已輸出 dawlab_output/l06_{drums,pluck}_{dry,glue,tube,parallel,cubic}.wav\n");
    std::printf("%s\n", ok ? "所有輸出 peak ≤ 0 dBFS、無 NaN，DC blocker 有效。" : "[FAIL] 有輸出不符預期！");
    return ok ? 0 : 1;
}
