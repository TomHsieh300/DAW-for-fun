// l14_render_song -- Steps 4, 6 and 9: a complete 8-track song, mixed and bounced offline.
//
// What it shows
//   - 8 tracks of material generated with Signals.h (no audio files needed):
//       1 Drums   drum loop (2 bars, repeated 4 times = 4 clips sharing one buffer)
//       2 Bass    plucked bass, two notes per bar (16 clips, 4 shared pitch buffers)
//       3 Arp     the A-minor pluck arpeggio, eighth notes
//       4 Pad     detuned sine chords Am - F - C - G with slow attack, stereo
//       5 Wind    low-passed noise, chopped in 16ths by a tremolo, cleaned by a gate
//       6 Lead    sine melody with vibrato in bars 5-8, distorted and flanged
//       7 Shaker  high-passed noise bursts on 16ths with accents
//       8 Hits    short noise bursts on beat 1 of every other bar, through a convolution reverb
//   - 17 effects of chapters 01-12 are used somewhere as inserts (all but Filter and AmpChain).
//   - Two FX buses: A = algorithmic reverb, B = tempo-synced ping-pong delay (dotted eighth);
//     post-fader sends at different levels per track.
//   - Master: glue compressor + lookahead limiter (ceiling -1 dBFS).
//   - Offline bounce of 8 bars (16 s) + 3 s of tail, latency-compensated.
//   - A mix report: per-track and master peak / RMS / integrated loudness (chapter 11 meters).
//   - Solo: a second bounce with only the drums soloed (their reverb send stays: buses are
//     solo safe).
//
// Run
//   build/bin/Debug/l14_render_song.exe
//
// Output
//   dawlab_output/l14_render_song.wav              the full mix, 19 s, 24-bit
//   dawlab_output/l14_render_song_drums_solo.wav   bars 1-4 with the drums soloed (+ their reverb)
//
// What you should hear
//   A short loop-based tune at 120 BPM in A minor: punchy drums, a saturated bass, an
//   arpeggio bouncing through a ping-pong delay, a wide chorus pad in a reverb, a rhythmic
//   "wind" texture on the right, a fuzzy lead entering at bar 5, shaker and a roomy hit.
//   The end rings out with reverb and delay tails. Peaks never exceed -1 dBFS.

#include <dawlab/Common.h>
#include <dawlab/engine/All.h>

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace dawlab;
using namespace dawlab::engine;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr double kBpm = 120.0;
constexpr int kBars = 8;

using BufferPtr = std::shared_ptr<const AudioBuffer>;

// ---- material ---------------------------------------------------------------------------------

// Sine with a slow attack / release envelope; `detuneCents` per channel gives width.
AudioBuffer makePadChord(const std::vector<double>& freqs, double seconds) {
    const int n = secondsToSamples(seconds, kSampleRate);
    AudioBuffer b(2, n);
    const double attack = 0.35, release = 0.5;
    for (int ch = 0; ch < 2; ++ch) {
        const double detune = std::pow(2.0, (ch == 0 ? -6.0 : 6.0) / 1200.0); // +-6 cents
        for (double f0 : freqs) {
            const double f = f0 * detune;
            for (int i = 0; i < n; ++i) {
                const double t = i / kSampleRate;
                const double env = std::min({1.0, t / attack, (seconds - t) / release});
                // sine + a bit of 2nd and 3rd harmonic so the EQ / chorus have something to work on
                const double w = kTwoPi * f * t;
                const double v = std::sin(w) + 0.3 * std::sin(2.0 * w) + 0.15 * std::sin(3.0 * w);
                b.addSample(ch, i, static_cast<float>(0.08 * env * v));
            }
        }
    }
    return b;
}

// One-pole lowpassed white noise ("wind").
AudioBuffer makeWind(double seconds) {
    AudioBuffer b = makeWhiteNoise(kSampleRate, seconds, 0.6f, 2, 77u);
    const float a = std::exp(-static_cast<float>(kTwoPi) * 800.0f / static_cast<float>(kSampleRate));
    for (int ch = 0; ch < 2; ++ch) {
        float z = 0.0f;
        for (int i = 0; i < b.getNumSamples(); ++i) {
            z = (1.0f - a) * b.getSample(ch, i) + a * z; // y[n] = (1-a) x[n] + a y[n-1]
            b.setSample(ch, i, z);
        }
    }
    return b;
}

