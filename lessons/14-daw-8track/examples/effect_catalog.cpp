// l14_effect_catalog -- Step 5: the uniform InsertEffect API, seen from a UI's point of view.
//
// What it shows
//   - The factory: every effect is created by name (what an "Add insert" menu and a project
//     file do).
//   - The parameter description API: for every effect we print the "knob list" a UI would
//     generate automatically (name, range, default, unit, kind, log taper, choices) without
//     knowing anything about the effect.
//   - Every effect runs on the same 2-second test phrase (drums + plucks) at its default
//     settings (a few wet/dry mixes turned up so they are audible); the result of all 19 is
//     concatenated into one file, in the order of the EffectType enum.
//   - Reported latency and the optional meter (gain reduction).
//
// Run
//   build/bin/Debug/l14_effect_catalog.exe
//
// Output
//   dawlab_output/l14_effect_catalog.wav   19 x (2 s processed + 0.5 s gap) = 47.5 s
//
// What you should hear (in order)
//   Gain (unchanged), ParametricEQ (+9 dB bell at 2 kHz: brighter, nasal), Compressor (drums
//   flatter), Limiter (louder, peaks held at -1 dBFS), Saturation (warmer, denser), Distortion
//   (hard clipped), Gate (the plucks between the drum hits cut away), Expander (quiet parts
//   pushed down), DeEsser (hats softer), TransientShaper (snappier attacks, +9 dB), Delay
//   (echoes), Chorus (wider, detuned), Flanger (jet sweep), Phaser (slow swirl), Tremolo
//   (5 Hz pulsing), Reverb (room), ConvolutionReverb (room), Filter (resonant 600 Hz lowpass:
//   dark, with a whistle at the cutoff), AmpChain (crunchy guitar amp through a speaker cabinet).
//   Snippets louder than -1 dBFS are turned down in the file only (printed as "in the file").

#include <dawlab/Common.h>
#include <dawlab/engine/All.h>

#include <cstdio>
#include <cstring>
#include <memory>

using namespace dawlab;
using namespace dawlab::engine;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

const char* kindName(ParameterKind k) {
    switch (k) {
    case ParameterKind::Float: return "float";
    case ParameterKind::Int: return "int";
    case ParameterKind::Bool: return "bool";
    case ParameterKind::Choice: return "choice";
    }
    return "?";
}

// What a UI does: walk the parameter table and describe one knob per parameter.
void printKnobs(const InsertEffect& fx) {
    const int n = fx.getNumParameters();
    const int shown = std::min(n, 10); // the 30-parameter EQ would flood the console
    for (int i = 0; i < shown; ++i) {
        const ParameterInfo p = fx.getParameterInfo(i);
        char def[48];
        formatParameterValue(p, p.defaultValue, def, sizeof(def));
        std::printf("    [%2d] %-16s %-6s  %9.3g .. %-9.3g default %-12s%s", i, p.name, kindName(p.kind),
                    static_cast<double>(p.min), static_cast<double>(p.max), def, p.logarithmic ? " (log)" : "");
        if (p.kind == ParameterKind::Choice) {
            std::printf("  {");
            for (int c = 0; c < p.numChoices; ++c)
                std::printf("%s%s", c ? ", " : "", p.choices[c]);
            std::printf("}");
        }
        std::printf("\n");
    }
    if (n > shown)
        std::printf("    ... %d more%s\n", n - shown,
                    std::strcmp(fx.getName(), "ParametricEQ") == 0 ? " (6 bands x Enabled/Type/Frequency/Q/Gain)" : "");
}

// Make the "invisible at default" effects audible for the demo.
void demoSettings(InsertEffect& fx) {
    const char* name = fx.getName();
    if (std::strcmp(name, "Limiter") == 0)
        fx.setParameterByName("Input Gain", 9.0f);
    else if (std::strcmp(name, "ParametricEQ") == 0)
        fx.setParameterByName("B4 Gain", 9.0f); // +9 dB bell at 2 kHz
    else if (std::strcmp(name, "Delay") == 0)
        fx.setParameterByName("Mix", 0.5f);
    else if (std::strcmp(name, "Reverb") == 0 || std::strcmp(name, "ConvolutionReverb") == 0)
        fx.setParameterByName("Mix", 0.4f);
    else if (std::strcmp(name, "TransientShaper") == 0)
        fx.setParameterByName("Attack", 9.0f);
    else if (std::strcmp(name, "Gate") == 0) {
        fx.setParameterByName("Threshold", -12.0f); // closes between the drum hits
        fx.setParameterByName("Hold", 5.0f);
    } else if (std::strcmp(name, "Expander") == 0) {
        fx.setParameterByName("Threshold", -14.0f);
        fx.setParameterByName("Ratio", 4.0f);
    } else if (std::strcmp(name, "Filter") == 0) {
        fx.setParameterByName("Cutoff", 600.0f); // Lowpass by default
        fx.setParameterByName("Resonance", 6.0f);
    }
}

} // namespace

