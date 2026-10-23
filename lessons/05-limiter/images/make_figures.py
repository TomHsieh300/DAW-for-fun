"""Figures for Chapter 05 (Limiter). Run: python lessons/05-limiter/images/make_figures.py

Every curve is computed from the same formulas as the chapter's README and
dsp/include/dawlab/dsp/Limiter.h (static curve, window minimum + moving average,
a fs/4 sine sampled at a 45 degree phase offset).
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)) + "/../../../docs/tools")

import numpy as np  # noqa: E402
import plotstyle  # noqa: E402
from plotstyle import COLORS  # noqa: E402


def static_curve_db(x_db, threshold_db, ratio):
    """Hard-knee static curve (section 2.1). ratio = inf is the limiter."""
    x_db = np.asarray(x_db, dtype=float)
    over = np.maximum(x_db - threshold_db, 0.0)
    return np.where(x_db <= threshold_db, x_db, threshold_db + over / ratio)


def fig_transfer_curve():
    T = -20.0
    x = np.linspace(-60.0, 0.0, 601)
    fig, ax = plotstyle.figure(5.4, 4.0)
    ax.plot(x, x, color="#888888", linestyle=":", label="1:1 (no processing)")
    ax.plot(x, static_curve_db(x, T, 4.0), color=COLORS[1], label="Compressor 4:1")
    ax.plot(x, static_curve_db(x, T, np.inf), color=COLORS[0], label="Limiter (ratio inf) = ceiling")
    ax.axvline(T, color="#555555", linestyle="--", linewidth=0.9)
    ax.annotate("T = -20 dB", xy=(T, -58), xytext=(T + 1, -58), fontsize=9, color="#555555")
    ax.set_xlim(-60, 0)
    ax.set_ylim(-60, 0)
    ax.set_aspect("equal")
    ax.set_xlabel("Input (dB)")
    ax.set_ylabel("Output (dB)")
    ax.set_title("Static curve: compressor 4:1 vs limiter")
    ax.legend(loc="upper left")
    plotstyle.save(fig, "limiter-static-curve.svg")


def sliding_min(a, window):
    out = np.empty_like(a)
    for n in range(len(a)):
        out[n] = a[max(0, n - window + 1):n + 1].min()
    return out


def fig_gain_smoothing():
    L = 10
    g = 0.5
    n = np.arange(-L - 3, 3 * L + 1)  # n0 = 0, with extra history before
    gr = np.where(n < 0, 1.0, g)
    # The window minimum has length L+1 and the average has length L+1 (section 2.4).
    m = sliding_min(gr, L + 1)
    s = np.empty_like(m)
    for i in range(len(m)):
        lo = max(0, i - L)
        seg = m[lo:i + 1]
        # samples before the plotted range are 1 (gain unity)
        pad = L + 1 - len(seg)
        s[i] = (seg.sum() + pad * 1.0) / (L + 1)
    fig, ax = plotstyle.figure(6.4, 3.6)
    ax.step(n, gr, where="post", color=COLORS[4], linewidth=5, alpha=0.45, label="gr[n]  (required gain after release)")
    ax.step(n, m, where="post", color=COLORS[1], linestyle="--", label="m[n]  (window minimum, length L+1)")
    ax.step(n, s, where="post", color=COLORS[0], label="s[n]  (moving average, length L+1)")
    ax.axvline(0, color="#555555", linestyle=":", linewidth=1)
    ax.axvline(L, color="#555555", linestyle=":", linewidth=1)
    ax.annotate("gr drops to g", xy=(0, g), xytext=(-L - 2, 0.65), fontsize=9,
                arrowprops={"arrowstyle": "->", "color": "#555555"})
    ax.annotate("x[n0] leaves the delay line\n(s[n0+L] = g exactly)", xy=(L, g),
                xytext=(L + 1, 0.72), fontsize=9, arrowprops={"arrowstyle": "->", "color": "#555555"})
    ax.set_xticks([-L, 0, L, 2 * L, 3 * L])
    ax.set_xticklabels(["n0-L", "n0", "n0+L", "n0+2L", "n0+3L"])
    ax.set_xlim(-L - 3, 3 * L)
    ax.set_ylim(0.1, 1.08)
    ax.set_yticks([g, 1.0])
    ax.set_yticklabels(["g", "1"])
    ax.set_xlabel("Sample index (L = 10)")
    ax.set_ylabel("Linear gain")
    ax.set_title("The gain ramp: min, then average")
    ax.legend(loc="lower right", fontsize=8)
    plotstyle.save(fig, "limiter-gain-smoothing.svg")


def fig_inter_sample_peak():
    A = 1.0
    t = np.linspace(0.0, 8.0, 1601)  # time in samples
    wave = A * np.sin(2 * np.pi * 0.25 * t + np.pi / 4)  # f = fs/4, phase 45 degrees
    k = np.arange(0, 9)
    samples = A * np.sin(2 * np.pi * 0.25 * k + np.pi / 4)
    fig, ax = plotstyle.figure(6.4, 3.4)
    ax.plot(t, wave, color=COLORS[0], label="True waveform (peak = A)")
    ax.vlines(k, 0, samples, color="#999999", linewidth=0.8)
    ax.plot(k, samples, "o", color=COLORS[1], label="Sampling points (all at +/-0.707 A)")
    ax.axhline(A, color=COLORS[0], linestyle=":", linewidth=1)
    ax.axhline(samples.max(), color=COLORS[1], linestyle="--", linewidth=1)
    ax.axhline(-samples.max(), color=COLORS[1], linestyle="--", linewidth=1)
    ax.text(8.1, A, "A", va="center", fontsize=9, color=COLORS[0])
    ax.text(8.1, samples.max(), "0.707 A", va="center", fontsize=9, color=COLORS[1])
    ax.set_xlim(0, 8)
    ax.set_ylim(-1.9, 1.3)
    ax.set_xlabel("Time (samples)")
    ax.set_ylabel("Amplitude (A = 1)")
    ax.set_title("Inter-sample peak: sample peak 0.707 A is 3.01 dB below the true peak")
    ax.legend(loc="lower center", fontsize=8)
    plotstyle.save(fig, "limiter-inter-sample-peak.svg")


if __name__ == "__main__":
    fig_transfer_curve()
    fig_gain_smoothing()
    fig_inter_sample_peak()
