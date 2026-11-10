// Exercise 11 (solution): edit many notes at once.
//
// Build and run:  l18_ex11_solution   (ctest, label "solution"; needs -Juce)
// Your version:   exercises/starter/ex11_note_editor.cpp -> l18_ex11_starter

#include "../exercises/ex11_note_editor.h"

#include <algorithm>

namespace ex11 {

namespace {

constexpr std::int64_t kBar = 4 * 960;

} // namespace

void NoteEditor::selectRect(std::int64_t fromTick, std::int64_t toTick, int lowPitch, int highPitch, bool add) {
    if (!sync())
        return;
    for (size_t i = 0; i < seen_.size(); ++i) {
        const Note& n = seen_[i];
        const bool inside = n.start >= fromTick && n.start < toTick && n.pitch >= lowPitch && n.pitch <= highPitch;
        selected_[i] = inside || (add && selected_[i]);
    }
}

void NoteEditor::selectAll() {
    if (sync())
        selected_.assign(seen_.size(), true);
}

void NoteEditor::selectNone() {
    if (sync())
        selected_.assign(seen_.size(), false);
}

bool NoteEditor::write(std::vector<std::pair<Note, bool>> notes, std::int64_t length, const juce::String& name) {
    const std::vector<MidiClipModel>& clips = session_.getMidiClips(track_);
    int index = -1;
    for (size_t i = 0; i < clips.size() && index < 0; ++i)
        if (clips[i].id == clipId_)
            index = static_cast<int>(i);
    if (index < 0)
        return false;
    std::stable_sort(notes.begin(), notes.end(), [](const auto& a, const auto& b) { return a.first.start < b.first.start; });
    MidiClipModel clip = clips[static_cast<size_t>(index)];
    clip.played.clear();
    std::vector<bool> selected;
    for (const auto& [note, isSelected] : notes) {
        clip.played.push_back(note);
        selected.push_back(isSelected);
    }
    clip.length = std::max(clip.length, length);
    if (session_.replaceMidiClip(track_, index, clip, name) < 0)
        return false;
    seen_ = clip.played;
    selected_ = selected;
    return true;
}

bool NoteEditor::transpose(int semitones) {
    if (!sync() || numSelected() == 0)
        return false;
    std::vector<std::pair<Note, bool>> notes;
    for (size_t i = 0; i < seen_.size(); ++i) {
        Note n = seen_[i];
        if (selected_[i]) {
            n.pitch += semitones;
            if (n.pitch < 0 || n.pitch > 127)
                return false; // all or nothing
        }
        notes.push_back({n, selected_[i]});
    }
    return write(std::move(notes), 0, "Transpose");
}

bool NoteEditor::move(std::int64_t ticks) {
    if (!sync() || numSelected() == 0)
        return false;
    std::vector<std::pair<Note, bool>> notes;
    std::int64_t end = 0;
    for (size_t i = 0; i < seen_.size(); ++i) {
        Note n = seen_[i];
        if (selected_[i]) {
            n.start += ticks;
            if (n.start < 0)
                return false;
        }
        end = std::max(end, n.start + n.length);
        notes.push_back({n, selected_[i]});
    }
    return write(std::move(notes), (end + kBar - 1) / kBar * kBar, "Move Notes");
}

bool NoteEditor::duplicate() {
    if (!sync() || numSelected() == 0)
        return false;
    std::int64_t first = INT64_MAX, last = 0;
    for (size_t i = 0; i < seen_.size(); ++i)
        if (selected_[i]) {
            first = std::min(first, seen_[i].start);
            last = std::max(last, seen_[i].start + seen_[i].length);
        }
    const std::int64_t shift = (last - first + grid_ - 1) / grid_ * grid_;
    std::vector<std::pair<Note, bool>> notes;
    std::int64_t end = 0;
    for (size_t i = 0; i < seen_.size(); ++i) {
        notes.push_back({seen_[i], false});
        if (selected_[i]) {
            Note copy = seen_[i];
            copy.start += shift;
            end = std::max(end, copy.start + copy.length);
            notes.push_back({copy, true});
        }
    }
    return write(std::move(notes), (end + kBar - 1) / kBar * kBar, "Duplicate Notes");
}

bool NoteEditor::removeSelected() {
    if (!sync() || numSelected() == 0)
        return false;
    std::vector<std::pair<Note, bool>> notes;
    for (size_t i = 0; i < seen_.size(); ++i)
        if (!selected_[i])
            notes.push_back({seen_[i], false});
    return write(std::move(notes), 0, "Delete Notes");
}

bool NoteEditor::keyPressed(const juce::KeyPress& key) {
    const bool shift = key.getModifiers().isShiftDown();
    const bool command = key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown();
    const int code = key.getKeyCode();
    // A key it knows is used even when there is nothing to do (nothing selected, a note at the top already).
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
        transpose((code == juce::KeyPress::upKey ? 1 : -1) * (shift ? 12 : 1));
    else if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey)
        move(code == juce::KeyPress::leftKey ? -grid_ : grid_);
    else if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
        removeSelected();
    else if (command && (code == 'D' || code == 'd'))
        duplicate();
    else if (command && (code == 'A' || code == 'a'))
        selectAll();
    else
        return false;
    return true;
}

} // namespace ex11

#include "../exercises/ex11_check.h"
