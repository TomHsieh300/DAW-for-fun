// Exercise 4 (starter): a fit to a fraction of a sample.
//
// Fill in the TODOs. Until then the self-check prints FAIL (expected). The solution is
// solutions/ex4_fine_fit.cpp. What the functions must do is in ../ex4_fine_fit.h.

#include "../ex4_fine_fit.h"

namespace ex4 {

double refine(double before, double at, double after) noexcept {
    // TODO: the top of the parabola through the three scores, clamped to [-0.5, 0.5]; 0 if they are not a peak.
    (void)before, (void)at, (void)after;
    return 0.0;
}

double readHermite(const std::vector<float>& x, double position) noexcept {
    // TODO: Step 3's Hermite with the four samples around the position (inputAt() gives 0 outside x).
    return inputAt(x, static_cast<std::int64_t>(std::floor(position)));
}

std::vector<float> stretchFine(const std::vector<float>& x, const std::vector<double>& positions, const StretchSettings& settings) {
    // TODO: wsola.h's stretch(), with `before` and `start` as doubles: read the natural continuation with
    //       readHermite(), search the whole offsets against it, refine() the best, read the piece with readHermite().
    return stretch(x, positions, settings);
}

} // namespace ex4

#include "../ex4_check.h"
