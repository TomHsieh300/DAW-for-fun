"""Figures for Chapter 06 (saturation). Run: python lessons/06-saturation/images/make_figures.py"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "docs", "tools"))

import numpy as np  # noqa: E402
import plotstyle  # noqa: E402
from plotstyle import COLORS, figure, save  # noqa: E402


def sine_through_curve():
    """A large sine through y = tanh(x): input, transfer curve, output with flattened tops."""
    amp = 3.0
    t = np.linspace(0.0, 2.0, 801)          # two periods
    x_t = amp * np.sin(2 * np.pi * t)
    y_t = np.tanh(x_t)
    x = np.linspace(-4.0, 4.0, 801)

    fig, axes = figure(9.6, 3.0, ncols=3)
    ax_in, ax_f, ax_out = axes

    ax_in.plot(t, x_t, color=COLORS[0])
    ax_in.set_title("Input: sine, A = 3")
    ax_in.set_xlabel("Time (periods)")
    ax_in.set_ylabel("x")
    ax_in.set_ylim(-4, 4)

    ax_f.plot(x, x, color="#888888", linestyle="--", linewidth=1.0, label="y = x")
    ax_f.plot(x, np.tanh(x), color=COLORS[1], label="y = tanh(x)")
    ax_f.axvspan(-0.5, 0.5, color=COLORS[2], alpha=0.15, label="straight part")
    ax_f.set_xlim(-4, 4)
    ax_f.set_ylim(-1.5, 1.5)
    ax_f.set_title("Transfer curve f(x)")
    ax_f.set_xlabel("Input x")
    ax_f.set_ylabel("Output y")
    ax_f.legend(loc="upper left")

    ax_out.plot(t, y_t, color=COLORS[1])
    ax_out.set_title("Output: tops flattened")
    ax_out.set_xlabel("Time (periods)")
    ax_out.set_ylabel("y")
    ax_out.set_ylim(-1.5, 1.5)

    save(fig, "saturation-sine-through-curve.svg")


def aliasing_fold():
    """f_alias = | ((f + fs/2) mod fs) - fs/2 | for fs = 48 kHz, with the 7 kHz tanh harmonics marked."""
    fs = 48000.0
    f = np.linspace(0.0, 2.0 * fs, 4001)
    alias = np.abs(np.mod(f + fs / 2.0, fs) - fs / 2.0)

    fig, ax = figure(6.4, 3.6)
    ax.plot(f / 1000.0, alias / 1000.0, color=COLORS[0], label="alias frequency")
    ax.axvline(fs / 2000.0, color="#888888", linestyle="--", linewidth=1.0)
    ax.axvline(fs / 1000.0, color="#888888", linestyle="--", linewidth=1.0)
    ax.axvline(1.5 * fs / 1000.0, color="#888888", linestyle="--", linewidth=1.0)
    for k in (3, 5, 7, 9, 11, 13):
        fk = 7000.0 * k
        fa = abs(((fk + fs / 2.0) % fs) - fs / 2.0)
        ax.plot(fk / 1000.0, fa / 1000.0, "o", color=COLORS[1], zorder=3,
                label="harmonics of 7 kHz (3, 5, ... 13)" if k == 3 else None)
        ax.annotate(f"{k}", (fk / 1000.0, fa / 1000.0), textcoords="offset points", xytext=(5, 5), fontsize=8)
    ax.set_xlim(0, 96)
    ax.set_ylim(0, 32)
    ax.set_xticks([0, 24, 48, 72, 96])
    ax.set_xticklabels(["0", "fs/2 = 24", "fs = 48", "3fs/2 = 72", "2fs = 96"])
    ax.set_xlabel("True frequency (kHz)")
    ax.set_ylabel("Frequency after sampling (kHz)")
    ax.set_title("Aliasing: folding the frequency axis at fs/2 (fs = 48 kHz)")
    ax.legend(loc="upper center", ncol=2)
    save(fig, "aliasing-fold.svg")


if __name__ == "__main__":
    sine_through_curve()
    aliasing_fold()
