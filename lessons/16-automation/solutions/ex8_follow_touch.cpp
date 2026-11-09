// Exercise 8 (solution): touch a control, see its automation.
//
// Build and run:  l16_ex8_solution   (ctest, label "solution"; needs -Juce)
// Your version:   exercises/starter/ex8_follow_touch.cpp -> l16_ex8_starter

#include "../exercises/ex8_follow_touch.h"

#include <utility>

using namespace dawlab;
using namespace dawlab::app;

namespace ex8 {

void LaneFollower::attach(ParameterPanel& panel, engine::ChannelRef ref) {
    engine::InsertEffect& effect = panel.getEffect();
    const StripTarget strip = StripTarget::of(ref);
    // The app's handler first: it is what tells the lane that a hand is on it.
    panel.onTouch = [this, previous = std::move(panel.onTouch), strip, &effect](int index, bool down) {
        if (previous)
            previous(index, down);
        if (down)
            touched(LaneAddress{strip, effect.getEffectId(), effect.getParameterInfo(index).name});
    };
}

bool LaneFollower::touched(const LaneAddress& address) {
    if (!enabled_ || address.strip.kind != engine::ChannelKind::Track || !session_.hasTrack(address.strip.track))
        return false;
    const engine::TrackId track = address.strip.track;
    arrangement_.showAutomation(track, address);
    arrangement_.setSelectedTrack(track); // also scrolls it into view
    return true;
}

void LaneFollower::addToMenu(juce::PopupMenu& menu) {
    menu.addItem(juce::PopupMenu::Item("Show Touched Automation").setTicked(enabled_).setAction([this] { enabled_ = !enabled_; }));
}

} // namespace ex8

#include "../exercises/ex8_check.h"
