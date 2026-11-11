#pragma once
// Exercise 8 - the self-check (main). Included at the end of the starter and of the solution.

#include "app_rig.h"
#include "ex8_quantize_audio.h"

#include <dawlab/Check.h>

#include <cstdio>
#include <random>

namespace ex8::check {

using namespace ex20rig;
using engine::FollowMode;

inline void checkOnsets() {
    AudioBuffer audio(2, 48000);
    addClick(audio, 1000, 0.8f);
    addClick(audio, 10000, 0.5f);
    addClick(audio, 10300, 0.5f); // 6 ms after the one before: the same hit, a rattle
    addClick(audio, 30000, 0.1f); // too quiet for a threshold of 0.25 x 0.8
    for (int k = 0; k < 200; ++k)
        audio.setSample(1, 40000 + k, k == 0 ? -0.6f : 0.0f); // a hit on the second channel only
    const std::vector<std::int64_t> found = findOnsets(audio, 0, 48000, 0.25, 960);
    DAWLAB_CHECK((found == std::vector<std::int64_t>{1000, 10000, 40000}));
    DAWLAB_CHECK((findOnsets(audio, 0, 48000, 0.1, 960) == std::vector<std::int64_t>{1000, 10000, 30000, 40000}));
    DAWLAB_CHECK((findOnsets(audio, 0, 48000, 0.25, 100) == std::vector<std::int64_t>{1000, 10000, 10300, 40000}));
    // A range: its own peak (here 0.5, so the quiet hit at 30000 counts with 0.15), and its start counts as quiet -
    // also in the middle of a hit's tail.
    DAWLAB_CHECK((findOnsets(audio, 10200, 35000, 0.15, 960) == std::vector<std::int64_t>{10300, 30000}));
    DAWLAB_CHECK((findOnsets(audio, 1010, 5000, 0.25, 960) == std::vector<std::int64_t>{1010}));
    DAWLAB_CHECK((findOnsets(audio, 39000, 90000, 0.25, 960) == std::vector<std::int64_t>{40000})); // a range past the end ends at the end
    DAWLAB_CHECK(findOnsets(audio, 41000, 48000, 0.25, 960).empty() && findOnsets(audio, 5000, 5000, 0.25, 960).empty() &&
                 findOnsets(audio, -100, 1001, 0.25, 960) == std::vector<std::int64_t>{1000});
}

// A take of 16 beats played by a drummer at 120 BPM, each hit a few milliseconds early or late; on bar 2.
struct Human {
    engine::TrackId track;
    std::vector<std::int64_t> late; // samples, per hit
};

inline Human playedByHand(Rig& rig) {
    Human h;
    h.track = rig.addTrack();
    std::mt19937 rng(80);
    auto audio = std::make_shared<AudioBuffer>(1, 16 * 24000 + 4000);
    for (int k = 0; k < 16; ++k) {
        const std::int64_t late = (k == 0 ? 0 : 1) * std::uniform_int_distribution<std::int64_t>(480, 1200)(rng) * (std::uniform_int_distribution<int>(0, 1)(rng) ? 1 : -1);
        h.late.push_back(late);
        addClick(*audio, 2000 + k * 24000 + late);
    }
    addClick(*audio, 2000 + 5 * 24000 + h.late[5] + 300, 0.5f); // a rattle 6 ms after the 6th hit: the same hit
    engine::Clip clip = rig.session.makeAudioClip(audio, "drums", 96000 - 2000);
    rig.session.perform(app::edit::SetClips{h.track, {clip}}, "Add clip");
    rig.engine.applyPendingCommandsOffline();
    return h;
}

// The worst distance of the 16 clicks from the beats of `bpm` from bar 2, in samples (a large number if not 16).
inline std::int64_t offBeats(const std::vector<std::int64_t>& clicks, double bpm, std::vector<std::int64_t>* each = nullptr) {
    if (clicks.size() != 16)
        return 1000000;
    std::int64_t worst = 0;
    const double beat = 60.0 / bpm * 48000.0;
    for (size_t k = 0; k < 16; ++k) {
        const std::int64_t d = clicks[k] - static_cast<std::int64_t>(std::llround(4.0 * beat + static_cast<double>(k) * beat));
        if (each != nullptr)
            each->push_back(d);
        worst = std::max<std::int64_t>(worst, std::abs(d));
    }
    return worst;
}

inline void checkQuantize() {
    Rig rig;
    const Human h = playedByHand(rig);
    const std::int64_t before = offBeats(rig.clicksIn(rig.playFromStart(48000 * 12)), 120.0);
    const size_t undo0 = rig.session.getHistory().undoCount();

    // Onto the beats.
    const Quantized q = quantizeClip(rig.session, h.track, 0, 960, 1.0, FollowMode::Varispeed);
    rig.engine.applyPendingCommandsOffline();
    DAWLAB_CHECK(q.ok && q.onsets == 16 && q.markers == 18); // the clip's start, 16 hits, its end
    DAWLAB_CHECK(rig.session.getHistory().undoCount() == undo0 + 1 && rig.session.getHistory().getUndoName() == "Quantize audio");
    const std::int64_t after = offBeats(rig.clicksIn(rig.playFromStart(48000 * 12)), 120.0);
    std::printf("  16 hits played by hand at 120 BPM, up to %.1f ms off the beats; quantized onto quarter notes: %lld samples off\n",
                static_cast<double>(before) / 48.0, static_cast<long long>(after));
    DAWLAB_CHECK(before >= 400 && after <= 2);
    if (!q.ok)
        return; // what follows undoes the quantize

    // The tempo changes afterwards: the hits follow, still on the beats.
    DAWLAB_CHECK(rig.setTempo(96.0));
    const std::int64_t at96 = offBeats(rig.clicksIn(rig.playFromStart(48000 * 14)), 96.0);
    DAWLAB_CHECK(at96 <= 2);
    // Quantized again: nothing to move, and nothing moves.
    const Quantized again = quantizeClip(rig.session, h.track, 0, 960, 1.0, FollowMode::Varispeed);
    rig.engine.applyPendingCommandsOffline();
    // A warped clip's source range is its sourceLength, not its length in the song (which is longer at 96 BPM).
    DAWLAB_CHECK(rig.session.getClips(h.track)[0].follow != nullptr && rig.session.getClips(h.track)[0].follow->sourceLength == 16 * 24000 + 4000 &&
                 rig.session.getClips(h.track)[0].follow->markers.back().source == 16.0 * 24000 + 4000);
    DAWLAB_CHECK(again.ok && again.markers == q.markers && offBeats(rig.clicksIn(rig.playFromStart(48000 * 14)), 96.0) <= 2);
    // Two undo steps take the clip back to a plain clip, where it was.
    DAWLAB_CHECK(rig.main->undo() && rig.main->undo() && rig.main->undo());
    rig.engine.applyPendingCommandsOffline();
    DAWLAB_CHECK(rig.session.getClips(h.track)[0].follow == nullptr && rig.session.getClips(h.track)[0].start == 94000 &&
                 rig.session.getTempoMap().point(0).bpm == 120.0);

    // Half way: every hit half as far from its beat as it was.
    const Quantized half = quantizeClip(rig.session, h.track, 0, 960, 0.5, FollowMode::Varispeed);
    rig.engine.applyPendingCommandsOffline();
    std::vector<std::int64_t> each;
    offBeats(rig.clicksIn(rig.playFromStart(48000 * 12)), 120.0, &each);
    bool halfWay = half.ok && each.size() == 16;
    for (size_t k = 0; halfWay && k < 16; ++k)
        halfWay = std::abs(each[k] - h.late[k] / 2) <= 2;
    DAWLAB_CHECK(halfWay);
    DAWLAB_CHECK(rig.main->undo());
    rig.engine.applyPendingCommandsOffline();

    // Onto eighth notes and stretch: every hit is on a line of the grid (each is less than an eighth off its beat).
    const Quantized stretch = quantizeClip(rig.session, h.track, 0, 480, 1.0, FollowMode::Stretch);
    rig.engine.applyPendingCommandsOffline();
    DAWLAB_CHECK(stretch.ok && engine::followModeOf(rig.session.getClips(h.track)[0]) == FollowMode::Stretch);
    bool onLines = rig.session.getClips(h.track)[0].follow != nullptr;
    if (onLines)
        for (size_t i = 1; i + 1 < rig.session.getClips(h.track)[0].follow->markers.size(); ++i)
            onLines = onLines && std::fmod(rig.session.getClips(h.track)[0].follow->markers[i].tick, 480.0) == 0.0;
    DAWLAB_CHECK(onLines);
}

inline void checkRefused() {
    Rig rig;
    const Human h = playedByHand(rig);
    const size_t undo0 = rig.session.getHistory().undoCount();
    DAWLAB_CHECK(!quantizeClip(rig.session, h.track, 0, 960, 1.0, FollowMode::Off).ok && !quantizeClip(rig.session, h.track, 0, 960, 1.0, FollowMode::Start).ok);
    DAWLAB_CHECK(!quantizeClip(rig.session, h.track, 0, 0, 1.0, FollowMode::Varispeed).ok && !quantizeClip(rig.session, h.track, 0, 960, 1.5, FollowMode::Varispeed).ok &&
                 !quantizeClip(rig.session, h.track, 0, 960, -0.1, FollowMode::Varispeed).ok);
    DAWLAB_CHECK(!quantizeClip(rig.session, h.track, 1, 960, 1.0, FollowMode::Varispeed).ok && !quantizeClip(rig.session, engine::TrackId{999}, 0, 960, 1.0, FollowMode::Varispeed).ok);
    DAWLAB_CHECK(rig.session.getHistory().undoCount() == undo0 && rig.session.getClips(h.track)[0].follow == nullptr);
    // Two hits close to one line of a coarse grid: one marker for them, the first.
    const Quantized coarse = quantizeClip(rig.session, h.track, 0, 3840 * 4, 1.0, FollowMode::Varispeed);
    DAWLAB_CHECK(coarse.ok && coarse.onsets == 16 && coarse.markers < 17 && engine::areWarpMarkers(rig.session.getClips(h.track)[0].follow->markers));
    rig.session.finishRenders();
    rig.engine.applyPendingCommandsOffline();

    // A clip trimmed at its front: its source starts 12000 samples in (past the first hit), and it is where the
    // rest of the take was. The tick of a source sample counts from the clip's start AND its offset.
    Rig trimmed;
    const Human t = playedByHand(trimmed);
    std::vector<engine::Clip> list = trimmed.session.getClips(t.track);
    list[0].sourceOffset = 12000;
    list[0].start += 12000;
    list[0].length -= 12000;
    list[0].fadeInSamples = 0;
    trimmed.session.setClips(t.track, list);
    const Quantized q = quantizeClip(trimmed.session, t.track, 0, 960, 1.0, FollowMode::Varispeed);
    DAWLAB_CHECK(q.ok && q.onsets == 15);
    std::vector<std::int64_t> clicks = trimmed.clicksIn(trimmed.playFromStart(48000 * 12));
    bool onBeats = clicks.size() == 15;
    for (size_t k = 0; onBeats && k < 15; ++k)
        onBeats = std::abs(clicks[k] - (96000 + static_cast<std::int64_t>(k + 1) * 24000)) <= 2;
    DAWLAB_CHECK(onBeats);
}

} // namespace ex8::check

int main() {
    juce::ScopedJuceInitialiser_GUI juce;
    using namespace ex8::check;
    std::printf("Exercise 8 - audio quantize\n");
    checkOnsets();
    checkQuantize();
    checkRefused();
    return dawlab::test::summary("l20_ex8");
}
