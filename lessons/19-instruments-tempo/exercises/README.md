# Chapter 19 - Exercises
**English** | [繁體中文](README.zh-TW.md)

[Back to the chapter](../README.md) | [Solutions](../solutions/README.md)

Difficulty: ★☆☆ easy, ★★☆ medium, ★★★ hard. Implementation exercises have a starter file in
`exercises/starter/` that compiles and runs; until you complete it, its self-check prints `FAIL`
(that is expected). Build target names are given in brackets. Exercises 1-6 need no JUCE:

```powershell
.\scripts\build.ps1 -Only 19-instruments-tempo
.\build\bin\Debug\l19_ex3_starter.exe
```

**Exercises 1-6** are the chapter's exercise set, about the steps (no JUCE). **Exercises 7-9 are optional
projects** that extend DawLab 19. They need a JUCE build (`.\scripts\build.ps1 -Only 19-instruments-tempo -Juce`);
like chapter 18's they are console programs that build the real `MainComponent` in its offline mode
(`exercises/app_rig.h`), and none of them changes the app. Exercises 8 and 9 use Exercises 4 and 5, solved, from
`exercises/tempo_model.h` - do those two first.

Exercises 2, 4 and 5 build on Step 1's tempo map (`steps/step02-transport/minidaw.h` has it solved). Exercise 3
stands alone. Do the steps first.

---

## Exercise 1 - Why this way? ★☆☆ (concept)

Answer in a few sentences each.

1. Step 1 keeps the tempo map in seconds, not samples. What would have to happen to a map in samples when the
   audio device changes its rate - and what would it cost?
2. A loop set in bars is kept in ticks (Step 2), and its two edges are *given* in ticks to the segments instead
   of being found from samples. Which tick would a pass lose or play twice otherwise, and when?
3. When the tempo map changes while the song plays, the playhead keeps its musical place and the samples jump.
   Give the other choice, and say what the user would see and hear with each.
4. Step 4 passes the map from the UI thread to the audio thread through three buffers. Why not an atomic, why
   not a mutex, and what is wrong with a seqlock here although it "works"?
5. With low-latency monitoring, the armed track is early against the others by its compensation. Why does
   nobody mind while recording, and why must the plan be whole again for playback?
6. A recorded note is placed by where the song was heard, whatever the latency of the instrument it was
   played on (Step 3). A musician on a slow sampler plays early to make up for it. Where do their notes land,
   and is that right?
7. Exporting a MIDI file, Step 5 writes a ramp as steps whose tempo is *not* the ramp's tempo anywhere in
   the step. What does it keep instead, and why is that what matters to the program that reads the file?

## Exercise 2 - Tempo by the numbers ★★☆ (calculation)

Compute by hand (show the formulas), then run [`l19_ex2_solution`] to check.

- (a) A ramp from 60 to 120 BPM over four bars of 4/4. How long does it take? When does the bar line in its
  middle come? At which tick has half its time passed, and what is the tempo there?
- (b) 120 BPM, and 90 from bar 3. When does bar 5 start, and at which sample at 48 kHz and at 44.1 kHz?
- (c) Tracks with latencies 0, 256, 64 + 32 and 4096 samples at 48 kHz. The total, and how long each track
  waits. A key on the first track, with the keyboard scheduled 1024 samples late and an output latency of 512:
  when is it heard, with and without low-latency monitoring?
- (d) A MIDI file stores a tempo as a whole number of microseconds a beat. What does 97.5 BPM become, and how
  far has a song drifted after ten minutes?
- (e) A ramp from 120 to 60 BPM over one beat, written as four steps of a sixteenth. How long is the beat?
  How long with each step at the tempo of its middle?

## Exercise 3 - A meter map ★★☆ (implementation)

[`l19_ex3_starter`, file `exercises/starter/ex3_meter_map.cpp`]

The chapter gave the tempo a map and kept one time signature. Give the meter its map too: a list of changes,
each a bar and a time signature that holds from that bar on. Write `prepare()`, `tickOfBar()`, `positionOf()`
(the bar, the beat and the ticks into the beat of a tick) and its inverse `tickOf()`.

It is all whole numbers - nothing to round - but a 6/8 bar is not six quarters long, a beat is whatever the
denominator names, and a tick before 0 is in bar -1, not in bar 0 counted backwards. The self-check compares
with a reference that counts bar after bar for 300 random maps.

## Exercise 4 - A tempo map from beats ★★☆ (implementation)

[`l19_ex4_starter`, file `exercises/starter/ex4_beats_to_map.cpp`]

A band recorded without a click, and the grid fits nowhere. Turn it around: from the times where the beats
*are* - tapped along, or found by a beat detector - build the tempo map that puts the grid there.

