// Example 2: a simple guitar amp chain -- input HPF → pre-EQ → preamp → tone stack → power amp → cabinet
//
// What it demonstrates:
//   (1) Prints frequency-response tables for the three linear stages (pre-EQ, tone stack, cabinet) to show what each stage does to the spectrum.
//   (2) Three presets (Clean / Crunch / Lead) process the same pluck sequence and write WAVs (RMS-matched to the input).
//   (3) The difference between the cabinet on and off: a 110 Hz sine goes through the Lead preset and the dBFS of the high harmonics is printed.
//       The cabinet (LR4 lowpass at 4.5 kHz) cuts the harmonics above 5 kHz -- which is why recording the amp output directly
//       sounds so different from a microphone on the speaker cabinet.
//
// How to run: l07_amp_chain (no arguments needed; optional argv[1] = input WAV path, e.g. a dry guitar DI)
//
// Output files (in dawlab_output/):
//   l07_amp_dry.wav      input
//   l07_amp_clean.wav    Clean: preamp 6 dB, power amp 0 dB
//   l07_amp_crunch.wav   Crunch: preamp 18 dB, power amp 6 dB
//   l07_amp_lead.wav     Lead: preamp 30 dB, power amp 12 dB, mid +3 dB, pre-EQ +8 dB
//   l07_amp_lead_nocab.wav  Lead with the cabinet turned off (hear how harsh it is without the speaker simulation)
//
// What to listen for: Clean is just a bit "thicker"; Crunch has grit and cleans up when you play softer; Lead has long sustain and
//         the note tails get pushed by the preamp into sustained distortion; the nocab version is harsh and mosquito-like -- those are the high harmonics the cabinet filters out.

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

    // ---- (1) Frequency response of the linear stages ----
    {
        AmpChain amp;
        amp.prepare(kSampleRate, kBlockSize, 1);
        applyPreset(amp, kPresets[2], true); // Lead's EQ settings (getXxxMagnitudeDb() computes from the current parameters directly, no need to process first)

        std::printf("(1) Frequency response of the linear stages (Lead preset, dB)\n");
        std::printf("%8s %10s %12s %10s\n", "freq", "pre-EQ", "tone stack", "cabinet");
        for (double f : {50.0, 80.0, 110.0, 200.0, 400.0, 700.0, 800.0, 1000.0, 2000.0, 3000.0, 4500.0, 6000.0, 9000.0})
            std::printf("%8.0f %10.2f %12.2f %10.2f\n", f, amp.getPreEqMagnitudeDb(f), amp.getToneStackMagnitudeDb(f),
                        amp.getCabinetMagnitudeDb(f));
        std::printf("(The cabinet should be -6 dB at the 4500 Hz cutoff: two Q=0.7071 Butterworth lowpass filters in cascade = 4th-order Linkwitz-Riley)\n");
        std::printf("latency = %d samples (one 4x oversampler each in the preamp and the power amp)\n", amp.getLatencySamples());

        // self-check: the pre-EQ peak is exactly the set gain at the center frequency; the LR4 at cutoff is 2 × (-3.01) dB;
        // the resonance peak at 110 Hz is +4 dB (the LR4 is almost 0 dB at 110 Hz); latency = the sum of two 4x oversamplers' delays
        dawlab::dsp::Oversampler os4(2);
        os4.prepare(kSampleRate, kBlockSize, 1);
        const bool c1 = std::abs(amp.getPreEqMagnitudeDb(800.0) - 8.0) < 0.01;
        const bool c2 = std::abs(amp.getCabinetMagnitudeDb(4500.0) - (-6.02)) < 0.02;
        const bool c3 = std::abs(amp.getCabinetMagnitudeDb(110.0) - 4.0) < 0.05;
        const bool c4 = amp.getLatencySamples() == 2 * os4.getLatencySamples();
        std::printf("self-check: pre-EQ@800 Hz = +8 dB %s, cabinet@4500 Hz = -6.02 dB %s, cabinet@110 Hz = +4 dB %s, "
                    "latency = 2 × %d %s\n",
                    c1 ? "OK" : "NG", c2 ? "OK" : "NG", c3 ? "OK" : "NG", os4.getLatencySamples(), c4 ? "OK" : "NG");
        ok = ok && c1 && c2 && c3 && c4;
    }

    // ---- (2) Three presets process the pluck sequence ----
    dawlab::AudioBuffer dry;
    if (argc > 1) {
        dawlab::WavData wav;
        std::string error;
        if (!dawlab::readWav(argv[1], wav, &error)) {
            std::printf("Failed to read: %s\n", error.c_str());
            return 1;
        }
        dry = wav.buffer;
        std::printf("\n(2) Using input file %s\n", argv[1]);
    } else {
        dry = dawlab::makePluckSequence(kSampleRate, 4.0, 0.4f, 2);
        std::printf("\n(2) pluck sequence through the three presets (output RMS matched to the input)\n");
    }
    dawlab::writeWav(dawlab::outputPath("l07_amp_dry.wav"), dry, kSampleRate);
    std::printf("%8s %8s %8s %14s %14s %14s\n", "preset", "preamp", "power", "peak pre(dBFS)", "rms pre(dBFS)", "RMS comp(dB)");
    std::printf("%8s %8s %8s %14.2f %14.2f %14s\n", "dry", "-", "-", dawlab::gainToDb(dawlab::peakAbs(dry)),
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
        // gain staging: the more drive, the louder after processing (Clean < Crunch < Lead), and all are louder than the input
        ok = ok && rmsBefore > previousRms && rmsBefore > dawlab::rms(dry, 0);
        previousRms = rmsBefore;
        const float comp = rmsMatch(dry, out);
        std::printf("%8s %6.0fdB %6.0fdB %14.2f %14.2f %+14.2f\n", p.name, p.preampDriveDb, p.powerDriveDb,
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

    // ---- (3) Cabinet on / off: high harmonics of a 110 Hz sine ----
    {
        std::printf("\n(3) 110 Hz sine (-12 dBFS) through the Lead preset: harmonics in dBFS, cabinet on vs off\n");
        const int harmonics[] = {1, 2, 3, 5, 9, 21, 41, 61, 81};
        std::printf("%8s %8s %10s %10s %8s %12s\n", "harm.", "freq", "cab off", "cab on", "diff", "cab resp.");
        const double f0 = 110.0;
        dawlab::AudioBuffer outs[2];
        for (int c = 0; c < 2; ++c) {
            outs[c] = dawlab::makeSine(f0, kSampleRate, 1.0, 0.25f, 1);
            AmpChain amp;
            amp.prepare(kSampleRate, kBlockSize, 1);
            applyPreset(amp, kPresets[2], c == 1);
            processInBlocks(outs[c], amp);
        }
        AmpChain ref; // used only to look up the cabinet's frequency response
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
            // The cabinet is last in the chain and linear: each harmonic's "on - off" should equal the cabinet response at that frequency exactly
            // (compare only the measurable harmonics; the even harmonics sit at the -100 dB measurement floor)
            if (off > -80.0)
                matches = matches && std::abs((on - off) - cab) < 0.5;
            if (f > 8000.0)
                ok = ok && (on - off) < -15.0; // the cabinet must cut the high frequencies
        }
        std::printf("self-check: each harmonic's \"on - off\" = cabinet response (±0.5 dB) %s\n", matches ? "OK" : "NG");
        ok = ok && matches;
    }

    std::printf("\nWrote dawlab_output/l07_amp_*.wav\n");
    std::printf("%s\n", ok ? "All OK." : "[FAIL] Some item did not meet expectations!");
    return ok ? 0 : 1;
}
