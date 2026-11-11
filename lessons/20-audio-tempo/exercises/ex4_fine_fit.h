#pragma once
// Exercise 4 - A fit to a fraction of a sample.
//
// Step 4's WSOLA slides a piece by whole samples, so it fits the natural continuation to within half a sample.
// For a 440 Hz tone half a sample is a step of 1.65 degrees of phase at every joint, and that is where the 31 to
// 40 dB between the tone and the rest come from. Fit to a fraction:
//
//   refine(before, at, after)
//       The scores of three neighbouring offsets, the middle one the best. The parabola through the three has its
//       top at   delta = (before - after) / (2 (before - 2 at + after))   from the middle. Return it, clamped to
//       [-0.5, 0.5]; return 0 if the three are not a peak (before - 2 at + after is not below 0).
//   readHermite(x, position)
//       x between its samples, as Step 3's Hermite (Catmull-Rom): the four samples around the position, 0 outside x.
//   stretchFine(x, positions, settings)
//       Step 4's stretch() with starts that are fractions:
//         - the first piece starts at its candidate, round(positionAt(c)) - h, as before;
//         - every later piece: its candidate, and the natural continuation at (the start before) + h - now a
//           fraction. If the two are the same (within 1e-9), the piece starts there: nothing slides. Otherwise
//           read the natural continuation (h samples, readHermite) and search the WHOLE offsets d as Step 4 does,
//           scoring the input at candidate + d against it; then start = candidate + d + refine(score(d - 1),
//           score(d), score(d + 1));
//         - a piece is read from x with readHermite at start + i.
//       Empty unless window is even and at least 4, and search is not negative.
//
// Step 4, solved, is in wsola.h (do Step 4 first).

#include "wsola.h"

#include <vector>

namespace ex4 {

using namespace wsola;

// ---- YOUR TURN (starter/ex4_fine_fit.cpp) ---------------------------------------------------------------------
double refine(double before, double at, double after) noexcept;
double readHermite(const std::vector<float>& x, double position) noexcept;
std::vector<float> stretchFine(const std::vector<float>& x, const std::vector<double>& positions, const StretchSettings& settings);

} // namespace ex4
