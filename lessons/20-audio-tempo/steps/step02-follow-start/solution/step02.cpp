// Chapter 20, Step 2 (solution): a clip whose start follows the tempo.
//
// Build and run:  l20_step02_solution   (ctest, label "solution")
// Your version:   starter/step02.cpp -> l20_step02_starter

#include "../minidaw.h"

namespace minidaw {

void setFollow(Clip& clip, Follow follow, const TempoMap& map) noexcept {
    if (follow == Follow::Start && clip.follow != Follow::Start)
        clip.startTick = tickOfSample(map, clip.start); // the tick of where it is: nothing moves
    clip.follow = follow;
}

std::int64_t startUnder(const Clip& clip, const TempoMap& map) noexcept {
    return clip.follow == Follow::Start ? sampleOfTick(map, clip.startTick) : clip.start;
}

void retime(std::vector<Clip>& clips, const TempoMap& newMap) noexcept {
    for (Clip& clip : clips)
        clip.start = startUnder(clip, newMap); // from the tick, every time: the tick is never found again
}

void moveTo(Clip& clip, std::int64_t sample, const TempoMap& map) noexcept {
    clip.start = sample;
    if (clip.follow == Follow::Start)
        clip.startTick = tickOfSample(map, sample);
}

void moveToTick(Clip& clip, double tick, const TempoMap& map) noexcept {
    clip.start = sampleOfTick(map, tick);
    if (clip.follow == Follow::Start)
        clip.startTick = tick; // exactly the grid's tick, not the tick of a rounded sample
}

double endTick(const Clip& clip, const TempoMap& map) noexcept {
    return tickOfSample(map, startUnder(clip, map) + clip.length);
}

std::vector<std::int64_t> gaps(std::vector<Clip> clips) {
    std::stable_sort(clips.begin(), clips.end(), [](const Clip& a, const Clip& b) { return a.start < b.start; });
    std::vector<std::int64_t> between;
    for (size_t i = 0; i + 1 < clips.size(); ++i)
        between.push_back(clips[i + 1].start - clips[i].end());
    return between;
}

std::int64_t jumpInClip(const Clip& clip, const TempoMap& oldMap, const TempoMap& newMap, double playheadTick) noexcept {
    const std::int64_t before = sampleOfTick(oldMap, playheadTick) - startUnder(clip, oldMap);
    const std::int64_t after = sampleOfTick(newMap, playheadTick) - startUnder(clip, newMap);
    return after - before;
}

} // namespace minidaw

#include "../check.h"
