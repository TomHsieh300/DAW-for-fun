#pragma once
// Edits: every change the user makes to the project, as plain values (chapter 15, Step 9 -
// Step 5's design on the application's Session).
//
//   Session::applyEdit(edit)  carries the edit out AND returns the edit that takes it back.
//   History                   keeps those inverses: undo applies them, redo applies theirs.
//
// An edit names what it changes by track id (StripTarget), never by slot or display position:
// after a track is deleted and brought back by undo, it has the same id but may sit in another
// engine slot, and older edits must still find it.
//
// What is undoable follows Step 5's rule: what is saved in the project file is undoable (tracks,
// names, faders, pans, mutes, solos, sends, track format, clips, inserts, built-in parameters,
// bypass, tempo); what only plays or shows the project is not (transport, loop, selection, zoom).
// One exception: changes made inside a VST3 plugin's own editor go to the plugin directly and
// are not seen; they are saved with the plugin's state, but not undoable.

#include <dawlab/engine/All.h>

#include "ProjectFormat.h" // kInsertSlotsPerStrip

#include <juce_core/juce_core.h>

#include <array>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

namespace dawlab::app {

enum class SlotKind { Empty, Builtin, Vst3 };

// Which strip an edit means. A track by its id (the slot is looked up when the edit is applied).
struct StripTarget {
    engine::ChannelKind kind = engine::ChannelKind::Master;
    engine::TrackId track; // kind == Track
    int bus = 0;           // kind == Bus

    static StripTarget of(engine::ChannelRef ref) noexcept { return {ref.kind, ref.trackId, ref.index}; }
    static StripTarget ofTrack(engine::TrackId id) noexcept { return {engine::ChannelKind::Track, id, 0}; }
};

// An insert slot as values: enough to build the same effect again.
struct InsertSnapshot {
    SlotKind kind = SlotKind::Empty;
    engine::EffectType type = engine::EffectType::Gain; // Builtin
    std::vector<float> params;                          // Builtin: every parameter, by index
    bool bypassed = false;
    juce::String vstFile;                               // Vst3: the plugin file ...
    juce::MemoryBlock vstState;                         // ... and its own state
};

struct SendSnapshot {
    float levelDb = kMinusInfinityDb;
    bool preFader = false;
};

// A whole track as values, for deleting it and bringing it back.
struct TrackSnapshot {
    engine::TrackId id;
    engine::TrackFormat format = engine::TrackFormat::Mono;
    juce::String name;
    float gainDb = 0.0f;
    float pan = 0.0f;
    bool mute = false;
    bool solo = false;
    std::array<SendSnapshot, engine::kNumBuses> sends{};
    std::array<InsertSnapshot, kInsertSlotsPerStrip> inserts{};
    std::vector<engine::Clip> clips; // the clips share their audio buffers with the project
};

namespace edit {

// Puts the track back at display position `index` (-1 = at the end), with everything in the
// snapshot. The inverse of RemoveTrack - and how a NEW track is added (Session::makeNewTrack).
struct AddTrack {
    TrackSnapshot track;
    int index = -1;
};
struct RemoveTrack {
    engine::TrackId id;
};
enum class Field { Gain, Pan, Mute, Solo, SendLevel };
struct SetValue {
    StripTarget strip;
    Field field = Field::Gain;
    int bus = 0;        // SendLevel
    float value = 0.0f; // dB, -1..1, or 0/1 for Mute and Solo
};
struct Rename {
    StripTarget strip;
    juce::String name;
};
struct SetFormat {
    engine::TrackId track;
    engine::TrackFormat format = engine::TrackFormat::Mono;
};
struct SetClips {
    engine::TrackId track;
    std::vector<engine::Clip> clips;
};
struct SetInsert { // add, replace or (an Empty snapshot) remove an insert
    StripTarget strip;
    int slot = 0;
    InsertSnapshot insert;
};
struct SetParameter {
    StripTarget strip;
    int slot = 0;
    int index = 0;
    float value = 0.0f;
};
struct SetBypass {
    StripTarget strip;
    int slot = 0;
    bool bypassed = false;
};
struct SetTempo {
    double bpm = 120.0;
};

} // namespace edit

using Edit = std::variant<edit::AddTrack, edit::RemoveTrack, edit::SetValue, edit::Rename, edit::SetFormat, edit::SetClips,
                          edit::SetInsert, edit::SetParameter, edit::SetBypass, edit::SetTempo>;

// What the History applies edits to (the Session). Returns the inverse, or nothing when the edit
// does not apply (its track is gone, its slot is empty...) - then nothing changed.
class EditTarget {
public:
    virtual ~EditTarget() = default;
    virtual std::optional<Edit> applyEdit(const Edit& edit) = 0;
};

// A merge key for a continuous gesture on one control ("the gain of track 17"): the same key
// for every step of one fader drag, a different one for another control.
std::uint32_t mergeKeyFor(const StripTarget& strip, std::uint32_t what) noexcept;

} // namespace dawlab::app
