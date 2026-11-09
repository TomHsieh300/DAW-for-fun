#pragma once
// Exercise 7 - the self-check (main). Included at the end of the starter and of the solution.

#include "ex7_reorder_tracks.h"

#include "MainComponent.h"

#include <dawlab/Check.h>

#include <cstdio>
#include <vector>

namespace ex7::check {

using namespace dawlab;
using namespace dawlab::app;
using engine::TrackId;

inline std::vector<TrackId> order(const Session& s) {
    std::vector<TrackId> ids;
    for (int i = 0; i < s.getNumTracks(); ++i)
        ids.push_back(s.getTrackId(i));
    return ids;
}

// Everything a track has, compared field by field (the clips by position and by audio buffer).
inline bool sameTrack(const TrackSnapshot& a, const TrackSnapshot& b) {
    if (a.id != b.id || a.format != b.format || a.name != b.name || a.gainDb != b.gainDb || a.pan != b.pan ||
        a.mute != b.mute || a.solo != b.solo || a.clips.size() != b.clips.size())
        return false;
    for (size_t i = 0; i < a.sends.size(); ++i)
        if (a.sends[i].levelDb != b.sends[i].levelDb || a.sends[i].preFader != b.sends[i].preFader)
            return false;
    for (size_t i = 0; i < a.inserts.size(); ++i) {
        const InsertSnapshot &x = a.inserts[i], &y = b.inserts[i];
        if (x.kind != y.kind || x.type != y.type || x.params != y.params || x.bypassed != y.bypassed)
            return false;
    }
    for (size_t i = 0; i < a.clips.size(); ++i)
        if (a.clips[i].start != b.clips[i].start || a.clips[i].length != b.clips[i].length || a.clips[i].source != b.clips[i].source)
            return false;
    return true;
}

inline juce::MouseEvent mouseAt(ArrangementView& view, int x, int y, juce::Point<float> down) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), {static_cast<float>(x), static_cast<float>(y)},
                            juce::ModifierKeys::leftButtonModifier, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &view, &view,
                            juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), 1, false);
}

// A press, drags through `ys`, and a release, sent to the arrangement and to the dragger as a mouse
// listener would get them.
inline void drag(ArrangementView& view, TrackDragger& dragger, int x, int fromY, const std::vector<int>& ys) {
    const juce::Point<float> down{static_cast<float>(x), static_cast<float>(fromY)};
    const juce::MouseEvent press = mouseAt(view, x, fromY, down);
    view.mouseDown(press);
    dragger.mouseDown(press);
    for (const int y : ys) {
        const juce::MouseEvent move = mouseAt(view, x, y, down);
        view.mouseDrag(move);
        dragger.mouseDrag(move);
    }
    const juce::MouseEvent release = mouseAt(view, x, ys.empty() ? fromY : ys.back(), down);
    view.mouseUp(release);
    dragger.mouseUp(release);
}

