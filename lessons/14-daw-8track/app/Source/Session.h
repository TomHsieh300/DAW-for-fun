#pragma once
// Session: the application's model of the project (Steps 15, 16 and 18).
//
// The engine (stage 1) knows *what to play*: clip lists, insert chains, fader values. It does
// not know what the GUI needs to show: "slot 3 of track 2 holds a Compressor", the name of a
// clip, which slots are empty. Session is that bookkeeping layer. It lives on the message
// thread and is the only object that sends structural changes to the engine:
//
//   GUI component --> Session --> Engine::addInsert / replaceInsert / removeInsert /
//                                  setTrackClips   (command queue, Step 7)
//                 --> Engine::getMixer().getTrack(t).setGainDb(...)   (atomics, Step 4)
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

#include "MediaLoader.h" // ConvertedBuffers

#include <juce_events/juce_events.h>
#include <juce_core/juce_core.h>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace dawlab::app {

class VstInsert;

constexpr int kInsertSlotsPerStrip = 4;
constexpr int kNumStrips = engine::kNumTracks + engine::kNumBuses + 1; // tracks, buses, master

// Strip index (0 .. kNumStrips-1, mixer order) <-> engine::ChannelRef.
int stripIndexOf(engine::ChannelRef ref) noexcept;
engine::ChannelRef stripRefAt(int stripIndex) noexcept;

enum class SlotKind { Empty, Builtin, Vst3 };

struct InsertSlotState {
    SlotKind kind = SlotKind::Empty;
    engine::EffectType type = engine::EffectType::Gain; // valid when kind == Builtin
    engine::InsertEffect* effect = nullptr;             // observer; valid while it sits in the slot
    VstInsert* vst = nullptr;                           // the same object, when kind == Vst3
    juce::String name;                                  // what the slot button shows

    bool isEmpty() const noexcept { return kind == SlotKind::Empty || effect == nullptr; }
};

class Session final : public juce::ChangeBroadcaster {
public:
    explicit Session(engine::Engine& engine);
    ~Session() override;

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    engine::Engine& getEngine() noexcept { return engine_; }
    const engine::Engine& getEngine() const noexcept { return engine_; }
    double getSampleRate() const noexcept { return engine_.getSampleRate(); }

    // ---- project ---------------------------------------------------------------------------
    // Empty project: 8 empty tracks, default names, faders at 0 dB, tempo 120, loop bars 1-4
    // (off), and a Limiter (ceiling -1 dBFS) in master slot 4. The engine must be prepared.
    void newProject();

    // Replaces the whole project with `song` (generated at the engine's sample rate, see
    // MediaLoader::loadDemoSong). Returns the number of unknown parameter names (0 expected).
    int applyDemoSong(const engine::DemoSong& song);

    juce::String getProjectName() const { return projectName_; }

    // ---- tracks and clips --------------------------------------------------------------------
    const std::vector<engine::Clip>& getClips(int track) const;
    void setClips(int track, std::vector<engine::Clip> clips);

    // Replaces the track's clips by one clip that plays `audio` from timeline sample `start`.
    // The audio must already be at getClipSampleRate() (MediaLoader resamples it).
    void setTrackAudio(int track, std::shared_ptr<const AudioBuffer> audio, const juce::String& clipName,
                       int64_t start = 0);

    // Moves clip `clipIndex` of `track` to `newStart` (clamped to >= 0). Returns the clip's new
    // index in the (start-sorted) list, or -1 if the arguments are invalid.
    int moveClip(int track, int clipIndex, int64_t newStart);

    // End of the last clip on any track (GUI copy; 0 for an empty project).
    int64_t getSongEndSample() const;

    juce::String getStripName(engine::ChannelRef ref) const;
    void setStripName(engine::ChannelRef ref, const juce::String& name);

    // Mono / stereo track (Step 4). Loading a file sets it from the file's channel count
    // (Step 18); the strip's format button switches it by hand (Step 15).
    engine::TrackFormat getTrackFormat(int track) const;
    void setTrackFormat(int track, engine::TrackFormat format);

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

    // First insert on the strip whose InsertEffect::getName() equals `effectName`.
    engine::InsertEffect* findInsert(engine::ChannelRef ref, const char* effectName) const noexcept;

    // Called (message thread) right before an insert leaves a slot, so editor windows that point
    // at it can close while the pointer is still valid.
    std::function<void(engine::InsertEffect*)> onInsertWillBeRemoved;

    // ---- master strip helpers (the "Ceiling" knob drives the first Limiter on the master) -----
    bool hasMasterLimiter() const noexcept;
    float getMasterCeilingDb() const noexcept;
    void setMasterCeilingDb(float db) noexcept;

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

private:
    struct StripModel {
        std::array<InsertSlotState, kInsertSlotsPerStrip> slots;
    };

    StripModel& strip(engine::ChannelRef ref);
    const StripModel& strip(engine::ChannelRef ref) const;
    void removeAllInserts();
    void resetMixerSettings();
    void pushClips(int track);

    engine::Engine& engine_;
    std::array<StripModel, kNumStrips> strips_;
    std::array<std::vector<engine::Clip>, engine::kNumTracks> clips_;
    double clipSampleRate_ = 0.0; // 0 = not set yet: follow the engine
    juce::String projectName_ = "Untitled";
};

} // namespace dawlab::app
