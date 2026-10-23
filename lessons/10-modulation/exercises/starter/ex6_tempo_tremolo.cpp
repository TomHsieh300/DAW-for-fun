// Exercise 6 starter: Tempo-synced tremolo -- the rate follows the bpm, the phase follows the host's song position
//
// Tempo sync has two parts:
//   1. rate: one LFO cycle = beats beats, one beat = 60/bpm seconds -> rate = bpm / 60 / beats
//   2. phase: for each block the host supplies the song position ppq (in quarter-note beats) of the block's first sample,
//        phase = frac(ppq / beats + phaseOffset)
//      Re-aligning at the start of every block makes the phase correct immediately after jumping
//      anywhere in the song or changing the tempo.
//
// Goal: complete the TODOs in TempoSyncTremolo and pass the self-check:
//   1. 120 bpm eighth -> 4 Hz, dotted eighth -> 2.667 Hz, 90 bpm quarter -> 1.5 Hz
//   2. Gain ≈ 1 on every eighth-note beat, gain ≈ 0 between beats, 8 valleys in 2 seconds
//   3. Playing from ppq = 3.3 gives the same gain as playing from the start
//   4. After 120 -> 90 bpm the valley spacing = 0.3333 s
// Hint: for the rate you can use Lfo::tempoToRateHz directly; frac(x) = x − floor(x).
// This program compiles; when run, the self-check FAILs because the TODOs are not done yet (expected).

#include <dawlab/Common.h>
#include <dawlab/dsp/Lfo.h>
#include <dawlab/dsp/Tremolo.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using dawlab::dsp::Lfo;
using dawlab::dsp::LfoShape;
using dawlab::dsp::Tremolo;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

class TempoSyncTremolo {
public:
    void prepare(double sampleRate, int maxBlockSize, int numChannels) {
        sampleRate_ = sampleRate;
        tremolo_.prepare(sampleRate, maxBlockSize, numChannels);
        reset();
    }

    void reset() noexcept { tremolo_.reset(); }

    void setTempoBpm(double bpm) noexcept { bpm_ = std::max(1.0, bpm); }
    // How many beats one LFO cycle lasts: 1 = quarter note, 0.5 = eighth, 0.25 = sixteenth, 0.75 = dotted eighth
    void setDivisionBeats(double beats) noexcept { beats_ = std::max(1.0 / 64.0, beats); }
    // Where the beat falls within the LFO cycle. For a sine, 0.25 -> sin = +1 on the beat -> maximum gain
    void setPhaseOffset(float offset01) noexcept { phaseOffset_ = offset01; }
    void setDepth(float depth) noexcept { tremolo_.setDepth(depth); }
    void setShape(LfoShape shape) noexcept { tremolo_.setShape(shape); }

    float getRateHz() const noexcept {
        // TODO 1: return bpm_ / 60 / beats_ (or call Lfo::tempoToRateHz)
        return 1.0f;
    }

    // ppqAtBlockStart: the song position of the block's first sample (in quarter-note beats), supplied by the host
    void process(dawlab::AudioBlock block, double ppqAtBlockStart) noexcept {
        tremolo_.setRateHz(getRateHz());
        // TODO 2: phase = frac(ppqAtBlockStart / beats_ + phaseOffset_), set with tremolo_.setLfoPhase(...)
        (void)ppqAtBlockStart;
        tremolo_.process(block);
    }

    // The length of a block in beats: n / fs seconds x bpm / 60
    double blockLengthInBeats(int numSamples) const noexcept { return numSamples / sampleRate_ * bpm_ / 60.0; }

private:
    double sampleRate_ = 48000.0;
    double bpm_ = 120.0;
    double beats_ = 0.5;
    float phaseOffset_ = 0.25f;
    Tremolo tremolo_;
};

