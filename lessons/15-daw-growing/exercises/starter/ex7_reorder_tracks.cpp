// Exercise 7 (starter): reorder tracks by dragging their headers.
//
// Fill in the TODOs. Until then the self-check prints FAIL (expected). The solution is
// solutions/ex7_reorder_tracks.cpp. What each function must do is in ../ex7_reorder_tracks.h.
// Build with -Juce: .\scripts\build.ps1 -Only 15-daw-growing -Juce

#include "../ex7_reorder_tracks.h"

#include <algorithm>

using namespace dawlab;
using namespace dawlab::app;

namespace ex7 {

bool moveTrack(Session& session, engine::TrackId id, int index) {
    // TODO: find the track (session.indexOf); clamp `index`; nothing to do -> false.
    //       Otherwise perform ONE change of two edits, named "Move Track":
    //         edit::RemoveTrack{id}, then edit::AddTrack{session.snapshotTrack(id), index}
    //       (take the snapshot before anything is removed).
    (void)session;
    (void)id;
    (void)index;
    return false;
}

void TrackDragger::mouseDown(const juce::MouseEvent& e) {
    // TODO: only a press in the header column (x < ArrangementView::kHeaderWidth) on a lane
    //       (arrangement_.laneAt) starts a possible drag: remember that track and e.y.
    (void)e;
}

void TrackDragger::mouseDrag(const juce::MouseEvent& e) {
    // TODO: after kThresholdPixels of vertical movement it is a drag: dropIndex_ = the lane under
    //       the mouse; above the lanes (in the ruler) 0, below them the last index.
    (void)e;
}

void TrackDragger::mouseUp(const juce::MouseEvent& e) {
    // TODO: if it was a drag, moveTrack() to dropIndex_; then forget the drag (dropIndex_ = -1).
    (void)e;
}

} // namespace ex7

#include "../ex7_check.h"
