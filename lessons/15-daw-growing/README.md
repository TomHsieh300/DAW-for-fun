# Chapter 15: Growing the DAW - tracks you can add, projects you can save, edits you can undo

**English** | [繁體中文](README.zh-TW.md)

> **Status: in progress.** Steps 1-7 are finished; Steps 8 and 9 are planned and listed below so
> you can see where the chapter is going.

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

From Step 2 on, the checks run a real audio thread next to the UI thread. Build them with
ThreadSanitizer as well (GCC or Clang; on Windows in WSL) - see
[Step 2](docs/step-02-track-pool.md#checking-with-threadsanitizer):

```bash
./scripts/build.sh --only 15-daw-growing --sanitize thread --build-dir build-tsan
```

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

<div align="center">

| Step | Document | Adds | Status |
|---|---|---|---|
| 1 | [Stable track IDs](docs/step-01-track-ids.md) | `TrackId`, a track list that hands out ids that are never reused; why indices break | done |
| 2 | [A pool of tracks](docs/step-02-track-pool.md) | `kMaxTracks` pre-allocated slots, add / remove track commands, the id -> slot map; checked with ThreadSanitizer | done |
| 3 | [A CPU budget](docs/step-03-cpu-budget.md) | the callback load and xrun count; why the real limit is CPU time, not a track count | done |
| 4 | [Saving and opening projects](docs/step-04-project-files.md) | a versioned project file built on track ids; opening it at another sample rate | done |
| 5 | [Undo and redo](docs/step-05-undo.md) | commands that can be inverted, an undo history, what should (not) be undoable | done |
| 6 | [The engine with a track pool](docs/step-06-engine-pool.md) | chapter 14's engine copied and changed: `kMaxTracks` slots with ids, add / remove track commands, the CPU meter; chapter 14's tests ported, pool tests, ThreadSanitizer | done |
| 7 | [The app with growing tracks](docs/step-07-app-tracks.md) | chapter 14's app copied and moved onto track ids: a scrollable arrangement and mixer, add / delete track, the CPU warning | done |
| 8 | Saving and opening in the app | the File menu: the whole project (inserts, sends, VST3 state, audio) in an extended Step 4 format | planned |
| 9 | Undo and redo in the app | Step 5's history behind Edit > Undo / Redo, `*` in the window title | planned |

</div>

## 3. Where this chapter starts

Everything in chapter 14 that refers to a track uses an index between 0 and 7:
`std::array<Track, kNumTracks>` in the `Mixer`, `ChannelRef::track(i)` in the commands,
`Session::clips_`, the lanes of the `ArrangementView`. `kNumTracks` appears in 34 files of chapter 14. Step 1
starts with the root of the problem - how to name a track at all - before any of these containers
change.

## 4. Next

Start with [Step 1 - Stable track IDs](docs/step-01-track-ids.md).