- `mapFromBeats()`: a jump at each beat to the tempo that makes the next beat arrive on time - but no point
  where the tempo just goes on. It refuses beats that do not come one after another, tempos no map can hold,
  and more points than a map has.
- `tapTempo()`: one tempo from a few taps, by the median interval, so that a missed tap does not halve it.

The self-check builds maps for 300 random bands and asks Step 1's map where each beat plays: within a tenth
of a microsecond of where it was heard.

## Exercise 5 - Thin a tempo list ★★★ (implementation)

[`l19_ex5_starter`, file `exercises/starter/ex5_thin_tempo.cpp`]

Exercise 4's map of a real band has a point on every beat, and a MIDI file from another program may have a
thousand. The easy way - keep every k-th point - lets the song drift. Write `thinTempo()`: the *fewest* points
with which no tick of the song moves by more than a tolerance. (DawLab 19's import does this, in
`app/Source/TempoThinning.h` - which gives the exercise away, so look at it afterwards.)

- A point that stays gets the tempo that brings the next point that stays at exactly its old time, so that a
  point that stays never moves and errors do not add up. The last point stays as it is: its tempo holds for ever.
- A run of points may go if each of them then plays within the tolerance of its old time.
- Taking the longest run that fits, again and again, is not always the fewest. Find the fewest.

The self-check compares with every possible choice for 400 short lists, and thins a band of 250 beats within
1, 5 and 20 ms - next to what "every k-th point" does with the same number of points.

## Exercise 6 - Audio that follows the tempo ★★★ (design)

No code. In DawLab 19 the notes move with the tempo map and the audio clips stay where they are in samples
(Step 2): drag a tempo point, and the recorded guitar is no longer with the drums. Design the option "this clip
follows the tempo", in a page:

1. "Follow" can mean two things: the clip's *start* stays on its bar, or *everything in it* stays on its beat.
   What does each need, and which audio is each right for?
2. What must a clip store so that it knows where its audio belongs in the music - also when it was recorded
   over a map with a ramp?
3. Changing a clip's speed without its pitch needs a time-stretcher. Should it run on the audio thread or
   ahead of time? What does each choice cost, and what happens when the map changes while the song plays?
4. How does the player know which audio a block needs, so that a long clip does not drift? (Step 2 already
   gives it what it needs.)
5. What do recording, the project file and undo have to do differently?

---

## Exercise 7 - A click track ★☆☆ (optional project, JUCE)

[`l19_ex7_starter`, file `exercises/starter/ex7_click_track.cpp`]

The metronome clicks while you play; the drummer who gets the song as a file needs the click *in* the song.
Write `addClickTrack()`: a new instrument track "Click" with one clip that has a note on every beat of the bars
you name, a higher and louder one on the first beat of each bar - as one undo step.

The self-check plays it: at 120 BPM bar 2 is heard after 2 s, at 60 BPM after 4 s, and with a ramp between the
two after $`4 \ln 2`$ s. Nobody moved a note: they are in ticks. It also fills the project with click tracks until
there is no room, and expects the last one to be refused with nothing left behind.

## Exercise 8 - Tap tempo ★★☆ (optional project, JUCE)

[`l19_ex8_starter`, file `exercises/starter/ex8_tap_tempo.cpp`]

Exercise 4 in the app, with the history:

- `applyTapTempo()`: the "Tap" button. The median tempo of the taps, rounded to a tenth of a BPM, becomes the
  tempo of the map's first point; the other points stay. One undo step - and none at all when the tempo is the
  one the map already has.
- `followBeats()`: the map becomes the one that puts the song's beats where they were heard, as one undo step;
  refused when it would need more than the engine's 256 points.

The self-check asks the engine's transport for the sample of every beat of 100 random bands: within one sample
of where it was heard.

## Exercise 9 - A better MIDI import ★★☆ (optional project, JUCE)

[`l19_ex9_starter`, file `exercises/starter/ex9_better_import.cpp`]

A file may have more tempos than the map's 256, and keeping every k-th one lets the song drift. Put Exercise 5
before the import. (The app's import does this itself today, in `app/Source/TempoThinning.h`; at first it kept
every k-th tempo. Write it yourself before you look.)

- `prepareTempos()`: the file's tempos as a list a map can be made of (clamped, the first at tick 0, the later of
  two at one tick), then thinned with a tolerance of 0 - which drops every tempo that only repeats - and, only if
  that is still too many, with the smallest of 0.5 ms, 1 ms, 2 ms ... that fits.
- `importSongBetter()`: the app's `importSong()` for the song with these tempos - still one undo step.

The self-check imports a file with 700 tempos your way and with every k-th tempo, compares how far the beats
move, and expects the app's own import to need the same number of points.
