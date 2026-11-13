# Step 7 - The end of the chapter

**English** | [繁體中文](step-07-the-end.zh-TW.md)

[Overview](../README.md) | Previous: [Step 6 - The app](step-06-app.md)

There is no code in this step. It says what the chapter built, where each piece is, and - as plainly - what it
left out.

## What was built

Chapter 19 ended with a song whose notes followed the tempo and whose audio did not. Now a clip can do one of
four things when the tempo changes:

<div align="center">

| The clip ... | What follows | Pitch | What it costs | Right for |
|---|---|---|---|---|
| stays | nothing | as recorded | nothing | anything, while the tempo is fixed |
| keeps its start on the beat (Step 2) | its start | as recorded | nothing; cut takes come apart or overlap | a cymbal, a spoken line, a sound effect |
| follows by varispeed (Step 3) | everything in it | moves with the speed: $`12 \log_2`$ of it in semitones | a render ahead of time | drums, and small changes of tempo |
| follows by stretch (Step 4) | everything in it | stays (within a cent) | a render ahead of time; single hits double or vanish | what has a pitch and no sharp hits |

</div>

Underneath there is one idea, the one of chapter 18 and of chapter 19: **what is counted in beats is stored in
ticks**. A note was a tick from the start. A loop's edges became ticks in chapter 19. Here a clip's start became a
tick, and then every sample of its audio got one, through a few warp markers.

## Where each piece is

<div align="center">

| Idea | Step | In the engine | In the app |
|---|---|---|---|
| warp markers, the speed | 1 | `Warp.h`: `WarpMap`, `markersForLoop()`, `markersForTake()` | - |
| the tick is the truth | 2 | `Warp.h`: `placeUnder()`, `movedTo()` | `Session::setClips()`, `setTempoMap()` |
| reading between samples | 3 | `Warp.h`: `readSinc()` | - |
| WSOLA | 4 | `Warp.h`: `stretchAlong()` | - |
| a render ahead of time | 5 | `Warp.h`: `renderFollowClip()`; `FollowRenderer.h` | `Session::pushClips()`, `pollRenders()`, `finishRenders()` |
| the clip carries the data | 6 | `Clip.h`: `Clip::follow`; `Warp.h`: `ClipFollow`, `withFollowMode()`, `cutToSongRange()` | the clip menu, project formats 8 and 9 |

</div>

The audio thread got nothing new. `Track.h`, `Transport.h`, `Mixer.h` and `LatencyCompensator.h` are chapter 19's
files, byte for byte (`scripts/check_copies.py` says so on every push), and `Clip.h` differs by one member that
the audio thread does not read. All of this chapter runs before the audio thread sees a clip.

## What the numbers were

- A take over a tempo map of jumps needs a marker at each tempo point and is then **exact**; a ramp from 60 to
  120 BPM over four bars needs 46 markers to stay within 1 ms (Step 1).
- A clip whose tick is found again from its rounded sample at every tempo change is 15 samples off after 1000
  changes; one that keeps its tick is 0 off (Step 2).
- A position counted up from the speed is 31 ms off after ten minutes of ramps; computed from the song's own
  sample it is 0.003 samples off (Step 3).
- Reading a 5 kHz tone between samples leaves an error 35 dB below it with a line, 49 dB with a cubic, 100 dB
  with a windowed sinc; reading 15 kHz twice as fast leaves an alias at -3 dB, or at -78 dB with a sinc that
  narrows (Step 3).
- WSOLA keeps a tone within 0.72 cents and 0.01 dB, with 31 to 40 dB between the tone and the rest; of 8 clicks
  all are heard twice at half the speed and 4 are lost at twice the speed (Step 4).
- Of 23 requests for a render that overtake each other, 4 are begun (Step 5).

Every one of these is printed by a check you can run.

## What was left out

Said in the steps where it belongs, and once more here in one place:

- **No transients.** WSOLA does not know what a drum hit is. A stretcher that does - or one that works on the
  spectrum - is a different and much longer program.
- **A piece fits to half a sample.** That is where the stretched tone's 31 to 40 dB come from; fitting to a
  fraction of a sample would take them further down.
- **No beat detection, no markers to drag.** A clip is taken to be in time as it plays when following is turned
  on. Audio that was *not* played in time cannot be made to be.
- **Recording does not make a take follow.** You turn it on afterwards, before the tempo changes.
- **Not while you listen.** A change of tempo is heard at once as a placeholder - the clip on its beat at its
  start, at the old speed - and correctly when the render is done.
- **The renders are a cache.** They are not in the project file, and an export waits for them.
- **A seek still clicks.** Chapter 14's engine does not fade over a jump of the playhead, and a tempo change
  while the song plays is such a jump for every audio clip (Step 2).
- **One meter.** As in chapter 19, the time signature does not change in a song.

## What you can do now

- say where audio belongs in music, and compute what to play and when from two maps that do not know each other;
- choose between following with a start, a tape and a stretch, and say what each will do to a given sound;
- read audio at a fraction of a sample at three prices, and stop aliasing when reading faster;
- build a time-stretcher that keeps pitch, and measure what it damages;
- move heavy work off the audio thread into a render that is handed over, with generations for what goes stale;
- add a feature to an app whose model is plain lists, by letting the list's element carry it.

## Next

The [exercises](../exercises/README.md) take up some of this. The steps leave obvious ones: the fewest markers for a ramp (chapter 19's
Exercise 5 applied to Step 1), a fit to a fraction of a sample for Step 4, a fade over a tempo change, and
markers made from the map a take was recorded over.
