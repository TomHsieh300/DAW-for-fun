#pragma once
// check.h - the self-check of Step 4 (what a curve belongs to). Defines main(); included at the END
// of starter/step04.cpp and solution/step04.cpp, so both run exactly the same checks.
//
// What a target means after every edit, undo and redo of those edits, the project file, and random
// sessions: at every step no lane may point at something that is gone, and the file must read back
// exactly. Undoing all of it must pass through exactly the same projects.

#include <dawlab/Common.h>

#include <cstdint>
#include <cstdio>
#include <iterator>
#include <string>
#include <vector>

namespace step04_check {

using namespace minidaw;

// A deterministic random generator (the same sequence on every compiler).
struct Lcg {
    std::uint32_t state = 416u;
    std::uint32_t next() noexcept {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    }
    int below(int n) noexcept { return n <= 0 ? 0 : static_cast<int>(next() % static_cast<std::uint32_t>(n)); }
    float unit() noexcept { return static_cast<float>(next() % 10001u) / 10000.0f; }
};

inline const char* statusText(TargetStatus s) {
    switch (s) {
    case TargetStatus::Live: return "Live";
    case TargetStatus::NoStrip: return "NoStrip";
    case TargetStatus::NoEffect: return "NoEffect";
    case TargetStatus::NoParameter: return "NoParameter";
    }
    return "?";
}

inline LaneContent ramp(float from, float to) {
    LaneContent c;
    c.curve.add({0, from, Shape::Linear});
    c.curve.add({48000, to, Shape::Linear});
    return c;
}

inline float laneValue(const Project& p, const AutomationTarget& t, std::int64_t at) {
    const AutomationLaneState* lane = findLane(p, t);
    return lane != nullptr ? lane->curve.valueAt(at) : -1.0f;
}

// A small project: Drums with a compressor and a delay, Bass, a reverb bus, a limiter on the master.
struct Song {
    Project p;
    History history;
    TrackId drums, bass;
    EffectId comp, delay, busGain, masterGain;

