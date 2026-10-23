#pragma once
// check.h - the self-check of Step 9 (offline render). Defines main(); included at the END of
// starter/step09.cpp and solution/step09.cpp.
//
// Tests only what Step 9 adds: length bookkeeping, latency removal, the tail, transport
// restore, "bounce == playback", block-size independence and the WAV file.

#include <dawlab/Common.h>

#include <cmath>
#include <cstdio>
#include <memory>

namespace step09_check {

using namespace dawlab;
using namespace minidaw;

constexpr double kSampleRate = 48000.0;
constexpr float g = 0.70710678f;

inline float at(const AudioBuffer& b, int ch, int i) { // 0 outside the buffer (starter safety)
    return (i >= 0 && i < b.getNumSamples() && ch < b.getNumChannels()) ? b.getSample(ch, i) : 0.0f;
}

// The test song: 1 s long (track 1 = an impulse buffer of 48000 samples, impulses at 0 and
// 24000), a 240-sample "lookahead" (PureDelay) on the master.
inline void buildSong(Engine& engine) {
    engine.prepare(kSampleRate, 512);
    auto clip = std::make_shared<AudioBuffer>(1, 48000);
    clip->setSample(0, 0, 0.5f);
    clip->setSample(0, 24000, 0.25f);
    engine.setTrackClips(0, {Clip::fromBuffer(clip, 0)});
    auto fx = createEffect("PureDelay");
    fx->setParameter(0, 240.0f);
    engine.addInsert(ChannelRef::master(), -1, std::move(fx));
}

inline int run() {
    std::printf("Step 9: offline render (build-along)\n");

    // ---- 1) length, latency removal, transport restore ---------------------------------------------
    Engine engine;
    buildSong(engine);
    engine.getTransport().setPosition(12345); // the user's state must survive the bounce
    engine.getTransport().setLoopRange(0, 1000);
    engine.getTransport().setLoopEnabled(true);
    RenderOptions opt;
    opt.tailSeconds = 0.5;
    AudioBuffer mix;
    const bool written = bounceToWav(engine, outputPath("l14_step09.wav"), opt, 24, &mix);
    std::printf("  rendered %d samples (expected 48000 + 24000 = 72000), latency %d (expected 240)\n",
                mix.getNumSamples(), engine.getLatencySamples());
    std::printf("  file[0] = %.4f, file[24000] = %.4f (expected %.4f, %.4f: the latency is gone)\n", at(mix, 0, 0),
                at(mix, 0, 24000), 0.5f * g, 0.25f * g);
    DAWLAB_CHECK(mix.getNumSamples() == 72000);
    DAWLAB_CHECK(engine.getLatencySamples() == 240);
    DAWLAB_CHECK_NEAR(at(mix, 0, 0), 0.5 * g, 1e-6);
    DAWLAB_CHECK_NEAR(at(mix, 1, 24000), 0.25 * g, 1e-6);
    DAWLAB_CHECK(at(mix, 0, 240) == 0.0f); // nothing left at the uncompensated position
    std::printf("  transport after the bounce: position %lld, loop %s, %s (expected 12345, on, stopped)\n",
                static_cast<long long>(engine.getTransport().getPosition()),
                engine.getTransport().isLoopEnabled() ? "on" : "off", engine.getTransport().isPlaying() ? "playing" : "stopped");
    DAWLAB_CHECK(engine.getTransport().getPosition() == 12345);
    DAWLAB_CHECK(engine.getTransport().isLoopEnabled() && !engine.getTransport().isPlaying());

    // ---- 2) the WAV file ---------------------------------------------------------------------------
    WavData back;
    const bool readBack = written && mix.getNumSamples() > 0 && readWav(outputPath("l14_step09.wav"), back);
    std::printf("  wrote dawlab_output/l14_step09.wav: %d samples at %.0f Hz\n",
                readBack ? back.buffer.getNumSamples() : 0, readBack ? back.sampleRate : 0.0);
    DAWLAB_CHECK(readBack && back.buffer.getNumSamples() == 72000 && back.sampleRate == kSampleRate);

    // ---- 3) bounce == playback: the same engine code, the clock taken away from the device --------
    {
        Engine live;
        buildSong(live);
        live.getTransport().play();
        AudioBuffer played(kNumChannels, 72000 + 240);
        for (int pos = 0; pos < played.getNumSamples(); pos += 100) // a "device" with 100-sample blocks
            live.processBlock(played.toBlock().getSubBlock(pos, std::min(100, played.getNumSamples() - pos)));
        live.collectGarbage();
        float worst = mix.getNumSamples() == 72000 ? 0.0f : 1.0f;
        for (int ch = 0; ch < kNumChannels && mix.getNumSamples() == 72000; ++ch)
            for (int i = 0; i < 48000; ++i) // the song part (the tail of playback keeps playing)
                worst = std::max(worst, std::abs(mix.getSample(ch, i) - played.getSample(ch, i + 240)));
        std::printf("  bounce vs live playback shifted by the latency: max difference %g (expected 0)\n", worst);
        DAWLAB_CHECK(worst == 0.0f);
    }

    // ---- 4) block size, start position ------------------------------------------------------------
    {
        Engine e;
        buildSong(e);
        RenderOptions small;
        small.blockSize = 37;
        small.tailSeconds = 0.5;
        const AudioBuffer m37 = renderOffline(e, small);
        float worst = m37.getNumSamples() == mix.getNumSamples() && mix.getNumSamples() > 0 ? 0.0f : 1.0f;
        for (int ch = 0; ch < kNumChannels && worst == 0.0f; ++ch)
            for (int i = 0; i < m37.getNumSamples(); ++i)
                worst = std::max(worst, std::abs(m37.getSample(ch, i) - mix.getSample(ch, i)));
        DAWLAB_CHECK(worst == 0.0f);
        RenderOptions from;
        from.startSample = 24000;
        const AudioBuffer half = renderOffline(e, from);
        std::printf("  render from 24000: %d samples (expected 24000), file[0] = %.4f (expected %.4f)\n",
                    half.getNumSamples(), at(half, 0, 0), 0.25f * g);
        DAWLAB_CHECK(half.getNumSamples() == 24000);
        DAWLAB_CHECK_NEAR(at(half, 0, 0), 0.25 * g, 1e-6);
    }

    // ---- 5) the tail: effects keep running after the song end -----------------------------------------
    {
        Engine e;
        e.prepare(kSampleRate, 512);
        auto clip = std::make_shared<AudioBuffer>(1, 1000);
        clip->setSample(0, 900, 0.5f); // the last event of the song, 100 samples before its end
        e.setTrackClips(0, {Clip::fromBuffer(clip, 47000)});
        auto fx = createEffect("PureDelay");
        fx->setParameter(0, 5000.0f); // an "echo" 5000 samples later...
        e.addInsert(ChannelRef::track(0), -1, std::move(fx));
        e.getMixer().setLatencyCompensationEnabled(false); // ...that is not compensated
        RenderOptions t;
        t.tailSeconds = 0.25;
        const AudioBuffer withTail = renderOffline(e, t);
        std::printf("  tail: %d samples (expected 60000), the echo at 52900 = %.4f (expected %.4f)\n",
                    withTail.getNumSamples(), at(withTail, 0, 52900), 0.5f * g);
        DAWLAB_CHECK(withTail.getNumSamples() == 60000);
        DAWLAB_CHECK_NEAR(at(withTail, 0, 52900), 0.5 * g, 1e-6);
    }

    return dawlab::test::summary("l14_step09");
}

} // namespace step09_check

int main() { return step09_check::run(); }
