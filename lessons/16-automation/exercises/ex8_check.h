#pragma once
// Exercise 8 - the self-check (main). Included at the end of the starter and of the solution.

#include "ex8_follow_touch.h"

#include "MainComponent.h"

#include <dawlab/Check.h>

#include <cstdio>

namespace ex8::check {

using namespace dawlab;
using namespace dawlab::app;

inline bool shows(ArrangementView& view, engine::TrackId track, const LaneAddress& address) {
    const LaneAddress* shown = view.getShownLane(track);
    return shown != nullptr && *shown == address;
}

inline void run() {
    MainComponent main(MainComponent::Mode::Offline);
    main.setSize(MainComponent::kDefaultWidth, MainComponent::kDefaultHeight);
    DAWLAB_CHECK(main.loadDemoSongNow() == 0);
    main.simulatePlayback(512);
    Session& session = main.getSession();
    ArrangementView& view = main.getArrangement();

    // The last track with a built-in effect of three parameters or more: far enough down to need scrolling.
    int lane = -1, slot = -1;
    for (int i = 0; i < session.getNumTracks(); ++i) {
        const engine::ChannelRef ref = session.trackRef(session.getTrackId(i));
        for (int s = 0; s < kInsertSlotsPerStrip; ++s)
            if (session.getSlot(ref, s).kind == SlotKind::Builtin && session.getSlot(ref, s).effect != nullptr &&
                session.getSlot(ref, s).effect->getNumParameters() >= 3) {
                lane = i;
                slot = s;
            }
    }
    DAWLAB_CHECK(lane >= 0);
    if (lane < 0)
        return;
    const engine::TrackId track = session.getTrackId(lane);
    const engine::ChannelRef ref = session.trackRef(track);
    engine::InsertEffect& effect = *session.getSlot(ref, slot).effect;
    auto laneOf = [&](int index) { return LaneAddress{StripTarget::of(ref), effect.getEffectId(), effect.getParameterInfo(index).name}; };

    LaneFollower follower(view, session);
    ParameterPanel panel(effect);
    int calls = 0;
    panel.onTouch = [&calls](int, bool) { ++calls; }; // stands in for the app's own handler
    follower.attach(panel, ref);
    DAWLAB_CHECK(panel.onTouch != nullptr);
    if (!panel.onTouch)
        return;

    // Every track shows a lane, so the rows are tall and the track is out of view at first.
    for (int i = 0; i < session.getNumTracks(); ++i)
        view.showAutomation(session.getTrackId(i), {StripTarget::ofTrack(session.getTrackId(i)), engine::EffectId{}, "Pan"});
    view.selectLane(0);
    view.setScrollY(0);
    DAWLAB_CHECK(view.getAutomationBounds(lane).getBottom() > view.getHeight());

    // Grab parameter 1: its lane appears under its track, in view, the track selected.
    panel.onTouch(1, true);
    DAWLAB_CHECK(calls == 1); // the app's handler still runs
    DAWLAB_CHECK(shows(view, track, laneOf(1)));
    DAWLAB_CHECK(view.getSelectedTrack() == track);
    const juce::Rectangle<int> row = view.getAutomationBounds(lane);
    DAWLAB_CHECK(!row.isEmpty() && row.getY() >= ArrangementView::kRulerHeight && row.getBottom() <= view.getHeight());
    std::printf("  touched %s on track %d: its lane is shown at y %d..%d of %d\n", effect.getParameterInfo(1).name, lane + 1, row.getY(),
                row.getBottom(), view.getHeight());
    // Letting go changes nothing; another parameter takes the row.
    panel.onTouch(1, false);
    DAWLAB_CHECK(calls == 2 && shows(view, track, laneOf(1)));
    panel.onTouch(0, true);
    DAWLAB_CHECK(shows(view, track, laneOf(0)));
    panel.onTouch(2, false); // letting go of another control is no touch
    DAWLAB_CHECK(shows(view, track, laneOf(0)));

    // A track's own Gain, from wherever a fader is grabbed; the other track keeps its lane.
    const engine::TrackId first = session.getTrackId(0);
    const LaneAddress gain{StripTarget::ofTrack(first), engine::EffectId{}, "Gain"};
    DAWLAB_CHECK(follower.touched(gain));
    DAWLAB_CHECK(shows(view, first, gain) && shows(view, track, laneOf(0)));

    // Buses and the master have no rows; a track that is gone neither.
    DAWLAB_CHECK(!follower.touched({StripTarget::of(engine::ChannelRef::bus(0)), engine::EffectId{}, "Gain"}));
    DAWLAB_CHECK(!follower.touched({StripTarget::of(engine::ChannelRef::master()), engine::EffectId{}, "Gain"}));
    DAWLAB_CHECK(!follower.touched({StripTarget::ofTrack(engine::TrackId{9999}), engine::EffectId{}, "Gain"}));

    // The menu item turns it off and on.
    juce::PopupMenu menu;
    follower.addToMenu(menu);
    juce::PopupMenu::MenuItemIterator it(menu);
    const bool hasItem = it.next();
    DAWLAB_CHECK(hasItem && it.getItem().text == "Show Touched Automation" && it.getItem().isTicked);
    if (hasItem && it.getItem().action)
        it.getItem().action();
    DAWLAB_CHECK(!follower.isEnabled());
    panel.onTouch(2, true);
    DAWLAB_CHECK(calls == 5);                      // the app's handler runs regardless
    DAWLAB_CHECK(shows(view, track, laneOf(0)));   // but the row does not follow
    DAWLAB_CHECK(!follower.touched(gain));
    juce::PopupMenu again;
    follower.addToMenu(again);
    juce::PopupMenu::MenuItemIterator it2(again);
    const bool hasItem2 = it2.next();
    DAWLAB_CHECK(hasItem2 && !it2.getItem().isTicked);
}

} // namespace ex8::check

int main() {
    const juce::ScopedJuceInitialiser_GUI juceInit;
    std::printf("Exercise 8 - touch a control, see its automation\n");
    ex8::check::run();
    return dawlab::test::summary("l16_ex8");
}
