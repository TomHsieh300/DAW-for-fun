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
// Automation (chapter 16, Step 6): Session keeps the GUI copy of every lane, like the clips. Every
// change makes a new curve and hands a copy to the engine; the audio thread swaps one pointer. A lane
// is named by LaneAddress, so an edit still finds it after its effect moved, or after undo brought the
// effect back under the same id. A lane leaves with its owner, and the owner's inverse brings it back.
// A parameter this version does not know keeps its lane in the model: silent, but saved again.
//
// Takes (chapter 17, Step 7): a recording is one audio buffer, and each pass of a loop is a part of it
// (Step 4). The clip plays one of them; choosing another changes its source offset. So a take is a value
// next to the clips: the clip lists stay what they were, and undo needs nothing new.
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
#include <utility>
#include <vector>

namespace dawlab::app {

class VstInsert;
class PluginInstrument;

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

    // ---- clips that follow the tempo (chapter 20, Step 6) -------------------------------------
    // A clip of getClips() may carry how it follows the tempo (Clip::follow). Its start and length are then the
    // song samples it covers under the tempo map of now: setClips() and every change of the map bring them up to
    // date. What the ENGINE plays for the clips of a track - renders, or placeholders while a render is on its
    // way - is getPlayedClips(): the same number of clips in the same order, plain ones, with the audio as it
    // sounds. The arrangement draws those.
    engine::FollowMode getClipFollow(engine::TrackId track, int clipIndex) const;
    // One undo step, "Clip follows the tempo" (or "Clip does not follow the tempo"). The clip keeps its place and
    // its sound; from now on a change of the tempo map moves or stretches it. False for a clip that is not there.
    bool setClipFollow(engine::TrackId track, int clipIndex, engine::FollowMode mode);
    const std::vector<engine::Clip>& getPlayedClips(engine::TrackId track) const;
    // Takes the renders that are ready and hands them to the engine. True if something changed. The window's
    // timer calls it; nothing blocks.
    bool pollRenders();
    // Waits for every render and takes it: before an export, and in tests. Blocks.
    void finishRenders();
    bool hasPendingRenders() const;
    // Clips whose render under the map of now could not be made (longer than the engine renders, or no memory
    // for it): they play their placeholder, so an export would not sound as the project says.
    int getNumFailedRenders() const;

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

    // ---- recorded takes (chapter 17, Step 7) -----------------------------------------------------
    // The edit that puts a recording on a track: a clip plays the best of its takes, in the place of
    // whatever was on the track there. What it covers is cut out of the old clips, not mixed with them.
    edit::SetClips makeTakeEdit(engine::TrackId track, std::shared_ptr<const AudioBuffer> audio,
                                std::vector<engine::Placement> takes, const juce::String& name);
    // `clips` with `clip` in the place of what it covers; the rest of each old clip stays, with new fades.
    std::vector<engine::Clip> withClipOnTop(std::vector<engine::Clip> clips, const engine::Clip& clip) const;
    void registerTakes(std::shared_ptr<const AudioBuffer> audio, std::vector<engine::Placement> takes);
    const std::vector<std::pair<std::shared_ptr<const AudioBuffer>, std::vector<engine::Placement>>>& getTakeSets() const noexcept {
        return takeSets_;
    }
    // The takes this clip can choose from, and which one it plays (-1: none, or it has been cut since).
    const std::vector<engine::Placement>* getTakesOf(const engine::Clip& clip) const noexcept;
    int getCurrentTake(const engine::Clip& clip) const noexcept;
    // Undoable. False if the clip has no such take.
    bool chooseTake(engine::TrackId track, int clipIndex, int takeIndex);

    // ---- instruments and MIDI clips (chapter 18, Step 8) -------------------------------------------
    // WITHOUT recording, like setClips(); the edits SetInstrument and SetMidiClips record.
    bool isInstrument(engine::TrackId track) const noexcept;
    void setInstrument(engine::TrackId track, bool instrument);
    const std::vector<MidiClipModel>& getMidiClips(engine::TrackId track) const; // empty for an unknown id
    // Sorted by start; a clip with id 0 gets a new one. The engine gets each clip's quantized notes.
    void setMidiClips(engine::TrackId track, std::vector<MidiClipModel> clips);
    // A clip id never handed out before.
    int newMidiClipId() noexcept { return nextMidiClipId_++; }
    // Undoable edits of one track's MIDI clips (each is a SetMidiClips of the whole list). A drag passes one
    // merge key for all its steps. They return the clip's index in the (start-sorted) list, or -1.
    int addMidiClip(engine::TrackId track, int64_t startTick, int64_t lengthTicks);
    int moveMidiClip(engine::TrackId track, int index, int64_t newStartTick, std::uint32_t mergeKey);
    int replaceMidiClip(engine::TrackId track, int index, MidiClipModel clip, const juce::String& name, std::uint32_t mergeKey = 0);
    bool removeMidiClip(engine::TrackId track, int index);
    // The timeline sample where a tick plays, through the tempo map (chapter 19).
    int64_t sampleOfTick(int64_t tick) const noexcept;
    int64_t tickOfSample(int64_t sample) const noexcept; // the nearest tick
    double exactTickOfSample(double sample) const noexcept; // with its fraction

