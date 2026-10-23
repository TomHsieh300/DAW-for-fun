// l02_filter_sweep: runs white noise and a log sweep through LPF / HPF / BPF / Notch and writes WAVs to listen to.
//
// What it shows:
//   - Uses Biquad to process the whole signal block by block (block size 512), simulating an audio callback.
//   - How different the four filter types sound with the same 1 kHz cutoff / center.
//   - Matches the listening impression with RMS numbers: white noise has its energy spread evenly
//     over 0..24 kHz, so a 1 kHz LPF keeps only about 1/24 of the bandwidth and the RMS should drop
//     sharply; the HPF barely changes it; the BPF drops it even more; the notch barely changes it.
//
// How to run: run with no arguments; or `l02_filter_sweep your.wav` (uses your file as the input instead).
// Output WAVs (in dawlab_output/):
//   l02_noise_dry.wav, l02_noise_lpf.wav, l02_noise_hpf.wav, l02_noise_bpf.wav, l02_noise_notch.wav
//   l02_sweep_dry.wav, l02_sweep_lpf.wav, l02_sweep_hpf.wav, l02_sweep_bpf.wav, l02_sweep_notch.wav
// What you should hear:
//   - noise_lpf sounds "muffled", as if heard through a wall; noise_hpf is just a "hiss"; noise_bpf
//     is a narrow band like an old telephone; noise_notch is almost the same as dry (only a small
//     chunk around 1 kHz is cut out).
//   - The sweep runs from 20 Hz to 20 kHz: the lpf version gets quieter and quieter after 1 kHz; the
//     hpf is the opposite; the bpf is loudest only when passing 1 kHz; the notch has a "dip" at the
//     moment it passes 1 kHz.
#include <dawlab/Common.h>
#include <dawlab/dsp/Biquad.h>

#include <cstdio>
#include <string>
#include <vector>

using namespace dawlab;
using dawlab::dsp::Biquad;
using dawlab::dsp::BiquadCoefficients;

constexpr int kBlockSize = 512;

// Process block by block (simulating an audio callback) and return the processed new buffer
static AudioBuffer processInBlocks(const AudioBuffer& input, const BiquadCoefficients& coeffs, double sampleRate) {
    AudioBuffer out = input; // make a copy and process the copy in place
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
            std::printf("Using input file %s (%.0f Hz, %d ch, %.2f s)\n", argv[1], sampleRate, noise.getNumChannels(),
                        noise.getNumSamples() / sampleRate);
        } else {
            std::printf("Cannot read %s: %s, falling back to the synthetic signal\n", argv[1], err.c_str());
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

    std::printf("=== Chapter 02: noise / sweep through four biquads (fc = %.0f Hz, block = %d) ===\n\n", fc, kBlockSize);
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
            // White noise has its power spread evenly, so output / input power ratio = mean |H|².
            // Cross-check by integrating the theoretical response over 0..Nyquist once (numerical integration, 2000 points).
            double power = 0.0;
            const int steps = 2000;
            for (int i = 0; i < steps; ++i) {
                const double f = (i + 0.5) * (sampleRate * 0.5) / steps;
                const double mag = std::abs(v.c.response(f, sampleRate));
                power += mag * mag / steps;
            }
            const double expectedDb = 10.0 * std::log10(power);
            const double measuredDb = gainToDb(nRms / noiseDryRms);
            std::printf("              Noise RMS change: measured %.2f dB, theory ∫|H|² = %.2f dB\n", measuredDb, expectedDb);
            DAWLAB_CHECK_NEAR(measuredDb, expectedDb, 0.3);
        }
    }
    std::printf("\nWAVs written to dawlab_output/l02_noise_*.wav and l02_sweep_*.wav\n\n");
    return dawlab::test::summary("l02_filter_sweep");
}
