#pragma once
// Chapter 20, Step 2: a clip whose start follows the tempo.
//
// The cheapest way for audio to follow the tempo: the clip's START is a tick. When the tempo map changes, the
// clip moves to where its tick now is, and plays at its own speed, sample for sample, as before. Nothing is
// stretched. That is all a cymbal on bar 17, a spoken line or a sound effect needs - audio with no beat of its
// own - and it is wrong for a loop or for a take of a band, which Steps 3 and 4 are for.
//
//   The tick is the truth    A clip that follows keeps `startTick` (a fraction: Step 1 said why) and DERIVES its
//                            start in samples from it, round(secondsAt(startTick) x rate), every time the map
//                            changes. Never the other way round: a tick found again from a rounded sample is
//                            a little off each time, and a clip that is re-anchored at every tempo change walks
//                            away from its bar line.
//   Turning it on            moves nothing: startTick is the exact tick of the sample where the clip is now.
//   Moving it                to a sample (a free drag): the tick is found from the sample, once.
//                            to a tick (a drag that snaps to the grid): the tick is kept exactly, and a clip that
//                            does not follow just gets the tick's sample.
//   Its length               stays in samples. So the clip's END is at another tick under another map: a clip of
//                            two seconds is a bar long at 120 BPM and half a bar at 60.
//   Seams                    Two clips cut from one take lie end to start. When both follow and the tempo goes
//                            down they come apart - a gap of silence; when it goes up they overlap, and the
//                            engine adds overlapping clips (chapter 14), so the overlap is heard twice.
//   While it plays           A new map keeps the playhead at its musical place (chapter 19, Step 2). A clip that
//                            follows moves with its start; the playhead inside it does not land on the sample it
//                            was at, because the time from the clip's start to the playhead is another one at
//                            another tempo. The audio jumps - by how much is jumpInClip().
//
// No JUCE, no threads. Header-only: the starter and the solution define the functions marked YOUR TURN.

#include "../tempo_map.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace minidaw {

enum class Follow {
    Off,   // the clip stays at its sample, whatever the tempo does (DawLab 19)
    Start, // the clip's start stays on its tick
};

struct Clip {
    int source = 0;                // which audio (an index into the sources of render())
    std::int64_t start = 0;        // the timeline sample of its first sample: what the player reads
    std::int64_t length = 0;       // samples
    std::int64_t sourceOffset = 0; // the first sample of the source it plays
    Follow follow = Follow::Off;
    double startTick = 0.0;        // Follow::Start: where the clip belongs. Unused while it does not follow.

    std::int64_t end() const noexcept { return start + length; }
};

// ---- provided ---------------------------------------------------------------------------------------------

// The timeline sample of a tick (a fraction) under a map.
inline std::int64_t sampleOfTick(const TempoMap& map, double tick) noexcept {
    return std::llround(map.secondsAt(tick) * map.sampleRate());
}

// The exact tick (a fraction) of a timeline sample under a map.
inline double tickOfSample(const TempoMap& map, std::int64_t sample) noexcept {
    return map.tickAt(static_cast<double>(sample) / map.sampleRate());
}

// The mix of the clips over the timeline samples [from, from + n): overlapping clips add up.
inline void render(const std::vector<Clip>& clips, const std::vector<std::vector<float>>& sources, std::int64_t from, int n, float* out) {
    std::fill(out, out + n, 0.0f);
    for (const Clip& clip : clips) {
        if (clip.source < 0 || clip.source >= static_cast<int>(sources.size()))
            continue;
        const std::vector<float>& audio = sources[static_cast<size_t>(clip.source)];
        const std::int64_t a = std::max(from, clip.start), b = std::min(from + n, clip.end());
        for (std::int64_t s = a; s < b; ++s) {
            const std::int64_t i = clip.sourceOffset + (s - clip.start);
            if (i >= 0 && i < static_cast<std::int64_t>(audio.size()))
                out[s - from] += audio[static_cast<size_t>(i)];
        }
    }
}

// ---- YOUR TURN (starter/step02.cpp) -----------------------------------------------------------------------

// Turns following on or off. On: startTick becomes the exact tick of the clip's start; the start stays. Off: the
// clip stays where it is now.
void setFollow(Clip& clip, Follow follow, const TempoMap& map) noexcept;

// Where the clip starts under a map: the sample of its tick if it follows, its start if it does not.
std::int64_t startUnder(const Clip& clip, const TempoMap& map) noexcept;

// The map has changed: every clip that follows goes to its place under the new one.
void retime(std::vector<Clip>& clips, const TempoMap& newMap) noexcept;

// A free drag: the clip starts at `sample`. A clip that follows gets the tick of that sample.
void moveTo(Clip& clip, std::int64_t sample, const TempoMap& map) noexcept;

// A drag that snaps: the clip starts at `tick`. A clip that follows keeps exactly this tick.
void moveToTick(Clip& clip, double tick, const TempoMap& map) noexcept;

// The tick of the sample after the clip's last one, under a map (the clip at its place under that map).
double endTick(const Clip& clip, const TempoMap& map) noexcept;

// The clips in the order of their starts, and after each but the last one: the samples from its end to the next
// clip's start. 0: they meet. Above 0: a gap of silence. Below 0: an overlap, heard twice.
std::vector<std::int64_t> gaps(std::vector<Clip> clips);

// The map changes from oldMap to newMap while the playhead is at `playheadTick`, and keeps its musical place.
// By how many samples does the position INSIDE the clip jump? (The position inside the clip is the playhead's
// sample minus the clip's start, each under the same map.)
std::int64_t jumpInClip(const Clip& clip, const TempoMap& oldMap, const TempoMap& newMap, double playheadTick) noexcept;

} // namespace minidaw
