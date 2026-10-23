#pragma once
// DemoSong: the 8-bar, 8-track demo project of l14_render_song (Step 9), described as data, for
// the application's "Load demo song" command (Step 18), its smoke test and the JUCE exercises.
// (l14_render_song builds the same song by hand, step by step, so that Step 9 can explain it.)
//
// The song is described as *data* in two phases, so that a GUI can do the expensive part on a
// background thread and the cheap part on the message thread:
//
//   makeDemoSong(sampleRate)   generates all audio with Signals.h and builds the clip lists,
//                              the insert list (effect type + parameters by name) and the mix
//                              settings. Allocates a lot; any non-audio thread. Pure function:
//                              the same sample rate always gives the same song.
//   applyDemoSong(engine, s)   pushes it into an Engine: tempo, names, setTrackClips(),
//                              addInsert() with parameters, faders, pans, sends. The engine's
//                              single producer thread (the UI thread; offline: main()).
//
// The application does not call applyDemoSong(): it applies the same data through its own
// Session model, so that the GUI knows which insert sits in which slot (app/Source/Session.cpp).
//
// The material (no audio files needed):
//   1 Drums   drum loop (2 bars, repeated 4 times = 4 clips sharing one buffer)
//   2 Bass    plucked bass, two notes per bar (16 clips, 4 shared pitch buffers)
//   3 Arp     the A-minor pluck arpeggio, eighth notes, from bar 3
//   4 Pad     detuned chords Am - F - C - G with slow attack, stereo
//   5 Wind    low-passed noise, chopped in 16ths by a tremolo, cleaned by a gate (bars 5-8)
//   6 Lead    sine melody with vibrato in bars 5-8, distorted and flanged
//   7 Shaker  high-passed noise bursts on 16ths with accents, from bar 3
//   8 Hits    short noise bursts on beat 1 of every other bar, through a convolution reverb
// 17 effect types are used (every one except Filter and AmpChain, which were added to the catalog
// later); bus A = reverb, bus B = dotted-eighth ping-pong
// delay; master = glue compressor + -1 dBFS lookahead limiter.

#include <dawlab/AudioBuffer.h>
#include <dawlab/DspMath.h>
#include <dawlab/Signals.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "Clip.h"
#include "Commands.h"
#include "Constants.h"
#include "Effects.h"
#include "Engine.h"
#include "Transport.h"

namespace dawlab::engine {

struct DemoInsert {
    ChannelRef channel;
    EffectType type = EffectType::Gain;
    std::vector<std::pair<const char*, float>> parameters; // by name (string literals)
};

struct DemoSong {
    double sampleRate = 48000.0;
    double bpm = 120.0;
    int beatsPerBar = 4;
    int bars = 8;

    std::array<std::string, kNumTracks> trackNames;
    std::array<std::string, kNumBuses> busNames;
    std::array<std::vector<Clip>, kNumTracks> clips;
    std::vector<DemoInsert> inserts; // in the order they are added

    std::array<float, kNumTracks> trackGainDb{};
    std::array<float, kNumTracks> trackPan{};
    std::array<std::array<float, kNumBuses>, kNumTracks> sendDb{};
    std::array<float, kNumBuses> busGainDb{};

