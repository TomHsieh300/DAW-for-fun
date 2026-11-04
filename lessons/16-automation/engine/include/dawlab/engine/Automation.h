#pragma once
// Automation: Steps 1-4 of chapter 16 in the engine (Step 5).
//
//   AutomationCurve     points on the timeline, read as one value or as a block (Step 1). Immutable
//                       once the audio thread has it: an edit makes a new curve (Step 2).
//   CurveCursor         where the last block ended; a hint that checks itself (Step 2).
//   AutomationLane      one value per sample: the curve or the hand, limited in speed (Steps 2, 3).
//                       Lives in what it automates: a ChannelStrip has one for its fader and one for
//                       its pan, an InsertEffect one per parameter. So it leaves with its owner.
//   AutomationRecorder  UI thread: events from the audio thread -> passes -> thinned points (Step 3).
//   thin(), punch()     Step 3.
//
// What the snapshots left open, decided here:
//   - The speed limit comes from the sample rate. A curve may move the full range in 2 ms: that only
//     takes the click out of a jump. The hand gets 50 ms, like a fader without automation, because
//     the UI sends its values in coarse steps (a few dozen a second).
//   - An effect's parameters are not limited: they take one value per block, like a knob, and a value
//     between two choices would mean nothing.
//   - One CaptureQueue for the whole engine, an SpscQueue; each event says which lane it is from.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "Commands.h"
#include "Constants.h"
#include "SpscQueue.h"
#include "Transport.h"

namespace dawlab::engine {

// ---- the curve (Step 1) -------------------------------------------------------------------------

enum class Shape {
    Linear, // a straight line to the next point
    Hold,   // the value stays until the next point
};

struct AutomationPoint {
    int64_t position = 0; // timeline sample
    float value = 0.0f;   // normalized, 0 .. 1
    Shape shape = Shape::Linear;

    friend bool operator==(const AutomationPoint& a, const AutomationPoint& b) noexcept {
        return a.position == b.position && a.value == b.value && a.shape == b.shape;
    }
};

class AutomationCurve;

// Step 2. A hint: locate() checks it against the position and the curve in hand, so a seek, a loop or
// a swapped curve cannot make it wrong.
struct CurveCursor {
    int segment = -2; // -1: before the first point; -2: no hint yet
    int locate(const AutomationCurve& curve, int64_t position) noexcept;
};

class AutomationCurve {
public:
    AutomationCurve() = default;
    explicit AutomationCurve(float defaultValue) : default_(clamp01(defaultValue)) {}

    // After every point at this position, so a second point there makes a jump. Returns its index.
    int add(AutomationPoint point) {
        point.value = clamp01(point.value);
        const auto at = std::upper_bound(points_.begin(), points_.end(), point.position,
                                         [](int64_t p, const AutomationPoint& a) { return p < a.position; });
        const auto inserted = points_.insert(at, point);
        return static_cast<int>(inserted - points_.begin());
    }
    bool remove(int index) {
        if (index < 0 || index >= size())
            return false;
        points_.erase(points_.begin() + index);
        return true;
    }
    float valueAt(int64_t position) const noexcept {
        if (points_.empty())
            return default_;
        const int i = segmentAt(position);
        if (i < 0)
            return points_.front().value;
        if (i == size() - 1)
            return points_.back().value;
        return between(i, position);
    }
    // out[i] = valueAt(start + i), without a search if the cursor is right.
    void render(int64_t start, float* out, int n, CurveCursor& cursor) const noexcept {
        if (n <= 0)
            return;
        if (points_.empty()) {
            std::fill_n(out, n, default_);
            return;
        }
        int i = cursor.locate(*this, start);
        const int last = size() - 1;
        int64_t position = start;
        for (int k = 0; k < n; ++k, ++position) {
            while (i < last && points_[static_cast<size_t>(i) + 1].position <= position)
                ++i;
            out[k] = i < 0 ? points_.front().value : (i == last ? points_.back().value : between(i, position));
        }
        cursor.segment = i;
    }

