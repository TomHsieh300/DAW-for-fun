// Exercise 9 (starter): a loop that fits.
//
// Fill in the TODOs. Until then the self-check prints FAIL (expected). The solution is
// solutions/ex9_loop_fits.cpp. What the functions must do is in ../ex9_loop_fits.h.

#include "../ex9_loop_fits.h"

namespace ex9 {

using namespace dawlab;

int guessBeats(std::int64_t lengthSamples, double sampleRate, double bpm) noexcept {
    // TODO: the beats it lasts at bpm; the power of two closest to it as a ratio (log2).
    (void)lengthSamples, (void)sampleRate, (void)bpm;
    return 0;
}

Dropped dropLoop(app::Session& session, engine::TrackId track, std::shared_ptr<const AudioBuffer> audio, int bar, engine::FollowMode mode) {
    // TODO: 1. refuse what the header says;
    //       2. the bar's tick and the tempo there; the beats;
    //       3. session.makeAudioClip() at the bar's sample, no fade in, an engine::ClipFollow with
    //          engine::markersForLoop(), placed with engine::placeUnder();
    //       4. ONE session.perform(app::edit::SetClips{track, session.withClipOnTop(...)}, "Add loop").
    (void)session, (void)track, (void)audio, (void)bar, (void)mode;
    return {};
}

} // namespace ex9

#include "../ex9_check.h"
