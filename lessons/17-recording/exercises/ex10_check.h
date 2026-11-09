#pragma once
// Exercise 10 - the self-check (main). Included at the end of the starter and of the solution.

#include "ex10_comping_ui.h"

#include "app_rig.h"

#include <dawlab/Check.h>

#include <cmath>
#include <cstdio>
#include <random>
#include <utility>

namespace ex10::check {

using namespace dawlab;
using namespace dawlab::app;
using ex17rig::Rig;

inline bool sameClips(const std::vector<engine::Clip>& a, const std::vector<engine::Clip>& b) {
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].source != b[i].source || a[i].start != b[i].start || a[i].length != b[i].length || a[i].sourceOffset != b[i].sourceOffset ||
            a[i].fadeInSamples != b[i].fadeInSamples || a[i].fadeOutSamples != b[i].fadeOutSamples || a[i].name != b[i].name)
            return false;
    return true;
}

// What the track plays of the recording, worked out from the clips alone: at every position of the comp the
// fades add up to 1 (away from its two ends), and where one clip plays alone it plays the frame of the
// take the comp has there. Returns the number of positions that are wrong.
inline int wrongPositions(const std::vector<engine::Clip>& clips, const std::shared_ptr<const AudioBuffer>& source,
                          const std::vector<Placement>& takes, const Comp& comp, int endFadeIn, int endFadeOut) {
    int wrong = 0;
    for (std::int64_t p = comp.front().from; p < comp.back().to; ++p) {
        double sum = 0.0;
        int playing = 0;
        const engine::Clip* alone = nullptr;
        for (const engine::Clip& c : clips)
            if (c.source == source && c.start <= p && p < c.end()) {
                sum += static_cast<double>(engine::clipFadeAt(c, p - c.start));
                ++playing;
                alone = &c;
            }
        const bool atAnEnd = p < comp.front().from + endFadeIn || p >= comp.back().to - endFadeOut;
        if (!atAnEnd && std::abs(sum - 1.0) > 1e-5)
            ++wrong;
        if (playing == 1 && !atAnEnd) {
            int take = -1;
            for (const CompSegment& s : comp)
                if (s.from <= p && p < s.to)
                    take = s.take;
            const Placement& t = takes[static_cast<size_t>(take)];
            const float expected = source->getSample(0, static_cast<int>(t.skipFrames + p - t.timelineStart));
            if (alone->source->getSample(0, static_cast<int>(alone->sourceOffset + p - alone->start)) != expected)
                ++wrong;
        }
    }
    return wrong;
}

