// l14_transport_clips -- Steps 2 and 3: the musical grid, sample-accurate clips, fades, loop.
//
// What it shows
//   - Transport maths: bars:beats:ticks <-> samples at 120 BPM 4/4 and 90 BPM 7/8.
//   - A little A-minor arpeggio built from 16 short pluck clips, one per eighth note, placed
//     with barsBeatsToSamples(). Every clip's first sample is checked to land exactly on its
//     timeline position (sample accuracy).
//   - Clip gain and a fade-out on each note; an accent on every beat 1.
//   - The loop: bars 1-2 are played three times in a row with 441-sample blocks; the block
//     in which the loop end falls is split into two timeline segments.
//
// Run
//   build/bin/Debug/l14_transport_clips.exe
//
// Output
//   dawlab_output/l14_transport_clips.wav   the 2-bar arpeggio played once (4 s + 1 s)
//   dawlab_output/l14_transport_loop.wav    the same 2 bars looped 3 times (12 s)
//
// What you should hear
//   Even eighth notes (A2 C3 E3 A3 ...), the first note of each bar louder. In the loop file
//   the pattern repeats with no gap and no click at the loop point: the wrap happens inside
//   a block, on the exact sample.

#include <dawlab/Common.h>
#include <dawlab/engine/All.h>

#include <cstdio>
#include <memory>
#include <vector>

using namespace dawlab;
using namespace dawlab::engine;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

void printGrid() {
    Transport t;
    t.prepare(kSampleRate);
    t.setTempo(120.0);
    std::printf("Musical grid at 120 BPM, 4/4, 48 kHz: %.0f samples per beat, %.0f per bar\n",
                t.getSamplesPerBeat(), t.getSamplesPerBar());
    std::printf("   bar:beat:tick    samples    seconds\n");
    const BarsBeats points[] = {{1, 1, 0}, {1, 2, 0}, {1, 2, 480}, {2, 1, 0}, {3, 2, 480}, {9, 1, 0}};
    for (const BarsBeats& p : points) {
        const int64_t s = t.barsBeatsToSamples(p);
        const BarsBeats back = t.samplesToBarsBeats(s);
        std::printf("   %3d:%d:%03d   %9lld   %7.3f    (back: %d:%d:%03d)\n", p.bar, p.beat, p.tick,
                    static_cast<long long>(s), t.toSeconds(s), back.bar, back.beat, back.tick);
    }
    t.setTempo(90.0);
    t.setTimeSignature(7, 8);
    std::printf("7/8 at 90 BPM (an eighth note is the beat): %.0f samples per beat, %.0f per bar\n\n",
                t.getSamplesPerBeat(), t.getSamplesPerBar());
}

} // namespace

