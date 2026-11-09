// Exercise 5 (solution): tell the user what is wrong with the input.
//
// Build and run:  l17_ex5_solution   (ctest, label "solution")
// Your version:   exercises/starter/ex5_input_health.cpp -> l17_ex5_starter

#include "../exercises/ex5_input_health.h"

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
    const int count = std::min(numChannels, kMaxChannels);
    for (int i = 0; i < n; ++i) {
        bool allZero = true;
        for (int c = 0; c < count; ++c) {
            const float x = channels[c][i];
            const float a = std::abs(x);
            peak_ = std::max(peak_, a);
            allZero = allZero && x == 0.0f;
            // A run at full scale, of one sign; it may have begun in the last block.
            int& run = run_[static_cast<size_t>(c)];
            const int sign = a >= kFullScale ? (x > 0.0f ? 1 : -1) : 0;
            if (sign != 0 && (run == 0 || (run > 0) == (sign > 0))) {
                run += sign;
                const int length = std::abs(run);
                if (length == kClipRun)
                    ++clips_;
                if (length >= kClipRun)
                    longestClip_ = std::max(longestClip_, length);
            } else {
                run = sign; // a new run of the other sign (length 1), or none
            }
        }
        if (allZero) {
            ++zeroFrames_;
            ++zeroRun_;
        } else {
            // A run of zeros that ends here lay between sound: a gap. Zeros before the first sound do not count.
            if (heardSomething_)
                longestGap_ = std::max(longestGap_, zeroRun_);
            zeroRun_ = 0;
            heardSomething_ = true;
        }
    }
    frames_ += n;
}

std::vector<std::string> InputHealth::warnings() const {
    std::vector<std::string> out;
    char text[256];
    if (frames_ > 0 && zeroFrames_ == frames_) {
        out.emplace_back("Every sample was exactly 0: the input is muted or blocked. Check the system's microphone "
                         "settings and privacy settings.");
        return out; // nothing else can be said about silence
    }
    if (static_cast<double>(longestGap_) >= kGapSeconds * sampleRate_) {
        std::snprintf(text, sizeof(text), "The input stopped for %.3f s in the middle of the take: a cable or an interface?",
                      static_cast<double>(longestGap_) / sampleRate_);
        out.emplace_back(text);
    }
    const double peakDb = peak_ > 0.0f ? 20.0 * std::log10(peak_) : -999.0;
    if (frames_ > 0 && peakDb < kQuietDb) {
        std::snprintf(text, sizeof(text), "The input is very quiet: its peak is %.1f dBFS. Raise the input gain.", peakDb);
        out.emplace_back(text);
    }
    if (clips_ > 0) {
        std::snprintf(text, sizeof(text), "The input clipped %d times, the longest for %d samples: lower the input gain.", clips_,
                      longestClip_);
        out.emplace_back(text);
    }
    return out;
}

} // namespace ex5

#include "../exercises/ex5_check.h"
