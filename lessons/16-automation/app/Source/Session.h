#pragma once
// Session: the application's model of the project (chapter 14, Steps 15, 16 and 18; chapter 15,
// Step 7: tracks with ids in a growing list).
//
// The engine (stage 1) knows *what to play*: clip lists, insert chains, fader values. It does
// not know what the GUI needs to show: "slot 3 of the bass track holds a Compressor", the name
// of a clip, which slots are empty, in which order the tracks are shown. Session is that
// bookkeeping layer. It lives on the message thread and is the only object that sends
// structural changes to the engine:
//
//   GUI component --> Session --> Engine::addTrack / removeTrack / addInsert / replaceInsert /
//                                  removeInsert / setTrackClips   (command queue)
//                 --> Engine::getStrip(ref)->setGainDb(...)   (atomics)
//
// Tracks (chapter 15): the project holds a list of tracks in display order, each with a
// TrackId. The id is the track's name for the program: every function here takes an id (or a
// ChannelRef, which carries it), never a position, because positions change when a track above
// is added or deleted and ids do not. Session hands out the ids, counting up from 1, and never
// gives the same id twice while the program runs - not even after File > New - so a late result
// for a deleted track (a file that was still loading, say) finds nothing instead of landing on
// another track. The display position is only for drawing: getTrackId(index) / indexOf(id).
//
// Insert slots: every strip shows kInsertSlotsPerStrip (= 4) slots. The engine's InsertChain is
// compact (no holes), so a GUI slot is translated into a chain index: the number of occupied
// GUI slots before it. Because commands are applied in FIFO order, the GUI and the engine agree
// as soon as the audio thread has caught up with the queue.
//
//   GUI slots:   [ EQ ][    ][Comp][    ]
//   chain:       [ EQ ][Comp]                 GUI slot 2 -> chain index 1
//
// Clips: Session keeps the GUI copy of every track's clip list (sorted by start). Every edit
// builds a new vector and hands a copy to the engine with setTrackClips(); the audio thread
// swaps one pointer and the old list comes back as garbage.
//
// Session broadcasts a change message (juce::ChangeBroadcaster) whenever the structure changes,
// so the arrangement and the mixer can rebuild what they display.

#include <dawlab/AudioBuffer.h>
#include <dawlab/engine/All.h>
#include <dawlab/engine/DemoSong.h>

#include "History.h"       // Edit, History, SlotKind (Step 9)
#include "MediaLoader.h"   // ConvertedBuffers
#include "ProjectFormat.h" // kInsertSlotsPerStrip

#include <juce_events/juce_events.h>
#include <juce_core/juce_core.h>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace dawlab::app {

class VstInsert;

constexpr int kNewProjectTracks = 8; // what File > New starts with

struct InsertSlotState {
    SlotKind kind = SlotKind::Empty;
    engine::EffectType type = engine::EffectType::Gain; // valid when kind == Builtin
    engine::InsertEffect* effect = nullptr;             // observer; valid while it sits in the slot
    VstInsert* vst = nullptr;                           // the same object, when kind == Vst3
    juce::String name;                                  // what the slot button shows

    bool isEmpty() const noexcept { return kind == SlotKind::Empty || effect == nullptr; }
};

class Session final : public juce::ChangeBroadcaster, public EditTarget {
public:
    explicit Session(engine::Engine& engine);
    ~Session() override;

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    engine::Engine& getEngine() noexcept { return engine_; }
    const engine::Engine& getEngine() const noexcept { return engine_; }
    double getSampleRate() const noexcept { return engine_.getSampleRate(); }

    // ---- project ---------------------------------------------------------------------------
    // Empty project: kNewProjectTracks empty mono tracks, faders at 0 dB, tempo 120, loop bars
    // 1-4 (off), and a Limiter (ceiling -1 dBFS) in master slot 4. The engine must be prepared.
    void newProject();

    // A project with nothing in it: no tracks, no inserts, buses and master at their defaults,
    // tempo 120, loop bars 1-4 (off). What ProjectIO::open() fills in (chapter 15, Step 8).
    void clearProject();

    // Replaces the whole project with `song` (generated at the engine's sample rate, see
    // MediaLoader::loadDemoSong). Returns the number of unknown parameter names (0 expected).
    int applyDemoSong(const engine::DemoSong& song);

    juce::String getProjectName() const { return projectName_; }
    void setProjectName(const juce::String& name);

    // ---- tracks ------------------------------------------------------------------------------
    // Adds a track at display position `index` (-1 = at the end) with a new id. Returns the id,
    // or an invalid one when the engine's pool (engine::kMaxTracks) or its queue is full.
    engine::TrackId addTrack(engine::TrackFormat format = engine::TrackFormat::Mono, const juce::String& name = {},
                             int index = -1);
    // Deletes the track, its clips and its inserts (onInsertWillBeRemoved is called for each).
    // False if the id is not here or the engine's queue is full; then nothing changed.
    bool removeTrack(engine::TrackId id);