int main() {
    printGrid();

    Engine engine;
    engine.prepare(kSampleRate, kBlockSize);
    Transport& transport = engine.getTransport();
    transport.setTempo(120.0);

    // 4 pitches, each rendered once and shared by every clip that plays it.
    const double pitches[] = {110.0, 130.81, 164.81, 220.0}; // A2 C3 E3 A3
    std::vector<std::shared_ptr<const AudioBuffer>> notes;
    for (int k = 0; k < 4; ++k)
        notes.push_back(std::make_shared<const AudioBuffer>(
            makePluck(pitches[k], kSampleRate, 0.6, 0.5f, 1, 7u + static_cast<uint32_t>(k))));

    // 2 bars of eighth notes = 16 clips. Eighth note n starts at beat n/2.
    std::vector<Clip> clips;
    const int pattern[] = {0, 1, 2, 3, 2, 1, 0, 1};
    for (int n = 0; n < 16; ++n) {
        const BarsBeats at{1 + n / 8, 1 + (n % 8) / 2, (n % 2) * Transport::kTicksPerBeat / 2};
        Clip c = Clip::fromBuffer(notes[static_cast<size_t>(pattern[n % 8])], transport.barsBeatsToSamples(at));
        c.length = static_cast<int64_t>(transport.getSamplesPerBeat() / 2); // one eighth note
        c.fadeOutSamples = 1200;                                          // 25 ms: no click at the cut
        c.gainDb = (n % 8 == 0) ? 0.0f : -6.0f;                            // accent on beat 1
        clips.push_back(c);
    }
    std::printf("Clip placement (first 6 of 16):\n");
    for (int n = 0; n < 6; ++n) {
        const BarsBeats p = transport.samplesToBarsBeats(clips[static_cast<size_t>(n)].start);
        std::printf("   clip %2d at %d:%d:%03d = sample %6lld, length %lld, gain %.0f dB\n", n, p.bar, p.beat,
                    p.tick, static_cast<long long>(clips[static_cast<size_t>(n)].start),
                    static_cast<long long>(clips[static_cast<size_t>(n)].length),
                    static_cast<double>(clips[static_cast<size_t>(n)].gainDb));
    }
    const std::vector<Clip> clipCopy = clips;
    engine.setTrackClips(0, clips);

    // ---- render once (offline renderer, 4 s + 1 s tail) ----
    RenderOptions once;
    once.numSamples = transport.barsBeatsToSamples({3, 1, 0});
    once.tailSeconds = 1.0;
    AudioBuffer straight = renderOffline(engine, once);
    writeWav(outputPath("l14_transport_clips.wav"), straight, kSampleRate);

    // Sample accuracy: at its start sample S a clip contributes source[0] * clipGain (no
    // fade-in), and the previous clip has just ended, so out_L[S] must equal exactly
    // source[0] * clipGain * 0.7071. A clip that is off by even one sample shows a different
    // value, because a pluck starts with random noise.
    int misplaced = 0;
    for (const Clip& c : clipCopy) {
        const float expected = c.source->getSample(0, 0) * dbToGain(c.gainDb) * 0.70710678f;
        if (std::abs(straight.getSample(0, static_cast<int>(c.start)) - expected) > 1e-5f)
            ++misplaced;
    }
    DAWLAB_CHECK(misplaced == 0);
    std::printf("\nRendered once: %d samples, peak %.2f dBFS, misplaced clips: %d\n", straight.getNumSamples(),
                gainToDb(peakAbs(straight)), misplaced);

    // ---- loop bars 1-2 three times (driving processBlock ourselves, like a device) ----
    // 441-sample blocks: the loop length (192000) is not a multiple of 441, so the loop end
    // falls in the middle of a block and the transport has to split that block in two.
    constexpr int kOddBlock = 441;
    transport.setLoopRangeBars(1, 2);
    transport.setLoopEnabled(true);
    transport.setPosition(0);
    transport.play();
    engine.requestReset();
    const int loopLen = static_cast<int>(transport.getLoopEnd() - transport.getLoopStart());
    const int total = 3 * loopLen;
    AudioBuffer looped(kNumChannels, total);
    int wraps = 0;
    for (int pos = 0; pos < total; pos += kOddBlock) {
        const int n = std::min(kOddBlock, total - pos);
        const int64_t before = transport.getPosition();
        engine.processBlock(looped.toBlock().getSubBlock(pos, n));
        if (transport.getPosition() < before + n) { // the position wrapped during this block
            ++wraps;
            const int64_t split = transport.getLoopEnd() - before;
            if (split < n)
                std::printf("   wrap %d in the block at output %6d..%6d: segment 1 = %3lld samples (%lld..%lld), "
                            "segment 2 = %3lld samples from %lld\n",
                            wraps, pos, pos + n - 1, static_cast<long long>(split), static_cast<long long>(before),
                            static_cast<long long>(transport.getLoopEnd() - 1), static_cast<long long>(n - split),
                            static_cast<long long>(transport.getLoopStart()));
            else // the block ends exactly on the loop end: one segment, the next block starts at loopStart
                std::printf("   wrap %d in the block at output %6d..%6d: one segment of %3lld samples (%lld..%lld) "
                            "ending exactly at the loop end; the position wraps to %lld\n",
                            wraps, pos, pos + n - 1, static_cast<long long>(split), static_cast<long long>(before),
                            static_cast<long long>(transport.getLoopEnd() - 1),
                            static_cast<long long>(transport.getLoopStart()));
        }
    }
    transport.stop();
    writeWav(outputPath("l14_transport_loop.wav"), looped, kSampleRate);

    // Each pass of the loop must be identical to the first (no state leaks across the wrap,
    // because notes are clips and clips are a pure function of the timeline).
    float maxDiff = 0.0f;
    for (int i = 0; i < loopLen; ++i)
        maxDiff = std::max(maxDiff, std::abs(looped.getSample(0, i + loopLen) - looped.getSample(0, i)));
    std::printf("Looped 3 x %d samples, %d wraps, max |pass 2 - pass 1| = %.3g\n", loopLen, wraps, maxDiff);
    std::printf("Wrote dawlab_output/l14_transport_clips.wav and dawlab_output/l14_transport_loop.wav\n");

    DAWLAB_CHECK(loopLen == 192000);
    DAWLAB_CHECK(wraps == 3);
    DAWLAB_CHECK(maxDiff < 1e-6f);
    return dawlab::test::summary("l14_transport_clips");
}
