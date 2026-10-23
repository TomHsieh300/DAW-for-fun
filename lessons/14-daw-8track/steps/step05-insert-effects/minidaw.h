#pragma once
// minidaw.h - Step 5 snapshot of the build-along DAW: Steps 1-4 + insert effects.
//
// New in Step 5:
//   - ParameterInfo: a self-description of one parameter (name, unit, range, default, kind,
//     taper), so a UI can generate knobs and a project file can store values by name.
//   - InsertEffect: the one interface every effect speaks; EffectBase: lock-free parameter
//     hand-over (an atomic per parameter + a dirty bit mask); two adapters (Gain, Compressor)
//     and a factory createEffect("Compressor").
//   - InsertChain: up to 8 effects per strip, processed in order; tracks and master run it.
//   - Engine::addInsertNow(): prepare an effect and put it into a strip (setup only - Step 7
//     turns this into a lock-free command).
//
// Still missing: sends and buses (Step 6), command queue (Step 7), PDC and effect latency
// (Step 8).
//
// YOUR TURN (Step 5): ParameterInfo::clamp(), toNormalized(), fromNormalized(),
// EffectBase::setParameter(), EffectBase::applyPendingParameters() and InsertChain::process()
// in starter/step05.cpp. They are only declared here.

#include <dawlab/AudioBuffer.h>
#include <dawlab/DspMath.h>
#include <dawlab/dsp/Compressor.h>
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
// Insert effects (Step 5)
// ================================================================================================
//
// One interface for every effect, plus a self-description of its parameters, so that a mixer,
// a UI and a project file can use any effect without knowing what a "Compressor" is.

enum class ParameterKind { Float, Int, Bool, Choice };

struct ParameterInfo {
    const char* name = "";
    const char* unit = "";
    float min = 0.0f;
    float max = 1.0f;
    float defaultValue = 0.0f;
    ParameterKind kind = ParameterKind::Float;
    bool logarithmic = false; // UI hint: frequency- or time-like, use a log taper

    // NaN -> defaultValue; then clamp to [min, max]; Int / Bool / Choice are rounded.
    float clamp(float v) const noexcept; // YOUR TURN
    // A knob position 0..1 and back:
    //   linear taper  n = (v - min) / (max - min)            v = min + n (max - min)
    //   log taper     n = ln(v / min) / ln(max / min)        v = min (max / min)^n   (only if min > 0)
    // Both clamp their result (v through clamp(), n to [0, 1]).
    float toNormalized(float v) const noexcept;   // YOUR TURN
    float fromNormalized(float n) const noexcept; // YOUR TURN
};

class InsertEffect {
public:
    virtual ~InsertEffect() = default;
    virtual const char* getName() const noexcept = 0;

    virtual void prepare(double sampleRate, int maxBlockSize, int numChannels) = 0; // may allocate
    virtual void reset() noexcept = 0;                                              // audio thread
    virtual void process(AudioBlock block) noexcept = 0;                            // in place

    virtual int getNumParameters() const noexcept = 0;
    virtual ParameterInfo getParameterInfo(int index) const noexcept = 0;
    virtual float getParameter(int index) const noexcept = 0;
    virtual void setParameter(int index, float value) noexcept = 0; // ANY thread, clamps

    virtual bool hasMeter() const noexcept { return false; } // e.g. gain reduction in dB
    virtual float getMeterDb() const noexcept { return 0.0f; }

    void setBypassed(bool b) noexcept { bypassed_.store(b, std::memory_order_relaxed); } // any thread
    bool isBypassed() const noexcept { return bypassed_.load(std::memory_order_relaxed); }

    void setParameterNormalized(int index, float n) noexcept { setParameter(index, getParameterInfo(index).fromNormalized(n)); }
    float getParameterNormalized(int index) const noexcept { return getParameterInfo(index).toNormalized(getParameter(index)); }

private:
    std::atomic<bool> bypassed_{false};
};

constexpr int kMaxEffectParameters = 32; // one bit each in EffectBase's dirty mask

// The boilerplate of every adapter: parameter storage and the lock-free hand-over to the DSP
// object. An adapter only writes a ParameterInfo table, applyParameter() (one switch that calls
// the DSP setter), prepare() (dsp.prepare + finishPrepare) and processDsp().
//
//   UI thread: setParameter(i, v)                 audio thread: process(block)
//     values_[i].store(clamp(v))                    applyPendingParameters():
//     dirty_.fetch_or(1 << i)   --- release --->      mask = dirty_.exchange(0)
//                                                     for each bit i: applyParameter(i, values_[i])
//                                                   processDsp(block)
class EffectBase : public InsertEffect {
public:
    int getNumParameters() const noexcept override { return numParameters_; }
    ParameterInfo getParameterInfo(int index) const noexcept override {
        return isValid(index) ? infos_[index] : ParameterInfo{};
    }
    float getParameter(int index) const noexcept override {
        return isValid(index) ? values_[static_cast<size_t>(index)].load(std::memory_order_relaxed) : 0.0f;
    }
    // Any thread: ignore invalid indices; store the CLAMPED value; set bit `index` in dirty_.
    void setParameter(int index, float value) noexcept override; // YOUR TURN