// Simulate a host: process block by block, telling the tremolo the current ppq for each block.
// When tempoChangeAtSample >= 0, blocks after that sample use bpm2 (for simplicity the change happens only at block boundaries)
void runHost(TempoSyncTremolo& t, dawlab::AudioBuffer& buffer, double startPpq, double bpm1,
             int tempoChangeAtSample = -1, double bpm2 = 120.0) {
    dawlab::AudioBlock whole(buffer);
    double ppq = startPpq;
    for (int start = 0; start < buffer.getNumSamples(); start += kBlockSize) {
        const int n = std::min(kBlockSize, buffer.getNumSamples() - start);
        t.setTempoBpm((tempoChangeAtSample >= 0 && start >= tempoChangeAtSample) ? bpm2 : bpm1);
        t.process(whole.getSubBlock(start, n), ppq);
        ppq += t.blockLengthInBeats(n);
    }
}

dawlab::AudioBuffer makeDc(double seconds) {
    dawlab::AudioBuffer b(1, dawlab::secondsToSamples(seconds, kSampleRate));
    for (int i = 0; i < b.getNumSamples(); ++i)
        b.setSample(0, i, 1.0f);
    return b;
}

// Find the valleys of the gain curve: each contiguous run where "gain < 0.05" counts as one valley, and the midpoint of the run is its position.
// (We do not test "smaller than both neighbours": float quantization near the bottom of a valley creates several false local minima.)
std::vector<int> findMinima(const float* g, int n, int from, int to) {
    std::vector<int> idx;
    int regionStart = -1;
    for (int i = std::max(0, from); i < std::min(n, to); ++i) {
        const bool low = g[i] < 0.05f;
        if (low && regionStart < 0)
            regionStart = i;
        if (!low && regionStart >= 0) {
            if (regionStart > from) // A run that is already inside a valley at the very start of the range is incomplete and is not counted
                idx.push_back((regionStart + i - 1) / 2);
            regionStart = -1;
        }
    }
    return idx;
}

TempoSyncTremolo makeTremolo(double beats, LfoShape shape = LfoShape::Sine) {
    TempoSyncTremolo t;
    t.prepare(kSampleRate, kBlockSize, 2);
    t.setDivisionBeats(beats);
    t.setDepth(1.0f);
    t.setShape(shape);
    t.setPhaseOffset(0.25f);
    t.reset();
    return t;
}

} // namespace

