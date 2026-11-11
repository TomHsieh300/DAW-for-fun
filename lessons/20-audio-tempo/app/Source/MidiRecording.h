#pragma once
// MidiRecording: what the keyboard plays while recording, as MIDI clips (chapter 18, Step 8 - Step 6 in the app).
//
//   driver's thread --AudioHost::pushMidi--> record FIFO --poll() on the UI thread--> NoteRecorder (per track)
//   audio thread ----AudioHost::Anchor-------------------> where in the song each time was heard
//
// A key's place is where the player heard the song when it went down (Step 6): the message's time through
// the anchor of the recording's first block,
//
//   unwrapped sample = anchor.songSample + (time - anchor.blockTime) x rate - mixLatency
//
// "Unwrapped": the anchor of the FIRST block, so the position just goes on growing through a loop's wraps, and
// the recorder wraps it itself into passes. The thru delay (AudioHost, Step 5) plays no part: the note goes
// where the key went down, not where its sound came out.
//
// stop() turns every armed instrument track's take into one new clip over what was recorded, and, in Replace
// mode, erases the notes that the take played over - of the new clip's earlier passes and of the old clips
// (Step 6's rule: the last pass that covered a place keeps it). All of it is one undo step.

#include "AudioHost.h"
#include "Edits.h"
#include "Session.h"

#include <vector>

namespace dawlab::app {

// Step 6, in ticks of the song.
struct LoopTicks {
    int64_t from = 0, to = 0;
    bool on() const noexcept { return to > from; }
    int64_t length() const noexcept { return to - from; }
};
struct Coverage {
    int pass = 0;
    int64_t from = 0, to = 0;
};
struct TakeNote {
    engine::Note note; // start in song ticks
    int pass = 0;
};

class NoteRecorder {
public:
    void start(int64_t startTick, LoopTicks loop);
    void noteOn(int64_t u, int pitch, int velocity, int channel);
    void noteOff(int64_t u, int pitch, int channel);
    void stop(int64_t u);
    int passOf(int64_t u) const noexcept;
    int64_t songTickOf(int64_t u) const noexcept;
    int64_t passEnd(int pass) const noexcept;
    const std::vector<TakeNote>& notes() const noexcept { return notes_; }
    const std::vector<Coverage>& passes() const noexcept { return passes_; }
    bool covered(int64_t songTick, int laterThanPass) const noexcept; // by a pass after `laterThanPass`

private:
    struct Open {
        int64_t on = 0;
        int pitch = 0, velocity = 0, channel = 0;
    };
    int64_t start_ = 0;
    LoopTicks loop_;
    std::vector<TakeNote> notes_;
    std::vector<Coverage> passes_;
    std::vector<Open> open_;
};

enum class MidiRecordMode { Overdub, Replace };

class MidiRecording {
public:
    MidiRecording(Session& session, AudioHost& host) : session_(session), host_(host) {}

    // Starts recording the armed instrument tracks at the transport's position; messages that came before are
    // dropped. False if no instrument track is armed.
    bool start(MidiRecordMode mode);
    bool isRecording() const noexcept { return recording_; }
    // UI tick: the keyboard's messages so far, into the recorders. Before the first block has been heard
    // (no anchor of this recording yet) they wait in the FIFO.
    void poll();
    // Stops at the time now: the takes become clips, one undo step "Record MIDI". Returns the number of notes.
    int stop();
    MidiRecordMode getMode() const noexcept { return mode_; }

private:
    int64_t unwrappedTick(double time) const noexcept;

    Session& session_;
    AudioHost& host_;
    bool recording_ = false;
    int64_t startSample_ = 0;   // the transport's position when the recording started
    int64_t loopStartSample_ = 0, loopEndSample_ = 0; // the loop then, in samples (none: equal) and in ticks
    LoopTicks loopTicks_;
    int64_t startRendered_ = 0; // the anchors' renderedBefore of the recording's first block
    uint64_t startBlock_ = 0;   // the last block before the recording started
    MidiRecordMode mode_ = MidiRecordMode::Overdub;
    int64_t startTick_ = 0;
    std::vector<std::pair<engine::TrackId, NoteRecorder>> takes_;
};

} // namespace dawlab::app