    int getNumTracks() const noexcept { return static_cast<int>(tracks_.size()); }
    engine::TrackId getTrackId(int index) const noexcept; // invalid outside 0 .. getNumTracks()-1
    int indexOf(engine::TrackId id) const noexcept;        // display position, or -1
    bool hasTrack(engine::TrackId id) const noexcept { return indexOf(id) >= 0; }
    // The reference the engine and the strip functions below take; index -1 if the id is not here.
    engine::ChannelRef trackRef(engine::TrackId id) const noexcept { return engine_.trackRef(id); }

    // ---- strips, in mixer order: the tracks (display order), the buses, the master -----------
    int getNumStrips() const noexcept { return getNumTracks() + engine::kNumBuses + 1; }
    engine::ChannelRef stripRefAt(int stripIndex) const noexcept;
    int stripIndexOf(engine::ChannelRef ref) const noexcept; // -1 for a track that is not here

    // ---- clips -------------------------------------------------------------------------------
    const std::vector<engine::Clip>& getClips(engine::TrackId track) const; // empty for an unknown id
    void setClips(engine::TrackId track, std::vector<engine::Clip> clips);

    // One clip that plays `audio` from timeline sample `start`, with 5 ms fades. The audio must
    // already be at getClipSampleRate() (MediaLoader resamples it).
    engine::Clip makeAudioClip(std::shared_ptr<const AudioBuffer> audio, const juce::String& clipName,
                               int64_t start = 0) const;

    // Moves clip `clipIndex` of `track` to `newStart` (clamped to >= 0), as an undoable edit: all
    // moves of one drag are one entry (the arrangement ends the merge on mouse down and up).
    // Returns the clip's new index in the (start-sorted) list, or -1 if the arguments are invalid.
    int moveClip(engine::TrackId track, int clipIndex, int64_t newStart);

    // End of the last clip on any track (GUI copy; 0 for an empty project).
    int64_t getSongEndSample() const;

    juce::String getStripName(engine::ChannelRef ref) const;
    void setStripName(engine::ChannelRef ref, const juce::String& name);

    // Mono / stereo track (Step 4). Loading a file sets it from the file's channel count
    // (Step 18); the strip's format button switches it by hand (Step 15).
    engine::TrackFormat getTrackFormat(engine::TrackId track) const;
    void setTrackFormat(engine::TrackId track, engine::TrackFormat format);

    // ---- insert slots ------------------------------------------------------------------------
    const InsertSlotState& getSlot(engine::ChannelRef ref, int uiSlot) const;
    int chainIndexOf(engine::ChannelRef ref, int uiSlot) const noexcept;
    int getNumUsedSlots(engine::ChannelRef ref) const noexcept;

    // Puts a new built-in effect of `type` into the slot (replacing what was there). Returns the
    // new effect (observer pointer) or nullptr if the engine refused it.
    engine::InsertEffect* setBuiltinInsert(engine::ChannelRef ref, int uiSlot, engine::EffectType type);

    // Same for a hosted VST3 plugin (Step 17). Ownership passes to the engine.
    engine::InsertEffect* setVstInsert(engine::ChannelRef ref, int uiSlot, std::unique_ptr<VstInsert> plugin);

    void clearInsert(engine::ChannelRef ref, int uiSlot);
    void setInsertBypassed(engine::ChannelRef ref, int uiSlot, bool bypassed);

    // First insert on the strip whose InsertEffect::getName() equals `effectName` (its GUI slot, or -1).
    engine::InsertEffect* findInsert(engine::ChannelRef ref, const char* effectName) const noexcept;
    int findInsertSlot(engine::ChannelRef ref, const char* effectName) const noexcept;

    // Called (message thread) right before an insert leaves a slot - or leaves with its track -
    // so editor windows that point at it can close while the pointer is still valid.
    std::function<void(engine::InsertEffect*)> onInsertWillBeRemoved;

    // ---- master strip helpers (the "Ceiling" knob drives the first Limiter on the master) -----
    bool hasMasterLimiter() const noexcept;
    float getMasterCeilingDb() const noexcept;

    // ---- device changes -----------------------------------------------------------------------
    // Clip positions and clip sources are in samples, so they belong to one sample rate: the
    // "clip rate". It equals the engine's rate except while MainComponent converts the clips
    // after a device change (the conversion runs on the loader thread, Step 11). New audio for
    // a clip must be at this rate.
    double getClipSampleRate() const noexcept;

