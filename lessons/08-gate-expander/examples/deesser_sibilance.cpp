// Example 3: a de-esser on a synthetic voice made of a "vowel + 7 kHz sibilance burst"
//
// What it shows:
//   - Synthetic test signal: a 220 Hz "vowel" (harmonics 1..12 with amplitude falling as 1/k, RMS about -19 dBFS)
//     lasting 3 seconds, with a 120 ms "sibilance" burst (white noise through a 7 kHz bandpass, peak about -12 dBFS)
//     layered on at 0.5, 1.5, and 2.5 seconds
//   - Runs the DeEsser (frequency 7 kHz, threshold -30 dB, range 12 dB) in both wideband and split-band modes,
//     reading getGainReductionDb() block by block to print a gain-reduction timeline
//   - Uses Goertzel to measure the level of the 220 Hz vowel during the sibilance: in wideband mode the vowel is
//     pulled down along with it; in split-band mode the vowel is unaffected
//   - Listen mode: outputs the bandpassed detection signal, which is what you listen to while adjusting
//     frequency / threshold
// How to run: l08_deesser_sibilance (takes no arguments).
// Output files: dawlab_output/l08_deesser_input.wav, l08_deesser_wideband.wav, l08_deesser_splitband.wav,
//          l08_deesser_listen.wav.
// What to listen for: the input has a harsh "sss" once per second; in the wideband version the sibilance gets
//            quieter but the vowel also ducks at that instant (a pumping feel); in the split-band version only the
//            sibilance gets quieter and the vowel stays put; the listen version contains only the sibilance
//            (plus a little of the vowel's high harmonics).

#include <dawlab/Common.h>
#include <dawlab/dsp/Biquad.h>
#include <dawlab/dsp/DeEsser.h>

#include <cstdio>
#include <vector>

using namespace dawlab;
using dawlab::dsp::Biquad;
using dawlab::dsp::BiquadCoefficients;
using dawlab::dsp::DeEsser;