    Song() {
        p.tracks.push_back(TrackState{TrackId{1}, "Drums", {}});
        p.tracks.push_back(TrackState{TrackId{2}, "Bass", {}});
        p.buses.push_back(BusState{"Reverb", {}});
        p.nextTrackId = 3;
        drums = TrackId{1};
        bass = TrackId{2};
        p.tracks[0].effects = {EffectState{EffectId{1}, "Compressor"}, EffectState{EffectId{2}, "Delay"}};
        p.buses[0].effects = {EffectState{EffectId{3}, "Gain"}};
        p.masterEffects = {EffectState{EffectId{4}, "Gain"}};
        p.nextEffectId = 5;
        comp = EffectId{1};
        delay = EffectId{2};
        busGain = EffectId{3};
        masterGain = EffectId{4};
    }
    AutomationTarget fader(TrackId id) const { return {StripRef::ofTrack(id), EffectId{}, "Gain"}; }
    AutomationTarget threshold() const { return {StripRef::ofTrack(drums), comp, "Threshold"}; }
    AutomationTarget feedback() const { return {StripRef::ofTrack(drums), delay, "Feedback"}; }
};

// The same project, whatever the id counters say: undoing an add does not take its id back.
inline bool sameExceptCounters(Project a, const Project& b) {
    a.nextTrackId = b.nextTrackId;
    a.nextEffectId = b.nextEffectId;
    return a == b;
}

// Does the text fail on that line, with those words?
inline bool failsWith(const std::string& body, int line, const std::string& words) {
    const LoadResult r = readProject(body);
    const std::string expected = "line " + std::to_string(line) + ":";
    const bool right = !r.ok && r.error.rfind(expected, 0) == 0 && r.error.find(words) != std::string::npos;
    if (!right)
        std::printf("    expected \"%s ... %s\", got \"%s\"\n", expected.c_str(), words.c_str(), r.ok ? "ok" : r.error.c_str());
    return right;
}

inline int run() {
    std::printf("Step 4: what a curve belongs to (build-along)\n");

    // ---- what a target means ---------------------------------------------------------------------
    {
        Song s;
        struct Case {
            const char* what;
            AutomationTarget target;
            TargetStatus expected;
        };
        const Case cases[] = {
            {"Drums fader", s.fader(s.drums), TargetStatus::Live},
            {"Drums pan", {StripRef::ofTrack(s.drums), EffectId{}, "Pan"}, TargetStatus::Live},
            {"Drums compressor threshold", s.threshold(), TargetStatus::Live},
            {"bus effect", {StripRef::ofBus(0), s.busGain, "Gain"}, TargetStatus::Live},
            {"master effect", {StripRef::master(), s.masterGain, "Gain"}, TargetStatus::Live},
            {"a track that does not exist", s.fader(TrackId{99}), TargetStatus::NoStrip},
            {"a bus that does not exist", {StripRef::ofBus(1), EffectId{}, "Gain"}, TargetStatus::NoStrip},
            {"an effect id that does not exist", {StripRef::ofTrack(s.drums), EffectId{77}, "Gain"}, TargetStatus::NoEffect},
            {"Drums' compressor on Bass", {StripRef::ofTrack(s.bass), s.comp, "Threshold"}, TargetStatus::NoEffect},
            {"a parameter the compressor lacks", {StripRef::ofTrack(s.drums), s.comp, "Feedback"}, TargetStatus::NoParameter},
            {"a parameter the strip lacks", {StripRef::ofTrack(s.drums), EffectId{}, "Threshold"}, TargetStatus::NoParameter},
        };
        int wrong = 0;
        for (const Case& c : cases) {
            const TargetStatus got = resolve(s.p, c.target);
            std::printf("  %-34s %s\n", c.what, statusText(got));
            wrong += got == c.expected ? 0 : 1;
        }
        DAWLAB_CHECK(wrong == 0);
    }

    // ---- moving an effect changes nothing about its lanes ------------------------------------------
    {
        Song s;
        s.history.perform(s.p, {SetLane{s.threshold(), ramp(0.25f, 0.75f)}});
        const auto lanesBefore = s.p.lanes;
        const bool moved = s.history.perform(s.p, {MoveEffect{s.comp, 1}});
        const int slotNow = slotOf(s.p.tracks[0].effects, s.comp);
        std::printf("  compressor moved to slot %d: its lane is %s, the delay is now in slot %d (expected 1, Live, 0)\n",
                    slotNow, statusText(resolve(s.p, s.threshold())), slotOf(s.p.tracks[0].effects, s.delay));
        DAWLAB_CHECK(moved && slotNow == 1 && resolve(s.p, s.threshold()) == TargetStatus::Live);
        DAWLAB_CHECK(s.p.lanes == lanesBefore);
        const bool undone = s.history.undo(s.p);
        DAWLAB_CHECK(undone && slotOf(s.p.tracks[0].effects, s.comp) == 0);
        DAWLAB_CHECK(!s.history.perform(s.p, {MoveEffect{EffectId{77}, 0}}));
        // An id is never in use twice, on any strip, also not through a track that brings effects along.
        DAWLAB_CHECK(!s.history.perform(s.p, {AddEffect{StripRef::ofTrack(s.bass), 0, EffectState{s.comp, "Gain"}, {}}}));
        const TrackState clash{TrackId{50}, "Clash", {EffectState{s.comp, "Gain"}}};
        DAWLAB_CHECK(!s.history.perform(s.p, {AddTrack{clash, 0, {}}}));
    }

    // ---- removing an effect takes its lanes with it; undo brings them back ---------------------------
    {
        Song s;
        s.history.perform(s.p, {SetLane{s.threshold(), ramp(0.25f, 0.75f)}});
        s.history.perform(s.p, {SetLane{s.feedback(), ramp(0.5f, 0.5f)}});
        s.history.perform(s.p, {SetLane{s.fader(s.drums), ramp(0.0f, 1.0f)}});
        const Project before = s.p;
        s.history.perform(s.p, {RemoveEffect{s.comp}});
        const bool laneGone = findLane(s.p, s.threshold()) == nullptr;
        const bool othersStay = findLane(s.p, s.feedback()) != nullptr && findLane(s.p, s.fader(s.drums)) != nullptr;
        std::printf("  compressor removed: its lane gone %s, the others stay %s", laneGone ? "yes" : "no", othersStay ? "yes" : "no");
        const bool undone = s.history.undo(s.p);
        std::printf(", undo restores all of it %s (expected yes, yes, yes)\n", s.p == before ? "yes" : "no");
        DAWLAB_CHECK(laneGone && othersStay);
        DAWLAB_CHECK(undone && s.p == before);
        const bool redone = s.history.redo(s.p);
        DAWLAB_CHECK(redone && findLane(s.p, s.threshold()) == nullptr);
        s.history.undo(s.p);

        // Replacing an effect is a remove and an add: another id, so the old curve cannot land on it.
        const Project beforeReplace = s.p;
        const AddEffect replacement = newEffect(s.p, StripRef::ofTrack(s.drums), 0, "Compressor");
        s.history.perform(s.p, {RemoveEffect{s.comp}, replacement});
        const AutomationTarget newThreshold{StripRef::ofTrack(s.drums), replacement.effect.id, "Threshold"};
        std::printf("  replaced by a new compressor, id %u: new target %s without a lane, old target %s (expected 5, Live, NoEffect)\n",
                    replacement.effect.id.value, statusText(resolve(s.p, newThreshold)), statusText(resolve(s.p, s.threshold())));
        DAWLAB_CHECK(resolve(s.p, newThreshold) == TargetStatus::Live && findLane(s.p, newThreshold) == nullptr &&
                     resolve(s.p, s.threshold()) == TargetStatus::NoEffect);
        s.history.undo(s.p);
        DAWLAB_CHECK(sameExceptCounters(s.p, beforeReplace));
        DAWLAB_CHECK(s.p.nextEffectId == 6); // undoing an add does not free its id
    }

    // ---- removing a track takes all its lanes; older undo entries still find them ---------------------
    {
        Song s;
        s.history.perform(s.p, {SetLane{s.threshold(), ramp(0.25f, 0.75f)}}); // entry 1
        s.history.perform(s.p, {SetLane{s.fader(s.drums), ramp(0.0f, 1.0f)}});
        s.history.perform(s.p, {SetLane{s.fader(s.bass), ramp(1.0f, 0.0f)}});
        LaneContent second = ramp(0.75f, 0.25f);
        s.history.perform(s.p, {SetLane{s.threshold(), second}}); // a second recording
        const Project full = s.p;
        s.history.perform(s.p, {RemoveTrack{s.drums}});
        const size_t lanesLeft = s.p.lanes.size();
        std::printf("  Drums deleted: %d lane left (expected 1, on Bass)\n", static_cast<int>(lanesLeft));
        DAWLAB_CHECK(lanesLeft == 1 && findLane(s.p, s.fader(s.bass)) != nullptr);
        s.history.undo(s.p); // Drums returns, at its old place, with its lanes
        DAWLAB_CHECK(s.p == full && s.p.tracks[0].id == s.drums);
        s.history.undo(s.p); // the second recording: this entry was made before the delete
        const float afterUndo = laneValue(s.p, s.threshold(), 0);
        std::printf("  undo the delete, undo the second recording: threshold starts at %.2f (expected 0.25)\n",
                    static_cast<double>(afterUndo));
        DAWLAB_CHECK(afterUndo == 0.25f);
        s.history.undo(s.p);
        s.history.undo(s.p);
        s.history.undo(s.p); // the first recording too: no lane at all
        DAWLAB_CHECK(s.p.lanes.empty());
        for (int i = 0; i < 5; ++i)
            s.history.redo(s.p);
        DAWLAB_CHECK(lanesLeft == s.p.lanes.size() && indexOf(s.p, s.drums) < 0);
    }

    // ---- SetLane ---------------------------------------------------------------------------------------
    {
        Song s;
        const size_t none = s.history.undoCount();
        DAWLAB_CHECK(!s.history.perform(s.p, {SetLane{s.fader(TrackId{99}), ramp(0, 1)}}));
        DAWLAB_CHECK(!s.history.perform(s.p, {SetLane{{StripRef::ofTrack(s.bass), s.comp, "Threshold"}, ramp(0, 1)}}));
        DAWLAB_CHECK(s.history.undoCount() == none && s.p.lanes.empty());
        // A whole change is all or nothing: a good lane, then one on a track that is gone.
        const bool partly = s.history.perform(s.p, {SetLane{s.threshold(), ramp(0, 1)}, SetLane{s.fader(TrackId{99}), ramp(0, 1)}});
        DAWLAB_CHECK(!partly && s.p.lanes.empty());

        s.history.perform(s.p, {SetLane{s.threshold(), ramp(0.25f, 0.75f)}});
        LaneContent touched = ramp(0.25f, 0.75f);
        touched.mode = AutomationMode::Touch;
        s.history.perform(s.p, {SetLane{s.threshold(), touched}}); // the mode is saved, so it can be undone
        const AutomationMode nowMode = findLane(s.p, s.threshold()) ? findLane(s.p, s.threshold())->mode : AutomationMode::Read;
        s.history.undo(s.p);
        const AutomationMode backMode = findLane(s.p, s.threshold()) ? findLane(s.p, s.threshold())->mode : AutomationMode::Touch;
        DAWLAB_CHECK(nowMode == AutomationMode::Touch && backMode == AutomationMode::Read);
        s.history.perform(s.p, {SetLane{s.threshold(), std::nullopt}}); // the user deletes the lane
        DAWLAB_CHECK(findLane(s.p, s.threshold()) == nullptr);
        s.history.undo(s.p);
        DAWLAB_CHECK(laneValue(s.p, s.threshold(), 48000) == 0.75f);

        // A dragged point: many edits, one undo.
        const size_t entries = s.history.undoCount();
        for (int i = 1; i <= 10; ++i)
            s.history.perform(s.p, {SetLane{s.threshold(), ramp(0.25f, 0.75f - 0.01f * static_cast<float>(i))}}, 7);
        s.history.endMerge();
        const bool oneEntry = s.history.undoCount() == entries + 1;
        s.history.undo(s.p);
        DAWLAB_CHECK(oneEntry && laneValue(s.p, s.threshold(), 48000) == 0.75f);
    }

    // ---- the file: everything reads back exactly -------------------------------------------------------
    {
        Song s;
        LaneContent hard;
        hard.mode = AutomationMode::Latch;
        hard.curve = AutomationCurve(0.1f);
        hard.curve.add({0, 1.0f / 3.0f, Shape::Hold});
        hard.curve.add({1000, 0.1f, Shape::Linear});
        hard.curve.add({1000, 0.7f, Shape::Linear}); // a jump
        hard.curve.add({123456789012LL, 0.30000001f, Shape::Linear});
        s.history.perform(s.p, {SetLane{s.threshold(), hard}});
        s.history.perform(s.p, {SetLane{{StripRef::ofBus(0), s.busGain, "Gain"}, ramp(0.0f, 1.0f)}});
        s.history.perform(s.p, {SetLane{{StripRef::master(), s.masterGain, "Gain"}, ramp(1.0f, 0.0f)}});
        s.history.perform(s.p, {SetLane{{StripRef::ofTrack(s.bass), EffectId{}, "Pan \"wide\""}, ramp(0.5f, 0.5f)}});
        const std::string text = writeProject(s.p);
        const LoadResult back = readProject(text);
        std::printf("  a project with %d lanes reads back: %s (expected ok); same project: %s (expected yes)\n",
                    static_cast<int>(s.p.lanes.size()), back.ok ? "ok" : back.error.c_str(), back.ok && back.project == s.p ? "yes" : "no");
        DAWLAB_CHECK(s.p.lanes.size() == 4 && back.ok);
        DAWLAB_CHECK(back.ok && back.project == s.p);
        DAWLAB_CHECK(back.ok && writeProject(back.project) == text);
    }

    // ---- the file: the order of the lines does not matter, except within a jump --------------------------
    {
        const std::string shuffled =
            "DawLabProject 4\n"
            "point 1 2000 0.25 linear\n" // before its lane
            "point 1 1000 0.5 linear\n"  // not sorted, and a jump: 0.5 first, then 0.75
            "point 1 1000 0.75 hold\n"
            "lane 1 track:2 7 \"Threshold\" touch 0.5\n" // before its track and its effect
            "effect track:2 7 \"Compressor\"\n"
            "track 2 \"Bass\"\n"
            "sampleRate 48000\n";
        const LoadResult r = readProject(shuffled);
        const AutomationTarget target{StripRef::ofTrack(TrackId{2}), EffectId{7}, "Threshold"};
        const AutomationLaneState* lane = r.ok ? findLane(r.project, target) : nullptr;
        const bool order = lane != nullptr && lane->curve.size() == 3 && lane->curve.points()[0].value == 0.5f &&
                           lane->curve.points()[1].value == 0.75f && lane->curve.points()[2].position == 2000;
        std::printf("  lines in any order: %s; points sorted, the jump's order kept: %s; ids above those in the file: %u, %u (expected ok, yes, 3, 8)\n",
                    r.ok ? "ok" : r.error.c_str(), order ? "yes" : "no", r.project.nextTrackId, r.project.nextEffectId);
        DAWLAB_CHECK(r.ok && order);
        DAWLAB_CHECK(r.ok && r.project.nextTrackId == 3 && r.project.nextEffectId == 8);
        DAWLAB_CHECK(lane != nullptr && lane->mode == AutomationMode::Touch && lane->curve.defaultValue() == 0.5f);

        // Lanes in any order give the same project as the edits do, or undo could not compare or restore it.
        Project byEdits;
        byEdits.tracks = {TrackState{TrackId{1}, "A", {}}, TrackState{TrackId{2}, "B", {}}};
        byEdits.nextTrackId = 3;
        applyEdit(byEdits, SetLane{{StripRef::ofTrack(TrackId{2}), EffectId{}, "Gain"}, LaneContent{}});
        applyEdit(byEdits, SetLane{{StripRef::ofTrack(TrackId{1}), EffectId{}, "Pan"}, LaneContent{}});
        const LoadResult anyOrder = readProject(
            "DawLabProject 4\nsampleRate 48000\ntrack 1 \"A\"\ntrack 2 \"B\"\n"
            "lane 1 track:2 0 \"Gain\" read 0\nlane 2 track:1 0 \"Pan\" read 0\n");
        DAWLAB_CHECK(anyOrder.ok && anyOrder.project == byEdits);
    }

    // ---- the file: a parameter this version does not know stays, silently ----------------------------------
    {
        const std::string text =
            "DawLabProject 4\nsampleRate 48000\nnextTrackId 3\nnextEffectId 8\n"
            "track 2 \"Bass\"\neffect track:2 7 \"Compressor\"\n"
            "lane 1 track:2 7 \"Knee\" read 0.5\npoint 1 0 0.25 linear\n"
            "lane 2 track:2 7 \"Threshold\" read 0.5\n";
        const LoadResult r = readProject(text);
        const AutomationTarget knee{StripRef::ofTrack(TrackId{2}), EffectId{7}, "Knee"};
        const TargetStatus status = r.ok ? resolve(r.project, knee) : TargetStatus::Live;
        const std::string again = r.ok ? writeProject(r.project) : std::string();
        std::printf("  a lane for \"Knee\", unknown to this compressor: %s, %s; saved again: %s (expected ok, NoParameter, kept)\n",
                    r.ok ? "ok" : r.error.c_str(), statusText(status), again.find("\"Knee\"") != std::string::npos ? "kept" : "LOST");
        DAWLAB_CHECK(r.ok && status == TargetStatus::NoParameter && r.project.lanes.size() == 2);
        DAWLAB_CHECK(again.find("\"Knee\"") != std::string::npos && again.find("point 1 0 0.25 linear") != std::string::npos);
    }

    // ---- the file: outside data ----------------------------------------------------------------------------
    {
        const std::string head = "DawLabProject 4\nsampleRate 48000\ntrack 2 \"Bass\"\neffect track:2 7 \"Gain\"\ntrack 3 \"Other\"\n"; // 5 lines
        const std::string lane = "lane 1 track:2 7 \"Gain\" read 0.5\n";
        struct Bad {
            std::string body;
            int line;
            const char* words;
        };
        const Bad bad[] = {
            {head + "lane 1 track:9 0 \"Gain\" read 0.5\n", 6, "does not exist"},
            {head + "lane 1 track:9 0 \"Gain\" read 0.5\ntrack 4 \"Later\"\n", 6, "does not exist"}, // not just the last line
            {head + "lane 1 bus:0 0 \"Gain\" read 0.5\n", 6, "does not exist"},
            {head + "lane 1 track:2 8 \"Gain\" read 0.5\n", 6, "is not on"},
            {head + "lane 1 track:3 7 \"Gain\" read 0.5\n", 6, "is not on"}, // the effect is on another track
            {head + lane + lane, 7, "used twice"},
            {head + lane + "lane 2 track:2 7 \"Gain\" read 0.5\n", 7, "two lanes"},
            {head + lane + "point 2 0 0.5 linear\n", 7, "does not exist"},
            {head + "lane 0 track:2 7 \"Gain\" read 0.5\n", 6, "lane needs"},
            {head + "lane 1 track:2 7 Gain read 0.5\n", 6, "lane needs"},
            {head + "lane 1 track:2 7 \"Gain\" auto 0.5\n", 6, "unknown mode"},
            {head + "lane 1 track:2 7 \"Gain\" read 1.5\n", 6, "default"},
            {head + lane + "point 1 0 1.5 linear\n", 7, "value"},
            {head + lane + "point 1 0 -0.1 linear\n", 7, "value"},
            {head + lane + "point 1 0 nan linear\n", 7, "value"},
            {head + lane + "point 1 -1 0.5 linear\n", 7, "position"},
            {head + lane + "point 1 0 0.5 curvy\n", 7, "unknown shape"},
            {head + lane + "point 1 0 0.5\n", 7, "point needs"},
            {"DawLabProject 3\nsampleRate 48000\ntrack 2 \"Bass\"\nlane 1 track:2 0 \"Gain\" read 0.5\n", 4, "needs format 4"},
            {"DawLabProject 5\n", 1, "reads formats"},
        };
        int wrong = 0;
        for (const Bad& b : bad)
            wrong += failsWith(b.body, b.line, b.words) ? 0 : 1;
        const LoadResult old = readProject("DawLabProject 3\nsampleRate 44100\ntrack 2 \"Bass\"\n");
        std::printf("  %d broken files: %d not refused with the right line and reason (expected 0); format 3 opens: %s, %d lanes\n",
                    static_cast<int>(std::size(bad)), wrong, old.ok ? "ok" : old.error.c_str(), static_cast<int>(old.project.lanes.size()));
        DAWLAB_CHECK(wrong == 0);
        DAWLAB_CHECK(old.ok && old.project.lanes.empty() && old.project.sampleRate == 44100.0);
    }

    // ---- another sample rate ---------------------------------------------------------------------------------
    {
        const std::string text =
            "DawLabProject 4\nsampleRate 44100\ntrack 2 \"Bass\"\n"
            "lane 1 track:2 0 \"Gain\" read 0.5\n"
            "point 1 0 0.25 linear\npoint 1 44100 0.75 linear\npoint 1 44100 0.5 linear\npoint 1 88200 0.125 hold\n";
        const LoadResult r = readProject(text, 48000.0);
        const AutomationLaneState* lane = r.ok && !r.project.lanes.empty() ? &r.project.lanes[0] : nullptr;
        const bool positions = lane != nullptr && lane->curve.size() == 4 && lane->curve.points()[1].position == 48000 &&
                               lane->curve.points()[2].position == 48000 && lane->curve.points()[3].position == 96000;
        const bool values = lane != nullptr && lane->curve.size() == 4 && lane->curve.points()[1].value == 0.75f &&
                            lane->curve.points()[2].value == 0.5f && lane->curve.points()[3].shape == Shape::Hold;
        std::printf("  44.1 kHz file opened at 48 kHz: 1 s is now at %lld, the jump is still a jump: %s (expected 48000, yes)\n",
                    lane != nullptr && lane->curve.size() > 1 ? static_cast<long long>(lane->curve.points()[1].position) : -1LL, values ? "yes" : "no");
        DAWLAB_CHECK(r.ok && r.project.sampleRate == 48000.0);
        DAWLAB_CHECK(positions && values);
    }

    // ---- random sessions --------------------------------------------------------------------------------------
    {
        Lcg rng;
        int dangling = 0, undoWrong = 0, redoWrong = 0, fileWrong = 0, performed = 0, rounds = 0;
        for (int round = 0; round < 60; ++round, ++rounds) {
            Song s;
            std::vector<Project> states{s.p};
            for (int step = 0; step < 40; ++step) {
                // A random strip and effect that exist now, or sometimes ones that do not.
                StripRef strip = StripRef::master();
                const int pick = rng.below(static_cast<int>(s.p.tracks.size()) + 2);
                if (pick < static_cast<int>(s.p.tracks.size()))
                    strip = StripRef::ofTrack(s.p.tracks[static_cast<size_t>(pick)].id);
                else if (pick == static_cast<int>(s.p.tracks.size()))
                    strip = StripRef::ofBus(0);
                const std::vector<EffectState>& effects = *effectsOf(s.p, strip);
                const EffectId effect = effects.empty() || rng.below(3) == 0 ? EffectId{} : effects[static_cast<size_t>(rng.below(static_cast<int>(effects.size())))].id;
                Change change;
                switch (rng.below(9)) {
                case 0: change = {newTrack(s.p, "T")}; break;
                case 1: if (!s.p.tracks.empty()) change = {RemoveTrack{s.p.tracks[static_cast<size_t>(rng.below(static_cast<int>(s.p.tracks.size())))].id}}; break;
                case 2: change = {newEffect(s.p, strip, rng.below(4), rng.below(2) == 0 ? "Delay" : "Compressor")}; break;
                case 3: if (effect.isValid()) change = {RemoveEffect{effect}}; break;
                case 4: if (effect.isValid()) change = {MoveEffect{effect, rng.below(4)}}; break;
                case 5: {
                    LaneContent c;
                    c.mode = static_cast<AutomationMode>(rng.below(4));
                    c.curve = AutomationCurve(rng.unit());
                    for (int k = rng.below(5); k > 0; --k)
                        c.curve.add({static_cast<std::int64_t>(rng.below(5)) * 100, rng.unit(), rng.below(2) == 0 ? Shape::Hold : Shape::Linear});
                    change = {SetLane{{strip, effect, rng.below(4) == 0 ? "Unknown" : "Gain"}, c}};
                    break;
                }
                case 6: if (!s.p.lanes.empty()) change = {SetLane{s.p.lanes[static_cast<size_t>(rng.below(static_cast<int>(s.p.lanes.size())))].target, std::nullopt}}; break;
                case 7: if (effect.isValid()) change = {RemoveEffect{effect}, newEffect(s.p, strip, 0, "Gain")}; break;
                default: if (!s.p.lanes.empty()) change = {SetLane{s.fader(TrackId{99}), LaneContent{}}}; break; // does not apply
                }
                if (change.empty() || !s.history.perform(s.p, change))
                    continue;
                ++performed;
                states.push_back(s.p);
                for (const AutomationLaneState& lane : s.p.lanes) {
                    const TargetStatus status = resolve(s.p, lane.target);
                    dangling += status == TargetStatus::NoStrip || status == TargetStatus::NoEffect ? 1 : 0;
                }
                const LoadResult back = readProject(writeProject(s.p));
                fileWrong += back.ok && back.project == s.p ? 0 : 1;
            }
            for (size_t i = states.size() - 1; i > 0; --i) {
                const bool undone = s.history.undo(s.p);
                undoWrong += undone && sameExceptCounters(s.p, states[i - 1]) ? 0 : 1;
            }
            for (size_t i = 1; i < states.size(); ++i) {
                const bool redone = s.history.redo(s.p);
                redoWrong += redone && sameExceptCounters(s.p, states[i]) ? 0 : 1;
            }
        }
        std::printf("  %d random sessions, %d changes: %d lanes without an owner, %d files that read back differently,\n"
                    "  %d wrong undos, %d wrong redos (expected 0, 0, 0, 0)\n",
                    rounds, performed, dangling, fileWrong, undoWrong, redoWrong);
        DAWLAB_CHECK(performed > 500);
        DAWLAB_CHECK(dangling == 0);
        DAWLAB_CHECK(fileWrong == 0);
        DAWLAB_CHECK(undoWrong == 0 && redoWrong == 0);
    }

    return dawlab::test::summary("l16_step04");
}

} // namespace step04_check

int main() { return step04_check::run(); }
