# Step 5 - The app: tempo

**English** | [繁體中文](step-05-app-tempo.zh-TW.md)

[Overview](../README.md) | Previous: [Step 4 - The engine](step-04-engine.md) | Next: [Step 6 - The app: instruments](step-06-app-instruments.md)

## Goal

Bring the tempo map into DawLab: a lane to draw and edit it, a ruler and a grid that follow it, undo, the project
file, and MIDI files that carry their tempo changes in and out.

## Design

### Copy first, and it already works

The first commit of this step is chapter 18's app with only its name changed to DawLab 19, on chapter 19's engine. It
builds, and its smoke test passes as it is: for the app, one tempo is a tempo map of one point, and
`setTempo()` still means what it meant. That is Step 4's care about old interfaces paying off.

### The model: one more edit

Everything about the map is undoable with one edit, `SetTempoMap`, which replaces all the points - as chapter 18's
`SetMidiClips` replaces a track's clips. The transport bar's tempo field keeps its edit, `SetTempo`: it now sets
the *first* point's tempo and leaves the others. `Session::sampleOfTick()` and `tickOfSample()` go through the map,
so everything that already asked the session - MIDI clips in the arrangement, the piano roll's playhead, recording -
follows the map without knowing.

### What was in samples and should not be

Three places computed with "samples per beat", which no longer exists:

- **The ruler and the grid.** Bars and beats are ticks; each is drawn at the sample the map gives it. Beats lie
  closer together where the song is faster.
- **Snapping.** A clip snaps to the nearest beat: the exact tick of the mouse, rounded to a beat, back to a sample.
- **The loop.** Set in bars, it is now kept in ticks (`setLoopRangeBars()`), so it stays on its bars when the tempo
  changes. A sample-rate change leaves a loop in ticks alone.

And one place needed real thought - **MIDI recording through a loop**. Chapter 18 unwrapped a key's time into a
sample that counts on through the loop's passes, and divided by the tempo to get a tick. With a map, a sample past
the loop's end would be read at whatever tempo comes *after* the loop. So the sample is wrapped into its pass
first, its tick found through the map, and the passes counted on in ticks.

### The project file, format 7

```text
tempo 120 4 4
loop 3840 11520 0 ticks
tempopoint 0 120 0
tempopoint 3840 60 1
tempopoint 7680 180 0
```

A map of one point writes no `tempopoint` line: the file of a song with one tempo looks as it did. A loop set in
bars says `ticks`. A tempo is written with 17 digits, so that it reads back as the same number. Point 0 of the map
is the song's tempo, so the `tempo` line must give the same BPM; a file where they differ is refused. A file saved
before the tempo line had 17 digits (it had a float's 9) still opens: its line is point 0's BPM written with 9
digits, and the song takes point 0's.
`tests/test_project_format.cpp` reads and writes it and refuses what is not a map: a first point that is not at 0,
ticks that go back, a tempo out of range, a tick beyond 2^40, more than 256 points.

### The tempo lane

A strip above the arrangement, on its time axis: a level line where the tempo holds, a sloped one on a ramp, a dot
on every point.

| Gesture | Does |
|---|---|
| double click on nothing | a new point on the nearest beat, at the tempo of the click's height |
| drag a point | moves it between its neighbours (Shift: any tick, any tempo); the first stays at tick 0 |
| Alt + click a point | its tempo ramps to the next point's, or holds again |
| right click a point | deletes it (not the first) |

Each gesture is one undo step. As in chapter 18's piano roll, the mouse handlers only call `press()`, `dragTo()`
and `release()`, and the smoke test calls them too.

One thing is new. The time axis is in samples, so moving a tempo point moves every beat after it on the screen -
including the point under the mouse. Read through the *current* map, the mouse would be over another beat after
every step, and the point would run away from it. So a drag reads the mouse through the map **as it was when the
drag began**: the point goes to the beat the mouse was over then.

### MIDI files with their tempos

Chapter 18's Exercises 3 and 9 become part of the app. `MidiFile.h` reads and writes the bytes (no JUCE, tested in
every build); `MidiFileIO.h` turns a project into a song and a song into tracks; File > Import MIDI file and Export
MIDI file put them in the menu.

