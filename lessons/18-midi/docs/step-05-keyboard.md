# Step 5 - A keyboard

**English** | [繁體中文](step-05-keyboard.zh-TW.md)

[Overview](../README.md) | Previous: [Step 4 - Polyphony](step-04-polyphony.md) | Next: [Step 6 - Recording MIDI](step-06-recording.md)

## Goal

Play the synth from a keyboard: turn the driver's bytes into messages, hand them from the MIDI thread to the
audio thread without either of them waiting, and decide at which sample each one is played - trading latency
against jitter.

## Design

### Bytes, not messages

A MIDI connection - a 5-pin cable, or USB pretending to be one - carries a stream of bytes. Step 1's
three-byte messages are what they add up to, but three habits of real devices get in the way:

- **Running status.** A keyboard playing a chord sends `90 3C 64 40 64 43 64`: one status byte, then three
  notes. A data byte without a status uses the last status. Together with velocity-0 note offs (Step 1),
  this saves a third of the bandwidth of a 31250-baud cable, and every keyboard does it.
- **Real-time bytes anywhere.** Clock (`F8`), start, stop are one byte each and may arrive *between the
  bytes of another message*: `90 3C F8 64` is a note on, with a clock tick in the middle. They change
  nothing about the message around them.
- **System exclusive.** `F0 ... F7` is a manufacturer's message of any length - a patch dump can be
  kilobytes. Its data bytes are not notes.

`MidiParser::feed()` takes one byte at a time and says when a channel message is complete. It keeps the
running status, a count of data bytes collected, and whether a SysEx is open. A status byte - any but real
time - starts over: it ends an unfinished message and even a SysEx that never got its `F7`. System common
messages (`F1`-`F6`) end the running status, as the MIDI specification says, and data bytes with no status
to belong to are skipped and counted.

### From the MIDI thread to the audio thread

The driver calls back on its own thread when bytes arrive. That thread must not wait for the audio thread
(a key press would be delayed), and the audio thread must not wait at all. So it is chapter 17's ring again,
for messages: a single-producer, single-consumer FIFO of fixed size with two counters that only grow,
`written` and `read`.

- `push()` writes the item into slot `written & (N - 1)`, *then* stores `written + 1` with **release**.
- `pop()` loads `written` with **acquire**, copies the item, *then* stores `read + 1` with **release**.

The release/acquire pairs are the whole design: the consumer never sees a `written` that is ahead of the item
it describes, and the producer never reuses a slot the consumer is still copying. On x86 the processor
orders these stores anyway, so a version with `relaxed` passes every functional test on a PC. ThreadSanitizer
models the C++ memory model rather than the processor, and it reports both mistakes - which is why the CI runs
this step under TSan, and why the check runs a real producer and consumer: 100000 messages that must arrive
complete and in order, and a stalled consumer whose lost messages must all be counted.

When the FIFO is full, `push()` drops the message and counts it. A keyboard cannot be told to wait.

### Every message has a time

The driver stamps each message with the time it arrived, on a clock that the audio callback can read too
(JUCE: `Time::getMillisecondCounterHiRes()`). The audio callback renders block `k` ahead of time, and its first
sample will be heard at a known time, `blockTime`. Between the two clocks is the choice of this step.

**As soon as possible.** Take whatever is in the FIFO and play it at offset 0 of the block being rendered.
A key pressed just before the callback runs is heard after one block and the output latency; a key pressed
just after it waits for the next callback. The delay depends on *when in the block period* the key went
down: **jitter**.

**Scheduled.** Play each message a fixed `delay` after it arrived, at

```text
offset = floor((time + delay - blockTime) x sampleRate)
```

A message due after the block's end stays in the FIFO for the next block (`peek()` looks without taking).
The delay has to cover the worst case: a message is seen at most two block periods before its block is heard,
plus the output latency. With that, every note is heard exactly `delay` after its key, to the sample. A
message that arrives too late for its due time (a delay set too short, a callback that ran very late) is
played at offset 0 and counted.

The check plays 20 seconds of key presses against a simulated device whose callbacks are not punctual
(up to half a block late), and measures, for every note, how long after its key it is heard:

<div align="center">

| 512 samples at 48 kHz | heard after the key | jitter |
|---|---|---|
| as soon as possible | 11.0 to 25.6 ms | 14.6 ms |
| scheduled (delay 26.3 ms) | 26.3 ms | 0.02 ms (one sample) |

</div>

Fifteen milliseconds of jitter is audible on a drum pad or a fast passage; most players prefer a constant
26 ms, which they compensate for without noticing, to a delay that wanders. With 64-sample blocks the
scheduled delay drops below 8 ms. Most DAWs schedule; some offer a "low latency" setting that does not.

## Your turn

Write the five functions in `steps/step05-keyboard/starter/step05.cpp`:

1. `MidiParser::feed()`: real time, system, status, data - in that order.
2. `MidiFifo::push()`: full or not; the item, then `written` with release.
3. `MidiFifo::pop()`: empty or not (acquire); the copy, then `read` with release.
4. `MidiFifo::peek()`: as `pop()`, without taking.
5. `drain()`: offset 0 for everything, or the scheduled offset, the block's end, the late ones.

When it works, the output ends like this (the numbers of the stalled consumer change from run to run):

```text
Step 5: a keyboard
  two threads: 100000 of 100000 messages in order; a stalled consumer: 7708 received, 8292 dropped, 0 out of order
  512-sample blocks, as soon as possible: heard 11.0 to 25.6 ms after the key (14.6 ms of jitter)
  512-sample blocks, scheduled:           heard 26.3 to 26.3 ms after the key (0.02 ms of jitter)
[PASS] l18_step05 (31 checks passed)
```

## What you learned

- A MIDI stream has running status, real-time bytes inside messages, and SysEx of any length.
- A status byte starts over; system common ends the running status.
- An SPSC FIFO needs release on the stores that publish and acquire on the loads that read; x86 hides the
  mistake, ThreadSanitizer does not.
- "As soon as possible" is early but jittery; scheduling with a fixed delay is later but exact.
- The delay must cover the worst case, or messages come late.
