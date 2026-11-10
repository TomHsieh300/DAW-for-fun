# Step 5 - The engine

**English** | [繁體中文](step-05-engine.zh-TW.md)

[Overview](../README.md) | Previous: [Step 4 - A time-stretcher](step-04-stretch.md)

## Goal

Bring Steps 1 to 4 into DawLab's engine without making its audio thread do any of it: clips that follow the
tempo are rendered ahead of time on a worker thread and handed to an engine that plays plain clips, as it
always has.

## Design

### Copy first, then change

`engine/` and `tests/` start as a plain copy of chapter 19's (one commit, nothing changed, 15 tests pass). This
step then adds two headers and one test - and changes **no** existing file but `All.h`, which includes them.
`scripts/check_copies.py` shows it: every other engine file is still the same as chapter 19's.

### Where the stretcher runs

There are two places to put a stretcher.

**On the audio thread**, a new tempo is heard at once. But WSOLA works on pieces of 20 ms, so it has latency,
which the track would have to report as an instrument does (chapter 19, Step 3). Its search costs some 50 million
multiplications a second of output - for every clip, in every block, in the one thread that must never be late.

**Ahead of time**, a worker thread makes a new buffer, and the engine gets it in a command like everything else.
The audio thread plays a plain clip: no latency, no extra cost, and `ClipPlayer` does not change. The price is
the time the render takes - what plays until it is ready? - and memory for a second copy of the audio.

DawLab renders ahead of time. It is the hand-over the engine already has (`setTrackClips()`, the command queue,
the garbage queue for the old list), and it lets the render use the sinc of Step 3 and the search of Step 4 at
full quality. A DAW in which you drag the tempo while listening and hear it stretch under your hand needs the
other one, usually with a cache of this one behind it.

### What a clip is now

```cpp
enum class FollowMode { Off, Start, Varispeed, Stretch };

struct FollowClip {
    Clip clip;                       // as chapter 14's: what plays when it does not follow
    FollowMode mode;
    double startTick;                // Start: where the clip belongs (Step 2)
    std::vector<WarpMarker> markers; // Varispeed, Stretch: where the audio belongs (Step 1)
};
```

`renderFollowClip()` turns a `FollowClip` and a tempo map into the plain `Clip` that plays:

- **Off**: the clip.
- **Start**: the clip at the sample of its tick.
- **Varispeed**, **Stretch**: a new buffer for exactly the song samples whose position is in the clip's range of
  its source, `[sourceOffset, sourceOffset + length)` - so two halves of a cut take still meet at every tempo.

Offline the position of **every** song sample is computed exactly. Step 3's lines between knots are for a player
that has to be fast; here a second of audio costs a few milliseconds of `tickAt()`, and nothing bends.

Two channels slide together in a stretch: the search runs on their mean and every channel is cut at the same
place. If left and right each found their own best fit, the stereo image would wobble.

### The worker

`FollowRenderer` owns one thread, a queue of requests and a list of results, behind one mutex. A mutex is fine
here: only the UI thread and the worker ever touch it, and neither of them is the audio thread.

```text
UI thread                         worker thread                    audio thread
tempo map or a clip changes
FollowTrack::update()
  placeholders to the engine  ----------------------------------->  plays them
  FollowRenderer::request()  -->  renderFollowClip()
(a timer)                         ... done
FollowRenderer::collect()    <--  result
FollowTrack::take()
  the renders to the engine  ------------------------------------>  plays them
```

### Stale renders

The user drags a tempo point: a new map every few milliseconds, each asking for a render that takes longer than
that. Three rules keep this in order.

1. **Every request has a generation**, counted per clip. `collect()` gives back a result only if it is the
   newest generation asked for; a render of an older map is dropped even if it was already finished.
2. **A request that waits is replaced** by a newer one of the same clip, and one the worker is at is
   **cancelled**: the render looks at a flag once a piece and gives up.
3. **What has not changed is not rendered**. A new gain, fade, name or mute needs no new buffer, and a clip that
   is the same under the same map keeps its render.

The test sends 120 maps in a row: one or two renders are finished on the way, and what plays in the end is the
render of the last map.

### What plays in between

Until its render is ready a clip plays a **placeholder**: its last render, moved to where the clip now starts.
That is Step 2's behaviour for a moment - the clip is on its beat at its start and at the old speed after it.
With no render yet (the clip was just made) the placeholder is the clip's own audio.

An export must not go out with placeholders: `FollowRenderer::waitUntilIdle()` blocks until every render is
done. Step 6 calls it before a bounce.

### What it does not do

- The renders are not saved. They are a cache: the source, the markers and the map make them again.
- A render longer than 93 minutes at 48 kHz is refused, and the clip plays as it is.
- In a stretch, two halves of a cut take are stretched separately: each may slide up to 5 ms, so their seam is
  not sample-exact as it is in varispeed. A short crossfade at the cut hides it.
- A piece of a stretch reads up to 5 ms beyond the clip's range of its source: audio just outside a trimmed edge
  can be heard at the edge. A fade at the edge hides that too.

## Checks

`tests/test_warp.cpp` runs without JUCE, also under ThreadSanitizer and AddressSanitizer:

```text
l20_test_warp: audio clips that follow the tempo
  150 takes over random maps: 0 refused, the worst sample 0.488 ms from where it was recorded (7005 markers)
  varispeed, a 440 Hz loop at half and at twice its tempo: 220 Hz (the rest 109 dB below) and 880 Hz (151 dB)
  stretch, the same loop: 440 Hz at both tempos (+0.72 and +0.25 cents, the rest 31 and 40 dB below)
  12 clicks recorded at 120 BPM, through the engine over 100, 150 and 75 BPM: 12 heard, 0 off their beats (the last at 6.400 s)
  120 maps in a row: 1 renders taken on the way, the render at the end is the last map's (392505 samples, wanted 392504)
  an audio thread next to a UI thread that changes the map and a worker that renders: 27 renders handed over in 30527 blocks,
    0 allocations on the audio thread
[PASS] l20_test_warp (71 checks passed)
```

The numbers of the last two lines depend on how fast the machine is; the checks do not.

Its last part runs a real audio thread next to the UI thread and the worker: the audio thread allocates nothing
while renders arrive (the file replaces `operator new` and counts), and ThreadSanitizer sees no race. The old
buffers are freed where chapter 14 put that: with the old clip list, in `collectGarbage()` on the UI thread.

## What you learned

- Work that is heavy and has latency does not belong on the audio thread if its result can be made before.
- An engine that plays plain clips can play warped ones without knowing it: the render is just another buffer.
- A cache that is made in the background needs generations: only the newest request of a thing counts.
- What plays while the cache is stale is a decision, and the cheapest honest one is the last result moved to its
  new place.
