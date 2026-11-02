// Step 5 (starter): undo and redo.
//
// Build and run:  l15_step05_starter   (prints [FAIL] until all four TODOs are done)
// Reference:      ../solution/step05.cpp, docs/step-05-undo.md ("Your turn")
//
// The edits (AddTrack, RemoveTrack, SetGain, RenameTrack, MoveTrack), Change, applyAll() and
// History with its data members are in ../minidaw.h.

#include "../minidaw.h"

namespace minidaw {

std::optional<Edit> applyEdit(Project& p, const Edit& edit) {
    // TODO 1. Write one small applyKind() function per kind of edit and call the right one with
    //   return std::visit([&p](const auto& e) { return applyKind(p, e); }, edit);
    // Each checks first and changes `p` only if the edit applies (else: return std::nullopt):
    //   AddTrack     id valid and not in use; insert at clamp(index, 0, size); insert the clips at
    //                their positions in ascending order; nextTrackId = max(nextTrackId, id + 1);
    //                return RemoveTrack{id}
    //   RemoveTrack  build AddTrack{the track, its index, (position, clip) of each of its clips},
    //                then erase its clips and the track; return that AddTrack
    //   SetGain / RenameTrack / MoveTrack: remember the old gain / name / index, set the new one
    //                (MoveTrack: clamp to [0, size - 1], as in Step 1); return the same kind of
    //                edit with the old value
    (void)p;
    (void)edit;
    return std::nullopt;
}

bool History::perform(Project& p, const Change& change, std::uint32_t mergeKey) {
    // TODO 2. Change inverses; if (!applyAll(p, change, inverses)) return false - and touch
    // nothing else. Then clear redo_. If mergeKey != 0, mergeKey == openMergeKey_ and undo_ is
    // not empty: return true (merged - the newest entry already undoes the whole drag).
    // Otherwise push Entry{inverses, mergeKey, nextSerial_++}, pop_front() while over limit_,
    // set openMergeKey_ = mergeKey, return true.
    (void)p;
    (void)change;
    (void)mergeKey;
    return false;
}

bool History::undo(Project& p) {
    // TODO 3. endMerge(). Nothing to undo -> false. Apply the newest entry's edits LAST FIRST:
    // reverse a copy, applyAll() it, and reverse the inverses you get back, so that they are in
    // the original order again. If that fails: pop the entry (it no longer fits the project) and
    // return false - applyAll() has already put the project back. Otherwise push
    // Entry{those inverses, 0, the entry's serial} on redo_, pop the undo entry, return true.
    (void)p;
    return false;
}

bool History::redo(Project& p) {
    // TODO 4. The mirror of undo(): endMerge(); the newest redo entry's edits in their own
    // order (applyAll); on failure pop it and return false; otherwise push the inverses with the
    // same serial on undo_ (pop_front() when over limit_), pop the redo entry, return true.
    (void)p;
    return false;
}

} // namespace minidaw

#include "../check.h"
