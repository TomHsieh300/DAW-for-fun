// Exercise 5 (starter): a fade over a jump.
//
// Fill in the TODOs. Until then the self-check prints FAIL (expected). The solution is
// solutions/ex5_declick.cpp. What the class must do is in ../ex5_declick.h.

#include "../ex5_declick.h"

namespace ex5 {

void Declicker::read(const std::vector<float>& source, const std::vector<double>& positions, float* out) {
    // TODO: 1. is the first position a jump? (more than the threshold from next_, and not the first call) Then a
    //          fade begins: the old path starts at next_ and goes on at speed_;
    //       2. every sample: the new reading, and during a fade the old one too, mixed by sin and cos;
    //       3. remember where the reading goes on: speed_ and next_ from the last two positions.
    for (size_t k = 0; k < positions.size(); ++k)
        out[k] += static_cast<float>(readHermite(source, positions[k]));
}

void Declicker::reset() noexcept {
    // TODO: forget everything.
}

bool Declicker::isFading() const noexcept {
    return false;
}

} // namespace ex5

#include "../ex5_check.h"