int main() {
    std::printf("=== Chapter 10 Exercise 6: Tempo-synced tremolo (starter) ===\n\n");

    // ---- 1. Rate conversion ----------------------------------------------------
    {
        // Verify through TempoSyncTremolo::getRateHz() itself (TODO 1), rather than calling Lfo::tempoToRateHz directly
        auto rateOf = [](double bpm, double beats) {
            TempoSyncTremolo t;
            t.prepare(kSampleRate, kBlockSize, 2);
            t.setTempoBpm(bpm);
            t.setDivisionBeats(beats);
            return static_cast<double>(t.getRateHz());
        };
        const double r8 = rateOf(120.0, 0.5);
        const double r8d = rateOf(120.0, 0.75);
        const double r4at90 = rateOf(90.0, 1.0);
        std::printf("Rate conversion: 120 bpm eighth %.4f Hz, dotted eighth %.4f Hz, 90 bpm quarter %.4f Hz\n\n", r8, r8d, r4at90);
        DAWLAB_CHECK_NEAR(r8, 4.0, 1e-4);
        DAWLAB_CHECK_NEAR(r8d, 8.0 / 3.0, 1e-4);
        DAWLAB_CHECK_NEAR(r4at90, 1.5, 1e-4);
    }

    // ---- 2. Beat alignment -----------------------------------------------------
    {
        dawlab::AudioBuffer g = makeDc(2.0);
        TempoSyncTremolo t = makeTremolo(0.5);
        runHost(t, g, 0.0, 120.0);
        const float* d = g.getReadPointer(0);
        const int eighth = dawlab::secondsToSamples(0.25, kSampleRate); // 12000 samples
        std::printf("120 bpm eighth notes, sine, depth 1 (input DC 1.0 -> output = gain):\n");
        std::printf("   beat   time(s)   gain on the beat   gain between beats\n");
        float worstOnBeat = 1.0f, worstMid = 0.0f;
        for (int k = 0; k < 8; ++k) {
            const float onBeat = d[k * eighth];
            const float mid = d[k * eighth + eighth / 2];
            worstOnBeat = std::min(worstOnBeat, onBeat);
            worstMid = std::max(worstMid, mid);
            std::printf("   %4d   %6.3f         %8.5f           %8.5f\n", k, k * 0.25, onBeat, mid);
        }
        const std::vector<int> minima = findMinima(d, g.getNumSamples(), 0, g.getNumSamples());
        std::printf("  -> Found %zu minima in 2 seconds (theory 8)\n\n", minima.size());
        DAWLAB_CHECK(worstOnBeat > 0.999f);
        DAWLAB_CHECK(worstMid < 0.001f);
        DAWLAB_CHECK(minima.size() == 8);
    }

    // ---- 3. Starting playback from the middle of the song ------------------------
    {
        const double startBeat = 3.3;                       // = 1.65 s at 120 bpm
        const int offset = dawlab::secondsToSamples(startBeat * 0.5, kSampleRate);
        dawlab::AudioBuffer full = makeDc(4.0);
        TempoSyncTremolo a = makeTremolo(0.5);
        runHost(a, full, 0.0, 120.0);

        dawlab::AudioBuffer late = makeDc(4.0 - startBeat * 0.5);
        TempoSyncTremolo b = makeTremolo(0.5);
        runHost(b, late, startBeat, 120.0);

        float maxDiff = 0.0f;
        for (int i = 0; i < late.getNumSamples(); ++i)
            maxDiff = std::max(maxDiff, std::abs(late.getSample(0, i) - full.getSample(0, offset + i)));
        std::printf("Playing from ppq = %.1f vs playing from the start: largest gain difference at the same song position = %.2e\n\n", startBeat, maxDiff);
        DAWLAB_CHECK(maxDiff < 1.0e-3f);
    }

    // ---- 4. Tempo change -------------------------------------------------------------
    {
        dawlab::AudioBuffer g = makeDc(4.0);
        TempoSyncTremolo t = makeTremolo(0.5);
        const int change = dawlab::secondsToSamples(2.0, kSampleRate);
        runHost(t, g, 0.0, 120.0, change, 90.0);
        const float* d = g.getReadPointer(0);
        const std::vector<int> after = findMinima(d, g.getNumSamples(), change + kBlockSize, g.getNumSamples());
        double meanGap = 0.0;
        for (size_t i = 1; i < after.size(); ++i)
            meanGap += (after[i] - after[i - 1]) / kSampleRate;
        meanGap = after.size() > 1 ? meanGap / static_cast<double>(after.size() - 1) : 0.0;
        std::printf("After the tempo change 120 -> 90 bpm: mean minimum spacing %.4f s (theory 60/90 x 0.5 = %.4f s)\n\n", meanGap,
                    60.0 / 90.0 * 0.5);
        DAWLAB_CHECK(after.size() >= 4);
        DAWLAB_CHECK_NEAR(meanGap, 60.0 / 90.0 * 0.5, 0.002);
    }

    // ---- Listening ---------------------------------------------------------------
    {
        dawlab::AudioBuffer pluck = dawlab::makePluckSequence(kSampleRate, 4.0, 0.4f, 2);
        TempoSyncTremolo t = makeTremolo(0.25, LfoShape::Square); // sixteenth-note "chopping"
        t.setPhaseOffset(0.0f); // square is +1 for phase 0..0.5: "open" on the beat
        runHost(t, pluck, 0.0, 120.0);
        dawlab::writeWav(dawlab::outputPath("l10_ex6_tempo_tremolo.wav"), pluck, kSampleRate);
        std::printf("Output l10_ex6_tempo_tremolo.wav (120 bpm, sixteenth notes, square, depth 1): peak %.3f\n",
                    dawlab::peakAbs(pluck));
        DAWLAB_CHECK(!dawlab::hasNanOrInf(pluck));
    }

    return dawlab::test::summary("l10_ex6");
}
