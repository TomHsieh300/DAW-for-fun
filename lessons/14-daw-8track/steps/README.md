# Build-along snapshots, Steps 1-10

**English** | [繁體中文](README.zh-TW.md)

> [Chapter 14](../README.md) | How the two routes work: [How to use this chapter](../README.md#how-to-use-this-chapter)

Each folder is a small, complete engine that contains exactly what exists after its step. You
write the step's functions in `starter/`, the self-check tells you when they work.

<div align="center">

| Step | Folder | You write | Run |
|---|---|---|---|
| 1 | `step01-skeleton` | `Engine::processBlock`, `Engine::processChunk` | `l14_step01_starter` |
| 2 | `step02-transport` | `Transport::barsBeatsToSamples`, `samplesToBarsBeats`, `advance` | `l14_step02_starter` |
| 3 | `step03-clips` | `clipFadeAt`, `renderClip` | `l14_step03_starter` |
| 4 | `step04-mixer` | `ChannelStrip::computeStripGains`, `Mixer::isTrackAudible`, `Mixer::processTrack` | `l14_step04_starter` |
| 5 | `step05-insert-effects` | `ParameterInfo::clamp` / `toNormalized` / `fromNormalized`, `EffectBase::setParameter` / `applyPendingParameters`, `InsertChain::process` | `l14_step05_starter` |
| 6 | `step06-sends-buses` | `ChannelStrip::addSend`, `Mixer::processTrack` with send taps | `l14_step06_starter` |
| 7 | `step07-commands` | `SpscQueue::push` / `pop`, `Engine::applyCommands` | `l14_step07_starter` |
| 8 | `step08-pdc` | `LatencyCompensator::process`, `Mixer::updateLatencies` | `l14_step08_starter` |
| 9 | `step09-offline-render` | `renderOffline` | `l14_step09_starter` |
| 10 | `step10-realtime-safety` | counting `operator new` / `delete`, `audit::renderWithBlockSizes` | `l14_step10_starter` |

</div>

Every folder has the same four files:

- `minidaw.h` - the mini engine so far: earlier steps solved, this step's functions only declared
  (marked `YOUR TURN`).
- `check.h` - the step's self-check (`main()`), included at the end of both programs below. It
  tests only what this step adds.
- `starter/stepNN.cpp` - your code, with TODOs. Builds without warnings and prints `[FAIL]` until
  the step works.
- `solution/stepNN.cpp` - a worked solution; ctest runs it as `l14_stepNN_solution`
  (label `solution`).

Build with `.\scripts\build.ps1 -Only 14-daw-8track` from the repository root (no JUCE needed) and
run the programs from the `build` folder, e.g. `.\bin\Debug\l14_step03_starter.exe`. The "Your
turn" section of each step document (`../docs/step-NN-*.md`) explains the task and shows the
expected output.
