# Build-along snapshots, chapter 20

**English** | [繁體中文](README.zh-TW.md)

> [Chapter 20](../README.md)

Each folder is a small, complete program that contains exactly what exists after its step. You
write the step's functions in `starter/`, the self-check tells you when they work.

<div align="center">

| Step | Folder | You write | Run |
|---|---|---|---|
| 1 | `step01-warp-map` | `WarpMap::sourceAt`, `tickAt`, `sourceAtSample`, `songSecondsOf`, `speedAt`, `markersForLoop`, `markersForTake` | `l20_step01_starter` |
| 2 | `step02-follow-start` | `setFollow`, `startUnder`, `retime`, `moveTo`, `moveToTick`, `endTick`, `gaps`, `jumpInClip` | `l20_step02_starter` |

</div>

Every folder has the same four files:

- `minidaw.h` - the model so far: earlier steps solved, this step's functions only declared
  (marked `YOUR TURN`).
- `check.h` - the step's self-check (`main()`), included at the end of both programs below. It
  tests only what this step adds.
- `starter/stepNN.cpp` - your code, with TODOs. Builds without warnings and prints `[FAIL]` until
  the step works.
- `solution/stepNN.cpp` - a worked solution; ctest runs it as `l20_stepNN_solution`
  (label `solution`).

`tempo_map.h`, next to the folders, is chapter 19's tempo map, solved: every step starts from it.

Build with `.\scripts\build.ps1 -Only 20-audio-tempo` from the repository root (no JUCE needed) and
run the programs from the `build` folder, e.g. `.\bin\Debug\l20_step01_starter.exe`. The "Your
turn" section of each step document (`../docs/step-NN-*.md`) explains the task and shows the
expected output.
