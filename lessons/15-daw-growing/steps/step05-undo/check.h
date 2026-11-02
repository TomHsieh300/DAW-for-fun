#pragma once
// check.h - the self-check of Step 5 (undo and redo). Defines main(); included at the END of
// starter/step05.cpp and solution/step05.cpp, so both run exactly the same checks.
//
// Tests only what Step 5 adds: edits and their inverses, undo / redo, merged fader drags, a new
// edit clearing redo, the history limit, changes that apply all or nothing, the "modified" flag,
// and a random session compared step by step against a reference model of every state.
//
// "The same project" means the same tracks and clips. The id counter (nextTrackId) is left out
// on purpose: it must NOT go back on undo - see sameContent().

#include <dawlab/Common.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace step05_check {

using namespace minidaw;

// Everything the user can see and undo. nextTrackId is not in it: undoing "add track 3" must not
// make id 3 available again (Step 1: ids are never reused).
inline bool sameContent(const Project& a, const Project& b) {
    return a.sampleRate == b.sampleRate && a.tracks == b.tracks && a.clips == b.clips;
}

inline const char* yesNo(bool b) { return b ? "yes" : "no"; }

inline std::string names(const Project& p) {
    std::string s;
    for (const TrackState& t : p.tracks)
        s += (s.empty() ? "" : ", ") + t.name;
    return s.empty() ? "-" : s;
}

inline float gainOf(const Project& p, TrackId id) {
    const int i = indexOf(p, id);
    return i >= 0 ? p.tracks[static_cast<size_t>(i)].gainDb : -999.0f;
}

// Drums (1), Bass (2); Drums has two clips, Bass one, interleaved in the clip list.
inline Project startProject() {
    Project p;
    p.nextTrackId = 3;
    p.tracks = {{TrackId{1}, "Drums", 0.0f, 0.0f}, {TrackId{2}, "Bass", 0.0f, 0.0f}};
    p.clips = {{TrackId{1}, 0, 1000, "kick.wav"}, {TrackId{2}, 0, 2000, "bass.wav"}, {TrackId{1}, 1000, 1000, "snare.wav"}};
    return p;
}

inline void workedExample() {
    Project p = startProject();
    const Project start = p;
    History h;
    const AddTrack keys = newTrack(p, "Keys");
    const bool a = h.perform(p, {keys});
    const bool b = h.perform(p, {SetGain{TrackId{2}, -6.0f}});
    const bool c = h.perform(p, {RemoveTrack{TrackId{1}}}); // Drums and both its clips
    const Project end = p;
    std::printf("  add Keys, Bass to -6 dB, delete Drums: %s; %zu clip(s) left (expected Bass, Keys; 1)\n", names(p).c_str(),
                p.clips.size());
    DAWLAB_CHECK(a && b && c && h.undoCount() == 3);
    DAWLAB_CHECK(p.tracks.size() == 2 && p.clips.size() == 1 && gainOf(p, TrackId{2}) == -6.0f);

    const bool u1 = h.undo(p);
    std::printf("  undo: %s, Drums is id %u again, clips in the old order: %s (expected Drums, Bass, Keys; 1; yes)\n",
                names(p).c_str(), p.tracks.empty() ? 0u : p.tracks[0].id.value,
                yesNo(p.clips == start.clips));
    DAWLAB_CHECK(u1 && indexOf(p, TrackId{1}) == 0 && p.clips == start.clips);
    const bool u2 = h.undo(p);
    const bool u3 = h.undo(p);
    const bool u4 = h.undo(p); // nothing left
    std::printf("  undo, undo: back to the start %s, nextTrackId still %u (expected yes, 4); a 4th undo: %s\n",
                yesNo(sameContent(p, start)), p.nextTrackId, u4 ? "done?!" : "nothing to undo");
    DAWLAB_CHECK(u2 && u3 && !u4 && sameContent(p, start));
    DAWLAB_CHECK(p.nextTrackId == 4); // id 3 (Keys) stays used up

    const bool r = h.redo(p) && h.redo(p) && h.redo(p);
    std::printf("  redo x3: the same as before the undos %s, Keys has id %u again (expected yes, 3)\n", yesNo(sameContent(p, end)),
                indexOf(p, TrackId{3}) >= 0 ? 3u : 0u);
    DAWLAB_CHECK(r && sameContent(p, end) && !h.canRedo());

    // Undo the add of Keys, then add a new track: it gets id 4, not 3, and the redo is gone.
    h.undo(p);
    h.undo(p);
    h.undo(p);
    h.redo(p); // Keys is back...
    h.undo(p); // ...and gone again
    const AddTrack pad = newTrack(p, "Pad");
    h.perform(p, {pad});
    std::printf("  undo the add of Keys, then add Pad: Pad's id %u (expected 4); redo still possible: %s (expected no)\n",
                pad.track.id.value, yesNo(h.canRedo()));
    DAWLAB_CHECK(pad.track.id.value == 4 && !h.canRedo());
}

