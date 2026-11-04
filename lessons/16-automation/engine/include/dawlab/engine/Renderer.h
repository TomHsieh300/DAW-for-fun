#pragma once
// Renderer: offline render / bounce to WAV (Step 9).
//
// Offline rendering is nothing more than calling processBlock() in a loop as fast as the CPU
// allows, with the transport driven by the render loop instead of an audio device:
//
//   apply pending commands -> reset -> seek(start) -> play
//   -> processBlock() until the song end -> stop
//   -> keep rendering `tailSeconds` (reverb / delay tails) + `latency` samples
//   -> drop the first `latency` samples (latency-compensated bounce) -> restore transport
//
// Because processBlock() is the very code the audio device calls, "what you hear is what you
// bounce" holds by construction. The render runs on the calling thread; never run it while
// an audio device is also calling processBlock() (the JUCE app stops the device first, or
// renders a second Engine built from the same project).
//
// Latency compensation: if the mix has a total latency of L samples (e.g. a lookahead limiter
// on the master), the first sound of the song comes out L samples late. We render L extra
// samples and discard the first L, so sample 0 of the file is timeline sample `startSample`.

#include <dawlab/AudioBuffer.h>
#include <dawlab/WavFile.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include "Engine.h"

namespace dawlab::engine {

struct RenderOptions {
    int64_t startSample = 0;        // timeline position to start from
    int64_t numSamples = 0;         // length of the song part (0 = up to the end of the last clip)
    double tailSeconds = 0.0;       // extra time rendered with the transport stopped
    int blockSize = 512;            // processBlock() size used while rendering
    bool resetBeforeRender = true;  // clear tails / meters so the bounce starts from silence
    bool compensateLatency = true;  // drop the first getLatencySamples() samples
    bool restoreTransport = true;   // put position / loop / play state back afterwards
};

// Renders into a new stereo AudioBuffer of exactly numSamples + tail samples. Afterwards the
// strips' exact statistics (ChannelStrip::getStats()) describe this render, so a "mix report"
// can be printed from them.
inline AudioBuffer renderOffline(Engine& engine, const RenderOptions& options) {
    engine.applyPendingCommandsOffline(); // clips and inserts queued during setup are now live
    Transport& transport = engine.getTransport();
    const double sr = engine.getSampleRate();
    const int blockSize = std::max(1, options.blockSize);

    const int64_t songSamples =
        options.numSamples > 0 ? options.numSamples
                               : std::max<int64_t>(0, engine.getMixer().getSongEndSample() - options.startSample);
    const int64_t tailSamples = static_cast<int64_t>(std::llround(std::max(0.0, options.tailSeconds) * sr));
    const int64_t latency = options.compensateLatency ? engine.getLatencySamples() : 0;
    const int64_t outLength = songSamples + tailSamples;
    const int64_t total = outLength + latency;

    // Remember the transport so the user's play position and loop survive a bounce.
    const bool wasPlaying = transport.isPlaying();
    const bool wasLooping = transport.isLoopEnabled();
    const int64_t wasAt = transport.getPosition();

    transport.stop();
    transport.setLoopEnabled(false);
    transport.setPosition(options.startSample);
    if (options.resetBeforeRender)
        engine.requestReset();

    AudioBuffer work(kNumChannels, static_cast<int>(total));
    AudioBlock workBlock = work.toBlock();
    transport.play();
    int64_t done = 0;
    while (done < total) {
        if (done >= songSamples)
            transport.stop(); // tail: tracks go silent, inserts keep ringing
        int64_t len = std::min<int64_t>(blockSize, total - done);
        if (done < songSamples)
            len = std::min(len, songSamples - done); // stop exactly at the song end
        engine.processBlock(workBlock.getSubBlock(static_cast<int>(done), static_cast<int>(len)));
        engine.collectGarbage();
        done += len;
    }
    transport.stop();

    if (options.restoreTransport) {
        transport.setLoopEnabled(wasLooping);
        transport.setPosition(wasAt);
        transport.setPlaying(wasPlaying);
    }

    if (latency == 0)
        return work;
    AudioBuffer out(kNumChannels, static_cast<int>(outLength));
    for (int ch = 0; ch < kNumChannels; ++ch)
        out.copyFrom(ch, 0, work, ch, static_cast<int>(latency), static_cast<int>(outLength));
    return out;
}

// Renders and writes a WAV (24-bit by default; 32 = float keeps overs). False on I/O error.
inline bool bounceToWav(Engine& engine, const std::string& path, const RenderOptions& options, int bitDepth = 24,
                        AudioBuffer* renderedOut = nullptr) {
    AudioBuffer mix = renderOffline(engine, options);
    const bool ok = writeWav(path, mix, engine.getSampleRate(), bitDepth);
    if (renderedOut)
        *renderedOut = std::move(mix);
    return ok;
}

} // namespace dawlab::engine
