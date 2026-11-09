# Step 4 - Polyphony

**English** | [繁體中文](step-04-polyphony.zh-TW.md)

[Overview](../README.md) | Previous: [Step 3 - One voice](step-03-one-voice.md) | Next: Step 5 - A keyboard (coming)

## Goal

Play chords: a fixed number of voices, an allocator that decides which voice plays which note - and what to
do when there is none left - plus the sustain pedal, pitch bend and "all notes off".

## Design

### A fixed number of voices

Every voice costs CPU for as long as it sounds, and the audio thread may not allocate, so a synth has a fixed
pool: 8, 16, 64 voices. The notes come and go; the voices stay. Which voice plays which note is the
*voice allocator*'s decision, and it has three cases:

```text
1. the pitch already sounds on a voice    play it again on that voice
2. a voice is idle                        that one
3. none is idle                           steal one
```

**The same note again.** A piano key struck while its string still rings strikes the same string; it does not
add a second one. Two voices on one pitch would sound like one note too loud, and since their phases differ,
with a slow phasing smear. So a note that still sounds - held, sustained or releasing - is played again on its
own voice. Step 3's `Voice::start()` already keeps the phase of a sounding voice, so this is seamless.

**Stealing.** With every voice busy, a new note takes one. Which? The one that will be missed least:

1. a released note (its key is up, the pedal does not hold it, it is fading) - the one released longest ago,
   because it has faded the furthest;
2. if every note is held, the one that started longest ago: in a chord the oldest note has decayed the
   most, and a melody moves on from it.

Other synths make other choices - the quietest voice, the lowest note never - and the bookkeeping is the same:
each voice has a slot with its key state and two time stamps, `started` and `released`, from a counter that
only counts up.

A note off belongs to a voice by its pitch. A stolen note's note off therefore finds no voice: the voice now
has another pitch, and it must not end the new note.

### A steal must not click

A voice that jumps from one note to the other mid-waveform clicks. So `Voice::steal()` fades the old note out
over 2 ms (96 samples at 48 kHz) and starts the new note, from rest, in the sample after that. The new note is
2 ms late; nobody hears 2 ms, and everyone hears a click. A note off that arrives during those 2 ms cancels the
new note. "All notes off" does too.

### The sustain pedal and pitch bend

The sustain pedal is controller 64: 64 or more is down, less is up. While it is down, a note off does not
release the note, it marks it *sustained*. When the pedal comes up, every sustained note is released - but a
note whose key is still held keeps sounding. A key struck again under the pedal plays its note again on the
same voice, and is held by the key now, not by the pedal.

Pitch bend arrives as Step 1's 14-bit value, -8192 to 8191. This synth bends +-2 semitones at the extremes,
`value / 8192 x 2`, so full down is exactly two semitones and full up just short of it. Every sounding voice
follows at once, and a new note starts bent.

Controller 123, "all notes off", releases every voice and forgets the pedal: the panic button of Step 2's
hanging notes, as a message.

### Sample-accurate, as always

`Synth::render()` clears the buffer and lets every voice add itself, cut at every event exactly as Step 1's
`renderBlock()`. The check plays 800 random events - notes, velocity-0 note offs, the pedal, bends, panics -
over 26 seconds on 6 voices, in blocks of 1, 64, 512 and random sizes, and requires the same output to the
bit, never two held voices on one pitch, and silence at the end.

## Your turn

Write the three functions in `steps/step04-polyphony/starter/step04.cpp`:

1. `Synth::allocate()`: the same pitch, an idle voice, the released voice released first, the oldest note.
2. `Synth::handle()`: note on (start or steal), note off (release or sustain), the pedal, all notes off,
   pitch bend.
3. `Synth::render()`: clear, then render every voice up to each event, handle it, and on.

When it works, the output ends like this:

```text
Step 4: polyphony
  stealing a voice of a 4-note chord: the largest step around it is 0.650, 1.075 before it
  a random performance of 800 events on 6 voices, 25.9 s: the same in blocks of 1, 64, 512 and random: yes
[PASS] l18_step04 (53 checks passed)
```

## What you learned

- A synth has a fixed pool of voices; the allocator maps notes to them.
- A note played again goes to its own voice.
- Steal the voice that will be missed least: released before held, oldest first.
- A stolen voice fades out in 2 ms before its new note: late, but no click.
- A note off finds its voice by pitch, so a stolen note's note off cannot end another note.
- The sustain pedal turns note offs into "sustained"; its release lets them go, but not the keys still held.
