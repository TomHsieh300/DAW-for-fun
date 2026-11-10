// Chapter 20, Step 3 (solution): varispeed.
//
// Build and run:  l20_step03_solution   (ctest, label "solution")
// Your version:   starter/step03.cpp -> l20_step03_starter

#include "../minidaw.h"

namespace minidaw {

double semitonesOf(double speed) noexcept {
    return 12.0 * std::log2(speed);
}

float readAt(const std::vector<float>& source, double position, double speed, Interpolation how) noexcept {
    const double whole = std::floor(position);
    const std::int64_t i = static_cast<std::int64_t>(whole);
    const double frac = position - whole;
    if (how == Interpolation::Linear) {
        const double a = sampleAt(source, i), b = sampleAt(source, i + 1);
        return static_cast<float>(a + frac * (b - a));
    }
    if (how == Interpolation::Hermite) {
        const double y0 = sampleAt(source, i - 1), y1 = sampleAt(source, i), y2 = sampleAt(source, i + 1), y3 = sampleAt(source, i + 2);
        const double c1 = 0.5 * (y2 - y0);
        const double c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3;
        const double c3 = 1.5 * (y1 - y2) + 0.5 * (y3 - y0);
        return static_cast<float>(((c3 * frac + c2) * frac + c1) * frac + y1);
    }
    // Sinc: a low-pass at Nyquist, or at Nyquist / speed when reading faster than 1.
    const double cutoff = std::min(1.0, 1.0 / speed);
    const double reach = kSincZeroCrossings / cutoff; // samples to each side
    const std::int64_t first = static_cast<std::int64_t>(std::ceil(position - reach));
    const std::int64_t last = static_cast<std::int64_t>(std::floor(position + reach));
    double sum = 0.0, weights = 0.0;
    for (std::int64_t k = first; k <= last; ++k) {
        const double x = static_cast<double>(k) - position;
        const double w = sinc(cutoff * x) * blackman(x / reach);
        sum += w * sampleAt(source, k);
        weights += w;
    }
    return static_cast<float>(weights != 0.0 ? sum / weights : 0.0);
}

void positionsFor(const WarpedClip& clip, const TempoMap& map, std::int64_t from, int n, std::vector<double>& positions) {
    positions.assign(static_cast<size_t>(std::max(n, 0)), 0.0);
    if (n <= 0)
        return;
    const double rate = map.sampleRate();
    const double a = static_cast<double>(from), b = static_cast<double>(from + n);
    const double tickA = map.tickAt(a / rate), tickB = map.tickAt(b / rate);

    // The knots inside the block, as ticks: where the line bends.
    std::vector<double> ticks;
    for (const TempoPoint& p : map.points())
        if (static_cast<double>(p.tick) > tickA && static_cast<double>(p.tick) < tickB)
            ticks.push_back(static_cast<double>(p.tick));
    for (const WarpMarker& m : clip.warp.markers())
        if (m.tick > tickA && m.tick < tickB)
            ticks.push_back(m.tick);

    // The line's corners, as song samples: the block's ends, the knots, and the grid (a ramp bends everywhere).
    std::vector<double> at{a, b};
    for (const double tick : ticks)
        at.push_back(map.secondsAt(tick) * rate);
    const std::int64_t intoGrid = ((from % kPositionGrid) + kPositionGrid) % kPositionGrid; // also right before sample 0
    for (std::int64_t s = from - intoGrid + kPositionGrid; s < from + n; s += kPositionGrid)
        at.push_back(static_cast<double>(s));
    std::sort(at.begin(), at.end());
    at.erase(std::unique(at.begin(), at.end()), at.end());
    while (at.front() < a) // a knot that rounding put outside the block
        at.erase(at.begin());
    while (at.back() > b)
        at.pop_back();
    std::vector<double> pos;
    for (const double sample : at)
        pos.push_back(exactPosition(clip, map, sample));

    size_t piece = 0;
    for (int i = 0; i < n; ++i) {
        const double s = static_cast<double>(from + i);
        while (piece + 2 < at.size() && s >= at[piece + 1])
            ++piece;
        positions[static_cast<size_t>(i)] = pos[piece] + (s - at[piece]) * (pos[piece + 1] - pos[piece]) / (at[piece + 1] - at[piece]);
    }
}

namespace {

// The first song sample whose position is at or after `source`.
std::int64_t firstSampleAtOrAfter(const WarpedClip& clip, const TempoMap& map, double source) noexcept {
    std::int64_t s = static_cast<std::int64_t>(std::ceil(map.secondsAt(clip.warp.tickAt(source)) * map.sampleRate()));
    while (exactPosition(clip, map, static_cast<double>(s)) < source)
        ++s;
    while (exactPosition(clip, map, static_cast<double>(s - 1)) >= source)
        --s;
    return s;
}

} // namespace

std::int64_t firstSample(const WarpedClip& clip, const TempoMap& map) noexcept {
    return firstSampleAtOrAfter(clip, map, clip.sourceFrom);
}

std::int64_t endSample(const WarpedClip& clip, const TempoMap& map) noexcept {
    return firstSampleAtOrAfter(clip, map, clip.sourceTo);
}

void renderWarped(const WarpedClip& clip, const std::vector<std::vector<float>>& sources, const TempoMap& map, std::int64_t from, int n, float* out,
                  Interpolation how) {
    if (clip.source < 0 || clip.source >= static_cast<int>(sources.size()))
        return;
    const std::int64_t a = std::max(from, firstSample(clip, map)), b = std::min(from + n, endSample(clip, map));
    if (a >= b)
        return;
    std::vector<double> positions;
    positionsFor(clip, map, from, n + 1, positions); // one more: the speed of the block's last sample
    const std::vector<float>& source = sources[static_cast<size_t>(clip.source)];
    for (std::int64_t s = a; s < b; ++s) {
        const size_t i = static_cast<size_t>(s - from);
        out[i] += readAt(source, positions[i], positions[i + 1] - positions[i], how);
    }
}

} // namespace minidaw

#include "../check.h"
