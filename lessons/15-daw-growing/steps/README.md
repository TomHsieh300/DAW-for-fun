# Build-along snapshots, chapter 15

**English** | [繁體中文](README.zh-TW.md)

> [Chapter 15](../README.md)

Each folder is a small, complete program that contains exactly what exists after its step. You
write the step's functions in `starter/`, the self-check tells you when they work.

| Step | Folder | You write | Run |
|---|---|---|---|
| 1 | `step01-track-ids` | `TrackList::add`, `remove`, `indexOf`, `move` | `l15_step01_starter` |

Every folder has the same four files:

- `minidaw.h` - the model / engine so far: earlier steps solved, this step's functions only
  declared (marked `YOUR TURN`).
- `check.h` - the step's self-check (`main()`), included at the end of both programs below. It
  tests only what this step adds.
- `starter/stepNN.cpp` - your code, with TODOs. Builds without warnings and prints `[FAIL]` until
  the step works.
- `solution/stepNN.cpp` - a worked solution; ctest runs it as `l15_stepNN_solution`
  (label `solution`).

Build with `.\scripts\build.ps1 -Only 15-daw-growing` from the repository root (no JUCE needed) and
run the programs from the `build` folder, e.g. `.\bin\Debug\l15_step01_starter.exe`. The "Your
turn" section of each step document (`../docs/step-NN-*.md`) explains the task and shows the
expected output.
