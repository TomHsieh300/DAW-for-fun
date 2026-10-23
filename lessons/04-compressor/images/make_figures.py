"""Figures for chapter 04 (compressor).

Run:  python lessons/04-compressor/images/make_figures.py

Curves are computed from the same formulas as dsp/include/dawlab/dsp/Compressor.h
(computeOutputDb) and EnvelopeFollower.h (AttackReleaseFilter), with the same parameters
as examples/transfer_curve.cpp and examples/step_response.cpp.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "docs", "tools"))

import numpy as np  # noqa: E402
import plotstyle as ps  # noqa: E402

FS = 48000.0


# ---- Static curves (Compressor::computeOutputDb and the expander of example 1) ----------

def compressor_out_db(x, threshold, ratio, knee):
    x = np.asarray(x, dtype=float)
    over = x - threshold
    hard = np.where(over > 0.0, threshold + over / ratio, x)
    if knee <= 0.0:
        return hard
    t = over + 0.5 * knee
    soft = x + (1.0 / ratio - 1.0) * t * t / (2.0 * knee)
    return np.where(2.0 * over <= -knee, x, np.where(2.0 * over >= knee, threshold + over / ratio, soft))


def expander_out_db(x, threshold, ratio):
    x = np.asarray(x, dtype=float)
    return np.where(x >= threshold, x, threshold + (x - threshold) * ratio)


def fig_transfer_curve():
    t, r, w = -20.0, 4.0, 10.0
    fig, (ax, zoom) = ps.figure(8.4, 4.2, ncols=2)
    for a, lo, hi in ((ax, -60.0, 0.0), (zoom, -30.0, -10.0)):
        x = np.linspace(lo, hi, 601)
        a.plot(x, x, color="#888888", linestyle=":", label="1:1 (no compression)")
        a.plot(x, compressor_out_db(x, t, r, 0.0), color=ps.COLORS[0], label="hard knee")
        a.plot(x, compressor_out_db(x, t, r, w), color=ps.COLORS[1], linestyle="--", label="soft knee, W = 10 dB")
        a.axvspan(t - w / 2, t + w / 2, color=ps.COLORS[4], alpha=0.15)
        a.axvline(t, color="#888888", linewidth=0.8)
        a.set_xlim(lo, hi)
        a.set_ylim(lo, hi if hi == 0.0 else -8)
        a.set_xlabel("Input level (dB)")
        a.set_ylabel("Output level (dB)")
    ax.set_ylim(-60, 0)
    ax.set_aspect("equal")
    ax.set_title("T = -20 dB, R = 4:1")
    ax.legend(loc="upper left")
    ax.text(t - 1, -57, "threshold", ha="right", fontsize=9)
    ax.annotate("slope 1/4 above the knee", xy=(-6, compressor_out_db(-6.0, t, r, 0.0)),
                xytext=(-48, -34), fontsize=9, arrowprops=dict(arrowstyle="->", color="#555555"))
    zoom.set_ylim(-30, -10)
    zoom.set_aspect("equal")
    zoom.set_title("Zoom on the knee (shaded: -25 to -15 dB)")
    zoom.plot([t], [compressor_out_db(t, t, r, w)], "o", color=ps.COLORS[1], markersize=4)
    zoom.annotate("-20.94 dB at the threshold\n(soft knee)", xy=(t, compressor_out_db(t, t, r, w)),
                  xytext=(-29, -13), fontsize=9, arrowprops=dict(arrowstyle="->", color="#555555"))
    ps.save(fig, "transfer-curve-knee.svg")


def fig_expander_gate():
    x = np.linspace(-60.0, 0.0, 601)
    fig, ax = ps.figure(5.2, 4.6)
    ax.plot(x, x, color="#888888", linestyle=":", label="1:1 (no processing)")
    ax.plot(x, compressor_out_db(x, -20.0, 4.0, 0.0), color=ps.COLORS[0], label="compressor (T = -20 dB, R = 4)")
    ax.plot(x, expander_out_db(x, -40.0, 2.0), color=ps.COLORS[2], label="expander (T = -40 dB, R = 2)")
    ax.plot(x, np.maximum(-100.0, expander_out_db(x, -40.0, 20.0)), color=ps.COLORS[3], linestyle="--",
            label="gate (T = -40 dB, R = 20)")
    ax.set_xlim(-60, 0)
    ax.set_ylim(-100, 0)
    ax.set_xlabel("Input level (dB)")
    ax.set_ylabel("Output level (dB)")
    ax.set_title("Compressor vs expander vs gate")
    ax.legend(loc="lower right")
    ps.save(fig, "transfer-curve-expander-gate.svg")


# ---- Release curves (example 2, part B) ------------------------------------------------

def static_gr_db(level_db, threshold=-20.0, ratio=4.0):
    return level_db - compressor_out_db(level_db, threshold, ratio, 0.0)


def gain_to_db(g):
    g = np.asarray(g, dtype=float)
    return np.where(g > 0.0, np.maximum(-100.0, 20.0 * np.log10(np.maximum(g, 1e-30))), -100.0)


def smooth(target, mode, att_ms=10.0, rel_ms=100.0):
    aa = np.exp(-1.0 / (att_ms * 0.001 * FS))
    ar = np.exp(-1.0 / (rel_ms * 0.001 * FS))
    y = 0.0
    hold = 0.0
    out = np.empty(len(target))
    for i, x in enumerate(target):
        if mode == "branching":
            a = aa if x > y else ar
            y = x + a * (y - x)
        else:
            hold = max(x, x + ar * (hold - x))
            y = hold + aa * (y - hold)
        out[i] = y
    return out


def recovered_gr(inp, out):
    """GR recovered from the peak ratio over the most recent 1 ms, as in the C++ example."""
    win = 48
    n = len(inp)
    pad_i = np.concatenate([np.zeros(win - 1), np.abs(inp)])
    pad_o = np.concatenate([np.zeros(win - 1), np.abs(out)])
    view = np.lib.stride_tricks.sliding_window_view
    px = view(pad_i, win).max(axis=1)
    py = view(pad_o, win).max(axis=1)
    ok = (px > 1e-6) & (py > 1e-9)
    gr = np.zeros(n)
    gr[ok] = gain_to_db(px[ok]) - gain_to_db(py[ok])
    return gr


def fig_release_curves():
    seg = int(0.5 * FS)
    n = 3 * seg
    levels = np.array([-30.0, -10.0, -30.0])
    amp = 10.0 ** (levels[np.arange(n) // seg] / 20.0)
    x = amp * np.sin(2.0 * np.pi * 1000.0 * np.arange(n) / FS)
    level_db = gain_to_db(np.abs(x))
    target = static_gr_db(level_db)

    grs = {}
    for name, mode in (("B1", "decoupled"), ("B2", "branching")):
        g = smooth(target, mode)
        y = x * 10.0 ** (-g / 20.0)
        grs[name] = recovered_gr(x, y)

    # B4: linear domain (EnvelopeFollower, Peak, Decoupled) then the static curve
    env = smooth(np.abs(x), "decoupled")
    grs["B4"] = static_gr_db(gain_to_db(env))

    rel = 2 * seg
    t_ms = np.arange(0, int(0.49 * FS) + 1) / FS * 1000.0
    ref = 0.368 * grs["B1"][rel - 1]

    fig, ax = ps.figure(6.4, 3.6)
    ax.plot(t_ms, grs["B1"][rel:rel + len(t_ms)], color=ps.COLORS[0], label="B1 decoupled, log domain (Compressor default)")
    ax.plot(t_ms, grs["B2"][rel:rel + len(t_ms)], color=ps.COLORS[1], linestyle="--", label="B2 branching, log domain")
    ax.plot(t_ms, grs["B4"][rel:rel + len(t_ms)], color=ps.COLORS[2], linestyle="-.", label="B4 linear domain")
    ax.axhline(ref, color="#555555", linewidth=0.9, linestyle=":")
    ax.annotate(f"36.8% of B1 steady state ({ref:.2f} dB)", xy=(490, ref), xytext=(488, ref + 0.15),
                ha="right", fontsize=8.5, color="#333333")
    ax.set_xlim(0, 490)
    ax.set_ylim(0, 8)
    ax.set_xlabel("Time after returning to the quiet segment (ms)")
    ax.set_ylabel("Gain reduction (dB)")
    ax.set_title("Release curves, T = -20 dB, R = 4:1, attack 10 ms, release 100 ms")
    ax.legend(loc="upper right")
    ps.save(fig, "release-curves.svg")


if __name__ == "__main__":
    fig_transfer_curve()
    fig_expander_gate()
    fig_release_curves()