    const std::vector<AutomationPoint>& points() const noexcept { return points_; }
    int size() const noexcept { return static_cast<int>(points_.size()); }
    float defaultValue() const noexcept { return default_; }

    int segmentAt(int64_t position) const noexcept {
        const auto after = std::upper_bound(points_.begin(), points_.end(), position,
                                            [](int64_t p, const AutomationPoint& a) { return p < a.position; });
        return static_cast<int>(after - points_.begin()) - 1;
    }
    static float clamp01(float v) noexcept { return v >= 0.0f ? (v <= 1.0f ? v : 1.0f) : 0.0f; }

    friend bool operator==(const AutomationCurve& a, const AutomationCurve& b) {
        return a.default_ == b.default_ && a.points_ == b.points_;
    }

private:
    float between(int i, int64_t position) const noexcept {
        const AutomationPoint& a = points_[static_cast<size_t>(i)];
        const AutomationPoint& b = points_[static_cast<size_t>(i) + 1];
        if (a.shape == Shape::Hold || b.position == a.position)
            return a.value;
        const double t = static_cast<double>(position - a.position) / static_cast<double>(b.position - a.position);
        return static_cast<float>(a.value + (static_cast<double>(b.value) - a.value) * t);
    }

    std::vector<AutomationPoint> points_; // sorted by position; equal positions in the order added
    float default_ = 0.0f;
};

inline int CurveCursor::locate(const AutomationCurve& curve, int64_t position) noexcept {
    const auto& points = curve.points();
    const int last = curve.size() - 1;
    const int i = segment;
    if (i >= -1 && i <= last && (i < 0 || points[static_cast<size_t>(i)].position <= position) &&
        (i == last || points[static_cast<size_t>(i) + 1].position > position))
        return i;
    segment = curve.segmentAt(position);
    return segment;
}

// ---- thinning and punching (Step 3) ----------------------------------------------------------------

// The points to keep, so that no dropped point is further than `tolerance` (in value, at its
// position) from the line through the kept ones. Positions must be strictly increasing.
inline std::vector<AutomationPoint> thin(const std::vector<AutomationPoint>& points, float tolerance) {
    const std::size_t count = points.size();
    if (count <= 2)
        return points;
    std::vector<char> keep(count, 0);
    keep[0] = keep[count - 1] = 1;
    std::vector<std::pair<std::size_t, std::size_t>> todo{{0, count - 1}};
    while (!todo.empty()) {
        const auto [first, last] = todo.back();
        todo.pop_back();
        const AutomationPoint& a = points[first];
        const AutomationPoint& b = points[last];
        double worst = tolerance;
        std::size_t worstAt = 0;
        for (std::size_t i = first + 1; i < last; ++i) {
            const double t = static_cast<double>(points[i].position - a.position) / static_cast<double>(b.position - a.position);
            const double error = std::abs(points[i].value - (a.value + (static_cast<double>(b.value) - a.value) * t));
            if (error > worst) {
                worst = error;
                worstAt = i;
            }
        }
        if (worstAt != 0) {
            keep[worstAt] = 1;
            todo.push_back({first, worstAt});
            todo.push_back({worstAt, last});
        }
    }
    std::vector<AutomationPoint> kept;
    for (std::size_t i = 0; i < count; ++i)
        if (keep[i])
            kept.push_back(points[i]);
    return kept;
}

// `old` with `pass` (at least two points) in place of everything from the pass's first position to
// its last, with a jump at both ends if needed. Outside that range the curve does not change.
inline AutomationCurve punch(const AutomationCurve& old, const std::vector<AutomationPoint>& pass) {
    if (pass.size() < 2)
        return old;
    const int64_t from = pass.front().position;
    const int64_t to = pass.back().position;
    const auto& points = old.points();
    const auto atFrom = std::lower_bound(points.begin(), points.end(), from,
                                         [](const AutomationPoint& a, int64_t p) { return a.position < p; });
    const float before = atFrom != points.end() && atFrom->position == from ? atFrom->value : old.valueAt(from);
    const int segment = old.segmentAt(to);
    const Shape shapeAfter = segment >= 0 ? points[static_cast<size_t>(segment)].shape : Shape::Linear;
    const float after = old.valueAt(to);

    AutomationCurve result(old.defaultValue());
    for (const AutomationPoint& p : points)
        if (p.position < from)
            result.add(p);
    result.add({from, before, Shape::Linear});
    for (const AutomationPoint& p : pass)
        result.add(p);
    result.add({to, after, shapeAfter});
    for (const AutomationPoint& p : points)
        if (p.position > to)
            result.add(p);
    return result;
}

// ---- targets and capture ---------------------------------------------------------------------------

enum class AutomationMode { Read, Touch, Latch, Write }; // Step 3

// A strip's own parameters (LaneTarget::parameter when there is no effect).
enum : int { kLaneGain = 0, kLanePan = 1, kNumStripLanes = 2 };

// What the engine calls a lane (Step 4 names the same thing for the project file). `parameter` is an
// index: the UI turns a name into one with InsertEffect::findParameter().
struct LaneTarget {
    ChannelRef channel;
    EffectId effect;   // none: the strip's own controls
    int parameter = 0; // kLaneGain, kLanePan, or the effect's parameter

