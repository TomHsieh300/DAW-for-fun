// Exercise 8 (starter): audio quantize.
//
// Fill in the TODOs. Until then the self-check prints FAIL (expected). The solution is
// solutions/ex8_quantize_audio.cpp. What the functions must do is in ../ex8_quantize_audio.h.

#include "../ex8_quantize_audio.h"

namespace ex8 {

using namespace dawlab;

std::vector<std::int64_t> findOnsets(const AudioBuffer& audio, std::int64_t from, std::int64_t to, double threshold, std::int64_t quietSamples) {
    // TODO: the peak of the range; then every sample at or above threshold x peak that comes after quietSamples
    //       below it (the range's start counts as quiet).
    (void)audio, (void)from, (void)to, (void)threshold, (void)quietSamples;
    return {};
}

Quantized quantizeClip(app::Session& session, engine::TrackId track, int clipIndex, std::int64_t gridTicks, double strength, engine::FollowMode mode) {
    // TODO: 1. refuse what the header says;
    //       2. the tick where a source sample is heard now (the clip's markers if it is warped, else its place);
    //       3. markers: the start, the onsets moved towards the grid, the end;
    //       4. the clip with a new engine::ClipFollow, ONE session.perform(app::edit::SetClips{...}, "Quantize audio").
    (void)session, (void)track, (void)clipIndex, (void)gridTicks, (void)strength, (void)mode;
    return {};
}

} // namespace ex8

#include "../ex8_check.h"
