#pragma once
// Chapter 19, Step 2 - the self-check (main). Included at the end of the starter and of the solution.

#include "minidaw.h"

#include <dawlab/Check.h>

#include <cstdio>
#include <functional>
#include <random>
#include <string>

namespace step02_check {

using namespace minidaw;

// What a run played: for every tick, the output sample it played at (in order), the jumps, and the clicks.
struct Played {
    std::vector<std::pair<std::int64_t, std::int64_t>> ticks; // (tick, output sample)
    std::vector<std::int64_t> jumps;                          // output samples where the ticks jumped
    std::vector<std::pair<std::int64_t, bool>> clicks;        // (output sample, accent)
    bool contiguous = true;   // a segment that does not jump starts at the tick the last one ended
    bool inside = true;       // every tick of a segment, but at a loop's edge, plays inside it
    bool samplesOk = true;    // the segments cover their block, in order
    bool clicksInOrder = true; // every click after the one before, none twice on a sample
    std::int64_t outputs = 0; // samples played
};

inline Played play(Transport& tr, std::int64_t total, const std::function<int()>& block,
                   const std::function<void(Transport&, std::int64_t)>& beforeBlock = {}) {
    Played p;
    Segment seg[Transport::kMaxSegments];
    Click clicks[64];
    std::int64_t lastTo = 0;
    bool first = true;
    while (p.outputs < total) {
        if (beforeBlock)
            beforeBlock(tr, p.outputs);
        const int n = static_cast<int>(std::min<std::int64_t>(block(), total - p.outputs));
        int covered = 0;
        const int count = tr.advance(n, seg, covered);
        int expectOffset = 0;
        for (int k = 0; k < count; ++k) {
            const Segment& s = seg[k];
            p.samplesOk = p.samplesOk && s.blockOffset == expectOffset && s.numSamples > 0;
            expectOffset += s.numSamples;
            if (s.ticksJump || first)
                p.jumps.push_back(p.outputs + s.blockOffset);
            else
                p.contiguous = p.contiguous && s.tickFrom == lastTo;
            first = false;
            for (std::int64_t t = s.tickFrom; t < s.tickTo; ++t) {
                const std::int64_t at = tr.map().sampleOfTick(t) - s.timelineStart;
                const bool edge = t == s.tickFrom || t == s.tickTo - 1;
                p.inside = p.inside && ((at >= 0 && at < s.numSamples) || edge);
                p.ticks.push_back({t, p.outputs + s.blockOffset + std::clamp<std::int64_t>(at, 0, s.numSamples - 1)});
            }
            lastTo = s.tickTo;
        }
        p.samplesOk = p.samplesOk && expectOffset == covered;
        const int c = clicksIn(tr.map(), seg, count, clicks, 64);
        for (int k = 0; k < c; ++k) {
            const std::int64_t at = p.outputs + clicks[k].offset;
            p.clicksInOrder = p.clicksInOrder && (p.clicks.empty() || at > p.clicks.back().first) && clicks[k].offset < n;
            p.clicks.push_back({at, clicks[k].accent});
        }
        p.outputs += covered;
    }
    return p;
}

inline TempoMap mapOf(double rate, std::vector<TempoPoint> points) {
    TempoMap m;
    m.set(rate, std::move(points));
    return m;
}

// Speeds up over bars 1-2, holds, jumps down at bar 4, slows down over bars 5-6.
inline TempoMap song(double rate = 48000.0) {
    return mapOf(rate, {{0, 90.0, true}, {7680, 150.0}, {11520, 100.0, true}, {19200, 70.0}});
}

inline std::function<int()> randomBlocks(unsigned seed) {
    auto rng = std::make_shared<std::mt19937>(seed);
    return [rng] { return std::uniform_int_distribution<int>(1, 1500)(*rng); };
}

inline bool same(const Played& a, const Played& b) {
    return a.ticks == b.ticks && a.jumps == b.jumps && a.clicks == b.clicks;
}

inline void checkStraight() {
    const std::int64_t total = song().sampleOfTick(8 * 3840);
    Transport a(song()), b(song());
    a.play();
    b.play();
    const Played whole = play(a, total, [] { return 1 << 20; });
    const Played pieces = play(b, total, randomBlocks(2));
    // Every tick once, in order, at its own sample; one jump: the start.
    bool everyTick = whole.ticks.size() == 8 * 3840;
    for (size_t i = 0; i < whole.ticks.size() && everyTick; ++i)
        everyTick = whole.ticks[i].first == static_cast<std::int64_t>(i) && whole.ticks[i].second == song().sampleOfTick(static_cast<std::int64_t>(i));
    DAWLAB_CHECK(everyTick && whole.jumps == std::vector<std::int64_t>{0} && pieces.contiguous && pieces.inside && pieces.samplesOk);
    DAWLAB_CHECK(same(whole, pieces));
    // A click on every beat at its sample, every fourth accented.
    bool clicks = whole.clicks.size() == 32;
    for (size_t i = 0; i < whole.clicks.size() && clicks; ++i)
        clicks = whole.clicks[i].first == song().sampleOfTick(static_cast<std::int64_t>(i) * 960) && whole.clicks[i].second == (i % 4 == 0);
    DAWLAB_CHECK(clicks);
    std::printf("  8 bars that speed up, jump and slow down: %zu ticks once each at their samples, %zu clicks; the same in blocks of 1-1500\n",
                whole.ticks.size(), whole.clicks.size());
    if (whole.clicks.size() == 32)
        std::printf("    beats 1-4 at samples %lld, %lld, %lld, %lld; beats 29-32 at %lld, %lld, %lld, %lld\n",
                static_cast<long long>(whole.clicks[0].first), static_cast<long long>(whole.clicks[1].first),
                static_cast<long long>(whole.clicks[2].first), static_cast<long long>(whole.clicks[3].first),
                static_cast<long long>(whole.clicks[28].first), static_cast<long long>(whole.clicks[29].first),
                static_cast<long long>(whole.clicks[30].first), static_cast<long long>(whole.clicks[31].first));
}

inline void checkLoop() {
    // Bars 2-4 looped, over the ramp's end and the jump; 5 passes. Every pass plays exactly the loop's ticks.
    const TempoMap m = song();
    const std::int64_t ls = 3840, le = 3 * 3840;
    const std::int64_t len = m.sampleOfTick(le) - m.sampleOfTick(ls);
    const std::int64_t total = m.sampleOfTick(ls) + 5 * len;
    Transport a(m), b(m);
    a.setLoop(ls, le, true);
    b.setLoop(ls, le, true);
    a.play();
    b.play();
    const Played whole = play(a, total, [] { return 1 << 20; });
    const Played pieces = play(b, total, randomBlocks(3));
    bool passes = whole.ticks.size() == static_cast<size_t>(ls + 5 * (le - ls));
    for (size_t i = 0; i < whole.ticks.size() && passes; ++i) {
        const std::int64_t want = static_cast<std::int64_t>(i) < ls ? static_cast<std::int64_t>(i) : ls + (static_cast<std::int64_t>(i) - ls) % (le - ls);
        passes = whole.ticks[i].first == want;
    }
    std::vector<std::int64_t> jumps{0};
    for (int k = 1; k <= 4; ++k)
        jumps.push_back(m.sampleOfTick(ls) + k * len);
    DAWLAB_CHECK(passes && whole.jumps == jumps && same(whole, pieces) && pieces.contiguous && pieces.samplesOk);
    DAWLAB_CHECK(whole.clicks.size() == 4 + 5 * 8); // bar 1, then the loop's 8 beats a pass
    std::printf("  bars 2-4 looped 5 times: each pass the loop's %lld ticks, the ticks jump at each wrap; the same in blocks of 1-1500\n",
                static_cast<long long>(le - ls));

    // Where ticks share samples (999 BPM at 8 kHz: two ticks a sample), the edges still come out exact.
    const TempoMap fast = mapOf(8000.0, {{0, 999.0}});
    Transport c(fast), d(fast);
    c.setLoop(960, 1921, true);
    d.setLoop(960, 1921, true);
    c.play();
    d.play();
    const std::int64_t total2 = fast.sampleOfTick(960) + 7 * (fast.sampleOfTick(1921) - fast.sampleOfTick(960));
    const Played p1 = play(c, total2, [] { return 64; });
    const Played p2 = play(d, total2, randomBlocks(4));
    bool exact = p1.ticks.size() == static_cast<size_t>(960 + 7 * 961);
    for (size_t i = 0; i < p1.ticks.size() && exact; ++i) {
        const std::int64_t want = i < 960 ? static_cast<std::int64_t>(i) : 960 + (static_cast<std::int64_t>(i) - 960) % 961;
        exact = p1.ticks[i].first == want;
    }
    DAWLAB_CHECK(exact && same(p1, p2) && p2.contiguous && p1.clicksInOrder && p2.clicksInOrder && p1.clicks.size() == 1 + 7 * 2);
    std::printf("  two ticks a sample, ticks 960-1920 looped 7 times, both edges on a shared sample: every pass exactly its 961 ticks\n");

    // Blocks that end exactly at the loop's end (a beat of 24000 samples, blocks of 3000): no empty segment.
    Transport h(mapOf(48000.0, {{0, 120.0}})), i(mapOf(48000.0, {{0, 120.0}}));
    h.setLoop(960, 1920, true);
    i.setLoop(960, 1920, true);
    h.play();
    i.play();
    const Played even = play(h, 6 * 24000, [] { return 3000; });
    const Played odd = play(i, 6 * 24000, [] { return 1 << 20; });
    DAWLAB_CHECK(even.samplesOk && even.contiguous && same(even, odd) && even.jumps.size() == 5); // the start, then 4 wraps

    // A loop of one beat in big blocks: more wraps than segments in a block; the rest comes in the next call.
    Transport e(mapOf(48000.0, {{0, 120.0}})), f(mapOf(48000.0, {{0, 120.0}}));
    e.setLoop(0, 960, true);
    f.setLoop(0, 960, true);
    e.play();
    f.play();
    Segment seg[Transport::kMaxSegments];
    int covered = 0;
    const int count = e.advance(1 << 20, seg, covered);
    DAWLAB_CHECK(count == Transport::kMaxSegments && covered == 8 * 24000);
    const Played rest = play(f, 20 * 24000, [] { return 1 << 20; });
    DAWLAB_CHECK(rest.ticks.size() == 20 * 960 && rest.jumps.size() == 20 && rest.clicks.size() == 20);
    // The playhead after the loop's end when the loop is switched on: it plays on.
    Transport g(mapOf(48000.0, {{0, 120.0}}));
    g.seekTick(5000);
    g.setLoop(0, 960, true);
    g.play();
    DAWLAB_CHECK(g.advance(512, seg, covered) == 1 && seg[0].timelineStart == 125000 && g.position() == 125512);
}

inline void checkNewMap() {
    // At 120 BPM, in bar 3; the user puts a jump to 60 at bar 2. The playhead stays in bar 3, the ticks go on.
    const TempoMap before = mapOf(48000.0, {{0, 120.0}});
    const TempoMap after = mapOf(48000.0, {{0, 120.0}, {3840, 60.0}});
    std::vector<std::int64_t> changedAt;
    auto change = [&](Transport& tr, std::int64_t played) {
        if (changedAt.empty() && played >= 2 * 96000 + 6000) { // bar 3, beat 2: tick 8640
            tr.setMap(after);
            changedAt.push_back(played);
        }
    };
    Transport a(before);
    a.play();
    const Played p = play(a, 600000, [] { return 512; }, change);
    bool onwards = !p.ticks.empty();
    for (size_t i = 1; i < p.ticks.size() && onwards; ++i)
        onwards = p.ticks[i].first == p.ticks[i - 1].first + 1;
    DAWLAB_CHECK(onwards && p.jumps.size() == 1 && p.contiguous);
    DAWLAB_CHECK(changedAt.size() == 1);
    // The tick at the change came out where the new map puts it relative to the change.
    const std::int64_t changeTick = before.firstTickAtOrAfter(changedAt.empty() ? 0 : changedAt[0]);
    std::int64_t heardAt = -1;
    for (const auto& [t, at] : p.ticks)
        if (t == changeTick + 3840)
            heardAt = at;
    // Where the playhead went: the exact (fractional) tick it was at, at its sample under the new map.
    const std::int64_t changed = changedAt.empty() ? 0 : changedAt[0];
    const std::int64_t newPosition = std::llround(after.secondsAt(before.tickAt(static_cast<double>(changed) / 48000.0)) * 48000.0);
    const std::int64_t want = changed + after.sampleOfTick(changeTick + 3840) - newPosition;
    DAWLAB_CHECK(heardAt == want);
    std::printf("  a tempo point put in while it plays: the playhead stays at tick %lld, no tick lost or played twice, no jump;\n"
                "    a bar after it is %lld samples later (it was 96000)\n",
                static_cast<long long>(changeTick), static_cast<long long>(after.sampleOfTick(changeTick + 3840) - after.sampleOfTick(changeTick)));
    // The loop is in ticks: its samples follow the new map.
    Transport b(before);
    b.setLoop(3840, 7680, true);
    b.seekTick(5000);
    b.play();
    Segment seg[Transport::kMaxSegments];
    int covered = 0;
    b.advance(512, seg, covered);
    b.setMap(after);
    const int count = b.advance(1 << 20, seg, covered);
    DAWLAB_CHECK(count >= 2 && seg[0].tickFrom == b.map().firstTickAtOrAfter(seg[0].timelineStart) && !seg[0].ticksJump);
    DAWLAB_CHECK(count >= 2 && seg[0].timelineStart + seg[0].numSamples == after.sampleOfTick(7680) && seg[1].timelineStart == after.sampleOfTick(3840));
    // A seek is a jump; stopped, nothing plays.
    b.seek(300000);
    b.advance(512, seg, covered);
    DAWLAB_CHECK(seg[0].ticksJump && seg[0].tickFrom == after.firstTickAtOrAfter(300000));
    b.stop();
    DAWLAB_CHECK(b.advance(512, seg, covered) == 0 && covered == 512);
    b.play();
    DAWLAB_CHECK(b.advance(512, seg, covered) == 1 && seg[0].ticksJump);
}

} // namespace step02_check

int main() {
    using namespace step02_check;
    std::printf("Step 2: a transport on a tempo map\n");
    checkStraight();
    checkLoop();
    checkNewMap();
    return dawlab::test::summary("l19_step02");
}
