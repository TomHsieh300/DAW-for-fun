#pragma once
// MidiModel: a MIDI clip as the app keeps it (chapter 18, Step 8).
//
// The engine plays engine::MidiClip: notes in ticks, sorted. The app keeps more: the notes AS PLAYED and how
// they are quantized (Step 6). What the engine gets is computed - quantizeNotes(played) - every time the clip
// changes, so quantize can be changed or taken back at any time, and the project file saves what was played.
//
// A clip's id is how the engine's player knows it through edits (Step 2); the Session hands them out and never
// gives the same one twice.

#include <dawlab/engine/Midi.h>

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace dawlab::app {

struct QuantizeSettings {
    std::int64_t grid = 240; // ticks: a sixteenth
    double strength = 0.0;   // 0: as played
    double swing = 0.0;      // 0 .. 0.5

    bool operator==(const QuantizeSettings& o) const noexcept { return grid == o.grid && strength == o.strength && swing == o.swing; }
};

// Step 6: starts moved towards the nearest grid line by `strength`, every odd line `swing` later; lengths stay;
// sorted by start (stable). Strength 0 gives the notes back as they are.
inline std::vector<engine::Note> quantizeNotes(const std::vector<engine::Note>& played, const QuantizeSettings& q) {
    std::vector<engine::Note> out = played;
    if (q.strength != 0.0 && q.grid > 0)
        for (engine::Note& n : out) {
            const std::int64_t line = std::llround(static_cast<double>(n.start) / static_cast<double>(q.grid));
            std::int64_t target = line * q.grid;
            if (line % 2 != 0)
                target += std::llround(q.swing * static_cast<double>(q.grid));
            n.start = std::max<std::int64_t>(0, std::llround(static_cast<double>(n.start) + q.strength * static_cast<double>(target - n.start)));
        }
    std::stable_sort(out.begin(), out.end(), [](const engine::Note& a, const engine::Note& b) { return a.start < b.start; });
    return out;
}

struct MidiClipModel {
    int id = 0;                       // 0: not yet given one (Session::setMidiClips gives it)
    std::int64_t start = 0;           // timeline tick
    std::int64_t length = 4 * 960;    // ticks
    std::int64_t offset = 0;          // the material's tick at the clip's start
    bool muted = false;
    juce::String name;
    QuantizeSettings quantize;
    std::vector<engine::Note> played;                 // sorted by start
    std::vector<engine::ControllerPoint> controllers; // sorted by tick

    std::int64_t end() const noexcept { return start + length; }
    // What plays: the notes after quantize.
    std::vector<engine::Note> notes() const { return quantizeNotes(played, quantize); }
    engine::MidiClip toEngine() const {
        engine::MidiClip c;
        c.id = id;
        c.start = start;
        c.length = length;
        c.offset = offset;
        c.muted = muted;
        c.notes = notes();
        c.controllers = controllers;
        return c;
    }
    bool operator==(const MidiClipModel& o) const noexcept {
        auto sameControllers = [](const std::vector<engine::ControllerPoint>& a, const std::vector<engine::ControllerPoint>& b) {
            if (a.size() != b.size())
                return false;
            for (size_t i = 0; i < a.size(); ++i)
                if (a[i].tick != b[i].tick || a[i].number != b[i].number || a[i].value != b[i].value || a[i].channel != b[i].channel)
                    return false;
            return true;
        };
        return id == o.id && start == o.start && length == o.length && offset == o.offset && muted == o.muted && name == o.name &&
               quantize == o.quantize && played == o.played && sameControllers(controllers, o.controllers);
    }
};

inline void sortNotes(std::vector<engine::Note>& notes) {
    std::stable_sort(notes.begin(), notes.end(), [](const engine::Note& a, const engine::Note& b) { return a.start < b.start; });
}

} // namespace dawlab::app
