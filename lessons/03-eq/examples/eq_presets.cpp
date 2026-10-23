// Example 2: apply typical mixing EQ settings to a pluck sequence and a drum loop
//
// What it demonstrates:
//   - A "bass / guitar" style EQ: a low cut removes the sub-bass, the mud region is scooped out, presence is boosted, and a high shelf adds air.
//   - A "drum bus" style EQ: a low shelf adds weight to the kick, the boxy region is scooped out, and a high shelf adds crispness to the hi-hat.
//   - Processes in blocks of 512 samples, simulating an audio callback, one block at a time.
//   - Uses Goertzel to measure the "after / before" ratio at each harmonic and compares it with the theoretical EQ curve.
// How to run: l03_eq_presets (no arguments), or l03_eq_presets input.wav (applies the guitar-style EQ to your own audio file)
// Output (build directory/dawlab_output/):
//   l03_pluck_dry.wav / l03_pluck_eq.wav, l03_drums_dry.wav / l03_drums_eq.wav,
//   (when argv[1] is given) l03_custom_eq.wav
// What you should hear: the pluck becomes "cleaner", less muffled, with the string noise further forward; the kick of the drum loop is fuller, the snare sounds less boxy, and the hi-hat is brighter.

#include <dawlab/Common.h>
#include <dawlab/dsp/ParametricEq.h>

#include <cstdio>
#include <string>

using namespace dawlab;
using namespace dawlab::dsp;

namespace {

constexpr int kBlockSize = 512;

// Simulates an audio callback: 512 samples at a time
void processInBlocks(ParametricEq& eq, AudioBuffer& buffer) {
    AudioBlock whole(buffer);
    const int n = buffer.getNumSamples();
    for (int start = 0; start < n; start += kBlockSize)
        eq.process(whole.getSubBlock(start, std::min(kBlockSize, n - start)));
}

void printLevels(const char* name, const AudioBuffer& b) {
    std::printf("  %-18s peak %+6.2f dBFS   RMS %+6.2f dBFS\n", name, gainToDb(peakAbs(b)), gainToDb(rms(b, 0)));
}

void printCurve(const ParametricEq& eq) {
    std::printf("  theoretical curve:");
    for (double f : {50.0, 100.0, 200.0, 400.0, 800.0, 1600.0, 3200.0, 6400.0, 12800.0})
        std::printf(" %.0fHz:%+.1f", f, eq.getMagnitudeDb(f));
    std::printf(" (dB)\n");
}

// Use Goertzel to compare the "after / before" amplitude ratio at several frequencies; it should be close to the EQ curve
void printMeasuredRatios(const ParametricEq& eq, const AudioBuffer& dry, const AudioBuffer& wet,
                         std::initializer_list<double> freqs, double sr) {
    std::printf("     freq(Hz)   theory(dB)   measured(dB)\n");
    for (double f : freqs) {
        const double a = goertzelAmplitude(dry.getReadPointer(0), dry.getNumSamples(), f, sr);
        const double b = goertzelAmplitude(wet.getReadPointer(0), wet.getNumSamples(), f, sr);
        std::printf("     %7.0f     %+7.2f      %+7.2f\n", f, eq.getMagnitudeDb(f), gainToDb(b / a));
    }
}

void setupGuitarEq(ParametricEq& eq) {
    eq.setBand(0, {EqBandType::LowCut, 70.0f, 0.707f, 0.0f, true});     // remove sub-bass that carries no musical content
    eq.setBand(1, {EqBandType::Peak, 300.0f, 1.2f, -4.0f, true});      // scoop the mud region (200-400 Hz)
    eq.setBand(2, {EqBandType::Peak, 900.0f, 2.0f, -2.0f, true});      // slightly reduce the nasal / boxy character
    eq.setBand(3, {EqBandType::Peak, 3000.0f, 1.0f, 3.0f, true});      // presence: bring the "fingernail" sound of the plucks forward
    eq.setBand(4, {EqBandType::HighShelf, 8000.0f, 0.707f, 2.0f, true}); // air
    eq.setBand(5, {EqBandType::HighCut, 16000.0f, 0.707f, 0.0f, false});
}

void setupDrumBusEq(ParametricEq& eq) {
    eq.setBand(0, {EqBandType::LowCut, 30.0f, 0.707f, 0.0f, true});     // subsonic rumble
    eq.setSlope(0, EqSlope::Db24);
    eq.setBand(1, {EqBandType::LowShelf, 90.0f, 0.707f, 3.0f, true});  // weight of the kick
    eq.setBand(2, {EqBandType::Peak, 400.0f, 1.5f, -3.0f, true});      // boxiness
    eq.setBand(3, {EqBandType::Peak, 2500.0f, 1.2f, 1.5f, true});      // crack of the snare
    eq.setBand(4, {EqBandType::HighShelf, 7000.0f, 0.707f, 3.0f, true}); // crispness of the hi-hat
    eq.setBand(5, {EqBandType::HighCut, 18000.0f, 0.707f, 0.0f, false});
}

} // namespace

