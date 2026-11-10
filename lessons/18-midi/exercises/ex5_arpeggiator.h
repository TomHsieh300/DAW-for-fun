#pragma once
// Exercise 5 - An arpeggiator.
//
// An arpeggiator is a MIDI effect: it sits between the keyboard and the instrument, takes the chord that is
// held and plays its notes one after another, on a grid in musical time. It is Step 1 and Step 2 again in a
// new shape: events at their exact samples, in ticks that follow the tempo, and no note left hanging.
//
//   Steps      at every tick that is a multiple of `rate` (240 = 1/16). A step plays when at least one key
//              is held AT THAT SAMPLE: an input event at the step's own offset counts (input first).
//   Pattern    the held keys (by pitch, at most kMaxHeld), in `octaves` octaves (+12 each, above 127 left out):
//                Up        all of them in ascending pitch
//                Down      descending
//                UpDown    ascending, then back down without repeating the top and the bottom:
//                          60 64 67 64 | 60 64 67 64 | ...  (one note: just it; two: up only)
//                AsPlayed  in the order the keys went down, octave after octave
//              A key that is already held is not added again; one key too many pushes out the oldest.
//   Index      the step plays pattern[index % pattern size], then index + 1. A step with no key held sets
//              index back to 0: a new chord starts at its beginning.
//   Note       on at the step with the velocity and channel of its key; off at tick step + gateTicks(), where
//              gateTicks() = clamp(round(gate x rate), 1, rate). A note still sounds when its key goes up; it
//              ends at its gate as always.
//   Pass       note ons and offs of the input are the arpeggiator's; every other input event (a controller, a
//              bend) goes out at its own offset.
//   Order      the output is in offset order; at one offset the note offs come first, the rest as they happened.
//
// process() runs on the audio thread: `out` is cleared first and must not grow beyond the capacity the caller
// reserved (no allocation; std::stable_sort allocates - keep the order as you go). Blocks may be of any size,
// and the output must not depend on them.

#include "model.h"

#include <array>
#include <cstdint>
#include <vector>

namespace ex5 {

using minidaw::BlockEvent;
using minidaw::Kind;
using minidaw::MidiMessage;
using minidaw::Tempo;

enum class ArpMode { Up, Down, UpDown, AsPlayed };

struct ArpSettings {
    int rate = 240;      // ticks per step
    double gate = 0.5;   // the length of a note, as a fraction of a step
    ArpMode mode = ArpMode::Up;
    int octaves = 1;     // 1 .. 4
};

class Arpeggiator {
public:
    static constexpr int kMaxHeld = 16;

    // ---- provided -----------------------------------------------------------------------------------------
    void setSettings(const ArpSettings& s) noexcept {
        settings_ = s;
        settings_.rate = std::max(s.rate, 1);
        settings_.octaves = std::clamp(s.octaves, 1, 4);
    }
    int gateTicks() const noexcept {
        return static_cast<int>(std::clamp<long long>(std::llround(settings_.gate * settings_.rate), 1, settings_.rate));
    }
    int numHeld() const noexcept { return numHeld_; }
    bool sounding() const noexcept { return sounding_; }

    // ---- YOUR TURN (starter/ex5_arpeggiator.cpp) ----------------------------------------------------------------
    // The song's block [blockStart, blockStart + numSamples) while the transport plays, with the keyboard's
    // events of that block (in offset order).
    void process(std::int64_t blockStart, int numSamples, const Tempo& tempo, const std::vector<BlockEvent>& input,
                 std::vector<BlockEvent>& out) noexcept;
    // The transport stopped or jumped: the sounding note ends now (a note off at offset 0 into `out`, which is
    // cleared first), and the next step starts the pattern from its beginning. The keys stay held.
    void reset(std::vector<BlockEvent>& out) noexcept;

private:
    struct Key {
        int pitch = 0;
        int velocity = 0;
        int channel = 0;
    };
    // Fills pattern_ (and patternSize_) from the held keys, in the mode and octaves of the settings.
    void buildPattern() noexcept;

    ArpSettings settings_;
    std::array<Key, kMaxHeld> held_{}; // in the order they went down
    int numHeld_ = 0;
    std::array<Key, kMaxHeld * 4 * 2> pattern_{};
    int patternSize_ = 0;
    int index_ = 0;
    bool sounding_ = false;   // a note of ours is on
    Key playing_;             // which one
    std::int64_t offTick_ = 0; // and when it ends
};

} // namespace ex5
