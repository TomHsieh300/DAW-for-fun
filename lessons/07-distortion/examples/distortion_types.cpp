// Example 1: transfer curves, harmonic structure, and sound of the five DistortionTypes
//
// What it demonstrates:
//   (1) Prints the transfer curve of each type (input u from -3 to +3) so you can see how the waveform gets folded.
//   (2) Feeds a 220 Hz sine (-6 dBFS) through every Distortion and prints harmonics 1..8 in dBFS plus THD.
//       Key comparisons:
//         - HardClip has only odd harmonics; at very high drive its THD approaches the square wave's 48.3%;
//         - Fuzz is asymmetric → even harmonics appear;
//         - Rectify (full-wave) loses the fundamental and moves the energy to 2x the frequency → one octave up;
//         - Bitcrush's "harmonics" are really quantization noise spread across the whole band (THD looks small but it sounds dirty).
//       A self-check then compares against theory: square wave H1 = 4/π, H3/H1 = 1/3, |sin| H2 = 4/(3π), H4 = 4/(15π).
//   (2b) Fuzz gating threshold: at 30 dB drive, an input peak below 20·log10(0.1) - 30 = -50 dBFS produces no output at all.
//   (3) Runs the pluck sequence through every type and writes WAVs (each output is RMS-matched to the input for a fair comparison).
//   (4) Measures each type's delay by cross-correlation, confirming that the oversampled path and the bitcrush path (+ FixedDelay)
//       both equal getLatencySamples() (36 at 4x), and that at mix 50% dry and wet are aligned with no comb filtering.
//
// How to run: l07_distortion_types (no arguments needed; optional argv[1] = input WAV path)
//
// Output files (in dawlab_output/):
//   l07_types_dry.wav       original pluck sequence
//   l07_types_hardclip.wav  HardClip  drive 20 dB, tone 6 kHz
//   l07_types_foldback.wav  Foldback  drive 12 dB
//   l07_types_fuzz.wav      Fuzz      drive 30 dB, tone 4 kHz
//   l07_types_rectify.wav   Rectify   drive 6 dB (full-wave; sounds an octave higher, synth-like)
//   l07_types_bitcrush.wav  Bitcrush  6 bit, downsample x4
//
// What to listen for: hard clip is hard and bright; foldback sounds synth-like and its overtones "sweep" with the level; fuzz is rough and the tail cuts off abruptly (gating);
//         rectify sounds an octave higher and has lost its "bottom"; bitcrush has hissy quantization noise and metallic aliasing.

#include <dawlab/Common.h>
#include <dawlab/dsp/Distortion.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

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

// The settings each type uses in this example
struct Preset {
    dawlab::dsp::DistortionType type;
    float driveDb;
    float toneHz;
    float bits;
    int downsample;
    const char* wavName;
};

const Preset kPresets[] = {
    {dawlab::dsp::DistortionType::HardClip, 20.0f, 6000.0f, 24.0f, 1, "l07_types_hardclip.wav"},
    {dawlab::dsp::DistortionType::Foldback, 12.0f, 20000.0f, 24.0f, 1, "l07_types_foldback.wav"},
    {dawlab::dsp::DistortionType::Fuzz, 30.0f, 4000.0f, 24.0f, 1, "l07_types_fuzz.wav"},
    {dawlab::dsp::DistortionType::Rectify, 6.02f, 20000.0f, 24.0f, 1, "l07_types_rectify.wav"},
    {dawlab::dsp::DistortionType::Bitcrush, 0.0f, 20000.0f, 6.0f, 4, "l07_types_bitcrush.wav"},
};

void configure(dawlab::dsp::Distortion& d, const Preset& p) {
    d.setType(p.type);
    d.setDriveDb(p.driveDb);
    d.setToneHz(p.toneHz);
    d.setBitDepth(p.bits);
    d.setDownsampleFactor(p.downsample);
    d.setMix(1.0f);
    d.setOutputGainDb(0.0f);
}

