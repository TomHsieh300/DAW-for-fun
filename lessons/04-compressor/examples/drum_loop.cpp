// Example 3: compress a drum loop — peak / RMS / crest factor before and after, an attack-time sweep, and a gain
// reduction meter.
//
// What it demonstrates:
//   - Using Compressor on "musical" material, processed block by block (512 samples) to mimic an audio callback
//   - How crest factor (the distance between peak and RMS) changes with attack time:
//       very fast attack (20 µs, like an FET hardware compressor) → even single-sample peaks get squashed → crest
//       drops; after applying makeup so the peak returns to its original level, the RMS is higher (this is where
//       "compression makes things sound louder" comes from)
//       attack of 1 ms or more → the drum peaks (snare noise, hi-hats only a few samples wide) slip through before
//       the GR has built up, but the "body" behind them is squashed → crest actually rises and it sounds more
//       "punchy"; this is also why truly "guaranteeing" a ceiling needs the lookahead limiter of chapter 05
//   - How to use getGainReductionDb(): read it once per block and draw it as a text GR meter
// How to run: l04_drum_loop [input.wav]; with no argument it uses the drum loop synthesized by makeDrumLoop().
// Output files: dawlab_output/l04_drums_dry.wav, l04_drums_compressed.wav (attack 20 µs, with makeup, same peak as
//          dry), l04_drums_compressed_slow_attack.wav (attack 30 ms, with makeup).
// What you should hear: in the compressed version the kick / snare "impact" is reduced and the whole thing is
//            tighter and louder; the slow-attack version keeps the drum hits' punch but squeezes the tails harder,
//            and ends up slightly quieter than dry overall.

#include <dawlab/Common.h>
#include <dawlab/dsp/Compressor.h>

#include <cstdio>
#include <string>
#include <vector>

using namespace dawlab;
using dawlab::dsp::Compressor;

namespace {

constexpr int kBlockSize = 512;

struct Stats {
    float peakDb, rmsDb, crestDb;
};

// Peak is the largest |sample| over all channels; RMS is the largest across channels
Stats measure(const AudioBuffer& b) {
    const float p = peakAbs(b);
    float r = 0.0f;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        r = std::max(r, rms(b, ch));
    return {gainToDb(p), gainToDb(r), gainToDb(p) - gainToDb(r)};
}

// Compress block by block; record getGainReductionDb() for each block
AudioBuffer compress(const AudioBuffer& input, Compressor& comp, std::vector<float>& grPerBlock) {
    AudioBuffer out = input;
    AudioBlock whole(out);
    const int n = out.getNumSamples();
    grPerBlock.clear();
    for (int start = 0; start < n; start += kBlockSize) {
        comp.process(whole.getSubBlock(start, std::min(kBlockSize, n - start)));
        grPerBlock.push_back(comp.getGainReductionDb());
    }
    return out;
}

void printMeter(const std::vector<float>& grPerBlock, double sampleRate, double seconds) {
    // Print one row about every 50 ms: time, the largest GR in that span, and a text bar (0.5 dB per tick)
    const double blockSec = kBlockSize / sampleRate;
    const int blocksPerRow = std::max(1, static_cast<int>(0.05 / blockSec));
    std::printf("    Time(s)  GR(dB)  meter (0.5 dB per tick)\n");
    for (size_t i = 0; i < grPerBlock.size() && static_cast<double>(i) * blockSec < seconds; i += blocksPerRow) {
        float maxGr = 0.0f;
        for (size_t j = i; j < std::min(grPerBlock.size(), i + blocksPerRow); ++j)
            maxGr = std::max(maxGr, grPerBlock[j]);
        const int bars = static_cast<int>(std::lround(maxGr / 0.5f));
        std::printf("    %6.2f   %5.2f   %s\n", static_cast<double>(i) * blockSec, maxGr,
                    std::string(static_cast<size_t>(bars), '|').c_str());
    }
}

struct Setting {
    const char* name;
    float thresholdDb, ratio, kneeDb, attackMs, releaseMs;
    const char* wavName;
};

Compressor makeCompressor(const Setting& s, double sampleRate, int numChannels) {
    Compressor comp;
    comp.prepare(sampleRate, kBlockSize, numChannels);
    comp.setThresholdDb(s.thresholdDb);
    comp.setRatio(s.ratio);
    comp.setKneeDb(s.kneeDb);
    comp.setAttackMs(s.attackMs);
    comp.setReleaseMs(s.releaseMs);
    comp.setStereoLink(true);
    return comp;
}

} // namespace

