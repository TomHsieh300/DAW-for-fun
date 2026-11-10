#pragma once
// Sizes of the DawLab engine, chapter 15 version.
//
// Chapter 14's engine was "static": 8 tracks, every one of them allocated in prepare(). Chapter
// 15 keeps the rule that matters - the audio thread never allocates - but turns the 8 tracks
// into a POOL of kMaxTracks slots (Step 2): all slots are allocated in prepare(), and adding a
// track means "start using a free slot". How many tracks a machine can PLAY is a question of
// CPU time (Step 3), not of this number; kMaxTracks only bounds the memory.

#include <cstdint>

namespace dawlab::engine {

// A track's identity (Step 1): handed out by the model, never reused. 0 = no track.
struct TrackId {
    std::uint32_t value = 0;

    constexpr bool isValid() const noexcept { return value != 0; }
    friend constexpr bool operator==(TrackId a, TrackId b) noexcept { return a.value == b.value; }
    friend constexpr bool operator!=(TrackId a, TrackId b) noexcept { return a.value != b.value; }
};

// An effect's identity (chapter 16, Step 4): handed out once, never reused, and NOT its slot, so that
// a curve stays with its effect when the effect moves. 0 means none.
struct EffectId {
    std::uint32_t value = 0;

    constexpr bool isValid() const noexcept { return value != 0; }
    friend constexpr bool operator==(EffectId a, EffectId b) noexcept { return a.value == b.value; }
    friend constexpr bool operator!=(EffectId a, EffectId b) noexcept { return a.value != b.value; }
};

// Track slots in the pool. Tests build with a smaller pool (-DDAWLAB_ENGINE_MAX_TRACKS=16) so
// they can fill it.
#ifndef DAWLAB_ENGINE_MAX_TRACKS
#define DAWLAB_ENGINE_MAX_TRACKS 128
#endif
constexpr int kMaxTracks = DAWLAB_ENGINE_MAX_TRACKS;
static_assert(kMaxTracks >= 1, "the pool needs at least one slot");
// FX send/return buses (chapter 14, Step 6). Exercise 12 of chapter 14 builds with
// -DDAWLAB_ENGINE_NUM_BUSES=3; everything (mixer, sends, GUI) follows this one constant.
#ifndef DAWLAB_ENGINE_NUM_BUSES
#define DAWLAB_ENGINE_NUM_BUSES 2
#endif
constexpr int kNumBuses = DAWLAB_ENGINE_NUM_BUSES;
static_assert(kNumBuses >= 1 && kNumBuses <= 26, "buses are named A..Z");
constexpr int kNumChannels = 2;               // the whole engine is stereo
constexpr int kMaxInserts = 8;                // insert slots per channel strip (Step 5)
constexpr int kDefaultMaxPdcSamples = 16384;  // upper bound for plugin delay compensation (Step 8)
constexpr int kCommandQueueCapacity = 1024;   // UI -> audio commands in flight (Step 7)
constexpr float kMaxFaderDb = 12.0f;          // fader range is [-inf, +12] dB
constexpr int kCaptureQueueCapacity = 4096;   // audio -> UI automation events in flight (chapter 16)
// The most garbage one command can produce: removing a track hands back all its inserts, its
// clip list, its MIDI clip list and its instrument (chapter 18) and the curves of its fader and pan. The audio
// thread takes a command only when the garbage queue has this much room, so nothing it gives back is ever lost
// (chapter 15, Step 2).
constexpr int kMaxGarbagePerCommand = kMaxInserts + 5;

} // namespace dawlab::engine
