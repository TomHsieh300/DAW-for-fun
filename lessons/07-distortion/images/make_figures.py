"""Figures for Chapter 07 (distortion): transfer curves and sample & hold.

Run:  python lessons/07-distortion/images/make_figures.py

Curves use the same equations as dsp/include/dawlab/dsp/Distortion.h (hard clip, foldback,
fuzz with dead zone d = 0.1 and ceilings +1.0 / -0.6) and tanh from Chapter 06.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)) + "/../../../docs/tools")

import numpy as np  # noqa: E402
import plotstyle as ps  # noqa: E402

C = ps.COLORS


def hard_clip(u):
    return np.clip(u, -1.0, 1.0)


def foldback(u):
    t = np.mod(u + 1.0, 4.0)
    return np.where(t < 2.0, t - 1.0, 3.0 - t)


def fuzz(u, d=0.1, pos=1.0, neg=0.6):
    a = np.abs(u)
    v = a - d
    return np.where(a <= d, 0.0, np.where(u > 0, np.minimum(v, pos), -np.minimum(v, neg)))


def transfer_axes(ax, xlim, ylim, xticks, yticks):
    ax.set_xlim(*xlim)
    ax.set_ylim(*ylim)
    ax.set_xticks(xticks)
    ax.set_yticks(yticks)
    ax.set_xlabel("Input u (after drive)")
    ax.set_ylabel("Output y")
    ax.axhline(0, color="#666666", lw=0.8)
    ax.axvline(0, color="#666666", lw=0.8)


def fig_tanh_vs_hardclip():
    u = np.linspace(-3, 3, 1201)
    fig, ax = ps.figure(6.0, 3.6)
    ax.plot(u, np.tanh(u), color=C[0], label="tanh (soft knee)")
    ax.plot(u, hard_clip(u), color=C[1], label="hard clip (hard knee)")
    transfer_axes(ax, (-3, 3), (-1.25, 1.25), [-3, -2, -1, 0, 1, 2, 3], [-1, -0.5, 0, 0.5, 1])
    ax.set_title("Transfer curves: soft knee vs hard knee")
    ax.legend(loc="lower right")
    ps.save(fig, "transfer-tanh-vs-hardclip.svg")


def fig_foldback():
    u = np.linspace(-1, 7, 3201)
    fig, ax = ps.figure(6.4, 3.2)
    ax.plot(u, foldback(u), color=C[0], label="foldback")
    ax.plot(u, hard_clip(u), color=C[1], lw=1.2, ls="--", label="hard clip (for comparison)")
    transfer_axes(ax, (-1, 7), (-1.25, 1.25), list(range(-1, 8)), [-1, -0.5, 0, 0.5, 1])
    ax.set_title("Foldback: triangle wave of period 4 in u")
    ax.legend(loc="lower right")
    ps.save(fig, "transfer-foldback.svg")


def fig_fuzz():
    u = np.linspace(-1.5, 1.5, 3001)
    fig, ax = ps.figure(6.0, 3.4)
    ax.plot(u, fuzz(u), color=C[0], label="fuzz (d = 0.1, ceilings +1.0 / -0.6)")
    ax.axvspan(-0.1, 0.1, color=C[4], alpha=0.25, lw=0, label="dead zone |u| <= d")
    transfer_axes(ax, (-1.5, 1.5), (-1.0, 1.25), [-1.5, -1, -0.5, 0, 0.5, 1, 1.5],
                  [-0.6, 0, 0.5, 1.0])
    ax.set_title("Fuzz transfer curve: dead zone and asymmetric ceilings")
    ax.legend(loc="lower right")
    ps.save(fig, "transfer-fuzz.svg")


def fig_sample_hold():
    n = np.arange(0, 17)
    D = 4
    x = np.sin(2 * np.pi * n / 16.0)
    y = x[(n // D) * D]
    fig, ax = ps.figure(6.4, 3.4)
    nn = np.linspace(0, 16, 800)
    ax.plot(nn, np.sin(2 * np.pi * nn / 16.0), color="#999999", lw=1.0, label="underlying sine")
    ax.stem(n, x, linefmt=C[0], markerfmt="o", basefmt=" ", label="x[n]: original samples")
    ax.step(n, y, where="post", color=C[1], lw=2.0, label="y[n]: sample & hold, D = 4")
    ax.plot(n[::D], x[::D], "s", color=C[1], ms=7, label="samples that are kept")
    ax.set_xticks(n)
    ax.set_xlabel("Sample index n")
    ax.set_ylabel("Amplitude")
    ax.set_ylim(-1.35, 2.0)
    ax.axhline(0, color="#666666", lw=0.8)
    ax.set_title("Sample & hold: keep one sample in D, hold it for the rest")
    ax.legend(loc="upper right", ncol=2, fontsize=8)
    ps.save(fig, "sample-and-hold.svg")


if __name__ == "__main__":
    fig_tanh_vs_hardclip()
    fig_foldback()
    fig_fuzz()
    fig_sample_hold()
