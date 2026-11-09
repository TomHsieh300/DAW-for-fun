// Exercise 5 (starter): tell the user what is wrong with the input.
//
// Fill in the two TODOs. Until then the self-check prints FAIL (expected). The solution is
// solutions/ex5_input_health.cpp. What the functions must do is in ../ex5_input_health.h.

#include "../ex5_input_health.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace ex5 {

void InputHealth::reset(double sampleRate) noexcept {
    *this = InputHealth{};
    sampleRate_ = sampleRate;
}

void InputHealth::add(const float* const* channels, int numChannels, int n) noexcept {
    // TODO: per frame: the peak; whether every channel is exactly 0 (zeroFrames_, zeroRun_, and a gap
    //       when a run of zeros ends after something was heard); per channel the run at full scale of
    //       one sign in run_ (it goes on into the next block) - a run reaching kClipRun is one clip.
    (void)channels;
    (void)numChannels;
    frames_ += n;
}

std::vector<std::string> InputHealth::warnings() const {
    // TODO: the sentences of the header, in its order. A take of exact zeros gets only the first.
    return {};
}

} // namespace ex5

#include "../ex5_check.h"
