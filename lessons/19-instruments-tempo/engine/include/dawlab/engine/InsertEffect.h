#pragma once
// InsertEffect: the one interface every effect in the DAW speaks, and InsertChain, the
// fixed-size list of inserts on a channel strip (Step 5).
//
// Why an interface? Chapters 01-12 gave us seventeen DSP classes, each with its own setters
// (setThresholdDb, setDelayMs, ...). A mixer, a UI, a project file and a plugin host cannot
// know all of them. So every effect is wrapped behind
//
//   - the standard lifecycle     prepare / reset / process
//   - a latency report           getLatencySamples()          (needed for PDC, Step 8)
//   - a parameter description    index -> name, unit, range, default, kind, choices
//                                so a UI can auto-generate knobs and a project file can store
//                                "Compressor.Threshold = -18" without knowing what it means.
//
// Threading contract (the stage-2 JUCE app relies on it):
//   prepare()                   non-audio thread, may allocate; never while the effect is in
//                               a running engine (Engine::addInsert prepares it for you).
//   reset(), process()          audio thread only.
//   getLatencySamples()         audio thread, or setup code before the effect is handed to a
//                               running engine. It may read DSP state that the audio thread
//                               changes (a limiter's lookahead), so a UI must not call it.
//   getReportedLatencySamples() any thread: the copy the audio thread published (atomic).
//   setParameter()              ANY thread. The value is stored in an atomic and handed to
//                               the DSP object by the audio thread at its next block.
//   getParameter(), getMeterDb() any thread (may be one block stale).
//   setBypassed(), isBypassed() any thread (atomic).

#include <dawlab/AudioBuffer.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "Constants.h"
#include "Transport.h"

#include "Automation.h"

#include <memory>

namespace dawlab::engine {

// What an insert may want to know about the timeline of the block it is about to process
// (tempo-synced LFOs, automation - Exercise 3). Set by the Mixer right before process().
// The segments describe the block exactly like Transport::advance() does; numSegments == 0
// means the transport is stopped. The pointer is only valid during that block.
struct PlayHead {
    const TimelineSegment* segments = nullptr;
    int numSegments = 0;
    double bpm = 120.0;

    bool isPlaying() const noexcept { return numSegments > 0; }
    int64_t blockStart() const noexcept { return numSegments > 0 ? segments[0].timelineStart : -1; }
};

enum class ParameterKind { Float, Int, Bool, Choice };

struct ParameterInfo {
    const char* name = "";
    const char* unit = "";
    float min = 0.0f;
    float max = 1.0f;
    float defaultValue = 0.0f;
    ParameterKind kind = ParameterKind::Float;
    bool logarithmic = false;              // UI hint: frequency/time-like, use a log taper
    const char* const* choices = nullptr;  // Bool / Choice: labels for the values min..max
    int numChoices = 0;

    // Clamp to [min, max]; Int / Bool / Choice values are rounded to whole numbers.
    float clamp(float v) const noexcept {
        if (!(v == v)) // NaN guard: a NaN would poison the DSP state forever
            v = defaultValue;
        v = std::clamp(v, min, max);
        if (kind != ParameterKind::Float)
            v = std::round(v);
        return v;
    }

    // Map a value to a 0..1 knob position, and back.
    //   linear taper:  n = (v - min) / (max - min)
    //   log taper:     n = ln(v / min) / ln(max / min)        (only when min > 0)
    float toNormalized(float v) const noexcept {
        v = clamp(v);
        if (max <= min)
            return 0.0f;
        if (logarithmic && min > 0.0f)
            return static_cast<float>(std::log(v / min) / std::log(max / min));
        return (v - min) / (max - min);
    }
    float fromNormalized(float n) const noexcept {
        n = std::clamp(n, 0.0f, 1.0f);
        if (logarithmic && min > 0.0f)
            return clamp(static_cast<float>(min * std::pow(max / min, n)));
        return clamp(min + n * (max - min));
    }
};

// "-18.0 dB", "Peak", "On", "4". UI thread helper (snprintf is not for the audio thread).
inline int formatParameterValue(const ParameterInfo& info, float value, char* buffer, int size) noexcept {
    if (buffer == nullptr || size <= 0)
        return 0;
    const float v = info.clamp(value);
    if ((info.kind == ParameterKind::Choice || info.kind == ParameterKind::Bool) && info.choices != nullptr) {
        const int idx = std::clamp(static_cast<int>(v - info.min), 0, std::max(0, info.numChoices - 1));
        return std::snprintf(buffer, static_cast<size_t>(size), "%s", info.choices[idx]);
    }
    // "4.00:1" for a ratio, "-18.0 dB" for everything else.
    const char* sep = (info.unit[0] != '\0' && info.unit[0] != ':') ? " " : "";
    if (info.kind == ParameterKind::Int)
        return std::snprintf(buffer, static_cast<size_t>(size), "%d%s%s", static_cast<int>(v), sep, info.unit);
    const char* fmt = std::abs(v) >= 100.0f ? "%.0f%s%s" : (std::abs(v) >= 10.0f ? "%.1f%s%s" : "%.2f%s%s");
    return std::snprintf(buffer, static_cast<size_t>(size), fmt, static_cast<double>(v), sep, info.unit);
}

class InsertEffect {
public:
    virtual ~InsertEffect() = default;

