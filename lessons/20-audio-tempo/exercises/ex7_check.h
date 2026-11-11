#pragma once
// Exercise 7 - the self-check (main). Included at the end of the starter and of the solution.

#include "app_rig.h"
#include "ex7_follow_as_recorded.h"

#include <dawlab/Check.h>

#include <cstdio>

namespace ex7::check {

using namespace ex20rig;
using engine::FollowMode;

constexpr std::int64_t kBeat120 = 24000;

// A take of 16 beats played at 120 BPM: a click on every beat, the first 1000 samples into the audio, recorded so
// that the first click is on bar 2 at 120 BPM (sample 96000). It is a recording: the session knows its takes.
inline engine::TrackId recordTake(Rig& rig, std::int64_t firstClickAt = 96000) {
    const engine::TrackId track = rig.addTrack();
    auto audio = std::make_shared<AudioBuffer>(1, static_cast<int>(16 * kBeat120 + 2000));
    for (int k = 0; k < 16; ++k)
        addClick(*audio, 1000 + k * kBeat120);
    const std::vector<engine::Placement> takes{{firstClickAt - 1000, 0, audio->getNumSamples()}};
    rig.session.registerTakes(audio, takes);
    rig.session.perform(rig.session.makeTakeEdit(track, audio, takes, "Take"), "Record");
    rig.engine.applyPendingCommandsOffline();
    return track;
}

// How far the clicks are from the beats of `bpm` from bar 2 on (samples), the worst of 16; a large number if
// there are not 16.
inline std::int64_t offBeats(const std::vector<std::int64_t>& clicks, double bpm) {
    if (clicks.size() != 16)
        return 1000000;
    std::int64_t worst = 0;
    const double beat = 60.0 / bpm * 48000.0;
    for (size_t k = 0; k < 16; ++k)
        worst = std::max<std::int64_t>(worst, std::abs(clicks[k] - static_cast<std::int64_t>(std::llround(4.0 * beat + static_cast<double>(k) * beat))));
    return worst;
}

inline void checkTheWrongMap() {
    // What the app does: the tempo changes to 90, then following is turned on - markers of the map of now. The
    // take is made to stay where 90 BPM left it, and is off the beat.
    Rig rig;
    const engine::TrackId track = recordTake(rig);
    DAWLAB_CHECK(rig.session.getTakesOf(rig.session.getClips(track)[0]) != nullptr);
    DAWLAB_CHECK(rig.setTempo(90.0) && rig.session.getClips(track)[0].start == 95000);
    DAWLAB_CHECK(rig.main->setClipFollow(track, 0, FollowMode::Varispeed));
    const std::vector<std::int64_t> clicks = rig.clicksIn(rig.playFromStart(48000 * 14));
    const std::int64_t off = offBeats(clicks, 90.0);
    std::printf("  recorded at 120, the tempo set to 90, then the app's \"follow\": %zu clicks, up to %.0f ms off the beats of 90\n", clicks.size(),
                static_cast<double>(off) / 48.0);
    DAWLAB_CHECK(off > 4800);
}

inline void checkAsRecorded() {
    Rig rig;
    const engine::TrackId track = recordTake(rig);
    engine::TempoMap at120;
    at120.set({{0, 120.0, false}});
    DAWLAB_CHECK(rig.setTempo(90.0));
    const size_t undo0 = rig.session.getHistory().undoCount();

    const Followed f = followAsRecorded(rig.session, track, FollowMode::Varispeed, at120);
    rig.engine.applyPendingCommandsOffline();
    DAWLAB_CHECK(f.ok && f.clips == 1);
    DAWLAB_CHECK(rig.session.getHistory().undoCount() == undo0 + 1 && rig.session.getHistory().getUndoName() == "Takes follow the tempo");
    const engine::Clip& clip = rig.session.getClips(track)[0];
    DAWLAB_CHECK(engine::followModeOf(clip) == FollowMode::Varispeed && clip.follow->sourceLength == 16 * kBeat120 + 2000);
    // At 90 BPM the take is a third longer, and starts where its first 1000 samples end on bar 2 under 90.
    DAWLAB_CHECK(std::abs(clip.start - (128000 - 1333)) <= 1 && std::abs(clip.length - (16 * 32000 + 2667)) <= 2);
    const std::vector<std::int64_t> clicks = rig.clicksIn(rig.playFromStart(48000 * 14));
    const std::int64_t off = offBeats(clicks, 90.0);
    std::printf("  the same take following as it was recorded (over 120): %zu clicks, at most %lld samples off the beats of 90\n", clicks.size(),
                static_cast<long long>(off));
    DAWLAB_CHECK(off <= 2);

    // One undo step takes it back: the clip stays again, where it was recorded.
    DAWLAB_CHECK(f.clips == 1 && rig.main->undo() && rig.session.getClips(track)[0].follow == nullptr && rig.session.getClips(track)[0].start == 95000);
    rig.engine.applyPendingCommandsOffline();

    // Stretch: the same markers, the same place.
    const Followed s = followAsRecorded(rig.session, track, FollowMode::Stretch, at120);
    rig.engine.applyPendingCommandsOffline();
    DAWLAB_CHECK(s.ok && s.clips == 1 && engine::followModeOf(rig.session.getClips(track)[0]) == FollowMode::Stretch);
    DAWLAB_CHECK(rig.session.getClips(track)[0].follow != nullptr &&
                 rig.session.getClips(track)[0].follow->markers == engine::markersForTake(at120, 48000.0, 95000, 0, 16 * kBeat120 + 2000));
    DAWLAB_CHECK(std::abs(rig.session.getClips(track)[0].start - 126667) <= 1);
    if (s.clips == 1)
        DAWLAB_CHECK(rig.main->undo());
    rig.engine.applyPendingCommandsOffline();

    // Start: its start tick is that of where it was recorded, under the map it was recorded over.
    const Followed st = followAsRecorded(rig.session, track, FollowMode::Start, at120);
    rig.engine.applyPendingCommandsOffline();
    const engine::Clip& c = rig.session.getClips(track)[0];
    DAWLAB_CHECK(st.ok && st.clips == 1 && engine::followModeOf(c) == FollowMode::Start && std::fabs(c.follow->startTick - 3800.0) < 1e-6);
    DAWLAB_CHECK(std::abs(c.start - 126667) <= 1 && c.length == 16 * kBeat120 + 2000);
}

inline void checkWhichClips() {
    Rig rig;
    const engine::TrackId track = recordTake(rig);
    // Another take later on the same track, and a clip that is not a recording.
    auto second = std::make_shared<AudioBuffer>(1, 50000);
    addClick(*second, 1000);
    const std::vector<engine::Placement> takes{{600000, 0, 50000}};
    rig.session.registerTakes(second, takes);
    rig.session.perform(rig.session.makeTakeEdit(track, second, takes, "Take 2"), "Record");
    auto file = std::make_shared<AudioBuffer>(1, 30000);
    std::vector<engine::Clip> list = rig.session.getClips(track);
    list.push_back(rig.session.makeAudioClip(file, "file", 900000));
    rig.session.setClips(track, list);
    rig.engine.applyPendingCommandsOffline();
    DAWLAB_CHECK(rig.session.getClips(track).size() == 3);

    engine::TempoMap at120;
    at120.set({{0, 120.0, false}});
    const size_t undo0 = rig.session.getHistory().undoCount();
    const Followed f = followAsRecorded(rig.session, track, FollowMode::Varispeed, at120);
    DAWLAB_CHECK(f.ok && f.clips == 2 && rig.session.getHistory().undoCount() == undo0 + 1);
    int following = 0;
    for (const engine::Clip& c : rig.session.getClips(track))
        following += c.follow != nullptr ? 1 : 0;
    DAWLAB_CHECK(following == 2 && rig.session.getClips(track).size() == 3);
    // Again: nothing left to change - ok, and no undo step.
    const Followed again = followAsRecorded(rig.session, track, FollowMode::Varispeed, at120);
    DAWLAB_CHECK(again.ok && again.clips == 0 && rig.session.getHistory().undoCount() == undo0 + 1);
    // Refused.
    DAWLAB_CHECK(!followAsRecorded(rig.session, track, FollowMode::Off, at120).ok);
    DAWLAB_CHECK(!followAsRecorded(rig.session, engine::TrackId{9999}, FollowMode::Varispeed, at120).ok);
    DAWLAB_CHECK(rig.session.getHistory().undoCount() == undo0 + 1);
    rig.session.finishRenders();
    rig.engine.applyPendingCommandsOffline();

    // A take that plays from 3000 samples into its recording (a later pass of a loop does): its markers start there.
    Rig other;
    const engine::TrackId t = other.addTrack();
    auto audio = std::make_shared<AudioBuffer>(1, 60000);
    const std::vector<engine::Placement> pass{{48000, 3000, 50000}};
    other.session.registerTakes(audio, pass);
    other.session.perform(other.session.makeTakeEdit(t, audio, pass, "Take"), "Record");
    const Followed f2 = followAsRecorded(other.session, t, FollowMode::Varispeed, at120);
    const engine::Clip& c = other.session.getClips(t)[0];
    DAWLAB_CHECK(f2.ok && f2.clips == 1 && c.sourceOffset == 3000 && c.follow != nullptr && c.follow->markers.front().source == 3000.0 &&
                 c.follow->markers.back().source == 53000.0 && c.start == 48000);
    other.session.finishRenders();
    other.engine.applyPendingCommandsOffline();
}

} // namespace ex7::check

int main() {
    juce::ScopedJuceInitialiser_GUI juce;
    using namespace ex7::check;
    std::printf("Exercise 7 - takes that follow as they were recorded\n");
    checkTheWrongMap();
    checkAsRecorded();
    checkWhichClips();
    return dawlab::test::summary("l20_ex7");
}
