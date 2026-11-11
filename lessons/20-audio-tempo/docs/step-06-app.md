# Step 6 - The app

**English** | [繁體中文](step-06-app.zh-TW.md)

[Overview](../README.md) | Previous: [Step 5 - The engine](step-05-engine.md)

## Goal

DawLab 20: right-click an audio clip and say what it does when the tempo changes - stay, keep its start on the
beat, follow as a tape, or follow and keep its pitch. With undo, in the project file, and without rewriting the
app around it.

## Design

### Copy first, and it already works

`app/` starts as a plain copy of chapter 19's with its name changed to DawLab 20 (one commit). On chapter 20's
engine it builds, and its smoke test passes as it is: no clip follows yet.

### Where the data lives

The app's model is plain lists of `engine::Clip`: the session keeps one per track, an edit (`SetClips`) carries a
whole list, and the undo history is a pile of those. Arrangement, takes, recording, the project file and the
sample-rate conversion all read a clip's `start` and `length` in samples.

Step 5's `FollowClip` wraps a clip. Wrapping every clip of the app would have meant touching all of that. So the
data goes the other way round - **the clip carries it**:

```cpp
struct Clip {
    ...
    std::string name;                         // UI only; the audio thread never reads it
    std::shared_ptr<const ClipFollow> follow; // UI only: how the clip follows the tempo; null if it does not
};

struct ClipFollow {
    FollowMode mode;
    double startTick;                // Start
    std::int64_t sourceLength;       // Varispeed, Stretch: the clip plays [sourceOffset, sourceOffset + sourceLength)
    std::vector<WarpMarker> markers; // Varispeed, Stretch
};
```

It is the one line this chapter adds to chapter 14's `Clip.h`, next to `name`, which has been "UI only" since
then. Every list of clips, every edit and every undo step now carries the follow data without knowing it.

### A clip's start and length are derived

For a clip that follows, `start` and `length` are **the song samples it covers under the tempo map of now** -
so everything that draws, hits and drags clips goes on working in samples. What the clip *is*, is in `follow`.
The session keeps the two in step in exactly two places:

- `setClips()` places every clip of the list (`placeUnder()`), whatever the list said. An undo step holds a list
  from another time, perhaps another tempo; when it comes back, its clips go where the map of now puts them.
- `setTempoMap()` places and sends the clips of every track.

This is Step 2's rule once more - the tick is the truth, the sample is derived - and it is why undo needs
nothing new: `SetTempoMap` restores the map, and the clips follow it back.

### What the engine gets

`Session::pushClips()` used to hand a track's clips to the engine. Now it hands them to the track's
`FollowTrack` (Step 5) and gives the engine what comes back: the clips themselves where they do not follow,
renders or placeholders where they do. The window's timer calls `Session::pollRenders()`, which takes finished
renders and hands those over. `getPlayedClips()` is a copy of what the engine has - the arrangement draws that,
so a stretched clip shows the waveform it plays.

An export calls `finishRenders()` first and waits: a bounce must not go out with placeholders.

### Turning it on

`setClipFollow(track, clip, mode)` is one undo step and moves nothing (`withFollowMode()`):

- **Start**: `startTick` is the tick of where the clip starts now.
- **Varispeed, Stretch**: the clip gets the markers of a take recorded over the map of now, where the clip is
  now. In words: *as it plays at this moment, it is in time*. That is right for a take that was just recorded,
  and for a loop that was dropped in at the tempo it was made for.
- A clip that was already warped keeps its markers when it changes between varispeed and stretch.
- **Off**: the clip stays where it is and plays its source at its own speed again.

### Moving and cutting

A clip that is dragged takes its tick or its markers along (`movedTo()`): the markers move by as many ticks as
the start does.

When a recording lands on a clip, chapter 17 cuts the old clip around the new one. For a warped clip the cut is
made in its own terms (`cutToSongRange()`): the pieces keep the markers and play less of the source. The piece
before ends, and the piece after starts, within a sample of the new clip's edges.

### The project file, format 8

A clip that follows has lines under its `clip` line:

```text
clip 1 384000 256000 "Follow_media/000.wav" 0 0 0 linear 0 0 "clicks"
follow stretch 192000
marker 0 11520
marker 192000 19200
```

(Two bars of audio made at 120 BPM, on bar 4, in a song at 90 BPM.) `follow start <tick>` for a start; `follow varispeed|stretch <length in the source>` and at least two `marker
<source> <tick>` lines for the others. Ticks and source positions are written with 17 digits: they are
fractions, and they must read back as the same numbers. The `start` and `length` of the `clip` line are where the
clip was when the file was saved; on opening it is placed again under the file's tempo map. Files of formats 1
to 7 have no such lines and open as before.

### What it does not do

- **Recording does not make a take follow.** A take is recorded as a clip that stays; you turn following on
  afterwards, and it gets the markers of the map at that moment. Turn it on before you change the tempo.
- **Takes are chosen before a clip follows.** The clip menu offers the takes only while the clip does not follow.
- **Markers cannot be edited.** There is no beat detection and no marker to drag: a clip is in time as it plays
  when following is turned on.
- **The renders are not in the project file.** After opening, clips that follow play placeholders for the
  moment their renders take.
- A stretch of a long clip takes its time (Step 4: some 50 million multiplications a second of audio), and
  `finishRenders()` before an export blocks the window for that long.

## Checks

The smoke test has a new part, and the format test a new section:

```text
audio clips that follow the tempo
  a take of 8 clicks made at 120 BPM, by varispeed at 60: 8 clicks, on their beats yes, the clip 8 s long
  start, varispeed and stretch: set, moved, cut, saved (format 8), opened, at another sample rate, and off again
```

```text
clips that follow: a start in ticks, varispeed and stretch with their markers written and read back exactly
[PASS] l20_test_project_format (176 checks passed)
```

`tests/test_warp.cpp` has a seventh part for the clip helpers, without JUCE:

```text
  a clip that carries how it follows: its mode changed, placed, moved and cut - the pieces of 200 cuts tile its source
```

## What you have to try yourself

1. **A loop.** Drop a drum loop on a track at its own tempo. Right-click it: *Follow, pitch changes
   (varispeed)*. Drag the first tempo point up and down while it plays: the loop moves at once (a placeholder),
   and a moment later it is in time.
2. **The same with stretch.** *Follow, pitch stays (stretch)* on the loop: listen to what happens to the hits at
   half the tempo. Then try it on a sustained sound.
3. **A cymbal.** *Keep its start on the beat* on a single hit at bar 5; change the tempo before it.
4. **Undo.** Undo the tempo change, then the mode: the clip goes back both times.

## What you learned

- Data that every part of a model must carry is cheapest inside the thing the model already passes around.
- A value derived from a truth is recomputed in the few places where the truth or the context changes - here two.
- An undo history of whole states needs no new edits for data that travels inside those states.
- A renderer in the background shows in the app as one question: what does the user see and hear until it is done?
