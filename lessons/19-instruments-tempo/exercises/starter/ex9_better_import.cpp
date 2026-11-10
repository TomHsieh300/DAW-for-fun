// Exercise 9 (starter): a better MIDI import in DawLab 19.
//
// Fill in the TODOs. Until then the self-check prints FAIL (expected). The solution is
// solutions/ex9_better_import.cpp. What the functions must do is in ../ex9_better_import.h.

#include "../ex9_better_import.h"

namespace ex9 {

Prepared prepareTempos(const std::vector<dawlab::app::midifile::Tempo>& tempos, int maxPoints) {
    // TODO: the tempos as engine::TempoPoint jumps (clamped, the first at tick 0, a later one at the same tick
    //       replaces, an earlier tick is dropped, none: 120); tempomodel::thinTempo() with 0, then 0.0005 and
    //       doubling while there are more than maxPoints; back to midifile::Tempo.
    (void)tempos;
    (void)maxPoints;
    return {};
}

BetterImport importSongBetter(dawlab::app::Session& session, const dawlab::app::midifile::Song& song) {
    // TODO: a copy of the song with prepareTempos(song.tempos, engine::TempoMap::kMaxPoints), and the app's
    //       importSong() for it.
    (void)session;
    (void)song;
    BetterImport r;
    r.result.error = "not done yet";
    return r;
}

} // namespace ex9

#include "../ex9_check.h"
