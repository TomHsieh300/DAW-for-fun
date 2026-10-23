// 範例 2：簡易吉他 amp chain —— input HPF → pre-EQ → preamp → tone stack → power amp → cabinet
//
// 示範什麼：
//   (1) 印出三個線性級（pre-EQ、tone stack、cabinet）的頻率響應表，看每一級「在頻譜上做了什麼」。
//   (2) 三組 preset（Clean / Crunch / Lead）處理同一段 pluck sequence，輸出 WAV（RMS 對齊到輸入）。
//   (3) cabinet 開 / 關的差別：用 110 Hz 正弦經過 Lead preset，印出高次諧波的 dBFS。
//       cabinet（LR4 lowpass 4.5 kHz）會把 5 kHz 以上的諧波砍掉——這就是「直接錄 amp 輸出」
//       跟「麥克風收音箱」聽起來差那麼多的原因。
//
// 怎麼執行：l07_amp_chain（不需要參數；可選 argv[1] = 輸入 WAV 路徑，例如乾的吉他 DI）
//
// 輸出檔案（在 dawlab_output/）：
//   l07_amp_dry.wav      輸入
//   l07_amp_clean.wav    Clean：preamp 6 dB、power amp 0 dB
//   l07_amp_crunch.wav   Crunch：preamp 18 dB、power amp 6 dB
//   l07_amp_lead.wav     Lead：preamp 30 dB、power amp 12 dB、mid +3 dB、pre-EQ +8 dB
//   l07_amp_lead_nocab.wav  Lead 但關掉 cabinet（聽聽看沒有喇叭模擬有多刺）
//
// 該聽到：Clean 只是比較「厚」；Crunch 有顆粒感、彈輕一點就乾淨；Lead 的 sustain 很長、
//         音尾會被 preamp 推成持續的失真；nocab 版本刺耳、像蚊子叫——那些就是被 cabinet 擋掉的高次諧波。

#include <dawlab/Common.h>
#include <dawlab/dsp/AmpChain.h>
#include <dawlab/dsp/Oversampler.h>

#include <cmath>
#include <cstdio>
#include <string>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

template <typename Processor>
void processInBlocks(dawlab::AudioBuffer& buffer, Processor& processor) {
    dawlab::AudioBlock whole(buffer);
    for (int start = 0; start < buffer.getNumSamples(); start += kBlockSize) {
        const int len = std::min(kBlockSize, buffer.getNumSamples() - start);
        processor.process(whole.getSubBlock(start, len));
    }
}

struct Preset {
    const char* name;
    float preampDriveDb;
    float powerDriveDb;
    float preEqGainDb;
    float bassDb, midDb, trebleDb;
    const char* wavName;
};

const Preset kPresets[] = {
    {"Clean", 6.0f, 0.0f, 3.0f, 0.0f, 0.0f, 0.0f, "l07_amp_clean.wav"},
    {"Crunch", 18.0f, 6.0f, 6.0f, 0.0f, 0.0f, 2.0f, "l07_amp_crunch.wav"},
    {"Lead", 30.0f, 12.0f, 8.0f, -2.0f, 3.0f, 0.0f, "l07_amp_lead.wav"},
};

void applyPreset(dawlab::dsp::AmpChain& amp, const Preset& p, bool cabinet) {
    amp.setInputHighpassHz(80.0f);
    amp.setPreEqHz(800.0f);
    amp.setPreEqGainDb(p.preEqGainDb);
    amp.setPreampDriveDb(p.preampDriveDb);
    amp.setPowerAmpDriveDb(p.powerDriveDb);
    amp.setPowerAmpType(dawlab::dsp::DistortionType::HardClip);
    amp.setBassDb(p.bassDb);
    amp.setMidDb(p.midDb);
    amp.setTrebleDb(p.trebleDb);
    amp.setCabinetEnabled(cabinet);
    amp.setCabinetCutoffHz(4500.0f);
    amp.setCabinetResonanceHz(110.0f);
    amp.setCabinetResonanceDb(4.0f);
    amp.setOutputGainDb(0.0f);
}