    friend bool operator==(const LaneTarget& a, const LaneTarget& b) noexcept {
        return a.channel == b.channel && a.effect == b.effect && a.parameter == b.parameter;
    }
};

// From `position`, for numSamples samples, the user's value was `value`. numSamples == 0: the
// recording of this lane ends.
struct CaptureEvent {
    LaneTarget target;
    int64_t position = 0;
    int numSamples = 0;
    float value = 0.0f;
};

using CaptureQueue = SpscQueue<CaptureEvent>;

// Where the lanes of one strip are on the timeline in this block. A signal that passed `shift`
// samples of latency is that much older than the transport's position: a fader behind a lookahead
// limiter must read the curve that much earlier, or it moves before the audio it belongs to arrives.
struct LaneBlock {
    const TimelineSegment* segments = nullptr;
    int numSegments = 0;   // 0: stopped
    int64_t playhead = 0;
    int shift = 0;
    CaptureQueue* capture = nullptr;
};

// ---- the lane (Steps 2 and 3) -------------------------------------------------------------------------

class AutomationLane {
public:
    static constexpr double kCurveRampSeconds = 0.002; // full range; takes the click out of a jump
    static constexpr double kHandRampSeconds = 0.05;   // full range; the UI's values come in coarse steps

    AutomationLane() = default;
    ~AutomationLane() { delete curve_.load(std::memory_order_relaxed); }
    AutomationLane(const AutomationLane&) = delete;
    AutomationLane& operator=(const AutomationLane&) = delete;

    // Non-audio thread, while the lane is not playing.
    void prepare(double sampleRate, bool unlimited = false) noexcept {
        curveStep_ = unlimited ? 1.0f : stepFor(sampleRate, kCurveRampSeconds);
        handStep_ = unlimited ? 1.0f : stepFor(sampleRate, kHandRampSeconds);
    }

    // Audio thread (the ReplaceCurve command; setup code). Returns the old curve: the caller hands it
    // to the UI thread, like a clip list.
    const AutomationCurve* exchangeCurve(const AutomationCurve* next) noexcept {
        return curve_.exchange(next, std::memory_order_acq_rel);
    }
    // Any thread may ask whether there is a curve. To read one, see Engine::setLaneCurve.
    const AutomationCurve* getCurve() const noexcept { return curve_.load(std::memory_order_acquire); }