int main(int argc, char** argv) {
    constexpr double sr = 48000.0;
    std::printf("=== Chapter 03 Example 2: typical EQ settings ===\n");

    // ---- 1. pluck sequence + guitar-style EQ ----
    {
        AudioBuffer dry = makePluckSequence(sr, 4.0);
        AudioBuffer wet = dry;
        ParametricEq eq;
        eq.prepare(sr, kBlockSize, wet.getNumChannels());
        setupGuitarEq(eq);
        eq.reset(); // Reset after configuring: the smoothers jump straight to the targets, so there is no 20 ms "fade-in" at the start
        processInBlocks(eq, wet);

        std::printf("\n[pluck sequence] guitar-style EQ: LowCut 70 / -4 dB@300 / -2 dB@900 / +3 dB@3k / HighShelf +2 dB@8k\n");
        printCurve(eq);
        printLevels("dry", dry);
        printLevels("eq", wet);
        std::printf("  harmonic amplitude ratios (harmonic series of A2 = 110 Hz):\n");
        printMeasuredRatios(eq, dry, wet, {110.0, 220.0, 330.0, 440.0, 880.0, 1760.0, 3520.0}, sr);
        writeWav(outputPath("l03_pluck_dry.wav"), dry, sr);
        writeWav(outputPath("l03_pluck_eq.wav"), wet, sr);
    }

    // ---- 2. drum loop + drum-bus EQ ----
    {
        AudioBuffer dry = makeDrumLoop(sr, 2);
        AudioBuffer wet = dry;
        ParametricEq eq;
        eq.prepare(sr, kBlockSize, wet.getNumChannels());
        setupDrumBusEq(eq);
        eq.reset();
        processInBlocks(eq, wet);

        std::printf("\n[drum loop] drum-bus EQ: LowCut 30 (24 dB/oct) / LowShelf +3@90 / -3@400 / +1.5@2.5k / HighShelf +3@7k\n");
        printCurve(eq);
        printLevels("dry", dry);
        printLevels("eq", wet);
        writeWav(outputPath("l03_drums_dry.wav"), dry, sr);
        writeWav(outputPath("l03_drums_eq.wav"), wet, sr);
    }

    // ---- 3. Optional: your own audio file ----
    if (argc > 1) {
        WavData wav;
        std::string err;
        if (!readWav(argv[1], wav, &err)) {
            std::printf("\nFailed to read %s: %s\n", argv[1], err.c_str());
            return 1;
        }
        ParametricEq eq;
        eq.prepare(wav.sampleRate, kBlockSize, wav.buffer.getNumChannels());
        setupGuitarEq(eq);
        eq.reset();
        processInBlocks(eq, wav.buffer);
        writeWav(outputPath("l03_custom_eq.wav"), wav.buffer, wav.sampleRate);
        std::printf("\n[%s] guitar-style EQ applied -> dawlab_output/l03_custom_eq.wav\n", argv[1]);
    }

    std::printf("\nOutput: dawlab_output/l03_pluck_dry.wav, l03_pluck_eq.wav, l03_drums_dry.wav, l03_drums_eq.wav\n");
    return 0;
}
