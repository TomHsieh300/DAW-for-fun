# Chapter 18: MIDI - notes in musical time, an instrument, and a keyboard

**English** | [繁體中文](README.zh-TW.md)

> **Status: in progress.** Steps 1-4 of 8.

DawLab 17 (chapter 17) records and plays audio: sound that already exists, placed in samples. A DAW also
plays *notes* - instructions to an instrument, placed in beats, that become sound only when they are played
- and records them from a keyboard. Notes bring their own problems: a note that is never ended sounds
forever, an instrument has a limited number of voices, a keyboard sends its events from another thread with
its own idea of time, and a recorded performance is often wanted a little tighter than it was played.

## How to use this chapter

Like chapters 15 to 17, every step is a small, self-contained snapshot in `steps/stepNN-*/`: you write the
step's functions in `starter/`, and the self-check tells you when they work. See
[steps/README.md](steps/README.md).

```powershell
.\scripts\build.ps1 -Only 18-midi          # no JUCE needed
.\build\bin\Debug\l18_step01_starter.exe
```

On Linux / macOS: `./scripts/build.sh --only 18-midi`, then run `./build/bin/l18_step01_starter`.

## 1. Goals

After this chapter you can

- represent MIDI messages, keep notes in musical time and play every event at its exact sample;
- play a MIDI clip through loops, seeks and stops without a note left hanging;
- build a polyphonic instrument: oscillators without aliasing, envelopes, voices and voice stealing;
- take a keyboard's events from its thread to the audio thread, and decide between latency and jitter;
- record a performance where it was played, and quantize it without losing what was played.

## 2. The steps

<div align="center">

| Step | Document | What it adds | Status |
|---|---|---|---|
| 1 | [MIDI events and musical time](docs/step-01-midi-time.md) | three-byte messages, notes in ticks, the exact inverse for a block's edges, note offs first, an instrument cut at every event | done |
| 2 | [Playing a MIDI clip](docs/step-02-midi-clips.md) | clips as windows on their notes; a table of what was started, so no note hangs after a stop, seek, wrap, mute or delete; controllers chased after a jump | done |
| 3 | [One voice](docs/step-03-one-voice.md) | a sawtooth with PolyBLEP, its aliases measured; an ADSR that never jumps; a filter, velocity; a voice that adds to the mix | done |
| 4 | [Polyphony](docs/step-04-polyphony.md) | a pool of voices, the same note on its own voice, stealing what is missed least without a click, the sustain pedal, pitch bend, all notes off | done |
| 5 | A keyboard | events from the MIDI thread through a lock-free FIFO, timestamps into sample offsets: latency or jitter | planned |
| 6 | Recording MIDI | placing a performance, loop overdub or replace, quantize that keeps what was played | planned |
| 7 | The engine with instruments | a copy of chapter 17's engine: instrument tracks, MIDI clips through the command queue | planned |
| 8 | The app with instruments | a copy of chapter 17's app: instrument tracks, MIDI clips, a piano roll, a keyboard input, recording, quantize, saving, undo | planned |

</div>

## 3. Where this chapter starts

Nothing in DawLab 17 knows a note. Its tracks play audio clips, its clips are samples, and its `Transport`
counts ticks (960 to a quarter note) only to show bars and beats. Step 1 starts with what a note is.

## 4. Next

Start with [Step 1 - MIDI events and musical time](docs/step-01-midi-time.md).
