// Exercise 5 (starter): an undo history limited by memory.
//
// perform(), undo() and redo() below are Step 5's, unchanged: they do not count bytes yet, and
// trim() does nothing. Make the changes marked TODO; until then the self-check prints FAIL
// (expected). The solution is solutions/ex5_byte_budget.cpp. The rules are in ../ex5_byte_budget.h.

#include "../ex5_byte_budget.h"

namespace minidaw {

bool BudgetHistory::perform(Project& p, const Change& change, std::uint32_t mergeKey) {
    Change inverses;
    if (!applyAll(p, change, inverses))
        return false;
    redo_.clear(); // TODO: these entries' bytes leave bytes_
    if (mergeKey != 0 && mergeKey == openMergeKey_ && !undo_.empty())
        return true;
    undo_.push_back(Entry{std::move(inverses), mergeKey, nextSerial_++, 0}); // TODO: its size, into bytes_
    openMergeKey_ = mergeKey;
    trim();
    return true;
}

bool BudgetHistory::undo(Project& p) {
    endMerge();
    if (undo_.empty())
        return false;
    Change redoEdits;
    if (!applyReversed(p, undo_.back().edits, redoEdits)) {
        undo_.pop_back(); // TODO: bytes
        return false;
    }
    // TODO: the redo entry may be larger than the undo entry it replaces - count both, then trim().
    redo_.push_back(Entry{std::move(redoEdits), 0, undo_.back().serial, 0});
    undo_.pop_back();
    return true;
}

bool BudgetHistory::redo(Project& p) {
    endMerge();
    if (redo_.empty())
        return false;
    Change undoEdits;
    if (!applyAll(p, redo_.back().edits, undoEdits)) {
        redo_.pop_back(); // TODO: bytes
        return false;
    }
    // TODO: bytes, then trim().
    undo_.push_back(Entry{std::move(undoEdits), 0, redo_.back().serial, 0});
    redo_.pop_back();
    return true;
}

void BudgetHistory::trim() {
    // TODO: while over byteLimit_ and more than one entry is left: drop the oldest undo entry
    //       (undo_.front()), or, with none left, the farthest redo entry (redo_.front()).
}

} // namespace minidaw

#include "../ex5_check.h"
