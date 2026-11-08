#pragma once
// Exercise 5 - the self-check (main). Included at the end of the starter and of the solution.

#include "ex5_byte_budget.h"

#include <dawlab/Check.h>

#include <cstdio>
#include <random>

namespace minidaw::ex5_check {

constexpr std::int64_t kLong = 1000000;                      // 1 M samples = 4 MB of audio
constexpr std::size_t kMB = 1000000;

// `n` tracks, each with one long clip.
inline Project bigProject(int n) {
    Project p;
    for (int i = 0; i < n; ++i) {
        const TrackId id{p.nextTrackId++};
        p.tracks.push_back({id, "Track " + std::to_string(id.value), 0.0f, 0.0f});
        p.clips.push_back({id, 0, kLong, "take" + std::to_string(id.value) + ".wav"});
    }
    return p;
}

inline bool consistent(const BudgetHistory& h) {
    return h.bytes() == h.recountBytes() && (h.bytes() <= h.byteLimit() || h.undoCount() + h.redoCount() <= 1);
}

inline void checkBudget() {
    Project p = bigProject(6);
    BudgetHistory h(10 * kMB);

    // Three deleted tracks would hold 12 MB: the oldest goes.
    for (std::uint32_t id = 1; id <= 3; ++id) {
        DAWLAB_CHECK(h.perform(p, {RemoveTrack{TrackId{id}}}));
        DAWLAB_CHECK(h.bytes() == h.recountBytes());
    }
    std::printf("  3 deleted tracks of 4 MB, a budget of 10 MB: %zu entries, %zu bytes\n", h.undoCount(), h.bytes());
    DAWLAB_CHECK(h.undoCount() == 2);
    DAWLAB_CHECK(h.bytes() > 8 * kMB && h.bytes() <= 10 * kMB);
    DAWLAB_CHECK(h.undo(p) && h.undo(p) && !h.undo(p));
    DAWLAB_CHECK(p.tracks.size() == 5 && indexOf(p, TrackId{1}) < 0); // track 1 is gone for good
    DAWLAB_CHECK(h.redoCount() == 2 && h.bytes() == h.recountBytes()); // redo entries are small
    DAWLAB_CHECK(h.redo(p) && h.redo(p) && p.tracks.size() == 3);

    // Small edits are cheap: a thousand of them fit, no count limit.
    for (int i = 0; i < 1000; ++i)
        DAWLAB_CHECK(h.perform(p, {SetGain{TrackId{4}, static_cast<float>(-i % 24)}}));
    DAWLAB_CHECK(h.undoCount() == 1002 && h.bytes() == h.recountBytes());
    // A merged drag adds no entry and no bytes.
    const std::size_t before = h.bytes();
    for (int i = 0; i < 50; ++i)
        DAWLAB_CHECK(h.perform(p, {SetGain{TrackId{5}, -0.1f * static_cast<float>(i)}}, 42));
    DAWLAB_CHECK(h.undoCount() == 1003 && h.bytes() > before && h.bytes() == h.recountBytes());
    h.endMerge();
    // A change that does not apply changes nothing.
    const std::size_t now = h.bytes();
    DAWLAB_CHECK(!h.perform(p, {SetGain{TrackId{99}, 0.0f}}));
    DAWLAB_CHECK(h.bytes() == now && h.undoCount() == 1003);
}

inline void checkOneBigEntry() {
    Project p = bigProject(3);
    BudgetHistory h(1 * kMB);
    DAWLAB_CHECK(h.perform(p, {RemoveTrack{TrackId{1}}}));
    DAWLAB_CHECK(h.undoCount() == 1 && h.bytes() > 4 * kMB); // over the budget, but the last thing stays undoable
    DAWLAB_CHECK(h.perform(p, {RemoveTrack{TrackId{2}}}));
    DAWLAB_CHECK(h.undoCount() == 1 && h.bytes() == h.recountBytes());
    DAWLAB_CHECK(h.undo(p) && indexOf(p, TrackId{2}) >= 0);
    DAWLAB_CHECK(!h.undo(p) && indexOf(p, TrackId{1}) < 0);
}

inline void checkRedoGrows() {
    // Undoing an add puts the whole track - with its audio - into the redo entry.
    Project p = bigProject(2);
    BudgetHistory h(10 * kMB);
    DAWLAB_CHECK(h.perform(p, {RemoveTrack{TrackId{1}}}));
    DAWLAB_CHECK(h.perform(p, {RemoveTrack{TrackId{2}}}));
    AddTrack add = newTrack(p, "New");
    add.clips.emplace_back(0, ClipState{add.track.id, 0, kLong, "new.wav"});
    DAWLAB_CHECK(h.perform(p, {add}));
    DAWLAB_CHECK(h.undoCount() == 3 && h.bytes() < 9 * kMB); // the add's inverse is small
    DAWLAB_CHECK(h.undo(p));
    std::printf("  undoing an add of 4 MB next to 8 MB: %zu undo, %zu redo, %zu bytes\n", h.undoCount(), h.redoCount(), h.bytes());
    DAWLAB_CHECK(h.undoCount() == 1 && h.redoCount() == 1); // the oldest undo entry made room
    DAWLAB_CHECK(h.bytes() == h.recountBytes() && h.bytes() <= 10 * kMB);
    DAWLAB_CHECK(h.redo(p) && p.tracks.size() == 1 && p.clips.size() == 1);
    DAWLAB_CHECK(h.bytes() == h.recountBytes() && h.bytes() < 5 * kMB);

    // With no undo entry left, the farthest redo entry goes.
    Project q = bigProject(0);
    BudgetHistory hq(6 * kMB);
    for (int i = 0; i < 2; ++i) {
        AddTrack a = newTrack(q, "Add");
        a.clips.emplace_back(static_cast<int>(q.clips.size()), ClipState{a.track.id, 0, kLong, "a.wav"});
        DAWLAB_CHECK(hq.perform(q, {a}));
    }
    DAWLAB_CHECK(hq.undo(q));  // 4 MB in redo
    DAWLAB_CHECK(hq.undo(q));  // 8 MB: over; no undo entry left, the farthest redo (the second add) goes
    DAWLAB_CHECK(hq.undoCount() == 0 && hq.redoCount() == 1 && hq.bytes() == hq.recountBytes());
    DAWLAB_CHECK(hq.redo(q) && q.tracks.size() == 1 && q.tracks[0].id == TrackId{1});
    DAWLAB_CHECK(!hq.redo(q));
}

inline void checkRandom() {
    std::mt19937 rng(17);
    Project p = bigProject(8);
    BudgetHistory h(20 * kMB);
    int broken = 0, savedWrong = 0;
    for (int step = 0; step < 4000; ++step) {
        const int op = static_cast<int>(rng() % 8);
        auto randomTrack = [&]() { return p.tracks.empty() ? TrackId{999} : p.tracks[rng() % p.tracks.size()].id; };
        if (op == 0) {
            h.perform(p, {RemoveTrack{randomTrack()}});
        } else if (op == 1) {
            AddTrack a = newTrack(p, "T");
            a.clips.emplace_back(static_cast<int>(p.clips.size()),
                                 ClipState{a.track.id, 0, static_cast<std::int64_t>(1 + rng() % 2000000), "x.wav"});
            h.perform(p, {a});
        } else if (op == 2) {
            h.perform(p, {SetGain{randomTrack(), -1.0f}}, rng() % 2 == 0 ? 0u : 7u);
        } else if (op == 3) {
            h.perform(p, {RenameTrack{randomTrack(), std::string(rng() % 40, 'n')}});
        } else if (op == 4 || op == 5) {
            h.undo(p);
        } else if (op == 6) {
            h.redo(p);
        } else {
            h.markSaved();
            savedWrong += h.isModified() ? 1 : 0;
        }
        broken += consistent(h) ? 0 : 1;
    }
    std::printf("  4000 random edits, undos and redos: %d broken (expected 0)\n", broken);
    DAWLAB_CHECK(broken == 0);
    DAWLAB_CHECK(savedWrong == 0);
}

} // namespace minidaw::ex5_check

int main() {
    std::printf("Exercise 5 - an undo history limited by memory\n");
    minidaw::ex5_check::checkBudget();
    minidaw::ex5_check::checkOneBigEntry();
    minidaw::ex5_check::checkRedoGrows();
    minidaw::ex5_check::checkRandom();
    return dawlab::test::summary("l15_ex5");
}