    // Audio thread: if prepared, take the whole dirty mask at once (exchange with 0) and call
    // applyParameter(i, values_[i]) for every set bit. Before prepare() do nothing (the bits stay
    // set and finishPrepare() applies everything).
    void applyPendingParameters() noexcept; // YOUR TURN

    void process(AudioBlock block) noexcept final {
        applyPendingParameters();
        processDsp(block);
    }

protected:
    EffectBase(const ParameterInfo* infos, int numParameters) noexcept
        : infos_(infos), numParameters_(std::min(numParameters, kMaxEffectParameters)) {
        for (int i = 0; i < numParameters_; ++i)
            values_[static_cast<size_t>(i)].store(infos_[i].defaultValue, std::memory_order_relaxed);
        dirty_.store(numParameters_ >= 32 ? 0xFFFFFFFFu : ((1u << numParameters_) - 1u), std::memory_order_relaxed);
    }
    // Call at the end of prepare(): pushes every value (also those set before prepare) into the DSP.
    void finishPrepare() noexcept {
        prepared_ = true;
        dirty_.store(0u, std::memory_order_relaxed);
        for (int i = 0; i < numParameters_; ++i)
            applyParameter(i, values_[static_cast<size_t>(i)].load(std::memory_order_relaxed));
    }
    virtual void applyParameter(int index, float value) noexcept = 0; // audio thread (or setup)
    virtual void processDsp(AudioBlock block) noexcept = 0;

    bool isValid(int index) const noexcept { return index >= 0 && index < numParameters_; }

    const ParameterInfo* infos_;
    int numParameters_;
    std::array<std::atomic<float>, kMaxEffectParameters> values_{};
    std::atomic<uint32_t> dirty_{0u};
    bool prepared_ = false;
};

// Gain: the simplest adapter (one parameter, no DSP class needed).
class GainEffect final : public EffectBase {
public:
    GainEffect() : EffectBase(kInfos, 1) {}
    const char* getName() const noexcept override { return "Gain"; }
    void prepare(double, int, int) override { finishPrepare(); }
    void reset() noexcept override {}

protected:
    void applyParameter(int index, float v) noexcept override {
        if (index == 0)
            gain_ = dawlab::dbToGain(v);
    }
    void processDsp(AudioBlock block) noexcept override { block.multiplyBy(gain_); }

private:
    static constexpr ParameterInfo kInfos[1] = {{"Gain", "dB", -60.0f, 24.0f, 0.0f, ParameterKind::Float, false}};
    float gain_ = 1.0f;
};

// Compressor: an adapter around chapter 04's dsp::Compressor - the pattern of all 19 adapters
// of the finished engine (engine/include/dawlab/engine/Effects.h).
class CompressorEffect final : public EffectBase {
public:
    enum Param { Threshold, Ratio, Attack, Release, Makeup, kCount };
    CompressorEffect() : EffectBase(kInfos, kCount) {}
    const char* getName() const noexcept override { return "Compressor"; }
    void prepare(double sampleRate, int maxBlockSize, int numChannels) override {
        dsp_.prepare(sampleRate, maxBlockSize, numChannels);
        finishPrepare();
    }
    void reset() noexcept override { dsp_.reset(); }
    bool hasMeter() const noexcept override { return true; }
    float getMeterDb() const noexcept override { return dsp_.getGainReductionDb(); }

protected:
    void applyParameter(int index, float v) noexcept override {
        switch (index) {
        case Threshold: dsp_.setThresholdDb(v); break;
        case Ratio: dsp_.setRatio(v); break;
        case Attack: dsp_.setAttackMs(v); break;
        case Release: dsp_.setReleaseMs(v); break;
        case Makeup: dsp_.setMakeupGainDb(v); break;
        default: break;
        }
    }
    void processDsp(AudioBlock block) noexcept override { dsp_.process(block); }

private:
    static constexpr ParameterInfo kInfos[kCount] = {
        {"Threshold", "dB", -60.0f, 0.0f, -18.0f, ParameterKind::Float, false},
        {"Ratio", ":1", 1.0f, 20.0f, 4.0f, ParameterKind::Float, false},
        {"Attack", "ms", 0.1f, 200.0f, 10.0f, ParameterKind::Float, true},
        {"Release", "ms", 5.0f, 2000.0f, 100.0f, ParameterKind::Float, true},
        {"Makeup", "dB", 0.0f, 24.0f, 0.0f, ParameterKind::Float, false},
    };
    dawlab::dsp::Compressor dsp_;
};

// The factory: a UI menu or a project file names effects; nullptr for an unknown name.
inline std::unique_ptr<InsertEffect> createEffect(const std::string& name) {
    if (name == "Gain")
        return std::make_unique<GainEffect>();
    if (name == "Compressor")
        return std::make_unique<CompressorEffect>();
    return nullptr;
}

constexpr int kMaxInserts = 8; // insert slots per strip

