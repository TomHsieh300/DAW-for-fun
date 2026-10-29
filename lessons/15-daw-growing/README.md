# Chapter 15: Growing the DAW - tracks you can add, projects you can save, edits you can undo

**English** | [繁體中文](README.zh-TW.md)

> **Status: in progress.** Step 1 is finished; Steps 2-6 are planned and listed below so you can
> see where the chapter is going.

Chapter 14 built an engine that is deliberately *static*: 8 tracks, 2 FX buses, every container
allocated once in `prepare()`, so the audio thread never has to grow anything. That made the real-time
rules easy to keep. A DAW you actually use needs more: tracks you can add and delete, a project you
can save and open again, and an undo button.

This chapter adds those features one step at a time, **without giving up the real-time rules of
chapter 14**. Each step first explains what breaks when a fixed size becomes a variable one, then
fixes it. Chapter 14 itself stays as it is: it is the starting point, and comparing the two chapters
is part of the lesson.

## How to use this chapter

Like chapter 14's build-along route, every step is a small, self-contained snapshot in
`steps/stepNN-*/`: you write the step's functions in `starter/`, and the self-check tells you when
they work. See [steps/README.md](steps/README.md).

```powershell
.\scripts\build.ps1 -Only 15-daw-growing          # no JUCE needed
.\build\bin\Debug\l15_step01_starter.exe
```

On Linux / macOS: `./scripts/build.sh --only 15-daw-growing`, then `./build/bin/l15_step01_starter`.

## 1. Goals

After this chapter you can

- explain why an index cannot name a track once tracks can be deleted or moved, and replace it
  with a stable id;
- let the user add and delete tracks while the audio thread keeps running, still without
  allocating or locking on it;
- decide what limits the number of tracks (memory or CPU) and show the user when the machine is
  running out;
- save a project to a file and open it again, also in a later version of the program;
- implement undo / redo on top of the same commands the engine already uses.

## 2. The steps

| Step | Document | Adds | Status |
|---|---|---|---|
| 1 | [Stable track IDs](docs/step-01-track-ids.md) | `TrackId`, a track list that hands out ids that are never reused; why indices break | done |
| 2 | A pool of tracks | `kMaxTracks` pre-allocated slots, add / remove track commands, the id -> slot map; checked with ThreadSanitizer | planned |
| 3 | A CPU budget | the callback load and xrun count; why the real limit is CPU time, not a track count | planned |
| 4 | Saving and opening projects | a versioned project file built on track ids; opening it at another sample rate | planned |
| 5 | Undo and redo | commands that can be inverted, an undo history, what should (not) be undoable | planned |
| 6 | The app | a scrollable arrangement and mixer, add / delete track in the GUI, the File menu | planned |

## 3. Where this chapter starts

Everything in chapter 14 that refers to a track uses an index between 0 and 7:
`std::array<Track, kNumTracks>` in the `Mixer`, `ChannelRef::track(i)` in the commands,
`Session::clips_`, the lanes of the `ArrangementView`. `kNumTracks` appears in 34 files of chapter 14. Step 1
starts with the root of the problem - how to name a track at all - before any of these containers
change.

## 4. Next

Start with [Step 1 - Stable track IDs](docs/step-01-track-ids.md).