namespace {

constexpr int kBlockSize = 512;
constexpr double kSampleRate = 48000.0;
constexpr double kVowelHz = 220.0;
constexpr double kSibilanceHz = 7000.0;

// Vowel: summed harmonics with amplitude 1/k (like a sawtooth), band-limited by keeping only up to the 12th (2640 Hz)
AudioBuffer makeVowel(double seconds, float amplitude) {
    AudioBuffer b(2, secondsToSamples(seconds, kSampleRate));
    for (int i = 0; i < b.getNumSamples(); ++i) {
        double v = 0.0;
        for (int k = 1; k <= 12; ++k)
            v += std::sin(kTwoPi * kVowelHz * k * i / kSampleRate) / k;
        const auto s = static_cast<float>(amplitude * v / 1.6); // 1.6 ~= peak of the harmonic sum, normalizing to +/-amplitude
        b.setSample(0, i, s);
        b.setSample(1, i, s);
    }
    return b;
}

// Sibilance: white noise -> 7 kHz bandpass (Q = 1.5), shaped by an envelope with a 10 ms fade-in / 30 ms fade-out
void addSibilance(AudioBuffer& target, double startSec, double lengthSec, float peakAmplitude) {
    const int start = secondsToSamples(startSec, kSampleRate);
    const int len = secondsToSamples(lengthSec, kSampleRate);
    Biquad bp;
    bp.prepare(kSampleRate, kBlockSize, 1);
    bp.setCoefficients(BiquadCoefficients::makeBandpass(kSampleRate, kSibilanceHz, 1.5));
    NoiseGenerator noise(777);
    std::vector<float> burst(static_cast<size_t>(len));
    float peak = 0.0f;
    for (int i = 0; i < len; ++i) {
        const double t = i / kSampleRate;
        const double env = std::min(1.0, t / 0.010) * std::min(1.0, (lengthSec - t) / 0.030);
        burst[static_cast<size_t>(i)] = static_cast<float>(env * bp.processSample(0, noise.next()));
        peak = std::max(peak, std::abs(burst[static_cast<size_t>(i)]));
    }
    for (int i = 0; i < len && start + i < target.getNumSamples(); ++i)
        for (int ch = 0; ch < target.getNumChannels(); ++ch)
            target.addSample(ch, start + i, burst[static_cast<size_t>(i)] * peakAmplitude / peak);
}

AudioBuffer runDeEsser(const AudioBuffer& input, DeEsser& de, std::vector<float>& grPerBlock) {
    AudioBuffer out = input;
    AudioBlock whole(out);
    const int n = out.getNumSamples();
    grPerBlock.clear();
    for (int start = 0; start < n; start += kBlockSize) {
        de.process(whole.getSubBlock(start, std::min(kBlockSize, n - start)));
        grPerBlock.push_back(de.getGainReductionDb());
    }
    return out;
}

void printTimeline(const char* title, const std::vector<float>& gr, double fromSec, double toSec) {
    std::printf("    %s\n    time(ms)  GR(dB)  meter (0.5 dB per bar)\n", title);
    const double blockSec = kBlockSize / kSampleRate;
    for (size_t i = 0; i < gr.size(); ++i) {
        const double t = static_cast<double>(i) * blockSec;
        if (t < fromSec || t > toSec)
            continue;
        const int bars = static_cast<int>(std::lround(gr[i] / 0.5f));
        std::printf("    %7.1f   %5.2f   %s\n", t * 1000.0, gr[i], std::string(static_cast<size_t>(bars), '|').c_str());
    }
}

double levelDb(const AudioBuffer& b, double startSec, double lengthSec, double freq) {
    const int start = secondsToSamples(startSec, kSampleRate);
    const int n = secondsToSamples(lengthSec, kSampleRate);
    return gainToDb(goertzelAmplitude(b.getReadPointer(0) + start, n, freq, kSampleRate));
}

// Binary-search between [outside, centre] for the frequency where magnitudeDb = -3.01 dB
// (the bandpass is 0 dB at the center and falls monotonically outward)
double findMinus3dbEdge(const BiquadCoefficients& c, double outside, double centre) {
    const double target = -10.0 * std::log10(2.0);
    double a = outside, b = centre;
    for (int it = 0; it < 60; ++it) {
        const double mid = 0.5 * (a + b);
        if (c.magnitudeDb(mid, kSampleRate) < target)
            a = mid;
        else
            b = mid;
    }
    return 0.5 * (a + b);
}

} // namespace