// A fixed array of up to kMaxInserts effects, processed in order; the chain owns them.
// Until Step 7 it is only changed while the engine is not processing.
class InsertChain {
public:
    InsertChain() = default;
    ~InsertChain() {
        for (InsertEffect* e : slots_)
            delete e;
    }
    InsertChain(const InsertChain&) = delete;
    InsertChain& operator=(const InsertChain&) = delete;

    // Runs every non-bypassed effect in slot order, in place.
    void process(AudioBlock block) noexcept; // YOUR TURN

    void reset() noexcept {
        for (int i = 0; i < count_; ++i)
            slots_[static_cast<size_t>(i)]->reset();
    }
    // Inserts at `slot` (-1 or past the end = append). Returns nullptr on success, or `effect`
    // itself when the chain is full (the caller keeps ownership).
    InsertEffect* insertAt(int slot, InsertEffect* effect) noexcept {
        if (effect == nullptr)
            return nullptr;
        if (count_ >= kMaxInserts)
            return effect;
        if (slot < 0 || slot > count_)
            slot = count_;
        for (int i = count_; i > slot; --i)
            slots_[static_cast<size_t>(i)] = slots_[static_cast<size_t>(i - 1)];
        slots_[static_cast<size_t>(slot)] = effect;
        ++count_;
        return nullptr;
    }
    // Removes and returns the effect at `slot` (nullptr if there is none); the caller owns it.
    InsertEffect* removeAt(int slot) noexcept {
        if (slot < 0 || slot >= count_)
            return nullptr;
        InsertEffect* removed = slots_[static_cast<size_t>(slot)];
        for (int i = slot; i + 1 < count_; ++i)
            slots_[static_cast<size_t>(i)] = slots_[static_cast<size_t>(i + 1)];
        slots_[static_cast<size_t>(--count_)] = nullptr;
        return removed;
    }
    int getNumInserts() const noexcept { return count_; }
    InsertEffect* getInsert(int slot) const noexcept {
        return (slot >= 0 && slot < count_) ? slots_[static_cast<size_t>(slot)] : nullptr;
    }

private:
    std::array<InsertEffect*, kMaxInserts> slots_{};
    int count_ = 0;
};

// ================================================================================================
// Channel strip (Steps 4-5)
// ================================================================================================
//
//   in --> [inserts] --> [mute/solo ramp] --> [fader x pan, one ramped gain per side] --> [meter] --> out
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
        inserts_.reset();
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
                                  float& right) noexcept { // Step 4
        float pl = 1.0f, pr = 1.0f;
        if (mode == PanMode::PanLaw) {
            dawlab::dsp::Panner::computeGains(pan, law, pl, pr);
        } else {
            const float p = std::clamp(pan, -1.0f, 1.0f);
            pl = std::min(1.0f, 1.0f - p);
            pr = std::min(1.0f, 1.0f + p);
        }
        const float g = gainDb > kMinusInfinityDb ? dawlab::dbToGain(gainDb) : 0.0f;
        left = g * pl;
        right = g * pr;
    }

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
    InsertChain& getInserts() noexcept { return inserts_; }
    const InsertChain& getInserts() const noexcept { return inserts_; }
    void processInserts(AudioBlock block) noexcept { inserts_.process(block); }

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
    InsertChain inserts_;
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
// Mixer (Steps 4-5): 8 tracks -> sum -> master -> out
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
    bool isTrackAudible(int t) const noexcept { // Step 4
        const Track& track = getTrack(t);
        return !track.isMuted() && (!isAnyTrackSoloed() || track.isSoloed());
    }

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
        master_.processInserts(sum);
        master_.applyMuteSolo(sum);
        master_.applyFaderAndPan(sum);
        master_.meter(sum);
        out.copyFrom(sum);
    }

private:
    // Track t: clips -> inserts (Step 5) -> mute/solo -> fader x pan -> meter -> sum.
    void processTrack(int t, AudioBlock tb, AudioBlock sum, const TimelineSegment* segments,
                      int numSegments) noexcept {
        Track& track = getTrack(t);
        track.renderClips(tb, segments, numSegments);
        track.beginBlock(isTrackAudible(t));
        track.processInserts(tb);
        track.applyMuteSolo(tb);
        track.applyFaderAndPan(tb);
        track.meter(tb);
        sum.addFrom(tb);
    }

    std::array<Track, kNumTracks> tracks_;
    ChannelStrip master_;
    AudioBuffer scratch_, sum_;
};

// ================================================================================================
// Engine (Steps 1-5)
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

    // Setup only (until Step 7): prepares `effect` for this engine and appends it to `strip`
    // (e.g. getMixer().getTrack(0) or getMixer().getMaster()). Returns an observer pointer, or
    // nullptr when the chain is full or the engine is not prepared.
    InsertEffect* addInsertNow(ChannelStrip& strip, std::unique_ptr<InsertEffect> effect) {
        if (!effect || !prepared_)
            return nullptr;
        effect->prepare(sampleRate_, maxBlockSize_, kNumChannels);
        InsertEffect* observer = effect.get();
        if (strip.getInserts().insertAt(-1, effect.get()) != nullptr)
            return nullptr; // chain full: the unique_ptr still owns it and deletes it
        effect.release();   // the chain owns it now
        return observer;
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
