// l02_cutoff_modulation: fast cutoff modulation, biquad vs. TPT state-variable filter.
//
// What it shows:
//   - A 6 Hz LFO sweeps the lowpass cutoff quickly between 200 Hz and 8 kHz (like an auto-wah).
//   - A biquad's coefficients can only be swapped between blocks (once every 512 samples,
//     about 10.7 ms), so the cutoff advances in steps: the resonance peak sits at the wrong
//     frequency and jumps from step to step ([B2] quantifies the difference from an ideal smooth
//     modulation), which sounds grainy.
//     [B3] also measures the overshoot when the cutoff jumps suddenly: neither the TDF-II biquad
//     nor the SVF overshoots much (the TDF-II state is of the same magnitude as its output).
//   - The StateVariableFilter (TPT) can update its cutoff on every sample (one tan), and its
//     state variables are physical quantities, so it is much smoother under modulation.
//   - Their static frequency responses are identical (verified at the end with measureGainDb);
//     the difference only appears when things are moving.
//
// How to run: run with no arguments. It writes WAV files (to dawlab_output/):
//   l02_mod_dry.wav, l02_mod_biquad_block.wav, l02_mod_svf_sample.wav
// What you should hear: both are wah-wah sounds. In biquad_block the resonance peak jumps in
//   steps (once every 10.7 ms, and at the LFO's fastest almost an octave per step), which sounds
//   grainy; svf_sample slides continuously. Peak / RMS are about the same for the two, so the
//   difference can only be measured with the "difference from the ideal smooth modulation" of [B2].
#include <dawlab/Common.h>
#include <dawlab/dsp/Biquad.h>
#include <dawlab/dsp/StateVariableFilter.h>

#include <cstdio>
#include <vector>

using namespace dawlab;
using dawlab::dsp::Biquad;
using dawlab::dsp::BiquadCoefficients;
using dawlab::dsp::StateVariableFilter;

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr double kLfoHz = 6.0;
constexpr double kQ = 6.0;

// LFO: a cutoff that swings sinusoidally along a logarithmic frequency axis
static double cutoffAt(int sampleIndex) {
    const double lfo = std::sin(kTwoPi * kLfoHz * sampleIndex / kSampleRate); // -1..1
    return 200.0 * std::pow(40.0, 0.5 + 0.5 * lfo);                             // 200 Hz..8 kHz
}