- **Out.** Every tempo point is a tempo event. A file has no ramps, so a ramp becomes steps of a sixteenth - each at
  the tempo that makes the step exactly as long as the ramp is over it. The tempo in the middle of the step would
  be the obvious choice, and it is 2 ms off after one beat from 120 to 60 BPM; the right one keeps every sixteenth
  on time. A gap longer than a delta time holds (2^28 - 1 ticks, some 39 hours at 120 BPM; a project holds MIDI
  clips up to 2^40 ticks) is split by empty text events, so every tick reads back exactly. A tempo is kept to the
  tempo map's 20 .. 999 BPM, and one that is no tempo (0, below 0, NaN) is written as MIDI's default of 120.
- **In.** Tempo events may be in any track, out of order, twice at one tick: they are sorted, and the last at a tick
  stays. They become jumps of the tempo map. A file may have more than the map's 256 points; keeping every k-th
  one would let the song drift by seconds, so `TempoThinning.h` keeps the *fewest* points with which no tick
  moves by more than a tolerance - 0 first, which only drops tempos that repeat, then the smallest of 0.5 ms,
  1 ms, 2 ms ... that fits - and the status line says which (Exercises 5 and 9 are this, to write yourself). The
  tracks and the tempos are **one** undo step.

## Checks

The smoke test has two new parts, and three tests run without JUCE:

```text
the tempo map
  120 then 60 BPM from bar 2: bar 3 at sample 288000, a note at bar 2 beat 2 sounds from 144000
  the lane: a point added and dragged, a point that stops at its neighbour, a ramp, a delete - each one undo step
MIDI files
  2 tracks and a map with a ramp out and in again: 6 tempo points, the ramp's sixteenths and bar 3 off by at most 0.0002 ms
  a file with 700 tempos in: 179 tempo points, the worst beat 3.99 ms off ("the file had more than the tempo map holds: no beat moved by more than 4.0 ms")
```

```text
l19_test_midi_file: Standard MIDI Files with tempo changes
  a song with three tempos: 90 bytes, exactly the expected ones, and read back
  tempos in two tracks, out of order, two at one tick, none at 0: 3 tempos in order
  1000 of 1000 random songs (tempos, names, every kind of message) came back the same
  14 of 14 broken files refused, 75 files cut short refused, 20000 damaged files: no crash (1338 still readable)
[PASS] l19_test_midi_file (10 checks passed)
```

```text
l19_test_tempo_thinning: the fewest tempo points within a timing tolerance
  400 of 400 random lists: as few points as the best of every choice (678 points dropped)
  700 tempos into 256 points: 179 points, the worst beat 3.99 ms off (within 4.0 ms); every k-th: 234 points, 222 ms off
  700 tempos into  64 points:  44 points, the worst beat 63.79 ms off (within 64.0 ms); every k-th:  64 points, 2557 ms off
  700 tempos into  16 points:  12 points, the worst beat 506.31 ms off (within 512.0 ms); every k-th:  16 points, 6067 ms off
  700 tempos into   2 points:   2 points, the worst beat 16888.37 ms off (within 32768.0 ms); every k-th:   2 points, 49216 ms off
[PASS] l19_test_tempo_thinning (49 checks passed)
```

## What you have to try yourself

1. **Draw a tempo.** Double click in the tempo lane at bar 3, drag the point up and down while the song plays: the
   notes follow, the audio clips do not.
2. **A ramp.** Alt-click the point before it, and listen to the metronome of your notes speed up.
3. **A MIDI file.** File > Import MIDI file with any `.mid` from the internet: its tracks, and its tempo changes in
   the lane. Export it again and open it in another program.

## What you learned

- An app that asks one place for its conversions follows a new model without knowing.
- What is counted in beats must be kept in ticks: the ruler, snapping, a loop set in bars.
- A drag that changes the axis it is drawn on reads the mouse through the axis of its start.
- A ramp becomes steps that keep the time, not steps that look like the ramp.