    // ---- the user (any thread) --------------------------------------------------------------------
    void setMode(AutomationMode mode) noexcept { mode_.store(mode, std::memory_order_relaxed); }
    AutomationMode getMode() const noexcept { return mode_.load(std::memory_order_relaxed); }
    void setKnobValue(float value) noexcept { knob_.store(AutomationCurve::clamp01(value), std::memory_order_relaxed); }
    float getKnobValue() const noexcept { return knob_.load(std::memory_order_relaxed); }
    void touch() noexcept {
        touchCount_.fetch_add(1, std::memory_order_relaxed);
        touching_.store(true, std::memory_order_relaxed);
    }
    void release() noexcept { touching_.store(false, std::memory_order_relaxed); }
    // What the lane last played: what a fader on screen shows.
    float getShownValue() const noexcept { return shown_.load(std::memory_order_relaxed); }

    // Does this lane do anything? Without a curve and in Read mode the parameter simply is the knob.
    bool isActive() const noexcept { return getCurve() != nullptr || getMode() != AutomationMode::Read; }

    // ---- audio thread -----------------------------------------------------------------------------
    // Clears the memory of the last block: the next value is taken as it is, not ramped to.
    void resetPlayback() noexcept { hasLast_ = false; }

    // One value per sample, and the events of the hand while it wins.
    void render(const LaneBlock& block, const LaneTarget& target, float* out, int n) noexcept {
        if (n <= 0)
            return;
        const bool playing = block.numSegments > 0;
        const bool user = userWins(playing);
        const float knob = getKnobValue();
        const AutomationCurve* curve = getCurve();
        const bool hand = user || curve == nullptr;
        if (hand) {
            std::fill_n(out, n, knob);
        } else if (!playing) {
            std::fill_n(out, n, curve->valueAt(block.playhead));
        } else {
            for (int s = 0; s < block.numSegments; ++s) {
                const TimelineSegment& segment = block.segments[s];
                curve->render(segment.timelineStart - block.shift, out + segment.blockOffset, segment.numSamples, cursor_);
            }
        }
        const bool capture = user && playing;
        sendEndMarkerIfOwed(capture, block, target, knob);
        if (capture && block.capture != nullptr)
            for (int s = 0; s < block.numSegments; ++s)
                sendEvent(block, target, block.segments[s].timelineStart - block.shift, block.segments[s].numSamples, knob);
        limit(out, n, hand ? handStep_ : curveStep_);
        shown_.store(last_, std::memory_order_relaxed);
    }

    // Instead of render() while !isActive(): the parameter is at `value`, so a curve that arrives next
    // block ramps from there. Costs a few loads: there may be hundreds of idle lanes.
    void idle(float value, const LaneBlock& block, const LaneTarget& target) noexcept {
        seenTouches_ = touchCount_.load(std::memory_order_relaxed);
        latched_ = false;
        last_ = value;
        hasLast_ = true;
        shown_.store(value, std::memory_order_relaxed);
        if (capturing_ || endOwed_)
            sendEndMarkerIfOwed(false, block, target, value);
    }

private:
    static float stepFor(double sampleRate, double seconds) noexcept {
        return sampleRate > 0.0 ? std::min(1.0f, static_cast<float>(1.0 / (sampleRate * seconds))) : 1.0f;
    }

    bool userWins(bool playing) noexcept {
        const bool touching = touching_.load(std::memory_order_relaxed);
        const unsigned touches = touchCount_.load(std::memory_order_relaxed);
        const bool touchedSinceLastBlock = touches != seenTouches_;
        seenTouches_ = touches;
        const AutomationMode mode = getMode();
        if (!playing || mode != AutomationMode::Latch)
            latched_ = false;
        if (!playing)
            return touching;
        switch (mode) {
        case AutomationMode::Read: return false;
        case AutomationMode::Touch: return touching;
        case AutomationMode::Latch:
            latched_ = latched_ || touching || touchedSinceLastBlock;
            return latched_;
        case AutomationMode::Write: return true;
        }
        return false;
    }

