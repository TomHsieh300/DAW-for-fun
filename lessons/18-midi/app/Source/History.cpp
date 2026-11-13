#include "History.h"

#include <algorithm>

namespace dawlab::app {

namespace {

// Applies `edits` in order, all or nothing; on success `inverses` holds one inverse per edit.
bool applyAll(EditTarget& target, const std::vector<Edit>& edits, std::vector<Edit>& inverses) {
    inverses.clear();
    for (const Edit& e : edits) {
        std::optional<Edit> inverse = target.applyEdit(e);
        if (!inverse) {
            for (auto it = inverses.rbegin(); it != inverses.rend(); ++it)
                target.applyEdit(*it);
            inverses.clear();
            return false;
        }
        inverses.push_back(std::move(*inverse));
    }
    return true;
}

// Applies `edits` last first (an undo); on success `inverses` is in the ORIGINAL order again.
bool applyReversed(EditTarget& target, const std::vector<Edit>& edits, std::vector<Edit>& inverses) {
    const std::vector<Edit> reversed(edits.rbegin(), edits.rend());
    if (!applyAll(target, reversed, inverses))
        return false;
    std::reverse(inverses.begin(), inverses.end());
    return true;
}

} // namespace

std::uint32_t mergeKeyFor(const StripTarget& strip, std::uint32_t what) noexcept {
    // Any mix that differs for different controls and is never 0 ("no merge").
    std::uint32_t h = 2166136261u;
    for (std::uint32_t v : {static_cast<std::uint32_t>(strip.kind), strip.track.value, static_cast<std::uint32_t>(strip.bus), what})
        h = (h ^ v) * 16777619u;
    return h != 0 ? h : 1;
}

bool History::perform(EditTarget& target, std::vector<Edit> edits, const juce::String& name, std::uint32_t mergeKey) {
    std::vector<Edit> inverses;
    if (!applyAll(target, edits, inverses))
        return false; // nothing changed, not even the redo stack
    redo_.clear();
    if (mergeKey != 0 && mergeKey == openMergeKey_ && !undo_.empty())
        return true; // the newest entry already leads back to before the gesture
    undo_.push_back(Entry{std::move(inverses), name, mergeKey, nextSerial_++});
    if (undo_.size() > limit_)
        undo_.pop_front();
    openMergeKey_ = mergeKey;
    return true;
}

bool History::undo(EditTarget& target) {
    endMerge();
    if (undo_.empty())
        return false;
    std::vector<Edit> redoEdits;
    if (!applyReversed(target, undo_.back().edits, redoEdits)) {
        undo_.pop_back(); // no longer applies: drop it
        return false;
    }
    redo_.push_back(Entry{std::move(redoEdits), undo_.back().name, 0, undo_.back().serial});
    undo_.pop_back();
    return true;
}

bool History::redo(EditTarget& target) {
    endMerge();
    if (redo_.empty())
        return false;
    std::vector<Edit> undoEdits;
    if (!applyAll(target, redo_.back().edits, undoEdits)) {
        redo_.pop_back();
        return false;
    }
    undo_.push_back(Entry{std::move(undoEdits), redo_.back().name, 0, redo_.back().serial});
    redo_.pop_back();
    if (undo_.size() > limit_)
        undo_.pop_front();
    return true;
}

void History::clear() {
    undo_.clear();
    redo_.clear();
    openMergeKey_ = 0;
    savedSerial_ = 0;
}

void History::forgetEntries() {
    const bool modified = isModified();
    clear();
    if (modified)
        markModified();
}

} // namespace dawlab::app