// Sine lead with vibrato and a pluck-like envelope.
AudioBuffer makeLeadNote(double freq, double seconds) {
    const int n = secondsToSamples(seconds, kSampleRate);
    AudioBuffer b(1, n);
    double phase = 0.0;
    for (int i = 0; i < n; ++i) {
        const double t = i / kSampleRate;
        const double vib = 1.0 + 0.006 * std::sin(kTwoPi * 5.5 * t) * std::min(1.0, t / 0.2);
        phase += kTwoPi * freq * vib / kSampleRate;
        const double env = std::min(1.0, t / 0.01) * std::exp(-t * 1.5) * std::min(1.0, (seconds - t) / 0.05);
        b.setSample(0, i, static_cast<float>(0.35 * env * std::sin(phase)));
    }
    return b;
}

// Short noise burst; `highpass` = first difference (tilts the spectrum up).
AudioBuffer makeBurst(double seconds, double decayPerSecond, float amplitude, bool highpass, uint32_t seed) {
    const int n = secondsToSamples(seconds, kSampleRate);
    AudioBuffer b(1, n);
    NoiseGenerator noise(seed);
    float prev = 0.0f;
    for (int i = 0; i < n; ++i) {
        const float x = noise.next();
        const float y = highpass ? 0.5f * (x - prev) : x;
        prev = x;
        b.setSample(0, i, amplitude * y * static_cast<float>(std::exp(-decayPerSecond * i / kSampleRate)));
    }
    return b;
}

std::shared_ptr<const AudioBuffer> share(AudioBuffer b) { return std::make_shared<const AudioBuffer>(std::move(b)); }

// ---- the project ------------------------------------------------------------------------------

