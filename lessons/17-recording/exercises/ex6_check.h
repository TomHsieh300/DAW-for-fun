#pragma once
// Exercise 6 - the self-check (main). Included at the end of the starter and of the solution.

#include "ex6_comping.h"

#include <dawlab/Check.h>

#include <cstdio>
#include <random>

namespace minidaw::ex6_check {

constexpr std::int64_t kLoopStart = 96000, kLoopEnd = 192000;

// Four whole passes of a loop from 2 s to 4 s, and a fifth that stopped at 2.83 s.
inline std::vector<Placement> passes() {
    RecordRange r;
    r.loopStart = kLoopStart;
    r.loopEnd = kLoopEnd;
    return takesOf(0, Latencies{480, 480, 240, 0}, r, 1200 + 96000 + 4 * 96000 + 40000);
}

inline bool valid(const Comp& c, std::int64_t from, std::int64_t to) {
    if (c.empty() || c.front().from != from || c.back().to != to)
        return false;
    for (size_t i = 0; i < c.size(); ++i) {
        if (c[i].to <= c[i].from)
            return false;
        if (i > 0 && (c[i].from != c[i - 1].to || c[i].take == c[i - 1].take))
            return false;
    }
    return true;
}

inline void checkChoose() {
    const Comp whole = wholeComp(kLoopStart, kLoopEnd, 3);
    Comp c = chooseTake(whole, 100000, 120000, 1);
    DAWLAB_CHECK((c == Comp{{96000, 100000, 3}, {100000, 120000, 1}, {120000, 192000, 3}}));
    c = chooseTake(c, 110000, 150000, 3);
    DAWLAB_CHECK((c == Comp{{96000, 100000, 3}, {100000, 110000, 1}, {110000, 192000, 3}}));
    DAWLAB_CHECK(chooseTake(c, 0, 1000000, 3) == whole);                    // everything one take again
    DAWLAB_CHECK((chooseTake(whole, 0, 100000, 2) == Comp{{96000, 100000, 2}, {100000, 192000, 3}})); // clipped to the window
    DAWLAB_CHECK(chooseTake(whole, 120000, 120000, 2) == whole);
    DAWLAB_CHECK(chooseTake(whole, 200000, 300000, 2) == whole);
    DAWLAB_CHECK(chooseTake(whole, 100000, 150000, 3) == whole);            // the same take: no cut

    // Random edits against a plain model: one take per 100 frames.
    std::mt19937 rng(17);
    const int cells = static_cast<int>((kLoopEnd - kLoopStart) / 100);
    std::vector<int> model(static_cast<size_t>(cells), 0);
    Comp comp = wholeComp(kLoopStart, kLoopEnd, 0);
    int wrong = 0;
    for (int step = 0; step < 3000; ++step) {
        int a = static_cast<int>(rng() % static_cast<unsigned>(cells + 40)) - 20, b = static_cast<int>(rng() % static_cast<unsigned>(cells + 40)) - 20;
        if (a > b)
            std::swap(a, b);
        const int take = static_cast<int>(rng() % 4);
        comp = chooseTake(comp, kLoopStart + a * 100, kLoopStart + b * 100, take);
        for (int k = std::max(a, 0); k < std::min(b, cells); ++k)
            model[static_cast<size_t>(k)] = take;
        bool ok = valid(comp, kLoopStart, kLoopEnd);
        for (const CompSegment& s : comp)
            for (std::int64_t p = s.from; p < s.to && ok; p += 100)
                ok = model[static_cast<size_t>((p - kLoopStart) / 100)] == s.take;
        wrong += ok ? 0 : 1;
    }
    std::printf("  3000 random choices: %d wrong (expected 0), %zu segments at the end\n", wrong, comp.size());
    DAWLAB_CHECK(wrong == 0);

    // A comp is a value: undo is keeping the last one.
    CompHistory h(whole);
    h.apply(chooseTake(h.now(), 100000, 120000, 1));
    h.apply(chooseTake(h.now(), 150000, 160000, 2));
    h.apply(chooseTake(h.now(), 150000, 160000, 2)); // the same: no step
    DAWLAB_CHECK(h.now().size() == 5);
    DAWLAB_CHECK(h.undo() && h.now().size() == 3 && h.undo() && h.now() == whole && !h.undo());
    DAWLAB_CHECK(h.redo() && h.now().size() == 3);
}

// Every frame of the window: the clips' gains add up to 1, and each clip plays its own pass.
struct Mix {
    double worstSum = 0.0;
    int foreign = 0; // frames a clip took from another pass than its own
};
inline Mix mix(const std::vector<CompClip>& clips, const std::vector<Placement>& takes) {
    Mix m;
    for (std::int64_t p = kLoopStart; p < kLoopEnd; ++p) {
        double sum = 0.0;
        for (const CompClip& c : clips) {
            const std::int64_t end = c.timelineStart + c.numFrames;
            if (p < c.timelineStart || p >= end)
                continue;
            double g = 1.0;
            if (p < c.timelineStart + c.fadeIn)
                g *= static_cast<double>(p - c.timelineStart) / static_cast<double>(c.fadeIn);
            if (p >= end - c.fadeOut)
                g *= static_cast<double>(end - p) / static_cast<double>(c.fadeOut);
            sum += g;
            const Placement& t = takes[static_cast<size_t>(c.take)];
            const std::int64_t frame = c.skipFrames + (p - c.timelineStart);
            if (frame != t.skipFrames + (p - t.timelineStart) || frame < t.skipFrames || frame >= t.skipFrames + t.numFrames)
                ++m.foreign;
        }
        m.worstSum = std::max(m.worstSum, std::abs(sum - 1.0));
    }
    return m;
}

inline void checkClips() {
    const std::vector<Placement> takes = passes();
    DAWLAB_CHECK(takes.size() == 5 && takes[4].numFrames == 40000);
    if (takes.size() != 5)
        return;

    // Pass 2 at the start, pass 4 in the middle, pass 1 at the end; 20 ms crossfades.
    Comp c = wholeComp(kLoopStart, kLoopEnd, 0);
    c = chooseTake(c, kLoopStart, 120000, 1);
    c = chooseTake(c, 120000, 170000, 3);
    std::vector<CompClip> clips = compClips(c, takes, 960);
    DAWLAB_CHECK(clips.size() == 3);
    if (clips.size() == 3) {
        DAWLAB_CHECK(clips[0].timelineStart == kLoopStart && clips[0].fadeIn == 0 && clips[0].fadeOut == 960);
        DAWLAB_CHECK(clips[1].timelineStart == 120000 - 480 && clips[1].fadeIn == 960 && clips[1].fadeOut == 960);
        DAWLAB_CHECK(clips[2].timelineStart + clips[2].numFrames == kLoopEnd && clips[2].fadeOut == 0);
        DAWLAB_CHECK(clips[0].skipFrames == takes[1].skipFrames && clips[1].take == 3);
    }
    Mix m = mix(clips, takes);
    std::printf("  three passes, 960-frame crossfades: gains off by %.1e, %d frames from the wrong pass\n", m.worstSum, m.foreign);
    DAWLAB_CHECK(m.worstSum < 1e-12 && m.foreign == 0);

    // A segment of 300 frames: its two crossfades shrink to fit.
    Comp narrow = chooseTake(c, 140000, 140300, 2);
    clips = compClips(narrow, takes, 960);
    DAWLAB_CHECK(clips.size() == 5);
    if (clips.size() == 5)
        DAWLAB_CHECK(clips[2].fadeIn == 300 && clips[2].fadeOut == 300 && clips[2].numFrames == 600);
    m = mix(clips, takes);
    DAWLAB_CHECK(m.worstSum < 1e-12 && m.foreign == 0);

    // The fifth pass ends at 136000: a crossfade at 135000 can only reach 1000 frames into it. (The one at
    // 100000 is limited by the 4000 frames of the first segment.)
    Comp partial = wholeComp(kLoopStart, kLoopEnd, 1);
    partial = chooseTake(partial, 100000, 135000, 4);
    clips = compClips(partial, takes, 20000);
    DAWLAB_CHECK(clips.size() == 3);
    if (clips.size() == 3)
        DAWLAB_CHECK(clips[1].fadeOut == 2000 && clips[1].timelineStart + clips[1].numFrames == 136000 && clips[1].fadeIn == 4000);
    m = mix(clips, takes);
    DAWLAB_CHECK(m.worstSum < 1e-12 && m.foreign == 0);

    // An odd fade becomes even; no fade: the clips just meet.
    clips = compClips(c, takes, 961);
    DAWLAB_CHECK(clips.size() == 3 && clips[0].fadeOut == 960);
    clips = compClips(c, takes, 0);
    DAWLAB_CHECK(clips.size() == 3 && clips[0].timelineStart + clips[0].numFrames == clips[1].timelineStart && clips[1].fadeIn == 0);
    m = mix(clips, takes);
    DAWLAB_CHECK(m.worstSum < 1e-12 && m.foreign == 0);
    // One take: one clip, the whole window.
    clips = compClips(wholeComp(kLoopStart, kLoopEnd, 2), takes, 960);
    DAWLAB_CHECK(clips.size() == 1 && clips[0].numFrames == kLoopEnd - kLoopStart && clips[0].fadeIn == 0 && clips[0].fadeOut == 0);
}

} // namespace minidaw::ex6_check

int main() {
    std::printf("Exercise 6 - comping\n");
    minidaw::ex6_check::checkChoose();
    minidaw::ex6_check::checkClips();
    return dawlab::test::summary("l17_ex6");
}
