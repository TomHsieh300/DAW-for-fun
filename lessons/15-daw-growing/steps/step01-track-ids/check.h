#pragma once
// check.h - the self-check of Step 1 (stable track IDs). Defines main(); included at the END of
// starter/step01.cpp and solution/step01.cpp, so both run exactly the same checks.
//
// Tests only what Step 1 adds: ids that survive deleting and reordering, ids that are never
// reused, and lookups that never return the wrong track. Ends with a random sequence of
// add / remove / move compared against a simple reference model.

#include <dawlab/Common.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace step01_check {

using namespace minidaw;

// Safe accessors for printing: they must not crash while the starter is still unfinished.
inline const char* nameAt(const TrackList& list, int index) {
    return (index >= 0 && index < list.size()) ? list.tracks()[static_cast<size_t>(index)].name.c_str() : "-";
}
inline float gainOf(const TrackList& list, TrackId id) {
    const TrackInfo* t = list.find(id);
    return t ? t->gainDb : NAN;
}

// A deterministic random generator (the same sequence on every compiler).
struct Lcg {
    std::uint32_t state = 12345u;
    std::uint32_t next() noexcept {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    }
    int below(int n) noexcept { return n <= 0 ? 0 : static_cast<int>(next() % static_cast<std::uint32_t>(n)); }
};

