// Exercise 10 (starter): comping in the app.
//
// Fill in the TODOs. Until then the self-check prints FAIL (expected). The solution is
// solutions/ex10_comping_ui.cpp. What each function must do is in ../ex10_comping_ui.h.
// Build with -Juce: .\scripts\build.ps1 -Only 17-recording -Juce

#include "../ex10_comping_ui.h"

#include <cmath>

using namespace dawlab;
using namespace dawlab::app;

namespace ex10 {

int Comper::takeOf(const engine::Clip& clip, const std::vector<Placement>& takes) {
    // TODO: the first take with the same offset-to-position relation that contains the clip; else -1.
    (void)clip;
    (void)takes;
    return -1;
}

Comp Comper::compOf(engine::TrackId track, int clipIndex) const {
    // TODO: Session::getTakesOf() of the clip; then every clip of the track with the same source (the list is
    //       sorted by start) becomes a segment with takeOf() as its take.
    (void)session_;
    (void)track;
    (void)clipIndex;
    return {};
}

bool Comper::choose(engine::TrackId track, int clipIndex, std::int64_t from, std::int64_t to, int take) {
    // TODO: compOf(); the range within the take's (nothing to do if empty); chooseTake(); nothing to do if the
    //       comp stays the same. Then the
    //       track's list without the recording's clips, plus one engine::Clip per compClips() piece (the two
    //       ends fade over kEdgeFadeSeconds, at most half the clip), sorted by start, as
    //       session_.perform(edit::SetClips{...}, "Comp Take N").
    (void)track;
    (void)clipIndex;
    (void)from;
    (void)to;
    (void)take;
    return false;
}

void Comper::addToMenu(juce::PopupMenu& menu, engine::TrackId track, int clipIndex) {
    // TODO: for a clip with a comp, one juce::PopupMenu::Item per take, enabled when the loop's locators make
    //       a range, whose action choose()s that take between them.
    (void)menu;
    (void)track;
    (void)clipIndex;
}

} // namespace ex10

#include "../ex10_check.h"