int main() {
    std::printf("=== Chapter 02: fast cutoff modulation (LFO %.0f Hz, 200 Hz..8 kHz, Q = %.0f) ===\n\n", kLfoHz, kQ);

    AudioBuffer dry = makePluckSequence(kSampleRate, 4.0, 0.35f, 2);
    writeWav(outputPath("l02_mod_dry.wav"), dry, kSampleRate);
    const int total = dry.getNumSamples();

    // ---- A. biquad: update the coefficients once per block ---------------------------
    AudioBuffer outBiquad = dry;
    {
        Biquad bq;
        bq.prepare(kSampleRate, kBlockSize, 2);
        AudioBlock whole(outBiquad);
        for (int start = 0; start < total; start += kBlockSize) {
            const int len = std::min(kBlockSize, total - start);
            bq.setCoefficients(BiquadCoefficients::makeLowpass(kSampleRate, cutoffAt(start), kQ));
            bq.process(whole.getSubBlock(start, len));
        }
    }

    // ---- B. SVF: update the cutoff on every sample -----------------------------------
    AudioBuffer outSvf = dry;
    {
        StateVariableFilter svf;
        svf.prepare(kSampleRate, kBlockSize, 2);
        svf.setType(StateVariableFilter::Type::Lowpass);
        svf.setQ(static_cast<float>(kQ));
        for (int i = 0; i < total; ++i) {
            svf.setCutoff(static_cast<float>(cutoffAt(i)));
            for (int ch = 0; ch < 2; ++ch)
                outSvf.setSample(ch, i, svf.processSample(ch, outSvf.getSample(ch, i)));
        }
    }

    std::printf("    %-22s  peak %7.2f dBFS   RMS %7.2f dB\n", "dry", gainToDb(peakAbs(dry)), gainToDb(rms(dry, 0)));
    std::printf("    %-22s  peak %7.2f dBFS   RMS %7.2f dB\n", "biquad (per-block)", gainToDb(peakAbs(outBiquad)),
                gainToDb(rms(outBiquad, 0)));
    std::printf("    %-22s  peak %7.2f dBFS   RMS %7.2f dB\n", "SVF (per-sample)", gainToDb(peakAbs(outSvf)),
                gainToDb(rms(outSvf, 0)));
    DAWLAB_CHECK(!hasNanOrInf(outBiquad) && !hasNanOrInf(outSvf));
    writeWav(outputPath("l02_mod_biquad_block.wav"), outBiquad, kSampleRate, 32);
    writeWav(outputPath("l02_mod_svf_sample.wav"), outSvf, kSampleRate, 32);

    // How large is the transient of "jumping the coefficients between blocks"? Measure the peak of
    // the output's first difference (the largest jump between samples). Both use the same LFO,
    // so any difference in smoothness shows up directly in this number.
    auto maxStep = [](const AudioBuffer& b) {
        float m = 0.0f;
        for (int i = 1; i < b.getNumSamples(); ++i)
            m = std::max(m, std::abs(b.getSample(0, i) - b.getSample(0, i - 1)));
        return m;
    };
    std::printf("\n    Largest jump between samples: biquad %.4f, SVF %.4f (the pluck's own transient dominates this number, so no difference is visible)\n",
                maxStep(outBiquad), maxStep(outSvf));

    // ---- B2. Measure the effect of a "stepped cutoff" with a steady-state signal -----------
    // Input: a 110 Hz band-limited saw (harmonics up to 8 kHz) with no transient of its own.
    // A biquad that recomputes its RBJ coefficients on every sample serves as the ideal smooth
    // modulation. Measure:
    //   - the difference between the per-block biquad and it (= the zipper component caused by the
    //     stepped cutoff, RMS dB re. signal)
    //   - the difference between the per-sample SVF and it (the two structures have different state
    //     semantics during modulation, but both are smooth)
    std::printf("\n[B2] Steady-state saw 110 Hz, difference from the per-sample biquad (RMS, dB re. output)\n");
    std::vector<float> saw(static_cast<size_t>(48000 * 2));
    for (size_t i = 0; i < saw.size(); ++i) {
        double v = 0.0;
        for (int h = 1; h * 110.0 < 8000.0; ++h)
            v += std::sin(kTwoPi * 110.0 * h * static_cast<double>(i) / kSampleRate) / h;
        saw[i] = static_cast<float>(0.3 * v);
    }
    {
        Biquad perBlock, perSample;
        perBlock.prepare(kSampleRate, kBlockSize, 1);
        perSample.prepare(kSampleRate, kBlockSize, 1);
        StateVariableFilter svfMod;
        svfMod.prepare(kSampleRate, kBlockSize, 1);
        svfMod.setQ(static_cast<float>(kQ));
        double refPow = 0.0, errBlock = 0.0, errSvf = 0.0;
        for (size_t i = 0; i < saw.size(); ++i) {
            const int n = static_cast<int>(i);
            if (n % kBlockSize == 0)
                perBlock.setCoefficients(BiquadCoefficients::makeLowpass(kSampleRate, cutoffAt(n), kQ));
            perSample.setCoefficients(BiquadCoefficients::makeLowpass(kSampleRate, cutoffAt(n), kQ));
            svfMod.setCutoff(static_cast<float>(cutoffAt(n)));
            const double yBlock = perBlock.processSample(0, saw[i]);
            const double yRef = perSample.processSample(0, saw[i]);
            const double ySvf = svfMod.processSample(0, saw[i]);
            if (n >= 4800) { // skip the first 0.1 s
                refPow += yRef * yRef;
                errBlock += (yBlock - yRef) * (yBlock - yRef);
                errSvf += (ySvf - yRef) * (ySvf - yRef);
            }
        }
        const double blockDb = 10.0 * std::log10(errBlock / refPow);
        const double svfDb = 10.0 * std::log10(errSvf / refPow);
        std::printf("    biquad per-block (coefficients swapped every %d samples)   %7.2f dB\n", kBlockSize, blockDb);
        std::printf("    SVF per-sample                                             %7.2f dB\n", svfDb);
        std::printf("    -> The error from the block steps is almost as large as the signal itself (the resonance peak sits at the wrong frequency);\n"
                    "       the per-sample SVF is much closer to the per-sample biquad (the remaining difference comes from the two structures' different state semantics during modulation).\n");
        DAWLAB_CHECK(blockDb > -30.0);
        DAWLAB_CHECK(svfDb < blockDb - 10.0);
    }

    // ---- B3. A sudden large cutoff jump (200 Hz -> 8 kHz): overshoot after the coefficient change ----
    // Run the steady-state saw with the cutoff at 200 Hz for 0.5 s, then jump to 8 kHz within one sample.
    // Compare "the peak within 20 ms after the jump" with "the steady-state peak at 8 kHz":
    // the closer to 1, the better the state is compatible with the new coefficients.
    std::printf("\n[B3] Sudden cutoff jump 200 Hz -> 8 kHz: peak within 20 ms after the jump / steady-state peak at 8 kHz\n");
    {
        auto overshoot = [&](auto&& setCutoff, auto&& processOne) {
            const int jumpAt = 24000, window = 960;
            float peakAfter = 0.0f, peakSteady = 0.0f;
            setCutoff(200.0);
            for (size_t i = 0; i < saw.size(); ++i) {
                const int n = static_cast<int>(i);
                if (n == jumpAt)
                    setCutoff(8000.0);
                const float y = std::abs(processOne(saw[i]));
                if (n >= jumpAt && n < jumpAt + window)
                    peakAfter = std::max(peakAfter, y);
                if (n >= jumpAt + 24000)
                    peakSteady = std::max(peakSteady, y);
            }
            return peakAfter / peakSteady;
        };
        Biquad bqJump;
        bqJump.prepare(kSampleRate, kBlockSize, 1);
        StateVariableFilter svfJump;
        svfJump.prepare(kSampleRate, kBlockSize, 1);
        svfJump.setQ(static_cast<float>(kQ));
        const double rBq = overshoot(
            [&](double fc) { bqJump.setCoefficients(BiquadCoefficients::makeLowpass(kSampleRate, fc, kQ)); },
            [&](float x) { return bqJump.processSample(0, x); });
        const double rSvf = overshoot([&](double fc) { svfJump.setCutoff(static_cast<float>(fc)); },
                                      [&](float x) { return svfJump.processSample(0, x); });
        std::printf("    biquad (TDF-II)  %.3f\n    SVF (TPT)        %.3f\n", rBq, rSvf);
        std::printf("    -> Neither blows up (the TDF-II state is of the same magnitude as its output, see l02_direct_forms);\n"
                    "       the SVF's advantage is that a per-sample update costs just one tan, and the same set of states serves LP/BP/HP/Notch at once.\n");
        DAWLAB_CHECK(rBq < 2.0 && rSvf < 2.0);
    }

    // ---- C. The static responses agree -------------------------------------------------
    std::printf("\n[C] Static response (fixed cutoff 1 kHz, Q = %.0f): the biquad and the SVF should be exactly the same\n", kQ);
    Biquad bq;
    bq.prepare(kSampleRate, kBlockSize, 1);
    bq.setCoefficients(BiquadCoefficients::makeLowpass(kSampleRate, 1000.0, kQ));
    StateVariableFilter svf;
    svf.prepare(kSampleRate, kBlockSize, 1);
    svf.setQ(static_cast<float>(kQ));
    svf.setCutoff(1000.0f);
    std::printf("    %8s  %10s  %10s\n", "f (Hz)", "biquad dB", "SVF dB");
    for (double f : {100.0, 500.0, 900.0, 1000.0, 1100.0, 2000.0, 8000.0}) {
        bq.reset();
        svf.reset();
        const double a = measureGainDb([&](float x) { return bq.processSample(0, x); }, f, kSampleRate, 0.5, 0.5);
        const double b = measureGainDb([&](float x) { return svf.processSample(0, x); }, f, kSampleRate, 0.5, 0.5);
        std::printf("    %8.0f  %10.2f  %10.2f\n", f, a, b);
        DAWLAB_CHECK_NEAR(a, b, 0.05);
    }
    // The SVF's highpass / bandpass / notch also match RBJ's filters of the same names
    for (auto [type, coeffs] : {
             std::pair{StateVariableFilter::Type::Highpass, BiquadCoefficients::makeHighpass(kSampleRate, 1000.0, 2.0)},
             std::pair{StateVariableFilter::Type::Bandpass, BiquadCoefficients::makeBandpass(kSampleRate, 1000.0, 2.0)},
             std::pair{StateVariableFilter::Type::Notch, BiquadCoefficients::makeNotch(kSampleRate, 1000.0, 2.0)}}) {
        svf.setType(type);
        svf.setQ(2.0f);
        for (double f : {300.0, 1000.0, 3000.0}) {
            svf.reset();
            const double meas = measureGainDb([&](float x) { return svf.processSample(0, x); }, f, kSampleRate, 0.5, 0.5);
            const double theory = coeffs.magnitudeDb(f, kSampleRate);
            if (theory > -50.0)
                DAWLAB_CHECK_NEAR(meas, theory, 0.1);
            else
                DAWLAB_CHECK(meas < -50.0);
        }
    }
    std::printf("    (The SVF's Highpass / Bandpass / Notch also match the RBJ filters of the same names; see the self-check)\n\n");

    return dawlab::test::summary("l02_cutoff_modulation");
}
