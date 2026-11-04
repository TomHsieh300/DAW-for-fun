#pragma once
// History: undo and redo (chapter 15, Step 9) - Step 5's History, applied to the Session through
// EditTarget and with a name for each entry ("Delete track Bass"), shown in the Edit menu.
//
//   perform(target, edits, name, key)  apply every edit -> push their inverses, clear redo
//   undo(target)                        apply the inverses, last first -> push THEIR inverses on redo
//   redo(target)                        the same, the other way round
//
// A change is all or nothing: if one edit of it does not apply, the ones already applied are
// taken back. A merge key keeps a gesture (a fader drag) in one entry: while the key stays the
// same, new edits are applied but not recorded, because the entry already leads back to before
// the gesture. Every entry has a serial that is never reused; isModified() compares the newest
// entry's serial with the one at the last save, so it is right after undo, redo and new edits.

#include "Edits.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace dawlab::app {

class History {
public:
    explicit History(std::size_t limit = 200) : limit_(limit > 0 ? limit : 1) {}

    bool perform(EditTarget& target, std::vector<Edit> edits, const juce::String& name, std::uint32_t mergeKey = 0);
    // False if there is nothing to undo / redo, or if the entry no longer applies (the project
    // was changed behind the history's back): that entry is then dropped, the project unchanged.
    bool undo(EditTarget& target);
    bool redo(EditTarget& target);

    // Ends a merge: at the start and the end of every gesture (mouse down / up on a control).
    void endMerge() noexcept { openMergeKey_ = 0; }
    // Forgets everything (a new or opened project). The empty history counts as saved.
    void clear();
    void markSaved() noexcept {
        savedSerial_ = currentSerial();
        endMerge(); // a drag continued after saving must count as a modification
    }
    bool isModified() const noexcept { return currentSerial() != savedSerial_; }

    bool canUndo() const noexcept { return !undo_.empty(); }
    bool canRedo() const noexcept { return !redo_.empty(); }
    juce::String getUndoName() const { return undo_.empty() ? juce::String() : undo_.back().name; }
    juce::String getRedoName() const { return redo_.empty() ? juce::String() : redo_.back().name; }
    std::size_t undoCount() const noexcept { return undo_.size(); }
    std::size_t redoCount() const noexcept { return redo_.size(); }

private:
    struct Entry {
        std::vector<Edit> edits; // undo stack: the inverses; redo stack: the edits to redo
        juce::String name;
        std::uint32_t mergeKey = 0;
        std::uint64_t serial = 0;
    };

    std::uint64_t currentSerial() const noexcept { return undo_.empty() ? 0 : undo_.back().serial; }

    std::deque<Entry> undo_; // oldest first
    std::vector<Entry> redo_; // the next redo last
    std::size_t limit_;
    std::uint32_t openMergeKey_ = 0;
    std::uint64_t nextSerial_ = 1;
    std::uint64_t savedSerial_ = 0;
};

} // namespace dawlab::app
