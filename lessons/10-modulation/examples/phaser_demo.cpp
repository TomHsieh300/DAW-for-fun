// Example 4: Phaser -- the phase shift of cascaded allpasses added to the dry signal -> notches
//
// What it demonstrates:
//   1. Verifying the first-order allpass: |H| is 0 dB at every frequency; the phase is
//      φ(f) = −2·arctan(tan(πf/fs) / tan(πfc/fs)), exactly −90° at f = fc. The phase computed by
//      the Chapter 02 BiquadCoefficients::response is compared with the formula.
//   2. Number of notches = stages / 2: freeze the LFO (rate 0, depth 0), centre 1 kHz, mix 0.5,
//      feedback 0, sweep the frequency response from 50 Hz to 16 kHz, find the notches
//      automatically and compare them with the theoretical formula (4 / 6 / 8 stages -> 2 / 3 / 4 notches).
//      Also look at the spacing between notches: it is not even, but even on the tan(πf/fs) scale.
//   3. Feedback sharpens the peaks between the notches (a resonant feel).
//   4. WAVs of a plucked arpeggio and white noise through the phaser.
//
// How to run: l10_phaser (no arguments)
//
// Output files (dawlab_output/):
//   l10_phaser_dry.wav          plucked arpeggio
//   l10_phaser_wet.wav          4 stages, rate 0.4 Hz, centre 800 Hz, depth 1, feedback 0.3, mix 0.5
//   l10_phaser_8stage.wav       8 stages, feedback 0.6 (more notches, more "watery")
//   l10_phaser_noise.wav        white noise through a 6-stage phaser (the easiest way to hear the notches sweep)
//
// What you should hear: compared with the flanger, the phaser's sweep is "softer" and more
// "watery", with no metallic resonance; the more stages and the more feedback, the more
// pronounced the sweep.

#include <dawlab/Common.h>
#include <dawlab/dsp/Biquad.h>
#include <dawlab/dsp/Phaser.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <string>
#include <vector>

using dawlab::dsp::BiquadCoefficients;
using dawlab::dsp::Phaser;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

template <typename Effect>
void processInBlocks(Effect& fx, dawlab::AudioBuffer& buffer) {
    dawlab::AudioBlock whole(buffer);
    for (int start = 0; start < buffer.getNumSamples(); start += kBlockSize) {
        const int n = std::min(kBlockSize, buffer.getNumSamples() - start);
        fx.process(whole.getSubBlock(start, n));
    }
}

// A phaser with the LFO frozen (mono, for measurement)
Phaser makeFrozenPhaser(int stages, float centreHz, float feedback, float mix) {
    Phaser p;
    p.prepare(kSampleRate, kBlockSize, 1);
    p.setRateHz(0.0f);
    p.setDepth(0.0f);
    p.setCentreHz(centreHz);
    p.setFeedback(feedback);
    p.setStages(stages);
    p.setMix(mix);
    p.reset();
    return p;
}

double measurePhaserDb(Phaser& p, double freq) {
    p.reset();
    return dawlab::measureGainDb([&](float x) { return p.processSample(0, x); }, freq, kSampleRate, 0.3, 0.1);
}

// Theoretical frequency response: A = (first-order allpass)^N, with feedback returning to the input through a 1-sample delay
//   W = X + g·z⁻¹·Y,  Y = A·W  ⇒  Y/X = A / (1 − g·A·z⁻¹)
//   H = (1 − mix) + mix · A / (1 − g·A·z⁻¹)
double theoryPhaserDb(double f, int stages, double fc, double feedback, double mix) {
    const std::complex<double> ap = Phaser::makeFirstOrderAllpass(fc, kSampleRate).response(f, kSampleRate);
    const std::complex<double> a = std::pow(ap, stages);
    const std::complex<double> z1 = std::polar(1.0, -dawlab::kTwoPi * f / kSampleRate);
    const std::complex<double> h = (1.0 - mix) + mix * a / (1.0 - feedback * a * z1);
    return dawlab::gainToDb(std::abs(h));
}

