// Exercise 4 (starter code): 10-band graphic EQ
//
// Goal: build a graphic EQ from 10 peak Biquads:
//   - Center frequencies 31.25 × 2^k Hz (k = 0..9), with Q converted from a "1 octave bandwidth" (the formula from Exercise 2).
//   - setGainDb(band, dB) recomputes that band's coefficients; getMagnitudeDb adds up the dB of every band.
//   - Observe how the bands influence each other.
// When run, the self-check prints FAIL until you complete the TODOs.

#include <dawlab/Common.h>
#include <dawlab/dsp/Biquad.h>

#include <array>
#include <cstdio>

using namespace dawlab;
using namespace dawlab::dsp;

namespace {

class GraphicEq10 {
public:
    static constexpr int kNumBands = 10;
    static constexpr double kBandwidthOctaves = 1.0;
    static constexpr double kQ = 1.0; // TODO: convert from a 1 octave bandwidth to Q (hint: 2^(BW/2) / (2^BW - 1))

    static double centreFrequency(int band) noexcept { return 31.25 * std::pow(2.0, band); }

    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        sampleRate_ = sampleRate;
        for (auto& f : filters_)
            f.prepare(sampleRate, maxBlockSize, numChannels);
        for (int i = 0; i < kNumBands; ++i)
            updateBand(i);
        reset();
    }

    void reset() noexcept {
        for (auto& f : filters_)
            f.reset();
    }

    void setGainDb(int band, float gainDb) noexcept {
        gainsDb_[static_cast<size_t>(band)] = std::clamp(gainDb, -12.0f, 12.0f);
        updateBand(band);
    }
    float getGainDb(int band) const noexcept { return gainsDb_[static_cast<size_t>(band)]; }

    double getMagnitudeDb(double freq) const noexcept {
        (void)freq;
        return 0.0; // TODO: add up getCoefficients().magnitudeDb(freq, sampleRate_) of every filter
    }

    float processSample(int ch, float x) noexcept {
        (void)ch;
        return x; // TODO: pass through the 10 filters in order
    }

    void process(AudioBlock block) noexcept {
        (void)block; // TODO
    }

private:
    void updateBand(int band) noexcept {
        (void)band; // TODO: filters_[band].setCoefficients(BiquadCoefficients::makePeak(...))
    }

    double sampleRate_ = 48000.0;
    std::array<Biquad, kNumBands> filters_;
    std::array<float, kNumBands> gainsDb_{};
};

} // namespace

int main() {
    constexpr double sr = 48000.0;
    std::printf("=== Chapter 03 Exercise 4: 10-band graphic EQ ===\n\n");

    GraphicEq10 eq;
    eq.prepare(sr, 512, 1);

    const double flat = measureGainDb([&](float x) { return eq.processSample(0, x); }, 1000.0, sr);
    std::printf("  all 0 dB: measured at 1 kHz %+.3f dB\n", flat);
    DAWLAB_CHECK_NEAR(flat, 0.0, 0.01);

    eq.setGainDb(5, 6.0f);
    eq.reset();
    for (double f : {500.0, 1000.0, 1414.2, 2000.0}) {
        eq.reset();
        const double m = measureGainDb([&](float x) { return eq.processSample(0, x); }, f, sr);
        std::printf("    %7.1f Hz: theory %+6.2f dB, measured %+6.2f dB\n", f, eq.getMagnitudeDb(f), m);
        DAWLAB_CHECK_NEAR(m, eq.getMagnitudeDb(f), 0.05);
    }
    DAWLAB_CHECK_NEAR(eq.getMagnitudeDb(1000.0), 6.0, 0.01);
    DAWLAB_CHECK_NEAR(eq.getMagnitudeDb(1414.2), 3.0, 0.1);
    DAWLAB_CHECK_NEAR(eq.getMagnitudeDb(500.0), 1.14, 0.1);

    for (int i = 0; i < GraphicEq10::kNumBands; ++i)
        eq.setGainDb(i, 6.0f);
    eq.reset();
    std::printf("  all +6 dB: theory at 1 kHz %+.2f dB\n", eq.getMagnitudeDb(1000.0));
    DAWLAB_CHECK(eq.getMagnitudeDb(1000.0) > 8.0); // the tails of the neighboring bands pile on top

    // TODO (part 4, listening test): apply a "smiley curve" to the drum loop
    //   const float smile[GraphicEq10::kNumBands] = {6, 4, 2, 0, -3, -3, 0, 2, 4, 6};
    //   AudioBuffer wet = makeDrumLoop(sr, 2);  call eq.prepare(sr, 512, wet.getNumChannels()) again,
    //   set the 10 gains, call eq.process(...) in blocks of 512 samples, print the peak (dBFS) before and after processing,
    //   and finally writeWav(outputPath("l03_ex4_graphic_smile.wav"), wet, sr).

    return dawlab::test::summary("l03_ex4_starter");
}
