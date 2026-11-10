#pragma once
// Chapter 20, Step 2 - the self-check (main). Included at the end of the starter and of the solution.

#include "minidaw.h"

#include <dawlab/Check.h>

#include <cstdio>
#include <random>

namespace step02_check {

using namespace minidaw;

inline TempoMap mapOf(double rate, std::vector<TempoPoint> points) {
    TempoMap map;
    map.set(rate, std::move(points));
    return map;
}

// A random tempo map: 1 to 12 points, 50 to 240 BPM, jumps and ramps.
inline TempoMap randomMap(std::mt19937& rng, double rate) {
    auto pick = [&rng](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };
    std::vector<TempoPoint> points;
    std::int64_t tick = 0;
    const int n = pick(1, 12);
    for (int i = 0; i < n; ++i) {
        points.push_back({tick, static_cast<double>(pick(500, 2400)) / 10.0, pick(0, 2) == 0});
        tick += pick(200, 12000);
    }
    return mapOf(rate, points);
}

inline Clip clipAt(std::int64_t start, std::int64_t length, int source = 0) {
    Clip c;
    c.source = source;
    c.start = start;
    c.length = length;
    return c;
}

inline void checkByHand() {
    const TempoMap at120 = mapOf(48000.0, {{0, 120.0, false}});
    const TempoMap at60 = mapOf(48000.0, {{0, 60.0, false}});

    // Two clips of 2 s on bar 2 (2 s at 120 BPM): one follows, one does not.
    Clip follows = clipAt(96000, 96000), stays = clipAt(96000, 96000);
    setFollow(follows, Follow::Start, at120);
    DAWLAB_CHECK(follows.follow == Follow::Start && follows.start == 96000 && std::fabs(follows.startTick - 3840.0) < 1e-6);
    DAWLAB_CHECK(startUnder(follows, at120) == 96000 && startUnder(follows, at60) == 192000);
    DAWLAB_CHECK(startUnder(stays, at120) == 96000 && startUnder(stays, at60) == 96000);
    // Two seconds are a bar at 120 and half a bar at 60 - and from sample 96000, which at 60 is beat 3 of bar 1.
    DAWLAB_CHECK(std::fabs(endTick(follows, at120) - 7680.0) < 1e-6 && std::fabs(endTick(follows, at60) - 5760.0) < 1e-6);
    DAWLAB_CHECK(std::fabs(endTick(stays, at60) - 3840.0) < 1e-6);

    std::vector<Clip> clips{follows, stays};
    retime(clips, at60);
    DAWLAB_CHECK(clips.size() == 2 && clips[0].start == 192000 && clips[1].start == 96000 && clips[0].length == 96000);
    std::printf("  two clips on bar 2, then 60 BPM instead of 120: the one that follows starts at %.1f s, the other at %.1f s\n",
                static_cast<double>(clips[0].start) / 48000.0, static_cast<double>(clips[1].start) / 48000.0);

    // Off again: it stays where it is now, also when the tempo goes back.
    setFollow(clips[0], Follow::Off, at60);
    retime(clips, at120);
    DAWLAB_CHECK(clips[0].follow == Follow::Off && clips[0].start == 192000);
    // Turned on while it is on: the tick is not found again from the sample.
    Clip twice = clipAt(0, 100);
    setFollow(twice, Follow::Start, at120);
    twice.startTick = 1234.5; // as a snap would have set it
    setFollow(twice, Follow::Start, at120);
    DAWLAB_CHECK(twice.startTick == 1234.5);
}

inline void checkNothingMoves() {
    // Turning it on moves nothing, on any map, at any sample.
    std::mt19937 rng(30);
    int moved = 0;
    constexpr int kClips = 400;
    for (int c = 0; c < kClips; ++c) {
        const TempoMap map = randomMap(rng, c % 2 == 0 ? 48000.0 : 44100.0);
        Clip clip = clipAt(std::uniform_int_distribution<std::int64_t>(0, 20000000)(rng), 1000);
        const std::int64_t before = clip.start;
        setFollow(clip, Follow::Start, map);
        std::vector<Clip> clips{clip};
        retime(clips, map);
        moved += clip.follow == Follow::Start && clips[0].start == before && startUnder(clip, map) == before ? 0 : 1;
    }
    std::printf("  following turned on for %d clips on random maps: %d moved\n", kClips, moved);
    DAWLAB_CHECK(moved == 0);
}

inline void checkNoDrift() {
    // A clip on bar 9, through 1000 tempo maps and back to the first: where it was, to the sample. And what
    // happens to a clip whose tick is found again from its sample at every change.
    std::mt19937 rng(31);
    const TempoMap first = mapOf(48000.0, {{0, 120.0, false}});
    Clip clip = clipAt(0, 1000);
    setFollow(clip, Follow::Start, first);
    moveToTick(clip, 8.0 * 3840.0, first);
    const std::int64_t home = clip.start;
    DAWLAB_CHECK(home == 16 * 48000 && clip.startTick == 8.0 * 3840.0);

    std::vector<Clip> clips{clip};
    Clip forgetful = clip;
    int offItsBar = 0;
    for (int k = 0; k < 1000; ++k) {
        const TempoMap map = randomMap(rng, 48000.0);
        retime(clips, map);
        offItsBar += clips[0].start == sampleOfTick(map, 8.0 * 3840.0) ? 0 : 1;
        forgetful.start = sampleOfTick(map, forgetful.startTick);
        forgetful.startTick = tickOfSample(map, forgetful.start); // the tick, found again from the rounded sample
    }
    retime(clips, first);
    forgetful.start = sampleOfTick(first, forgetful.startTick);
    std::printf("  a clip on bar 9 through 1000 tempo maps and back: %lld samples from where it was, %d times off its bar;\n"
                "    with its tick found again from its sample each time: %lld samples\n",
                static_cast<long long>(clips[0].start - home), offItsBar, static_cast<long long>(forgetful.start - home));
    DAWLAB_CHECK(clips[0].start == home && offItsBar == 0 && clips[0].startTick == 8.0 * 3840.0);
}

inline void checkMoving() {
    std::mt19937 rng(32);
    int wrong = 0;
    constexpr int kMoves = 300;
    for (int c = 0; c < kMoves; ++c) {
        const TempoMap map = randomMap(rng, 48000.0), other = randomMap(rng, 48000.0);
        Clip follows = clipAt(5000, 1000), stays = clipAt(5000, 1000);
        setFollow(follows, Follow::Start, map);

        // To a tick of the grid: exactly that tick, and its sample.
        const double tick = 240.0 * std::uniform_int_distribution<int>(0, 400)(rng);
        moveToTick(follows, tick, map);
        moveToTick(stays, tick, map);
        wrong += follows.startTick == tick && follows.start == sampleOfTick(map, tick) && stays.start == sampleOfTick(map, tick) ? 0 : 1;
        wrong += startUnder(follows, other) == sampleOfTick(other, tick) && startUnder(stays, other) == stays.start ? 0 : 1;
        wrong += stays.follow == Follow::Off && follows.follow == Follow::Start && follows.length == 1000 ? 0 : 1;

        // To a sample: that sample, and for the clip that follows the tick of it.
        const std::int64_t sample = std::uniform_int_distribution<std::int64_t>(0, 9000000)(rng);
        moveTo(follows, sample, map);
        moveTo(stays, sample, map);
        wrong += follows.start == sample && stays.start == sample && std::fabs(follows.startTick - tickOfSample(map, sample)) < 1e-6 ? 0 : 1;
        wrong += startUnder(follows, map) == sample ? 0 : 1;
    }
    std::printf("  %d clips moved to a tick and to a sample: %d wrong\n", kMoves, wrong);
    DAWLAB_CHECK(wrong == 0);
}

inline void checkClicks() {
    // A click (one sample) on each of 8 bars, as clips that follow; and the same clicks as clips that do not.
    const TempoMap at120 = mapOf(48000.0, {{0, 120.0, false}});
    const TempoMap ramp = mapOf(48000.0, {{0, 120.0, true}, {4 * 3840, 70.0, false}, {6 * 3840, 150.0, false}});
    const std::vector<std::vector<float>> sources{{1.0f}};
    std::vector<Clip> following, fixed;
    for (int bar = 0; bar < 8; ++bar) {
        Clip c = clipAt(sampleOfTick(at120, bar * 3840.0), 1);
        fixed.push_back(c);
        setFollow(c, Follow::Start, at120);
        moveToTick(c, bar * 3840.0, at120);
        following.push_back(c);
    }
    retime(following, ramp);
    retime(fixed, ramp);
    const int n = static_cast<int>(std::max(sampleOfTick(ramp, 8 * 3840.0), sampleOfTick(at120, 8 * 3840.0))) + 48000;
    std::vector<float> heard(static_cast<size_t>(n)), heardFixed(static_cast<size_t>(n));
    render(following, sources, 0, n, heard.data());
    render(fixed, sources, 0, n, heardFixed.data());
    int onBars = 0, onOldBars = 0, clicks = 0, clicksFixed = 0;
    for (int s = 0; s < n; ++s) {
        clicks += heard[static_cast<size_t>(s)] != 0.0f ? 1 : 0;
        clicksFixed += heardFixed[static_cast<size_t>(s)] != 0.0f ? 1 : 0;
    }
    for (int bar = 0; bar < 8; ++bar) {
        onBars += heard[static_cast<size_t>(sampleOfTick(ramp, bar * 3840.0))] == 1.0f ? 1 : 0;
        onOldBars += heardFixed[static_cast<size_t>(sampleOfTick(at120, bar * 3840.0))] == 1.0f ? 1 : 0;
    }
    std::printf("  8 clicks on 8 bars, then a map with a ramp: %d of the clips that follow are on their bars (bar 8 at %.3f s);\n"
                "    of those that do not, %d are where they were (bar 8 was at 14.000 s)\n",
                onBars, static_cast<double>(sampleOfTick(ramp, 7 * 3840.0)) / 48000.0, onOldBars);
    DAWLAB_CHECK(clicks == 8 && onBars == 8);
    DAWLAB_CHECK(clicksFixed == 8 && onOldBars == 8);
}

inline void checkSeams() {
    // A take of 8 s from bar 1, cut in two at bar 3 (4 s at 120 BPM). Both halves follow.
    const TempoMap at120 = mapOf(48000.0, {{0, 120.0, false}});
    Clip a = clipAt(0, 192000), b = clipAt(192000, 192000);
    b.sourceOffset = 192000;
    setFollow(a, Follow::Start, at120);
    setFollow(b, Follow::Start, at120);
    std::vector<Clip> clips{b, a}; // not in order: gaps() sorts
    const std::vector<std::int64_t> meet = gaps(clips);
    DAWLAB_CHECK(meet.size() == 1 && meet[0] == 0);
    DAWLAB_CHECK(gaps({}).empty() && gaps({a}).empty());

    retime(clips, mapOf(48000.0, {{0, 60.0, false}}));
    const std::vector<std::int64_t> slow = gaps(clips);
    retime(clips, mapOf(48000.0, {{0, 240.0, false}}));
    const std::vector<std::int64_t> fast = gaps(clips);
    DAWLAB_CHECK(slow.size() == 1 && slow[0] == 192000);  // bar 3 at 8 s, the first half ends at 4 s
    DAWLAB_CHECK(fast.size() == 1 && fast[0] == -96000);  // bar 3 at 2 s: two seconds are heard twice
    if (slow.size() == 1 && fast.size() == 1)
        std::printf("  a take cut in two at bar 3, both halves follow: at 60 BPM a gap of %.1f s, at 240 BPM %.1f s heard twice\n",
                    static_cast<double>(slow[0]) / 48000.0, static_cast<double>(-fast[0]) / 48000.0);

    // Three clips: a gap, then an overlap.
    const std::vector<std::int64_t> three = gaps({clipAt(1000, 500), clipAt(0, 900), clipAt(1400, 10)});
    DAWLAB_CHECK(three.size() == 2 && three[0] == 100 && three[1] == -100);
}

inline void checkWhilePlaying() {
    // The song plays at 120 BPM and is on beat 2 of bar 2 when the tempo becomes 60. Clips on bar 2.
    const TempoMap at120 = mapOf(48000.0, {{0, 120.0, false}});
    const TempoMap at60 = mapOf(48000.0, {{0, 60.0, false}});
    Clip follows = clipAt(96000, 480000), stays = clipAt(96000, 480000);
    setFollow(follows, Follow::Start, at120);
    // Half a second into the clip before; a beat after its start is a whole second at 60.
    DAWLAB_CHECK(jumpInClip(follows, at120, at60, 4800.0) == 24000);
    // The clip that stays: the playhead goes from 2.5 s to 5 s, the clip does not move.
    DAWLAB_CHECK(jumpInClip(stays, at120, at60, 4800.0) == 120000);
    // At the clip's own start nothing jumps if it follows; and no change of the map, no jump.
    DAWLAB_CHECK(jumpInClip(follows, at120, at60, 3840.0) == 0 && jumpInClip(follows, at120, at120, 4800.0) == 0);
    DAWLAB_CHECK(jumpInClip(stays, at60, at60, 4800.0) == 0);
    std::printf("  120 to 60 BPM while beat 2 of bar 2 plays: the audio of a clip that follows jumps by %.1f s, of one that does not by %.1f s\n",
                static_cast<double>(jumpInClip(follows, at120, at60, 4800.0)) / 48000.0, static_cast<double>(jumpInClip(stays, at120, at60, 4800.0)) / 48000.0);
}

} // namespace step02_check

int main() {
    using namespace step02_check;
    std::printf("Step 2: a clip whose start follows the tempo\n");
    checkByHand();
    checkNothingMoves();
    checkNoDrift();
    checkMoving();
    checkClicks();
    checkSeams();
    checkWhilePlaying();
    return dawlab::test::summary("l20_step02");
}