    // Timeline end of the song part (without tails): bar `bars + 1`.
    int64_t songEndSample() const noexcept {
        return static_cast<int64_t>(std::llround(bars * beatsPerBar * 60.0 * sampleRate / bpm));
    }
};

namespace demo {

using BufferPtr = std::shared_ptr<const AudioBuffer>;

inline BufferPtr share(AudioBuffer b) { return std::make_shared<const AudioBuffer>(std::move(b)); }

// Sine with a slow attack / release envelope; +-6 cents detune per channel gives width.
inline AudioBuffer makePadChord(double sr, const std::vector<double>& freqs, double seconds) {
    const int n = secondsToSamples(seconds, sr);
    AudioBuffer b(2, n);
    const double attack = 0.35, release = 0.5;
    for (int ch = 0; ch < 2; ++ch) {
        const double detune = std::pow(2.0, (ch == 0 ? -6.0 : 6.0) / 1200.0);
        for (double f0 : freqs) {
            const double f = f0 * detune;
            for (int i = 0; i < n; ++i) {
                const double t = i / sr;
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

// One-pole lowpassed white noise ("wind"): y[n] = (1-a) x[n] + a y[n-1], a = e^{-2 pi 800 / fs}.
inline AudioBuffer makeWind(double sr, double seconds) {
    AudioBuffer b = makeWhiteNoise(sr, seconds, 0.6f, 2, 77u);
    const float a = std::exp(-static_cast<float>(kTwoPi) * 800.0f / static_cast<float>(sr));
    for (int ch = 0; ch < 2; ++ch) {
        float z = 0.0f;
        for (int i = 0; i < b.getNumSamples(); ++i) {
            z = (1.0f - a) * b.getSample(ch, i) + a * z;
            b.setSample(ch, i, z);
        }
    }
    return b;
}

// Sine lead with vibrato and a pluck-like envelope.
inline AudioBuffer makeLeadNote(double sr, double freq, double seconds) {
    const int n = secondsToSamples(seconds, sr);
    AudioBuffer b(1, n);
    double phase = 0.0;
    for (int i = 0; i < n; ++i) {
        const double t = i / sr;
        const double vib = 1.0 + 0.006 * std::sin(kTwoPi * 5.5 * t) * std::min(1.0, t / 0.2);
        phase += kTwoPi * freq * vib / sr;
        const double env = std::min(1.0, t / 0.01) * std::exp(-t * 1.5) * std::min(1.0, (seconds - t) / 0.05);
        b.setSample(0, i, static_cast<float>(0.35 * env * std::sin(phase)));
    }
    return b;
}

// Short noise burst; `highpass` = first difference (tilts the spectrum up).
inline AudioBuffer makeBurst(double sr, double seconds, double decayPerSecond, float amplitude, bool highpass,
                             uint32_t seed) {
    const int n = secondsToSamples(seconds, sr);
    AudioBuffer b(1, n);
    NoiseGenerator noise(seed);
    float prev = 0.0f;
    for (int i = 0; i < n; ++i) {
        const float x = noise.next();
        const float y = highpass ? 0.5f * (x - prev) : x;
        prev = x;
        b.setSample(0, i, amplitude * y * static_cast<float>(std::exp(-decayPerSecond * i / sr)));
    }
    return b;
}

} // namespace demo

// Generates the whole song for `sampleRate`. Non-audio thread (allocates ~40 MB at 48 kHz).
inline DemoSong makeDemoSong(double sampleRate) {
    using namespace demo;
    DemoSong song;
    song.sampleRate = sampleRate;
    const double sr = sampleRate;

    Transport tr; // only for the bars:beats -> samples conversions
    tr.prepare(sr);
    tr.setTempo(song.bpm);
    tr.setTimeSignature(song.beatsPerBar, 4);
    const auto at = [&](int bar, int beat, int tick = 0) { return tr.barsBeatsToSamples({bar, beat, tick}); };
    const int64_t eighth = static_cast<int64_t>(tr.getSamplesPerBeat() / 2);
    const int64_t sixteenth = eighth / 2;
    const int kBars = song.bars;

    song.trackNames = {"Drums", "Bass", "Arp", "Pad", "Wind", "Lead", "Shaker", "Hits"};
    for (int b = 0; b < kNumBuses; ++b)
        song.busNames[static_cast<size_t>(b)] = std::string("Bus ") + static_cast<char>('A' + b);
    song.busNames[0] = "Reverb";
    if constexpr (kNumBuses > 1)
        song.busNames[static_cast<size_t>(std::min(1, kNumBuses - 1))] = "Delay";

    // 1 Drums: one 2-bar buffer, four clips
    {
        BufferPtr loop = share(makeDrumLoop(sr, 2, song.bpm, 0.8f));
        for (int k = 0; k < 4; ++k)
            song.clips[0].push_back(Clip::fromBuffer(loop, at(1 + 2 * k, 1), "drums"));
    }
    // 2 Bass: root on beats 1 and 3 (A1 F1 C2 G1)
    {
        const double roots[4] = {55.0, 43.65, 65.41, 49.0};
        BufferPtr notes[4];
        for (int k = 0; k < 4; ++k)
            notes[k] = share(makePluck(roots[k], sr, 1.0, 0.6f, 1, 300u + static_cast<uint32_t>(k)));
        for (int bar = 1; bar <= kBars; ++bar)
            for (int beat : {1, 3}) {
                Clip c = Clip::fromBuffer(notes[(bar - 1) % 4], at(bar, beat), "bass");
                c.length = 2 * static_cast<int64_t>(tr.getSamplesPerBeat()) - 240; // a hair shorter than 2 beats
                c.fadeOutSamples = 960;                                            // 20 ms release at 48 kHz
                c.gainDb = beat == 1 ? 0.0f : -3.0f;
                song.clips[1].push_back(c);
            }
    }
    // 3 Arp: the 12 s arpeggio as one clip, entering at bar 3 with a one-bar fade-in
    {
        Clip c = Clip::fromBuffer(share(makePluckSequence(sr, 12.0, 0.35f)), at(3, 1), "arp");
        c.fadeInSamples = static_cast<int>(tr.getSamplesPerBar());
        c.fadeOutSamples = static_cast<int>(eighth);
        song.clips[2].push_back(c);
    }
    // 4 Pad: one chord per bar
    {
        const std::vector<std::vector<double>> chords = {
            {220.0, 261.63, 329.63}, {174.61, 220.0, 261.63}, {196.0, 261.63, 329.63}, {196.0, 246.94, 293.66}};
        for (int bar = 1; bar <= kBars; ++bar)
            song.clips[3].push_back(
                Clip::fromBuffer(share(makePadChord(sr, chords[static_cast<size_t>((bar - 1) % 4)], 2.0)), at(bar, 1),
                                 "pad"));
    }
    // 5 Wind: bars 5-8
    {
        Clip c = Clip::fromBuffer(share(makeWind(sr, 8.0)), at(5, 1), "wind");
        c.fadeInSamples = static_cast<int>(std::lround(0.5 * sr));
        c.fadeOutSamples = static_cast<int>(std::lround(1.0 * sr));
        c.fadeShape = FadeShape::EqualPower;
        song.clips[4].push_back(c);
    }
    // 6 Lead: a little melody in bars 5-8
    {
        struct Note {
            int bar, beat;
            double freq;
            double beats;
        };
        const Note melody[] = {{5, 1, 440.0, 1.5}, {5, 3, 523.25, 1.0}, {5, 4, 659.26, 1.0}, {6, 1, 587.33, 2.0},
                               {6, 3, 523.25, 2.0}, {7, 1, 523.25, 1.5}, {7, 3, 659.26, 1.0}, {7, 4, 783.99, 1.0},
                               {8, 1, 739.99, 1.0}, {8, 2, 659.26, 3.0}};
        for (const Note& n : melody) {
            const double secs = n.beats * 60.0 / song.bpm;
            song.clips[5].push_back(Clip::fromBuffer(share(makeLeadNote(sr, n.freq, secs)), at(n.bar, n.beat), "lead"));
        }
    }
    // 7 Shaker: 16ths, accent on the off-beat eighths, from bar 3
    {
        BufferPtr soft = share(makeBurst(sr, 0.05, 70.0, 0.15f, true, 11u));
        BufferPtr loud = share(makeBurst(sr, 0.07, 50.0, 0.35f, true, 12u));
        for (int64_t pos = at(3, 1); pos < at(kBars + 1, 1); pos += sixteenth) {
            const bool accent = ((pos - at(3, 1)) / sixteenth) % 4 == 2;
            song.clips[6].push_back(Clip::fromBuffer(accent ? loud : soft, pos, "shaker"));
        }
    }
    // 8 Hits: beat 1 of bars 1, 3, 5, 7
    {
        BufferPtr hit = share(makeBurst(sr, 0.12, 30.0, 0.6f, false, 21u));
        for (int bar = 1; bar <= kBars; bar += 2)
            song.clips[7].push_back(Clip::fromBuffer(hit, at(bar, 1), "hit"));
    }

    // ---- inserts (17 effect types: all but Filter and AmpChain, which came later) ----
    const auto T = [](int t) { return ChannelRef::track(t); };
    song.inserts = {
        {T(0), EffectType::TransientShaper, {{"Attack", 4.0f}, {"Sustain", -3.0f}}},
        {T(0), EffectType::Compressor,
         {{"Threshold", -20.0f}, {"Ratio", 4.0f}, {"Attack", 10.0f}, {"Release", 120.0f}, {"Makeup", 3.0f}}},
        {T(1), EffectType::ParametricEq,
         {{"B1 Enabled", 1.0f}, {"B1 Frequency", 35.0f}, {"B3 Frequency", 120.0f}, {"B3 Gain", 3.0f}}},
        {T(1), EffectType::Saturation, {{"Drive", 9.0f}, {"Mix", 0.6f}, {"Output", -3.0f}}},
        {T(2), EffectType::Gain, {{"Gain", -3.0f}}},
        {T(2), EffectType::Phaser, {{"Rate", 0.3f}, {"Depth", 0.6f}, {"Mix", 0.4f}}},
        {T(3), EffectType::ParametricEq,
         {{"B1 Enabled", 1.0f}, {"B1 Frequency", 150.0f}, {"B6 Enabled", 1.0f}, {"B6 Frequency", 5000.0f}}},
        {T(3), EffectType::Chorus, {{"Mix", 0.5f}, {"Depth", 4.0f}}},
        {T(4), EffectType::Tremolo, {{"Rate", 8.0f}, {"Depth", 0.9f}, {"Shape", 3.0f}}},
        {T(4), EffectType::Gate, {{"Threshold", -35.0f}, {"Release", 40.0f}}},
        {T(5), EffectType::Distortion,
         {{"Type", 2.0f}, {"Drive", 18.0f}, {"Tone", 4000.0f}, {"Mix", 0.5f}, {"Output", -6.0f}}},
        {T(5), EffectType::Flanger, {{"Mix", 0.35f}, {"Rate", 0.2f}}},
        {T(6), EffectType::Expander, {{"Threshold", -40.0f}, {"Ratio", 2.0f}}},
        {T(6), EffectType::DeEsser, {{"Frequency", 7000.0f}, {"Threshold", -30.0f}, {"Range", 6.0f}}},
        {T(7), EffectType::ConvolutionReverb, {{"Mix", 0.5f}}},
        {ChannelRef::bus(0), EffectType::Reverb, {{"Mix", 1.0f}, {"Decay", 2.2f}, {"Pre-delay", 25.0f}}},
        {ChannelRef::bus(1), EffectType::Delay,
         {{"Mix", 1.0f},
          {"Time", DelayEffect::noteToMs(song.bpm, 0.75)},
          {"Feedback", 0.35f},
          {"Ping Pong", 1.0f},
          {"Lowpass", 5000.0f}}},
        {ChannelRef::master(), EffectType::Compressor,
         {{"Threshold", -12.0f}, {"Ratio", 2.0f}, {"Attack", 30.0f}, {"Release", 200.0f}}},
        {ChannelRef::master(), EffectType::Limiter, {{"Ceiling", -1.0f}, {"Lookahead", 5.0f}, {"Input Gain", 2.0f}}},
    };

    // ---- faders, pans, sends (bus A = reverb, bus B = delay) ----
    struct StripSetup {
        float gainDb, pan, sendReverbDb, sendDelayDb;
    };
    const StripSetup setup[kNumTracks] = {
        {-2.0f, 0.0f, -18.0f, kMinusInfinityDb},            // Drums
        {-3.0f, 0.0f, kMinusInfinityDb, kMinusInfinityDb},  // Bass: dry
        {-2.0f, -0.35f, -14.0f, -8.0f},                     // Arp
        {-4.0f, 0.0f, -8.0f, kMinusInfinityDb},             // Pad
        {-10.0f, 0.5f, -12.0f, kMinusInfinityDb},           // Wind
        {-5.0f, 0.2f, -12.0f, -6.0f},                       // Lead
        {-8.0f, 0.6f, -20.0f, kMinusInfinityDb},            // Shaker
        {-6.0f, -0.6f, kMinusInfinityDb, kMinusInfinityDb}, // Hits (own convolution reverb)
    };
    for (int t = 0; t < kNumTracks; ++t) {
        const StripSetup& s = setup[t];
        song.trackGainDb[static_cast<size_t>(t)] = s.gainDb;
        song.trackPan[static_cast<size_t>(t)] = s.pan;
        auto& sends = song.sendDb[static_cast<size_t>(t)];
        sends.fill(kMinusInfinityDb);
        sends[0] = s.sendReverbDb;
        if constexpr (kNumBuses > 1)
            sends[static_cast<size_t>(std::min(1, kNumBuses - 1))] = s.sendDelayDb;
    }
    song.busGainDb.fill(0.0f);
    song.busGainDb[0] = -2.0f;
    if constexpr (kNumBuses > 1)
        song.busGainDb[static_cast<size_t>(std::min(1, kNumBuses - 1))] = -4.0f;
    return song;
}

// Creates the effect of one DemoInsert with its parameters set. Returns the number of
// parameter names the effect did not know (0 = all applied) through `unknown`.
inline std::unique_ptr<InsertEffect> createDemoEffect(const DemoInsert& d, int* unknown = nullptr) {
    std::unique_ptr<InsertEffect> fx = createEffect(d.type);
    int missing = 0;
    for (const auto& p : d.parameters)
        if (!fx->setParameterByName(p.first, p.second))
            ++missing;
    if (unknown)
        *unknown += missing;
    return fx;
}

// Pushes the song into `engine` (prepared, empty). Returns the number of unknown parameter
// names (0 when the effect adapters match the song). Engine producer thread.
inline int applyDemoSong(Engine& engine, const DemoSong& song) {
    Transport& tr = engine.getTransport();
    tr.setTempo(song.bpm);
    tr.setTimeSignature(song.beatsPerBar, 4);
    Mixer& mix = engine.getMixer();
    for (int t = 0; t < kNumTracks; ++t)
        mix.getTrack(t).setName(song.trackNames[static_cast<size_t>(t)]);
    for (int b = 0; b < kNumBuses; ++b)
        mix.getBus(b).setName(song.busNames[static_cast<size_t>(b)]);
    for (int t = 0; t < kNumTracks; ++t)
        engine.setTrackClips(t, song.clips[static_cast<size_t>(t)]);

    int unknown = 0;
    for (const DemoInsert& d : song.inserts)
        engine.addInsert(d.channel, -1, createDemoEffect(d, &unknown));

    for (int t = 0; t < kNumTracks; ++t) {
        ChannelStrip& s = mix.getTrack(t);
        s.setGainDb(song.trackGainDb[static_cast<size_t>(t)]);
        s.setPan(song.trackPan[static_cast<size_t>(t)]);
        for (int b = 0; b < kNumBuses; ++b)
            s.setSendLevelDb(b, song.sendDb[static_cast<size_t>(t)][static_cast<size_t>(b)]);
    }
    for (int b = 0; b < kNumBuses; ++b)
        mix.getBus(b).setGainDb(song.busGainDb[static_cast<size_t>(b)]);
    return unknown;
}

} // namespace dawlab::engine