    virtual const char* getName() const noexcept = 0;

    virtual void prepare(double sampleRate, int maxBlockSize, int numChannels) = 0;
    virtual void reset() noexcept = 0;
    virtual void process(AudioBlock block) noexcept = 0;

    // Samples of delay this effect introduces (lookahead, oversampling filters, FFT
    // partitions...). Read by the audio thread every block for PDC, so it must be cheap.
    // Audio thread / setup only (see the threading contract); a UI reads
    // getReportedLatencySamples() instead.
    virtual int getLatencySamples() const noexcept { return 0; }

    // Any thread: getLatencySamples() as last published by publishLatency(). The audio thread
    // publishes every block (InsertChain::applyPendingParameters), the Engine right after it
    // prepared the effect, so the value is at most one block old.
    int getReportedLatencySamples() const noexcept { return reportedLatency_.load(std::memory_order_relaxed); }
    // Audio thread / setup: copy getLatencySamples() into the atomic read by the UI.
    void publishLatency() noexcept {
        reportedLatency_.store(std::max(0, getLatencySamples()), std::memory_order_relaxed);
    }

    // Audio thread, right before process(): where on the timeline this block is. Most
    // effects ignore it; override it for tempo sync or automation.
    virtual void setPlayHead(const PlayHead& playHead) noexcept { (void)playHead; }

    // Audio thread: push parameter values changed since the last call into the DSP object.
    // The Mixer calls it before asking for latencies, so a latency that depends on a
    // parameter (e.g. limiter lookahead) is up to date in the same block.
    virtual void applyPendingParameters() noexcept {}

    // ---- uniform parameter API ------------------------------------------------------------
    virtual int getNumParameters() const noexcept = 0;
    virtual ParameterInfo getParameterInfo(int index) const noexcept = 0;
    virtual float getParameter(int index) const noexcept = 0;
    virtual void setParameter(int index, float value) noexcept = 0; // clamps; any thread

    // Optional meter, e.g. gain reduction in dB (positive = reducing). 0 when hasMeter() is false.
    virtual bool hasMeter() const noexcept { return false; }
    virtual float getMeterDb() const noexcept { return 0.0f; }

    // ---- bypass (any thread) --------------------------------------------------------------
    void setBypassed(bool shouldBypass) noexcept { bypassed_.store(shouldBypass, std::memory_order_relaxed); }
    bool isBypassed() const noexcept { return bypassed_.load(std::memory_order_relaxed); }

    // ---- helpers built on the virtual API (any thread) ------------------------------------
    int findParameter(const char* name) const noexcept {
        if (name == nullptr)
            return -1;
        for (int i = 0; i < getNumParameters(); ++i)
            if (std::strcmp(getParameterInfo(i).name, name) == 0)
                return i;
        return -1;
    }
    bool setParameterByName(const char* name, float value) noexcept {
        const int i = findParameter(name);
        if (i < 0)
            return false;
        setParameter(i, value);
        return true;
    }
    float getParameterNormalized(int index) const noexcept {
        return getParameterInfo(index).toNormalized(getParameter(index));
    }
    void setParameterNormalized(int index, float normalized) noexcept {
        setParameter(index, getParameterInfo(index).fromNormalized(normalized));
    }
    void resetParametersToDefault() noexcept {
        for (int i = 0; i < getNumParameters(); ++i)
            setParameter(i, getParameterInfo(i).defaultValue);
    }

