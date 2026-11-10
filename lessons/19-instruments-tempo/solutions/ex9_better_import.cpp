// Exercise 9 (solution): a better MIDI import in DawLab 19.
//
// Build and run:  l19_ex9_solution   (ctest, label "solution"; needs -Juce)
// Your version:   exercises/starter/ex9_better_import.cpp -> l19_ex9_starter

#include "../exercises/ex9_better_import.h"

namespace ex9 {

using namespace dawlab;
using namespace dawlab::app;

Prepared prepareTempos(const std::vector<midifile::Tempo>& tempos, int maxPoints) {
    Prepared result;
    if (maxPoints < 2)
        return result;

    // A list a map can be made of.
    std::vector<engine::TempoPoint> points;
    for (const midifile::Tempo& t : tempos) {
        const double bpm = std::clamp(t.bpm, engine::TempoMap::kMinBpm, engine::TempoMap::kMaxBpm);
        const std::int64_t tick = points.empty() ? 0 : t.tick;
        if (points.empty() || tick > points.back().tick)
            points.push_back({tick, bpm, false});
        else if (tick == points.back().tick)
            points.back().bpm = bpm; // the later one is what plays
    }
    if (points.empty())
        points.push_back({0, 120.0, false});

    // As few as it takes: nothing lost if that fits, and else the smallest of 0.5 ms, 1 ms, 2 ms ... that does.
    std::vector<engine::TempoPoint> thinned;
    double tolerance = 0.0;
    tempomodel::thinTempo(points, tolerance, thinned);
    while (static_cast<int>(thinned.size()) > maxPoints) {
        tolerance = tolerance == 0.0 ? 0.0005 : 2.0 * tolerance;
        tempomodel::thinTempo(points, tolerance, thinned);
    }

    result.ok = true;
    result.tolerance = tolerance;
    for (const engine::TempoPoint& p : thinned) // a tempo computed again may be a hair outside the map's range
        result.tempos.push_back({p.tick, std::clamp(p.bpm, engine::TempoMap::kMinBpm, engine::TempoMap::kMaxBpm)});
    return result;
}

BetterImport importSongBetter(Session& session, const midifile::Song& song) {
    BetterImport better;
    const Prepared prepared = prepareTempos(song.tempos, engine::TempoMap::kMaxPoints);
    midifile::Song fits = song;
    fits.tempos = prepared.tempos;
    better.result = importSong(session, fits);
    better.tolerance = prepared.tolerance;
    return better;
}

} // namespace ex9

#include "../exercises/ex9_check.h"
