#pragma once
// minidaw.h - Step 3 snapshot of the build-along DAW: Steps 1-2 + clips on 8 tracks.
//
// New in Step 3:
//   - Clip: a window into a shared, immutable AudioBuffer, placed on the timeline, with clip
//     gain and fades (renderClip() adds the part that overlaps a block, sample-accurately).
//   - Track: a list of clips; renderClips() renders them for every timeline segment.
//   - Engine: 8 tracks instead of the "tape". The output is the plain sum of the tracks - no
//     fader, no pan yet (Step 4).
//
// Still missing: mixer (Step 4), effects (Step 5), sends (Step 6), command queue (Step 7), PDC
// (Step 8). Until Step 7, setTrackClips() may only be called while processBlock() is not running.
//
// YOUR TURN (Step 3): clipFadeAt() and renderClip() in starter/step03.cpp. They are only
// declared here.

#include <dawlab/AudioBuffer.h>
#include <dawlab/DspMath.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace minidaw {

using dawlab::AudioBlock;
using dawlab::AudioBuffer;

constexpr int kNumChannels = 2; // the engine is stereo
constexpr int kNumTracks = 8;   // audio tracks

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
    int64_t barsBeatsToSamples(const BarsBeats& p) const noexcept { // Step 2
        const int beatsPerBar = getTimeSignature().numerator;
        const double beats = static_cast<double>(p.bar - 1) * beatsPerBar + static_cast<double>(p.beat - 1) +
                             static_cast<double>(p.tick) / kTicksPerBeat;
        return static_cast<int64_t>(std::llround(beats * getSamplesPerBeat()));
    }
    // The inverse: floor of the beat count (plus a 1e-9 beat epsilon so that exact grid
    // positions do not become "tick 959 of the previous beat").
    BarsBeats samplesToBarsBeats(int64_t samples) const noexcept { // Step 2
        const int beatsPerBar = getTimeSignature().numerator;
        const double totalBeats = static_cast<double>(std::max<int64_t>(0, samples)) / getSamplesPerBeat() + 1e-9;
        const auto wholeBeats = static_cast<int64_t>(std::floor(totalBeats));
        BarsBeats p;
        p.bar = static_cast<int>(wholeBeats / beatsPerBar) + 1;
        p.beat = static_cast<int>(wholeBeats % beatsPerBar) + 1;
        p.tick = std::min(kTicksPerBeat - 1,
                          static_cast<int>(std::floor((totalBeats - static_cast<double>(wholeBeats)) * kTicksPerBeat)));
        return p;
    }

    // Audio thread, once per block: applies a pending seek; if playing, describes up to
    // numSamples samples as segments (split at the loop end), advances the position and returns
    // the number of segments. samplesCovered = how many of the numSamples were described (less
    // than numSamples only when a tiny loop needs more than kMaxSegmentsPerBlock segments).
    // Stopped: returns 0 and samplesCovered = numSamples.
    int advance(int numSamples, TimelineSegment* segments, int& samplesCovered) noexcept { // Step 2
        const int64_t seek = pendingSeek_.exchange(-1, std::memory_order_acq_rel);
        if (seek >= 0)
            position_.store(seek, std::memory_order_relaxed);
        samplesCovered = std::max(0, numSamples);
        if (!isPlaying() || numSamples <= 0)
            return 0;
        int64_t pos = position_.load(std::memory_order_relaxed);
        const int64_t loopStart = getLoopStart();
        const int64_t loopEnd = getLoopEnd();
        const bool loopActive = isLoopEnabled() && loopEnd > loopStart;
        int count = 0;
        int offset = 0;
        while (offset < numSamples && count < kMaxSegmentsPerBlock) {
            int64_t n = numSamples - offset;
            if (loopActive && pos < loopEnd)
                n = std::min<int64_t>(n, loopEnd - pos); // stop exactly at the loop end
            segments[count++] = {pos, offset, static_cast<int>(n)};
            pos += n;
            offset += static_cast<int>(n);
            if (loopActive && pos == loopEnd)
                pos = loopStart; // wrap
        }
        samplesCovered = offset;
        position_.store(pos, std::memory_order_relaxed);
        return count;
    }

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
// Clips (Step 3)
// ================================================================================================
//
//   timeline:   0 ........ start ......................... start+length ........
//   source:               [sourceOffset ............. sourceOffset+length)

enum class FadeShape { Linear, EqualPower };

struct Clip {
    std::shared_ptr<const AudioBuffer> source; // immutable audio, shared (mono or stereo)
    int64_t start = 0;                         // timeline position of the first clip sample
    int64_t length = 0;                        // timeline samples covered
    int64_t sourceOffset = 0;                  // first source sample used
    float gainDb = 0.0f;                       // clip gain
    int fadeInSamples = 0;
    int fadeOutSamples = 0;
    FadeShape fadeShape = FadeShape::Linear;
    bool muted = false;
    std::string name; // UI only