    // ---- identity and automation (chapter 16, Step 5) ------------------------------------------
    // Set before the effect reaches the audio thread (Engine::addInsert); constant afterwards.
    EffectId getEffectId() const noexcept { return effectId_; }
    void setEffectId(EffectId id) noexcept { effectId_ = id; }

    // Non-audio thread, while the effect is not playing: one lane per parameter, its knob at the
    // parameter's value. The ParameterInfo is kept here so that the audio thread never has to ask a
    // plugin for it.
    void prepareLanes(double sampleRate) {
        if (lanes_ == nullptr) {
            numLanes_ = getNumParameters();
            lanes_ = std::make_unique<ParameterLane[]>(static_cast<std::size_t>(numLanes_));
            for (int i = 0; i < numLanes_; ++i) {
                lanes_[static_cast<std::size_t>(i)].info = getParameterInfo(i);
                lanes_[static_cast<std::size_t>(i)].lane.setKnobValue(getParameterNormalized(i));
                lanes_[static_cast<std::size_t>(i)].lane.prepare(sampleRate, true);
            }
        }
        (void)sampleRate; // not limited: see applyLanes()
    }
    AutomationLane* getLane(int parameter) noexcept {
        return parameter >= 0 && parameter < numLanes_ ? &lanes_[static_cast<std::size_t>(parameter)].lane : nullptr;
    }

    // Audio thread, before process(). A parameter takes ONE value per block, the one at the block's
    // start: an effect has no way to take more. So its lane has no speed limit: to the effect this is
    // a knob that moved, which it already handles (or, for a switch, must simply obey). When a curve
    // goes away, the parameter returns to its knob.
    void applyLanes(const LaneBlock& block, ChannelRef channel, float* scratch, int n) noexcept {
        bool changed = false;
        for (int i = 0; i < numLanes_; ++i) {
            ParameterLane& l = lanes_[static_cast<std::size_t>(i)];
            const LaneTarget target{channel, effectId_, i};
            if (l.lane.isActive()) {
                l.lane.render(block, target, scratch, n);
                setParameter(i, l.info.fromNormalized(scratch[0]));
                l.wasActive = true;
                changed = true;
            } else {
                l.lane.idle(l.lane.getKnobValue(), block, target);
                if (l.wasActive) {
                    setParameter(i, l.info.fromNormalized(l.lane.getKnobValue()));
                    l.wasActive = false;
                    changed = true;
                }
            }
        }
        if (changed)
            applyPendingParameters();
    }
    void resetLanes() noexcept {
        for (int i = 0; i < numLanes_; ++i)
            lanes_[static_cast<std::size_t>(i)].lane.resetPlayback();
    }

private:
    struct ParameterLane {
        AutomationLane lane;
        ParameterInfo info;
        bool wasActive = false; // audio thread only
    };
    EffectId effectId_;
    std::unique_ptr<ParameterLane[]> lanes_;
    int numLanes_ = 0;
    std::atomic<bool> bypassed_{false};
    std::atomic<int> reportedLatency_{0};
};

// A fixed array of up to kMaxInserts effects, processed in order. The chain owns them.
//
// Structural changes (insertAt / removeAt / moveTo) are made only by the audio thread while
// it applies Engine commands; they just shuffle pointers, so they are real-time safe. Slots
// are atomics so that a UI thread may *look* at the chain at any time (getInsert) without a
// data race. A pointer obtained that way stays valid until the UI thread itself frees the
// removed effect in Engine::collectGarbage().
class InsertChain {
public:
    InsertChain() noexcept {
        for (auto& s : slots_)
            s.store(nullptr, std::memory_order_relaxed);
    }
    ~InsertChain() {
        for (auto& s : slots_)
            delete s.load(std::memory_order_relaxed);
    }
    InsertChain(const InsertChain&) = delete;
    InsertChain& operator=(const InsertChain&) = delete;

    // ---- audio thread ---------------------------------------------------------------------
    void reset() noexcept {
        for (int i = 0; i < size(); ++i) {
            at(i)->reset();
            at(i)->resetLanes();
        }
    }

