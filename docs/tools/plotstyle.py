"""Shared style for the course's SVG figures.

Every chapter that has curves (frequency responses, transfer curves, envelopes, waveforms)
keeps a small script next to its images, lessons/NN-name/images/make_figures.py, which
imports this module and writes SVG files. Regenerate a chapter's figures with:

    python lessons/NN-name/images/make_figures.py

Conventions:
- Labels, legends and titles are English: the same SVG is used by README.md and
  README.zh-TW.md (the surrounding text explains it in each language).
- White background, so the figures stay readable in GitHub's dark mode.
- Text stays text in the SVG (svg.fonttype = "none"), and the output is deterministic
  (no date, fixed hash salt), so regenerating an unchanged figure gives an identical file.
- Curves are computed from the same formulas as the C++ code (or the README's equations),
  never drawn by hand.
"""

from __future__ import annotations

import os
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402

# Colour-blind-friendly palette (Okabe-Ito), used in this order for multiple curves.
COLORS = ["#0072B2", "#D55E00", "#009E73", "#CC79A7", "#E69F00", "#56B4E9", "#000000", "#F0E442"]

plt.rcParams.update({
    "svg.fonttype": "none",
    "svg.hashsalt": "dawlab",
    "font.size": 10,
    # Text stays text in the SVG, so the *viewer's* browser picks the font: list fonts every
    # platform has and end with the generic "sans-serif", or a browser without DejaVu Sans
    # falls back to a serif font. DejaVu Sans (always available to matplotlib) does the layout.
    "font.family": "sans-serif",
    "font.sans-serif": ["DejaVu Sans", "Arial", "Helvetica", "Liberation Sans"],
    "axes.titlesize": 11,
    "axes.labelsize": 10,
    "legend.fontsize": 9,
    "axes.grid": True,
    "grid.alpha": 0.3,
    "grid.linewidth": 0.6,
    "axes.spines.top": False,
    "axes.spines.right": False,
    "axes.prop_cycle": matplotlib.cycler(color=COLORS),
    "lines.linewidth": 1.8,
    "figure.facecolor": "white",
    "axes.facecolor": "white",
    "savefig.facecolor": "white",
})


def figure(width: float = 6.4, height: float = 3.2, ncols: int = 1, nrows: int = 1, **kwargs):
    """A new figure (inches). Returns (fig, ax) or (fig, axes) like plt.subplots."""
    return plt.subplots(nrows, ncols, figsize=(width, height), **kwargs)


def freq_axis(ax, fmin: float = 20.0, fmax: float = 20000.0, label: str = "Frequency (Hz)"):
    """Logarithmic frequency axis with audio-friendly tick labels (20, 50, 100, ... 20k)."""
    ax.set_xscale("log")
    ax.set_xlim(fmin, fmax)
    ticks = [t for t in (10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000)
             if fmin <= t <= fmax]
    ax.set_xticks(ticks)
    ax.set_xticklabels([f"{t // 1000}k" if t >= 1000 else str(t) for t in ticks])
    ax.minorticks_off()
    ax.set_xlabel(label)


def db_axis(ax, lo: float, hi: float, step: float | None = None, label: str = "Gain (dB)"):
    ax.set_ylim(lo, hi)
    if step:
        ax.set_yticks(np.arange(np.ceil(lo / step) * step, hi + 1e-9, step))
    ax.set_ylabel(label)


def log_freqs(fmin: float = 20.0, fmax: float = 20000.0, n: int = 800) -> np.ndarray:
    return np.geomspace(fmin, fmax, n)


def biquad_mag_db(b, a, freqs, fs: float) -> np.ndarray:
    """|H(e^jw)| in dB of a biquad with b = (b0, b1, b2), a = (1, a1, a2) (a0 normalised)."""
    w = 2.0 * np.pi * np.asarray(freqs) / fs
    z1 = np.exp(-1j * w)
    z2 = z1 * z1
    num = b[0] + b[1] * z1 + b[2] * z2
    den = a[0] + a[1] * z1 + a[2] * z2
    return 20.0 * np.log10(np.maximum(np.abs(num / den), 1e-12))


def save(fig, path: str):
    """Writes an SVG next to the calling script (path relative to that script's folder)."""
    base = os.path.dirname(os.path.abspath(sys.argv[0]))
    out = path if os.path.isabs(path) else os.path.join(base, path)
    fig.tight_layout()
    fig.savefig(out, format="svg", metadata={"Date": None})
    plt.close(fig)
    print("wrote", os.path.relpath(out))
