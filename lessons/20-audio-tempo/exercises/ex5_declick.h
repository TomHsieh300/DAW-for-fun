#pragma once
// Exercise 5 - A fade over a jump.
//
// Step 3 computes, for every block, where in the file each song sample reads. Between two blocks those positions
// normally go on where the last block left off. When the tempo map changes while the song plays, the playhead
// keeps its musical place - its tick. A clip that follows by varispeed or stretch reads by the tick, so it goes on
// where it was, at a new speed. But a clip that does not follow, or follows with its start only, reads by the
// sample: the audio inside it jumps (Step 2 measured how far), and a wave that was at +0.5 goes on at -0.3 from
// one sample to the next. That step is a click. (A seek and a loop's wrap are jumps too.)
//
// The cure is a crossfade. For a few milliseconds after the jump, the OLD path - where the reading would have gone
// on, at the speed it had - fades out while the new one fades in. The two paths read different parts of the file:
// they are not correlated, so the fade keeps their POWER constant (chapter 14's equal-power curves).
//
//   Declicker(fadeSamples, thresholdSamples)
//   read(source, positions, out)   ADDS source read at positions[0 .. n) (readHermite) to out[0 .. n). It remembers
//                                  where the reading would go on after the call: the last position plus the last
//                                  speed (the step from the next-to-last position; 1 after a call of one sample).
//       A JUMP is a call whose first position is more than thresholdSamples from where it would have gone on. Then
//       a fade of fadeSamples output samples begins at the call's first sample: output sample k of the fade is
//           new(k) x sin(t pi/2) + old(k) x cos(t pi/2),   t = (k + 1) / (fadeSamples + 1)
//       where old(k) reads the source at (where it would have gone on) + k x (the last speed). A fade may go on
//       into the next calls; those go on with the old path as it was, the new path being their positions.
//       A jump during a fade starts a new fade, whose old path is where the CURRENT reading would have gone on
//       (the faded-in path: the one about to be heard; the faded-out one is dropped).
//       The first call after construction or reset() is never a jump: there was nothing before it.
//   reset()                        forgets everything (a stop of the transport).
//   isFading()                     true while a fade is not finished.
//
// No JUCE, no threads.

#include <cmath>
#include <cstdint>
#include <vector>

namespace ex5 {

constexpr double kPi = 3.14159265358979323846;

// x between its samples (Step 3's Hermite), 0 outside x (provided).
inline double readHermite(const std::vector<float>& x, double position) noexcept {
    const double whole = std::floor(position);
    const std::int64_t i = static_cast<std::int64_t>(whole);
    const double f = position - whole;
    const auto at = [&x](std::int64_t k) { return k >= 0 && k < static_cast<std::int64_t>(x.size()) ? static_cast<double>(x[static_cast<size_t>(k)]) : 0.0; };
    const double y0 = at(i - 1), y1 = at(i), y2 = at(i + 1), y3 = at(i + 2);
    const double c1 = 0.5 * (y2 - y0), c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3, c3 = 1.5 * (y1 - y2) + 0.5 * (y3 - y0);
    return ((c3 * f + c2) * f + c1) * f + y1;
}

class Declicker {
public:
    Declicker(int fadeSamples, double thresholdSamples) noexcept : fadeSamples_(fadeSamples), threshold_(thresholdSamples) {}

    // ---- YOUR TURN (starter/ex5_declick.cpp) ------------------------------------------------------------------
    void read(const std::vector<float>& source, const std::vector<double>& positions, float* out);
    void reset() noexcept;
    bool isFading() const noexcept;

private:
    int fadeSamples_;
    double threshold_;
    // Your state. These are what the solution keeps; use them or others.
    bool started_ = false;  // a call has been made since construction or reset()
    double next_ = 0.0;     // where the reading would go on
    double speed_ = 1.0;    // the last step of the reading
    int fadeDone_ = 0;      // samples of the fade already played
    bool fading_ = false;
    double oldNext_ = 0.0;  // the old path: its next position ...
    double oldSpeed_ = 1.0; // ... and its speed
};

} // namespace ex5
