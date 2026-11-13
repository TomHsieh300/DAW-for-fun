#pragma once

// What the numbers in a project file may be, beyond their sign: one set of limits for the readers of chapters 15
// to 20 (the same file in each). DawLab writes nothing near them. A file beyond them is refused where it is read:
// a sample rate of 1e-9 Hz made the conversion of the clips cast an enormous size, and a start of 2^63 - 1
// overflowed when its length was added.

#include <cstdint>

namespace dawlab::app::project {

// The largest position or length a file may give, in samples or ticks: 2^40, some 260 days at 48 kHz. The sum of
// two of them (a start and a length) is far from overflowing.
constexpr std::int64_t kMaxPosition = std::int64_t{1} << 40;

// The sample rates that a device has.
constexpr double kMinSampleRate = 8000.0, kMaxSampleRate = 768000.0;

// A position or a length: from `least` to kMaxPosition.
constexpr bool isPosition(std::int64_t value, std::int64_t least = 0) noexcept { return value >= least && value <= kMaxPosition; }

// A sample rate from kMinSampleRate to kMaxSampleRate (NaN is none).
constexpr bool isSampleRate(double rate) noexcept { return rate >= kMinSampleRate && rate <= kMaxSampleRate; }

} // namespace dawlab::app::project