inline int run() {
    std::printf("Step 1: stable track IDs (build-along)\n");

    // ---- the worked example of docs/step-01-track-ids.md -----------------------------------------
    TrackList list;
    const TrackId drums = list.add("Drums");
    const TrackId bass = list.add("Bass");
    const TrackId keys = list.add("Keys");
    const TrackId vox = list.add("Vox");
    std::printf("  ids: Drums %u, Bass %u, Keys %u, Vox %u (expected 1, 2, 3, 4)\n", drums.value, bass.value,
                keys.value, vox.value);
    DAWLAB_CHECK(drums.value == 1 && bass.value == 2 && keys.value == 3 && vox.value == 4);
    DAWLAB_CHECK(list.size() == 4 && list.indexOf(vox) == 3);

    // Two edits recorded while the list was Drums, Bass, Keys, Vox: Bass to -6 dB, Vox to -3 dB.
    const std::vector<GainEdit> byId = {{bass, -6.0f}, {vox, -3.0f}};
    const std::vector<IndexedGainEdit> byIndex = {{1, -6.0f}, {3, -3.0f}};

    DAWLAB_CHECK(list.remove(drums)); // ...then the user deletes Drums
    std::printf("  after deleting Drums: %s, %s, %s (expected Bass, Keys, Vox)\n", nameAt(list, 0), nameAt(list, 1),
                nameAt(list, 2));
    DAWLAB_CHECK(list.size() == 3 && list.indexOf(bass) == 0 && list.indexOf(vox) == 2);

    TrackList withIndices = list;
    const int appliedByIndex = applyIndexedEdits(withIndices, byIndex);
    std::printf("  by index: %d of 2 applied, Bass %.1f dB, Keys %.1f dB, Vox %.1f dB (the chapter-14 way: wrong)\n",
                appliedByIndex, gainOf(withIndices, bass), gainOf(withIndices, keys), gainOf(withIndices, vox));
    DAWLAB_CHECK(appliedByIndex == 1);                  // index 3 no longer exists: the Vox edit is lost
    DAWLAB_CHECK(gainOf(withIndices, keys) == -6.0f);   // index 1 is Keys now: the Bass edit hits Keys

    TrackList withIds = list;
    const int appliedById = applyEdits(withIds, byId);
    std::printf("  by id:    %d of 2 applied, Bass %.1f dB, Keys %.1f dB, Vox %.1f dB (expected -6.0, 0.0, -3.0)\n",
                appliedById, gainOf(withIds, bass), gainOf(withIds, keys), gainOf(withIds, vox));
    DAWLAB_CHECK(appliedById == 2);
    DAWLAB_CHECK(gainOf(withIds, bass) == -6.0f && gainOf(withIds, keys) == 0.0f && gainOf(withIds, vox) == -3.0f);
    list = withIds;

    // ---- ids are never reused ------------------------------------------------------------------------
    DAWLAB_CHECK(list.remove(vox)); // Vox had the highest id (4)...
    const TrackId pad = list.add("Pad");
    std::printf("  delete Vox (id 4), add Pad: Pad gets id %u (expected 5, not 4); Drums found: %s, Vox found: %s (expected no, no)\n",
                pad.value, list.find(drums) ? "yes" : "no", list.find(vox) ? "yes" : "no");
    DAWLAB_CHECK(pad.value == 5);
    DAWLAB_CHECK(list.find(drums) == nullptr && list.find(vox) == nullptr);
    DAWLAB_CHECK(applyEdits(list, {{vox, -20.0f}}) == 0); // a stale edit for Vox changes nothing
    DAWLAB_CHECK(gainOf(list, pad) == 0.0f);

    // ---- reordering keeps the id (and everything attached to the track) ----------------------------
    DAWLAB_CHECK(list.move(pad, 0));       // Pad, Bass, Keys
    const bool padFirst = list.idAt(0) == pad && list.idAt(1) == bass && list.idAt(2) == keys;
    DAWLAB_CHECK(list.move(bass, 99));     // clamped: Pad, Keys, Bass
    const bool bassLast = list.idAt(0) == pad && list.idAt(1) == keys && list.idAt(2) == bass;
    DAWLAB_CHECK(list.move(keys, -5));     // clamped: Keys, Pad, Bass
    std::printf("  move Pad to 0, Bass to 99, Keys to -5: %s, %s, %s (expected Keys, Pad, Bass); Bass still %.1f dB\n",
                nameAt(list, 0), nameAt(list, 1), nameAt(list, 2), gainOf(list, bass));
    DAWLAB_CHECK(padFirst && bassLast);
    DAWLAB_CHECK(list.idAt(0) == keys && list.idAt(1) == pad && list.idAt(2) == bass);
    DAWLAB_CHECK(gainOf(list, bass) == -6.0f);

    // ---- ids that do not name a track ----------------------------------------------------------------
    DAWLAB_CHECK(list.indexOf(TrackId{}) == -1 && list.find(TrackId{}) == nullptr);
    DAWLAB_CHECK(!list.remove(TrackId{}) && !list.remove(TrackId{99}) && !list.remove(drums));
    DAWLAB_CHECK(!list.move(TrackId{99}, 0));
    DAWLAB_CHECK(list.size() == 3);

    // ---- random add / remove / move against a reference model ---------------------------------------
    {
        TrackList fuzz;
        std::vector<std::pair<std::uint32_t, std::string>> model; // (id, name) in display order
        std::uint32_t lastId = 0;
        int mismatches = 0, reused = 0;
        Lcg rng;
        constexpr int kOps = 2000;
        for (int op = 0; op < kOps; ++op) {
            const int kind = rng.below(10);
            if (kind < 4 || model.empty()) { // add
                const std::string name = "T" + std::to_string(op);
                const TrackId id = fuzz.add(name);
                if (id.value <= lastId)
                    ++reused;
                lastId = id.value;
                model.emplace_back(id.value, name);
            } else if (kind < 7) { // remove an existing track, or now and then one that is gone
                const int i = rng.below(static_cast<int>(model.size()));
                const bool bogus = rng.below(8) == 0;
                const TrackId id{bogus ? lastId + 1000u : model[static_cast<size_t>(i)].first};
                const bool removed = fuzz.remove(id);
                if (removed == bogus)
                    ++mismatches;
                if (!bogus)
                    model.erase(model.begin() + i);
            } else { // move, with target positions also outside the list
                const int i = rng.below(static_cast<int>(model.size()));
                const int to = rng.below(static_cast<int>(model.size()) + 4) - 2;
                if (!fuzz.move(TrackId{model[static_cast<size_t>(i)].first}, to))
                    ++mismatches;
                auto entry = model[static_cast<size_t>(i)];
                model.erase(model.begin() + i);
                const int clamped = std::clamp(to, 0, static_cast<int>(model.size()));
                model.insert(model.begin() + clamped, entry);
            }
            // the whole list must equal the model, in order
            bool same = fuzz.size() == static_cast<int>(model.size());
            for (int k = 0; same && k < fuzz.size(); ++k) {
                const TrackInfo& t = fuzz.tracks()[static_cast<size_t>(k)];
                same = t.id.value == model[static_cast<size_t>(k)].first && t.name == model[static_cast<size_t>(k)].second &&
                       fuzz.indexOf(t.id) == k;
            }
            if (!same)
                ++mismatches;
        }
        std::printf("  %d random add / remove / move: %d mismatches, %d reused ids, %d tracks left (expected 0, 0)\n", kOps,
                    mismatches, reused, fuzz.size());
        DAWLAB_CHECK(mismatches == 0);
        DAWLAB_CHECK(reused == 0);
    }

    return dawlab::test::summary("l15_step01");
}

} // namespace step01_check

int main() { return step01_check::run(); }
