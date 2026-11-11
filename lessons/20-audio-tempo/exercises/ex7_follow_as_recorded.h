#pragma once
// Exercise 7 - Takes that follow as they were recorded (optional, JUCE).
//
// DawLab 20 records a take as a clip that stays (Step 6), and turning following on later gives it the markers of
// "a take recorded over the map of NOW". If the tempo was changed in between, that map is the wrong one: the take
// was in time with the old tempo, and is now made to stay where the new tempo leaves it - off the beat.
//
// The right markers are those of the map the take WAS recorded over. A clip that stays keeps its start in
// samples, so the sample where it was recorded is still its start; what is missing is that map. Give it:
//
//   followAsRecorded(session, track, mode, recordedOver)
//       Every clip of `track` that does not follow the tempo and plays a recording (Session::getTakesOf() knows
//       it) gets to follow by `mode`, as if it had been recorded over `recordedOver`:
//         Start               its start tick is recordedOver's tick of its start sample;
//         Varispeed, Stretch  its markers are engine::markersForTake(recordedOver, rate, clip.start,
//                             clip.sourceOffset, clip.length), its sourceLength is clip.length.
//       (rate: Session::getClipSampleRate().) All of them in ONE undo step, "Takes follow the tempo"
//       (edit::SetClips of the track's whole list - the session then places them under the map of now).
//       Refused (ok = false, nothing changes): mode Off, a track that is not there. No clip to change: ok, 0
//       clips, and no undo step.
//
// Chapter 20's engine helpers are in <dawlab/engine/Warp.h>.

#include "MainComponent.h"

namespace ex7 {

struct Followed {
    bool ok = false;
    int clips = 0; // how many clips now follow
};

// ---- YOUR TURN (starter/ex7_follow_as_recorded.cpp) -----------------------------------------------------------
Followed followAsRecorded(dawlab::app::Session& session, dawlab::engine::TrackId track, dawlab::engine::FollowMode mode,
                          const dawlab::engine::TempoMap& recordedOver);

} // namespace ex7
