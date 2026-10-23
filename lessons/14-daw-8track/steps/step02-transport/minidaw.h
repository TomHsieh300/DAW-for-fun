#pragma once
// minidaw.h - Step 2 snapshot of the build-along DAW: Step 1 + a transport.
//
// New in Step 2:
//   - Transport: play / stop / seek / loop / tempo / time signature, bars:beats <-> samples,
//     and advance(), which tells the engine which timeline ranges ("segments") a block covers.
//   - Engine::processChunk() asks the transport for segments and plays the "tape" (still one
//     source buffer, placed at timeline sample 0) at those timeline positions. Stopped = silence.
//
// Still missing: clips (Step 3), mixer (Step 4), effects (Step 5), sends (Step 6), command
// queue (Step 7), PDC (Step 8).
//
// YOUR TURN (Step 2): Transport::barsBeatsToSamples(), Transport::samplesToBarsBeats() and
// Transport::advance() in starter/step02.cpp. They are only declared here.

#include <dawlab/AudioBuffer.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>

namespace minidaw {

using dawlab::AudioBlock;
using dawlab::AudioBuffer;

constexpr int kNumChannels = 2; // the engine is stereo

// ================================================================================================
// Transport (Step 2)
// ================================================================================================

struct TimeSignature {
    int numerator = 4;   // beats per bar
    int denominator = 4; // note value of one beat
};

// Musical position, 1-based like every DAW display: "bar 3, beat 2, tick 480".
struct BarsBeats {
    int bar = 1;
    int beat = 1;
    int tick = 0; // 0 .. Transport::kTicksPerBeat - 1
};

// "Samples [blockOffset, blockOffset + numSamples) of this block are the timeline positions
// [timelineStart, timelineStart + numSamples)."
struct TimelineSegment {
    int64_t timelineStart = 0;
    int blockOffset = 0;
    int numSamples = 0;
};

// Every setter writes an atomic, so a UI thread may call it while the audio thread runs.
// A seek is "pending" until advance() - the audio thread, at a block start - picks it up.
class Transport {
public:
    static constexpr int kTicksPerBeat = 960;
    static constexpr int kMaxSegmentsPerBlock = 8;

    void prepare(double sampleRate) noexcept { sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0; }
    double getSampleRate() const noexcept { return sampleRate_; }

    void setTempo(double bpm) noexcept { tempo_.store(std::clamp(bpm, 20.0, 999.0), std::memory_order_relaxed); }
    double getTempo() const noexcept { return tempo_.load(std::memory_order_relaxed); }
    void setTimeSignature(int numerator, int denominator) noexcept {
        numerator_.store(std::clamp(numerator, 1, 32), std::memory_order_relaxed);
        denominator_.store(std::clamp(denominator, 1, 32), std::memory_order_relaxed);
    }
    TimeSignature getTimeSignature() const noexcept {
        return {numerator_.load(std::memory_order_relaxed), denominator_.load(std::memory_order_relaxed)};
    }

    void play() noexcept { playing_.store(true, std::memory_order_relaxed); }
    void stop() noexcept { playing_.store(false, std::memory_order_relaxed); }
    void setPlaying(bool p) noexcept { playing_.store(p, std::memory_order_relaxed); }
    bool isPlaying() const noexcept { return playing_.load(std::memory_order_relaxed); }

    // Seek (any thread): applied by advance() at the start of the next block.
    void setPosition(int64_t samples) noexcept {
        pendingSeek_.store(std::max<int64_t>(0, samples), std::memory_order_release);
    }
    // A pending seek is what a UI should display.
    int64_t getPosition() const noexcept {
        const int64_t pending = pendingSeek_.load(std::memory_order_acquire);
        return pending >= 0 ? pending : position_.load(std::memory_order_relaxed);
    }

    // The loop is active when enabled AND end > start.
    void setLoopRange(int64_t start, int64_t end) noexcept {
        loopStart_.store(std::max<int64_t>(0, start), std::memory_order_relaxed);
        loopEnd_.store(std::max<int64_t>(0, end), std::memory_order_relaxed);
    }
    void setLoopEnabled(bool enabled) noexcept { loopEnabled_.store(enabled, std::memory_order_relaxed); }
    bool isLoopEnabled() const noexcept { return loopEnabled_.load(std::memory_order_relaxed); }
    int64_t getLoopStart() const noexcept { return loopStart_.load(std::memory_order_relaxed); }
    int64_t getLoopEnd() const noexcept { return loopEnd_.load(std::memory_order_relaxed); }

    // samplesPerBeat = sampleRate * 60 / bpm,   samplesPerBar = samplesPerBeat * numerator
    double getSamplesPerBeat() const noexcept { return sampleRate_ * 60.0 / getTempo(); }
    double getSamplesPerBar() const noexcept { return getSamplesPerBeat() * getTimeSignature().numerator; }

