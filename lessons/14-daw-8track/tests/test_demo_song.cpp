// test_demo_song: DemoSong.h (the demo song as data, used by the stage-2 app) is the same song
// that examples/render_song.cpp builds by hand (Step 9), and it loads cleanly into an Engine.
//
// Checks: every parameter name in the song exists in its effect adapter; all 17 effect types
// that existed when the song was written are used (Filter and AmpChain, added later, are not); the clip layout; and a full bounce (8 bars + 3 s tail) that reproduces the numbers of
// the l14_render_song report: length 912000, latency 752 samples, file peak -1.00 dBFS, master
// integrated loudness -15.5 LUFS, drums and bass statistics.

#include "TestHelpers.h"

#include <dawlab/engine/DemoSong.h>

#include <array>
#include <cstdio>

using namespace l14test;

int main() {
    const DemoSong song = makeDemoSong(kSampleRate);

    // ---- the song as data ----
    DAWLAB_CHECK(song.songEndSample() == 768000); // 8 bars at 120 BPM
    std::array<bool, kNumUserEffectTypes> used{};
    for (const DemoInsert& d : song.inserts)
        if (static_cast<int>(d.type) < kNumUserEffectTypes)
            used[static_cast<size_t>(d.type)] = true;
    int numUsed = 0;
    for (bool u : used)
        numUsed += u ? 1 : 0;
    // Every effect type except the two appended later (Effects.h): adding them to the song would
    // change every number quoted for it.
    DAWLAB_CHECK(numUsed == kNumUserEffectTypes - 2);
    DAWLAB_CHECK(!used[static_cast<size_t>(EffectType::Filter)] && !used[static_cast<size_t>(EffectType::AmpChain)]);
    DAWLAB_CHECK(song.clips[0].size() == 4);                // drums: 4 x a 2-bar loop
    DAWLAB_CHECK(song.clips[1].size() == 16);               // bass: 2 notes per bar
    DAWLAB_CHECK(song.clips[0][0].source == song.clips[0][3].source); // clips share one buffer

    // ---- into an engine ----
    Engine engine;
    engine.prepare(kSampleRate, kBlockSize);
    const int unknownParameters = applyDemoSong(engine, song);
    engine.applyPendingCommandsOffline();
    DAWLAB_CHECK(unknownParameters == 0);
    DAWLAB_CHECK(engine.getLatencySamples() == 752); // convolution 512 (track 8) + limiter 240 (master)
    DAWLAB_CHECK(engine.getMixer().getSongEndSample() >= song.songEndSample());

    RenderOptions o;
    o.numSamples = song.songEndSample();
    o.tailSeconds = 3.0;
    const AudioBuffer mix = renderOffline(engine, o);
    const Mixer& m = engine.getMixer();
    const float peakDb = gainToDb(peakAbs(mix));
    const StripStats drums = m.getTrack(0).getStats();
    const StripStats bass = m.getTrack(1).getStats();
    std::printf("  demo song: %d samples, latency %d, peak %.2f dBFS, master %.1f LUFS, drums %.1f / %.1f dBFS, "
                "bass %.1f LUFS\n",
                mix.getNumSamples(), engine.getLatencySamples(), static_cast<double>(peakDb),
                static_cast<double>(m.getMaster().getIntegratedLufs()), static_cast<double>(drums.peakDb),
                static_cast<double>(drums.rmsDb), static_cast<double>(bass.integratedLufs));
    DAWLAB_CHECK(!hasNanOrInf(mix));
    DAWLAB_CHECK(mix.getNumSamples() == 768000 + 144000);
    DAWLAB_CHECK_NEAR(peakDb, -1.0, 0.05);                              // the master limiter's ceiling
    DAWLAB_CHECK_NEAR(m.getMaster().getIntegratedLufs(), -15.5, 0.06); // as in the l14_render_song report
    DAWLAB_CHECK_NEAR(drums.peakDb, -3.3, 0.06);
    DAWLAB_CHECK_NEAR(drums.rmsDb, -27.4, 0.06);
    DAWLAB_CHECK_NEAR(bass.integratedLufs, -16.8, 0.06);
    engine.collectGarbage();
    return dawlab::test::summary("l14_test_demo_song");
}
