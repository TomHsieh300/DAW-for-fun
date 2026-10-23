"""Figures for chapter 00 (digital audio fundamentals). Run: python lessons/00-basics/images/make_figures.py"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)) + "/../../../docs/tools")

import numpy as np  # noqa: E402
import plotstyle as ps  # noqa: E402
from plotstyle import COLORS  # noqa: E402


def sampling():
    """2.1: a continuous wave x(t) and its samples x[n] = x(nT)."""
    fs = 48000.0
    f0 = 1000.0
    T = 1.0 / fs

    def x(t):
        return 0.8 * np.sin(2 * np.pi * f0 * t) + 0.25 * np.sin(2 * np.pi * 3 * f0 * t + 0.6)

    t_ms = np.linspace(0, 2.0, 2000)          # two periods of 1 kHz
    n = np.arange(0, int(2.0e-3 * fs) + 1)
    fig, (a, b) = ps.figure(7.2, 3.0, ncols=2, sharey=True)
    a.plot(t_ms, x(t_ms * 1e-3), color=COLORS[0])
    a.set_title("Continuous x(t)")
    a.set_xlabel("Time t (ms)")
    a.set_ylabel("Amplitude")
    a.axhline(0, color="black", linewidth=0.8)
    b.stem(n, x(n * T), basefmt="k-")
    b.set_title("Sampled x[n], fs = 48 kHz")
    b.set_xlabel("Sample index n")
    # mark one sampling period T = 1/fs
    y0 = -1.0
    b.annotate("", xy=(4, y0), xytext=(5, y0), arrowprops=dict(arrowstyle="<->", color=COLORS[1]))
    b.text(4.5, y0 - 0.12, "T = 1/fs", ha="center", color=COLORS[1], fontsize=9)
    a.set_ylim(-1.3, 1.1)
    ps.save(fig, "sampling-continuous-vs-sampled.svg")


def spectra():
    """2.1: spectrum of a sine wave (one bar) and of a 110 Hz pluck (fundamental plus harmonics)."""
    fig, (a, b) = ps.figure(7.2, 3.0, ncols=2, sharey=True)
    a.bar([440], [1.0], width=18, color=COLORS[0])
    a.set_xlim(0, 700)
    a.set_title("Sine wave: a single bar")
    a.set_xlabel("Frequency (Hz)")
    a.set_ylabel("Amplitude")
    a.set_xticks([440])
    harm = np.arange(1, 7) * 110.0
    amp = np.array([4, 3, 2, 1, 0.5, 0.25]) / 4.0   # illustrative harmonic make-up
    b.bar(harm, amp, width=18, color=COLORS[0])
    for f, k in zip(harm, range(1, 7)):
        b.text(f, amp[k - 1] + 0.03, f"H{k}", ha="center", fontsize=8)
    b.set_xlim(0, 700)
    b.set_title("Pluck, f0 = 110 Hz: harmonics")
    b.set_xlabel("Frequency (Hz)")
    b.set_xticks(harm)
    a.set_ylim(0, 1.2)
    ps.save(fig, "spectrum-sine-vs-pluck.svg")


def aliasing():
    """2.2: a 30 kHz sine sampled at 48 kHz folds back to |30000 - 48000| = 18 kHz."""
    fs, fin = 48000.0, 30000.0
    falias = abs(fin - fs)
    nyq = fs / 2
    fig, ax = ps.figure(6.4, 3.2)
    ax.vlines([falias], 0, 0.5, color=COLORS[1], linewidth=6)
    ax.vlines([fin], 0, 0.5, color=COLORS[0], linewidth=2, linestyles="dashed")
    ax.axvline(nyq, color="black", linewidth=1.2)
    ax.text(nyq + 300, 0.62, "Nyquist = 24 kHz", fontsize=9)
    ax.text(falias, 0.53, "18 kHz (alias)", ha="center", color=COLORS[1], fontsize=9)
    ax.text(fin, 0.53, "30 kHz (original)", ha="center", color=COLORS[0], fontsize=9)
    ax.annotate("", xy=(falias + 400, 0.25), xytext=(fin - 400, 0.25),
                arrowprops=dict(arrowstyle="->", color="black", linestyle="dotted"))
    ax.text((falias + fin) / 2, 0.27, "fold back", ha="center", fontsize=9)
    ax.set_xlim(0, 36000)
    ax.set_ylim(0, 0.75)
    ax.set_xticks([0, 6000, 12000, 18000, 24000, 30000, 36000])
    ax.set_xticklabels(["0", "6k", "12k", "18k", "24k", "30k", "36k"])
    ax.set_xlabel("Frequency (Hz)")
    ax.set_ylabel("Amplitude")
    ax.set_title("Aliasing: 30 kHz sampled at 48 kHz appears at 18 kHz")
    ps.save(fig, "aliasing-fold-back.svg")


def clipping():
    """2.7: a 1 kHz sine of amplitude 1.5 hard-clipped to +-1."""
    fs, f0, amp = 48000.0, 1000.0, 1.5
    n = np.arange(0, int(2 * fs / f0) + 1)
    t_ms = n / fs * 1e3
    x = amp * np.sin(2 * np.pi * f0 * n / fs)
    y = np.clip(x, -1.0, 1.0)
    tt = np.linspace(0, 2.0, 1000)
    fig, ax = ps.figure(6.4, 3.2)
    ax.plot(tt, amp * np.sin(2 * np.pi * f0 * tt * 1e-3), color=COLORS[5], linestyle="--",
            label="input (amplitude 1.5)")
    ax.plot(t_ms, y, color=COLORS[0], marker="o", markersize=3, label="after hard clip to +-1")
    ax.axhline(1.0, color="black", linewidth=0.8)
    ax.axhline(-1.0, color="black", linewidth=0.8)
    ax.set_xlabel("Time (ms)")
    ax.set_ylabel("Amplitude")
    ax.set_yticks([-1.5, -1.0, 0.0, 1.0, 1.5])
    ax.set_title("Clipping: the part beyond +-1.0 is flattened")
    ax.legend(loc="lower right")
    ps.save(fig, "clipping-sine.svg")


if __name__ == "__main__":
    sampling()
    spectra()
    aliasing()
    clipping()
