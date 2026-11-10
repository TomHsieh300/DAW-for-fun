#pragma once
// Exercise 11 - Edit many notes at once (optional, JUCE).
//
// Step 8's piano roll edits one note at a time. Real work is a chord moved up a third, a phrase moved a beat
// later, a bar duplicated to fill a chorus. This exercise is the editing behind such commands, without changing
// the app: a selection of a clip's notes, and operations on it, each ONE undo step through
// Session::replaceMidiClip(). The notes are the clip's notes as played (MidiClipModel::played): what quantize
// starts from.
//
//   select      by a rectangle - starts in [fromTick, toTick), pitches in [lowPitch, highPitch] - adding to the
//               selection or replacing it; all; none.
//   transpose   every selected note by some semitones. If one of them would leave 0 .. 127, nothing moves.
//   move        every selected note by some ticks. If one of them would start before 0, nothing moves. The clip
//               grows to the end of the bar of the last note when they go past its end.
//   duplicate   a copy of the selected notes right after them: shifted by the selection's span (the first start to
//               the last end), rounded up to the grid. The copies become the selection. The clip grows to the end
//               of the bar of the last copy when they go past its end.
//   remove      the selected notes go; nothing is selected.
//   keys        Up / Down a semitone, with Shift an octave; Left / Right the grid; Ctrl+D duplicate; Delete or
//               Backspace remove; Ctrl+A all. True for a key it used.
//
// The selection belongs to the notes, not to their places in the list: the list is sorted by start after every
// change, and a moved note must stay selected. And it belongs to the clip as the editor last saw it: when the clip
// changes in another way - an undo, the piano roll, a quantize - the selection is cleared rather than pointing at
// other notes.

#include "MainComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <vector>

namespace ex11 {

using dawlab::app::MidiClipModel;
using dawlab::app::Session;
using dawlab::engine::Note;
using dawlab::engine::TrackId;

class NoteEditor {
public:
    NoteEditor(Session& session, TrackId track, int clipId) : session_(session), track_(track), clipId_(clipId) { sync(); }

    // ---- provided ---------------------------------------------------------------------------------------------
    // The clip now (by its id), or nullptr when it is gone.
    const MidiClipModel* getClip() const noexcept {
        for (const MidiClipModel& c : session_.getMidiClips(track_))
            if (c.id == clipId_)
                return &c;
        return nullptr;
    }
    // Whether note i of the clip's played notes is selected (false for a stale selection).
    bool isSelected(size_t i) {
        sync();
        return i < selected_.size() && selected_[i];
    }
    int numSelected() {
        sync();
        int n = 0;
        for (const bool s : selected_)
            n += s ? 1 : 0;
        return n;
    }
    void setGrid(std::int64_t ticks) noexcept { grid_ = std::max<std::int64_t>(ticks, 1); }

    // ---- YOUR TURN (starter/ex11_note_editor.cpp) ----------------------------------------------------------------
    void selectRect(std::int64_t fromTick, std::int64_t toTick, int lowPitch, int highPitch, bool add);
    void selectAll();
    void selectNone();
    bool transpose(int semitones);    // "Transpose"
    bool move(std::int64_t ticks);    // "Move Notes"
    bool duplicate();                 // "Duplicate Notes"
    bool removeSelected();            // "Delete Notes"
    bool keyPressed(const juce::KeyPress& key);

private:
    // ---- provided ---------------------------------------------------------------------------------------------
    // If the clip is not what the editor last saw, the selection is cleared. False when the clip is gone.
    bool sync() {
        const MidiClipModel* clip = getClip();
        if (clip == nullptr) {
            selected_.clear();
            seen_.clear();
            return false;
        }
        if (clip->played != seen_) {
            seen_ = clip->played;
            selected_.assign(seen_.size(), false);
        }
        return true;
    }

    // ---- YOUR TURN ----------------------------------------------------------------------------------------------
    // Writes `notes` - each with whether it is selected - into the clip as one undo step called `name`: sorted by
    // start (stable), the clip at least `length` ticks long; the selection follows the notes.
    bool write(std::vector<std::pair<Note, bool>> notes, std::int64_t length, const juce::String& name);

    Session& session_;
    TrackId track_;
    int clipId_ = 0;
    std::int64_t grid_ = 240;
    std::vector<Note> seen_;     // the clip's played notes as the editor last saw them
    std::vector<bool> selected_; // one per seen_ note
};

} // namespace ex11
