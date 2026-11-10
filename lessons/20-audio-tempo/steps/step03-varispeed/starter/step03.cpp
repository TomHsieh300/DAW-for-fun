// Chapter 20, Step 3 (starter): varispeed.
//
// Fill in the TODOs. Until then the self-check prints FAIL (expected). The solution is
// solution/step03.cpp. What the functions must do is in ../minidaw.h.

#include "../minidaw.h"

namespace minidaw {

double semitonesOf(double speed) noexcept {
    // TODO: 12 x log2(speed).
    (void)speed;
    return 0.0;
}

float readAt(const std::vector<float>& source, double position, double speed, Interpolation how) noexcept {
    // TODO: i = floor(position), frac = position - i; then Linear, Hermite or Sinc as in minidaw.h.
    //       sampleAt() gives 0 outside the source.
    (void)speed, (void)how;
    return sampleAt(source, static_cast<std::int64_t>(std::floor(position)));
}

void positionsFor(const WarpedClip& clip, const TempoMap& map, std::int64_t from, int n, std::vector<double>& positions) {
    // TODO: the corners, as song samples: the block's two ends; the knots between them - the map's points and
    //       the warp map's markers with a tick between the ends' ticks (map.tickAt), at map.secondsAt(tick) x rate;
    //       and the multiples of kPositionGrid inside the block. exactPosition() at each corner, in order; a
    //       line from corner to corner.
    (void)clip, (void)map, (void)from;
    positions.assign(static_cast<size_t>(std::max(n, 0)), 0.0);
}

std::int64_t firstSample(const WarpedClip& clip, const TempoMap& map) noexcept {
    // TODO: ceil(map.secondsAt(clip.warp.tickAt(sourceFrom)) x rate), then forward while exactPosition() is
    //       still before sourceFrom, and back while the sample before is not.
    (void)clip, (void)map;
    return 0;
}

std::int64_t endSample(const WarpedClip& clip, const TempoMap& map) noexcept {
    // TODO: the same for sourceTo.
    (void)clip, (void)map;
    return 0;
}

void renderWarped(const WarpedClip& clip, const std::vector<std::vector<float>>& sources, const TempoMap& map, std::int64_t from, int n, float* out,
                  Interpolation how) {
    // TODO: the part of [from, from + n) inside [firstSample, endSample); positionsFor() n + 1 samples (the speed
    //       of sample i is positions[i + 1] - positions[i]); out[i] += readAt(...).
    (void)clip, (void)sources, (void)map, (void)from, (void)n, (void)out, (void)how;
}

} // namespace minidaw

#include "../check.h"