// Ternary search within [lo, hi] for the frequency of minimum gain (a notch is sharp, so a grid sweep only finds its neighbourhood)
double refineNotch(Phaser& p, double lo, double hi) {
    for (int it = 0; it < 14; ++it) {
        const double m1 = lo + (hi - lo) / 3.0;
        const double m2 = hi - (hi - lo) / 3.0;
        if (measurePhaserDb(p, m1) < measurePhaserDb(p, m2))
            hi = m2;
        else
            lo = m1;
    }
    return 0.5 * (lo + hi);
}

} // namespace

int main() {
    std::printf("=== Chapter 10 Example 4: Phaser ===\n\n");

    // ---- 1. First-order allpass: 0 dB magnitude, phase formula -----------------
    {
        const double fc = 1000.0;
        const BiquadCoefficients c = Phaser::makeFirstOrderAllpass(fc, kSampleRate);
        std::printf("First-order allpass, fc = %.0f Hz: a = %.5f (b0 = a, b1 = 1, a1 = a)\n", fc, c.b0);
        std::printf("   freq(Hz)   |H|(dB)   phase Biquad::response   phase formula\n");
        for (double f : {100.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0}) {
            const double degResp = std::arg(c.response(f, kSampleRate)) * 180.0 / dawlab::kPi;
            const double degFormula = Phaser::allpassPhase(f, fc, kSampleRate) * 180.0 / dawlab::kPi;
            std::printf("   %8.0f   %+6.2f     %+8.2f°            %+8.2f°\n", f, c.magnitudeDb(f, kSampleRate), degResp,
                        degFormula);
        }
        std::printf("  -> The magnitude is 0 dB everywhere; the phase runs from 0° (DC) through −90° (fc) to −180° (Nyquist).\n\n");
    }

    // ---- 2. Number of notches = stages / 2; positions vs the formula ------------
    {
        const float centre = 1000.0f;
        std::vector<double> freqs;
        for (int i = 0; i <= 240; ++i) // 50 Hz .. 16 kHz, logarithmically spaced
            freqs.push_back(50.0 * std::pow(16000.0 / 50.0, i / 240.0));

        for (int stages : {4, 6, 8}) {
            Phaser p = makeFrozenPhaser(stages, centre, 0.0f, 0.5f);
            std::vector<double> gains;
            for (double f : freqs)
                gains.push_back(measurePhaserDb(p, f));

            std::printf("%d stages, centre %.0f Hz, mix 0.5, feedback 0:\n", stages, centre);
            std::printf("   k   measured notch(Hz)   theory(Hz)   ratio to the previous notch\n");
            int k = 0;
            double prev = 0.0;
            for (size_t i = 1; i + 1 < gains.size(); ++i) {
                if (gains[i] < gains[i - 1] && gains[i] <= gains[i + 1] && gains[i] < -6.0) {
                    const double notch = refineNotch(p, freqs[i - 1], freqs[i + 1]);
                    const double theory = Phaser::notchFrequencyHz(k, stages, centre, kSampleRate);
                    std::printf("  %2d   %14.1f       %8.1f   %s\n", k, notch, theory,
                                prev > 0.0 ? (std::to_string(notch / prev).substr(0, 5) + "x").c_str() : "-");
                    prev = notch;
                    ++k;
                }
            }
            std::printf("  -> Found %d notches = stages / 2 = %d. ", k, stages / 2);
            if (stages == 6)
                std::printf("The middle one is exactly at fc (%.0f Hz): 6 stages x (−90°) = −540° ≡ −180°, in antiphase with the dry signal.", centre);
            std::printf("\n\n");
        }
        std::printf("  -> The notches are neither evenly spaced in difference (unlike a flanger's comb) nor in ratio: their positions are set by tan((2k+1)π/(2N)),\n"
                    "     and at low frequencies (tan(πf/fs) ≈ πf/fs) they are geometrically symmetric about fc: f_k · f_(N/2−1−k) ≈ fc².\n");
        {
            const double f0 = Phaser::notchFrequencyHz(0, 4, centre, kSampleRate);
            const double f1 = Phaser::notchFrequencyHz(1, 4, centre, kSampleRate);
            const double fcSq = double(centre) * centre;
            std::printf("     Example: for 4 stages, f0 · f1 = %.4f x %.4f = %.0f ≈ fc² = %.0f (off by %.2f%%, from the bilinear frequency warping)\n",
                        f0, f1, f0 * f1, fcSq, 100.0 * (fcSq - f0 * f1) / fcSq);
            const auto tanOf = [](double f) { return std::tan(dawlab::kPi * f / kSampleRate); };
            std::printf("     Exact form: tan(πf0/fs) · tan(πf1/fs) = %.10f, tan²(πfc/fs) = %.10f\n\n", tanOf(f0) * tanOf(f1),
                        tanOf(centre) * tanOf(centre));
        }
    }

    // ---- 3. Feedback sharpens the peaks ----------------------------------------
    {
        std::printf("Effect of feedback on 4 stages (centre 1 kHz, mix 0.5) (measured / theory, dB):\n");
        std::printf("   freq(Hz)       fb 0.0            fb 0.5            fb 0.8\n");
        for (double f : {415.0, 1000.0, 2400.0, 6000.0}) {
            std::printf("   %8.0f", f);
            for (float fb : {0.0f, 0.5f, 0.8f}) {
                Phaser p = makeFrozenPhaser(4, 1000.0f, fb, 0.5f);
                std::printf("   %+6.1f / %+6.1f", measurePhaserDb(p, f), theoryPhaserDb(f, 4, 1000.0, fb, 0.5));
            }
            std::printf("\n");
        }
        std::printf("  -> Feedback makes the notches shallower and raises the region between them (1000 Hz, A = +1) into a peak: a resonant feel.\n"
                    "     The theory includes the 1-sample delay on the feedback path: H = (1−mix) + mix·A / (1 − g·A·z⁻¹).\n\n");
    }

    // ---- 4. Listening ----------------------------------------------------------
    {
        dawlab::AudioBuffer pluck = dawlab::makePluckSequence(kSampleRate, 4.0, 0.4f, 2);
        dawlab::writeWav(dawlab::outputPath("l10_phaser_dry.wav"), pluck, kSampleRate);
        std::printf("Output WAVs:\n  l10_phaser_dry.wav      peak %.3f\n", dawlab::peakAbs(pluck));

        Phaser p;
        p.prepare(kSampleRate, kBlockSize, 2);
        p.setRateHz(0.4f);
        p.setCentreHz(800.0f);
        p.setDepth(1.0f);
        p.setFeedback(0.3f);
        p.setStages(4);
        p.setMix(0.5f);
        p.reset();
        dawlab::AudioBuffer wet = pluck;
        processInBlocks(p, wet);
        dawlab::writeWav(dawlab::outputPath("l10_phaser_wet.wav"), wet, kSampleRate);
        std::printf("  l10_phaser_wet.wav      peak %.3f\n", dawlab::peakAbs(wet));

        p.setStages(8);
        p.setFeedback(0.6f);
        p.reset();
        wet = pluck;
        processInBlocks(p, wet);
        dawlab::writeWav(dawlab::outputPath("l10_phaser_8stage.wav"), wet, kSampleRate);
        std::printf("  l10_phaser_8stage.wav   peak %.3f\n", dawlab::peakAbs(wet));

        dawlab::AudioBuffer noise = dawlab::makeWhiteNoise(kSampleRate, 6.0, 0.2f, 2, 11u);
        p.setStages(6);
        p.setFeedback(0.5f);
        p.setRateHz(0.25f);
        p.reset();
        processInBlocks(p, noise);
        dawlab::writeWav(dawlab::outputPath("l10_phaser_noise.wav"), noise, kSampleRate);
        std::printf("  l10_phaser_noise.wav    peak %.3f\n", dawlab::peakAbs(noise));
    }
    return 0;
}
