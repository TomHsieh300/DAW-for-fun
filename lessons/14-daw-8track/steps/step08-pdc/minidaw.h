#pragma once
// minidaw.h - Step 8 snapshot of the build-along DAW: Steps 1-7 + plugin delay compensation.
//
// New in Step 8:
//   - InsertEffect::getLatencySamples(); InsertChain sums the latencies of its non-bypassed
//     effects; PureDelayEffect delays by N samples and reports N (the measuring instrument).
//   - LatencyCompensator: an integer delay line (ring buffer allocated in prepare()).
//   - Every strip runs inserts -> compensator; the dry track sum has its own compensator.
//   - Mixer::updateLatencies() computes all compensations once per chunk (Engine::processChunk:
//     applyCommands() -> updateLatencies() -> transport -> mixer), with a limit C per delay line.
//
// Still missing: offline render (Step 9).
//
// YOUR TURN (Step 8): LatencyCompensator::process() and Mixer::updateLatencies() in
// starter/step08.cpp. They are only declared here.

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
#include <type_traits>
#include <vector>

namespace minidaw {

using dawlab::AudioBlock;
using dawlab::AudioBuffer;

constexpr int kNumChannels = 2; // the engine is stereo
constexpr int kNumTracks = 8;   // audio tracks
constexpr int kNumBuses = 2;    // FX send/return buses (A, B)
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
    float clamp(float v) const noexcept { // Step 5
        if (!(v == v))
            v = defaultValue;
        v = std::clamp(v, min, max);
        if (kind != ParameterKind::Float)
            v = std::round(v);
        return v;
    }
    // A knob position 0..1 and back:
    //   linear taper  n = (v - min) / (max - min)            v = min + n (max - min)
    //   log taper     n = ln(v / min) / ln(max / min)        v = min (max / min)^n   (only if min > 0)
    // Both clamp their result (v through clamp(), n to [0, 1]).
    float toNormalized(float v) const noexcept { // Step 5
        v = clamp(v);
        if (max <= min)
            return 0.0f;
        if (logarithmic && min > 0.0f)
            return static_cast<float>(std::log(v / min) / std::log(max / min));
        return (v - min) / (max - min);
    }
    float fromNormalized(float n) const noexcept { // Step 5
        n = std::clamp(n, 0.0f, 1.0f);
        if (logarithmic && min > 0.0f)
            return clamp(static_cast<float>(min * std::pow(max / min, n)));
        return clamp(min + n * (max - min));
    }
};

class InsertEffect {
public:
    virtual ~InsertEffect() = default;
    virtual const char* getName() const noexcept = 0;

    virtual void prepare(double sampleRate, int maxBlockSize, int numChannels) = 0; // may allocate
    virtual void reset() noexcept = 0;                                              // audio thread
    virtual void process(AudioBlock block) noexcept = 0;                            // in place
    // Samples of delay this effect introduces (lookahead, oversampling, FFT partitions...).
    // Read by the audio thread every block for PDC (Step 8), so it must be cheap.
    virtual int getLatencySamples() const noexcept { return 0; }
    // Audio thread: hand parameter changes to the DSP before latencies are read (Step 8).
    virtual void applyPendingParameters() noexcept {}

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
    void setParameter(int index, float value) noexcept override { // Step 5
        if (!isValid(index))
            return;
        values_[static_cast<size_t>(index)].store(infos_[index].clamp(value), std::memory_order_relaxed);
        dirty_.fetch_or(1u << static_cast<unsigned>(index), std::memory_order_release);
    }

