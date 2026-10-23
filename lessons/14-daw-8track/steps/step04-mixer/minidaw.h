#pragma once
// minidaw.h - Step 4 snapshot of the build-along DAW: Steps 1-3 + the 8-track mixer.
//
// New in Step 4:
//   - ChannelStrip: fader (dB), pan (pan law or balance), mute, solo, a 50 ms fader ramp and a
//     10 ms mute/solo ramp (dsp::SmoothedValue, chapter 01), and a peak meter.
//   - Track = ChannelStrip + clips.  Mixer = 8 tracks + master; solo logic across all tracks.
//   - Engine::processChunk() hands every timeline range to Mixer::process().
//
// Still missing: effects (Step 5), sends and buses (Step 6), command queue (Step 7), PDC
// (Step 8). Until Step 7, setTrackClips() may only be called while processBlock() is not running.
//
// YOUR TURN (Step 4): ChannelStrip::computeStripGains(), Mixer::isTrackAudible() and
// Mixer::processTrack() in starter/step04.cpp. They are only declared here.

#include <dawlab/AudioBuffer.h>
#include <dawlab/DspMath.h>
#include <dawlab/dsp/Panner.h>
#include <dawlab/dsp/SmoothedValue.h>

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
using dawlab::kMinusInfinityDb;  // -100 dB: a fader at the bottom = silence
constexpr float kMaxFaderDb = 12.0f;

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
inline float clipFadeAt(const Clip& clip, int64_t i) noexcept { // Step 3
    float g = 1.0f;
    if (clip.fadeInSamples > 0 && i < clip.fadeInSamples)
        g *= fadeGain(clip.fadeShape, static_cast<float>(i + 1) / static_cast<float>(clip.fadeInSamples + 1));
    const int64_t r = clip.length - 1 - i;
    if (clip.fadeOutSamples > 0 && r < clip.fadeOutSamples)
        g *= fadeGain(clip.fadeShape, static_cast<float>(r + 1) / static_cast<float>(clip.fadeOutSamples + 1));
    return g;
}

// Adds the part of `clip` that overlaps the timeline range
// [timelineStart, timelineStart + dest.getNumSamples()) into `dest` (+=, never =):
//   from = max(timelineStart, clip.start),  to = min(timelineStart + n, clip.end())
//   dest[T - timelineStart] += src[sourceOffset + T - clip.start] * clipGain * fade(T - clip.start)
// Muted clips and empty sources add nothing; a mono source feeds every channel; source indices
// outside the source buffer are silence. Real-time safe: no allocation.
inline void renderClip(const Clip& clip, int64_t timelineStart, AudioBlock dest) noexcept { // Step 3
    if (clip.muted || !clip.source || clip.length <= 0 || clip.source->getNumChannels() <= 0)
        return;
    const AudioBuffer& src = *clip.source;
    const int64_t from = std::max(timelineStart, clip.start);
    const int64_t to = std::min(timelineStart + dest.getNumSamples(), clip.end());
    if (to <= from)
        return;
    const float gain = dawlab::dbToGain(clip.gainDb);
    const int n = static_cast<int>(to - from);
    const int destOffset = static_cast<int>(from - timelineStart);
    const int64_t clipIndex0 = from - clip.start;
    const int64_t srcIndex0 = clip.sourceOffset + clipIndex0;
    const int64_t srcLen = src.getNumSamples();
    for (int ch = 0; ch < dest.getNumChannels(); ++ch) {
        const float* sp = src.getReadPointer(std::min(ch, src.getNumChannels() - 1));
        float* d = dest.getChannelPointer(ch) + destOffset;
        for (int i = 0; i < n; ++i) {
            const int64_t si = srcIndex0 + i;
            if (si >= 0 && si < srcLen)
                d[i] += sp[si] * gain * clipFadeAt(clip, clipIndex0 + i);
        }
    }
}

// ================================================================================================
// Channel strip (Step 4)
// ================================================================================================
//
//   in --> [mute/solo ramp] --> [fader x pan, one ramped gain per side] --> [meter] --> out
//
// Parameters are atomics: a UI thread may set them at any time; the audio thread reads them once
// per block in beginBlock() and turns them into smoother targets.

enum class PanMode { PanLaw, Balance };

class ChannelStrip {
public:
    static constexpr double kFaderRampSeconds = 0.05;
    static constexpr double kMuteRampSeconds = 0.01;

    ChannelStrip() = default;
    ChannelStrip(const ChannelStrip&) = delete;
    ChannelStrip& operator=(const ChannelStrip&) = delete;

    void prepare(double sampleRate) {
        gainL_.reset(sampleRate, kFaderRampSeconds);
        gainR_.reset(sampleRate, kFaderRampSeconds);
        audible_.reset(sampleRate, kMuteRampSeconds);
        reset();
    }
    void reset() noexcept {
        maxPeak_.store(0.0f, std::memory_order_relaxed);
        snapNext_ = true; // the first block after a reset starts at the targets (no fade-in)
    }