    // ---- a track's instrument (chapter 19, Step 6) --------------------------------------------------
    // The synth's settings and the plugin in its place, as values (the plugin's state as it is now).
    InstrumentSnapshot getInstrumentSound(engine::TrackId track) const;
    // WITHOUT recording. The synth's settings go to the engine at once; another plugin file builds that plugin
    // (createPluginInstrument) and puts it in the synth's place; the same file only takes the state. False, with
    // the reason in *error, if the track is not here or the plugin cannot be built - then nothing changed.
    bool setInstrumentSound(engine::TrackId track, const InstrumentSnapshot& sound, juce::String* error = nullptr);
    // Undoable (an edit::SetInstrumentSound): only the synth's settings; a slider's steps share a merge key.
    bool setSynthSettings(engine::TrackId track, const engine::VoiceSettings& settings, std::uint32_t mergeKey = 0);
    // Undoable: a plugin in the synth's place (an empty file: the synth again).
    bool setPluginInstrument(engine::TrackId track, const juce::File& file, juce::String* error = nullptr);
    // The plugin in the synth's place, or nullptr.
    PluginInstrument* getPluginInstrument(engine::TrackId track) const noexcept;
    // Builds a plugin instrument from its file and state. MainComponent sets it with chapter 13's PluginHost; a
    // test may set its own. Without it no plugin can be put on a track.
    std::function<std::unique_ptr<PluginInstrument>(const juce::File& file, const juce::MemoryBlock& state, juce::String& error)>
        createPluginInstrument;

    // ---- the tempo map (chapter 19, Step 5) --------------------------------------------------------
    const engine::TempoMap& getTempoMap() const noexcept { return engine_.getTransport().getTempoMap(); }
    // WITHOUT recording. False (nothing changes) if the points are not a map (TempoMap::set).
    bool setTempoMap(const std::vector<engine::TempoPoint>& points);
    // Undoable (an edit::SetTempoMap).
    bool setTempoPoints(std::vector<engine::TempoPoint> points, const juce::String& name, std::uint32_t mergeKey = 0);

    // ---- insert slots ------------------------------------------------------------------------
    const InsertSlotState& getSlot(engine::ChannelRef ref, int uiSlot) const;
    int chainIndexOf(engine::ChannelRef ref, int uiSlot) const noexcept;
    int getNumUsedSlots(engine::ChannelRef ref) const noexcept;

    // Puts a new built-in effect of `type` into the slot (replacing what was there, with its lanes).
    // Returns the new effect (observer pointer) or nullptr if the engine refused it. `id`: the one
    // undo brings back; none gets a new one.
    engine::InsertEffect* setBuiltinInsert(engine::ChannelRef ref, int uiSlot, engine::EffectType type, engine::EffectId id = {});

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

    // ---- automation (chapter 16, Step 6) --------------------------------------------------------
    const LaneContent* getLane(const LaneAddress& address) const noexcept; // nullptr: no lane
    // Sets or (no content) removes a lane WITHOUT recording, like setClips(). False if its strip or
    // effect is not here. An unknown parameter is fine: the lane stays silent.
    bool setLane(const LaneAddress& address, std::optional<LaneContent> content);
    // The lanes of a strip (its own and its effects').
    std::vector<std::pair<LaneAddress, LaneContent>> getLanesOf(engine::ChannelRef ref) const;
    // Does anything play this lane? False for a parameter this version does not know.
    bool isLaneLive(const LaneAddress& address) const noexcept;
    // What can be automated on a strip: Gain, Pan and the parameters of its built-in effects.
    std::vector<std::pair<juce::String, LaneAddress>> getAutomatableParameters(engine::ChannelRef ref) const;
    // The GUI slot of an effect on a strip, or -1.
    int slotOfEffect(engine::ChannelRef ref, engine::EffectId id) const noexcept;

