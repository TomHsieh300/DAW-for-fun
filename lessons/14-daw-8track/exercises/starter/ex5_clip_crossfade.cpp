// Exercise 5 (starter): moving clips on a grid, and crossfading two clips.
//
// Fill in the three TODOs (snapToGrid, moveClip, crossfade). Until then the self-check prints
// FAIL (expected). The solution is solutions/ex5_clip_crossfade.cpp.
//
// Part A - move with snap
//   gridSamples = samplesPerBeat * gridBeats          (1/16 note at 120 BPM: 24000 * 0.25 = 6000)
//   snapped     = round(position / gridSamples) * gridSamples
//   After moving, the clip list is kept sorted by start (stable), which is what an arrange
//   view and the project file expect. The engine itself does not care about the order.
//
// Part B - crossfade
//   To crossfade clip A into clip B with an overlap of F samples:
//     B.start = A.end() - F,   A.fadeOut = B.fadeIn = F,   both get the same fade shape.
//   With Clip.h's fade definition the two gains at overlap index k are
//     g_out = shape((F - k) / (F + 1)),  g_in = shape((k + 1) / (F + 1)),  t_in + t_out = 1
//   Linear:      g_out + g_in = 1         -> correlated material (same audio) stays at 0 dB,
//                                           uncorrelated material dips to -3 dB in the middle
//   EqualPower:  g_out^2 + g_in^2 = 1      -> uncorrelated material keeps its power,
//                                           correlated material bumps up to +3 dB in the middle
//
// Self-check: snapping, sorted order, and four crossfade renders through the engine measured
// against those predictions.

#include <dawlab/Common.h>
#include <dawlab/engine/All.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <vector>

using namespace dawlab;
using namespace dawlab::engine;

namespace {

constexpr double kSampleRate = 48000.0;

int64_t snapToGrid(int64_t position, double gridSamples) {
    // TODO: round position to the nearest multiple of gridSamples (no snapping if <= 0).
    (void)gridSamples;
    return position;
}

// Moves clips[index] to newStart (snapped when gridSamples > 0, never before 0) and keeps the
// list sorted by start. Returns the clip's new index, or -1 for a bad index.
int moveClip(std::vector<Clip>& clips, int index, int64_t newStart, double gridSamples) {
    // TODO: validate index, snap newStart (and clamp it to >= 0), take the clip out of the
    // vector and insert it again so the vector stays sorted by start. Return the new index.
    (void)clips;
    (void)index;
    (void)newStart;
    (void)gridSamples;
    return -1;
}

// Moves b so that it overlaps the end of a by `overlap` samples and sets matching fades.
bool crossfade(Clip& a, Clip& b, int64_t overlap, FadeShape shape) {
    // TODO: check 0 < overlap <= both lengths, move b to a.end() - overlap, set
    // a.fadeOutSamples = b.fadeInSamples = overlap and both fade shapes.
    (void)a;
    (void)b;
    (void)overlap;
    (void)shape;
    return false;
}

std::shared_ptr<const AudioBuffer> makeDc(int n) {
    auto b = std::make_shared<AudioBuffer>(1, n);
    for (int i = 0; i < n; ++i)
        b->setSample(0, i, 0.5f);
    return b;
}

// Renders clips a then b on one track (balance pan: unity) and returns channel 0.
AudioBuffer renderPair(const Clip& a, const Clip& b, int64_t length) {
    Engine e;
    e.prepare(kSampleRate, 512);
    e.getMixer().getTrack(0).setPanMode(PanMode::Balance);
    e.setTrackClips(0, {a, b});
    RenderOptions o;
    o.numSamples = length;
    return renderOffline(e, o);
}

float windowRmsDb(const AudioBuffer& x, int centre, int half) {
    return gainToDb(rms(x.getReadPointer(0) + centre - half, 2 * half));
}

} // namespace

