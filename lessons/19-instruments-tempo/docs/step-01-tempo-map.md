# Step 1 - A tempo map

**English** | [繁體中文](step-01-tempo-map.zh-TW.md)

[Overview](../README.md) | Next: [Step 2 - A transport on a tempo map](step-02-transport.md)

## Goal

Turn ticks into time when the tempo changes: jumps from one tempo to another, and ramps - an accelerando or a
ritardando, where the tempo moves in a straight line from one point to the next. Exactly, both ways, and fast
enough for every block.

## Design

### Points, jumps and ramps

A tempo map is a list of points, each a tick and a tempo. From a point the tempo either holds until the next point
(a jump at the next one), or goes in a straight line to the next point's tempo (a ramp). The last point always holds.
Before tick 0 - a count-in - the first point's tempo holds.

```text
BPM
140 |      ______                 one map: 120 to bar 2, a jump to 140,
    |     /      |                a ramp down to 100 over bar 3,
120 |____/       |_____           100 holds
100 |            '-----.____
    +----+----+----+----+----> bars
```

The map is in **seconds**, not samples. The same song at 44.1 and at 48 kHz has the same map; a sample is
$`\mathrm{round}(seconds \cdot rate)`$, as late as possible.

### The time of a tick

A tick at tempo $`B`$ lasts $`60 / (960 B)`$ seconds. On a jump that is all: $`n`$ ticks into a segment are
$`n \cdot 60 / (960 B)`$ s. On a ramp the tempo changes under every tick, so the time is an integral. With the tempo
$`B(t) = B_0 + k (t - a)`$ from the point at tick $`a`$, $`k = (B_1 - B_0) / (c - a)`$ to the next point at $`c`$:

```math
s(t) = \frac{60}{960} \int_a^t \frac{d\tau}{B_0 + k(\tau - a)} = \frac{60}{960} \cdot \frac{\ln\left(1 + k (t - a) / B_0\right)}{k}
```

A ramp from 60 to 120 BPM over four bars takes $`16 \ln 2 = 11.09`$ s - between the 16 s it would take at 60 and the
8 s at 120, and not their mean of 12: the slow part lasts longer.

The inverse is closed too - the formula solved for $`t`$:

```math
t = a + \frac{B_0}{k}\left(e^{k \, s \cdot 960 / 60} - 1\right)
```

Where every point starts is the sum of the segments before it, kept unrounded (chapter 18's Exercise 4 showed what
rounding each one costs). A tick is then a binary search for its segment and one formula.

### Digits

$`\ln(1 + x)`$ and $`e^x - 1`$ for a small $`x`$ lose their digits when written that way: $`1 + x`$ rounds $`x`$ to
the precision of 1. A ramp that hardly ramps - 120 to 120.000001 BPM over a hundred thousand bars - has
$`x \approx 10^{-11}`$ at bar 1000, and `std::log((B0 + k t) / B0) / k` is already off there by more than the self-check allows.
`std::log1p` and `std::expm1` compute exactly these two functions without the loss.

### The first tick of a sample

As in chapter 18, the audio thread asks which ticks fall into a block $`[s_0, s_1)`$: they are
$`[\mathrm{first}(s_0), \mathrm{first}(s_1))`$, where $`\mathrm{first}(s)`$ is the smallest tick whose sample is at
least $`s`$. `tickAt()` gives the exact, fractional tick of a sample, and `ceil()` the whole tick at or after it.
Rounding to samples may still pull the tick before up to the same sample, so `firstTickAtOrAfter()` steps back
while it does - and forward while its tick's sample is still before $`s`$. In chapter 18 the multiplication made
that forward step impossible; with a logarithm and an exponential, two functions that are inverses only on paper,
it is a guard against the last digit. The self-check never needs it, and keeps it anyway.

## Your turn

Write `prepare()`, `bpmAt()`, `secondsAt()`, `tickAt()`, `sampleOfTick()` and `firstTickAtOrAfter()` in
`steps/step01-tempo-map/starter/step01.cpp`. Build and run:

```powershell
.\scripts\build.ps1 -Only 19-instruments-tempo
.\build\bin\Debug\l19_step01_starter.exe
```

The self-check compares with a reference that knows nothing of logarithms - Simpson's rule over $`1/B(t)`$ in long
double - for 150 random maps of jumps and ramps, goes from seconds back to ticks, and plays a song that ramps up and
down every bar in blocks of random sizes:

```text
Step 1: a tempo map
  120, 60 and 240 BPM a bar each: bars at 0, 2, 6 and 7 s
  60 to 120 BPM over 4 bars: 11.090355 s (16 ln 2 = 11.090355), 90 BPM half way
  a ramp of 0.000001 BPM over 100 000 bars, at bar 1000: 1999.999999916667 s (reference 1999.999999916666)
  150 random maps of jumps and ramps: 0 times off the reference (worst 2.0e-11 s), 0 not back from seconds,
    0 samples whose first tick is wrong, 0 steps backwards
  a song that ramps up and down every bar, 1536 events in random blocks: 1536 found, 0 outside their block
[PASS] l19_step01 (24 checks passed)
```

## What you learned

- A tempo map is points of jumps and ramps; a ramp's time is an integral with a closed form, and so is its inverse.
- Keeping the map in seconds makes it independent of the sample rate.
- `std::log1p` and `std::expm1` keep the digits that $`\ln(1+x)`$ and $`e^x-1`$ lose for small $`x`$.
- Two formulas that are inverses on paper are not in floating point: an exact inverse checks its result.
