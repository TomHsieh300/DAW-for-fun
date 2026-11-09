// Exercise 7 (solution): reorder tracks by dragging their headers.
//
// Build and run:  l15_ex7_solution   (ctest, label "solution"; needs -Juce)
// Your version:   exercises/starter/ex7_reorder_tracks.cpp -> l15_ex7_starter

#include "../exercises/ex7_reorder_tracks.h"

#include <algorithm>

using namespace dawlab;
using namespace dawlab::app;

namespace ex7 {

bool moveTrack(Session& session, engine::TrackId id, int index) {
    const int from = session.indexOf(id);
    if (from < 0)
        return false;
    const int to = std::clamp(index, 0, session.getNumTracks() - 1);
    if (to == from)
        return false;
    // Out and back in, as one change: the snapshot is taken before the track is gone.
    std::vector<Edit> edits;
    edits.emplace_back(edit::RemoveTrack{id});
    edits.emplace_back(edit::AddTrack{session.snapshotTrack(id), to});
    return session.perform(std::move(edits), "Move Track");
}

void TrackDragger::mouseDown(const juce::MouseEvent& e) {
    dragging_ = false;
    dropIndex_ = -1;
    dragged_ = {};
    if (e.x >= ArrangementView::kHeaderWidth || e.y < ArrangementView::kRulerHeight)
        return;
    const int lane = arrangement_.laneAt(e.y);
    if (lane < 0)
        return;
    dragged_ = session_.getTrackId(lane);
    downY_ = e.y;
}

void TrackDragger::mouseDrag(const juce::MouseEvent& e) {
    if (!dragged_.isValid())
        return;
    if (!dragging_ && std::abs(e.y - downY_) < kThresholdPixels)
        return;
    dragging_ = true;
    const int lane = arrangement_.laneAt(e.y);
    if (lane >= 0)
        dropIndex_ = lane;
    else // above the first lane or below the last one
        dropIndex_ = e.y < ArrangementView::kRulerHeight + arrangement_.getLaneHeight() ? 0 : session_.getNumTracks() - 1;
}

void TrackDragger::mouseUp(const juce::MouseEvent& e) {
    mouseDrag(e);
    if (dragging_ && dropIndex_ >= 0)
        moveTrack(session_, dragged_, dropIndex_);
    dragging_ = false;
    dropIndex_ = -1;
    dragged_ = {};
}

} // namespace ex7

#include "../exercises/ex7_check.h"
