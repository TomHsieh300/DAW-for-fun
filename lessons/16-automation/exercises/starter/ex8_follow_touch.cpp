// Exercise 8 (starter): touch a control, see its automation.
//
// Fill in the TODOs. Until then the self-check prints FAIL (expected). The solution is
// solutions/ex8_follow_touch.cpp. What each function must do is in ../ex8_follow_touch.h.
// Build with -Juce: .\scripts\build.ps1 -Only 16-automation -Juce

#include "../ex8_follow_touch.h"

#include <utility>

using namespace dawlab;
using namespace dawlab::app;

namespace ex8 {

void LaneFollower::attach(ParameterPanel& panel, engine::ChannelRef ref) {
    // TODO: replace panel.onTouch with a function that calls the old one (moved into the lambda), and
    //       on a touch (down) calls touched() with {StripTarget::of(ref), effect id, parameter name}.
    (void)panel;
    (void)ref;
}

bool LaneFollower::touched(const LaneAddress& address) {
    // TODO: enabled, a track strip, a track that exists: showAutomation(), then setSelectedTrack()
    //       (which also scrolls the track into view); true. Otherwise false.
    (void)address;
    return false;
}

void LaneFollower::addToMenu(juce::PopupMenu& menu) {
    // TODO: juce::PopupMenu::Item("Show Touched Automation"), ticked while enabled, its action flips it.
    (void)menu;
}

} // namespace ex8

#include "../ex8_check.h"