    // Before this block's events, which may already start a new pass.
    void sendEndMarkerIfOwed(bool capture, const LaneBlock& block, const LaneTarget& target, float value) noexcept {
        if (capturing_ && !capture)
            endOwed_ = true;
        capturing_ = capture;
        if (endOwed_ && (block.capture == nullptr || block.capture->push(CaptureEvent{target, block.playhead, 0, value})))
            endOwed_ = false;
    }
    static void sendEvent(const LaneBlock& block, const LaneTarget& target, int64_t position, int numSamples, float value) noexcept {
        block.capture->push(CaptureEvent{target, position, numSamples, value}); // full: lost, which ends the pass
    }

    // |out[i] - out[i-1]| <= step, also across blocks. What is within the step stays EXACTLY what it was.
    void limit(float* out, int n, float step) noexcept {
        float value = hasLast_ ? last_ : out[0];
        for (int i = 0; i < n; ++i) {
            const float wanted = out[i];
            if (wanted - value > step)
                value += step;
            else if (value - wanted > step)
                value -= step;
            else
                value = wanted;
            out[i] = value;
        }
        last_ = value;
        hasLast_ = true;
    }

    std::atomic<const AutomationCurve*> curve_{nullptr};
    std::atomic<float> knob_{0.0f};
    std::atomic<float> shown_{0.0f};
    std::atomic<AutomationMode> mode_{AutomationMode::Read};
    std::atomic<bool> touching_{false};
    std::atomic<unsigned> touchCount_{0};
    float curveStep_ = 1.0f, handStep_ = 1.0f; // prepare()
    // audio thread only
    CurveCursor cursor_;
    float last_ = 0.0f;
    bool hasLast_ = false;
    unsigned seenTouches_ = 0;
    bool latched_ = false;
    bool capturing_ = false;
    bool endOwed_ = false;
};

// ---- the recorder (Step 3) ---------------------------------------------------------------------------

// UI thread. Groups the events of all lanes into passes: an event that does not continue where its
// lane's last one ended, and an end marker, finish the pass.
class AutomationRecorder {
public:
    explicit AutomationRecorder(float tolerance = 0.002f) : tolerance_(tolerance) {}

    // Empties the queue. Calls onPass(target, thinned points) for each finished pass; punching it into
    // the UI's copy of the curve and publishing that is the caller's job. Returns the number of passes.
    template <typename OnPass>
    int collect(CaptureQueue& queue, OnPass&& onPass) {
        int finished = 0;
        CaptureEvent e;
        while (queue.pop(e)) {
            const std::size_t i = find(e.target);
            const bool open = i < open_.size();
            if (open && (e.numSamples == 0 || e.position != open_[i].end)) {
                finish(i, onPass);
                ++finished;
            }
            if (e.numSamples == 0)
                continue;
            const std::size_t at = find(e.target);
            if (at == open_.size())
                open_.push_back(Open{e.target, {}, 0, 0.0f});
            Open& pass = open_[at];
            pass.points.push_back({e.position, e.value, Shape::Linear});
            pass.end = e.position + e.numSamples;
            pass.last = e.value;
        }
        return finished;
    }
    bool isRecording(const LaneTarget& target) const noexcept { return find(target) < open_.size(); }

private:
    struct Open {
        LaneTarget target;
        std::vector<AutomationPoint> points;
        int64_t end = 0;
        float last = 0.0f;
    };
    std::size_t find(const LaneTarget& target) const noexcept {
        std::size_t i = 0;
        while (i < open_.size() && !(open_[i].target == target))
            ++i;
        return i;
    }
    template <typename OnPass>
    void finish(std::size_t i, OnPass& onPass) {
        Open pass = std::move(open_[i]);
        open_.erase(open_.begin() + static_cast<std::ptrdiff_t>(i));
        pass.points.push_back({pass.end, pass.last, Shape::Linear});
        onPass(pass.target, thin(pass.points, tolerance_));
    }

    float tolerance_;
    std::vector<Open> open_;
};

} // namespace dawlab::engine