    int64_t end() const noexcept { return start + length; }

    static Clip fromBuffer(std::shared_ptr<const AudioBuffer> buffer, int64_t startSample, std::string clipName = {}) {
        Clip c;
        c.length = buffer ? buffer->getNumSamples() : 0;
        c.source = std::move(buffer);
        c.start = startSample;
        c.name = std::move(clipName);
        return c;
    }
};

// shape(t) = t (Linear) or sin(t * pi / 2) (EqualPower), t in [0, 1]
inline float fadeGain(FadeShape shape, float t) noexcept {
    return shape == FadeShape::EqualPower ? std::sin(t * static_cast<float>(dawlab::kPi) * 0.5f) : t;
}

// Fade gain of clip sample i (0-based inside the clip), clip gain not included:
//   fadeIn(i)  = shape((i + 1) / (F_in + 1))    for i < F_in,  else 1
//   fadeOut(i) = shape((r + 1) / (F_out + 1))   r = length - 1 - i, for r < F_out, else 1
//   result     = fadeIn(i) * fadeOut(i)
float clipFadeAt(const Clip& clip, int64_t i) noexcept; // YOUR TURN

// Adds the part of `clip` that overlaps the timeline range
// [timelineStart, timelineStart + dest.getNumSamples()) into `dest` (+=, never =):
//   from = max(timelineStart, clip.start),  to = min(timelineStart + n, clip.end())
//   dest[T - timelineStart] += src[sourceOffset + T - clip.start] * clipGain * fade(T - clip.start)
// Muted clips and empty sources add nothing; a mono source feeds every channel; source indices
// outside the source buffer are silence. Real-time safe: no allocation.
void renderClip(const Clip& clip, int64_t timelineStart, AudioBlock dest) noexcept; // YOUR TURN

// A track owns a list of clips. Until Step 7 the list is a plain vector that is only replaced
// while the engine is not processing; Step 7 swaps it with a lock-free command.
class Track {
public:
    void setClips(std::vector<Clip> clips) { clips_ = std::move(clips); }
    const std::vector<Clip>& getClips() const noexcept { return clips_; }

    // Adds every clip that overlaps each timeline segment into `dest`.
    void renderClips(AudioBlock dest, const TimelineSegment* segments, int numSegments) noexcept {
        for (int s = 0; s < numSegments; ++s) {
            const TimelineSegment& seg = segments[s];
            const AudioBlock sub = dest.getSubBlock(seg.blockOffset, seg.numSamples);
            for (const Clip& clip : clips_)
                renderClip(clip, seg.timelineStart, sub);
        }
    }

private:
    std::vector<Clip> clips_;
};

// ================================================================================================
// Engine (Steps 1-3)
// ================================================================================================

class Engine {
public:
    void prepare(double sampleRate, int maxBlockSize) {
        sampleRate_ = sampleRate;
        maxBlockSize_ = std::max(1, maxBlockSize);
        transport_.prepare(sampleRate);
        trackBuffer_.setSize(kNumChannels, maxBlockSize_); // allocate here, never in processBlock()
        chunksProcessed_ = 0;
        prepared_ = true;
    }

    Transport& getTransport() noexcept { return transport_; }
    const Transport& getTransport() const noexcept { return transport_; }

    // Setup only (until Step 7): replaces the clips of track 0..7.
    bool setTrackClips(int track, std::vector<Clip> clips) {
        if (track < 0 || track >= kNumTracks)
            return false;
        tracks_[static_cast<size_t>(track)].setClips(std::move(clips));
        return true;
    }
    const Track& getTrack(int track) const noexcept {
        return tracks_[static_cast<size_t>(std::clamp(track, 0, kNumTracks - 1))];
    }

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
        const int n = out.getNumSamples();
        int done = 0;
        while (done < n) {
            TimelineSegment segments[Transport::kMaxSegmentsPerBlock];
            int covered = 0;
            const int numSegments = transport_.advance(n - done, segments, covered);
            if (covered <= 0)
                covered = n - done;
            mixTracks(out.getSubBlock(done, covered), segments, numSegments);
            done += covered;
        }
        ++chunksProcessed_;
    }

    // Step 3's "mixer": render each track into a scratch block and add it to the output.
    void mixTracks(AudioBlock out, const TimelineSegment* segments, int numSegments) noexcept {
        AudioBlock tb = trackBuffer_.toBlock().getSubBlock(0, out.getNumSamples());
        for (Track& track : tracks_) {
            tb.clear();
            track.renderClips(tb, segments, numSegments);
            out.addFrom(tb);
        }
    }

    double sampleRate_ = 0.0;
    int maxBlockSize_ = 0;
    bool prepared_ = false;
    Transport transport_;
    std::array<Track, kNumTracks> tracks_;
    AudioBuffer trackBuffer_;
    int64_t chunksProcessed_ = 0;
};

} // namespace minidaw
