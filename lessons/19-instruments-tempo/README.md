# Chapter 19: Instruments and tempo - a tempo map, instrument latency, and instruments in the app

**English** | [繁體中文](README.zh-TW.md)

> **Status: in progress.** Steps 1-4 of 6.

DawLab 18 (chapter 18) plays notes at one tempo, on its own synth, and its engine already lets another instrument
take the synth's place - but only its exercises use that. This chapter finishes what chapter 18 left: a tempo that
changes, by jumps and by ramps, with the notes following it; instruments whose latency is compensated; and the
app's side of it - a tempo lane, MIDI files with their tempo changes, a settings panel for the synth that undo and
the project file know, and VST3 instruments on tracks.

## How to use this chapter

Like chapters 15 to 18, every step is a small, self-contained snapshot in `steps/stepNN-*/`: you write the
step's functions in `starter/`, and the self-check tells you when they work. See
[steps/README.md](steps/README.md).

```powershell
.\scripts\build.ps1 -Only 19-instruments-tempo          # no JUCE needed
.\build\bin\Debug\l19_step01_starter.exe
```

On Linux / macOS: `./scripts/build.sh --only 19-instruments-tempo`, then run `./build/bin/l19_step01_starter`.

## 1. Goals

After this chapter you can

- convert between ticks and time on a map of tempo jumps and ramps, exactly and both ways;
- play a song whose tempo changes inside a block, through loops and seeks;
- compensate the latency of an instrument as chapter 14 compensates an effect's;
- give the app what makes these usable: a tempo lane, MIDI files with tempo changes, instrument settings that
  undo and save, and plugin instruments.

## 2. The steps

<div align="center">

| Step | Document | What it adds | Status |
|---|---|---|---|
| 1 | [A tempo map](docs/step-01-tempo-map.md) | points of jumps and ramps, a ramp's time as an integral and its closed inverse, `log1p` and `expm1`, the first tick of a sample | done |
| 2 | [A transport on a tempo map](docs/step-02-transport.md) | segments that carry their ticks, a loop in ticks with exact edges, a new map while it plays keeps the musical place, a metronome; what moves with the tempo and what does not | done |
| 3 | [Instrument latency](docs/step-03-latency.md) | an instrument's latency in its track's, chapter 14's compensation, what a key played now costs, low-latency monitoring, recording that does not move | done |
| 4 | [The engine](docs/step-04-engine.md) | a copy of chapter 18's engine: a tempo map passed between threads in a triple buffer, segments with ticks, a MIDI player that keeps notes through tempo changes, instrument latency in the plan, low-latency monitoring | done |
| 5 | The app: tempo | a copy of chapter 18's app: a tempo lane, undo, the project file, MIDI files with tempo changes | planned |
| 6 | The app: instruments | a synth panel that undo and the project file know, VST3 instruments on tracks | planned |

</div>

## 3. Where this chapter starts

DawLab 18's `Transport` has one tempo, and `sampleOfTick()` is a multiplication. Its engine has the hook of
chapter 18's Step 7, "Added after the chapter": an `Instrument` in the synth's place, and the synth's settings
changed while it plays. Chapter 18's Exercises 4, 3, 8, 9 and 12 built the first versions of a tempo map, of
MIDI files, of a synth panel and of a plugin instrument; this chapter builds them into DawLab.

## 4. Next

Start with [Step 1 - A tempo map](docs/step-01-tempo-map.md).
