// Exercise 7 (starter): takes that follow as they were recorded.
//
// Fill in the TODO. Until then the self-check prints FAIL (expected). The solution is
// solutions/ex7_follow_as_recorded.cpp. What the function must do is in ../ex7_follow_as_recorded.h.

#include "../ex7_follow_as_recorded.h"

namespace ex7 {

using namespace dawlab;

Followed followAsRecorded(app::Session& session, engine::TrackId track, engine::FollowMode mode, const engine::TempoMap& recordedOver) {
    // TODO: 1. refuse what the header says;
    //       2. a copy of the track's clips; for each one that does not follow and is a recording, a new
    //          engine::ClipFollow from `recordedOver` (engine::tickOfSample(), engine::markersForTake());
    //       3. if any changed: ONE session.perform(app::edit::SetClips{...}, "Takes follow the tempo").
    (void)session, (void)track, (void)mode, (void)recordedOver;
    return {};
}

} // namespace ex7

#include "../ex7_check.h"