void buildSong(Engine& engine) {
    Transport& tr = engine.getTransport();
    tr.setTempo(kBpm);
    tr.setTimeSignature(4, 4);
    const auto at = [&](int bar, int beat, int tick = 0) { return tr.barsBeatsToSamples({bar, beat, tick}); };
    const int64_t eighth = static_cast<int64_t>(tr.getSamplesPerBeat() / 2);
    const int64_t sixteenth = eighth / 2;
    Mixer& mix = engine.getMixer();

    const char* names[kNumTracks] = {"Drums", "Bass", "Arp", "Pad", "Wind", "Lead", "Shaker", "Hits"};
    for (int t = 0; t < kNumTracks; ++t)
        mix.getTrack(t).setName(names[t]);
    mix.getBus(0).setName("Reverb");
    mix.getBus(1).setName("Delay");

    // 1 Drums: one 2-bar buffer, four clips
    {
        BufferPtr loop = share(makeDrumLoop(kSampleRate, 2, kBpm, 0.8f));
        std::vector<Clip> clips;
        for (int k = 0; k < 4; ++k)
            clips.push_back(Clip::fromBuffer(loop, at(1 + 2 * k, 1), "drums"));
        engine.setTrackClips(0, std::move(clips));
    }
    // 2 Bass: root on beats 1 and 3 (A1 F1 C2 G1)
    {
        const double roots[4] = {55.0, 43.65, 65.41, 49.0};
        BufferPtr notes[4];
        for (int k = 0; k < 4; ++k)
            notes[k] = share(makePluck(roots[k], kSampleRate, 1.0, 0.6f, 1, 300u + static_cast<uint32_t>(k)));
        std::vector<Clip> clips;
        for (int bar = 1; bar <= kBars; ++bar)
            for (int beat : {1, 3}) {
                Clip c = Clip::fromBuffer(notes[(bar - 1) % 4], at(bar, beat), "bass");
                c.length = 2 * static_cast<int64_t>(tr.getSamplesPerBeat()) - 240; // a hair shorter than 2 beats
                c.fadeOutSamples = 960;                                            // 20 ms release
                c.gainDb = beat == 1 ? 0.0f : -3.0f;
                clips.push_back(c);
            }
        engine.setTrackClips(1, std::move(clips));
    }
    // 3 Arp: the whole 16 s arpeggio as one clip, entering at bar 3 with a fade-in
    {
        Clip c = Clip::fromBuffer(share(makePluckSequence(kSampleRate, 12.0, 0.35f)), at(3, 1), "arp");
        c.fadeInSamples = static_cast<int>(tr.getSamplesPerBar()); // one-bar fade-in
        c.fadeOutSamples = static_cast<int>(eighth);
        engine.setTrackClips(2, {c});
    }
    // 4 Pad: one chord per bar
    {
        const std::vector<std::vector<double>> chords = {
            {220.0, 261.63, 329.63}, {174.61, 220.0, 261.63}, {196.0, 261.63, 329.63}, {196.0, 246.94, 293.66}};
        std::vector<Clip> clips;
        for (int bar = 1; bar <= kBars; ++bar) {
            Clip c = Clip::fromBuffer(share(makePadChord(chords[static_cast<size_t>((bar - 1) % 4)], 2.0)), at(bar, 1),
                                      "pad");
            clips.push_back(c);
        }
        engine.setTrackClips(3, std::move(clips));
    }
    // 5 Wind: bars 5-8
    {
        Clip c = Clip::fromBuffer(share(makeWind(8.0)), at(5, 1), "wind");
        c.fadeInSamples = 24000;
        c.fadeOutSamples = 48000;
        c.fadeShape = FadeShape::EqualPower;
        engine.setTrackClips(4, {c});
    }
    // 6 Lead: a little melody in bars 5-8 (quarter and half notes)
    {
        struct Note { int bar, beat; double freq; double beats; };
        const Note melody[] = {{5, 1, 440.0, 1.5}, {5, 3, 523.25, 1.0}, {5, 4, 659.26, 1.0}, {6, 1, 587.33, 2.0},
                               {6, 3, 523.25, 2.0}, {7, 1, 523.25, 1.5}, {7, 3, 659.26, 1.0}, {7, 4, 783.99, 1.0},
                               {8, 1, 739.99, 1.0}, {8, 2, 659.26, 3.0}};
        std::vector<Clip> clips;
        for (const Note& n : melody) {
            const double secs = n.beats * 60.0 / kBpm;
            clips.push_back(Clip::fromBuffer(share(makeLeadNote(n.freq, secs)), at(n.bar, n.beat), "lead"));
        }
        engine.setTrackClips(5, std::move(clips));
    }
    // 7 Shaker: 16ths, accent on the off-beat eighths, from bar 3
    {
        BufferPtr soft = share(makeBurst(0.05, 70.0, 0.15f, true, 11u));
        BufferPtr loud = share(makeBurst(0.07, 50.0, 0.35f, true, 12u));
        std::vector<Clip> clips;
        for (int64_t pos = at(3, 1); pos < at(kBars + 1, 1); pos += sixteenth) {
            const bool accent = ((pos - at(3, 1)) / sixteenth) % 4 == 2;
            clips.push_back(Clip::fromBuffer(accent ? loud : soft, pos, "shaker"));
        }
        engine.setTrackClips(6, std::move(clips));
    }
    // 8 Hits: beat 1 of bars 1, 3, 5, 7
    {
        BufferPtr hit = share(makeBurst(0.12, 30.0, 0.6f, false, 21u));
        std::vector<Clip> clips;
        for (int bar = 1; bar <= kBars; bar += 2)
            clips.push_back(Clip::fromBuffer(hit, at(bar, 1), "hit"));
        engine.setTrackClips(7, std::move(clips));
    }

    // ---- inserts (17 effect types: all but Filter and AmpChain, which came later) ----
    auto add = [&](ChannelRef ch, EffectType type, std::initializer_list<std::pair<const char*, float>> params) {
        std::unique_ptr<InsertEffect> fx = createEffect(type);
        for (const auto& p : params)
            if (!fx->setParameterByName(p.first, p.second))
                std::printf("  (unknown parameter %s.%s)\n", fx->getName(), p.first);
        engine.addInsert(ch, -1, std::move(fx));
    };
    const ChannelRef T[kNumTracks] = {ChannelRef::track(0), ChannelRef::track(1), ChannelRef::track(2),
                                      ChannelRef::track(3), ChannelRef::track(4), ChannelRef::track(5),
                                      ChannelRef::track(6), ChannelRef::track(7)};
    add(T[0], EffectType::TransientShaper, {{"Attack", 4.0f}, {"Sustain", -3.0f}});
    add(T[0], EffectType::Compressor, {{"Threshold", -20.0f}, {"Ratio", 4.0f}, {"Attack", 10.0f}, {"Release", 120.0f}, {"Makeup", 3.0f}});
    add(T[1], EffectType::ParametricEq, {{"B1 Enabled", 1.0f}, {"B1 Frequency", 35.0f}, {"B3 Frequency", 120.0f}, {"B3 Gain", 3.0f}});
    add(T[1], EffectType::Saturation, {{"Drive", 9.0f}, {"Mix", 0.6f}, {"Output", -3.0f}});
    add(T[2], EffectType::Gain, {{"Gain", -3.0f}});
    add(T[2], EffectType::Phaser, {{"Rate", 0.3f}, {"Depth", 0.6f}, {"Mix", 0.4f}});
    add(T[3], EffectType::ParametricEq, {{"B1 Enabled", 1.0f}, {"B1 Frequency", 150.0f}, {"B6 Enabled", 1.0f}, {"B6 Frequency", 5000.0f}});
    add(T[3], EffectType::Chorus, {{"Mix", 0.5f}, {"Depth", 4.0f}});
    add(T[4], EffectType::Tremolo, {{"Rate", 8.0f}, {"Depth", 0.9f}, {"Shape", 3.0f}});
    add(T[4], EffectType::Gate, {{"Threshold", -35.0f}, {"Release", 40.0f}});
    add(T[5], EffectType::Distortion, {{"Type", 2.0f}, {"Drive", 18.0f}, {"Tone", 4000.0f}, {"Mix", 0.5f}, {"Output", -6.0f}});
    add(T[5], EffectType::Flanger, {{"Mix", 0.35f}, {"Rate", 0.2f}});
    add(T[6], EffectType::Expander, {{"Threshold", -40.0f}, {"Ratio", 2.0f}});
    add(T[6], EffectType::DeEsser, {{"Frequency", 7000.0f}, {"Threshold", -30.0f}, {"Range", 6.0f}});
    add(T[7], EffectType::ConvolutionReverb, {{"Mix", 0.5f}});
    add(ChannelRef::bus(0), EffectType::Reverb, {{"Mix", 1.0f}, {"Decay", 2.2f}, {"Pre-delay", 25.0f}});
    add(ChannelRef::bus(1), EffectType::Delay,
        {{"Mix", 1.0f}, {"Time", DelayEffect::noteToMs(kBpm, 0.75)}, {"Feedback", 0.35f}, {"Ping Pong", 1.0f}, {"Lowpass", 5000.0f}});
    add(ChannelRef::master(), EffectType::Compressor, {{"Threshold", -12.0f}, {"Ratio", 2.0f}, {"Attack", 30.0f}, {"Release", 200.0f}});
    add(ChannelRef::master(), EffectType::Limiter, {{"Ceiling", -1.0f}, {"Lookahead", 5.0f}, {"Input Gain", 2.0f}});

    // ---- faders, pans, sends ----
    struct StripSetup { float gainDb, pan, sendReverbDb, sendDelayDb; };
    const StripSetup setup[kNumTracks] = {
        {-2.0f, 0.0f, -18.0f, kMinusInfinityDb},  // Drums
        {-3.0f, 0.0f, kMinusInfinityDb, kMinusInfinityDb}, // Bass: dry
        {-2.0f, -0.35f, -14.0f, -8.0f},           // Arp
        {-4.0f, 0.0f, -8.0f, kMinusInfinityDb},   // Pad
        {-10.0f, 0.5f, -12.0f, kMinusInfinityDb}, // Wind
        {-5.0f, 0.2f, -12.0f, -6.0f},             // Lead
        {-8.0f, 0.6f, -20.0f, kMinusInfinityDb},  // Shaker
        {-6.0f, -0.6f, kMinusInfinityDb, kMinusInfinityDb}, // Hits (own convolution reverb)
    };
    for (int t = 0; t < kNumTracks; ++t) {
        ChannelStrip& s = mix.getTrack(t);
        s.setGainDb(setup[t].gainDb);
        s.setPan(setup[t].pan);
        s.setSendLevelDb(0, setup[t].sendReverbDb);
        s.setSendLevelDb(1, setup[t].sendDelayDb);
    }
    mix.getBus(0).setGainDb(-2.0f);
    mix.getBus(1).setGainDb(-4.0f);
}

