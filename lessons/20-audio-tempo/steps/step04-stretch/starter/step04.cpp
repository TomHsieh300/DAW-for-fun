// Chapter 20, Step 4 (starter): a time-stretcher.
//
// Fill in the TODOs. Until then the self-check prints FAIL (expected). The solution is
// solution/step04.cpp. What the functions must do is in ../minidaw.h.

#include "../minidaw.h"

namespace minidaw {

int bestOffset(const std::vector<float>& x, std::int64_t candidate, std::int64_t natural, const StretchSettings& settings) noexcept {
    // TODO: 0 if candidate == natural or there is no search. Else score(d) for d = 0, +1, -1, +2, -2 ... up to
    //       the search, over window / 2 samples: sum a b / sqrt(sum a a), a = inputAt(x, candidate + d + i),
    //       b = inputAt(x, natural + i). Keep a d only if its score is HIGHER than the best so far.
    (void)x, (void)candidate, (void)natural, (void)settings;
    return 0;
}

std::vector<float> stretch(const std::vector<float>& x, const std::vector<double>& positions, const StretchSettings& settings) {
    // TODO: refuse bad settings. Then for the output samples at = -h, 0, h ... below positions.size():
    //         candidate = round(positionAt(positions, at + h)) - h
    //         start     = candidate (the first piece), or candidate + bestOffset(x, candidate, the start before + h, settings)
    //         output[at + i] += hann(i, window) x inputAt(x, start + i)   for the i inside the output
    (void)x, (void)settings;
    return std::vector<float>(positions.size(), 0.0f);
}

} // namespace minidaw

#include "../check.h"