inline void run() {
    MainComponent main(MainComponent::Mode::Offline);
    main.setSize(MainComponent::kDefaultWidth, MainComponent::kDefaultHeight);
    DAWLAB_CHECK(main.loadDemoSongNow() == 0);
    main.simulatePlayback(512);
    Session& session = main.getSession();
    const std::vector<TrackId> ids = order(session);
    DAWLAB_CHECK(ids.size() >= 6);
    if (ids.size() < 6)
        return;
    const TrackSnapshot bass = session.snapshotTrack(ids[1]);
    const std::size_t entries = session.getHistory().undoCount();

    // ---- moveTrack --------------------------------------------------------------------------
    DAWLAB_CHECK(moveTrack(session, ids[1], 3));
    main.simulatePlayback(512); // the engine applies the commands
    main.tick();
    std::vector<TrackId> expected = ids;
    expected.erase(expected.begin() + 1);
    expected.insert(expected.begin() + 3, ids[1]);
    DAWLAB_CHECK(order(session) == expected);
    DAWLAB_CHECK(sameTrack(session.snapshotTrack(ids[1]), bass)); // the same id, everything kept
    DAWLAB_CHECK(session.getHistory().undoCount() == entries + 1);
    DAWLAB_CHECK(session.getHistory().getUndoName() == "Move Track");
    DAWLAB_CHECK(session.getEngine().getNumTracks() == static_cast<int>(ids.size()));
    DAWLAB_CHECK(session.trackRef(ids[1]).trackId == ids[1]);

    DAWLAB_CHECK(main.undo());
    main.simulatePlayback(512);
    DAWLAB_CHECK(order(session) == ids);
    DAWLAB_CHECK(sameTrack(session.snapshotTrack(ids[1]), bass));
    DAWLAB_CHECK(main.redo());
    main.simulatePlayback(512);
    DAWLAB_CHECK(order(session) == expected);
    DAWLAB_CHECK(main.undo());
    main.simulatePlayback(512);

    // Nothing to do: no entry.
    DAWLAB_CHECK(!moveTrack(session, ids[2], 2));
    DAWLAB_CHECK(!moveTrack(session, TrackId{9999}, 0));
    DAWLAB_CHECK(session.getHistory().undoCount() == entries);
    // Clamped.
    DAWLAB_CHECK(moveTrack(session, ids[0], 99));
    DAWLAB_CHECK(session.getTrackId(session.getNumTracks() - 1) == ids[0]);
    DAWLAB_CHECK(moveTrack(session, ids[0], -5));
    DAWLAB_CHECK(order(session) == ids);
    main.simulatePlayback(512);
    main.tick();

    // ---- dragging -----------------------------------------------------------------------------
    ArrangementView& view = main.getArrangement();
    view.setScrollY(0);
    TrackDragger dragger(view, session);
    const int x = ArrangementView::kHeaderWidth / 3;
    auto centre = [&view](int lane) { return view.getLaneBounds(lane).getCentreY(); };
    const std::size_t before = session.getHistory().undoCount();

    // A click is a click: it selects and moves nothing - even 3 pixels across the edge of a lane.
    const int edge = view.getLaneBounds(0).getBottom() - 2;
    drag(view, dragger, x, edge, {edge + 3});
    DAWLAB_CHECK(order(session) == ids);
    DAWLAB_CHECK(view.getSelectedTrack() == ids[0]);
    DAWLAB_CHECK(session.getHistory().undoCount() == before);

    // Lane 0 dragged onto lane 2.
    const juce::Point<float> down{static_cast<float>(x), static_cast<float>(centre(0))};
    view.mouseDown(mouseAt(view, x, centre(0), down));
    dragger.mouseDown(mouseAt(view, x, centre(0), down));
    dragger.mouseDrag(mouseAt(view, x, centre(1), down));
    DAWLAB_CHECK(dragger.getDropIndex() == 1); // where the line would be drawn
    dragger.mouseDrag(mouseAt(view, x, centre(2), down));
    DAWLAB_CHECK(dragger.getDropIndex() == 2);
    dragger.mouseUp(mouseAt(view, x, centre(2), down));
    DAWLAB_CHECK(dragger.getDropIndex() == -1);
    main.simulatePlayback(512);
    DAWLAB_CHECK(session.getTrackId(2) == ids[0] && session.getTrackId(0) == ids[1]);
    DAWLAB_CHECK(view.getSelectedTrack() == ids[0]); // the selection follows the id
    DAWLAB_CHECK(session.getHistory().undoCount() == before + 1);
    DAWLAB_CHECK(main.undo());
    main.simulatePlayback(512);
    DAWLAB_CHECK(order(session) == ids);

    // Up into the ruler: to the top.
    drag(view, dragger, x, centre(3), {centre(2), centre(1), ArrangementView::kRulerHeight / 2});
    main.simulatePlayback(512);
    DAWLAB_CHECK(session.getTrackId(0) == ids[3]);
    DAWLAB_CHECK(main.undo());
    main.simulatePlayback(512);

    // A press on a clip is not a header press.
    drag(view, dragger, ArrangementView::kHeaderWidth + 300, centre(4), {centre(1)});
    main.simulatePlayback(512);
    DAWLAB_CHECK(session.indexOf(ids[4]) == 4);
    std::printf("  dragged and moved: %d undo entries, the order is back: %s\n", static_cast<int>(session.getHistory().undoCount()),
                order(session) == ids ? "yes" : "no");
}

} // namespace ex7::check

int main() {
    const juce::ScopedJuceInitialiser_GUI juceInit;
    std::printf("Exercise 7 - reorder tracks by dragging\n");
    ex7::check::run();
    return dawlab::test::summary("l15_ex7");
}
