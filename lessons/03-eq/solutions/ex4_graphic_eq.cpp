// Exercise 4 (solution): 10-band graphic EQ
//
// A graphic EQ = a cascade of peaks with fixed center frequencies (the ISO 266 octave centers: 31.5, 63, 125 … 16k Hz) and a fixed Q;
// the user can only push each band's gain. Adjacent bands are 1 octave apart, so each band's bandwidth is also taken to be 1 octave,
// which corresponds to Q = 2^(1/2) / (2^1 - 1) = √2 ≈ 1.414 (the formula from Exercise 2).
//
// Key observation: the bands of a graphic EQ "leak" into each other -- push 1 kHz by +6 dB and 500 Hz and 2 kHz also rise by +1.1 dB;
// push all bands by +6 dB and 1 kHz ends up at nearly +9 dB rather than +6 (the tails of the adjacent bands add up).
// This is why the "fader shape" of a graphic EQ differs from the actual curve, and it is one of the problems that proportional-Q / constant-Q
// designs try to solve (README section 2.11).

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
    // Q = 2^(BW/2) / (2^BW - 1), BW = 1 -> √2
    static constexpr double kQ = 1.41421356237309505;

    static double centreFrequency(int band) noexcept { return 31.25 * std::pow(2.0, band); } // 31.25 … 16000 Hz

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

    // Only the gain can be changed (that is what makes it a graphic EQ); call between blocks
    void setGainDb(int band, float gainDb) noexcept {
        gainsDb_[static_cast<size_t>(band)] = std::clamp(gainDb, -12.0f, 12.0f);
        updateBand(band);
    }
    float getGainDb(int band) const noexcept { return gainsDb_[static_cast<size_t>(band)]; }

    // The whole curve = the dB of every band added up (a 0 dB band contributes 0)
    double getMagnitudeDb(double freq) const noexcept {
        double total = 0.0;
        for (const auto& f : filters_)
            total += f.getCoefficients().magnitudeDb(freq, sampleRate_);
        return total;
    }

    float processSample(int ch, float x) noexcept {
        for (auto& f : filters_)
            x = f.processSample(ch, x);
        return x;
    }

    void process(AudioBlock block) noexcept {
        for (auto& f : filters_)
            f.process(block);
    }

private:
    void updateBand(int band) noexcept {
        filters_[static_cast<size_t>(band)].setCoefficients(
            BiquadCoefficients::makePeak(sampleRate_, centreFrequency(band), kQ, gainsDb_[static_cast<size_t>(band)]));
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
    std::printf("  band center frequencies:");
    for (int i = 0; i < GraphicEq10::kNumBands; ++i)
        std::printf(" %.5g", GraphicEq10::centreFrequency(i));
    std::printf(" Hz, Q = %.4f (1 octave)\n", GraphicEq10::kQ);

    // ---- 1. All 0 dB: transparent ----
    const double flat = measureGainDb([&](float x) { return eq.processSample(0, x); }, 1000.0, sr);
    std::printf("\n  all 0 dB: measured at 1 kHz %+.3f dB\n", flat);
    DAWLAB_CHECK_NEAR(flat, 0.0, 0.01);

    // ---- 2. Push only the 1 kHz band by +6 dB: +6 at the center, about +1.14 dB at the neighboring band centers (the tails 1 octave away) ----
    eq.setGainDb(5, 6.0f);
    eq.reset();
    std::printf("  only the 1 kHz band at +6 dB:\n");
    for (double f : {250.0, 500.0, 707.1, 1000.0, 1414.2, 2000.0, 4000.0}) {
        eq.reset();
        const double m = measureGainDb([&](float x) { return eq.processSample(0, x); }, f, sr);
        std::printf("    %7.1f Hz: theory %+6.2f dB, measured %+6.2f dB\n", f, eq.getMagnitudeDb(f), m);
        DAWLAB_CHECK_NEAR(m, eq.getMagnitudeDb(f), 0.05);
    }
    DAWLAB_CHECK_NEAR(eq.getMagnitudeDb(1000.0), 6.0, 0.01);
    DAWLAB_CHECK_NEAR(eq.getMagnitudeDb(1414.2), 3.0, 0.1);  // midpoint-gain edge (Q = √2 -> ±1/2 octave)
    DAWLAB_CHECK_NEAR(eq.getMagnitudeDb(500.0), 1.14, 0.1);  // 1 octave away: the analog prototype gives +1.137 dB
    DAWLAB_CHECK_NEAR(eq.getMagnitudeDb(2000.0), 1.14, 0.15);

    // ---- 3. All +6 dB: the bands add up, and the center is far above +6 ----
    for (int i = 0; i < GraphicEq10::kNumBands; ++i)
        eq.setGainDb(i, 6.0f);
    eq.reset();
    std::printf("  all +6 dB:\n");
    double minDb = 100.0, maxDb = -100.0;
    for (double f = 125.0; f <= 4000.0; f *= std::pow(2.0, 0.25)) {
        const double db = eq.getMagnitudeDb(f);
        minDb = std::min(minDb, db);
        maxDb = std::max(maxDb, db);
    }
    std::printf("    theory at 1 kHz %+.2f dB; between 125 Hz and 4 kHz the curve lies in %+.2f .. %+.2f dB (ripple %.2f dB)\n",
                eq.getMagnitudeDb(1000.0), minDb, maxDb, maxDb - minDb);
    const double all6 = measureGainDb([&](float x) { return eq.processSample(0, x); }, 1000.0, sr);
    DAWLAB_CHECK_NEAR(all6, eq.getMagnitudeDb(1000.0), 0.05);
    DAWLAB_CHECK(eq.getMagnitudeDb(1000.0) > 8.0); // not 6 dB: the tails of the adjacent bands pile on top (theory is about +8.9)
    DAWLAB_CHECK(maxDb - minDb < 2.5);              // about 2 dB of ripple in the mid range: the "gaps" between the bands are not completely filled in

    // ---- 4. The classic "smiley" curve: boost the lows and highs, cut the mids ----
    const float smile[GraphicEq10::kNumBands] = {6.0f, 4.0f, 2.0f, 0.0f, -3.0f, -3.0f, 0.0f, 2.0f, 4.0f, 6.0f};
    for (int i = 0; i < GraphicEq10::kNumBands; ++i)
        eq.setGainDb(i, smile[i]);
    eq.reset();
    AudioBuffer dry = makeDrumLoop(sr, 2);
    AudioBuffer wet = dry;
    AudioBlock whole(wet);
    for (int start = 0; start < wet.getNumSamples(); start += 512)
        eq.process(whole.getSubBlock(start, std::min(512, wet.getNumSamples() - start)));
    DAWLAB_CHECK(!hasNanOrInf(wet));
    std::printf("  smiley curve applied to the drum loop: peak %+.2f -> %+.2f dBFS -> dawlab_output/l03_ex4_graphic_smile.wav\n",
                gainToDb(peakAbs(dry)), gainToDb(peakAbs(wet)));
    writeWav(outputPath("l03_ex4_graphic_smile.wav"), wet, sr);

    return dawlab::test::summary("l03_ex4");
}
