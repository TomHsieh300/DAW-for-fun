// Exercise 7 (solution): takes that follow as they were recorded.
//
// Build and run:  l20_ex7_solution   (ctest, label "solution"; needs -Juce)
// Your version:   exercises/starter/ex7_follow_as_recorded.cpp -> l20_ex7_starter

#include "../exercises/ex7_follow_as_recorded.h"

namespace ex7 {

using namespace dawlab;

Followed followAsRecorded(app::Session& session, engine::TrackId track, engine::FollowMode mode, const engine::TempoMap& recordedOver) {
    Followed result;
    if (mode == engine::FollowMode::Off || !session.hasTrack(track))
        return result;
    const double rate = session.getClipSampleRate();
    std::vector<engine::Clip> list = session.getClips(track);
    for (engine::Clip& clip : list) {
        if (engine::followModeOf(clip) != engine::FollowMode::Off || session.getTakesOf(clip) == nullptr)
            continue;
        auto follow = std::make_shared<engine::ClipFollow>();
        follow->mode = mode;
        if (mode == engine::FollowMode::Start) {
            follow->startTick = engine::tickOfSample(recordedOver, rate, clip.start);
        } else {
            follow->sourceLength = clip.length;
            follow->markers = engine::markersForTake(recordedOver, rate, clip.start, clip.sourceOffset, clip.length);
            if (!engine::areWarpMarkers(follow->markers))
                continue;
        }
        clip.follow = std::move(follow);
        ++result.clips;
    }
    result.ok = true;
    if (result.clips > 0 && !session.perform(app::edit::SetClips{track, std::move(list)}, "Takes follow the tempo"))
        return Followed{};
    return result;
}

} // namespace ex7

#include "../exercises/ex7_check.h"
