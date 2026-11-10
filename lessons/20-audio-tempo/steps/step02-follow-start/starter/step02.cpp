// Chapter 20, Step 2 (starter): a clip whose start follows the tempo.
//
// Fill in the TODOs. Until then the self-check prints FAIL (expected). The solution is
// solution/step02.cpp. What the functions must do is in ../minidaw.h.

#include "../minidaw.h"

namespace minidaw {

void setFollow(Clip& clip, Follow follow, const TempoMap& map) noexcept {
    // TODO: when following is turned on, startTick = tickOfSample(map, clip.start).
    (void)map;
    clip.follow = follow;
}

std::int64_t startUnder(const Clip& clip, const TempoMap& map) noexcept {
    // TODO: sampleOfTick(map, clip.startTick) for a clip that follows.
    (void)map;
    return clip.start;
}

void retime(std::vector<Clip>& clips, const TempoMap& newMap) noexcept {
    // TODO: every clip's start = startUnder(clip, newMap).
    (void)clips, (void)newMap;
}

void moveTo(Clip& clip, std::int64_t sample, const TempoMap& map) noexcept {
    // TODO: a clip that follows also gets the tick of `sample`.
    (void)map;
    clip.start = sample;
}

void moveToTick(Clip& clip, double tick, const TempoMap& map) noexcept {
    // TODO: start = the sample of `tick`; a clip that follows keeps `tick` itself.
    (void)clip, (void)tick, (void)map;
}

double endTick(const Clip& clip, const TempoMap& map) noexcept {
    // TODO: the tick of the sample startUnder(clip, map) + clip.length.
    (void)clip, (void)map;
    return 0.0;
}

std::vector<std::int64_t> gaps(std::vector<Clip> clips) {
    // TODO: sort by start; after each clip but the last: the next clip's start - this clip's end.
    (void)clips;
    return {};
}

std::int64_t jumpInClip(const Clip& clip, const TempoMap& oldMap, const TempoMap& newMap, double playheadTick) noexcept {
    // TODO: (playhead's sample - clip's start) under the new map, minus the same under the old map.
    (void)clip, (void)oldMap, (void)newMap, (void)playheadTick;
    return 0;
}

} // namespace minidaw

#include "../check.h"
