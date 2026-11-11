# Chapter 20: Audio that follows the tempo - warp markers, varispeed, and a time-stretcher

**English** | [繁體中文](README.zh-TW.md)

> **Status: done.** All 7 steps. Dragging a tempo under a loop still has to be tried by hand: see Step 6, "What you have to try yourself".

In DawLab 19 (chapter 19) the tempo can change, and the notes follow: they are in ticks. An audio clip is in
samples and stays where it is - drag a tempo point, and the recorded guitar is no longer with the drums. Chapter
19's Exercise 6 designed the way out on paper. This chapter builds it: a clip that knows where its audio belongs
in the music, played faster or slower as the tempo asks - as a tape would, or without changing its pitch.

## How to use this chapter

Like chapters 15 to 19, every step is a small, self-contained snapshot in `steps/stepNN-*/`: you write the
step's functions in `starter/`, and the self-check tells you when they work. See
[steps/README.md](steps/README.md).

```powershell
.\scripts\build.ps1 -Only 20-audio-tempo          # no JUCE needed
.\build\bin\Debug\l20_step01_starter.exe
```

On Linux / macOS: `./scripts/build.sh --only 20-audio-tempo`, then run `./build/bin/l20_step01_starter`.

## 1. Goals

After this chapter you can

- say where a clip's audio belongs in the music with warp markers, and make them for a loop and for a take;
- let a clip follow the tempo with its start only, or with everything in it;
- play audio at another speed by resampling, with a position that never drifts;
- change the speed without the pitch with a time-stretcher, and measure what it costs;
- render such audio ahead of time and hand it to an engine that plays.

## 2. The steps

<div align="center">

| Step | Document | What it adds | Status |
|---|---|---|---|
| 1 | [A warp map](docs/step-01-warp-map.md) | markers that tie a sample of the file to a tick of the song, both ways; the speed a tempo asks for; the markers of a loop and of a take, and how many a ramp needs | done |
| 2 | [A clip whose start follows the tempo](docs/step-02-follow-start.md) | a start kept as a tick and derived as a sample, a drag to a sample and a drag to the grid, what the length does, seams that open or overlap, a tempo change while it plays | done |
| 3 | [Varispeed](docs/step-03-varispeed.md) | speed and pitch; the position in the file from the song's sample in every block, exact at knots and on a grid; linear, Hermite and windowed-sinc reading, measured; aliasing when reading faster; clips as ranges of a source that meet at every tempo | done |
| 4 | [A time-stretcher](docs/step-04-stretch.md) | overlap-add in Hann windows, why the pieces do not fit, WSOLA's search for the natural continuation and its score; measured on tones, noise and clicks: what it keeps and what it costs | done |
| 5 | [The engine](docs/step-05-engine.md) | a copy of chapter 19's engine with two new headers: a clip and how it follows, its render to a plain clip, a worker thread that renders ahead of time, generations and cancelling for stale requests, placeholders until a render is ready | done |
| 6 | [The app](docs/step-06-app.md) | a copy of chapter 19's app: the follow data carried by the clip, start and length derived in two places, renders and placeholders through the session, a mode per clip in its menu, moving and cutting, project format 8 | done |
| 7 | [The end of the chapter](docs/step-07-the-end.md) | the four things a clip can do, where each piece is, the numbers the checks measured, and what was left out | done |

</div>

## 3. Where this chapter starts

DawLab 19's tempo map turns ticks into seconds and back, exactly, through jumps and ramps
(`steps/tempo_map.h` has it, solved). Its transport gives every block its ticks. Its audio clips have a start and
a length in samples and are played sample for sample.

## 4. Exercises

None yet. [Step 7](docs/step-07-the-end.md) lists what the steps leave open.

## 5. Next

Start with [Step 1 - A warp map](docs/step-01-warp-map.md).
