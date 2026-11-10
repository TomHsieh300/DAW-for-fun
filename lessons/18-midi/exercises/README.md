# Chapter 18 - Exercises
**English** | [繁體中文](README.zh-TW.md)

[Back to the chapter](../README.md) | [Solutions](../solutions/README.md)

Difficulty: ★☆☆ easy, ★★☆ medium, ★★★ hard. Implementation exercises have a starter file in
`exercises/starter/` that compiles and runs; until you complete it, its self-check prints `FAIL`
(that is expected). Build target names are given in brackets. None of them needs JUCE:

```powershell
.\scripts\build.ps1 -Only 18-midi
.\build\bin\Debug\l18_ex3_starter.exe
```

**Exercises 1-7** are the chapter's exercise set, about the steps (no JUCE). **Exercises 8-12 are
optional projects** that extend DawLab 18 (they need a JUCE build); they are still to come.

Exercises 2 and 3 build on Step 1's model (`exercises/model.h`: Step 1's `minidaw.h` with its functions
solved). Exercise 4 stands alone. Do the steps first.

---

## Exercise 1 - Why so careful with notes? ★☆☆ (concept)

Answer in a few sentences each.

1. An audio clip lives in samples, a MIDI clip in ticks (Step 1). What would go wrong with notes stored in
   samples when the user changes the tempo? And why does Step 1 compute `sampleOfTick()` from the tick
   every time instead of adding `samplesPerTick` up?
2. At one tick, Step 1 plays note offs before note ons, and Step 2 plays controllers between them. Give a
   case where the other order is audibly wrong, for each of the two rules.
3. After a seek, Step 2 chases the controllers (the pedal, the bend) but not the notes. What would a DAW
   have to do to chase a note correctly - and why might the user not even want it?
4. Step 4 steals a voice with a 2 ms fade, so the new note starts 2 ms late. Why not cut the old note at
   once? Why not drop the new note when all voices are busy?
5. Step 5 can play a keyboard's events as soon as possible or scheduled, a fixed delay after they arrived.
   Scheduled is always later. Why is it still the better default, and when would you choose as soon as
   possible?
6. Recording audio (chapter 17) subtracts the input latency; recording MIDI (Step 6) does not. Why not?
   Which latencies does it subtract, and why those?
7. Step 6's quantize never changes the notes as played; it computes where they play. What would be lost if
   it moved the notes themselves?

## Exercise 2 - MIDI by the numbers ★★☆ (calculation)

Compute by hand (show the formulas), then run [`l18_ex2_solution`] to check.

- (a) A song at 97 BPM, 44.1 kHz, 960 ticks a quarter: how many samples is a tick? At which sample does
  bar 33 (4/4) start? A program adds `samplesPerTick` to a `float` once a tick for an hour: guess how far off
  it ends up, then let the solution tell you.
- (b) A MIDI cable carries 31 250 bits a second, 10 bits a byte. How long does a note on take? A chord of 10
  notes, with running status and without? How many samples at 48 kHz lie between the first note and the
  last?
- (c) Blocks of 512 samples at 48 kHz, an output latency of 512. How much jitter does "as soon as possible"
  add, and how long is Step 5's scheduled delay of two blocks plus the output latency? At 128 samples?
- (d) How long is a tick in samples and milliseconds at 120 BPM and at 200 BPM (48 kHz)? How many samples
  is a 1/16 note at 120 BPM?
- (e) A naive sawtooth on C7 (MIDI note 96) at 48 kHz. Which harmonic is the first above Nyquist, and where
  does it fold to? Which is the first alias below 10 kHz, and how loud is it compared with the fundamental?
- (f) Step 6's quantize on a 1/8 grid (480 ticks) with swing 0.2: how late is every second line, in ticks,
  samples and milliseconds at 120 BPM? Where does strength 0.5 move a note at tick 990? Where does full
  strength with that swing move a note at tick 1430?

## Exercise 3 - Standard MIDI Files ★★★ (implementation)

[`l18_ex3_starter`, file `exercises/starter/ex3_smf.cpp`]

Every DAW reads and writes `.mid` files. Write both directions for format 0 and 1 (the header explains the
format in full):

- `writeVlq()` and `readVlq()`: the variable-length numbers that every delta time and length uses.
- `writeSmf()`: a song - a tempo, a time signature, named tracks of events - into a file of format 1 at 960
  ticks a quarter, with a conductor track and running status. The self-check compares the exact bytes.
- `readSmf()`: a file of format 0 or 1 at any division back into a song in ticks of 960. Running status,
  meta events, SysEx, unknown chunks and a header longer than 6 bytes must all work.

A file is untrusted input. The self-check feeds the reader 23 broken files, every truncation of a good one
and 20 000 randomly damaged ones: it must refuse what it cannot read, with a reason, and never read past the
end. Build it with AddressSanitizer too (`-DDAWLAB_SANITIZE=address,undefined` on Linux), or in Debug with
MSVC, whose checked iterators catch the same mistakes.

## Exercise 4 - A tempo map ★★☆ (implementation)

[`l18_ex4_starter`, file `exercises/starter/ex4_tempo_map.cpp`]

DawLab 18 keeps one tempo. Give it a tempo map - a list of changes, each a tick and a tempo - and write the
two conversions the audio thread needs: `sampleOfTick()` and its exact inverse `firstTickAtOrAfter()`, as in
Step 1, but across changes. `prepare()` computes where each change starts in samples.

The self-check compares with a reference for 300 random maps, requires that every tick of a song with a
ritardando into every beat falls into exactly one block of random size, and finds out whether you rounded
where a change starts: after 2000 changes, that is 480 samples (11 ms) off.
