#pragma once
// TestTracks.h - what the chapter-14 engine tests need to run on the chapter-15 engine.
//
// A chapter-14 Engine had 8 tracks from the start; a chapter-15 Engine has none until they are
// added. The ported tests call addTestTracks() right after creating the engine: it adds 8 Mono
// tracks (chapter 14's default) with ids 1..8. In an empty engine they take slots 0..7, so
// getMixer().getTrack(t) still means "track t" and the tests are unchanged otherwise:
//   ChannelRef::track(t)      ->  testTrack(t)        (slot t, id t + 1)
//   setTrackClips(t, ...)     ->  setTrackClips(testId(t), ...)
//   kNumTracks                ->  kTestTracks
// That the old tests pass unchanged in substance is the point: the pool is a structural change
// that must not change what the engine does with 8 tracks.

#include <dawlab/engine/All.h>

#include <cstdint>
#include <string>

namespace dawlab::engine::test {

constexpr int kTestTracks = 8;

inline TrackId testId(int t) noexcept { return TrackId{static_cast<std::uint32_t>(t + 1)}; }
inline ChannelRef testTrack(int t) noexcept { return ChannelRef::track(t, testId(t)); }

// Adds tracks 1..8 (Mono, named "Track 1".."Track 8") and applies each AddTrack at once - the
// tests run offline, with no audio thread - so the command queue is empty again afterwards, as
// the chapter-14 tests expect (some count its contents, one uses a 4-command queue). True if
// all 8 tracks got the expected slot.
inline bool addTestTracks(Engine& e) {
    bool ok = true;
    for (int t = 0; t < kTestTracks; ++t) {
        ok = e.addTrack(testId(t), TrackFormat::Mono, "Track " + std::to_string(t + 1)) == testTrack(t) && ok;
        e.applyPendingCommandsOffline();
    }
    return ok && e.getMixer().getNumActiveTracks() == kTestTracks;
}

} // namespace dawlab::engine::test
