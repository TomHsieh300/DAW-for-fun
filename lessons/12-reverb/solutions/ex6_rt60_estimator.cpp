// Exercise 6 solution: estimating RT60 with Schroeder backward integration
//
// Schroeder (1965): for an impulse response h[n], define the energy decay curve (EDC)
//
//     EDC[n] = sum_{k=n}^{N-1} h[k]^2        (the energy remaining from "now" to the end)
//     EDC_dB[n] = 10*log10(EDC[n] / EDC[0])
//
// This is equivalent to averaging "infinitely many decay curves with different starting points", so it is
// much smoother than looking at the envelope of h^2 directly (a single measurement yields the ensemble
// average). Then run a linear regression over the -5 .. -25 dB part of EDC_dB (T20) and extrapolate the
// slope s (dB/s): RT60 = -60 / s.
// You can also use -5 .. -35 dB (T30); the longer the range the more accurate, but the more SNR the IR needs.
//
// Note: the tail of a real recorded IR has a noise floor, and the EDC "turns up" and flattens at the end;
// in practice you first estimate the noise floor and truncate the integration where the noise begins
// (the synthetic IR here has no noise, so that is not needed).

#include <dawlab/Common.h>
#include <dawlab/dsp/ConvolutionReverb.h>
#include <dawlab/dsp/Reverb.h>
#include <dawlab/dsp/ReverbBuildingBlocks.h>

#include <cmath>
#include <cstdio>
#include <vector>

using namespace dawlab;
using namespace dawlab::dsp;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

// EDC (dB, relative to the total energy)
std::vector<double> schroederDecayDb(const float* h, int n) {
    std::vector<double> edc(static_cast<size_t>(n));
    double acc = 0.0;
    for (int i = n - 1; i >= 0; --i) { // accumulate backwards from the tail: EDC[n] = EDC[n+1] + h[n]^2
        acc += static_cast<double>(h[i]) * h[i];
        edc[static_cast<size_t>(i)] = acc;
    }
    const double total = edc.empty() ? 1.0 : std::max(edc[0], 1e-30);
    for (double& v : edc)
        v = 10.0 * std::log10(std::max(v, 1e-30) / total);
    return edc;
}

struct Rt60Estimate {
    double rt60 = 0.0;
    double slopeDbPerSecond = 0.0;
    bool valid = false;
};

// Run a linear regression over the part of EDC_dB between startDb and endDb, and extrapolate to -60 dB
Rt60Estimate estimateRt60(const std::vector<double>& edcDb, double sampleRate, double startDb = -5.0,
                          double endDb = -25.0) {
    Rt60Estimate r;
    double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
    int count = 0;
    for (size_t i = 0; i < edcDb.size(); ++i) {
        const double y = edcDb[i];
        if (y > startDb)
            continue;
        if (y < endDb)
            break;
        const double t = static_cast<double>(i) / sampleRate;
        sx += t;
        sy += y;
        sxx += t * t;
        sxy += t * y;
        ++count;
    }
    if (count < 10)
        return r;
    const double denom = count * sxx - sx * sx;
    if (denom <= 0.0)
        return r;
    r.slopeDbPerSecond = (count * sxy - sx * sy) / denom;
    if (r.slopeDbPerSecond >= 0.0)
        return r;
    r.rt60 = -60.0 / r.slopeDbPerSecond;
    r.valid = true;
    return r;
}

// An ideal exponentially decaying sine: envelope = exp(-ln(1000)*t / RT60), dropping exactly 60 dB in RT60 seconds
AudioBuffer makeDecayingSine(double freq, double rt60, double seconds) {
    AudioBuffer b(1, secondsToSamples(seconds, kSampleRate));
    const double k = std::log(1000.0) / rt60;
    for (int i = 0; i < b.getNumSamples(); ++i) {
        const double t = i / kSampleRate;
        b.setSample(0, i, static_cast<float>(std::exp(-k * t) * std::sin(kTwoPi * freq * t)));
    }
    return b;
}

AudioBuffer fdnImpulseResponse(float rt60, double seconds) {
    Reverb reverb;
    reverb.prepare(kSampleRate, kBlockSize, 2);
    reverb.setMix(1.0f);
    reverb.setDampingHz(20000.0f);
    reverb.setDecaySeconds(rt60);
    reverb.reset(); // make mix = 1 take effect immediately: otherwise sample 0 leaks the dry impulse and pollutes the peak of the windowed RMS
    AudioBuffer ir = makeImpulse(secondsToSamples(seconds, kSampleRate), 2);
    for (int start = 0; start < ir.getNumSamples(); start += kBlockSize)
        reverb.process(ir.toBlock().getSubBlock(start, std::min(kBlockSize, ir.getNumSamples() - start)));
    return ir;
}

} // namespace

