// Exercise 7 (solution): a click track in DawLab 19.
//
// Build and run:  l19_ex7_solution   (ctest, label "solution"; needs -Juce)
// Your version:   exercises/starter/ex7_click_track.cpp -> l19_ex7_starter

#include "../exercises/ex7_click_track.h"

namespace ex7 {

using namespace dawlab;
using namespace dawlab::app;

Click addClickTrack(MainComponent& main, int firstBar, int bars) {
    Click result;
    if (firstBar < 1 || bars < 1 || bars > 999)
        return result;
    constexpr std::int64_t kBeat = 960, kBar = 4 * kBeat;
    Session& session = main.getSession();

    MidiClipModel clip;
    clip.start = (firstBar - 1) * kBar;
    clip.length = bars * kBar;
    clip.name = "Click";
    for (int beat = 0; beat < 4 * bars; ++beat) {
        const bool first = beat % 4 == 0;
        clip.played.push_back({beat * kBeat, 240, first ? 84 : 72, first ? 127 : 90, 0});
    }

    // The track brings its clip: one edit, and undo takes both away.
    edit::AddTrack add = session.makeNewTrack(engine::TrackFormat::Mono, -1, "Click", true);
    clip.id = session.newMidiClipId();
    add.track.midiClips = {clip};
    const engine::TrackId id = add.track.id;
    if (!session.perform(Edit(add), "Add click track"))
        return result; // no room
    result.ok = true;
    result.track = id;
    result.notes = static_cast<int>(clip.played.size());
    return result;
}

} // namespace ex7

#include "../exercises/ex7_check.h"