    // Audio thread: if prepared, take the whole dirty mask at once (exchange with 0) and call
    // applyParameter(i, values_[i]) for every set bit. Before prepare() do nothing (the bits stay
    // set and finishPrepare() applies everything).
    void applyPendingParameters() noexcept override { // Step 5
        if (!prepared_)
            return;
        uint32_t mask = dirty_.exchange(0u, std::memory_order_acquire);
        for (int i = 0; mask != 0u; ++i, mask >>= 1u)
            if (mask & 1u)
                applyParameter(i, values_[static_cast<size_t>(i)].load(std::memory_order_relaxed));
    }

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

// PureDelay: delays by N samples and reports N - the test instrument for PDC (Step 8). Its
// latency follows its parameter, which is why the Mixer applies pending parameters before it
// reads latencies.
class PureDelayEffect final : public EffectBase {
public:
    static constexpr int kMaxDelay = 16384;
    PureDelayEffect() : EffectBase(kInfos, 1) {}
    const char* getName() const noexcept override { return "PureDelay"; }
    void prepare(double, int, int numChannels) override {
        numChannels_ = std::max(1, numChannels);
        ring_.assign(static_cast<size_t>(numChannels_) * (kMaxDelay + 1), 0.0f); // allocate here
        write_ = 0;
        finishPrepare();
    }
    void reset() noexcept override {
        std::fill(ring_.begin(), ring_.end(), 0.0f);
        write_ = 0;
    }
    int getLatencySamples() const noexcept override { return delay_; }

protected:
    void applyParameter(int index, float v) noexcept override {
        if (index == 0)
            delay_ = std::clamp(static_cast<int>(v), 0, kMaxDelay);
    }
    void processDsp(AudioBlock block) noexcept override {
        constexpr int size = kMaxDelay + 1;
        int w = write_;
        for (int ch = 0; ch < std::min(numChannels_, block.getNumChannels()); ++ch) {
            float* ring = ring_.data() + static_cast<size_t>(ch) * size;
            float* x = block.getChannelPointer(ch);
            w = write_;
            for (int i = 0; i < block.getNumSamples(); ++i) {
                ring[w] = x[i];
                x[i] = ring[(w - delay_ + size) % size];
                w = (w + 1) % size;
            }
        }
        write_ = w;
    }

private:
    static constexpr ParameterInfo kInfos[1] = {
        {"Delay", "samples", 0.0f, static_cast<float>(kMaxDelay), 0.0f, ParameterKind::Int, false}};
    std::vector<float> ring_;
    int numChannels_ = 2;
    int write_ = 0;
    int delay_ = 0;
};

// The factory: a UI menu or a project file names effects; nullptr for an unknown name.
inline std::unique_ptr<InsertEffect> createEffect(const std::string& name) {
    if (name == "Gain")
        return std::make_unique<GainEffect>();
    if (name == "Compressor")
        return std::make_unique<CompressorEffect>();
    if (name == "PureDelay")
        return std::make_unique<PureDelayEffect>();
    return nullptr;
}

constexpr int kMaxInserts = 8; // insert slots per strip

// A fixed array of up to kMaxInserts effects, processed in order; the chain owns them.
// From Step 7 on only the audio thread changes it (while applying commands); the UI keeps the
// observer pointer Engine::addInsert() returned. (The finished engine also makes the slots
// atomics, so that a UI may look at a chain at any time.)
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
    void process(AudioBlock block) noexcept { // Step 5
        for (int i = 0; i < count_; ++i) {
            InsertEffect* e = slots_[static_cast<size_t>(i)];
            if (!e->isBypassed())
                e->process(block);
        }
    }

