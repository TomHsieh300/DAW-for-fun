// Exercise 11 (starter): edit many notes at once.
//
// Fill in the TODOs. Until then the self-check prints FAIL (expected). The solution is
// solutions/ex11_note_editor.cpp. What the functions must do is in ../ex11_note_editor.h.

#include "../ex11_note_editor.h"

namespace ex11 {

void NoteEditor::selectRect(std::int64_t fromTick, std::int64_t toTick, int lowPitch, int highPitch, bool add) {
    // TODO: sync(), then selected_[i] for every note in the rectangle (or kept, when `add`).
    (void)fromTick;
    (void)toTick;
    (void)lowPitch;
    (void)highPitch;
    (void)add;
}

void NoteEditor::selectAll() {
    // TODO
}

void NoteEditor::selectNone() {
    // TODO
}

bool NoteEditor::write(std::vector<std::pair<Note, bool>> notes, std::int64_t length, const juce::String& name) {
    // TODO: find the clip's index by its id; sort the pairs by start (stable); the notes into a copy of the clip,
    //       its length at least `length`; Session::replaceMidiClip(); then seen_ and selected_ from the pairs.
    (void)notes;
    (void)length;
    (void)name;
    return false;
}

bool NoteEditor::transpose(int semitones) {
    // TODO: every selected note's pitch, all or nothing; write(..., "Transpose").
    (void)semitones;
    return false;
}

bool NoteEditor::move(std::int64_t ticks) {
    // TODO: every selected note's start, none before 0; the clip to the end of the last note's bar.
    (void)ticks;
    return false;
}

bool NoteEditor::duplicate() {
    // TODO: the span of the selection rounded up to grid_; the copies after it, selected, the originals not.
    return false;
}

bool NoteEditor::removeSelected() {
    // TODO
    return false;
}

bool NoteEditor::keyPressed(const juce::KeyPress& key) {
    // TODO: Up / Down (Shift: 12), Left / Right (grid_), Ctrl+D, Delete / Backspace, Ctrl+A.
    (void)key;
    return false;
}

} // namespace ex11

#include "../ex11_check.h"
