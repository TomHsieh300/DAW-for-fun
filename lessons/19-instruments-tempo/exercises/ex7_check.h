#pragma once
// Exercise 7 - the self-check (main). Included at the end of the starter and of the solution.

#include "app_rig.h"
#include "ex7_click_track.h"

#include <dawlab/Check.h>

#include <cstdio>

namespace ex7::check {

using namespace ex19rig;

// Silence up to `sample`, and sound in the 10 ms after it.
inline bool startsAt(const std::vector<float>& x, std::int64_t sample) {
    const size_t at = static_cast<size_t>(sample);
    return peakOf(x, 0, at) == 0.0f && peakOf(x, at, at + 480) > 1.0e-3f;
}

inline void checkTrack() {
    Rig rig;
    const int before = rig.session.getNumTracks();
    const Click click = addClickTrack(*rig.main, 2, 3);
    rig.engine.applyPendingCommandsOffline();
    DAWLAB_CHECK(click.ok && click.notes == 12 && rig.session.getNumTracks() == before + 1);
    DAWLAB_CHECK(rig.session.getHistory().getUndoName() == "Add click track");
    if (!click.ok || rig.session.getNumTracks() != before + 1)
        return;
    DAWLAB_CHECK(rig.session.getTrackId(before) == click.track); // at the end
    const std::vector<MidiClipModel> clips = rig.session.getMidiClips(click.track);
    DAWLAB_CHECK(clips.size() == 1);
    if (clips.size() != 1)
        return;
    const MidiClipModel& clip = clips[0];
    DAWLAB_CHECK(clip.start == 3840 && clip.length == 3 * 3840 && clip.offset == 0 && !clip.muted && clip.id != 0 && clip.name == "Click");
    bool notes = clip.played.size() == 12;
    for (size_t k = 0; notes && k < 12; ++k) {
        const engine::Note want{static_cast<std::int64_t>(k) * 960, 240, k % 4 == 0 ? 84 : 72, k % 4 == 0 ? 127 : 90, 0};
        notes = clip.played[k] == want;
    }
    DAWLAB_CHECK(notes);

    // It plays where bar 2 is: at 120 BPM after 2 s, at 60 BPM after 4 s - nobody moved a note.
    const std::int64_t mix = rig.engine.getLatencySamples(); // heard that much later
    const std::int64_t at120 = 96000 + mix;
    DAWLAB_CHECK(startsAt(rig.playFromStart(at120 + 9600), at120));
    DAWLAB_CHECK((rig.session.setTempoPoints({{0, 60.0, false}}, "Tempo")));
    const std::int64_t at60 = 192000 + mix;
    DAWLAB_CHECK(startsAt(rig.playFromStart(at60 + 9600), at60));
    // A ramp from 60 to 120 over bar 1: bar 2 starts after 4 ln 2 s.
    DAWLAB_CHECK((rig.session.setTempoPoints({{0, 60.0, true}, {3840, 120.0, false}}, "Tempo")));
    const std::int64_t bar2 = rig.engine.getTransport().tickToSample(3840);
    const std::int64_t atRamp = bar2 + mix;
    DAWLAB_CHECK(startsAt(rig.playFromStart(atRamp + 9600), atRamp) && std::llabs(bar2 - std::llround(4.0 * std::log(2.0) * 48000.0)) <= 1);
    std::printf("  three bars of click from bar 2: %d notes; heard from sample %lld (120 BPM), %lld (60 BPM), %lld (a ramp from 60 to 120)\n",
                click.notes, static_cast<long long>(at120), static_cast<long long>(at60), static_cast<long long>(atRamp));

    // One undo step for the track and its clip.
    DAWLAB_CHECK(rig.session.undo() && rig.session.undo() && rig.session.getNumTracks() == before + 1); // the two tempos
    DAWLAB_CHECK(rig.session.undo() && rig.session.getNumTracks() == before);
    DAWLAB_CHECK(rig.session.redo() && rig.session.getNumTracks() == before + 1 && rig.session.getMidiClips(click.track).size() == 1);
    rig.engine.applyPendingCommandsOffline();
}

inline void checkRefused() {
    Rig rig;
    const int before = rig.session.getNumTracks();
    DAWLAB_CHECK(!addClickTrack(*rig.main, 0, 4).ok && !addClickTrack(*rig.main, 1, 0).ok && !addClickTrack(*rig.main, 1, 1000).ok);
    DAWLAB_CHECK(rig.session.getNumTracks() == before && !rig.session.getHistory().canUndo());
    // One bar, the longest, and then tracks until the project is full: refused, and nothing is left behind.
    DAWLAB_CHECK(addClickTrack(*rig.main, 1, 1).notes == 4 && addClickTrack(*rig.main, 5, 999).notes == 4 * 999);
    int added = 2;
    while (added < 200 && addClickTrack(*rig.main, 1, 1).ok)
        ++added;
    const int full = rig.session.getNumTracks();
    const Click none = addClickTrack(*rig.main, 1, 1);
    DAWLAB_CHECK(added < 200 && !none.ok && none.notes == 0 && rig.session.getNumTracks() == full);
    rig.engine.applyPendingCommandsOffline();
    std::printf("  refused: bar 0, no bars, 1000 bars, and track %d of a project that holds %d\n", full + 1, full);
}

} // namespace ex7::check

int main() {
    juce::ScopedJuceInitialiser_GUI juce;
    using namespace ex7::check;
    std::printf("Exercise 7 - a click track in DawLab 19\n");
    checkTrack();
    checkRefused();
    return dawlab::test::summary("l19_ex7");
}
