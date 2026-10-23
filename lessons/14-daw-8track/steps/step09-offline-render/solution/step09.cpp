// Step 9 (solution): renderOffline().
//
// Build and run:  l14_step09_solution   (ctest, label "solution")
// Your version:   starter/step09.cpp -> l14_step09_starter

#include "../minidaw.h"

namespace minidaw {

AudioBuffer renderOffline(Engine& engine, const RenderOptions& options) {
    engine.applyPendingCommandsOffline(); // clips and inserts queued during setup are live now
    Transport& transport = engine.getTransport();
    const int blockSize = std::max(1, options.blockSize);

    const int64_t songSamples =
        options.numSamples > 0 ? options.numSamples
                               : std::max<int64_t>(0, engine.getMixer().getSongEndSample() - options.startSample);
    const int64_t tailSamples =
        static_cast<int64_t>(std::llround(std::max(0.0, options.tailSeconds) * engine.getSampleRate()));
    const int64_t latency = options.compensateLatency ? engine.getLatencySamples() : 0;
    const int64_t outLength = songSamples + tailSamples;
    const int64_t total = outLength + latency;

    const bool wasPlaying = transport.isPlaying(); // the user's transport survives the bounce
    const bool wasLooping = transport.isLoopEnabled();
    const int64_t wasAt = transport.getPosition();

    transport.stop();
    transport.setLoopEnabled(false);
    transport.setPosition(options.startSample);
    engine.requestReset(); // tails, meters and ramps start from zero

    AudioBuffer work(kNumChannels, static_cast<int>(total));
    transport.play();
    int64_t done = 0;
    while (done < total) {
        if (done >= songSamples)
            transport.stop(); // the tail: clips are silent, inserts keep running
        int64_t len = std::min<int64_t>(blockSize, total - done);
        if (done < songSamples)
            len = std::min(len, songSamples - done); // stop exactly at the song end
        engine.processBlock(work.toBlock().getSubBlock(static_cast<int>(done), static_cast<int>(len)));
        engine.collectGarbage();
        done += len;
    }
    transport.stop();
    transport.setLoopEnabled(wasLooping);
    transport.setPosition(wasAt);
    transport.setPlaying(wasPlaying);

    AudioBuffer out(kNumChannels, static_cast<int>(outLength)); // drop the first `latency` samples
    for (int ch = 0; ch < kNumChannels; ++ch)
        out.copyFrom(ch, 0, work, ch, static_cast<int>(latency), static_cast<int>(outLength));
    return out;
}

} // namespace minidaw

#include "../check.h"