inline void edits() {
    Project p = startProject();
    // Every kind of edit, then its inverse: back to the start.
    const Edit all[] = {newTrack(p, "New"), RemoveTrack{TrackId{2}}, SetGain{TrackId{1}, -12.0f},
                        RenameTrack{TrackId{1}, "Kit"}, MoveTrack{TrackId{2}, 0}, MoveTrack{TrackId{1}, 99}};
    int inverseOk = 0;
    for (const Edit& e : all) {
        Project q = startProject();
        const std::optional<Edit> inverse = applyEdit(q, e);
        const bool changed = !sameContent(q, startProject());
        if (inverse && changed && applyEdit(q, *inverse) && sameContent(q, startProject()))
            ++inverseOk;
    }
    // Edits that do not apply change nothing.
    Project q = startProject();
    const bool none = !applyEdit(q, SetGain{TrackId{9}, 1.0f}) && !applyEdit(q, RemoveTrack{TrackId{9}}) &&
                      !applyEdit(q, RenameTrack{TrackId{}, "x"}) && !applyEdit(q, MoveTrack{TrackId{9}, 0}) &&
                      !applyEdit(q, AddTrack{TrackState{TrackId{2}, "dup", 0, 0}, 0, {}}) && // id 2 is in use
                      !applyEdit(q, AddTrack{TrackState{TrackId{}, "bad", 0, 0}, 0, {}});
    std::printf("  6 kinds of edit undone by their inverse: %d of 6; 6 edits that do not apply: refused and harmless %s\n",
                inverseOk, yesNo(none && q == startProject()));
    DAWLAB_CHECK(inverseOk == 6);
    DAWLAB_CHECK(none && q == startProject());
}

inline void faderDrag() {
    Project p = startProject();
    History h;
    constexpr std::uint32_t kDrag = 42; // e.g. "fader of track 2"
    for (int i = 1; i <= 50; ++i)
        h.perform(p, {SetGain{TrackId{2}, -0.2f * static_cast<float>(i)}}, kDrag);
    const std::size_t afterDrag = h.undoCount();
    h.endMerge(); // mouse up
    h.perform(p, {SetGain{TrackId{2}, -20.0f}}, kDrag); // a second drag
    const std::size_t afterSecond = h.undoCount();
    h.undo(p);
    const float afterUndo1 = gainOf(p, TrackId{2});
    h.undo(p);
    const float afterUndo2 = gainOf(p, TrackId{2});
    std::printf("  a fader drag of 50 steps: %zu undo entry, a second drag: %zu; undo -> %.1f dB, undo -> %.1f dB\n"
                "  (expected 1, 2, -10.0, 0.0)\n",
                afterDrag, afterSecond, afterUndo1, afterUndo2);
    DAWLAB_CHECK(afterDrag == 1 && afterSecond == 2);
    DAWLAB_CHECK(afterUndo1 == -10.0f && afterUndo2 == 0.0f);

    // Undo ends a merge: drag, undo (mouse still down, or a keyboard nudge), drag the same fader
    // again - that is a NEW entry, not one merged into whatever is now the newest entry.
    Project q = startProject();
    History g;
    g.perform(q, {SetGain{TrackId{1}, -5.0f}});        // an earlier, unrelated edit
    g.perform(q, {SetGain{TrackId{2}, -1.0f}}, kDrag); // drag...
    g.undo(q);                                         // ...undo it...
    g.perform(q, {SetGain{TrackId{2}, -7.0f}}, kDrag); // ...and drag again
    const std::size_t entries = g.undoCount();
    g.undo(q);
    std::printf("  drag, undo, drag the same fader again: %zu undo entries; undo -> Drums %.1f dB, Bass %.1f dB\n"
                "  (expected 2, -5.0, 0.0)\n",
                entries, gainOf(q, TrackId{1}), gainOf(q, TrackId{2}));
    DAWLAB_CHECK(entries == 2);
    DAWLAB_CHECK(gainOf(q, TrackId{1}) == -5.0f && gainOf(q, TrackId{2}) == 0.0f);
}