int main() {
    // The test phrase: one bar of drums plus plucks, 2 s.
    AudioBuffer drums = makeDrumLoop(kSampleRate, 1, 120.0, 0.6f);
    AudioBuffer plucks = makePluckSequence(kSampleRate, 2.0, 0.3f);
    const int phraseLen = secondsToSamples(2.0, kSampleRate);
    AudioBuffer phrase(2, phraseLen);
    for (int ch = 0; ch < 2; ++ch) {
        phrase.addFrom(ch, 0, drums, ch, 0, std::min(phraseLen, drums.getNumSamples()));
        phrase.addFrom(ch, 0, plucks, ch, 0, phraseLen);
    }
    const float inRms = rms(phrase, 0);

    const int gap = secondsToSamples(0.5, kSampleRate);
    AudioBuffer all(2, kNumUserEffectTypes * (phraseLen + gap));

    std::printf("Step 5: %d effects behind one interface (input RMS %.1f dBFS)\n\n", kNumUserEffectTypes,
                gainToDb(inRms));
    const float inPeak = peakAbs(phrase);
    int failures = 0;
    float outRmsDb[kNumUserEffectTypes] = {};
    float outPeakDb[kNumUserEffectTypes] = {};
    int latency[kNumUserEffectTypes] = {};
    for (int t = 0; t < kNumUserEffectTypes; ++t) {
        std::unique_ptr<InsertEffect> fx = createEffect(effectTypeName(static_cast<EffectType>(t)));
        demoSettings(*fx);
        fx->prepare(kSampleRate, kBlockSize, 2);

        AudioBuffer x = phrase;
        float maxMeter = 0.0f;
        for (int pos = 0; pos < phraseLen; pos += kBlockSize) {
            fx->process(x.toBlock().getSubBlock(pos, std::min(kBlockSize, phraseLen - pos)));
            maxMeter = std::max(maxMeter, fx->getMeterDb());
        }
        // Some effects add gain (limiter input gain, transient boost...). Keep the WAV file
        // from clipping: snippets louder than -1 dBFS are scaled down for the file only.
        const float filePeak = peakAbs(x);
        const float fileGain = filePeak > 0.9f ? 0.891f / filePeak : 1.0f;
        for (int ch = 0; ch < 2; ++ch)
            all.addFrom(ch, t * (phraseLen + gap), x, ch, 0, phraseLen, fileGain);

        const bool bad = hasNanOrInf(x);
        failures += bad ? 1 : 0;
        outRmsDb[t] = gainToDb(rms(x, 0));
        outPeakDb[t] = gainToDb(peakAbs(x));
        latency[t] = fx->getLatencySamples();
        std::printf("%-17s latency %4d samples (%5.2f ms)  out RMS %6.1f dBFS  peak %6.1f dBFS", fx->getName(),
                    fx->getLatencySamples(), samplesToMs(fx->getLatencySamples(), kSampleRate),
                    gainToDb(rms(x, 0)), gainToDb(peakAbs(x)));
        if (fx->hasMeter())
            std::printf("  max meter %.1f dB", static_cast<double>(maxMeter));
        if (fileGain < 1.0f)
            std::printf("  (in the file: %.1f dB)", static_cast<double>(gainToDb(fileGain)));
        std::printf("%s\n", bad ? "  NaN!" : "");
        printKnobs(*fx);
    }
    writeWav(outputPath("l14_effect_catalog.wav"), all, kSampleRate);
    std::printf("\nWrote dawlab_output/l14_effect_catalog.wav (%.1f s)\n", all.getNumSamples() / kSampleRate);

    // The numbers Step 5 quotes, checked.
    const auto idx = [](EffectType e) { return static_cast<int>(e); };
    DAWLAB_CHECK(failures == 0);
    DAWLAB_CHECK_NEAR(outRmsDb[idx(EffectType::Gain)], gainToDb(inRms), 0.01); // 0 dB gain = unchanged
    DAWLAB_CHECK(outPeakDb[idx(EffectType::Limiter)] <= -1.0f + 0.01f);           // the ceiling holds
    DAWLAB_CHECK(latency[idx(EffectType::Limiter)] == 240);                       // 5 ms at 48 kHz
    DAWLAB_CHECK(latency[idx(EffectType::ConvolutionReverb)] == 512);             // one partition = block size
    DAWLAB_CHECK(latency[idx(EffectType::Saturation)] > 0 && latency[idx(EffectType::Distortion)] > 0);
    DAWLAB_CHECK(latency[idx(EffectType::AmpChain)] > 0 && latency[idx(EffectType::Filter)] == 0); // 2 oversamplers
    DAWLAB_CHECK(outPeakDb[idx(EffectType::TransientShaper)] > gainToDb(inPeak) + 6.0f); // +9 dB attack
    int zeroLatency = 0; // every other effect is latency-free
    for (int t = 0; t < kNumUserEffectTypes; ++t)
        zeroLatency += latency[t] == 0 ? 1 : 0;
    DAWLAB_CHECK(zeroLatency == kNumUserEffectTypes - 5); // all but Limiter, Saturation, Distortion, Convolution, Amp
    return dawlab::test::summary("l14_effect_catalog");
}
