#pragma once
// Exercise 8 - the self-check (main). Included at the end of the starter and of the solution.

#include "app_rig.h"
#include "ex8_tap_tempo.h"

#include <dawlab/Check.h>

#include <cstdio>
#include <random>

namespace ex8::check {

using namespace ex19rig;
using engine::TempoPoint;

inline bool samePoints(const std::vector<TempoPoint>& a, const std::vector<TempoPoint>& b) {
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].tick != b[i].tick || a[i].bpm != b[i].bpm || a[i].rampToNext != b[i].rampToNext)
            return false;
    return true;
}

inline void checkTap() {
    Rig rig;
    const std::vector<TempoPoint> song{{0, 120.0, true}, {3840, 90.0, false}, {7680, 100.0, false}};
    DAWLAB_CHECK(rig.session.setTempoPoints(song, "Song"));

    // Taps 0.48 s apart, one of them missed: 125 BPM for the first point, and the others stay.
    DAWLAB_CHECK((applyTapTempo(rig.session, {5.0, 5.48, 5.96, 6.92, 7.40, 7.88})));
    std::vector<TempoPoint> want = song;
    want[0].bpm = 125.0;
    DAWLAB_CHECK(samePoints(rig.tempoPoints(), want));
    DAWLAB_CHECK(rig.session.getHistory().getUndoName() == "Tap tempo");
    // Rounded to a tenth: 0.47 s is 127.66 BPM.
    DAWLAB_CHECK((applyTapTempo(rig.session, {0.0, 0.47, 0.94})));
    DAWLAB_CHECK_NEAR(rig.session.getTempoMap().point(0).bpm, 127.7, 1e-9);
    std::printf("  taps 0.48 s apart with one missed: %.1f BPM; 0.47 s apart: %.1f BPM\n", want[0].bpm, rig.session.getTempoMap().point(0).bpm);
    // The same tempo again: fine, and nothing new to undo - two undos lead back to the song.
    DAWLAB_CHECK((applyTapTempo(rig.session, {1.0, 1.47})));
    DAWLAB_CHECK(rig.session.undo() && samePoints(rig.tempoPoints(), want));
    DAWLAB_CHECK(rig.session.undo() && samePoints(rig.tempoPoints(), song));

    // Refused.
    const juce::String name = rig.session.getHistory().getUndoName();
    DAWLAB_CHECK((!applyTapTempo(rig.session, {}) && !applyTapTempo(rig.session, {1.0})));
    DAWLAB_CHECK((!applyTapTempo(rig.session, {0.0, 0.01}) && !applyTapTempo(rig.session, {0.0, 5.0}))); // 6000 BPM, 12 BPM
    DAWLAB_CHECK(samePoints(rig.tempoPoints(), song) && rig.session.getHistory().getUndoName() == name);
    // The limits themselves.
    DAWLAB_CHECK((applyTapTempo(rig.session, {0.0, 3.0}) && rig.session.getTempoMap().point(0).bpm == 20.0));
    DAWLAB_CHECK((applyTapTempo(rig.session, {0.0, 60.0 / 999.0}) && rig.session.getTempoMap().point(0).bpm == 999.0));
    rig.engine.applyPendingCommandsOffline();
}

// How far the song's beats are from where they were heard, at most (samples).
inline std::int64_t worstBeat(Rig& rig, const std::vector<double>& seconds) {
    std::int64_t worst = 0;
    for (size_t k = 0; k < seconds.size(); ++k) {
        const std::int64_t want = std::llround((seconds[k] - seconds[0]) * MainComponent::kOfflineSampleRate);
        worst = std::max<std::int64_t>(worst, std::llabs(rig.engine.getTransport().tickToSample(static_cast<std::int64_t>(k) * 960) - want));
    }
    return worst;
}

inline void checkFollow() {
    Rig rig;
    const std::vector<TempoPoint> before = rig.tempoPoints();
    // A band that speeds up: 0.6, 0.6, 0.5, 0.5, 0.4 s apart.
    const std::vector<double> band{10.0, 10.6, 11.2, 11.7, 12.2, 12.6};
    DAWLAB_CHECK(followBeats(rig.session, band));
    const std::vector<TempoPoint> points = rig.tempoPoints();
    DAWLAB_CHECK(points.size() == 3 && rig.session.getHistory().getUndoName() == "Tempo from beats");
    if (points.size() == 3) {
        DAWLAB_CHECK(points[0].tick == 0 && points[1].tick == 1920 && points[2].tick == 3840);
        DAWLAB_CHECK_NEAR(points[0].bpm, 100.0, 1e-9);
        DAWLAB_CHECK_NEAR(points[2].bpm, 150.0, 1e-9);
    }
    DAWLAB_CHECK(worstBeat(rig, band) == 0);
    std::printf("  beats 0.6, 0.6, 0.5, 0.5, 0.4 s apart: %d tempo points, beat 6 at sample %lld\n", rig.session.getTempoMap().size(),
                static_cast<long long>(rig.engine.getTransport().tickToSample(5 * 960)));

    // Refused: the map and the history stay.
    DAWLAB_CHECK((!followBeats(rig.session, {}) && !followBeats(rig.session, {1.0}) && !followBeats(rig.session, {1.0, 2.0, 2.0})));
    std::vector<double> wild{0.0}; // 300 beats that all differ: more points than the engine's map holds
    for (int k = 0; k < 300; ++k)
        wild.push_back(wild.back() + 0.4 + 0.001 * k);
    DAWLAB_CHECK(!followBeats(rig.session, wild));
    DAWLAB_CHECK(samePoints(rig.tempoPoints(), points) && rig.session.getHistory().getUndoName() == "Tempo from beats");
    wild.resize(257); // 256 tempos: just fits
    DAWLAB_CHECK(followBeats(rig.session, wild) && rig.session.getTempoMap().size() == 256 && worstBeat(rig, wild) <= 1);
    // One undo step each.
    DAWLAB_CHECK(rig.session.undo() && samePoints(rig.tempoPoints(), points));
    DAWLAB_CHECK(rig.session.undo() && samePoints(rig.tempoPoints(), before) && !rig.session.getHistory().canUndo());

    // Random bands: every beat within a sample of where it was heard.
    std::mt19937 rng(8);
    int refused = 0, off = 0;
    const int runs = 100;
    for (int run = 0; run < runs; ++run) {
        std::vector<double> seconds{std::uniform_real_distribution<double>(0.0, 100.0)(rng)};
        for (int tempos = 0; tempos < 40 && seconds.size() < 200; ++tempos) {
            const double interval = 60.0 / std::uniform_real_distribution<double>(50.0, 220.0)(rng);
            for (int i = 0, n = std::uniform_int_distribution<int>(1, 12)(rng); i < n; ++i)
                seconds.push_back(seconds.back() + interval);
        }
        if (!followBeats(rig.session, seconds)) {
            ++refused;
            continue;
        }
        off += worstBeat(rig, seconds) <= 1 ? 0 : 1;
    }
    std::printf("  %d random bands of up to 200 beats: %d refused, %d with a beat more than a sample off\n", runs, refused, off);
    DAWLAB_CHECK(refused == 0 && off == 0);
    rig.engine.applyPendingCommandsOffline();
}

} // namespace ex8::check

int main() {
    juce::ScopedJuceInitialiser_GUI juce;
    using namespace ex8::check;
    std::printf("Exercise 8 - tap tempo in DawLab 19\n");
    checkTap();
    checkFollow();
    return dawlab::test::summary("l19_ex8");
}