int main() {
    std::printf("=== Exercise 6: estimating RT60 with Schroeder backward integration ===\n");

    auto report = [](const char* name, const AudioBuffer& ir, double truth) {
        const std::vector<double> edc = schroederDecayDb(ir.getReadPointer(0), ir.getNumSamples());
        const Rt60Estimate t20 = estimateRt60(edc, kSampleRate, -5.0, -25.0);
        const Rt60Estimate t30 = estimateRt60(edc, kSampleRate, -5.0, -35.0);
        const DecayMeasurement rmsFit = measureRt60(ir.getReadPointer(0), ir.getNumSamples(), kSampleRate, 20.0);
        std::printf("%-28s %10.3f %12.3f %12.3f %14.3f\n", name, truth, t20.rt60, t30.rt60, rmsFit.rt60Seconds);
        return t20;
    };

    // ---- 1. Ideal exponential decay: the EDC must be a straight line and the RT60 almost exact ----------------
    {
        AudioBuffer sine = makeDecayingSine(440.0, 1.0, 1.3);
        const std::vector<double> edc = schroederDecayDb(sine.getReadPointer(0), sine.getNumSamples());
        // For an envelope e^(-kt) the energy is proportional to e^(-2kt), so EDC_dB(t) = -60*t/RT60: -30 dB at t = 0.5 s
        const double at05 = edc[static_cast<size_t>(secondsToSamples(0.5, kSampleRate))];
        const double at025 = edc[static_cast<size_t>(secondsToSamples(0.25, kSampleRate))];
        std::printf("EDC of the exponentially decaying sine: t=0.25 s -> %.2f dB (theory -15), t=0.5 s -> %.2f dB (theory -30)\n",
                    at025, at05);
        DAWLAB_CHECK_NEAR(at025, -15.0, 0.5);
        DAWLAB_CHECK_NEAR(at05, -30.0, 0.5);
        // The table header is printed after the EDC line so that the table stays contiguous
        std::printf("\n%-28s %10s %12s %12s %14s\n", "IR", "truth(s)", "T20(s)", "T30(s)", "windowed-RMS(s)");
        const Rt60Estimate e = report("decaying sine (RT60 1.0)", sine, 1.0);
        DAWLAB_CHECK(e.valid);
        DAWLAB_CHECK_NEAR(e.rt60, 1.0, 0.03);
    }

    // ---- 2. Synthetic noise IRs (ConvolutionReverb::makeSyntheticImpulseResponse) ---------------
    for (double truth : {0.5, 1.5}) {
        AudioBuffer ir = ConvolutionReverb::makeSyntheticImpulseResponse(kSampleRate, truth, 1, 11);
        const Rt60Estimate e = report("synthetic noise IR", ir, truth);
        DAWLAB_CHECK(e.valid);
        DAWLAB_CHECK_NEAR(e.rt60 / truth, 1.0, 0.1); // within 10%
    }

    // ---- 3. IR of the FDN: Schroeder and the windowed-RMS regression should roughly agree -----------------------
    {
        AudioBuffer ir = fdnImpulseResponse(2.0f, 2.8);
        const Rt60Estimate e = report("FDN Reverb (RT60 2.0)", ir, 2.0);
        const DecayMeasurement rmsFit = measureRt60(ir.getReadPointer(0), ir.getNumSamples(), kSampleRate, 20.0);
        DAWLAB_CHECK(e.valid);
        DAWLAB_CHECK_NEAR(e.rt60 / 2.0, 1.0, 0.15);
        DAWLAB_CHECK_NEAR(e.rt60 / rmsFit.rt60Seconds, 1.0, 0.2);

        // Print the EDC curve for people to look at
        const std::vector<double> edc = schroederDecayDb(ir.getReadPointer(0), ir.getNumSamples());
        std::printf("\nEDC of the FDN IR (every 0.25 s):\n");
        for (double t = 0.0; t <= 2.0; t += 0.25)
            std::printf("  t = %.2f s : %7.2f dB\n", t, edc[static_cast<size_t>(secondsToSamples(t, kSampleRate))]);
    }

    return dawlab::test::summary("l12_ex6");
}