    // ---- parameters (any thread) ----------------------------------------------------------------
    void setGainDb(float db) noexcept {
        gainDb_.store(std::clamp(db, kMinusInfinityDb, kMaxFaderDb), std::memory_order_relaxed);
    }
    float getGainDb() const noexcept { return gainDb_.load(std::memory_order_relaxed); }
    void setPan(float pan) noexcept { pan_.store(std::clamp(pan, -1.0f, 1.0f), std::memory_order_relaxed); }
    float getPan() const noexcept { return pan_.load(std::memory_order_relaxed); }
    void setPanLaw(dawlab::dsp::PanLaw law) noexcept { panLaw_.store(static_cast<int>(law), std::memory_order_relaxed); }
    dawlab::dsp::PanLaw getPanLaw() const noexcept {
        return static_cast<dawlab::dsp::PanLaw>(panLaw_.load(std::memory_order_relaxed));
    }
    void setPanMode(PanMode mode) noexcept { panMode_.store(static_cast<int>(mode), std::memory_order_relaxed); }
    PanMode getPanMode() const noexcept { return static_cast<PanMode>(panMode_.load(std::memory_order_relaxed)); }
    void setMute(bool mute) noexcept { mute_.store(mute, std::memory_order_relaxed); }
    bool isMuted() const noexcept { return mute_.load(std::memory_order_relaxed); }
    void setSolo(bool solo) noexcept { solo_.store(solo, std::memory_order_relaxed); } // tracks only
    bool isSoloed() const noexcept { return solo_.load(std::memory_order_relaxed); }

    // The per-side gains fader and pan settle at:
    //   PanMode::PanLaw   panL / panR from dsp::Panner::computeGains(pan, law, panL, panR)
    //   PanMode::Balance  panL = min(1, 1 - pan), panR = min(1, 1 + pan)   (centre = unity)
    //   g = dbToGain(gainDb), or 0 when gainDb <= kMinusInfinityDb
    //   left = g * panL, right = g * panR
    static void computeStripGains(float gainDb, float pan, dawlab::dsp::PanLaw law, PanMode mode, float& left,
                                  float& right) noexcept; // YOUR TURN

    // ---- audio thread -----------------------------------------------------------------------------
    // Atomics -> smoother targets, once per block. `audible` comes from the Mixer (mute/solo).
    void beginBlock(bool audible) noexcept {
        float l = 0.0f, r = 0.0f;
        computeStripGains(getGainDb(), getPan(), getPanLaw(), getPanMode(), l, r);
        gainL_.setTargetValue(l);
        gainR_.setTargetValue(r);
        audible_.setTargetValue(audible ? 1.0f : 0.0f);
        if (snapNext_) {
            snapNext_ = false;
            gainL_.setCurrentAndTargetValue(l);
            gainR_.setCurrentAndTargetValue(r);
            audible_.setCurrentAndTargetValue(audible ? 1.0f : 0.0f);
        }
    }
    // Mute / solo gain (10 ms ramp), the same on both channels.
    void applyMuteSolo(AudioBlock block) noexcept {
        for (int i = 0; i < block.getNumSamples(); ++i) {
            const float g = audible_.getNextValue();
            for (int ch = 0; ch < block.getNumChannels(); ++ch)
                block.getChannelPointer(ch)[i] *= g;
        }
    }
    // Fader x pan: one ramped gain per side (a mono block only gets the left one).
    void applyFaderAndPan(AudioBlock block) noexcept {
        for (int i = 0; i < block.getNumSamples(); ++i) {
            const float l = gainL_.getNextValue();
            const float r = gainR_.getNextValue();
            block.getChannelPointer(0)[i] *= l;
            if (block.getNumChannels() > 1)
                block.getChannelPointer(1)[i] *= r;
        }
    }
    // Highest |sample| since reset() (written by the audio thread, read by anyone).
    void meter(const AudioBlock& block) noexcept {
        float peak = maxPeak_.load(std::memory_order_relaxed);
        for (int ch = 0; ch < block.getNumChannels(); ++ch)
            for (int i = 0; i < block.getNumSamples(); ++i)
                peak = std::max(peak, std::abs(block.getChannelPointer(ch)[i]));
        maxPeak_.store(peak, std::memory_order_relaxed);
    }
    float getMaxPeakLinear() const noexcept { return maxPeak_.load(std::memory_order_relaxed); }

private:
    std::atomic<float> gainDb_{0.0f};
    std::atomic<float> pan_{0.0f};
    std::atomic<int> panLaw_{static_cast<int>(dawlab::dsp::PanLaw::ConstantPower)};
    std::atomic<int> panMode_{static_cast<int>(PanMode::PanLaw)};
    std::atomic<bool> mute_{false};
    std::atomic<bool> solo_{false};
    dawlab::dsp::SmoothedValue gainL_, gainR_, audible_;
    bool snapNext_ = true;
    std::atomic<float> maxPeak_{0.0f};
};

