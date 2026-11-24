// test_demo_song: DemoSong.h (the demo song as data, used by the stage-2 app) is the same song
// that examples/render_song.cpp builds by hand (Step 9), and it loads cleanly into an Engine.
//
// Checks: the generators on their own (pad harmonics and detune, lead vibrato, burst decay);
// every parameter name in the song exists in its effect adapter, and an unknown one is counted;
// all 17 effect types
// that existed when the song was written are used (Filter and AmpChain, added later, are not); the clip layout; and a full bounce (8 bars + 3 s tail) that reproduces the numbers of
// the l14_render_song report: length 912000, latency 752 samples, file peak -1.00 dBFS, master
// integrated loudness -15.5 LUFS, drums and bass statistics.

#include "TestHelpers.h"

#include <dawlab/engine/DemoSong.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

#include "TestTracks.h"

using namespace l15test;
using namespace dawlab::engine::test;

namespace {

// The amplitude of the sine at `hz` in channel `ch`, samples [from, to): a projection over a
// window of many periods.
double amplitudeAt(const AudioBuffer& b, int ch, double hz, int from, int to) {
    double re = 0.0, im = 0.0;
    for (int i = from; i < to; ++i) {
        const double w = kTwoPi * hz * i / kSampleRate;
        re += static_cast<double>(b.getSample(ch, i)) * std::cos(w);
        im += static_cast<double>(b.getSample(ch, i)) * std::sin(w);
    }
    return 2.0 * std::sqrt(re * re + im * im) / (to - from);
}

float peakOf(const AudioBuffer& b, int ch, int from, int to) {
    float p = 0.0f;
    for (int i = from; i < to; ++i)
        p = std::max(p, std::abs(b.getSample(ch, i)));
    return p;
}

double rmsOf(const AudioBuffer& b, int from, int to) {
    double sum = 0.0;
    for (int i = from; i < to; ++i)
        sum += static_cast<double>(b.getSample(0, i)) * b.getSample(0, i);
    return std::sqrt(sum / (to - from));
}

// The mean period of channel 0 in samples [from, to): between its first and last upward zero crossings.
double meanPeriod(const AudioBuffer& b, int from, int to) {
    double first = -1.0, last = -1.0;
    int periods = -1;
    for (int i = from + 1; i < to; ++i) {
        const float x = b.getSample(0, i - 1), y = b.getSample(0, i);
        if (x < 0.0f && y >= 0.0f) {
            last = i - 1 + static_cast<double>(x / (x - y));
            first = first < 0.0 ? last : first;
            ++periods;
        }
    }
    return periods > 0 ? (last - first) / periods : 0.0;
}

bool sameAudio(const AudioBuffer& a, const AudioBuffer& b) {
    return a.getNumChannels() == b.getNumChannels() && a.getNumSamples() == b.getNumSamples() && maxAbsDiff(a, b) == 0.0f;
}

// The generators the song is made of, on their own.
void testGenerators() {
    // A pad note of 1000 Hz: the left channel 6 cents flat, the right 6 cents sharp, each a sine
    // (amplitude 0.08) with its 2nd and 3rd harmonics at 0.3 and 0.15 of it; it fades out at its end.
    const AudioBuffer pad = demo::makePadChord(kSampleRate, {1000.0}, 2.0);
    const int from = 24000, to = 72000; // 0.5 .. 1.5 s: past the attack, before the release
    bool padOk = true;
    for (int ch = 0; ch < 2; ++ch) {
        const double f = 1000.0 * std::pow(2.0, (ch == 0 ? -6.0 : 6.0) / 1200.0);
        const double other = 1000.0 * std::pow(2.0, (ch == 0 ? 6.0 : -6.0) / 1200.0);
        padOk = padOk && std::abs(amplitudeAt(pad, ch, f, from, to) - 0.08) < 0.004 &&
                std::abs(amplitudeAt(pad, ch, 2.0 * f, from, to) - 0.024) < 0.002 &&
                std::abs(amplitudeAt(pad, ch, 3.0 * f, from, to) - 0.012) < 0.002 && amplitudeAt(pad, ch, other, from, to) < 0.02;
        padOk = padOk && peakOf(pad, ch, 96000 - 480, 96000) < 0.05f * peakOf(pad, ch, from, to);
    }
    DAWLAB_CHECK(padOk);

    // A lead note of 440 Hz, 1.5 s: its vibrato (+-0.6 %) is at full depth from 0.2 s on, and it fades out at its end.
    const AudioBuffer lead = demo::makeLeadNote(kSampleRate, 440.0, 1.5);
    std::vector<double> crossings; // upward zero crossings in 0.3 .. 1.0 s, between samples
    for (int i = 14400; i < 48000; ++i) {
        const float a = lead.getSample(0, i - 1), b = lead.getSample(0, i);
        if (a < 0.0f && b >= 0.0f)
            crossings.push_back(i - 1 + static_cast<double>(a / (a - b)));
    }
    double shortest = 1e9, longest = 0.0;
    for (size_t k = 1; k < crossings.size(); ++k) {
        shortest = std::min(shortest, crossings[k] - crossings[k - 1]);
        longest = std::max(longest, crossings[k] - crossings[k - 1]);
    }
    const double hz = (crossings.size() - 1) * kSampleRate / (crossings.back() - crossings.front());
    std::printf("  generators: lead %.1f Hz, periods %.3f %% apart\n", hz, (longest / shortest - 1.0) * 100.0);
    DAWLAB_CHECK(std::abs(hz - 440.0) < 3.0 && longest / shortest - 1.0 > 0.008 && longest / shortest - 1.0 < 0.016);
    DAWLAB_CHECK(peakOf(lead, 0, 72000 - 120, 72000) < 0.05f * peakOf(lead, 0, 47000, 48000));
    // The vibrato (5.5 Hz) goes up first: the pitch is highest at its first crest at full depth (0.227 s), lowest
    // at the trough after it (0.318 s), +-0.6 % times about 0.95 over 2000 samples around each.
    const double crest = meanPeriod(lead, 9909, 11909), trough = meanPeriod(lead, 14273, 16273);
    std::printf("  generators: lead period %.3f at the vibrato's crest, %.3f at its trough\n", crest, trough);
    DAWLAB_CHECK(crest > 0.0 && crest / trough > 0.985 && crest / trough < 0.995);

    // A noise burst decays by decayPerSecond: 30 per second over 0.12 s leaves e^-1.8 of the first
    // half's level in the second. The highpassed one is a first difference: neighbours anti-correlated.
    const AudioBuffer hit = demo::makeBurst(kSampleRate, 0.12, 30.0, 0.6f, false, 21u);
    const double decay = rmsOf(hit, 2880, 5760) / rmsOf(hit, 0, 2880);
    const AudioBuffer shaker = demo::makeBurst(kSampleRate, 0.07, 50.0, 0.35f, true, 12u);
    double lag1 = 0.0, energy = 0.0;
    for (int i = 1; i < shaker.getNumSamples(); ++i) {
        lag1 += static_cast<double>(shaker.getSample(0, i)) * shaker.getSample(0, i - 1);
        energy += static_cast<double>(shaker.getSample(0, i)) * shaker.getSample(0, i);
    }
    std::printf("  generators: burst decay %.3f (e^-1.8 = 0.165), highpass lag-1 correlation %.2f\n", decay, lag1 / energy);
    DAWLAB_CHECK(peakAbs(hit) > 0.1f && decay > 0.12 && decay < 0.22);
    DAWLAB_CHECK(lag1 / energy < -0.3);
}

// Where the song puts its material, track by track.
void testLayout(const DemoSong& song) {
    const double sr = kSampleRate;
    // Bass: bar 1 plays A1, bar 2 F1 (their own plucks); each note a hair shorter than 2 beats.
    DAWLAB_CHECK(sameAudio(*song.clips[1][0].source, makePluck(55.0, sr, 1.0, 0.6f, 1, 300u)));
    DAWLAB_CHECK(sameAudio(*song.clips[1][2].source, makePluck(43.65, sr, 1.0, 0.6f, 1, 301u)));
    DAWLAB_CHECK(song.clips[1][0].length == 2 * 24000 - 240 && song.clips[1][0].fadeOutSamples == 960);
    // Arp: fades out over an eighth note.
    DAWLAB_CHECK(song.clips[2][0].fadeOutSamples == 12000);
    // Pad: a chord in each of the 8 bars, Am in bar 1, F in bar 2.
    DAWLAB_CHECK(song.clips[3].size() == 8);
    DAWLAB_CHECK(sameAudio(*song.clips[3][0].source, demo::makePadChord(sr, {220.0, 261.63, 329.63}, 2.0)));
    DAWLAB_CHECK(sameAudio(*song.clips[3][1].source, demo::makePadChord(sr, {174.61, 220.0, 261.63}, 2.0)));
    // Wind: half a second in, a second out.
    DAWLAB_CHECK(song.clips[4][0].fadeInSamples == 24000 && song.clips[4][0].fadeOutSamples == 48000);
    // Shaker: every 16th (6000 samples) from bar 3 to the song's end, the off-beat eighths loud.
    const std::vector<Clip>& shaker = song.clips[6];
    bool shakerOk = shaker.size() == 96 && shaker[0].source->getNumSamples() == 2400 && shaker[2].source->getNumSamples() == 3360; // soft 50 ms, loud 70 ms
    for (size_t k = 0; shakerOk && k < shaker.size(); ++k)
        shakerOk = shaker[k].start == 192000 + static_cast<int64_t>(k) * 6000 && (shaker[k].source == shaker[2].source) == (k % 4 == 2);
    DAWLAB_CHECK(shakerOk);
    // Hits: beat 1 of bars 1, 3, 5 and 7.
    const std::vector<Clip>& hits = song.clips[7];
    DAWLAB_CHECK(hits.size() == 4 && hits[0].start == 0 && hits[1].start == 192000 && hits[3].start == 576000);
}

// Parts of the song on their own, where makeDemoSong() never takes them.
void testParts() {
    // The shaker's accents fall on the third 16th of every beat, also when a 16th is not a whole number of
    // samples: at 100 Hz it is 12 (12.5 cut), and bar 3 starts at sample 400, not on a multiple of 12.
    DemoSong slow;
    slow.sampleRate = 100.0;
    demo::addDemoShaker(slow, demo::SongGrid(slow));
    const std::vector<Clip>& shaker = slow.clips[6];
    bool accentsOk = shaker.size() == 100 && shaker[2].source != shaker[0].source;
    for (size_t k = 0; accentsOk && k < shaker.size(); ++k)
        accentsOk = shaker[k].start == 400 + static_cast<int64_t>(k) * 12 && (shaker[k].source == shaker[2].source) == (k % 4 == 2);
    DAWLAB_CHECK(accentsOk);

    // A song of 7 bars has its hits on bars 1, 3, 5 and 7: the last bar counts.
    DemoSong seven;
    seven.bars = 7;
    demo::addDemoHits(seven, demo::SongGrid(seven));
    DAWLAB_CHECK(seven.clips[7].size() == 4 && seven.clips[7][3].start == 576000);
}

// An insert's parameter names the effect does not know are counted, and the engine gets
// exactly the song's 8 tracks, its bus names and its bus faders.
void testApplied(Engine& engine, const DemoSong& song) {
    int unknown = 0;
    const DemoInsert odd{ChannelRef::master(), EffectType::Gain, {{"Gain", -3.0f}, {"No such parameter", 1.0f}}};
    DAWLAB_CHECK(createDemoEffect(odd, &unknown) != nullptr && unknown == 1);
    DAWLAB_CHECK(createDemoEffect(odd) != nullptr);
    const Mixer& m = engine.getMixer();
    DAWLAB_CHECK(engine.getTrack(demoTrackId(kDemoTracks - 1)) != nullptr && engine.getTrack(demoTrackId(kDemoTracks)) == nullptr);
    DAWLAB_CHECK(m.getBus(0).getName() == "Reverb" && m.getBus(kNumBuses - 1).getName() == "Delay");
    DAWLAB_CHECK(m.getBus(0).getGainDb() == -2.0f && m.getBus(kNumBuses - 1).getGainDb() == -4.0f);
    // Every track's fader, pan and sends are the song's.
    int wrong = 0;
    for (int t = 0; t < kDemoTracks; ++t) {
        const Track* track = engine.getTrack(demoTrackId(t));
        const auto at = static_cast<size_t>(t);
        wrong += track->getGainDb() == song.trackGainDb[at] && track->getPan() == song.trackPan[at] ? 0 : 1;
        for (int b = 0; b < kNumBuses; ++b)
            wrong += track->getSendLevelDb(b) == song.sendDb[at][static_cast<size_t>(b)] ? 0 : 1;
    }
    DAWLAB_CHECK(wrong == 0);
}

} // namespace

int main() {
    testGenerators();
    testParts();
    const DemoSong song = makeDemoSong(kSampleRate);
    testLayout(song);

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
    testApplied(engine, song);

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
    return dawlab::test::summary("l15_test_demo_song");
}
