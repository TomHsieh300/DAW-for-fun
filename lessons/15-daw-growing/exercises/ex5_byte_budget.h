#pragma once
// Exercise 5 - An undo history limited by memory, not by a count.
//
// Step 5's History keeps the last 100 entries. That is the wrong measure: renaming a track costs a
// few bytes, but the inverse of deleting a track holds its clips - and in the app (Step 9) a clip
// holds its audio in memory. As long as an undo entry can bring a deleted clip back, its audio
// cannot be freed. Delete ten long tracks and the history alone may keep gigabytes alive.
//
// BudgetHistory keeps every entry it can within a byte budget:
//   - bytes() is the sum of changeBytes() over every undo AND redo entry, kept up to date as
//     entries come and go (no recounting);
//   - whenever it is above the budget, entries are dropped - the oldest undo entry first, then,
//     with no undo entry left, the redo entry farthest away - until it fits;
//   - but the last remaining entry is never dropped: the user can always undo the last thing
//     they did, even if that one entry alone is over the budget.
// Everything else is Step 5's History: merging, the redo stack, serials for isModified().

#include "model.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace minidaw {

// What a clip's audio costs in memory: mono 32-bit float (the app's clips are float too).
constexpr std::size_t kBytesPerSample = 4;

// What one change costs while the history holds it. The clips an AddTrack would bring back count
// with their audio; the rest is the size of the edit and of its strings.
inline std::size_t changeBytes(const Change& change) {
    std::size_t bytes = 0;
    for (const Edit& edit : change) {
        bytes += sizeof(Edit);
        if (const auto* add = std::get_if<AddTrack>(&edit)) {
            bytes += add->track.name.size();
            for (const auto& [position, clip] : add->clips)
                bytes += sizeof(position) + sizeof(clip) + clip.file.size() + static_cast<std::size_t>(clip.length) * kBytesPerSample;
        } else if (const auto* rename = std::get_if<RenameTrack>(&edit)) {
            bytes += rename->name.size();
        }
    }
    return bytes;
}

class BudgetHistory {
public:
    explicit BudgetHistory(std::size_t byteLimit) : byteLimit_(byteLimit) {}

    // ---- YOUR TURN (starter/ex5_byte_budget.cpp) -------------------------------------------------
    // Step 5's perform(), undo() and redo(), keeping bytes_ right and calling trim() whenever an
    // entry was added.
    bool perform(Project& p, const Change& change, std::uint32_t mergeKey = 0);
    bool undo(Project& p);
    bool redo(Project& p);

    // ---- provided -----------------------------------------------------------------------------
    void endMerge() noexcept { openMergeKey_ = 0; }
    void markSaved() noexcept {
        savedSerial_ = currentSerial();
        endMerge();
    }
    bool isModified() const noexcept { return currentSerial() != savedSerial_; }
    bool canUndo() const noexcept { return !undo_.empty(); }
    bool canRedo() const noexcept { return !redo_.empty(); }
    std::size_t undoCount() const noexcept { return undo_.size(); }
    std::size_t redoCount() const noexcept { return redo_.size(); }
    std::size_t byteLimit() const noexcept { return byteLimit_; }
    std::size_t bytes() const noexcept { return bytes_; }
    // The same sum, counted again from the entries: for the checks.
    std::size_t recountBytes() const {
        std::size_t sum = 0;
        for (const Entry& e : undo_)
            sum += changeBytes(e.edits);
        for (const Entry& e : redo_)
            sum += changeBytes(e.edits);
        return sum;
    }

private:
    struct Entry {
        Change edits;
        std::uint32_t mergeKey = 0;
        std::uint64_t serial = 0;
        std::size_t bytes = 0; // changeBytes(edits), so that removing an entry is cheap
    };

    // YOUR TURN: drop entries until bytes_ <= byteLimit_ or only one entry is left (see above).
    void trim();

    std::uint64_t currentSerial() const noexcept { return undo_.empty() ? 0 : undo_.back().serial; }

    std::deque<Entry> undo_; // oldest first
    std::deque<Entry> redo_; // the next redo last; the farthest one first
    std::size_t byteLimit_;
    std::size_t bytes_ = 0;
    std::uint32_t openMergeKey_ = 0;
    std::uint64_t nextSerial_ = 1;
    std::uint64_t savedSerial_ = 0;
};

} // namespace minidaw