int main() {
    std::printf("Chapter 08 Example 3: de-esser (vowel + 7 kHz sibilance burst)\n");

    AudioBuffer input = makeVowel(3.0, dbToGain(-14.0f));
    for (double t : {0.5, 1.5, 2.5})
        addSibilance(input, t, 0.120, dbToGain(-12.0f));
    writeWav(outputPath("l08_deesser_input.wav"), input, kSampleRate);
    const int vowelStart = secondsToSamples(1.0, kSampleRate);
    const int vowelLen = secondsToSamples(0.4, kSampleRate);
    std::printf("\nInput: vowel-only section (1.0-1.4 s) RMS %.1f dBFS; whole-signal RMS %.1f dBFS, peak %.1f dBFS; "
                "sibilance bursts at 0.5 / 1.5 / 2.5 s, 120 ms each\n",
                gainToDb(rms(input.getReadPointer(0) + vowelStart, vowelLen)), gainToDb(rms(input, 0)),
                gainToDb(peakAbs(input)));

    DeEsser de;
    de.prepare(kSampleRate, kBlockSize, 2);
    de.setFrequency(7000.0f);
    de.setThresholdDb(-30.0f);
    de.setRangeDb(12.0f);
    std::printf("Settings: frequency %.0f Hz, threshold %.0f dB, range %.0f dB, crossover %.0f Hz (= %.4f f)\n",
                de.getFrequency(), de.getThresholdDb(), de.getRangeDb(), de.getCrossoverHz(), DeEsser::kCrossoverRatio);
    // -3 dB edges of the detection bandpass: analog prototype formula vs. measured digital RBJ bandpass (README 2.5.2)
    const auto bp = BiquadCoefficients::makeBandpass(kSampleRate, de.getFrequency(), DeEsser::kDetectorQ);
    const double edgeLow = findMinus3dbEdge(bp, 100.0, de.getFrequency());
    const double edgeHigh = findMinus3dbEdge(bp, kSampleRate * 0.5 - 1.0, de.getFrequency());
    std::printf("Detection band (-3 dB): analog formula %.0f-%.0f Hz (%.3f oct); measured digital bandpass %.0f-%.0f Hz (%.3f oct)\n",
                de.getFrequency() * DeEsser::kCrossoverRatio,
                de.getFrequency() * (DeEsser::kCrossoverRatio + 1.0f / DeEsser::kDetectorQ),
                std::log2(1.0 + 1.0 / (DeEsser::kCrossoverRatio * DeEsser::kDetectorQ)), edgeLow, edgeHigh,
                std::log2(edgeHigh / edgeLow));

    std::vector<float> grWide, grSplit, grListen;
    de.setSplitBand(false);
    const AudioBuffer wide = runDeEsser(input, de, grWide);
    de.reset();
    de.setSplitBand(true);
    const AudioBuffer split = runDeEsser(input, de, grSplit);
    de.reset();
    de.setListen(true);
    const AudioBuffer listen = runDeEsser(input, de, grListen);
    writeWav(outputPath("l08_deesser_wideband.wav"), wide, kSampleRate);
    writeWav(outputPath("l08_deesser_splitband.wav"), split, kSampleRate);
    writeWav(outputPath("l08_deesser_listen.wav"), listen, kSampleRate);

    std::printf("\n[1] Gain-reduction timeline (around the first burst, each block = %.1f ms)\n", kBlockSize * 1000.0 / kSampleRate);
    printTimeline("wideband (the split-band GR timeline is identical; the only difference is which band the gain is applied to)", grWide, 0.45, 0.72);
    float maxGr = 0.0f;
    for (float g : grWide)
        maxGr = std::max(maxGr, g);
    std::printf("    Maximum GR = %.2f dB (upper limit = range %.0f dB)\n", maxGr, de.getRangeDb());

    std::printf("\n[2] Levels of the vowel (220 Hz) and the sibilance (7 kHz), dB. Sibilance period taken at 0.52-0.60 s, vowel-only at 1.0-1.4 s\n");
    std::printf("    signal       vowel@vowel-only  vowel@sibilance  7 kHz@sibilance\n");
    const auto row = [&](const char* name, const AudioBuffer& b) {
        std::printf("    %-12s %16.2f  %15.2f  %15.2f\n", name, levelDb(b, 1.0, 0.4, kVowelHz),
                    levelDb(b, 0.52, 0.08, kVowelHz), levelDb(b, 0.52, 0.08, kSibilanceHz));
    };
    row("input", input);
    row("wideband", wide);
    row("split-band", split);
    row("listen", listen);
    std::printf("    -> wideband pulls the vowel down too (the second column differs from the first); in split-band the vowel barely moves and only 7 kHz is reduced.\n");

    std::printf("\n[3] With no sibilance (1.0-1.4 s), the split-band output = LP + HP ~= input (LR4 sums to an allpass):\n");
    std::printf("    220 Hz: input %.2f dB / output %.2f dB; 1760 Hz (8th harmonic): input %.2f dB / output %.2f dB\n",
                levelDb(input, 1.0, 0.4, kVowelHz), levelDb(split, 1.0, 0.4, kVowelHz),
                levelDb(input, 1.0, 0.4, kVowelHz * 8), levelDb(split, 1.0, 0.4, kVowelHz * 8));

    std::printf("\nOutput: dawlab_output/l08_deesser_input.wav, l08_deesser_wideband.wav, l08_deesser_splitband.wav, "
                "l08_deesser_listen.wav\n");
    return 0;
}