float rmsMatch(const dawlab::AudioBuffer& in, dawlab::AudioBuffer& out) {
    const float inRms = dawlab::rms(in, 0);
    const float outRms = dawlab::rms(out, 0);
    const float g = outRms > 1.0e-9f ? inRms / outRms : 1.0f;
    out.applyGain(g);
    return dawlab::gainToDb(g);
}

} // namespace

int main(int argc, char** argv) {
    using dawlab::dsp::AmpChain;
    bool ok = true;

    // ---- (1) 線性級的頻率響應 ----
    {
        AmpChain amp;
        amp.prepare(kSampleRate, kBlockSize, 1);
        applyPreset(amp, kPresets[2], true); // Lead 的 EQ 設定（getXxxMagnitudeDb() 直接用目前的參數算，不必先 process）

        std::printf("(1) 線性級的頻率響應（Lead preset，dB）\n");
        std::printf("%8s %10s %12s %10s\n", "freq", "pre-EQ", "tone stack", "cabinet");
        for (double f : {50.0, 80.0, 110.0, 200.0, 400.0, 700.0, 800.0, 1000.0, 2000.0, 3000.0, 4500.0, 6000.0, 9000.0})
            std::printf("%8.0f %10.2f %12.2f %10.2f\n", f, amp.getPreEqMagnitudeDb(f), amp.getToneStackMagnitudeDb(f),
                        amp.getCabinetMagnitudeDb(f));
        std::printf("（cabinet 在 cutoff 4500 Hz 處應為 -6 dB：兩顆 Q=0.7071 的 Butterworth lowpass 串接 = Linkwitz-Riley 4 階）\n");
        std::printf("latency = %d samples（preamp 與 power amp 各一個 4x oversampler）\n", amp.getLatencySamples());

        // self-check：pre-EQ 的 peak 在中心頻率剛好是設定的 gain；LR4 在 cutoff 是 2 × (-3.01) dB；
        // 共振 peak 在 110 Hz 是 +4 dB（LR4 在 110 Hz 幾乎是 0 dB）；latency = 兩個 4x oversampler 的延遲相加
        dawlab::dsp::Oversampler os4(2);
        os4.prepare(kSampleRate, kBlockSize, 1);
        const bool c1 = std::abs(amp.getPreEqMagnitudeDb(800.0) - 8.0) < 0.01;
        const bool c2 = std::abs(amp.getCabinetMagnitudeDb(4500.0) - (-6.02)) < 0.02;
        const bool c3 = std::abs(amp.getCabinetMagnitudeDb(110.0) - 4.0) < 0.05;
        const bool c4 = amp.getLatencySamples() == 2 * os4.getLatencySamples();
        std::printf("self-check：pre-EQ@800 Hz = +8 dB %s、cabinet@4500 Hz = -6.02 dB %s、cabinet@110 Hz = +4 dB %s、"
                    "latency = 2 × %d %s\n",
                    c1 ? "OK" : "NG", c2 ? "OK" : "NG", c3 ? "OK" : "NG", os4.getLatencySamples(), c4 ? "OK" : "NG");
        ok = ok && c1 && c2 && c3 && c4;
    }

    // ---- (2) 三組 preset 處理 pluck sequence ----
    dawlab::AudioBuffer dry;
    if (argc > 1) {
        dawlab::WavData wav;
        std::string error;
        if (!dawlab::readWav(argv[1], wav, &error)) {
            std::printf("讀取失敗：%s\n", error.c_str());
            return 1;
        }
        dry = wav.buffer;
        std::printf("\n(2) 使用輸入檔 %s\n", argv[1]);
    } else {
        dry = dawlab::makePluckSequence(kSampleRate, 4.0, 0.4f, 2);
        std::printf("\n(2) pluck sequence 經過三組 preset（輸出 RMS 對齊到輸入）\n");
    }
    dawlab::writeWav(dawlab::outputPath("l07_amp_dry.wav"), dry, kSampleRate);
    std::printf("%8s %8s %8s %12s %12s %12s\n", "preset", "preamp", "power", "peak 前(dBFS)", "rms 前(dBFS)", "RMS 補償(dB)");
    std::printf("%8s %8s %8s %12.2f %12.2f %12s\n", "dry", "-", "-", dawlab::gainToDb(dawlab::peakAbs(dry)),
                dawlab::gainToDb(dawlab::rms(dry, 0)), "-");
    float previousRms = 0.0f;
    for (const Preset& p : kPresets) {
        dawlab::AudioBuffer out = dry;
        AmpChain amp;
        amp.prepare(kSampleRate, kBlockSize, out.getNumChannels());
        applyPreset(amp, p, true);
        processInBlocks(out, amp);
        ok = ok && !dawlab::hasNanOrInf(out);
        const float peakBefore = dawlab::peakAbs(out), rmsBefore = dawlab::rms(out, 0);
        // gain staging：drive 越大，處理後越大聲（Clean < Crunch < Lead），而且都比輸入大聲
        ok = ok && rmsBefore > previousRms && rmsBefore > dawlab::rms(dry, 0);
        previousRms = rmsBefore;
        const float comp = rmsMatch(dry, out);
        std::printf("%8s %6.0fdB %6.0fdB %12.2f %12.2f %+12.2f\n", p.name, p.preampDriveDb, p.powerDriveDb,
                    dawlab::gainToDb(peakBefore), dawlab::gainToDb(rmsBefore), comp);
        dawlab::writeWav(dawlab::outputPath(p.wavName), out, kSampleRate);
    }
    {
        dawlab::AudioBuffer out = dry;
        AmpChain amp;
        amp.prepare(kSampleRate, kBlockSize, out.getNumChannels());
        applyPreset(amp, kPresets[2], false);
        processInBlocks(out, amp);
        rmsMatch(dry, out);
        dawlab::writeWav(dawlab::outputPath("l07_amp_lead_nocab.wav"), out, kSampleRate);
    }

    // ---- (3) cabinet 開 / 關：110 Hz 正弦的高次諧波 ----
    {
        std::printf("\n(3) 110 Hz 正弦（-12 dBFS）經過 Lead preset：諧波 dBFS，cabinet 開 vs 關\n");
        const int harmonics[] = {1, 2, 3, 5, 9, 21, 41, 61, 81};
        std::printf("%8s %8s %10s %10s %8s %12s\n", "harm.", "freq", "cab off", "cab on", "差", "cab 響應");
        const double f0 = 110.0;
        dawlab::AudioBuffer outs[2];
        for (int c = 0; c < 2; ++c) {
            outs[c] = dawlab::makeSine(f0, kSampleRate, 1.0, 0.25f, 1);
            AmpChain amp;
            amp.prepare(kSampleRate, kBlockSize, 1);
            applyPreset(amp, kPresets[2], c == 1);
            processInBlocks(outs[c], amp);
        }
        AmpChain ref; // 只用來查 cabinet 的頻率響應
        ref.prepare(kSampleRate, kBlockSize, 1);
        applyPreset(ref, kPresets[2], true);
        const int skip = dawlab::secondsToSamples(0.2, kSampleRate);
        const int n = dawlab::secondsToSamples(0.5, kSampleRate);
        bool matches = true;
        for (int h : harmonics) {
            const double f = f0 * h;
            const double off = dawlab::gainToDb(dawlab::goertzelAmplitude(outs[0].getReadPointer(0) + skip, n, f, kSampleRate));
            const double on = dawlab::gainToDb(dawlab::goertzelAmplitude(outs[1].getReadPointer(0) + skip, n, f, kSampleRate));
            const double cab = ref.getCabinetMagnitudeDb(f);
            std::printf("%8d %8.0f %10.1f %10.1f %+8.1f %+12.2f\n", h, f, off, on, on - off, cab);
            // cabinet 在鏈的最後、是線性的：每個諧波的「開 - 關」應該剛好等於 cabinet 在那個頻率的響應
            // （只比較量得到的諧波；偶次諧波是 -100 dB 的量測下限）
            if (off > -80.0)
                matches = matches && std::abs((on - off) - cab) < 0.5;
            if (f > 8000.0)
                ok = ok && (on - off) < -15.0; // cabinet 一定要把高頻砍掉
        }
        std::printf("self-check：每個諧波的「開 - 關」= cabinet 響應（±0.5 dB）%s\n", matches ? "OK" : "NG");
        ok = ok && matches;
    }

    std::printf("\n已輸出 dawlab_output/l07_amp_*.wav\n");
    std::printf("%s\n", ok ? "全部正常。" : "[FAIL] 有項目不符合預期！");
    return ok ? 0 : 1;
}
