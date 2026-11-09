#pragma once
// Exercise 8 - Touch a control, see its automation (optional, JUCE).
//
// To automate the compressor's threshold in DawLab 16, the user opens the lane's menu, finds the
// effect, finds the parameter. Most DAWs offer a shortcut: grab the control, and the arrangement shows
// its lane. Without changing the app:
//
//   LaneFollower::attach(panel, ref)   an effect's parameter window (ParameterPanel): its onTouch is
//                                      wrapped, so the app's own handler still runs (it is what makes
//                                      Touch and Latch record), and a touch shows that parameter's
//                                      lane under its track, scrolled into view, the track selected
//   LaneFollower::touched(address)     the same for any lane address, e.g. a track's Gain or Pan
//   LaneFollower::addToMenu(menu)      a View menu item "Show Touched Automation" with a tick, which
//                                      turns the following on and off
//
// Only tracks have rows in the arrangement: a touch on a bus or the master shows nothing.

#include "ArrangementView.h"
#include "ParameterPanel.h"
#include "Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace ex8 {

class LaneFollower {
public:
    LaneFollower(dawlab::app::ArrangementView& arrangement, dawlab::app::Session& session) : arrangement_(arrangement), session_(session) {}

    // ---- YOUR TURN (starter/ex8_follow_touch.cpp) -------------------------------------------------
    // Wraps panel.onTouch (keep the old function and call it first). `ref` is the strip the panel's
    // effect sits on; the lane of parameter i is {StripTarget::of(ref), effect id, parameter i's name}.
    void attach(dawlab::app::ParameterPanel& panel, dawlab::engine::ChannelRef ref);
    // While enabled: a track's lane is shown (ArrangementView::showAutomation) and the track selected
    // (setSelectedTrack() scrolls it into view). Returns true if a lane was shown.
    bool touched(const dawlab::app::LaneAddress& address);
    // A ticked item "Show Touched Automation" whose action flips isEnabled().
    void addToMenu(juce::PopupMenu& menu);

    // ---- provided ---------------------------------------------------------------------------------
    void setEnabled(bool enabled) noexcept { enabled_ = enabled; }
    bool isEnabled() const noexcept { return enabled_; }

private:
    dawlab::app::ArrangementView& arrangement_;
    dawlab::app::Session& session_;
    bool enabled_ = true;
};

} // namespace ex8