    // Undoable edits. A gesture passes one merge key for all its steps. addPoint() and movePoint()
    // return the point's index in the lane.
    int addPoint(const LaneAddress& address, std::int64_t position, float value, std::uint32_t mergeKey = 0);
    int movePoint(const LaneAddress& address, int index, std::int64_t position, float value, std::uint32_t mergeKey = 0);
    bool removePoint(const LaneAddress& address, int index);
    bool setLaneMode(const LaneAddress& address, engine::AutomationMode mode);

    // The user's hand on a control that may be automated. touchLane() and releaseLane() are mouse down
    // and mouse up. handMoves() takes a value in the parameter's own unit: if the control has an active
    // lane it goes to the lane's knob and true comes back: the movement is a recording, not an edit of
    // its own. Otherwise false, and the caller makes the usual undoable edit.
    void touchLane(const LaneAddress& address) noexcept;
    void releaseLane(const LaneAddress& address) noexcept;
    bool handMoves(const LaneAddress& address, float value) noexcept;

    // The UI tick: what the audio thread recorded. Each finished pass goes into its curve as one undo
    // step ("Record ..."). Returns the number of passes.
    int collectAutomation();

    // The value the user left a parameter at. While a curve moves the parameter, that is not
    // getParameter(): this is what is saved and what an undo restores.
    float knobValue(engine::InsertEffect& effect, int index) const noexcept;

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
    edit::AddTrack makeNewTrack(engine::TrackFormat format, int index = -1, const juce::String& name = {}, bool instrument = false) const;
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
        std::shared_ptr<engine::FollowTrack> follow; // chapter 20: what the engine plays for `clips` ...
        std::vector<engine::Clip> played;            // ... and a copy of it for the arrangement
        StripModel strip;
        bool instrument = false;               // chapter 18
        std::vector<MidiClipModel> midiClips;  // chapter 18, by start
        engine::VoiceSettings synth;           // chapter 19: the built-in synth's sound
        juce::String pluginFile;               // chapter 19: the plugin in its place (empty: none) ...
        PluginInstrument* plugin = nullptr;    // ... the engine's object, as long as it is there
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
    std::optional<Edit> apply(const edit::SetTempoMap&);
    std::optional<Edit> apply(const edit::SetInstrumentSound&);
    std::optional<Edit> apply(const edit::SetLane&);
    std::optional<Edit> apply(const edit::SetMidiClips&);
    std::optional<Edit> apply(const edit::SetInstrument&);
    void pushMidiClips(const TrackModel& track);
    engine::InsertEffect* effectOf(const LaneAddress& address) const noexcept;
    engine::AutomationLane* laneOf(const LaneAddress& address) const noexcept;
    int parameterIndexOf(const LaneAddress& address) const noexcept; // -1: not known
    void pushLane(const LaneAddress& address);
    void forgetLanesOfEffect(engine::EffectId id);
    void forgetLanesOfTrack(engine::TrackId id);
    std::vector<OwnedLane> ownedLanes(const StripTarget& strip, engine::EffectId effect) const;
    bool restoreLanes(const StripTarget& strip, engine::EffectId effect, const std::vector<OwnedLane>& lanes);
    bool editLane(const LaneAddress& address, LaneContent content, const juce::String& name, std::uint32_t mergeKey);
    LaneContent contentOrNew(const LaneAddress& address) const;
    void removeBusAndMasterInserts();
    void resetBusAndMasterSettings();
    void pushClips(TrackModel& track);
    void placeClips(TrackModel& track); // chapter 20: the clips that follow, to where they are under the map of now
    void retimeClips();                 // every track: placeClips() and pushClips()

    engine::Engine& engine_;
    engine::FollowRenderer renderer_;                         // chapter 20: before tracks_, which use it until they are gone
    std::vector<TrackModel> tracks_;                          // display order
    std::array<StripModel, engine::kNumBuses + 1> fixedStrips_; // the buses, then the master
    std::uint32_t nextTrackId_ = 1;                           // only counts up (see the top)
    int nextMidiClipId_ = 1;                                  // only counts up: the engine's player knows clips by it
    double clipSampleRate_ = 0.0; // 0 = not set yet: follow the engine
    juce::String projectName_ = "Untitled";
    History history_;
    std::vector<std::pair<LaneAddress, LaneContent>> lanes_; // the GUI copy of every lane
    std::vector<std::pair<std::shared_ptr<const AudioBuffer>, std::vector<engine::Placement>>> takeSets_;
    engine::AutomationRecorder recorder_;
};

} // namespace dawlab::app