    // bar / beat 1-based:  beats = (bar-1)*numerator + (beat-1) + tick/960,
    //                      samples = round(beats * samplesPerBeat)
    int64_t barsBeatsToSamples(const BarsBeats& p) const noexcept; // YOUR TURN
    // The inverse: floor of the beat count (plus a 1e-9 beat epsilon so that exact grid
    // positions do not become "tick 959 of the previous beat").
    BarsBeats samplesToBarsBeats(int64_t samples) const noexcept; // YOUR TURN

    // Audio thread, once per block: applies a pending seek; if playing, describes up to
    // numSamples samples as segments (split at the loop end), advances the position and returns
    // the number of segments. samplesCovered = how many of the numSamples were described (less
    // than numSamples only when a tiny loop needs more than kMaxSegmentsPerBlock segments).
    // Stopped: returns 0 and samplesCovered = numSamples.
    int advance(int numSamples, TimelineSegment* segments, int& samplesCovered) noexcept; // YOUR TURN

private:
    double sampleRate_ = 48000.0;
    std::atomic<double> tempo_{120.0};
    std::atomic<int> numerator_{4};
    std::atomic<int> denominator_{4};
    std::atomic<bool> playing_{false};
    std::atomic<int64_t> position_{0};
    std::atomic<int64_t> pendingSeek_{-1};
    std::atomic<bool> loopEnabled_{false};
    std::atomic<int64_t> loopStart_{0};
    std::atomic<int64_t> loopEnd_{0};
};

// ================================================================================================
// Engine (Step 1, now driven by the transport)
// ================================================================================================

class Engine {
public:
    void prepare(double sampleRate, int maxBlockSize) {
        sampleRate_ = sampleRate;
        maxBlockSize_ = std::max(1, maxBlockSize);
        transport_.prepare(sampleRate);
        chunksProcessed_ = 0;
        prepared_ = true;
    }

    // Setup only: the "tape", placed at timeline sample 0. Step 3 replaces it with clips.
    void setSource(std::shared_ptr<const AudioBuffer> source) { source_ = std::move(source); }

    Transport& getTransport() noexcept { return transport_; }
    const Transport& getTransport() const noexcept { return transport_; }

    void processBlock(AudioBlock out) noexcept {
        if (!prepared_) {
            out.clear();
            return;
        }
        const int total = out.getNumSamples();
        int done = 0;
        while (done < total) {
            const int len = std::min(maxBlockSize_, total - done);
            processChunk(out.getSubBlock(done, len));
            done += len;
        }
    }

    bool isPrepared() const noexcept { return prepared_; }
    double getSampleRate() const noexcept { return sampleRate_; }
    int getMaxBlockSize() const noexcept { return maxBlockSize_; }
    int64_t getChunksProcessed() const noexcept { return chunksProcessed_; }

private:
    void processChunk(AudioBlock out) noexcept {
        out.clear();
        // Usually one pass; only a loop shorter than chunk / kMaxSegmentsPerBlock needs more.
        const int n = out.getNumSamples();
        int done = 0;
        while (done < n) {
            TimelineSegment segments[Transport::kMaxSegmentsPerBlock];
            int covered = 0;
            const int numSegments = transport_.advance(n - done, segments, covered);
            if (covered <= 0)
                covered = n - done; // never loop forever, whatever advance() says
            const AudioBlock part = out.getSubBlock(done, covered);
            for (int s = 0; s < numSegments; ++s)
                playTape(segments[s].timelineStart, part.getSubBlock(segments[s].blockOffset, segments[s].numSamples));
            done += covered;
        }
        ++chunksProcessed_;
    }

    // Writes tape samples [timelineStart, timelineStart + dest length) into dest.
    void playTape(int64_t timelineStart, AudioBlock dest) noexcept {
        if (!source_ || source_->getNumChannels() <= 0)
            return;
        const int64_t srcLen = source_->getNumSamples();
        for (int ch = 0; ch < dest.getNumChannels(); ++ch) {
            const float* s = source_->getReadPointer(std::min(ch, source_->getNumChannels() - 1));
            float* d = dest.getChannelPointer(ch);
            for (int i = 0; i < dest.getNumSamples(); ++i) {
                const int64_t si = timelineStart + i;
                if (si >= 0 && si < srcLen)
                    d[i] = s[si];
            }
        }
    }

    double sampleRate_ = 0.0;
    int maxBlockSize_ = 0;
    bool prepared_ = false;
    Transport transport_;
    std::shared_ptr<const AudioBuffer> source_;
    int64_t chunksProcessed_ = 0;
};

} // namespace minidaw
