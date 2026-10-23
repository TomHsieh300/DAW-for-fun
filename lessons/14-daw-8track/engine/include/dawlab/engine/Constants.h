#pragma once
// Fixed sizes of the DawLab engine.
//
// The engine is deliberately "static": 8 tracks, 2 FX buses, stereo everywhere, at most
// 8 inserts per channel strip. Fixed sizes mean every container can be allocated once in
// prepare() and the audio thread never has to grow anything.

namespace dawlab::engine {

constexpr int kNumTracks = 8;                 // audio tracks (Steps 3 and 4)
// FX send/return buses (Step 6). Exercise 12 builds the engine and the app with
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

} // namespace dawlab::engine
