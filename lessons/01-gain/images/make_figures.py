"""Figures for chapter 01 (gain, dB, smoothing, pan law).

Run from anywhere:  python lessons/01-gain/images/make_figures.py
Every curve is computed from the formulas in the chapter's README (and the C++ in
dsp/include/dawlab/dsp/SmoothedValue.h, Panner.h and the exercise 4 solution).
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "docs", "tools"))

import numpy as np  # noqa: E402
import plotstyle as ps  # noqa: E402
from plotstyle import COLORS  # noqa: E402

FS = 48000.0


# --------------------------------------------------------------------------------------
# 1. Fader taper (section 2.3, exercise 4): position -> dB, piecewise
# --------------------------------------------------------------------------------------
def fader_to_db(p):
    """Exercise 4 solution: segments linear in dB above p = 0.05, linear in gain below."""
    pts_p = np.array([0.05, 0.25, 0.50, 0.75, 1.00])
    pts_db = np.array([-60.0, -30.0, -12.0, 0.0, 6.0])
    p = np.asarray(p, dtype=float)
    out = np.interp(p, pts_p, pts_db)
    low = p < 0.05
    with np.errstate(divide="ignore"):
        out = np.where(low, 20.0 * np.log10(0.001 * p / 0.05), out)
    return out


def fig_fader_taper():
    fig, ax = ps.figure(6.4, 3.8)
    p = np.linspace(0.0, 1.0, 2001)
    ax.plot(p, fader_to_db(p), color=COLORS[0])
    bp = [(1.00, 6), (0.75, 0), (0.50, -12), (0.25, -30), (0.05, -60)]
    ax.plot([b[0] for b in bp], [b[1] for b in bp], "o", color=COLORS[1], zorder=3)
    offs = {1.00: (-8, -14), 0.75: (-8, 6), 0.50: (-8, 6), 0.25: (-8, 6), 0.05: (8, 4)}
    for x, y in bp:
        ax.annotate(f"p = {x:.2f}: {y:+d} dB".replace("+0 dB", "0 dB"), (x, y), textcoords="offset points",
                    xytext=offs[x], ha="right" if x != 0.05 else "left", fontsize=8)
    ax.plot([0.0], [-80], "o", color=COLORS[1], zorder=3, clip_on=False)
    ax.annotate("p = 0: -inf dB (factor 0)", (0.0, -80), textcoords="offset points",
                xytext=(6, 4), ha="left", fontsize=8, color=COLORS[1])
    ax.text(0.55, -55, "above p = 0.05: each segment is\nlinear in dB versus position", fontsize=8)
    ax.text(0.10, -72, "bottom segment [0, 0.05]:\nfactor linear in position", fontsize=8)
    ax.set_xlim(-0.02, 1.02)
    ps.db_axis(ax, -85, 10, 10, "Fader level (dB)")
    ax.set_xlabel("Fader position p")
    ax.set_title("Fader taper of exercise 4")
    ps.save(fig, "fader-taper.svg")


# --------------------------------------------------------------------------------------
# 2. Gain step (section 2.4)
# --------------------------------------------------------------------------------------
def fig_gain_step():
    fig, ax = ps.figure(6.0, 2.6)
    n = np.arange(-20, 21)
    g1, g2, n0 = 1.0, 0.1, 0
    g = g1 + (g2 - g1) * (n >= n0)
    ax.step(n, g, where="post", color=COLORS[0])
    ax.plot(n, g, "o", color=COLORS[0], markersize=3)
    ax.set_xticks([-20, 0, 20])
    ax.set_xticklabels(["", "n0", ""])
    ax.set_yticks([g2, g1])
    ax.set_yticklabels(["g2", "g1"])
    ax.annotate("this vertical wall is the problem", (0, 0.55), textcoords="offset points",
                xytext=(40, 0), arrowprops=dict(arrowstyle="->"), va="center", fontsize=9)
    ax.set_ylim(0, 1.15)
    ax.set_xlabel("n (samples)")
    ax.set_ylabel("g[n]")
    ax.set_title("Instant gain change: a step at sample n0")
    ps.save(fig, "gain-step.svg")


# --------------------------------------------------------------------------------------
# 3. Zipper noise (section 2.4): staircase vs. smoothed gain while dragging a fader
# --------------------------------------------------------------------------------------
def fig_zipper_steps():
    block = 512
    nblocks = 8
    n_total = block * nblocks
    t_ms = np.arange(n_total) / FS * 1000.0
    targets = 10.0 ** (-np.arange(nblocks) / 20.0)  # -1 dB per block (example 1, part B)
    stepped = np.repeat(targets, block)

    # SmoothedValue: a linear ramp of N samples from the *current* value to each new target,
    # computed directly as target - step * countdown (no accumulation).
    ramp = int(round(0.050 * FS))
    cur = 1.0
    tgt = cur
    step = 0.0
    countdown = 0
    smooth = np.empty(n_total)
    i = 0
    for b in range(nblocks):
        if targets[b] != cur:
            tgt = targets[b]
            step = (tgt - cur) / ramp
            countdown = ramp
        for _ in range(block):
            if countdown > 0:
                countdown -= 1
                cur = tgt - step * countdown
            smooth[i] = cur
            i += 1

    fig, (a1, a2) = ps.figure(7.0, 3.0, ncols=2, sharey=True)
    a1.plot(t_ms, stepped, color=COLORS[1])
    a2.plot(t_ms, smooth, color=COLORS[0])
    a1.set_title("Stepped: one step per block", fontsize=10)
    a2.set_title("Smoothed: 50 ms linear ramps", fontsize=10)
    a1.text(0.04, 0.05, "sidebands fall as 1/k\n(6 dB per octave)", transform=a1.transAxes, fontsize=8)
    a2.text(0.04, 0.05, "sidebands fall as 1/k^2\n(12 dB per octave), about 40 dB lower",
            transform=a2.transAxes, fontsize=8)
    for a in (a1, a2):
        a.set_xlabel("Time (ms)")
    a1.set_ylabel("Gain g (-1 dB per block)")
    ps.save(fig, "zipper-stepped-vs-smoothed.svg")


# --------------------------------------------------------------------------------------
# 4. Linear ramp vs. one-pole exponential smoother (section 2.5)
# --------------------------------------------------------------------------------------
def fig_ramp_vs_exp():
    tau_ms = 10.0
    ramp_ms = 50.0
    t = np.linspace(0, 70, 701)
    lin = np.clip(t / ramp_ms, 0, 1)
    expo = 1.0 - np.exp(-t / tau_ms)
    t99 = np.log(100) * tau_ms
    fig, ax = ps.figure(6.4, 3.4)
    ax.plot(t, lin, color=COLORS[0], label="linear ramp: arrives exactly after N samples")
    ax.plot(t, expo, color=COLORS[1], label="exponential: 63.2% at tau, 99% at 4.6 tau")
    ax.axhline(1 - np.exp(-1), color="gray", ls=":", lw=0.8)
    ax.axhline(0.99, color="gray", ls=":", lw=0.8)
    for x in (tau_ms, t99, ramp_ms):
        ax.axvline(x, color="gray", ls=":", lw=0.8)
    ax.set_xticks([0, tau_ms, ramp_ms])
    ax.set_xticklabels(["0", "tau\n(10 ms)", "N\n(50 ms)"])
    ax.text(t99 - 0.8, 0.40, "4.6 tau", rotation=90, ha="right", va="bottom", fontsize=9)
    ax.set_yticks([0, 1 - np.exp(-1), 1.0])
    ax.set_yticklabels(["0", "0.632", "1.0"])
    ax.text(1.0, 0.985, "0.99", ha="left", va="top", fontsize=8, color="gray")
    ax.set_ylim(-0.02, 1.12)
    ax.set_xlim(0, 70)
    ax.set_xlabel("Time after the target changes")
    ax.set_ylabel("g (fraction of the way to the target)")
    ax.legend(loc="lower right")
    ax.set_title("Linear ramp vs. one-pole exponential smoothing")
    ps.save(fig, "linear-ramp-vs-exponential.svg")


# --------------------------------------------------------------------------------------
# 5. Headroom growth when summing N tracks (section 2.7)
# --------------------------------------------------------------------------------------
def fig_headroom_growth():
    n = np.geomspace(1, 8, 200)
    coherent = 20 * np.log10(n)
    incoh = 10 * np.log10(n)
    fig, ax = ps.figure(6.4, 3.6)
    ax.fill_between(n, incoh, coherent, color=COLORS[4], alpha=0.25,
                    label="the actual peak lands between the two lines")
    ax.plot(n, coherent, color=COLORS[1], label="coherent (identical tracks): 20 log10(N)")
    ax.plot(n, incoh, color=COLORS[0], label="incoherent RMS (independent tracks): 10 log10(N)")
    ns = np.array([1, 2, 4, 8])
    ax.plot(ns, 20 * np.log10(ns), "o", color=COLORS[1])
    ax.plot(ns, 10 * np.log10(ns), "o", color=COLORS[0])
    for k in ns[1:]:
        ax.annotate(f"{20 * np.log10(k):+.2f}", (k, 20 * np.log10(k)), textcoords="offset points",
                    xytext=(-4, 6), ha="right", fontsize=8)
        ax.annotate(f"{10 * np.log10(k):+.2f}", (k, 10 * np.log10(k)), textcoords="offset points",
                    xytext=(4, -12), ha="left", fontsize=8)
    ax.set_xscale("log", base=2)
    ax.set_xticks(ns)
    ax.set_xticklabels([str(int(k)) for k in ns])
    ax.minorticks_off()
    ax.set_xlim(0.9, 9)
    ax.set_ylim(-1.5, 24)
    ax.set_xlabel("Number of summed tracks N")
    ax.set_ylabel("Level growth of the sum (dB)")
    ax.legend(loc="upper left", fontsize=8)
    ax.set_title("Headroom: level growth when summing N tracks")
    ps.save(fig, "headroom-growth.svg")


# --------------------------------------------------------------------------------------
# 6. Pan laws (section 2.9)
# --------------------------------------------------------------------------------------
def pan_gains(law, theta):
    if law == "linear":
        return 1 - theta, theta
    if law == "const":
        return np.cos(theta * np.pi / 2), np.sin(theta * np.pi / 2)
    return (np.sqrt((1 - theta) * np.cos(theta * np.pi / 2)),
            np.sqrt(theta * np.sin(theta * np.pi / 2)))


def fig_pan_laws():
    pan = np.linspace(-1, 1, 401)
    theta = (pan + 1) / 2
    fig, ax = ps.figure(6.4, 4.0)
    laws = [("linear", "Linear", COLORS[0]),
            ("const", "Constant power", COLORS[1]),
            ("m45", "-4.5 dB", COLORS[2])]
    for key, name, col in laws:
        gl, gr = pan_gains(key, theta)
        ax.plot(pan, gl, color=col, label=f"{name}: gL")
        ax.plot(pan, gr, color=col, ls="--", label=f"{name}: gR")
    lines = []
    for key, name, col in laws:
        gc = float(pan_gains(key, np.array(0.5))[0])
        ax.plot([0], [gc], "o", color=col, markersize=4, zorder=3)
        lines.append(f"{name} at the center: {gc:.3f} ({20 * np.log10(gc):.2f} dB)")
    ax.text(0, 0.03, "\n".join(lines), ha="center", va="bottom", fontsize=8,
            bbox=dict(boxstyle="round", fc="white", ec="0.7"))
    ax.set_xlim(-1.02, 1.02)
    ax.set_ylim(0, 1.08)
    ax.set_xticks([-1, -0.5, 0, 0.5, 1])
    ax.set_xlabel("Pan position p  (-1 hard left, 0 center, +1 hard right)")
    ax.set_ylabel("Gain (amplitude factor)")
    ax.legend(loc="upper center", ncol=3, fontsize=7, bbox_to_anchor=(0.5, -0.2))
    ax.set_title("Pan laws: left (solid) and right (dashed) gains")
    ps.save(fig, "pan-laws.svg")


if __name__ == "__main__":
    fig_fader_taper()
    fig_gain_step()
    fig_zipper_steps()
    fig_ramp_vs_exp()
    fig_headroom_growth()
    fig_pan_laws()
