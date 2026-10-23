#pragma once
// minidaw.h - Step 1 snapshot of the build-along DAW: the smallest engine that works like a
// sound card callback.
//
// What exists after Step 1 (and nothing more):
//   - Engine::prepare(sampleRate, maxBlockSize)        setup, the only place that may allocate
//   - Engine::processBlock(AudioBlock out)             the audio callback, any block size
//   - one "tape": a source buffer that plays from position 0, then silence
//
// No transport (Step 2), no clips (Step 3), no mixer (Step 4), no effects (Step 5), no command
// queue (Step 7), no PDC (Step 8). The finished engine in engine/include/dawlab/engine/ keeps
// exactly this shape: processBlock() cuts the block into chunks, processChunk() does the work.
//
// YOUR TURN (Step 1): write Engine::processBlock() and Engine::processChunk() in
// starter/step01.cpp. They are only declared here.

#include <dawlab/AudioBuffer.h>

#include <algorithm>
#include <cstdint>
#include <memory>

namespace minidaw {

using dawlab::AudioBlock;
using dawlab::AudioBuffer;

constexpr int kNumChannels = 2; // the engine is stereo

class Engine {
public:
    // Setup thread, before the first processBlock(). Step 1 has nothing to allocate yet; later
    // steps allocate every scratch buffer here.
    void prepare(double sampleRate, int maxBlockSize) {
        sampleRate_ = sampleRate;
        maxBlockSize_ = std::max(1, maxBlockSize);
        position_ = 0;
        chunksProcessed_ = 0;
        prepared_ = true;
    }

    // Setup only (not while processBlock() runs): the audio the engine plays. A mono source
    // feeds both output channels. Step 3 replaces this "tape" with clips on tracks.
    void setSource(std::shared_ptr<const AudioBuffer> source) { source_ = std::move(source); }

    // ---- the audio callback --------------------------------------------------------------------
    // Fills `out` completely (stereo; a mono `out` gets channel 0). Accepts ANY block size:
    // blocks longer than maxBlockSize are cut into chunks of at most maxBlockSize samples, and
    // each chunk is handed to processChunk(). Before prepare() it outputs silence.
    void processBlock(AudioBlock out) noexcept; // YOUR TURN

    // ---- observers -------------------------------------------------------------------------------
    bool isPrepared() const noexcept { return prepared_; }
    double getSampleRate() const noexcept { return sampleRate_; }
    int getMaxBlockSize() const noexcept { return maxBlockSize_; }
    int64_t getPosition() const noexcept { return position_; }        // samples played so far
    int64_t getChunksProcessed() const noexcept { return chunksProcessed_; }

private:
    // One chunk (out.getNumSamples() <= maxBlockSize): copy the source samples
    // [position_, position_ + n) into `out` (silence beyond the end of the source), advance
    // position_ by n and count the chunk.
    void processChunk(AudioBlock out) noexcept; // YOUR TURN

    double sampleRate_ = 0.0;
    int maxBlockSize_ = 0;
    bool prepared_ = false;
    std::shared_ptr<const AudioBuffer> source_;
    int64_t position_ = 0;
    int64_t chunksProcessed_ = 0;
};

} // namespace minidaw