    void reset() noexcept {
        for (int i = 0; i < count_; ++i)
            slots_[static_cast<size_t>(i)]->reset();
    }
    void applyPendingParameters() noexcept {
        for (int i = 0; i < count_; ++i)
            slots_[static_cast<size_t>(i)]->applyPendingParameters();
    }
    // Effects in series add their delays; bypassed ones do not count (Step 8).
    int getLatencySamples() const noexcept {
        int total = 0;
        for (int i = 0; i < count_; ++i)
            if (!slots_[static_cast<size_t>(i)]->isBypassed())
                total += std::max(0, slots_[static_cast<size_t>(i)]->getLatencySamples());
        return total;
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
// Lock-free queues and commands (Step 7)
// ================================================================================================
//
// Values (fader, pan, parameters...) already travel through atomics. STRUCTURE (an effect added
// or removed, a new clip list) needs memory, so it travels as a Command through a wait-free
// single-producer / single-consumer queue: the UI thread allocates and prepares, the audio thread
// only links pointers in. Whatever the audio thread unlinks goes back through a second queue as
// Garbage and is deleted by the UI thread. The audio thread never allocates and never frees.

// Ring buffer of capacity + 1 slots (one stays empty so that "full" and "empty" differ):
//   head_ = next slot to pop  (written by the consumer only)
//   tail_ = next slot to push (written by the producer only)
//   empty: head_ == tail_          full: increment(tail_) == head_
// T must be trivially copyable (raw pointers, plain fields): slots are overwritten by assignment.
template <typename T>
class SpscQueue {
    static_assert(std::is_trivially_copyable_v<T>, "SpscQueue<T> requires a trivially copyable T");

public:
    explicit SpscQueue(std::size_t capacity) : slots_(capacity + 1) {} // allocate: UI thread
    SpscQueue(const SpscQueue&) = delete;
    SpscQueue& operator=(const SpscQueue&) = delete;

    // Producer: false when full (the item is NOT stored). Write the slot first, THEN publish it
    // with a release store of tail_; read head_ with acquire.
    bool push(const T& item) noexcept { // Step 7
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t next = increment(tail);
        if (next == head_.load(std::memory_order_acquire))
            return false;
        slots_[tail] = item;
        tail_.store(next, std::memory_order_release);
        return true;
    }
    // Consumer: false when empty. Read tail_ with acquire, copy the slot, THEN hand it back with
    // a release store of head_.
    bool pop(T& out) noexcept { // Step 7
        const std::size_t head = head_.load(std::memory_order_relaxed);
        if (head == tail_.load(std::memory_order_acquire))
            return false;
        out = slots_[head];
        head_.store(increment(head), std::memory_order_release);
        return true;
    }

    std::size_t size() const noexcept {
        const std::size_t head = head_.load(std::memory_order_acquire);
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        return tail >= head ? tail - head : slots_.size() - head + tail;
    }
    std::size_t capacity() const noexcept { return slots_.size() - 1; }

private:
    std::size_t increment(std::size_t i) const noexcept { return (i + 1 == slots_.size()) ? 0 : i + 1; }

    std::vector<T> slots_;
    std::atomic<std::size_t> head_{0};
    std::atomic<std::size_t> tail_{0};
    // (The finished engine pads head_ and tail_ onto separate cache lines; see SpscQueue.h.)
};

enum class ChannelKind : uint8_t { Track, Bus, Master };

struct ChannelRef {
    ChannelKind kind = ChannelKind::Track;
    int index = 0;
    static ChannelRef track(int i) noexcept { return {ChannelKind::Track, i}; }
    static ChannelRef bus(int i) noexcept { return {ChannelKind::Bus, i}; }
    static ChannelRef master() noexcept { return {ChannelKind::Master, 0}; }
};

enum class CommandType : uint8_t {
    None,
    AddInsert,    // channel, slot (-1 = append), effect   (ownership -> engine)
    RemoveInsert, // channel, slot                         (removed effect -> garbage)
    ReplaceClips, // channel (a track), clips              (old list -> garbage)
};

struct Command {
    CommandType type = CommandType::None;
    ChannelRef channel;
    int slot = -1;
    InsertEffect* effect = nullptr;
    std::vector<Clip>* clips = nullptr;
};

// "Please delete this on a non-audio thread": a void* plus a deleter that knows the real type
// (a capture-less lambda converts to a plain function pointer, so Garbage stays trivially copyable).
struct Garbage {
    void* ptr = nullptr;
    void (*deleter)(void*) = nullptr;

    template <typename T>
    static Garbage of(T* object) noexcept {
        Garbage g;
        g.ptr = object;
        g.deleter = [](void* p) { delete static_cast<T*>(p); };
        return g;
    }
    void destroy() noexcept {
        if (ptr != nullptr && deleter != nullptr)
            deleter(ptr);
        ptr = nullptr;
        deleter = nullptr;
    }
};

// ================================================================================================
// Latency compensation (Step 8)
// ================================================================================================
//
// An integer delay line: y[n] = x[n - d], 0 <= d <= maxDelay. Ring buffer of maxDelay + 1
// samples per channel, allocated in prepare(); setDelaySamples() only moves the read offset.

class LatencyCompensator {
public:
    void prepare(int maxDelaySamples, int numChannels) {
        maxDelay_ = std::max(0, maxDelaySamples);
        capacity_ = maxDelay_ + 1;
        numChannels_ = numChannels;
        ring_.assign(static_cast<size_t>(numChannels) * static_cast<size_t>(capacity_), 0.0f);
        reset();
    }
    void reset() noexcept {
        std::fill(ring_.begin(), ring_.end(), 0.0f);
        writeIndex_ = 0;
    }
    int getMaxDelaySamples() const noexcept { return maxDelay_; }
    int getDelaySamples() const noexcept { return delay_; }
    // Returns the delay actually applied (clamped to [0, maxDelay]).
    int setDelaySamples(int samples) noexcept {
        delay_ = std::clamp(samples, 0, maxDelay_);
        return delay_;
    }

    // In place, every channel: write x[i] into the ring at the write index, read the sample
    // written delay_ samples earlier (read index = write index - delay_, wrapped into
    // [0, capacity_)), advance the write index (wrap at capacity_). Write the ring ALSO when
    // delay_ is 0, so that a delay growing from 0 to D repeats the last D samples instead of
    // stale audio. writeIndex_ is shared by all channels: every channel starts at the same index.
    void process(AudioBlock block) noexcept; // YOUR TURN

private:
    std::vector<float> ring_; // channel c uses ring_[c * capacity_ .. (c + 1) * capacity_)
    int numChannels_ = 0;
    int capacity_ = 1;
    int maxDelay_ = 0;
    int delay_ = 0;
    int writeIndex_ = 0;
};

// ================================================================================================
// Channel strip (Steps 4-8)
// ================================================================================================
//
//   in --> [inserts] --> [PDC delay] --> [mute/solo ramp] --+--> pre-fader sends
//                                           |
//                                  [fader x pan, one ramped gain per side]
//                                           |
//                                           +--> post-fader sends
//                                           |
//                                       [meter] --> out
//
// Parameters are atomics: a UI thread may set them at any time; the audio thread reads them once
// per block in beginBlock() and turns them into smoother targets.

enum class PanMode { PanLaw, Balance };

class ChannelStrip {
public:
    static constexpr double kFaderRampSeconds = 0.05;
    static constexpr double kMuteRampSeconds = 0.01;

    ChannelStrip() noexcept {
        for (auto& d : sendDb_) // every send starts "off"
            d.store(kMinusInfinityDb, std::memory_order_relaxed);
        for (auto& p : sendPre_)
            p.store(false, std::memory_order_relaxed);
    }
    ChannelStrip(const ChannelStrip&) = delete;
    ChannelStrip& operator=(const ChannelStrip&) = delete;

    void prepare(double sampleRate, int maxPdcSamples) {
        compensator_.prepare(maxPdcSamples, kNumChannels);
        gainL_.reset(sampleRate, kFaderRampSeconds);
        gainR_.reset(sampleRate, kFaderRampSeconds);
        audible_.reset(sampleRate, kMuteRampSeconds);
        for (auto& g : sendGain_)
            g.reset(sampleRate, kFaderRampSeconds);
        reset();
    }
    void reset() noexcept {
        inserts_.reset();
        compensator_.reset();
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

    // Send level in dB per bus; kMinusInfinityDb (-100) = off (the default). Any thread.
    void setSendLevelDb(int bus, float db) noexcept {
        if (validBus(bus))
            sendDb_[static_cast<size_t>(bus)].store(std::clamp(db, kMinusInfinityDb, kMaxFaderDb), std::memory_order_relaxed);
    }
    float getSendLevelDb(int bus) const noexcept {
        return validBus(bus) ? sendDb_[static_cast<size_t>(bus)].load(std::memory_order_relaxed) : kMinusInfinityDb;
    }
    void setSendPreFader(int bus, bool pre) noexcept {
        if (validBus(bus))
            sendPre_[static_cast<size_t>(bus)].store(pre, std::memory_order_relaxed);
    }
    bool isSendPreFader(int bus) const noexcept {
        return validBus(bus) && sendPre_[static_cast<size_t>(bus)].load(std::memory_order_relaxed);
    }

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
        for (int b = 0; b < kNumBuses; ++b) {
            const float db = sendDb_[static_cast<size_t>(b)].load(std::memory_order_relaxed);
            sendGain_[static_cast<size_t>(b)].setTargetValue(db > kMinusInfinityDb ? dawlab::dbToGain(db) : 0.0f);
        }
        if (snapNext_) {
            snapNext_ = false;
            gainL_.setCurrentAndTargetValue(l);
            gainR_.setCurrentAndTargetValue(r);
            audible_.setCurrentAndTargetValue(audible ? 1.0f : 0.0f);
            for (auto& g : sendGain_)
                g.setCurrentAndTargetValue(g.getTargetValue());
        }
    }
    InsertChain& getInserts() noexcept { return inserts_; }
    const InsertChain& getInserts() const noexcept { return inserts_; }
    LatencyCompensator& getCompensator() noexcept { return compensator_; }
    const LatencyCompensator& getCompensator() const noexcept { return compensator_; }
    int getInsertLatencySamples() const noexcept { return inserts_.getLatencySamples(); }
    // Inserts in order, then the PDC delay (its amount is set by Mixer::updateLatencies()).
    void processInserts(AudioBlock block) noexcept {
        inserts_.process(block);
        compensator_.process(block);
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
    // dest += src * sendGain[bus], with the send gain ramped PER SAMPLE (sendGain_[bus] is a
    // dsp::SmoothedValue: call getNextValue() once per sample, also when the gain is 0, so the
    // ramp keeps moving). Ignore invalid bus numbers.
    void addSend(int bus, AudioBlock dest, const AudioBlock& src) noexcept { // Step 6
        if (!validBus(bus))
            return;
        dawlab::dsp::SmoothedValue& gain = sendGain_[static_cast<size_t>(bus)];
        const int n = std::min(dest.getNumSamples(), src.getNumSamples());
        const int nch = std::min(dest.getNumChannels(), src.getNumChannels());
        for (int i = 0; i < n; ++i) {
            const float g = gain.getNextValue();
            for (int ch = 0; ch < nch; ++ch)
                dest.getChannelPointer(ch)[i] += src.getChannelPointer(ch)[i] * g;
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
    LatencyCompensator compensator_;
    std::array<std::atomic<float>, kNumBuses> sendDb_;
    std::array<std::atomic<bool>, kNumBuses> sendPre_;
    std::array<dawlab::dsp::SmoothedValue, kNumBuses> sendGain_{};

    static bool validBus(int bus) noexcept { return bus >= 0 && bus < kNumBuses; }
};

// A track is a channel strip that reads clips. The clip list is an immutable vector owned
// through an atomic pointer: the UI builds a NEW list, the audio thread swaps the pointer
// (exchangeClips, one atomic store) and sends the old list back as garbage.
class Track : public ChannelStrip {
public:
    Track() = default;
    ~Track() { delete clips_.load(std::memory_order_relaxed); }

    // Audio thread (via a ReplaceClips command): returns the previous list for the garbage queue.
    const std::vector<Clip>* exchangeClips(const std::vector<Clip>* newClips) noexcept {
        return clips_.exchange(newClips, std::memory_order_acq_rel);
    }
    const std::vector<Clip>* getClips() const noexcept { return clips_.load(std::memory_order_acquire); }

    void renderClips(AudioBlock dest, const TimelineSegment* segments, int numSegments) noexcept {
        const std::vector<Clip>* clips = getClips();
        if (clips == nullptr)
            return;
        for (int s = 0; s < numSegments; ++s) {
            const TimelineSegment& seg = segments[s];
            const AudioBlock sub = dest.getSubBlock(seg.blockOffset, seg.numSamples);
            for (const Clip& clip : *clips)
                renderClip(clip, seg.timelineStart, sub);
        }
    }

private:
    std::atomic<const std::vector<Clip>*> clips_{nullptr};
};

// ================================================================================================
// Mixer (Steps 4-6)
//
//   track 1..8 --+-- pre/post-fader sends --> bus A, bus B --+
//                +--> track sum ---------------------------- +--> master --> out
// ================================================================================================

class Mixer {
public:
    Mixer() { // buses and master carry stereo: balance mode, centre = unity
        for (auto& b : buses_)
            b.setPanMode(PanMode::Balance);
        master_.setPanMode(PanMode::Balance);
    }
    Mixer(const Mixer&) = delete;
    Mixer& operator=(const Mixer&) = delete;

    void prepare(double sampleRate, int maxBlockSize, int maxPdcSamples) {
        const int n = std::max(1, maxBlockSize);
        for (auto& t : tracks_)
            t.prepare(sampleRate, maxPdcSamples);
        for (auto& b : buses_)
            b.prepare(sampleRate, maxPdcSamples);
        master_.prepare(sampleRate, maxPdcSamples);
        trackSumCompensator_.prepare(maxPdcSamples, kNumChannels);
        scratch_.setSize(kNumChannels, n); // every buffer is allocated here, never in process()
        sum_.setSize(kNumChannels, n);
        for (auto& b : busBuffers_)
            b.setSize(kNumChannels, n);
    }
    void reset() noexcept {
        for (auto& t : tracks_)
            t.reset();
        for (auto& b : buses_)
            b.reset();
        master_.reset();
        trackSumCompensator_.reset();
    }

    Track& getTrack(int i) noexcept { return tracks_[static_cast<size_t>(std::clamp(i, 0, kNumTracks - 1))]; }
    const Track& getTrack(int i) const noexcept {
        return tracks_[static_cast<size_t>(std::clamp(i, 0, kNumTracks - 1))];
    }
    ChannelStrip& getBus(int i) noexcept { return buses_[static_cast<size_t>(std::clamp(i, 0, kNumBuses - 1))]; }
    ChannelStrip& getMaster() noexcept { return master_; }
    // nullptr when the reference is out of range.
    ChannelStrip* getStrip(ChannelRef ref) noexcept {
        switch (ref.kind) {
        case ChannelKind::Track: return (ref.index >= 0 && ref.index < kNumTracks) ? &tracks_[static_cast<size_t>(ref.index)] : nullptr;
        case ChannelKind::Bus: return (ref.index >= 0 && ref.index < kNumBuses) ? &buses_[static_cast<size_t>(ref.index)] : nullptr;
        case ChannelKind::Master: return &master_;
        }
        return nullptr;
    }

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

    // ---- PDC (Step 8) -----------------------------------------------------------------------------
    void setLatencyCompensationEnabled(bool on) noexcept { pdcEnabled_.store(on, std::memory_order_relaxed); }
    bool isLatencyCompensationEnabled() const noexcept { return pdcEnabled_.load(std::memory_order_relaxed); }
    int getTotalLatencySamples() const noexcept { return totalLatency_.load(std::memory_order_relaxed); }
    bool hasPdcOverflow() const noexcept { return pdcOverflow_.load(std::memory_order_relaxed); }
    int getTrackSumCompensationSamples() const noexcept { return trackSumCompensator_.getDelaySamples(); }
    int getMaxPdcSamples() const noexcept { return trackSumCompensator_.getMaxDelaySamples(); } // C

    // Audio thread, once per chunk (also safe from setup code). First every chain (tracks, buses,
    // master) applies its pending parameters, so a latency that follows a parameter is current.
    // Then, with L_t / L_b / L_m the insert latencies of track t / bus b / master and C the limit:
    //   targetTrack = min(max_t L_t, C)          targetBus = min(max_b L_b, C)
    //   track t compensator = targetTrack - L_t  (setDelaySamples clamps negative values to 0)
    //   bus b compensator   = targetBus - L_b
    //   track-sum compensator = targetBus        (the dry sum waits for the slowest bus)
    //   pdcOverflow_  = enabled && (max_t L_t > C || max_b L_b > C)
    //   totalLatency_ = targetTrack + targetBus + L_m
    // With PDC disabled every compensator is 0, and so are totalLatency_ and pdcOverflow_.
    void updateLatencies() noexcept; // YOUR TURN

    // Audio thread: one block (n <= maxBlockSize); zero segments = stopped.
    void process(AudioBlock out, const TimelineSegment* segments, int numSegments) noexcept {
        const int n = out.getNumSamples();
        AudioBlock sum = sum_.toBlock().getSubBlock(0, n);
        sum.clear();
        std::array<AudioBlock, kNumBuses> bus;
        for (int b = 0; b < kNumBuses; ++b) {
            bus[static_cast<size_t>(b)] = busBuffers_[static_cast<size_t>(b)].toBlock().getSubBlock(0, n);
            bus[static_cast<size_t>(b)].clear();
        }
        AudioBlock tb = scratch_.toBlock().getSubBlock(0, n);
        for (int t = 0; t < kNumTracks; ++t) {
            tb.clear();
            processTrack(t, tb, sum, bus, segments, numSegments);
        }
        trackSumCompensator_.process(sum); // Step 8: the dry sum waits for the buses
        for (int b = 0; b < kNumBuses; ++b) { // after all tracks: the buses are complete
            ChannelStrip& strip = buses_[static_cast<size_t>(b)];
            const AudioBlock bb = bus[static_cast<size_t>(b)];
            strip.beginBlock(!strip.isMuted()); // solo-safe: only their own mute silences them
            strip.processInserts(bb);
            strip.applyMuteSolo(bb);
            strip.applyFaderAndPan(bb);
            strip.meter(bb);
            sum.addFrom(bb); // the return
        }
        master_.beginBlock(!master_.isMuted());
        master_.processInserts(sum);
        master_.applyMuteSolo(sum);
        master_.applyFaderAndPan(sum);
        master_.meter(sum);
        out.copyFrom(sum);
    }

private:
    // Track t: clips -> inserts -> mute/solo -> pre-fader sends -> fader x pan -> post-fader
    // sends -> meter -> sum (Step 6).
    void processTrack(int t, AudioBlock tb, AudioBlock sum, const std::array<AudioBlock, kNumBuses>& bus,
                      const TimelineSegment* segments, int numSegments) noexcept {
        Track& track = getTrack(t);
        track.renderClips(tb, segments, numSegments);
        track.beginBlock(isTrackAudible(t));
        track.processInserts(tb);
        track.applyMuteSolo(tb);
        for (int b = 0; b < kNumBuses; ++b)
            if (track.isSendPreFader(b))
                track.addSend(b, bus[static_cast<size_t>(b)], tb);
        track.applyFaderAndPan(tb);
        for (int b = 0; b < kNumBuses; ++b)
            if (!track.isSendPreFader(b))
                track.addSend(b, bus[static_cast<size_t>(b)], tb);
        track.meter(tb);
        sum.addFrom(tb);
    }

    std::array<Track, kNumTracks> tracks_;
    std::array<ChannelStrip, kNumBuses> buses_;
    ChannelStrip master_;
    AudioBuffer scratch_, sum_;
    std::array<AudioBuffer, kNumBuses> busBuffers_;
    LatencyCompensator trackSumCompensator_;
    std::atomic<bool> pdcEnabled_{true};
    std::atomic<int> totalLatency_{0};
    std::atomic<bool> pdcOverflow_{false};
};

// ================================================================================================
// Engine (Steps 1-8)
// ================================================================================================

class Engine {
public:
    static constexpr std::size_t kCommandQueueCapacity = 1024;

    explicit Engine(std::size_t queueCapacity = kCommandQueueCapacity)
        : commands_(queueCapacity), garbage_(queueCapacity) {}
    ~Engine() { // nothing processes any more: free the garbage and the payload of unapplied commands
        collectGarbage();
        Command c;
        while (commands_.pop(c))
            disposeNow(c);
    }
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    static constexpr int kDefaultMaxPdcSamples = 16384; // C: the longest compensating delay

    void prepare(double sampleRate, int maxBlockSize, int maxPdcSamples = kDefaultMaxPdcSamples) {
        sampleRate_ = sampleRate;
        maxBlockSize_ = std::max(1, maxBlockSize);
        transport_.prepare(sampleRate);
        mixer_.prepare(sampleRate, maxBlockSize_, maxPdcSamples);
        chunksProcessed_ = 0;
        prepared_.store(true, std::memory_order_release);
    }

    Transport& getTransport() noexcept { return transport_; }
    const Transport& getTransport() const noexcept { return transport_; }
    Mixer& getMixer() noexcept { return mixer_; }
    const Mixer& getMixer() const noexcept { return mixer_; }

    // ---- structural changes: UI thread (the single producer), also while audio runs ------------
    // false (and counted as dropped) when the queue is full; a payload is then deleted here.
    bool pushCommand(const Command& c) {
        if (commands_.push(c))
            return true;
        commandsDropped_.fetch_add(1, std::memory_order_relaxed);
        Command copy = c;
        disposeNow(copy);
        return false;
    }
    // Prepares the effect HERE (UI thread, may allocate), then queues it. Returns an observer
    // pointer, valid until the effect is removed AND collectGarbage() has run; nullptr on failure.
    InsertEffect* addInsert(ChannelRef channel, int slot, std::unique_ptr<InsertEffect> effect) {
        if (!effect || !isPrepared() || mixer_.getStrip(channel) == nullptr)
            return nullptr;
        effect->prepare(sampleRate_, maxBlockSize_, kNumChannels);
        InsertEffect* observer = effect.get();
        Command c;
        c.type = CommandType::AddInsert;
        c.channel = channel;
        c.slot = slot;
        c.effect = effect.release();
        return pushCommand(c) ? observer : nullptr;
    }
    bool removeInsert(ChannelRef channel, int slot) {
        Command c;
        c.type = CommandType::RemoveInsert;
        c.channel = channel;
        c.slot = slot;
        return pushCommand(c);
    }
    // The list is copied into a new vector here (UI thread); the audio thread swaps one pointer.
    bool setTrackClips(int track, std::vector<Clip> clips) {
        if (track < 0 || track >= kNumTracks)
            return false;
        Command c;
        c.type = CommandType::ReplaceClips;
        c.channel = ChannelRef::track(track);
        c.clips = new std::vector<Clip>(std::move(clips));
        return pushCommand(c);
    }
    // UI thread, regularly (~30 Hz) and after offline renders: deletes what the audio thread
    // handed back. Returns the number of objects freed.
    int collectGarbage() {
        int freed = 0;
        Garbage g;
        while (garbage_.pop(g)) {
            g.destroy();
            ++freed;
        }
        return freed;
    }

    // Total output latency of the mix (the PDC result) and the overflow flag. Any thread.
    int getLatencySamples() const noexcept { return mixer_.getTotalLatencySamples(); }
    bool hasPdcOverflow() const noexcept { return mixer_.hasPdcOverflow(); }

    uint64_t getCommandsApplied() const noexcept { return commandsApplied_.load(std::memory_order_relaxed); }
    uint64_t getCommandsDropped() const noexcept { return commandsDropped_.load(std::memory_order_relaxed); }
    uint64_t getGarbageLeaked() const noexcept { return garbageLeaked_.load(std::memory_order_relaxed); }

    // ---- audio thread ----------------------------------------------------------------------------
    void processBlock(AudioBlock out) noexcept {
        if (!isPrepared()) {
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

    bool isPrepared() const noexcept { return prepared_.load(std::memory_order_acquire); }
    double getSampleRate() const noexcept { return sampleRate_; }
    int getMaxBlockSize() const noexcept { return maxBlockSize_; }
    int64_t getChunksProcessed() const noexcept { return chunksProcessed_; }

private:
    void processChunk(AudioBlock out) noexcept {
        applyCommands();          // Step 7: structural changes, between two chunks
        mixer_.updateLatencies(); // Step 8: PDC for the chains as they are now
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

    // Audio thread: applies every pending command - pointer operations only (Step 7).
    void applyCommands() noexcept {
        Command c;
        while (commands_.pop(c)) {
            ChannelStrip* strip = mixer_.getStrip(c.channel);
            switch (c.type) {
            case CommandType::AddInsert: {
                InsertEffect* rejected = strip ? strip->getInserts().insertAt(c.slot, c.effect) : c.effect;
                if (rejected)
                    discard(Garbage::of(rejected));
                break;
            }
            case CommandType::RemoveInsert:
                if (strip)
                    if (InsertEffect* removed = strip->getInserts().removeAt(c.slot))
                        discard(Garbage::of(removed));
                break;
            case CommandType::ReplaceClips: {
                const std::vector<Clip>* old = c.clips;
                if (c.channel.kind == ChannelKind::Track && strip != nullptr)
                    old = static_cast<Track*>(strip)->exchangeClips(c.clips);
                if (old)
                    discard(Garbage::of(const_cast<std::vector<Clip>*>(old)));
                break;
            }
            case CommandType::None: break;
            }
            commandsApplied_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    // Audio thread: hand an object to the UI thread. If the garbage queue is full we may not
    // free it here, so it leaks and is counted (cannot happen while collectGarbage() runs).
    void discard(Garbage g) noexcept {
        if (!garbage_.push(g))
            garbageLeaked_.fetch_add(1, std::memory_order_relaxed);
    }
    // UI thread: a command that never reached the audio thread still owns its payload.
    static void disposeNow(Command& c) noexcept {
        if (c.type == CommandType::AddInsert)
            delete c.effect;
        else if (c.type == CommandType::ReplaceClips)
            delete c.clips;
        c.effect = nullptr;
        c.clips = nullptr;
    }

    double sampleRate_ = 0.0;
    int maxBlockSize_ = 0;
    std::atomic<bool> prepared_{false};
    Transport transport_;
    Mixer mixer_;
    SpscQueue<Command> commands_;
    SpscQueue<Garbage> garbage_;
    int64_t chunksProcessed_ = 0;
    std::atomic<uint64_t> commandsApplied_{0};
    std::atomic<uint64_t> commandsDropped_{0};
    std::atomic<uint64_t> garbageLeaked_{0};
};

} // namespace minidaw
