// Exercise 5 (solution): a fade over a jump.
//
// Build and run:  l20_ex5_solution   (ctest, label "solution")
// Your version:   exercises/starter/ex5_declick.cpp -> l20_ex5_starter

#include "../exercises/ex5_declick.h"

namespace ex5 {

void Declicker::read(const std::vector<float>& source, const std::vector<double>& positions, float* out) {
    const size_t n = positions.size();
    if (n == 0)
        return;
    if (started_ && std::fabs(positions[0] - next_) > threshold_ && fadeSamples_ > 0) {
        // A jump. The old path goes on from where the reading would have gone on, at the speed it had.
        fading_ = true;
        fadeDone_ = 0;
        oldNext_ = next_;
        oldSpeed_ = speed_;
    }
    for (size_t k = 0; k < n; ++k) {
        const double now = readHermite(source, positions[k]);
        if (!fading_) {
            out[k] += static_cast<float>(now);
            continue;
        }
        const double t = static_cast<double>(fadeDone_ + 1) / (fadeSamples_ + 1);
        const double old = readHermite(source, oldNext_);
        out[k] += static_cast<float>(now * std::sin(t * kPi / 2.0) + old * std::cos(t * kPi / 2.0));
        oldNext_ += oldSpeed_;
        if (++fadeDone_ >= fadeSamples_)
            fading_ = false;
    }
    speed_ = n >= 2 ? positions[n - 1] - positions[n - 2] : 1.0;
    next_ = positions[n - 1] + speed_;
    started_ = true;
}

void Declicker::reset() noexcept {
    started_ = false;
    fading_ = false;
    fadeDone_ = 0;
    speed_ = 1.0;
}

bool Declicker::isFading() const noexcept {
    return fading_;
}

} // namespace ex5

#include "../exercises/ex5_check.h"
