#pragma once
// TakeRecorder: one recording, from the record button to clips on the timeline (chapter 17, Step 7).
//
//   message thread                    disk thread (this object's)            audio thread
//   --------------                    ---------------------------            ------------
//   start() --RecordingSession::begin()                                      Recorder, Monitor
//                                     pump(): ring -> file  <---- rings ---- processBlock()
//   stop() --Engine::stopRecording()
//                                     seals; reads each take back
//   tick(): takes the results, one undo step, clips on the tracks
//
// The file is what is safe; the buffer read back from it is what plays. A take is read in whole on the
// disk thread, so the message thread never waits, and a very long take costs memory, not a hang.
//
// Takes live in one folder (Documents/DawLab 18 Takes), not next to the project: a recording may
// begin before there is a project file. They stay there after saving: the project's media folder has its
// own copy. recover() finds what a crash left behind in that folder.

#include "AudioHost.h"
#include "Session.h"

#include <dawlab/engine/RecordingSession.h>

#include <juce_core/juce_core.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace dawlab::app {

class TakeRecorder final : private juce::Thread {
public:
    enum class Phase { Idle, Recording, Finishing };

    // A take shorter than this is a stop just after a wrap, or a slip of the hand.
    static constexpr double kMinTakeSeconds = 0.1;

    TakeRecorder(Session& session, AudioHost& audioHost);
    ~TakeRecorder() override;

    static juce::File defaultFolder();
    void setFolder(const juce::File& folder) { folder_ = folder; }
    juce::File getFolder() const { return folder_; }

    // By hand: how much more (or, negative, less) the real round trip is than the driver reports (Step 2).
    void setTrimSamples(int samples) noexcept { trimSamples_ = samples; }
    int getTrimSamples() const noexcept { return trimSamples_; }
    // Punch: record only between the loop's locators. They are the punch points as well as the loop.
    void setPunchEnabled(bool enabled) noexcept { punch_ = enabled; }
    bool isPunchEnabled() const noexcept { return punch_; }
    // For the checks: a disk that fills up, a shorter ring.
    void setDiskOps(engine::DiskOps ops) { diskOps_ = std::move(ops); }
    void setRingSeconds(double seconds) noexcept { ringSeconds_ = seconds; }

    // ---- message thread ------------------------------------------------------------------------
    // Starts recording all armed tracks; they begin with the first block while playing. Returns what
    // went wrong, empty on success.
    juce::String start();
    // The transport goes on playing.
    void stop();
    // What to say to the user comes through onMessage. Call it from the UI tick.
    void tick();
    Phase getPhase() const noexcept { return phase_; }
    bool isBusy() const noexcept { return phase_ != Phase::Idle; }
    std::function<void(const juce::String& message, bool alarm)> onMessage;

    // What a track has recorded so far, for the arrangement. False if nothing yet.
    bool getLive(engine::TrackId track, engine::LiveTake& live) const;

    // Takes that a crash left in the folder: seals each one and puts it on a new track, where it was
    // played. Returns the number of takes found. Not while busy.
    int recover();

private:
    struct Loaded {
        engine::TrackId track;
        engine::SealedTake take;
        engine::TakeProblem problem = engine::TakeProblem::None;
        std::uint64_t droppedFrames = 0;
        std::shared_ptr<const AudioBuffer> audio;
        double audioRate = 0.0;
        juce::String error;
    };

    void run() override;
    void applyResults();
    // The take's clips at the clip rate: takesOf() in frames of the file, scaled to where they belong.
    std::vector<engine::Placement> placementsOf(const Loaded& loaded, std::shared_ptr<const AudioBuffer>& audio) const;
    void say(const juce::String& message, bool alarm = false) const {
        if (onMessage)
            onMessage(message, alarm);
    }

    Session& session_;
    AudioHost& audioHost_;
    juce::File folder_;
    int trimSamples_ = 0;
    bool punch_ = false;
    double ringSeconds_ = 10.0;
    engine::DiskOps diskOps_;

    Phase phase_ = Phase::Idle;
    bool stopAsked_ = false;
    std::uint64_t droppedShown_ = 0;
    std::unique_ptr<engine::RecordingSession> recording_;
    std::vector<Loaded> results_;       // the disk thread's until finished_
    std::atomic<bool> finished_{false};
};

} // namespace dawlab::app
