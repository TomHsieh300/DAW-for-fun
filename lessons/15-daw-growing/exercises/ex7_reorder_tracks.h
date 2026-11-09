#pragma once
// Exercise 7 - Reorder tracks by dragging their headers (optional, JUCE).
//
// DawLab 15 can add and delete tracks but not move them. Without changing the app:
//
//   moveTrack(session, id, index)   one undoable step "Move Track": the track goes to display
//                                   position `index` (clamped), with everything it has
//   TrackDragger                    a juce::MouseListener on the ArrangementView: press on a track
//                                   header, drag up or down, release - the track moves there
//
// The Session has no "move" edit. It has two edits that together are one: RemoveTrack takes the
// track out, AddTrack puts a snapshot of it (Session::snapshotTrack) back at any index. Performed
// as ONE change, they are one undo step, and the History already knows their inverses.

#include "ArrangementView.h"
#include "Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace ex7 {

// Moves track `id` to display position `index`, clamped to 0 .. getNumTracks() - 1, as one undo
// step named "Move Track". False - and nothing in the history - if there is no such track or it is
// already there.
bool moveTrack(dawlab::app::Session& session, dawlab::engine::TrackId id, int index);

// Drags a track by its header. Add it to the arrangement with arrangement.addMouseListener(&dragger,
// false); it only looks at presses in the header column (x < ArrangementView::kHeaderWidth).
class TrackDragger final : public juce::MouseListener {
public:
    static constexpr int kThresholdPixels = 4; // less than this is a click (which selects), not a drag

    TrackDragger(dawlab::app::ArrangementView& arrangement, dawlab::app::Session& session)
        : arrangement_(arrangement), session_(session) {}

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    // While dragging: the display position the track would move to (-1 when not dragging). A real
    // app draws a line there.
    int getDropIndex() const noexcept { return dropIndex_; }

private:
    dawlab::app::ArrangementView& arrangement_;
    dawlab::app::Session& session_;
    dawlab::engine::TrackId dragged_;
    int downY_ = 0;
    bool dragging_ = false;
    int dropIndex_ = -1;
};

} // namespace ex7
