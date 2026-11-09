# Chapter 17: Recording - from the audio input to a clip on the timeline

**English** | [繁體中文](README.zh-TW.md)

> **Status: done.** All 7 steps. Real recording, latency and monitoring still have to be tried with a real device: see Step 7, "What you have to try yourself".

DawLab 16 (chapter 16) can play, mix and automate audio that already exists, but its audio callback
ignores the input. A DAW also has to record, and recording puts together everything the earlier
chapters kept apart: a real-time thread that may not wait, a disk that may stall, and a timeline where
every sample has a place, although the sound reached the computer late.

[Exercises 6 and 9 of chapter 14](../14-daw-8track/exercises/README.md) sketch the basics. This chapter
starts over without JUCE, and then goes on to what a real recording needs: staying in time when the
disk stalls, a take that survives a crash, punching in and out, and hearing yourself while you play.

## How to use this chapter

Like chapters 15 and 16, every step is a small, self-contained snapshot in `steps/stepNN-*/`: you write
the step's functions in `starter/`, and the self-check tells you when they work. See
[steps/README.md](steps/README.md).

```powershell
.\scripts\build.ps1 -Only 17-recording          # no JUCE needed
.\build\bin\Debug\l17_step01_starter.exe
```

On Linux / macOS: `./scripts/build.sh --only 17-recording`, then run `./build/bin/l17_step01_starter`.

## 1. Goals

After this chapter you can

- move audio off the audio thread without waiting or allocating, and keep a recording in time when
  the disk cannot keep up;
- work out where on the timeline a take belongs, from the latencies of the device and the mix;
- write a take to disk so that a crash or a full disk does not lose it;
- punch in and out inside a block, and keep every pass of a loop as a take;
- monitor the input, and explain why what you hear is not what is recorded.

## 2. The steps

<div align="center">

| Step | Document | What it adds | Status |
|---|---|---|---|
| 1 | [From the audio thread to the disk thread](docs/step-01-input-ring.md) | a ring of counters that only grow, blocks that go in whole or not at all, gaps that keep the time; ThreadSanitizer | done |
| 2 | [Where the take belongs](docs/step-02-placement.md) | `P - (L_in + L_out + L_mix)` stamped by the audio thread, a simulated loop-back, measuring the round trip, a take both threads can end | done |
| 3 | [The take on disk](docs/step-03-take-on-disk.md) | a WAV that grows while recording and survives a crash or a full disk, one way to finish a take | done |
| 4 | [Punch and loop](docs/step-04-punch-and-loop.md) | one stream in one file, and takes worked out from it: exact punch points, every pass of a loop | done |
| 5 | [Input monitoring](docs/step-05-input-monitoring.md) | what is heard and what is recorded, latency through inserts, a guard against feedback | done |
| 6 | [The engine with recording](docs/step-06-engine.md) | a copy of chapter 16's engine: `processBlock` takes the input, tracks can be armed, a loop-back through the real mixer; ThreadSanitizer | done |
| 7 | [The app with recording](docs/step-07-app.md) | a copy of chapter 16's app: record button, arming, inputs only when asked, the take while recording, takes in the place of old ones, takes of a loop, undo, saving, recovery after a crash | done |

</div>

## 3. Where this chapter starts

`AudioHost::audioDeviceIOCallbackWithContext` in chapter 16 receives the device's input and throws it
away, `AudioHost::openDefaultDevice` asks for no input channels by default, and
`Engine::processBlock` only has an output. Nothing else in the engine knows that a sound can come from
outside. Step 1 does not touch any of this yet: it starts with the first thing a recording has to do.

## 4. Exercises

See [exercises/README.md](exercises/README.md), with the answers in [solutions/README.md](solutions/README.md).
Seven exercises, none needs JUCE: why recording is so careful (concept), recording by the numbers
(calculation), writing only what a take can use, measuring the latency and knowing when not to trust it,
telling the user what is wrong with the input, comping the best parts of a loop's passes, and two devices
with two clocks (design). Four optional projects extend DawLab 17 (they need `-Juce`): a "Measure Latency"
button, warnings about a silent, quiet or clipped take, comping on the track, and pre-roll.

## 5. Next

Start with [Step 1 - From the audio thread to the disk thread](docs/step-01-input-ring.md). After the chapter, continue
with [chapter 18 - MIDI](../18-midi/README.md).