inline void run() {
    Rig rig;
    MainComponent& main = *rig.main;
    Session& session = rig.session;
    engine::Transport& tr = rig.engine.getTransport();
    const engine::TrackId track = rig.track(0);
    const ex17rig::InputFn input = [](int, std::int64_t f) { return ex17rig::inputAt(f); };

    // A loop recording of bars 2-3 that starts in the middle of the loop: a first pass of 3/4, two whole
    // passes and a last one of 3/4 - four takes; the clip plays the third, the last whole one.
    DAWLAB_CHECK(main.setArmed(track, true));
    tr.setLoopRange(96000, 144000);
    tr.setLoopEnabled(true);
    tr.setPosition(108000);
    DAWLAB_CHECK(main.toggleRecord());
    rig.play(rig.engine.getLatencySamples() + 48000 * 3 + 24000, input);
    rig.stop(input);
    DAWLAB_CHECK(rig.waitForTakes());
    tr.setLoopEnabled(false);
    // Something else on the track, after the recording: it stays as it is.
    auto other = std::make_shared<AudioBuffer>(1, 9600);
    std::vector<engine::Clip> withOther = session.getClips(track);
    withOther.push_back(engine::Clip::fromBuffer(other, 200000, "other"));
    session.setClips(track, withOther);
    DAWLAB_CHECK(session.getClips(track).size() == 2);
    if (session.getClips(track).size() != 2)
        return;
    const engine::Clip original = session.getClips(track)[0];
    const std::shared_ptr<const AudioBuffer> source = original.source;
    const std::vector<Placement>* takesPtr = session.getTakesOf(original);
    DAWLAB_CHECK(takesPtr != nullptr && takesPtr->size() == 4);
    if (takesPtr == nullptr || takesPtr->size() != 4)
        return;
    const std::vector<Placement> takes = *takesPtr;

    // takeOf
    DAWLAB_CHECK(Comper::takeOf(original, takes) == 2);
    engine::Clip shifted = original;
    shifted.sourceOffset += 1;
    DAWLAB_CHECK(Comper::takeOf(shifted, takes) == -1);
    engine::Clip part = original; // the middle of pass 2
    part.start += 1000;
    part.sourceOffset = takes[1].skipFrames + (part.start - takes[1].timelineStart);
    part.length = 2000;
    DAWLAB_CHECK(Comper::takeOf(part, takes) == 1);
    engine::Clip beyond = original; // pass 4 is half a pass
    beyond.sourceOffset = takes[3].skipFrames + (beyond.start - takes[3].timelineStart);
    DAWLAB_CHECK(Comper::takeOf(beyond, takes) == -1);
    beyond.length = takes[3].timelineStart + takes[3].numFrames - beyond.start;
    DAWLAB_CHECK(Comper::takeOf(beyond, takes) == 3);
    engine::Clip early = part; // pass 2's audio, but from before pass 2 began
    early.start = takes[1].timelineStart - 6000;
    early.sourceOffset = takes[1].skipFrames - 6000;
    DAWLAB_CHECK(Comper::takeOf(early, takes) == -1);

    // The comp of a whole pass: one segment, the clip's ends.
    Comper comper(session);
    Comp expected{{original.start, original.end(), 2}};
    DAWLAB_CHECK(comper.compOf(track, 0) == expected);
    DAWLAB_CHECK(comper.compOf(track, 1).empty()); // no takes
    DAWLAB_CHECK(comper.compOf(track, 7).empty());
    const int endFadeIn = original.fadeInSamples, endFadeOut = original.fadeOutSamples;
    DAWLAB_CHECK(endFadeIn == 240 && endFadeOut == 240); // 5 ms: what a new take gets, and the comp's ends

    // The second pass in the middle: three clips, one undo step.
    const std::vector<engine::Clip> before = session.getClips(track);
    DAWLAB_CHECK(comper.choose(track, 0, 110000, 130000, 1));
    expected = chooseTake(expected, 110000, 130000, 1);
    DAWLAB_CHECK(comper.compOf(track, 0) == expected);
    const std::vector<engine::Clip> after = session.getClips(track);
    DAWLAB_CHECK(after.size() == 4 && after.back().source == other); // three pieces and the other clip
    DAWLAB_CHECK(after.front().fadeInSamples == endFadeIn);
    DAWLAB_CHECK(wrongPositions(after, source, takes, expected, endFadeIn, endFadeOut) == 0);
    DAWLAB_CHECK(session.getHistory().getUndoName() == "Comp Take 2");
    DAWLAB_CHECK(main.undo() && sameClips(session.getClips(track), before));
    DAWLAB_CHECK(main.redo() && sameClips(session.getClips(track), after));
    // Nothing to change, nothing in the history; no such take.
    DAWLAB_CHECK(!comper.choose(track, 0, 112000, 128000, 1));
    DAWLAB_CHECK(!comper.choose(track, 1, 112000, 128000, 1));
    DAWLAB_CHECK(!comper.choose(track, 0, 112000, 128000, 4) && !comper.choose(track, 0, 112000, 128000, -1));
    DAWLAB_CHECK(!comper.choose(track, 0, 150000, 160000, 0)); // outside the comp
    DAWLAB_CHECK(!comper.choose(track, 0, 96000, 104000, 0)); // pass 1 began at 108000
    DAWLAB_CHECK(!comper.choose(track, 0, 120000, 110000, 0)); // no range
    DAWLAB_CHECK(sameClips(session.getClips(track), after));
    // The last pass is half a pass: it can only be chosen where it was played.
    DAWLAB_CHECK(comper.choose(track, 0, 96000, 144000, 3));
    expected = chooseTake(expected, takes[3].timelineStart, takes[3].timelineStart + takes[3].numFrames, 3);
    DAWLAB_CHECK(comper.compOf(track, 0) == expected);
    std::printf("  pass 4 over the whole range: %d segments, the last from %lld\n", static_cast<int>(expected.size()),
                static_cast<long long>(expected.back().from));
    // A short last segment: the fade-out at the end, at most half of its clip - and back to 5 ms after.
    DAWLAB_CHECK(comper.choose(track, 0, 143900, 144000, 1));
    expected = chooseTake(expected, 143900, 144000, 1);
    DAWLAB_CHECK(comper.compOf(track, 0) == expected);
    {
        const std::vector<engine::Clip>& now = session.getClips(track);
        const engine::Clip& last = now[now.size() - 2];
        DAWLAB_CHECK(last.length < 2 * endFadeOut && last.fadeOutSamples == last.length / 2);
    }
    DAWLAB_CHECK(comper.choose(track, 0, 140000, 144000, 2));
    expected = chooseTake(expected, 140000, 144000, 2);
    DAWLAB_CHECK(session.getClips(track)[session.getClips(track).size() - 2].fadeOutSamples == endFadeOut);

    // Many choices, from any of the recording's clips: the comp is what Exercise 6 says, every position adds
    // up to 1 and plays its take.
    std::mt19937 rng(17);
    std::uniform_int_distribution<std::int64_t> where(90000, 150000);
    std::uniform_int_distribution<int> which(0, 3);
    int worse = 0, changes = 0;
    for (int round = 0; round < 60; ++round) {
        std::int64_t from = where(rng), to = where(rng);
        if (to < from)
            std::swap(from, to);
        if (round % 5 == 0)
            to = from + 1 + round; // short ones: the crossfades get short
        const int take = which(rng);
        const Placement& t = takes[static_cast<size_t>(take)];
        const std::int64_t a = std::max(from, t.timelineStart), b = std::min(to, t.timelineStart + t.numFrames);
        const Comp next = b > a ? chooseTake(expected, a, b, take) : expected;
        std::vector<int> recordingClips;
        for (int i = 0; i < static_cast<int>(session.getClips(track).size()); ++i)
            if (session.getClips(track)[static_cast<size_t>(i)].source == source)
                recordingClips.push_back(i);
        const int clipIndex = recordingClips[static_cast<size_t>(round) % recordingClips.size()];
        const bool changed = comper.choose(track, clipIndex, from, to, take);
        worse += changed == !(next == expected) ? 0 : 1;
        if (changed) {
            ++changes;
            expected = next;
        }
        worse += comper.compOf(track, 0) == expected ? 0 : 1; // the index may now be another clip's
        // 5 ms fades at the comp's ends, at most half of a short first or last clip.
        const std::vector<engine::Clip>& now = session.getClips(track);
        const engine::Clip& first = now.front();
        const engine::Clip& last = now[now.size() - 2]; // the other clip comes after the recording
        const int fadeIn = static_cast<int>(std::min<std::int64_t>(endFadeIn, first.length / 2));
        const int fadeOut = static_cast<int>(std::min<std::int64_t>(endFadeOut, last.length / 2));
        worse += first.fadeInSamples == fadeIn && last.fadeOutSamples == fadeOut && now.back().source == other ? 0 : 1;
        worse += wrongPositions(now, source, takes, expected, fadeIn, fadeOut) == 0 ? 0 : 1;
    }
    std::printf("  60 choices (%d changed the comp): %d wrong (expected 0)\n", changes, worse);
    DAWLAB_CHECK(worse == 0);

    // The menu: one item per take, between the locators.
    tr.setLoopRange(100000, 120000);
    juce::PopupMenu menu;
    comper.addToMenu(menu, track, 0);
    const auto items = ex17rig::itemsOf(menu);
    DAWLAB_CHECK(items.size() == 4);
    if (items.size() == 4) {
        DAWLAB_CHECK(items[0].text == "Take 1 Between the Locators" && items[3].text == "Take 4 Between the Locators");
        DAWLAB_CHECK(items[0].isEnabled && items[3].isEnabled);
        if (items[1].action)
            items[1].action();
        const Comp viaMenu = chooseTake(expected, 100000, 120000, 1);
        DAWLAB_CHECK(comper.compOf(track, 0) == viaMenu);
    }
    juce::PopupMenu notThis;
    const int otherIndex = static_cast<int>(session.getClips(track).size()) - 1;
    comper.addToMenu(notThis, track, otherIndex);
    DAWLAB_CHECK(ex17rig::itemsOf(notThis).empty());
    tr.setLoopRange(120000, 120000);
    juce::PopupMenu noRange;
    comper.addToMenu(noRange, track, 0);
    const auto noRangeItems = ex17rig::itemsOf(noRange);
    DAWLAB_CHECK(noRangeItems.size() == 4 && !noRangeItems[0].isEnabled);
}

} // namespace ex10::check

int main() {
    const juce::ScopedJuceInitialiser_GUI juceInit;
    std::printf("Exercise 10 - comping in the app\n");
    ex10::check::run();
    return dawlab::test::summary("l17_ex10");
}