    // Every distinct clip source buffer (shared buffers once), for a background conversion.
    std::vector<std::shared_ptr<const AudioBuffer>> getClipSources() const;

    // Converts every clip from the clip rate to `newRate`: sources found in `converted` are
    // swapped for their converted version, any other source is resampled here (synchronously),
    // start / length / offset / fades are scaled by newRate / clipRate. The clip rate becomes
    // newRate. Message thread.
    void applySampleRateConversion(double newRate, const ConvertedBuffers& converted);

    // Loop range and playhead follow a device change at once (they are engine-rate positions).
    void scaleTransportPositions(double oldRate, double newRate);

    // The synchronous version of the whole conversion, for code that already knows the clips
    // are at `oldRate` (tests, loading a project saved at another rate - Exercise 11): treats the
    // clips as being at oldRate, resamples every source on the calling thread, scales the clips
    // and the transport. Blocks for as long as the resampling takes. Message thread.
    void handleSampleRateChange(double oldRate, double newRate);

    // ---- undo and redo (chapter 15, Step 9) ---------------------------------------------------
    // Every change the GUI makes goes through perform(): it is applied, and its inverse is kept
    // in the history. The functions above change the project WITHOUT recording; applyEdit() is
    // built from them, and so are File > New, the demo song and opening a file, which all start
    // a new history. A device change converts every clip, which old entries know at the old
    // rate, so it starts a new history too.
    bool perform(std::vector<Edit> edits, const juce::String& name, std::uint32_t mergeKey = 0);
    bool perform(Edit edit, const juce::String& name, std::uint32_t mergeKey = 0);
    bool undo();
    bool redo();
    History& getHistory() noexcept { return history_; }
    const History& getHistory() const noexcept { return history_; }

    std::optional<Edit> applyEdit(const Edit& edit) override;

    // The edit that adds a NEW track at display position `index` (-1 = at the end). It names the
    // next id; applying it moves the id counter on (and undoing it never moves it back).
    edit::AddTrack makeNewTrack(engine::TrackFormat format, int index = -1, const juce::String& name = {}) const;
    TrackSnapshot snapshotTrack(engine::TrackId id) const;
    InsertSnapshot snapshotInsert(engine::ChannelRef ref, int uiSlot) const;
    engine::ChannelRef refOf(const StripTarget& target) const noexcept;

    // Builds a VST3 insert from a plugin file and its saved state (undoing the removal of a
    // plugin, Step 9). MainComponent sets it with chapter 13's PluginHost; without it such an edit
    // does not apply.
    std::function<std::unique_ptr<VstInsert>(const juce::File& file, const juce::MemoryBlock& state, juce::String& error)>
        createVst3;

private:
    struct StripModel {
        std::array<InsertSlotState, kInsertSlotsPerStrip> slots;
    };
    struct TrackModel {
        engine::TrackId id;
        std::vector<engine::Clip> clips;
        StripModel strip;
    };

    TrackModel* findTrack(engine::TrackId id) noexcept;
    const TrackModel* findTrack(engine::TrackId id) const noexcept;
    StripModel* findStrip(engine::ChannelRef ref) noexcept;
    const StripModel* findStrip(engine::ChannelRef ref) const noexcept;
    void removeAllTracks();
    engine::TrackId addTrackWithId(engine::TrackId id, engine::TrackFormat format, const juce::String& name, int index);
    bool applyInsertSnapshot(engine::ChannelRef ref, int uiSlot, const InsertSnapshot& insert);
    std::optional<Edit> apply(const edit::AddTrack&);
    std::optional<Edit> apply(const edit::RemoveTrack&);
    std::optional<Edit> apply(const edit::SetValue&);
    std::optional<Edit> apply(const edit::Rename&);
    std::optional<Edit> apply(const edit::SetFormat&);
    std::optional<Edit> apply(const edit::SetClips&);
    std::optional<Edit> apply(const edit::SetInsert&);
    std::optional<Edit> apply(const edit::SetParameter&);
    std::optional<Edit> apply(const edit::SetBypass&);
    std::optional<Edit> apply(const edit::SetTempo&);
    void removeBusAndMasterInserts();
    void resetBusAndMasterSettings();
    void pushClips(const TrackModel& track);

    engine::Engine& engine_;
    std::vector<TrackModel> tracks_;                          // display order
    std::array<StripModel, engine::kNumBuses + 1> fixedStrips_; // the buses, then the master
    std::uint32_t nextTrackId_ = 1;                           // only counts up (see the top)
    double clipSampleRate_ = 0.0; // 0 = not set yet: follow the engine
    juce::String projectName_ = "Untitled";
    History history_;
};

} // namespace dawlab::app
