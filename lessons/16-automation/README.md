# Chapter 16: Automation - parameters that change over time

**English** | [繁體中文](README.zh-TW.md)

> **Status: in progress.** Step 1 is finished; Steps 2-6 are planned and listed below so you can
> see where the chapter is going.

DawLab 15 (chapter 15) plays a mix whose faders and knobs stay where the user left them. Real
music moves: a fader rises into the chorus, a filter opens over eight bars, a delay is switched on
for one phrase. A DAW records such movements as **automation**: a curve along the timeline for each
parameter that the audio thread follows while it plays.

Automation touches every layer that chapter 15 built. It is data in the project file and in the
undo history, a real-time problem on the audio thread (sample-accurate and without allocating),
and a question of who wins when the user moves a fader that a curve is moving too. This chapter
takes those one at a time, in small snapshots first, then in a copy of chapter 15's engine and
application.

## How to use this chapter

Like chapter 15, every step is a small, self-contained snapshot in `steps/stepNN-*/`: you write the
step's functions in `starter/`, and the self-check tells you when they work. See
[steps/README.md](steps/README.md).

```powershell
.\scripts\build.ps1 -Only 16-automation          # no JUCE needed
.\build\bin\Debug\l16_step01_starter.exe
```

On Linux / macOS: `./scripts/build.sh --only 16-automation`, then `./build/bin/l16_step01_starter`.

## 1. Goals

After this chapter you can

- store automation as points on the timeline and read it back as one value or as a block;
- let the audio thread follow a curve sample-accurately, without allocating, across seeks and loops;
- decide who wins when the user moves an automated control (Read, Touch, Latch, Write) and record
  the movement as points;
- name what a curve belongs to so that it survives deleted tracks, moved effects, the project file
  and undo;
- draw and edit automation in the application.

## 2. The steps

<div align="center">

| Step | Document | Adds | Status |
|---|---|---|---|
| 1 | [The automation curve](docs/step-01-curve.md) | points with a position and a normalized value, Linear and Hold segments, jumps, `valueAt()` and a one-search `render()` | done |
| 2 | Sample-accurate playback | the curve on the audio thread: immutable snapshots swapped like clip lists, a cursor that survives seeks and loops | planned |
| 3 | Automation and the user | Read / Touch / Latch / Write; recording a fader move as points and thinning them | planned |
| 4 | What a curve belongs to | targets by track id and parameter; removed effects; the project file and undo | planned |
| 5 | The engine with automation | a copy of chapter 15's engine with automation lanes; ThreadSanitizer | planned |
| 6 | The app with automation | lanes under the tracks, drawing and dragging points, the mode switch | planned |

</div>

## 3. Where this chapter starts

Chapter 15 ends with an engine whose parameters are atomics that the UI writes and the audio thread
reads once per block (chapter 14, Step 4). That is enough for a knob in the user's hand. It is not
enough for automation: a value per block is a staircase at 48 kHz / 512 samples - ten steps a
second - and the audio thread, not the UI, has to know where the curve is at every sample.

## 4. Next

Start with [Step 1 - The automation curve](docs/step-01-curve.md).