void printStrip(const ChannelStrip& s, int numInserts, const Engine& engine, ChannelRef ref) {
    const StripStats st = s.getStats();
    std::string inserts;
    for (int i = 0; i < numInserts; ++i) {
        if (const InsertEffect* fx = engine.getInsert(ref, i)) {
            inserts += i ? ", " : "";
            inserts += fx->getName();
        }
    }
    std::printf("  %-7s %6.1f %7.1f %8.1f   %5.1f dB %5.2f   %s\n", s.getName().c_str(), static_cast<double>(st.peakDb),
                static_cast<double>(st.rmsDb), static_cast<double>(st.integratedLufs),
                static_cast<double>(s.getGainDb()), static_cast<double>(s.getPan()), inserts.c_str());
}

} // namespace

int main() {
    Engine engine;
    engine.prepare(kSampleRate, kBlockSize);
    buildSong(engine);
    engine.applyPendingCommandsOffline();

    const Transport& tr = engine.getTransport();
    RenderOptions options;
    options.numSamples = tr.barsBeatsToSamples({kBars + 1, 1, 0}); // 8 bars = 768000 samples
    options.tailSeconds = 3.0;
    options.blockSize = kBlockSize;

    const auto t0 = std::chrono::steady_clock::now();
    AudioBuffer mixdown;
    const bool ok = bounceToWav(engine, outputPath("l14_render_song.wav"), options, 24, &mixdown);
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const double audioSecs = mixdown.getNumSamples() / kSampleRate;

    const Mixer& m = engine.getMixer();
    std::printf("Step 9: offline bounce of an 8-track song (%d bars at %.0f BPM + %.0f s tail)\n", kBars, kBpm,
                options.tailSeconds);
    std::printf("  length %.2f s, rendered in %.2f s (%.1fx real time), mix latency %d samples (%.2f ms)\n\n",
                audioSecs, secs, audioSecs / secs, engine.getLatencySamples(),
                samplesToMs(engine.getLatencySamples(), kSampleRate));
    std::printf("  strip     peak     RMS     LUFS   fader   pan    inserts\n");
    std::printf("            dBFS    dBFS      (I)\n");
    for (int t = 0; t < kNumTracks; ++t)
        printStrip(m.getTrack(t), engine.getNumInserts(ChannelRef::track(t)), engine, ChannelRef::track(t));
    for (int b = 0; b < kNumBuses; ++b)
        printStrip(m.getBus(b), engine.getNumInserts(ChannelRef::bus(b)), engine, ChannelRef::bus(b));
    printStrip(m.getMaster(), engine.getNumInserts(ChannelRef::master()), engine, ChannelRef::master());

    const float filePeakDb = gainToDb(peakAbs(mixdown));
    const float fileRmsDb = gainToDb(0.5f * (rms(mixdown, 0) + rms(mixdown, 1)));
    std::printf("\n  file: peak %.2f dBFS, RMS %.1f dBFS, integrated loudness %.1f LUFS\n",
                static_cast<double>(filePeakDb), static_cast<double>(fileRmsDb),
                static_cast<double>(m.getMaster().getIntegratedLufs()));
    std::printf("  wrote dawlab_output/l14_render_song.wav\n");

    // Solo the drums (their reverb send stays: buses are solo safe) and bounce again.
    // (4 bars + 1 s tail are enough to hear it and keep the Debug run short.)
    engine.getMixer().getTrack(0).setSolo(true);
    RenderOptions soloOptions = options;
    soloOptions.numSamples = tr.barsBeatsToSamples({5, 1, 0});
    soloOptions.tailSeconds = 1.0;
    AudioBuffer solo = renderOffline(engine, soloOptions);
    writeWav(outputPath("l14_render_song_drums_solo.wav"), solo, kSampleRate);
    engine.getMixer().getTrack(0).setSolo(false);
    std::printf("  drums solo: peak %.2f dBFS, wrote dawlab_output/l14_render_song_drums_solo.wav\n",
                static_cast<double>(gainToDb(peakAbs(solo))));
    std::printf("  silent strips in the solo bounce: Bass %.0f dBFS, Arp %.0f dBFS (silenced by the solo)\n",
                static_cast<double>(m.getTrack(1).getMaxPeakDb()), static_cast<double>(m.getTrack(2).getMaxPeakDb()));

    DAWLAB_CHECK(ok);
    DAWLAB_CHECK(!hasNanOrInf(mixdown));
    DAWLAB_CHECK(mixdown.getNumSamples() == 768000 + 144000);
    DAWLAB_CHECK(filePeakDb <= -1.0f + 0.05f); // the master limiter's guarantee
    DAWLAB_CHECK(filePeakDb > -12.0f);          // and the song is not silent
    DAWLAB_CHECK(peakAbs(solo) > 0.0f);
    DAWLAB_CHECK(m.getTrack(1).getMaxPeakDb() <= kMinusInfinityDb); // solo silenced the bass strip
    return dawlab::test::summary("l14_render_song");
}
