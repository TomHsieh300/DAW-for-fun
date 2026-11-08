// Exercise 5 (solution): an undo history limited by memory.
//
// Build and run:  l15_ex5_solution   (ctest, label "solution")
// Your version:   exercises/starter/ex5_byte_budget.cpp -> l15_ex5_starter

#include "../exercises/ex5_byte_budget.h"

namespace minidaw {

bool BudgetHistory::perform(Project& p, const Change& change, std::uint32_t mergeKey) {
    Change inverses;
    if (!applyAll(p, change, inverses))
        return false;
    for (const Entry& e : redo_) // a new edit starts a new future: the redo entries are gone
        bytes_ -= e.bytes;
    redo_.clear();
    if (mergeKey != 0 && mergeKey == openMergeKey_ && !undo_.empty())
        return true; // merged: the newest entry is unchanged, and so is its size
    const std::size_t size = changeBytes(inverses);
    undo_.push_back(Entry{std::move(inverses), mergeKey, nextSerial_++, size});
    bytes_ += size;
    openMergeKey_ = mergeKey;
    trim();
    return true;
}

bool BudgetHistory::undo(Project& p) {
    endMerge();
    if (undo_.empty())
        return false;
    Change redoEdits;
    const bool applied = applyReversed(p, undo_.back().edits, redoEdits);
    bytes_ -= undo_.back().bytes;
    const std::uint64_t serial = undo_.back().serial;
    undo_.pop_back();
    if (!applied)
        return false; // no longer applies: dropped
    // The other direction may cost more: undoing an add puts the whole track into the redo entry.
    const std::size_t size = changeBytes(redoEdits);
    redo_.push_back(Entry{std::move(redoEdits), 0, serial, size});
    bytes_ += size;
    trim();
    return true;
}

bool BudgetHistory::redo(Project& p) {
    endMerge();
    if (redo_.empty())
        return false;
    Change undoEdits;
    const bool applied = applyAll(p, redo_.back().edits, undoEdits);
    bytes_ -= redo_.back().bytes;
    const std::uint64_t serial = redo_.back().serial;
    redo_.pop_back();
    if (!applied)
        return false;
    const std::size_t size = changeBytes(undoEdits);
    undo_.push_back(Entry{std::move(undoEdits), 0, serial, size});
    bytes_ += size;
    trim();
    return true;
}

void BudgetHistory::trim() {
    while (bytes_ > byteLimit_ && undo_.size() + redo_.size() > 1) {
        if (!undo_.empty()) {
            bytes_ -= undo_.front().bytes; // the oldest past first
            undo_.pop_front();
        } else {
            bytes_ -= redo_.front().bytes; // then the farthest future
            redo_.pop_front();
        }
    }
}

} // namespace minidaw

#include "../exercises/ex5_check.h"