inline void allOrNothing() {
    Project p = startProject();
    History h;
    // Delete two tracks in one action: one undo brings both back, in their places.
    const bool both = h.perform(p, {RemoveTrack{TrackId{1}}, RemoveTrack{TrackId{2}}});
    const bool empty = p.tracks.empty() && p.clips.empty();
    h.undo(p);
    const bool restored = sameContent(p, startProject());
    // A change whose second edit fails: the first is taken back, nothing is recorded.
    const std::size_t before = h.undoCount();
    const bool partial = h.perform(p, {SetGain{TrackId{2}, -3.0f}, RemoveTrack{TrackId{99}}});
    std::printf("  delete Drums and Bass in one action, one undo: both back %s; a change with a failing edit: %s, gain %.1f dB,\n"
                "  history unchanged %s (expected yes, refused, 0.0, yes)\n",
                yesNo(both && empty && restored), partial ? "applied?!" : "refused", gainOf(p, TrackId{2}),
                yesNo(h.undoCount() == before && h.redoCount() == 1));
    DAWLAB_CHECK(both && empty && restored);
    DAWLAB_CHECK(!partial && gainOf(p, TrackId{2}) == 0.0f && sameContent(p, startProject()));
    DAWLAB_CHECK(h.undoCount() == before && h.redoCount() == 1); // the failed change did not even clear redo
}

inline void limit() {
    Project p = startProject();
    History h(10);
    std::vector<Project> states{p};
    for (int i = 1; i <= 25; ++i) {
        h.perform(p, {SetGain{TrackId{1}, static_cast<float>(-i)}});
        states.push_back(p);
    }
    int undone = 0;
    while (h.undo(p))
        ++undone;
    std::printf("  history limit 10, 25 edits: %d undos possible, then the state after edit 15: %s (expected 10, yes)\n", undone,
                yesNo(sameContent(p, states[15])));
    DAWLAB_CHECK(undone == 10 && sameContent(p, states[15]));
}

inline void modifiedFlag() {
    Project p = startProject();
    History h;
    const bool fresh = h.isModified();
    h.perform(p, {RenameTrack{TrackId{1}, "Kit"}});
    const bool afterEdit = h.isModified();
    h.markSaved();
    const bool afterSave = h.isModified();
    h.undo(p);
    const bool afterUndo = h.isModified();
    h.redo(p);
    const bool afterRedo = h.isModified();
    // Save in the middle of a drag, then keep dragging: that IS a modification.
    h.perform(p, {SetGain{TrackId{1}, -1.0f}}, 7);
    h.markSaved();
    h.perform(p, {SetGain{TrackId{1}, -2.0f}}, 7);
    const bool dragAfterSave = h.isModified();
    std::printf("  modified: new %s, edit %s, save %s, undo %s, redo %s, drag on after a save %s\n"
                "  (expected no, yes, no, yes, no, yes)\n",
                yesNo(fresh), yesNo(afterEdit), yesNo(afterSave), yesNo(afterUndo), yesNo(afterRedo), yesNo(dragAfterSave));
    DAWLAB_CHECK(!fresh && afterEdit && !afterSave && afterUndo && !afterRedo && dragAfterSave);
}

// Someone changed the project without the history (a bug elsewhere, or a file reloaded under
// it). The entry no longer applies: undo must refuse cleanly, not half-apply.
inline void outsideChange() {
    Project p = startProject();
    History h;
    h.perform(p, {SetGain{TrackId{1}, -3.0f}, SetGain{TrackId{2}, -3.0f}});
    p.tracks.erase(p.tracks.begin() + 1); // Bass vanishes behind the history's back
    const Project damaged = p;
    const bool undone = h.undo(p);
    std::printf("  undo after Bass was deleted outside the history: %s, project unchanged %s, entry dropped %s\n"
                "  (expected refused, yes, yes)\n",
                undone ? "done?!" : "refused", yesNo(p == damaged), yesNo(!h.canUndo()));
    DAWLAB_CHECK(!undone && p == damaged && !h.canUndo());
}

struct Lcg {
    std::uint32_t state = 12345u;
    std::uint32_t next() noexcept {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    }
    int below(int n) noexcept { return n <= 0 ? 0 : static_cast<int>(next() % static_cast<std::uint32_t>(n)); }
};

