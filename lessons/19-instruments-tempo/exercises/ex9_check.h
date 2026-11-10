#pragma once
// Exercise 9 - the self-check (main). Included at the end of the starter and of the solution.

#include "app_rig.h"
#include "ex9_better_import.h"

#include <dawlab/Check.h>

#include <cstdio>
#include <random>

namespace ex9::check {

using namespace ex19rig;
using engine::TempoPoint;
using midifile::Tempo;

inline std::vector<TempoPoint> asPoints(const std::vector<Tempo>& tempos) {
    std::vector<TempoPoint> points;
    for (const Tempo& t : tempos)
        points.push_back({t.tick, t.bpm, false});
    return points;
}

// A band: a point on every beat, a tempo that wanders, every beat up to 1 % off.
inline std::vector<Tempo> band(int beats, unsigned seed) {
    std::mt19937 rng(seed);
    std::vector<Tempo> tempos;
    double bpm = 120.0;
    for (int k = 0; k < beats; ++k) {
        if (std::uniform_int_distribution<int>(0, 5)(rng) == 0)
            bpm = std::clamp(bpm * std::uniform_real_distribution<double>(0.9, 1.1)(rng), 60.0, 240.0);
        tempos.push_back({static_cast<std::int64_t>(k) * 960, bpm * (1.0 + std::uniform_real_distribution<double>(-0.01, 0.01)(rng))});
    }
    return tempos;
}

// How far a beat of `file` moves at most (seconds) when `now` says when ticks play.
template <typename SecondsAt>
double worstBeat(const std::vector<Tempo>& file, SecondsAt&& now) {
    const std::vector<TempoPoint> points = asPoints(file);
    const std::vector<double> times = tempomodel::pointTimes(points);
    double worst = 0.0;
    for (size_t i = 0; i < points.size(); ++i)
        worst = std::max(worst, std::fabs(now(static_cast<double>(points[i].tick)) - times[i]));
    return worst;
}

inline bool near(double a, double b) {
    return std::fabs(a - b) < 1e-9;
}

inline void checkPrepare() {
    // No tempos: 120. One late tempo: at tick 0. Clamped. A list that is fine stays.
    Prepared p = prepareTempos({}, 256);
    DAWLAB_CHECK(p.ok && p.tolerance == 0.0 && p.tempos.size() == 1 && p.tempos[0].tick == 0 && near(p.tempos[0].bpm, 120.0));
    p = prepareTempos({{480, 5.0}}, 256);
    DAWLAB_CHECK(p.ok && p.tempos.size() == 1 && p.tempos[0].tick == 0 && near(p.tempos[0].bpm, 20.0));
    p = prepareTempos({{0, 100.0}, {960, 2000.0}, {1920, 140.0}}, 256);
    DAWLAB_CHECK(p.ok && p.tolerance == 0.0 && p.tempos.size() == 3 && p.tempos[1].tick == 960 && near(p.tempos[1].bpm, 999.0) && near(p.tempos[2].bpm, 140.0));
    // The same tick twice: the later one. An earlier tick: dropped.
    p = prepareTempos({{0, 100.0}, {0, 110.0}, {960, 130.0}, {960, 90.0}, {480, 200.0}, {1920, 150.0}}, 256);
    DAWLAB_CHECK(p.ok && p.tempos.size() == 3);
    if (p.tempos.size() == 3)
        DAWLAB_CHECK(near(p.tempos[0].bpm, 110.0) && p.tempos[1].tick == 960 && near(p.tempos[1].bpm, 90.0) && p.tempos[2].tick == 1920 && near(p.tempos[2].bpm, 150.0));
    // A tempo that only repeats: gone, at no cost. (The last point always stays.)
    std::vector<Tempo> steady;
    for (int k = 0; k < 1000; ++k)
        steady.push_back({static_cast<std::int64_t>(k) * 240, k < 600 ? 96.0 : 132.0});
    p = prepareTempos(steady, 256);
    DAWLAB_CHECK(p.ok && p.tolerance == 0.0 && p.tempos.size() == 3);
    if (p.tempos.size() == 3)
        DAWLAB_CHECK(near(p.tempos[0].bpm, 96.0) && p.tempos[1].tick == 600 * 240 && near(p.tempos[1].bpm, 132.0) && p.tempos[2].tick == 999 * 240);
    DAWLAB_CHECK(!prepareTempos(steady, 1).ok && !prepareTempos(steady, 0).ok && prepareTempos(steady, 3).ok);

    // A band of 700 beats does not fit in 256 points without a tolerance: the smallest of 0.5 ms, 1 ms, 2 ms ...
    const std::vector<Tempo> file = band(700, 9);
    p = prepareTempos(file, 699); // one point too many: the first tolerance is enough
    DAWLAB_CHECK(p.ok && p.tolerance == 0.0005 && p.tempos.size() <= 699);
    for (const int maxPoints : {256, 64, 16}) {
        p = prepareTempos(file, maxPoints);
        DAWLAB_CHECK(p.ok && static_cast<int>(p.tempos.size()) <= maxPoints && p.tolerance >= 0.0005);
        if (!p.ok || p.tempos.empty())
            continue;
        const double steps = std::log2(p.tolerance / 0.0005);
        DAWLAB_CHECK(std::fabs(steps - std::round(steps)) < 1e-9); // one of the tolerances of the header
        const std::vector<TempoPoint> points = asPoints(p.tempos);
        const std::vector<double> times = tempomodel::pointTimes(points);
        DAWLAB_CHECK(tempomodel::isJumpList(points));
        const double worst = tempomodel::isJumpList(points) ? worstBeat(file, [&](double tick) { return tempomodel::secondsAt(points, times, tick); }) : 1e9;
        DAWLAB_CHECK(worst <= p.tolerance + 2e-9);
        // The tolerance before it did not fit.
        std::vector<TempoPoint> tighter;
        tempomodel::thinTempo(asPoints(file), p.tolerance > 0.0005 ? 0.5 * p.tolerance : 0.0, tighter);
        DAWLAB_CHECK(static_cast<int>(tighter.size()) > maxPoints);
        std::printf("  700 tempos into %3d points: %3zu points within %.1f ms (the worst beat %.2f ms off)\n", maxPoints, p.tempos.size(),
                    1000.0 * p.tolerance, 1000.0 * worst);
    }
}

inline midifile::Song songOf(const std::vector<Tempo>& tempos) {
    midifile::Song song;
    song.tempos = tempos;
    midifile::Track track;
    track.name = "Band";
    for (size_t k = 0; k + 1 < tempos.size(); k += 4) { // a note on the first beat of every bar
        track.events.push_back({tempos[k].tick, {0x90, 60, 100}});
        track.events.push_back({tempos[k].tick + 480, {0x80, 60, 0}});
    }
    song.tracks.push_back(track);
    return song;
}

inline void checkImport() {
    const std::vector<Tempo> file = band(700, 9);
    const midifile::Song song = songOf(file);

    Rig rig;
    const int tracksBefore = rig.session.getNumTracks();
    const std::vector<TempoPoint> mapBefore = rig.tempoPoints();
    const BetterImport better = importSongBetter(rig.session, song);
    rig.engine.applyPendingCommandsOffline();
    DAWLAB_CHECK(better.result.ok && better.result.tracks.size() == 1 && better.result.notes == 175 && !better.result.thinned);
    DAWLAB_CHECK(better.tolerance >= 0.0005 && better.result.tempos <= engine::TempoMap::kMaxPoints);
    DAWLAB_CHECK(rig.session.getNumTracks() == tracksBefore + 1 && rig.session.getHistory().getUndoName() == "Import MIDI");
    const double ours = worstBeat(file, [&](double tick) { return rig.session.getTempoMap().secondsAt(tick); });
    DAWLAB_CHECK(better.result.ok && ours <= better.tolerance + 2e-9);

    // The app's own import of the same file: every k-th tempo.
    Rig plain;
    const MidiImport app = importSong(plain.session, song);
    plain.engine.applyPendingCommandsOffline();
    const double theirs = worstBeat(file, [&](double tick) { return plain.session.getTempoMap().secondsAt(tick); });
    DAWLAB_CHECK(app.ok && app.thinned && theirs > 10.0 * ours);
    std::printf("  a file of 700 tempos imported: %d points, the worst beat %.2f ms off; the app's import: %d points, %.0f ms off\n", better.result.tempos,
                1000.0 * ours, app.tempos, 1000.0 * theirs);

    // One undo step takes the track and the tempos back.
    DAWLAB_CHECK(rig.session.undo() && rig.session.getNumTracks() == tracksBefore && rig.tempoPoints().size() == mapBefore.size() &&
                 rig.tempoPoints()[0].bpm == mapBefore[0].bpm && !rig.session.getHistory().canUndo());

    // A file that fits is imported as it is.
    const midifile::Song small = songOf(band(200, 3));
    const BetterImport same = importSongBetter(rig.session, small);
    rig.engine.applyPendingCommandsOffline();
    DAWLAB_CHECK(same.result.ok && same.tolerance == 0.0 && same.result.tempos == 200);
    DAWLAB_CHECK((same.result.ok && worstBeat(small.tempos, [&](double tick) { return rig.session.getTempoMap().secondsAt(tick); }) < 1e-9));
    // A song with no track is refused by the app, and nothing changes.
    midifile::Song none;
    none.tempos = file;
    const BetterImport refused = importSongBetter(rig.session, none);
    DAWLAB_CHECK(!refused.result.ok && rig.session.getTempoMap().size() == 200);
    rig.engine.applyPendingCommandsOffline();
}

} // namespace ex9::check

int main() {
    juce::ScopedJuceInitialiser_GUI juce;
    using namespace ex9::check;
    std::printf("Exercise 9 - a better MIDI import in DawLab 19\n");
    checkPrepare();
    checkImport();
    return dawlab::test::summary("l19_ex9");
}
