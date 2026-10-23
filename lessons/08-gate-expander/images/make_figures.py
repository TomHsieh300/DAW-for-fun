"""Figures for Chapter 08 (gate / expander): run  python lessons/08-gate-expander/images/make_figures.py

- transfer-curves-dynamics-family.svg : static curves of compressor / limiter / expander / gate (README 2.1)
- gate-chattering-single-threshold.svg : gate with one threshold chatters on a rippling envelope (README 2.4.1)
- gate-hysteresis-schmitt.svg          : the same envelope with hysteresis: it closes only once (README 2.4.1)

Curves use the same equations as dsp/include/dawlab/dsp/{Compressor,Expander,Gate,EnvelopeFollower}.h.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "docs", "tools"))

import numpy as np  # noqa: E402
import plotstyle as ps  # noqa: E402


# ---- static gain computers (hard knee), as in Compressor.h / Expander.h / expander_vs_gate_curve.cpp ----
def compressor_out_db(x, thr, ratio):
    over = x - thr
    return np.where(over > 0.0, thr + over / ratio, x)


def expander_out_db(x, thr, ratio, range_db):
    y = np.where(x >= thr, x, thr + ratio * (x - thr))
    return np.maximum(y, x + range_db)  # the range limits the maximum attenuation


def gate_out_db(x, thr, range_db):
    return np.where(x >= thr, x, x + range_db)


def transfer_curves():
    x = np.linspace(-60.0, 0.0, 601)
    fig, ax = ps.figure(6.0, 5.6)
    ax.plot(x, x, color="#999999", lw=4.0, alpha=0.45, label="1:1 (no processing)", zorder=1)
    ax.plot(x, compressor_out_db(x, -20.0, 4.0), color=ps.COLORS[0], label="Compressor 4:1, T = -20", zorder=3)
    ax.plot(x, compressor_out_db(x, -10.0, 1.0e9), color=ps.COLORS[1], label="Limiter inf:1, T = -10", zorder=3)
    ax.plot(x, expander_out_db(x, -40.0, 2.0, -40.0), color=ps.COLORS[2],
            label="Downward expander 1:2, T = -40", zorder=3)
    # The gate: 1:1 down to T, then a vertical drop of |range| = 80 dB (far below the bottom of the chart).
    gx = x[x >= -40.0]
    ax.plot(gx, gate_out_db(gx, -40.0, -80.0), color=ps.COLORS[3], label="Gate, T = -40, range -80", zorder=3)
    ax.annotate("", xy=(-40.0, -60.0), xytext=(-40.0, -40.0),
                arrowprops=dict(arrowstyle="-|>", color=ps.COLORS[3], lw=1.8, ls="--"), zorder=3)
    ax.text(-39.0, -57.0, "gate: drops 80 dB below T\n(off the chart)", color=ps.COLORS[3], fontsize=8.5, va="center")
    for t, c in ((-20.0, ps.COLORS[0]), (-10.0, ps.COLORS[1]), (-40.0, ps.COLORS[2])):
        ax.plot([t], [t], marker="o", ms=4, color=c, zorder=4)
    ax.set_xlim(-60, 0)
    ax.set_ylim(-60, 0)
    ax.set_xticks(range(-60, 1, 10))
    ax.set_yticks(range(-60, 1, 10))
    ax.set_aspect("equal")
    ax.set_xlabel("Input level (dB)")
    ax.set_ylabel("Output level (dB)")
    ax.set_title("Transfer curves of the dynamics-processor family")
    ax.legend(loc="upper left", fontsize=8.5)
    ps.save(fig, "transfer-curves-dynamics-family.svg")


# ---- envelope of a slowly decaying low tone, from the gate's detector (peak, attack 0.1 ms, release 20 ms) ----
FS = 48000.0


def detector_envelope_db():
    n = int(1.0 * FS)
    t = np.arange(n) / FS
    f = 50.0
    level_db = -20.0 - 25.0 * t                         # decays 25 dB per second
    x = 10.0 ** (level_db / 20.0) * np.sin(2.0 * np.pi * f * t)
    a_att = np.exp(-1.0 / (0.0001 * FS))
    a_rel = np.exp(-1.0 / (0.020 * FS))
    e = np.zeros(n)
    y = 0.0
    for i in range(n):                                   # EnvelopeFollower, Branching peak detector
        v = abs(x[i])
        a = a_att if v > y else a_rel
        y = a * y + (1.0 - a) * v
        e[i] = y
    return t, 20.0 * np.log10(np.maximum(e, 1e-5))


def gate_open_flags(env_db, t_open, t_close):
    is_open = False
    out = np.zeros(len(env_db), dtype=bool)
    for i, v in enumerate(env_db):
        if not is_open and v >= t_open:
            is_open = True
        elif is_open and v < t_close:
            is_open = False
        out[i] = is_open
    return out


def gate_figure(name, title, t_open, t_close, hysteresis):
    t, env = detector_envelope_db()
    flags = gate_open_flags(env, t_open, t_close)
    lo, hi = 0.30, 0.90
    sel = (t >= lo) & (t <= hi)
    fig, (ax, ag) = ps.figure(6.4, 4.2, nrows=2, sharex=True, gridspec_kw={"height_ratios": [3, 1]})
    # Plot every 8th sample (6 kHz): the 100 Hz ripple stays smooth and the SVG stays small.
    ax.plot(t[sel][::8] * 1000, env[sel][::8], color=ps.COLORS[0], label="detector envelope")
    ax.axhline(t_open, color=ps.COLORS[1], ls="--", lw=1.4,
               label="T (threshold)" if not hysteresis else "T_open = T")
    if hysteresis:
        ax.axhline(t_close, color=ps.COLORS[2], ls="--", lw=1.4, label="T_close = T - 6 dB")
    ax.set_ylabel("Envelope (dB)")
    ax.set_ylim(-42, -22)
    ax.set_title(title)
    ax.legend(loc="upper right", fontsize=8.5)
    # The gate state only needs its switching points (plus both ends), drawn as steps.
    ts, fs_ = t[sel], flags[sel].astype(float)
    keep = np.concatenate(([0], np.flatnonzero(np.diff(fs_)) + 1, [fs_.size - 1]))
    ag.fill_between(ts[keep] * 1000, 0, fs_[keep], step="post", color=ps.COLORS[0], alpha=0.6)
    ag.set_ylim(-0.1, 1.2)
    ag.set_yticks([0, 1])
    ag.set_yticklabels(["closed", "open"])
    ag.set_xlabel("Time (ms)")
    ag.set_xlim(lo * 1000, hi * 1000)
    changes = int(np.count_nonzero(np.diff(flags[sel].astype(int))))
    print(name, "state changes in window:", changes)
    ps.save(fig, name)


if __name__ == "__main__":
    transfer_curves()
    gate_figure("gate-chattering-single-threshold.svg",
                "One threshold: the gate chatters", -30.0, -30.0, False)
    gate_figure("gate-hysteresis-schmitt.svg",
                "Hysteresis (6 dB): the gate closes once", -30.0, -36.0, True)