// Random editing, undoing and redoing, compared after every operation with a reference model
// that simply keeps a copy of every state.
inline void randomSession() {
    Project p = startProject();
    History h(100000);
    std::vector<Project> past{p};   // past.back() is what the project must look like now
    std::vector<Project> future;    // what redo must bring back, next one last
    std::uint32_t openKey = 0;
    Lcg rng;
    int mismatches = 0, counterWentDown = 0, performed = 0, merged = 0, refused = 0, undos = 0, redos = 0;
    std::uint32_t lastCounter = p.nextTrackId;

    auto randomTrack = [&]() -> TrackId {
        if (p.tracks.empty() || rng.below(10) == 0)
            return TrackId{p.nextTrackId + 5}; // a track that does not exist
        return p.tracks[static_cast<size_t>(rng.below(static_cast<int>(p.tracks.size())))].id;
    };
    auto randomEdit = [&](Project& model) -> Edit {
        switch (rng.below(6)) {
            case 0: return newTrack(model, "T" + std::to_string(model.nextTrackId));
            case 1: return RemoveTrack{randomTrack()};
            case 2: return RenameTrack{randomTrack(), "R" + std::to_string(rng.below(1000))};
            case 3: return MoveTrack{randomTrack(), rng.below(static_cast<int>(model.tracks.size()) + 3) - 1};
            default: return SetGain{randomTrack(), -static_cast<float>(rng.below(60))};
        }
    };

    // Performs a change on the project and the same change on the model.
    auto performChecked = [&](const Change& change, std::uint32_t key) {
        Project expected = p;
        Change ignored;
        const bool applies = applyAll(expected, change, ignored); // what the change must do
        if (h.perform(p, change, key) != applies)
            ++mismatches;
        if (!applies) {
            ++refused;
            return;
        }
        ++performed;
        if (key != 0 && key == openKey && past.size() > 1) {
            past.back() = expected; // merged into the newest entry
            ++merged;
        } else {
            past.push_back(expected);
            openKey = key;
        }
        future.clear();
    };
    auto verify = [&] {
        if (!sameContent(p, past.back()) || h.undoCount() != past.size() - 1 || h.redoCount() != future.size())
            ++mismatches;
        if (p.nextTrackId < lastCounter)
            ++counterWentDown;
        lastCounter = p.nextTrackId;
        for (const TrackState& t : p.tracks)
            if (t.id.value >= p.nextTrackId)
                ++mismatches;
    };

    constexpr int kOps = 3000;
    for (int op = 0; op < kOps; ++op) {
        const int kind = rng.below(20);
        if (kind < 9) { // a change of 1..3 edits
            Change change;
            const int n = 1 + (rng.below(4) == 0 ? rng.below(3) : 0);
            Project scratch = p; // so that newTrack() hands out distinct ids within one change
            for (int i = 0; i < n; ++i) {
                Edit e = randomEdit(scratch);
                if (auto* add = std::get_if<AddTrack>(&e))
                    scratch.nextTrackId = add->track.id.value + 1;
                change.push_back(std::move(e));
            }
            performChecked(change, 0);
        } else if (kind < 11) { // a fader drag: 3..8 gain changes of one track, merged
            const TrackId id = randomTrack();
            const int steps = 3 + rng.below(6);
            for (int i = 0; i < steps; ++i) {
                performChecked({SetGain{id, -static_cast<float>(rng.below(60))}}, 1000u + id.value);
                verify();
            }
            if (rng.below(2) == 0) { // mouse up
                h.endMerge();
                openKey = 0;
            }
        } else if (kind < 15) {
            openKey = 0;
            const bool can = past.size() > 1;
            if (h.undo(p) != can)
                ++mismatches;
            if (can) {
                future.push_back(past.back());
                past.pop_back();
                ++undos;
            }
        } else if (kind < 19) {
            openKey = 0;
            const bool can = !future.empty();
            if (h.redo(p) != can)
                ++mismatches;
            if (can) {
                past.push_back(future.back());
                future.pop_back();
                ++redos;
            }
        } else {
            h.endMerge();
            openKey = 0;
        }
        verify();
    }
    std::printf("  %d random operations (%d changes, %d merged, %d refused, %d undos, %d redos): %d mismatches with the\n"
                "  reference model, id counter went down %d times (expected 0, 0)\n",
                kOps, performed, merged, refused, undos, redos, mismatches, counterWentDown);
    DAWLAB_CHECK(performed > 1000 && merged > 300 && refused > 50 && undos > 300 && redos > 100); // every path was taken
    DAWLAB_CHECK(mismatches == 0);
    DAWLAB_CHECK(counterWentDown == 0);
}

inline int run() {
    std::printf("Step 5: undo and redo (build-along)\n");
    workedExample();
    edits();
    faderDrag();
    allOrNothing();
    limit();
    modifiedFlag();
    outsideChange();
    randomSession();
    return dawlab::test::summary("l15_step05");
}

} // namespace step05_check

int main() { return step05_check::run(); }
