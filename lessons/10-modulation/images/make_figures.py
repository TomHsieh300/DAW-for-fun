"""Figures for Chapter 10 (modulation). Run: python lessons/10-modulation/images/make_figures.py"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "docs", "tools"))

import numpy as np  # noqa: E402
import plotstyle as ps  # noqa: E402


def lfo_value(shape, p):
    """Same waveform functions as Lfo::shapeValue (dsp/include/dawlab/dsp/Lfo.h), p in [0, 1)."""
    if shape == "Sine":
        return np.sin(2 * np.pi * p)
    if shape == "Triangle":
        return np.where(p < 0.25, 4 * p, np.where(p < 0.75, 2 - 4 * p, 4 * p - 4))
    if shape == "Saw":
        return 2 * p - 1
    if shape == "Square":
        return np.where(p < 0.5, 1.0, -1.0)
    raise ValueError(shape)


def lfo_shapes():
    names = ["Sine", "Triangle", "Saw", "Square", "Sample & Hold"]
    fig, axes = ps.figure(9.0, 2.2, ncols=5, sharey=True)
    n_cycles = 2
    t = np.linspace(0.0, n_cycles, 4000, endpoint=False)  # time in LFO cycles
    p = t % 1.0
    rng = np.random.RandomState(7)  # illustrative random values, one per cycle
    held = rng.uniform(-1, 1, n_cycles)
    for ax, name, color in zip(axes, names, ps.COLORS):
        if name == "Sample & Hold":
            y = held[np.floor(t).astype(int)]
        else:
            y = lfo_value(name, p)
        ax.plot(t, y, color=color)
        ax.set_title(name)
        ax.set_xlim(0, n_cycles)
        ax.set_ylim(-1.25, 1.25)
        ax.set_xticks(range(n_cycles + 1))
        ax.set_xlabel("Cycles")
    axes[0].set_ylabel("m")
    axes[0].set_yticks([-1, 0, 1])
    ps.save(fig, "lfo-shapes.svg")


def flanger_comb():
    f = np.linspace(0.0, 2000.0, 2000)
    fig, axes = ps.figure(7.0, 3.0, ncols=2, sharey=True)
    for ax, d_ms, color in zip(axes, (2.0, 1.0), ps.COLORS):
        d = d_ms / 1000.0
        mag = np.abs(np.cos(np.pi * f * d))  # |H| of y = (x[n] + x[n-D]) / 2
        ax.plot(f, mag, color=color)
        ax.set_title(f"d = {d_ms:g} ms")
        ax.set_xlim(0, 2000)
        ax.set_ylim(0, 1.1)
        ax.set_xlabel("Frequency (Hz)")
        notches = [(2 * k + 1) / (2 * d) for k in range(8) if (2 * k + 1) / (2 * d) <= 2000]
        ax.set_xticks(notches[:4] if d_ms == 2.0 else notches[:2])
        ax.plot(notches, np.zeros(len(notches)), "o", color=color, markersize=4)
    axes[0].set_ylabel("|H|")
    ps.save(fig, "flanger-comb-delay.svg")


if __name__ == "__main__":
    lfo_shapes()
    flanger_comb()