// Match the output RMS to the input RMS (gain staging: distortion makes the level jump, and without compensation the timbres cannot be compared fairly)
float rmsMatch(const dawlab::AudioBuffer& in, dawlab::AudioBuffer& out) {
    const float inRms = dawlab::rms(in, 0);
    const float outRms = dawlab::rms(out, 0);
    const float g = outRms > 1.0e-9f ? inRms / outRms : 1.0f;
    out.applyGain(g);
    return dawlab::gainToDb(g);
}

} // namespace

int main(int argc, char** argv) {
    using dawlab::dsp::Distortion;
    using dawlab::dsp::DistortionType;

    // ---- (1) transfer curves ----
    std::printf("(1) transfer curve y = shape(type, u) (Bitcrush uses 8-bit quantization)\n");
    std::printf("%6s", "u");
    for (const Preset& p : kPresets)
        std::printf(" %10s", dawlab::dsp::distortionTypeName(p.type));
    std::printf("\n");
    for (int i = -12; i <= 12; ++i) {
        const float u = 0.25f * static_cast<float>(i);
        std::printf("%6.2f", u);
        for (const Preset& p : kPresets)
            std::printf(" %10.4f", Distortion::shape(p.type, u));
        std::printf("\n");
    }

    // ---- (2) Harmonic analysis: 220 Hz sine, -6 dBFS ----
    const double f0 = 220.0;
    constexpr int kNumHarmonics = 8;
    std::printf("\n(2) Harmonics (dBFS) and THD (up to the 50th harmonic) of a 220 Hz sine (-6 dBFS) through each type\n");
    std::printf("%10s %7s", "type", "drive");
    for (int h = 1; h <= kNumHarmonics; ++h)
        std::printf("   H%d", h);
    std::printf("     THD\n");

    // Settings for the analysis (several drives of the same type: hard clip approaches a square wave, fuzz's even harmonics rise and fall with drive)
    const Preset analysis[] = {
        {DistortionType::HardClip, 10.0f, 20000.0f, 24.0f, 1, ""},
        {DistortionType::HardClip, 20.0f, 20000.0f, 24.0f, 1, ""},
        {DistortionType::HardClip, 40.0f, 20000.0f, 24.0f, 1, ""},
        {DistortionType::Foldback, 12.0f, 20000.0f, 24.0f, 1, ""},
        {DistortionType::Fuzz, 12.0f, 20000.0f, 24.0f, 1, ""},
        {DistortionType::Fuzz, 30.0f, 20000.0f, 24.0f, 1, ""},
        {DistortionType::Rectify, 6.02f, 20000.0f, 24.0f, 1, ""},
        {DistortionType::Bitcrush, 0.0f, 20000.0f, 6.0f, 1, ""},
    };
    bool ok = true;
    constexpr int kNumAnalysis = static_cast<int>(sizeof(analysis) / sizeof(analysis[0]));
    double levels[kNumAnalysis][kNumHarmonics + 1] = {}; // levels[a][h]: harmonic h (dBFS) of analysis setting a, h = 1..8
    double thd[kNumAnalysis] = {};
    for (int a = 0; a < kNumAnalysis; ++a) {
        const Preset& p = analysis[a];
        dawlab::AudioBuffer sine = dawlab::makeSine(f0, kSampleRate, 1.0, 0.5f, 1);
        Distortion d;
        d.prepare(kSampleRate, kBlockSize, 1);
        configure(d, p); // tone = 20 kHz (bypass): tone is turned off for the analysis so the full harmonic structure is visible
        processInBlocks(sine, d);
        ok = ok && !dawlab::hasNanOrInf(sine);

        // Skip the first 0.2 s (oversampler latency and the DC blocker transient) and analyze the following 0.5 s
        const int skip = dawlab::secondsToSamples(0.2, kSampleRate);
        const int n = dawlab::secondsToSamples(0.5, kSampleRate);
        const float* x = sine.getReadPointer(0) + skip;

        std::printf("%10s %5.1fdB", dawlab::dsp::distortionTypeName(p.type), p.driveDb);
        for (int h = 1; h <= kNumHarmonics; ++h) {
            levels[a][h] = dawlab::gainToDb(dawlab::goertzelAmplitude(x, n, f0 * h, kSampleRate));
            std::printf(" %5.1f", levels[a][h]);
        }
        if (p.type == DistortionType::Rectify) {
            std::printf("     n/a (fundamental is gone; THD relative to H1 is meaningless)\n");
        } else {
            thd[a] = dawlab::thdPercent(x, n, f0, kSampleRate, 50);
            std::printf(" %6.1f%%\n", thd[a]);
        }
    }
    std::printf("(The theoretical THD of a square wave = sqrt(pi^2/8 - 1) = 48.3%%; counting only up to the 50th harmonic it is about 47.3%%.\n"
                "  The higher the HardClip drive, the closer to a square wave; Fuzz has even harmonics at moderate drive, but at extreme drive both sides saturate,\n"
                "  the waveform becomes a \"DC-offset square wave\" and the even harmonics actually weaken -- the remaining DC is removed by the DC blocker.)\n");

    // ---- self-check: the numbers in README sections 2.2-2.5 ----
    // Indices into analysis[]: 0/1/2 = HardClip 10/20/40 dB, 3 = Foldback 12, 4/5 = Fuzz 12/30, 6 = Rectify, 7 = Bitcrush
    {
        const double squareH1 = dawlab::gainToDb(4.0 / dawlab::kPi);         // square-wave fundamental 4/π → +2.10 dBFS
        const double rectH2 = dawlab::gainToDb(4.0 / (3.0 * dawlab::kPi));    // |sin| with peak 1: 4/(3π) → -7.44 dBFS
        const double rectH4 = dawlab::gainToDb(4.0 / (15.0 * dawlab::kPi));   // 4/(15π) → -21.42 dBFS
        bool evenFree = true; // HardClip / Foldback are odd functions → no even harmonics may be measurable
        for (int a : {0, 1, 2, 3})
            for (int h = 2; h <= kNumHarmonics; h += 2)
                evenFree = evenFree && levels[a][h] < -80.0;
        const bool c1 = thd[2] > 44.0 && thd[2] < 48.3;                              // 40 dB: close to the square wave's 48.3%
        const bool c2 = std::abs(levels[2][1] - squareH1) < 0.3;                     // H1 → 4/π
        const bool c3 = std::abs((levels[2][3] - levels[2][1]) + 9.54) < 0.5;        // H3/H1 → 1/3 (-9.54 dB)
        const bool c4 = levels[3][3] > levels[3][1];                                 // Foldback 12 dB: H3 is larger than H1
        const bool c5 = levels[4][2] > -30.0 && levels[5][2] < levels[4][2] - 10.0;  // Fuzz: even harmonics are most prominent at moderate drive
        const bool c6 = levels[6][1] < -60.0 && std::abs(levels[6][2] - rectH2) < 0.3 &&
                        std::abs(levels[6][4] - rectH4) < 0.3;                       // Rectify: fundamental gone, H2/H4 follow the Fourier series
        std::printf("self-check: HardClip/Foldback have no even harmonics %s; HardClip 40 dB: THD %s, H1 = 4/π (%.2f dB) %s, "
                    "H3 - H1 = -9.54 dB %s; Foldback H3 > H1 %s; Fuzz even harmonics at 12 dB > 30 dB %s; "
                    "Rectify H1 gone, H2 = %.2f, H4 = %.2f dBFS %s\n",
                    evenFree ? "OK" : "NG", c1 ? "OK" : "NG", squareH1, c2 ? "OK" : "NG", c3 ? "OK" : "NG",
                    c4 ? "OK" : "NG", c5 ? "OK" : "NG", rectH2, rectH4, c6 ? "OK" : "NG");
        ok = ok && evenFree && c1 && c2 && c3 && c4 && c5 && c6;
    }

    // ---- (2b) Fuzz gating threshold: dead zone d = 0.1, drive G dB → an input peak below 20 log10(d) - G dBFS is completely silent ----
    {
        const float driveDb = 30.0f;
        const double thresholdDb = dawlab::gainToDb(static_cast<double>(Distortion::kFuzzDeadZone)) - driveDb; // -50 dBFS
        std::printf("\n(2b) Fuzz gating threshold at drive %.0f dB (theory %.1f dBFS): output peak of a 220 Hz sine\n", driveDb, thresholdDb);
        std::printf("%12s %14s\n", "input(dBFS)", "output peak");
        for (float inDb : {-60.0f, -53.0f, -47.0f, -40.0f}) {
            dawlab::AudioBuffer sine = dawlab::makeSine(f0, kSampleRate, 0.5, dawlab::dbToGain(inDb), 1);
            Distortion d;
            d.prepare(kSampleRate, kBlockSize, 1);
            d.setType(DistortionType::Fuzz);
            d.setDriveDb(driveDb);
            processInBlocks(sine, d);
            const int skip = dawlab::secondsToSamples(0.1, kSampleRate); // skip the first drive ramp block
            const float peak = dawlab::peakAbs(sine.getReadPointer(0) + skip, sine.getNumSamples() - skip);
            std::printf("%12.1f %14.6f%s\n", inDb, peak, peak == 0.0f ? " (completely silent)" : "");
            // Below the threshold the output must be completely silent; above it there must be sound
            if (inDb < thresholdDb)
                ok = ok && peak == 0.0f;
            else
                ok = ok && peak > 1.0e-3f;
        }
    }

    // ---- (3) pluck sequence → WAV ----
    dawlab::AudioBuffer dry;
    if (argc > 1) {
        dawlab::WavData wav;
        std::string error;
        if (!dawlab::readWav(argv[1], wav, &error)) {
            std::printf("Failed to read: %s\n", error.c_str());
            return 1;
        }
        dry = wav.buffer;
        std::printf("\n(3) Using input file %s (%d ch, %d samples)\n", argv[1], dry.getNumChannels(), dry.getNumSamples());
    } else {
        dry = dawlab::makePluckSequence(kSampleRate, 4.0, 0.4f, 2);
        std::printf("\n(3) pluck sequence (4 s, 2 ch) through each type\n");
    }
    dawlab::writeWav(dawlab::outputPath("l07_types_dry.wav"), dry, kSampleRate);
    std::printf("%10s %10s %10s %12s %10s\n", "type", "peak(dBFS)", "rms(dBFS)", "RMS comp(dB)", "latency");
    std::printf("%10s %10.2f %10.2f %12s %10s\n", "dry", dawlab::gainToDb(dawlab::peakAbs(dry)),
                dawlab::gainToDb(dawlab::rms(dry, 0)), "-", "-");

    for (const Preset& p : kPresets) {
        dawlab::AudioBuffer out = dry;
        Distortion d;
        d.prepare(kSampleRate, kBlockSize, out.getNumChannels());
        configure(d, p);
        processInBlocks(out, d);
        ok = ok && !dawlab::hasNanOrInf(out);
        const float peakBefore = dawlab::peakAbs(out);
        const float rmsBefore = dawlab::rms(out, 0);
        const float compDb = rmsMatch(dry, out);
        std::printf("%10s %10.2f %10.2f %+12.2f %7d smp\n", dawlab::dsp::distortionTypeName(p.type),
                    dawlab::gainToDb(peakBefore), dawlab::gainToDb(rmsBefore), compDb, d.getLatencySamples());
        dawlab::writeWav(dawlab::outputPath(p.wavName), out, kSampleRate);
    }
    std::printf("Wrote dawlab_output/l07_types_*.wav\n");

    // ---- (4) Measure the latency: are the two paths (oversampled shaper / bitcrush + FixedDelay) really aligned? ----
    // getLatencySamples() is only a "claim"; here cross-correlation finds how many samples the output is really delayed relative to the input.
    // At drive 0 dB, HardClip / Foldback / Bitcrush (24 bit) are nearly transparent; Fuzz only swallows a little of the small signal;
    // Rectify uses half-wave instead (full-wave |u| is an even function whose correlation with the input is 0, so nothing could be measured).
    std::printf("\n(4) Measured latency (white noise 0.3, drive 0 dB, 4x oversampling) and comb filtering at mix 50%%\n");
    std::printf("%10s %12s %12s %16s\n", "type", "claimed(smp)", "measured", "mix50-100% (dB)");
    constexpr int kExpectedLatency4x = 36; // formula in section 2.8: (124 + 22) / 4 = 36.5 → 36
    const dawlab::AudioBuffer noise = dawlab::makeWhiteNoise(kSampleRate, 0.5, 0.3f, 1);
    for (const Preset& p : kPresets) {
        auto makeDistortion = [&](float mix) {
            Distortion d;
            d.prepare(kSampleRate, kBlockSize, 1);
            configure(d, p);
            d.setDriveDb(0.0f);
            d.setToneHz(Distortion::kToneBypassHz);
            d.setBitDepth(24.0f);
            d.setDownsampleFactor(1);
            d.setRectifyMode(dawlab::dsp::RectifyMode::HalfWave);
            d.setMix(mix);
            return d;
        };

        // cross-correlation: r(lag) = Σ y[n]·x[n − lag]; the lag at the maximum is the delay
        dawlab::AudioBuffer out = noise;
        Distortion d = makeDistortion(1.0f);
        processInBlocks(out, d);
        const float* x = noise.getReadPointer(0);
        const float* y = out.getReadPointer(0);
        const int n = noise.getNumSamples();
        int bestLag = -1;
        double bestR = 0.0;
        for (int lag = 0; lag < 200; ++lag) {
            double r = 0.0;
            for (int i = 4800; i < n; ++i) // skip the first 0.1 s (DC blocker transient)
                r += static_cast<double>(y[i]) * static_cast<double>(x[i - lag]);
            if (r > bestR) {
                bestR = r;
                bestLag = lag;
            }
        }

        // mix 50% vs 100%: when wet and dry are aligned they are equally large at 15 kHz; if misaligned there is comb filtering (some frequencies cancel)
        auto gainAt15k = [&](float mix) {
            dawlab::AudioBuffer s = dawlab::makeSine(15000.0, kSampleRate, 0.5, 0.1f, 1);
            Distortion dm = makeDistortion(mix);
            processInBlocks(s, dm);
            const int skip = 4800;
            return dawlab::gainToDb(dawlab::rms(s.getReadPointer(0) + skip, s.getNumSamples() - skip) /
                                    (0.1f / std::sqrt(2.0f)));
        };
        const bool fuzzOrRectify = p.type == DistortionType::Fuzz || p.type == DistortionType::Rectify;
        const float combDb = fuzzOrRectify ? 0.0f : gainAt15k(0.5f) - gainAt15k(1.0f);

        std::printf("%10s %12d %12d", dawlab::dsp::distortionTypeName(p.type), d.getLatencySamples(), bestLag);
        if (fuzzOrRectify)
            std::printf(" %16s\n", "- (too nonlinear)");
        else
            std::printf(" %+16.3f\n", combDb);
        ok = ok && bestLag == d.getLatencySamples() && d.getLatencySamples() == kExpectedLatency4x;
        ok = ok && std::abs(combDb) < 0.1f;
    }
    std::printf("self-check: measured delay of every type = getLatencySamples() = %d; mix 50%% and 100%% differ by < 0.1 dB at 15 kHz\n",
                kExpectedLatency4x);

    std::printf("\n%s\n", ok ? "All OK." : "[FAIL] Some item did not meet expectations!");
    return ok ? 0 : 1;
}
