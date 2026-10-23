// Example 4: Saturation on musical material - a drum loop and a pluck sequence.
//
// What it shows:
//   Runs Saturation (4x oversampling) over a drum loop and a plucked arpeggio with a few common settings:
//     glue      Tanh, drive +6 dB, 100% wet, output -3 dB        -> tighter ("glued") drums, lower peak, higher RMS
//     tube      Asymmetric, drive +12 dB, 100% wet, output -6 dB -> even harmonics, thicker; also demonstrates the DC blocker
//     parallel  Tanh, drive +18 dB, 30% wet                      -> "New York" style parallel saturation: keeps transients, adds body
//     cubic     Cubic, drive +12 dB, 100% wet, output -3 dB      -> polynomial curve; pushed hard it has hard-clip roughness
//   Prints the peak, RMS and crest factor of every version, the DC offset of Asymmetric with the DC blocker
//   off / on, and the latency reported by Saturation (the host needs it for latency compensation).
//   For Cubic it also prints "the ceiling of the curve itself (2/3 x output gain)" next to "the actual
//   output peak": the actual peak is higher, because the downsampling lowpass and the DC blocker both
//   change the waveform (README section 5, "saturation is not a limiter").
//   To tell how much each one contributes, cubic is re-run with three combinations: "1x, DC blocker off",
//   "1x, on" and "4x, off".
//   Finally it verifies that when the host passes a block larger than the maxBlockSize given to prepare,
//   Saturation splits it automatically and the result is identical to normal processing.
//
// How to run: l06_saturate_music (no arguments); optional argv[1] = path to your own WAV file (replaces the drum loop)
//
// Output files (in dawlab_output/):
//   l06_drums_dry.wav   l06_drums_glue.wav   l06_drums_tube.wav   l06_drums_parallel.wav   l06_drums_cubic.wav
//   l06_pluck_dry.wav   l06_pluck_glue.wav   l06_pluck_tube.wav   l06_pluck_parallel.wav   l06_pluck_cubic.wav
//
// What you should hear:
//   glue: the drum kit gets tighter and louder; the kick transient is slightly blunted. tube: snare and
//   hi-hat get a gritty texture and the low end is thicker. parallel: close to the dry signal but fuller
//   and more present. cubic: the pluck has a little rough distortion at the attack.

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
    sat.setOversamplingFactorLog2(oversamplingLog2); // default 2 = 4x
    sat.prepare(sampleRate, kBlockSize, out.getNumChannels());
    sat.setCurve(p.curve);
    sat.setDriveDb(p.driveDb);
    sat.setMix(p.mix);
    sat.setOutputGainDb(p.outputGainDb);
    sat.setDcBlockerEnabled(dcBlocker);
    sat.reset(); // Snap the parameters into place (no ramp at the start)
    if (latencyOut)
        *latencyOut = sat.getLatencySamples();
    // hostBlockSize may deliberately be larger than the kBlockSize given to prepare (tests Saturation's automatic chunking)
    dawlab::AudioBlock whole(out);
    for (int start = 0; start < out.getNumSamples(); start += hostBlockSize) {
        const int len = std::min(hostBlockSize, out.getNumSamples() - start);
        sat.process(whole.getSubBlock(start, len));
    }
    return out;
}

bool runMaterial(const char* label, const dawlab::AudioBuffer& dry, double sampleRate) {
    bool ok = true;
    std::printf("=== %s: %d samples @ %.0f Hz, peak %.2f dBFS, RMS %.2f dBFS, crest %.2f dB ===\n", label,
                dry.getNumSamples(), sampleRate, peakDb(dry), rmsDb(dry), peakDb(dry) - rmsDb(dry));
    dawlab::writeWav(dawlab::outputPath(std::string("l06_") + label + "_dry.wav"), dry, sampleRate);
    std::printf("%-10s %-12s %7s %6s %10s %10s %10s %10s\n", "preset", "curve", "drive", "mix", "peak", "RMS", "crest",
                "dRMS");
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
            std::printf("           DC offset: dry %+.5f -> DC blocker off %+.5f -> DC blocker on %+.5f\n", meanOf(dry),
                        meanOf(noBlocker), meanOf(out));
            ok = ok && std::abs(meanOf(out)) < std::abs(meanOf(noBlocker)) * 0.2 + 1.0e-4;
        }
        if (p.curve == SaturationCurve::Cubic) {
            const float ceiling = (2.0f / 3.0f) * dawlab::dbToGain(p.outputGainDb);
            std::printf("           Curve ceiling 2/3 x output = %.2f dBFS, actual peak %.2f dBFS (%.2f dB over)\n",
                        dawlab::gainToDb(ceiling), peakDb(out), peakDb(out) - dawlab::gainToDb(ceiling));
            // Breakdown: at 1x with the DC blocker off, the output is exactly "curve x output gain", so the peak
            // cannot exceed the ceiling
            const float p1xOff = peakDb(applyPreset(dry, sampleRate, p, false, nullptr, 0));
            const float p1xOn = peakDb(applyPreset(dry, sampleRate, p, true, nullptr, 0));
            const float p4xOff = peakDb(applyPreset(dry, sampleRate, p, false, nullptr, 2));
            std::printf("           Peak breakdown: 1x DC blocker off %.2f, 1x on %.2f, 4x off %.2f, 4x on %.2f dBFS\n", p1xOff,
                        p1xOn, p4xOff, peakDb(out));
            ok = ok && std::abs(p1xOff - dawlab::gainToDb(ceiling)) < 0.01f; // No filter -> hits the ceiling exactly
        }
        if (&p == &kPresets[0])
            std::printf("           (Latency of 4x oversampling = %d samples = %.2f ms; the host must compensate for it)\n", latency,
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

    // The host passes a 2000-sample block (larger than the 512 given to prepare): Saturation chunks it
    // internally, and the result must be identical to processing in 512-sample blocks
    {
        const dawlab::AudioBuffer a = applyPreset(drums, sampleRate, kPresets[1], true, nullptr, 2, kBlockSize);
        const dawlab::AudioBuffer b = applyPreset(drums, sampleRate, kPresets[1], true, nullptr, 2, 2000);
        float maxDiff = 0.0f;
        for (int ch = 0; ch < a.getNumChannels(); ++ch)
            for (int i = 0; i < a.getNumSamples(); ++i)
                maxDiff = std::max(maxDiff, std::abs(a.getSample(ch, i) - b.getSample(ch, i)));
        std::printf("block 2000 > maxBlockSize 512: largest difference from normal processing %.3g (should be 0)\n\n", maxDiff);
        ok = ok && maxDiff == 0.0f;
    }

    std::printf("Wrote dawlab_output/l06_{drums,pluck}_{dry,glue,tube,parallel,cubic}.wav\n");
    std::printf("%s\n", ok ? "All outputs peak <= 0 dBFS, no NaN, and the DC blocker works." : "[FAIL] Some output did not match expectations!");
    return ok ? 0 : 1;
}