int main() {
    // ---- Part A: snapping and order ----
    Transport t;
    t.prepare(kSampleRate);
    t.setTempo(120.0);
    const double sixteenth = t.getSamplesPerBeat() * 0.25; // 6000
    DAWLAB_CHECK(snapToGrid(25000, sixteenth) == 24000);
    DAWLAB_CHECK(snapToGrid(27001, sixteenth) == 30000);
    DAWLAB_CHECK(snapToGrid(-2000, sixteenth) == 0);
    DAWLAB_CHECK(snapToGrid(12345, 0.0) == 12345); // snap off

    auto dc = makeDc(1000);
    std::vector<Clip> clips = {Clip::fromBuffer(dc, 0, "A"), Clip::fromBuffer(dc, 48000, "B"),
                               Clip::fromBuffer(dc, 96000, "C")};
    const int idx = moveClip(clips, 2, 25000, sixteenth); // C -> 24000, now between A and B
    DAWLAB_CHECK(idx == 1);
    DAWLAB_CHECK(clips[1].name == "C" && clips[1].start == 24000);
    DAWLAB_CHECK(std::is_sorted(clips.begin(), clips.end(), [](const Clip& x, const Clip& y) { return x.start < y.start; }));
    DAWLAB_CHECK(moveClip(clips, 0, -500, 0.0) == 0 && clips[0].start == 0);
    DAWLAB_CHECK(moveClip(clips, 7, 0, 0.0) == -1);
    std::printf("Exercise 5\n  snap 25000 -> %lld, 27001 -> %lld (1/16 grid = %.0f samples)\n",
                static_cast<long long>(snapToGrid(25000, sixteenth)), static_cast<long long>(snapToGrid(27001, sixteenth)),
                sixteenth);

    // ---- Part B: crossfades ----
    constexpr int kLen = 48000, kOverlap = 4800;
    const int64_t total = 2 * kLen;
    auto dcA = makeDc(kLen);
    auto noiseA = std::make_shared<const AudioBuffer>(makeWhiteNoise(kSampleRate, 1.0, 0.25f, 1, 101u));
    auto noiseB = std::make_shared<const AudioBuffer>(makeWhiteNoise(kSampleRate, 1.0, 0.25f, 1, 202u));

    for (FadeShape shape : {FadeShape::Linear, FadeShape::EqualPower}) {
        const char* shapeName = shape == FadeShape::Linear ? "Linear" : "EqualPower";

        // correlated: the same DC in both clips
        Clip a = Clip::fromBuffer(dcA, 0), b = Clip::fromBuffer(dcA, 0);
        DAWLAB_CHECK(crossfade(a, b, kOverlap, shape));
        DAWLAB_CHECK(b.start == kLen - kOverlap);
        const AudioBuffer y = renderPair(a, b, total);
        const int mid = static_cast<int>(b.start) + kOverlap / 2;
        float maxDevDb = 0.0f;
        for (int i = static_cast<int>(b.start); i < a.end(); ++i)
            maxDevDb = std::max(maxDevDb, std::abs(gainToDb(y.getSample(0, i) / 0.5f)));
        std::printf("  %-10s same audio  : level at the middle %+5.2f dB, max deviation in the overlap %.2f dB\n",
                    shapeName, static_cast<double>(gainToDb(y.getSample(0, mid) / 0.5f)), static_cast<double>(maxDevDb));
        if (shape == FadeShape::Linear)
            DAWLAB_CHECK(maxDevDb < 0.001f); // sums to exactly 1
        else
            DAWLAB_CHECK_NEAR(gainToDb(y.getSample(0, mid) / 0.5f), 3.01, 0.05); // 2 sin(pi/4) = 1.414

        // uncorrelated: two different noises
        Clip na = Clip::fromBuffer(noiseA, 0), nb = Clip::fromBuffer(noiseB, 0);
        crossfade(na, nb, kOverlap, shape);
        const AudioBuffer z = renderPair(na, nb, total);
        const float steady = windowRmsDb(z, 20000, 2400);
        const float middle = windowRmsDb(z, static_cast<int>(nb.start) + kOverlap / 2, 480);
        std::printf("  %-10s noise A->B : steady %.2f dBFS, middle of the overlap %.2f dBFS (%+.2f dB)\n", shapeName,
                    static_cast<double>(steady), static_cast<double>(middle), static_cast<double>(middle - steady));
        if (shape == FadeShape::Linear)
            DAWLAB_CHECK(middle - steady < -2.0f); // the famous -3 dB hole
        else
            DAWLAB_CHECK_NEAR(middle - steady, 0.0, 0.6); // power stays constant
    }
    return dawlab::test::summary("l14_ex5");
}
