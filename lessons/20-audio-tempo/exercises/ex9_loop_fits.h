#pragma once
// Exercise 9 - A loop that fits (optional, JUCE).
//
// A drum loop from a sample library says neither its tempo nor its number of bars: it is a file of some seconds.
// Dropped into a song as it is, it is in time only if it was made at the song's tempo. But a loop is almost always
// a whole number of bars, and bars come in powers of two: one, two, four. Guess how many beats it has from its
// length at the song's tempo, and give it the markers of a loop of that many beats (Step 1) - it is in time at
// once, and follows every tempo change after.
//
//   guessBeats(lengthSamples, sampleRate, bpm)
//       How many beats the loop lasts at `bpm` is lengthSamples / sampleRate x bpm / 60 - a fraction. The guess
//       is the power of two (1, 2, 4, ..., 256) closest to it AS A RATIO (|log2(guess / fraction)| smallest:
//       playing it 10 % faster is as wrong as 10 % slower). The middle between two guesses is an irrational number
//       of beats, so there is no tie. 0 if the length is below 1 sample or the tempo or the rate not above 0.
//   dropLoop(session, track, audio, bar, mode)
//       A clip that plays all of `audio`, on the first beat of bar `bar` (from 1, 4/4: bar b starts at tick
//       (b - 1) x 3840), following by `mode` (Varispeed or Stretch) with the markers of a loop of
//       guessBeats(length, rate, the tempo at that tick) beats from that tick. NO fade in - a loop starts on its
//       downbeat, and a fade would take its first hit - and a 5 ms fade out. Put over what is on the track there
//       (Session::withClipOnTop()); ONE undo step, "Add loop".
//       Refused (ok = false, nothing changes): a mode other than Varispeed and Stretch, bar < 1, no audio or an
//       empty one, a track that is not there.
//
// The guess cannot know double time: a loop at 170 BPM is as long as one at 85 with half as many beats, and is
// guessed as the second at a song tempo of 120. The answer says what an app does about it.

#include "MainComponent.h"

#include <cstdint>
#include <memory>

namespace ex9 {

struct Dropped {
    bool ok = false;
    int beats = 0;      // what the loop was taken to be
    int clipIndex = -1; // where it is in the track's list
};

// ---- YOUR TURN (starter/ex9_loop_fits.cpp) ----------------------------------------------------------------------
int guessBeats(std::int64_t lengthSamples, double sampleRate, double bpm) noexcept;
Dropped dropLoop(dawlab::app::Session& session, dawlab::engine::TrackId track, std::shared_ptr<const dawlab::AudioBuffer> audio, int bar,
                 dawlab::engine::FollowMode mode);

} // namespace ex9