// A track is a channel strip that reads clips. Until Step 7 the clip list is a plain vector that
// is only replaced while the engine is not processing; Step 7 swaps it with a lock-free command.
class Track : public ChannelStrip {
public:
    void setClips(std::vector<Clip> clips) { clips_ = std::move(clips); }
    const std::vector<Clip>& getClips() const noexcept { return clips_; }

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
// Mixer (Step 4): 8 tracks -> sum -> master -> out
// ================================================================================================

class Mixer {
public:
    Mixer() { master_.setPanMode(PanMode::Balance); } // the master carries stereo: balance, centre = unity
    Mixer(const Mixer&) = delete;
    Mixer& operator=(const Mixer&) = delete;

    void prepare(double sampleRate, int maxBlockSize) {
        const int n = std::max(1, maxBlockSize);
        for (auto& t : tracks_)
            t.prepare(sampleRate);
        master_.prepare(sampleRate);
        scratch_.setSize(kNumChannels, n); // every buffer is allocated here, never in process()
        sum_.setSize(kNumChannels, n);
    }
    void reset() noexcept {
        for (auto& t : tracks_)
            t.reset();
        master_.reset();
    }

    Track& getTrack(int i) noexcept { return tracks_[static_cast<size_t>(std::clamp(i, 0, kNumTracks - 1))]; }
    const Track& getTrack(int i) const noexcept {
        return tracks_[static_cast<size_t>(std::clamp(i, 0, kNumTracks - 1))];
    }
    ChannelStrip& getMaster() noexcept { return master_; }

    bool isAnyTrackSoloed() const noexcept {
        for (const auto& t : tracks_)
            if (t.isSoloed())
                return true;
        return false;
    }
    // audible = !mute && (!anySolo || solo)   - mute wins over solo on the same track
    bool isTrackAudible(int t) const noexcept; // YOUR TURN

    // Audio thread: one block (n <= maxBlockSize); zero segments = stopped.
    void process(AudioBlock out, const TimelineSegment* segments, int numSegments) noexcept {
        const int n = out.getNumSamples();
        AudioBlock sum = sum_.toBlock().getSubBlock(0, n);
        sum.clear();
        AudioBlock tb = scratch_.toBlock().getSubBlock(0, n);
        for (int t = 0; t < kNumTracks; ++t) {
            tb.clear();
            processTrack(t, tb, sum, segments, numSegments);
        }
        master_.beginBlock(!master_.isMuted());
        master_.applyMuteSolo(sum);
        master_.applyFaderAndPan(sum);
        master_.meter(sum);
        out.copyFrom(sum);
    }

private:
    // Track t: render its clips into `tb` (cleared by the caller), then the strip stages in order
    // beginBlock(audible) -> applyMuteSolo -> applyFaderAndPan -> meter, and add `tb` into `sum`.
    void processTrack(int t, AudioBlock tb, AudioBlock sum, const TimelineSegment* segments,
                      int numSegments) noexcept; // YOUR TURN

    std::array<Track, kNumTracks> tracks_;
    ChannelStrip master_;
    AudioBuffer scratch_, sum_;
};

// ================================================================================================
// Engine (Steps 1-4)
// ================================================================================================

class Engine {
public:
    void prepare(double sampleRate, int maxBlockSize) {
        sampleRate_ = sampleRate;
        maxBlockSize_ = std::max(1, maxBlockSize);
        transport_.prepare(sampleRate);
        mixer_.prepare(sampleRate, maxBlockSize_);
        chunksProcessed_ = 0;
        prepared_ = true;
    }

    Transport& getTransport() noexcept { return transport_; }
    const Transport& getTransport() const noexcept { return transport_; }
    Mixer& getMixer() noexcept { return mixer_; }
    const Mixer& getMixer() const noexcept { return mixer_; }

    // Setup only (until Step 7): replaces the clips of track 0..7.
    bool setTrackClips(int track, std::vector<Clip> clips) {
        if (track < 0 || track >= kNumTracks)
            return false;
        mixer_.getTrack(track).setClips(std::move(clips));
        return true;
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
        const int n = out.getNumSamples();
        int done = 0;
        while (done < n) {
            TimelineSegment segments[Transport::kMaxSegmentsPerBlock];
            int covered = 0;
            const int numSegments = transport_.advance(n - done, segments, covered);
            if (covered <= 0)
                covered = n - done;
            mixer_.process(out.getSubBlock(done, covered), segments, numSegments);
            done += covered;
        }
        ++chunksProcessed_;
    }

    double sampleRate_ = 0.0;
    int maxBlockSize_ = 0;
    bool prepared_ = false;
    Transport transport_;
    Mixer mixer_;
    int64_t chunksProcessed_ = 0;
};

} // namespace minidaw
