#pragma once
// Exercise 9 - the self-check (main). Included at the end of the starter and of the solution.

#include "app_rig.h"
#include "ex9_loop_fits.h"

#include <dawlab/Check.h>

#include <cstdio>

namespace ex9::check {

using namespace ex20rig;
using engine::FollowMode;

// A loop of `bars` bars made at `bpm`: a click on every beat, the first on its first sample.
inline std::shared_ptr<const AudioBuffer> loopAt(double bpm, int bars) {
    const double beat = 60.0 / bpm * 48000.0;
    auto audio = std::make_shared<AudioBuffer>(1, static_cast<int>(std::llround(4 * bars * beat)));
    for (int k = 0; k < 4 * bars; ++k)
        addClick(*audio, std::llround(k * beat));
    return audio;
}

inline void checkGuess() {
    // At 120 BPM: loops made at other tempos, of one, two and four bars.
    struct Case {
        double madeAt;
        int bars, beats;
    };
    int right = 0;
    for (const Case c : {Case{90, 2, 8}, Case{100, 1, 4}, Case{128, 4, 16}, Case{140, 2, 8}, Case{85, 4, 16}, Case{150, 1, 4}, Case{120, 8, 32}}) {
        const std::int64_t length = std::llround(4 * c.bars * 60.0 / c.madeAt * 48000.0);
        right += guessBeats(length, 48000.0, 120.0) == c.beats ? 1 : 0;
    }
    DAWLAB_CHECK(right == 7);
    // Double time: 4 bars at 170 BPM are as long as 2 bars at 85.
    DAWLAB_CHECK(guessBeats(std::llround(16 * 60.0 / 170.0 * 48000.0), 48000.0, 120.0) == 8);
    // As a ratio: 5.6 beats is closer to 4 than to 8 (5.66 is half way), 5.7 is closer to 8.
    DAWLAB_CHECK(guessBeats(std::llround(5.6 * 24000), 48000.0, 120.0) == 4 && guessBeats(std::llround(5.7 * 24000), 48000.0, 120.0) == 8);
    // Right at the middle, sqrt(32) = 5.657 beats: one sample less is 4, one sample more is 8.
    const std::int64_t middle = static_cast<std::int64_t>(std::floor(std::sqrt(32.0) * 24000));
    DAWLAB_CHECK(guessBeats(middle, 48000.0, 120.0) == 4 && guessBeats(middle + 1, 48000.0, 120.0) == 8);
    DAWLAB_CHECK(guessBeats(10, 48000.0, 120.0) == 1 && guessBeats(48000LL * 1000, 48000.0, 120.0) == 256);
    DAWLAB_CHECK(guessBeats(0, 48000.0, 120.0) == 0 && guessBeats(1000, 0.0, 120.0) == 0 && guessBeats(1000, 48000.0, 0.0) == 0);
}

inline void checkDrop() {
    Rig rig;
    const engine::TrackId track = rig.addTrack();
    const auto loop = loopAt(128.0, 4); // 7.5 s, 16 beats
    const size_t undo0 = rig.session.getHistory().undoCount();
    const Dropped d = dropLoop(rig.session, track, loop, 2, FollowMode::Varispeed);
    rig.engine.applyPendingCommandsOffline();
    DAWLAB_CHECK(d.ok && d.beats == 16 && d.clipIndex == 0);
    DAWLAB_CHECK(rig.session.getHistory().undoCount() == undo0 + 1 && rig.session.getHistory().getUndoName() == "Add loop");
    if (!d.ok || rig.session.getClips(track).size() != 1)
        return;
    const engine::Clip& c = rig.session.getClips(track)[0];
    DAWLAB_CHECK(c.start == 96000 && c.length == 16 * 24000 && c.fadeInSamples == 0 && c.fadeOutSamples == 240 && c.source == loop);
    DAWLAB_CHECK(engine::followModeOf(c) == FollowMode::Varispeed && c.follow->sourceLength == loop->getNumSamples());

    const std::vector<std::int64_t> clicks = rig.clicksIn(rig.playFromStart(48000 * 11));
    std::int64_t worst = clicks.size() == 16 ? 0 : 1000000;
    for (size_t k = 0; k < clicks.size() && k < 16; ++k)
        worst = std::max<std::int64_t>(worst, std::abs(clicks[k] - (96000 + static_cast<std::int64_t>(k) * 24000)));
    std::printf("  a loop of 7.5 s (4 bars at 128 BPM) dropped on bar 2 at 120 BPM: taken as %d beats, %zu clicks, at most %lld samples off the beats\n",
                d.beats, clicks.size(), static_cast<long long>(worst));
    DAWLAB_CHECK(worst <= 2);

    // The tempo changes: the loop follows, still bars 2 to 5.
    DAWLAB_CHECK(rig.setTempo(100.0));
    DAWLAB_CHECK(rig.session.getClips(track)[0].start == 115200 && rig.session.getClips(track)[0].length == 16 * 28800);
    // The tempo where the loop goes, not at the song's start: 120, and 60 from bar 3. A bar at 60 BPM is 4 s.
    Rig two;
    const engine::TrackId t2 = two.addTrack();
    DAWLAB_CHECK(two.session.setTempoPoints({{0, 120.0, false}, {7680, 60.0, false}}, "Tempo"));
    const Dropped slow = dropLoop(two.session, t2, loopAt(60.0, 1), 3, FollowMode::Varispeed);
    DAWLAB_CHECK(slow.ok && slow.beats == 4 && two.session.getClips(t2)[0].start == 192000 && two.session.getClips(t2)[0].length == 192000);
    two.session.finishRenders();
    two.engine.applyPendingCommandsOffline();

    // One undo step, two: the tempo, then the loop.
    DAWLAB_CHECK(rig.main->undo() && rig.main->undo() && rig.session.getClips(track).empty());
    rig.engine.applyPendingCommandsOffline();
}

inline void checkOverOthers() {
    Rig rig;
    const engine::TrackId track = rig.addTrack();
    // A long clip already on the track; a stretched loop of 2 bars on bar 3 cuts it in two.
    auto bed = std::make_shared<AudioBuffer>(1, 48000 * 12);
    rig.session.perform(app::edit::SetClips{track, {rig.session.makeAudioClip(bed, "bed", 0)}}, "Add clip");
    const Dropped d = dropLoop(rig.session, track, loopAt(90.0, 2), 3, FollowMode::Stretch);
    rig.engine.applyPendingCommandsOffline();
    DAWLAB_CHECK(d.ok && d.beats == 8 && d.clipIndex == 1 && rig.session.getClips(track).size() == 3);
    if (rig.session.getClips(track).size() == 3) {
        const auto& clips = rig.session.getClips(track);
        DAWLAB_CHECK(clips[0].end() == 192000 && clips[1].start == 192000 && clips[1].end() == 384000 && clips[2].start == 384000);
        DAWLAB_CHECK(engine::followModeOf(clips[1]) == FollowMode::Stretch && clips[0].follow == nullptr && clips[2].follow == nullptr);
    }
    // Refused, and nothing changes.
    const size_t undo = rig.session.getHistory().undoCount();
    DAWLAB_CHECK(!dropLoop(rig.session, track, loopAt(90.0, 2), 3, FollowMode::Off).ok && !dropLoop(rig.session, track, loopAt(90.0, 2), 3, FollowMode::Start).ok);
    DAWLAB_CHECK(!dropLoop(rig.session, track, loopAt(90.0, 2), 0, FollowMode::Varispeed).ok && !dropLoop(rig.session, track, nullptr, 3, FollowMode::Varispeed).ok);
    DAWLAB_CHECK(!dropLoop(rig.session, track, std::make_shared<AudioBuffer>(1, 0), 3, FollowMode::Varispeed).ok &&
                 !dropLoop(rig.session, engine::TrackId{999}, loopAt(90.0, 2), 3, FollowMode::Varispeed).ok);
    DAWLAB_CHECK(rig.session.getHistory().undoCount() == undo && rig.session.getClips(track).size() == 3);
    rig.session.finishRenders();
    rig.engine.applyPendingCommandsOffline();
}

} // namespace ex9::check

int main() {
    juce::ScopedJuceInitialiser_GUI juce;
    using namespace ex9::check;
    std::printf("Exercise 9 - a loop that fits\n");
    checkGuess();
    checkDrop();
    checkOverOthers();
    return dawlab::test::summary("l20_ex9");
}