    // Also publishes every effect's latency for the UI (InsertEffect::getReportedLatencySamples).
    void applyPendingParameters() noexcept {
        for (int i = 0; i < size(); ++i) {
            InsertEffect* e = at(i);
            e->applyPendingParameters();
            e->publishLatency();
        }
    }

    void process(AudioBlock block) noexcept {
        for (int i = 0; i < size(); ++i) {
            InsertEffect* e = at(i);
            if (!e->isBypassed())
                e->process(block);
        }
    }

    void applyLanes(const LaneBlock& block, ChannelRef channel, float* scratch, int n) noexcept {
        for (int i = 0; i < size(); ++i)
            at(i)->applyLanes(block, channel, scratch, n);
    }
    // The effect with this id, in any slot; nullptr if it is not on this chain. Any thread.
    InsertEffect* findById(EffectId id) const noexcept {
        for (int i = 0; i < size(); ++i) {
            InsertEffect* e = at(i); // a reader on another thread may see the last slot just emptied
            if (e != nullptr && e->getEffectId() == id)
                return e;
        }
        return nullptr;
    }

    void setPlayHead(const PlayHead& playHead) noexcept {
        for (int i = 0; i < size(); ++i)
            at(i)->setPlayHead(playHead);
    }

    // Sum of the latencies of all non-bypassed inserts (they are in series, so they add).
    int getLatencySamples() const noexcept {
        int total = 0;
        for (int i = 0; i < size(); ++i) {
            const InsertEffect* e = at(i);
            if (!e->isBypassed())
                total += std::max(0, e->getLatencySamples());
        }
        return total;
    }

    // Inserts `effect` at `slot` (-1 or >= size appends). Returns nullptr on success, or
    // `effect` itself if the chain is full (the caller keeps ownership).
    InsertEffect* insertAt(int slot, InsertEffect* effect) noexcept {
        const int n = size();
        if (effect == nullptr)
            return nullptr;
        if (n >= kMaxInserts)
            return effect;
        if (slot < 0 || slot > n)
            slot = n;
        for (int i = n; i > slot; --i)
            set(i, at(i - 1));
        set(slot, effect);
        count_.store(n + 1, std::memory_order_release);
        return nullptr;
    }

    // Removes and returns the effect at `slot` (nullptr if there is none).
    InsertEffect* removeAt(int slot) noexcept {
        const int n = size();
        if (slot < 0 || slot >= n)
            return nullptr;
        InsertEffect* removed = at(slot);
        count_.store(n - 1, std::memory_order_release); // shrink first: readers never see a hole
        for (int i = slot; i + 1 < n; ++i)
            set(i, at(i + 1));
        set(n - 1, nullptr);
        return removed;
    }

    // Puts `effect` into `slot` and returns the previous occupant (or `effect` itself when
    // the slot does not exist, so the caller can dispose of it).
    InsertEffect* replaceAt(int slot, InsertEffect* effect) noexcept {
        if (slot < 0 || slot >= size())
            return effect;
        InsertEffect* old = at(slot);
        set(slot, effect);
        return old;
    }

    // Moves the effect at `from` to position `to` (the others shift). False if out of range.
    bool moveTo(int from, int to) noexcept {
        const int n = size();
        if (from < 0 || from >= n || to < 0 || to >= n)
            return false;
        InsertEffect* e = at(from);
        if (from < to)
            for (int i = from; i < to; ++i)
                set(i, at(i + 1));
        else
            for (int i = from; i > to; --i)
                set(i, at(i - 1));
        set(to, e);
        return true;
    }

    // ---- any thread -------------------------------------------------------------------------
    int getNumInserts() const noexcept { return size(); }
    InsertEffect* getInsert(int slot) const noexcept {
        return (slot >= 0 && slot < kMaxInserts) ? slots_[static_cast<size_t>(slot)].load(std::memory_order_acquire)
                                                 : nullptr;
    }

private:
    int size() const noexcept { return count_.load(std::memory_order_acquire); }
    InsertEffect* at(int i) const noexcept { return slots_[static_cast<size_t>(i)].load(std::memory_order_relaxed); }
    void set(int i, InsertEffect* e) noexcept { slots_[static_cast<size_t>(i)].store(e, std::memory_order_release); }

    std::array<std::atomic<InsertEffect*>, kMaxInserts> slots_;
    std::atomic<int> count_{0};
};

} // namespace dawlab::engine
