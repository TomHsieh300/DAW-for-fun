// Step 9 (starter): renderOffline().
//
// Build and run:  l14_step09_starter   (prints [FAIL] until the TODO is done)
// Reference:      ../solution/step09.cpp, docs/step-09-offline-render.md ("Your turn")
//
// Engine::applyPendingCommandsOffline(), requestReset(), Mixer::getSongEndSample(),
// RenderOptions and bounceToWav() are in ../minidaw.h.

#include "../minidaw.h"

namespace minidaw {

AudioBuffer renderOffline(Engine& engine, const RenderOptions& options) {
    // TODO 1: engine.applyPendingCommandsOffline(); compute songSamples (numSamples, or song end
    //         - startSample), tailSamples = llround(tailSeconds * sampleRate), latency
    //         (getLatencySamples() when compensateLatency), total = song + tail + latency.
    // TODO 2: remember position / loop / playing; stop, loop off, setPosition(startSample),
    //         requestReset(); play().
    // TODO 3: render `total` samples into a work buffer with processBlock() in blocks of
    //         options.blockSize - never across the song end (cut that block there), and stop
    //         the transport once the song part is done; collectGarbage() after every block.
    // TODO 4: restore the transport; return a buffer of song + tail samples copied from the
    //         work buffer starting at index `latency`.
    (void)engine;
    (void)options;
    return {};
}

} // namespace minidaw

#include "../check.h"
