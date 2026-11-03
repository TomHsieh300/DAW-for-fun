#pragma once
// minidaw.h - Step 1 snapshot of chapter 16: the automation curve.
//
// Automation is a parameter that changes over time: a fader that rises into the chorus, a filter
// that opens over eight bars. A DAW stores it as a list of points along the timeline and draws a
// line through them:
//
//   value
//   1.0 |            *-----------*                     * point
//       |           /             \                    / Linear: a straight line to the next point
//   0.5 |          /               *          *        - Hold: keep the value until the next point
//       |         /                 \        /
//   0.0 *--------*                   *------*
//       +------------------------------------------- position (samples)
//
//   - A point is a timeline POSITION in samples and a VALUE from 0 to 1, "normalized". The engine
//     already maps 0..1 onto every parameter's own range and taper (ParameterInfo::fromNormalized
//     in chapter 14), so one curve type serves a fader in dB, a frequency in Hz and a switch.
//   - Each point says how the curve goes on to the NEXT point: Linear or Hold.
//   - Before the first point the curve has the first point's value, after the last point the last
//     point's value. An empty curve has its default value (the parameter's own value).
//   - Two points at the same position make a JUMP: just before it the curve heads for the first
//     one; from that sample on it is the second one's value.
//
// Two ways to read it:
//   valueAt(position)          one value: what the UI draws and shows.
//   render(start, out, n)      the values of n consecutive samples: what the audio thread needs
//                              for one block. It must cost about n + log(points) steps, not n
//                              searches (Step 2 puts it on the audio thread).
//
// YOUR TURN (Step 1): write add(), remove(), valueAt() and render() in starter/step01.cpp.

#include <algorithm>
#include <cstdint>
#include <vector>

namespace minidaw {

enum class Shape {
    Linear, // a straight line to the next point
    Hold,   // the value stays until the next point
};

struct AutomationPoint {
    std::int64_t position = 0; // timeline sample
    float value = 0.0f;        // normalized, 0 .. 1
    Shape shape = Shape::Linear; // how the curve goes on to the next point
};

class AutomationCurve {
public:
    explicit AutomationCurve(float defaultValue = 0.0f) : default_(clamp01(defaultValue)) {}

    // ---- YOUR TURN (starter/step01.cpp) --------------------------------------------------------
    // Inserts the point (its value clamped to 0..1), keeping the points sorted by position. A
    // point at a position that already has points goes AFTER them, so adding two points at the
    // same position makes a jump. Returns the new point's index.
    int add(AutomationPoint point);
    // Removes point `index`; false if there is no such point.
    bool remove(int index);
    // The curve at `position` (see the top of this file).
    float valueAt(std::int64_t position) const noexcept;
    // out[i] = valueAt(start + i) for i in 0 .. n-1, in one pass: find the segment of `start`
    // once (a binary search), then walk forward. Must give the same values as valueAt().
    void render(std::int64_t start, float* out, int n) const noexcept;

    // ---- provided ---------------------------------------------------------------------------
    const std::vector<AutomationPoint>& points() const noexcept { return points_; }
    int size() const noexcept { return static_cast<int>(points_.size()); }
    float defaultValue() const noexcept { return default_; }

    // The index of the LAST point at or before `position`, or -1 if there is none. (The points at
    // one position count as "at" it, so a jump is taken at its position.)
    int segmentAt(std::int64_t position) const noexcept {
        const auto after = std::upper_bound(points_.begin(), points_.end(), position,
                                            [](std::int64_t p, const AutomationPoint& a) { return p < a.position; });
        return static_cast<int>(after - points_.begin()) - 1;
    }

    // The value between point i and point i + 1 at `position` (p_i.position <= position <
    // p_{i+1}.position). The one formula both valueAt() and render() use, so they agree exactly:
    //
    //   v = v_i + (v_{i+1} - v_i) * (position - x_i) / (x_{i+1} - x_i)       (Linear)
    //   v = v_i                                                              (Hold)
    float between(int i, std::int64_t position) const noexcept {
        const AutomationPoint& a = points_[static_cast<size_t>(i)];
        const AutomationPoint& b = points_[static_cast<size_t>(i) + 1];
        if (a.shape == Shape::Hold || b.position == a.position)
            return a.value;
        const double t = static_cast<double>(position - a.position) / static_cast<double>(b.position - a.position);
        return static_cast<float>(a.value + (static_cast<double>(b.value) - a.value) * t);
    }

    // 0 .. 1; NaN becomes 0 (every comparison with NaN is false, so test what IS allowed).
    static float clamp01(float v) noexcept { return v >= 0.0f ? (v <= 1.0f ? v : 1.0f) : 0.0f; }

private:
    std::vector<AutomationPoint> points_; // sorted by position; equal positions in the order added
    float default_;
};

} // namespace minidaw