int main(int argc, char** argv) {
    std::printf("Chapter 04, example 3: drum loop compression\n");

    double sampleRate = 48000.0;
    AudioBuffer dry;
    if (argc > 1) {
        WavData wav;
        std::string err;
        if (!readWav(argv[1], wav, &err)) {
            std::printf("Failed to read: %s\n", err.c_str());
            return 1;
        }
        dry = wav.buffer;
        sampleRate = wav.sampleRate;
        std::printf("Input: %s (%d ch, %d samples, %.0f Hz)\n", argv[1], dry.getNumChannels(), dry.getNumSamples(),
                    sampleRate);
    } else {
        dry = makeDrumLoop(sampleRate, 2, 120.0);
        std::printf("Input: drum loop synthesized by makeDrumLoop(), 2 bars at 120 BPM (%.2f seconds)\n",
                    dry.getNumSamples() / sampleRate);
    }
    const int numChannels = dry.getNumChannels();
    const Stats in = measure(dry);
    std::printf("\n  Before compression: peak %6.2f dBFS, RMS %6.2f dBFS, crest factor %5.2f dB\n", in.peakDb, in.rmsDb,
                in.crestDb);

    writeWav(outputPath("l04_drums_dry.wav"), dry, sampleRate);

    // ---------- Three representative settings ----------
    const Setting settings[] = {
        {"Very fast attack (squashes transients, like an FET hardware compressor)", -20.0f, 4.0f, 6.0f, 0.02f, 80.0f,
         "l04_drums_compressed.wav"},
        {"Slow attack (lets transients through)", -20.0f, 4.0f, 6.0f, 30.0f, 80.0f,
         "l04_drums_compressed_slow_attack.wav"},
        {"Heavy compression (limiter style)", -26.0f, 10.0f, 0.0f, 0.02f, 50.0f, nullptr},
    };

    for (const Setting& s : settings) {
        Compressor comp = makeCompressor(s, sampleRate, numChannels);
        std::vector<float> gr;
        AudioBuffer wet = compress(dry, comp, gr);
        const Stats raw = measure(wet);
        float maxGr = 0.0f;
        double meanGr = 0.0;
        for (float g : gr) {
            maxGr = std::max(maxGr, g);
            meanGr += g;
        }
        meanGr /= static_cast<double>(gr.size());

        // Makeup: bring the output peak back to the dry peak, so the RMS comparison is fair ("which one is louder
        // at the same peak level")
        const float makeupDb = in.peakDb - raw.peakDb;
        wet.applyGain(dbToGain(makeupDb));
        const Stats out = measure(wet);

        std::printf("\n=== %s: T %.0f dB, R %.0f:1, knee %.0f dB, attack %.2f ms, release %.0f ms ===\n", s.name,
                    s.thresholdDb, s.ratio, s.kneeDb, s.attackMs, s.releaseMs);
        std::printf("  GR: max %.2f dB, mean %.2f dB\n", maxGr, meanGr);
        std::printf("  Without makeup: peak %6.2f dBFS, RMS %6.2f dBFS, crest %5.2f dB (peak down %.2f dB, RMS down %.2f dB)\n",
                    raw.peakDb, raw.rmsDb, raw.crestDb, in.peakDb - raw.peakDb, in.rmsDb - raw.rmsDb);
        std::printf("  With makeup %+.2f dB: peak %6.2f dBFS, RMS %6.2f dBFS, crest %5.2f dB (RMS is %.2f dB %s than dry)\n",
                    makeupDb, out.peakDb, out.rmsDb, out.crestDb, std::abs(out.rmsDb - in.rmsDb),
                    out.rmsDb >= in.rmsDb ? "higher" : "lower");
        if (s.wavName) {
            writeWav(outputPath(s.wavName), wet, sampleRate);
            std::printf("  Wrote dawlab_output/%s\n", s.wavName);
        }
        if (&s == &settings[0]) {
            std::printf("  Gain reduction meter for the first second:\n");
            printMeter(gr, sampleRate, 1.0);
        }
    }

    // ---------- Attack-time sweep ----------
    std::printf("\n=== Attack-time sweep (T -20 dB, R 4:1, knee 6 dB, release 80 ms; peak equals dry after makeup) ===\n");
    std::printf("  attack(ms) | max GR | peak (down) | RMS (down) | crest(dB) | RMS vs dry after makeup\n");
    std::printf("  -----------+--------+--------+-------+-----------+------------------------\n");
    for (float attackMs : {0.0f, 0.02f, 0.1f, 0.5f, 1.0f, 3.0f, 10.0f, 30.0f}) {
        const Setting s{"", -20.0f, 4.0f, 6.0f, attackMs, 80.0f, nullptr};
        Compressor comp = makeCompressor(s, sampleRate, numChannels);
        std::vector<float> gr;
        const AudioBuffer wet = compress(dry, comp, gr);
        const Stats raw = measure(wet);
        float maxGr = 0.0f;
        for (float g : gr)
            maxGr = std::max(maxGr, g);
        // RMS vs dry after makeup = (RMS_wet + makeup) - RMS_dry = crest_dry - crest_wet
        std::printf("  %10.2f | %6.2f | %6.2f | %5.2f | %9.2f | %+6.2f dB\n", attackMs, maxGr, in.peakDb - raw.peakDb,
                    in.rmsDb - raw.rmsDb, raw.crestDb, in.crestDb - raw.crestDb);
    }
    std::printf("  → The faster the attack, the more the peak is squashed, the lower the crest, and the louder it gets after makeup;\n"
                "    with attack ≥ 1 ms, peaks a few samples wide slip through almost untouched, the \"body\" is what gets squashed, and the crest actually rises.\n"
                "    To \"guarantee\" that the peak stays under some value you need the lookahead of the chapter 05 limiter.\n");

    std::printf("\n  Any NaN/Inf? %s\n", hasNanOrInf(dry) ? "yes" : "no");
    return 0;
}
