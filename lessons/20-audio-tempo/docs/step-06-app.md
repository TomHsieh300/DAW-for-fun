# Step 6 - The app

**English** | [繁體中文](step-06-app.zh-TW.md)

[Overview](../README.md) | Previous: [Step 5 - The engine](step-05-engine.md) | Next: [Step 7 - The end of the chapter](step-07-the-end.md)

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

An export calls `finishRenders()` first and waits: a bounce must not go out with placeholders. While it renders,
the session takes no edit (`holdEdits()`): one would be in the file from wherever the render had got to.

Every change of the map pushes new lists for many tracks at once, and the engine's command queue can be full
(no device takes from it, or the audio thread is late). What the session sends is always a whole state - a
track's clips, its MIDI clips, a lane's curve - so a list that was dropped is just sent again, the newest of it:
`catchUpEngine()`, from the window's timer. With no device the timer applies the commands itself.

`FollowTrack` knows a clip by its audio, not by its place in the list: a clip that another one is dragged in
front of keeps its render, and a clip that is moved plays its last render at its new start until the new one is
ready.

### Turning it on

`setClipFollow(track, clip, mode)` is one undo step and moves nothing (`withFollowMode()`):

- **Start**: `startTick` is the tick of where the clip starts now.
- **Varispeed, Stretch**: the clip gets the markers of a take recorded over the map of now, where the clip is
  now. In words: *as it plays at this moment, it is in time*. That is right for a take that was just recorded,
  and for a loop that was dropped in at the tempo it was made for.
- A clip that was already warped keeps its markers when it changes between varispeed and stretch.
- **Off**: the clip stays where it is and plays its source at its own speed again.
- The mode a clip already has changes nothing, and is no undo step: a start's tick is not found again from its
  sample, which would move it by a rounding each time.

The clip menu keeps the clip it was opened for, not its index (`chooseFromClipMenu()`): if the list changed
while it was open, the item still acts on that clip, or on nothing if the clip itself changed.

### Moving and cutting

A clip that is dragged takes its tick or its markers along (`movedTo()`): the markers move by as many ticks as
the start does.

When a recording lands on a clip, chapter 17 cuts the old clip around the new one. For a warped clip the cut is
made in its own terms (`cutToSongRange()`): the pieces keep the markers and play less of the source. The piece
before ends, and the piece after starts, on the samples of the new clip's edges. Where a sample of the source covers
many of the song (a take played much slower than it was made), the edge falls inside one: the pieces keep the
fraction of their first and last sample they play (`ClipFollow::headCut`, `tailCut`), and format 9 saves it.

### The project file, formats 8 and 9

A clip that follows has lines under its `clip` line:

```text
clip 1 384000 256000 "Follow_media/000.wav" 0 0 0 linear 0 0 "clicks"
follow stretch 192000
marker 0 11520
marker 192000 19200
```

(Two bars of audio made at 120 BPM, on bar 4, in a song at 90 BPM.) `follow start <tick>` for a start; `follow varispeed|stretch <length in the source>` and at least two `marker
<source> <tick>` lines for the others. Ticks and source positions are written with 17 digits, as every double
in the file is (the sample rate and the tempos too): they are fractions, and they must read back as the same
numbers. The `start` and `length` of the `clip` line are where the
clip was when the file was saved; on opening it is placed again under the file's tempo map. Files of formats 1
to 7 have no such lines and open as before.

Format 9 adds one line after the markers of a piece of a cut: `cut <head> <tail>`, the fractions of its first and
last sample of the source that it does not play, each from 0 up to (not with) 1. A clip with none has no such line,
so a project with no cut piece reads in chapter 20 as before; chapter 19 refuses format 9 as made by a newer DawLab.

These lines come from outside, so they are checked where they are read (`followProblem()` in ProjectFormat.cpp),
and a file that would place a clip where DawLab never puts one is refused with its line: a start tick below 0,
a tick or position beyond 2^40, two markers between which no tempo goes (outside 2^-20 to 2^20 ticks per sample),
or markers that do not cover the part of the source the clip plays (a sample of rounding allowed). A warped clip
may have a `length` of 0: at a fast tempo a piece of a sample or two covers no sample of the song. And a cut that
leaves a piece with nothing of the source in it does not keep that piece (`Session::withClipOnTop()`).

The `clip` line keeps the limits of chapters 15 to 19, in one file they share (`ProjectLimits.h`): a sample rate
of 8000 to 768000 Hz, every position and length at most 2^40, and each fade at most 60 seconds at the file's
rate. A fade is not limited by its clip: a warped clip keeps its fades when a faster tempo shortens it.

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
  start, varispeed and stretch: set, moved, cut, saved (format 9), opened, at another sample rate, and off again
```

```text
clips that follow: a start in ticks, varispeed and stretch with their markers written and read back exactly
[PASS] l20_test_project_format (220 checks passed)
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
