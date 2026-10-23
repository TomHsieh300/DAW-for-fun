"""Figures for chapter 02 (filters): python lessons/02-filter/images/make_figures.py"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)) + "/../../../docs/tools")

import numpy as np  # noqa: E402
import plotstyle as ps  # noqa: E402
from plotstyle import COLORS  # noqa: E402

FS = 48000.0


def unit_circle():
    """Euler's formula: e^{j theta} on the unit circle (section 2.3)."""
    fig, ax = ps.figure(4.6, 4.2)
    t = np.linspace(0, 2 * np.pi, 400)
    ax.plot(np.cos(t), np.sin(t), color="#888888", lw=1.2)
    th = np.deg2rad(40.0)
    px, py = np.cos(th), np.sin(th)
    ax.plot([0, px], [0, py], color=COLORS[0])
    ax.plot([px], [py], "o", color=COLORS[0], ms=7)
    ax.plot([px, px], [0, py], ":", color=COLORS[0], lw=1.0)
    ax.plot([px, 0], [py, py], ":", color=COLORS[0], lw=1.0)
    a = np.linspace(0, th, 40)
    ax.plot(0.28 * np.cos(a), 0.28 * np.sin(a), color=COLORS[1], lw=1.4)
    ax.text(0.34, 0.07, r"$\theta$", color=COLORS[1], fontsize=12)
    ax.annotate(r"$e^{j\theta} = \cos\theta + j\sin\theta$" + "\n" + r"angle $\theta$, magnitude 1",
                xy=(px, py), xytext=(0.15, 1.08), fontsize=9,
                arrowprops=dict(arrowstyle="->", color="#444444"))
    ax.text(px + 0.04, -0.12, r"$\cos\theta$", fontsize=9, color=COLORS[0])
    ax.text(-0.42, py + 0.02, r"$\sin\theta$", fontsize=9, color=COLORS[0])
    ax.plot([-1, 1], [0, 0], "o", color="black", ms=5)
    ax.text(1.05, 0.06, "z = 1\nDC (" + r"$\omega$" + " = 0)", fontsize=8.5, va="bottom")
    ax.text(-1.05, 0.06, "z = -1\nNyquist (" + r"$\omega$ = $\pi$" + ")", fontsize=8.5, ha="right", va="bottom")
    ax.axhline(0, color="black", lw=0.8)
    ax.axvline(0, color="black", lw=0.8)
    ax.set_xlim(-1.75, 1.75)
    ax.set_ylim(-1.25, 1.4)
    ax.set_aspect("equal")
    ax.set_xlabel("Re")
    ax.set_ylabel("Im")
    ax.set_title("Euler's formula on the unit circle")
    ps.save(fig, "unit-circle-euler.svg")


def moving_average():
    """|H| of an N = 8 moving average at fs = 48 kHz (section 2.4)."""
    N = 8
    f = np.linspace(1.0, FS / 2, 4000)
    w = 2 * np.pi * f / FS
    H = np.abs(np.sin(N * w / 2) / (N * np.sin(w / 2)))
    db = 20 * np.log10(np.maximum(H, 1e-12))
    fig, ax = ps.figure(6.4, 3.4)
    ax.plot(f / 1000, db, color=COLORS[0])
    ps_ylim = (-40, 3)
    ax.set_ylim(*ps_ylim)
    ax.set_xlim(0, 24)
    # -3 dB point (exact: bisection on the formula)
    lo, hi = 1.0, FS / N
    for _ in range(60):
        mid = 0.5 * (lo + hi)
        wm = 2 * np.pi * mid / FS
        if 20 * np.log10(np.sin(N * wm / 2) / (N * np.sin(wm / 2))) > -3.0103:
            lo = mid
        else:
            hi = mid
    f3 = lo
    ax.plot([f3 / 1000], [-3.0103], "o", color=COLORS[1])
    ax.annotate(f"-3 dB at {f3 / 1000:.2f} kHz\n(0.443 fs/N = {0.443 * FS / N / 1000:.2f} kHz)",
                xy=(f3 / 1000, -3.01), xytext=(4.2, -9), fontsize=8.5,
                arrowprops=dict(arrowstyle="->", color="#444444"))
    # first sidelobe peak
    m = (f > FS / N) & (f < 2 * FS / N)
    i = np.argmax(db[m])
    fp, dp = f[m][i], db[m][i]
    ax.plot([fp / 1000], [dp], "o", color=COLORS[2])
    ax.annotate(f"sidelobe peak: {fp / 1000:.1f} kHz, {dp:.1f} dB", xy=(fp / 1000, dp),
                xytext=(10.5, -8), fontsize=8.5, arrowprops=dict(arrowstyle="->", color="#444444"))
    for k in (1, 2, 3):
        ax.axvline(k * FS / N / 1000, color="#999999", ls="--", lw=0.8)
        ax.text(k * FS / N / 1000 + 0.15, -38, f"null\n{k * FS / N / 1000:g} kHz", fontsize=8, va="bottom")
    ax.set_xlabel("Frequency (kHz)")
    ax.set_ylabel("|H| (dB)")
    ax.set_title("Moving average, N = 8, fs = 48 kHz")
    ps.save(fig, "moving-average-response.svg")


def rbj_lpf(f0, q, fs=FS):
    w0 = 2 * np.pi * f0 / fs
    al = np.sin(w0) / (2 * q)
    c = np.cos(w0)
    a0 = 1 + al
    return (np.array([(1 - c) / 2, 1 - c, (1 - c) / 2]) / a0,
            np.array([1.0, -2 * c / a0, (1 - al) / a0]))


def pole_zero():
    """Poles and zeros of the RBJ lowpass (f0 = 1 kHz, Q = 2, fs = 48 kHz), section 2.6."""
    b, a = rbj_lpf(1000.0, 2.0)
    poles = np.roots(a)
    zeros = np.roots(b)
    fig, axes = ps.figure(7.4, 3.8, ncols=2, gridspec_kw={"width_ratios": [1.2, 1]})
    t = np.linspace(0, np.pi, 300)
    for k, ax in enumerate(axes):
        ax.plot(np.cos(t), np.sin(t), color="#888888", lw=1.2)
        ax.plot(np.cos(t), -np.sin(t), color="#888888", lw=1.2, ls=":")
        ax.axhline(0, color="black", lw=0.8)
        ax.axvline(0, color="black", lw=0.8)
        ax.plot(poles.real, poles.imag, "x", color=COLORS[1], ms=9, mew=2.2, label="pole (conjugate pair, inside the circle)")
        ax.plot(zeros.real, zeros.imag, "o", mfc="none", color=COLORS[0], ms=10, mew=1.8, label="zero (double, at z = -1)")
        ax.set_aspect("equal")
        ax.set_xlabel("Re")
        ax.set_ylabel("Im")
    axes[0].set_xlim(-1.4, 1.4)
    axes[0].set_ylim(-1.3, 1.3)
    axes[0].text(-1.0, 0.1, "z = -1\nNyquist", fontsize=8.5, ha="center", va="bottom")
    axes[0].text(1.0, -0.3, "z = 1\nDC", fontsize=8.5, ha="center", va="top")
    axes[0].text(0.0, 0.3, "unit circle |z| = 1\nsolid: upper half,\n0 .. Nyquist", fontsize=8.5, ha="center")
    axes[0].set_title("z-plane")
    axes[1].set_xlim(0.9, 1.02)
    axes[1].set_ylim(-0.2, 0.2)
    axes[1].set_title("zoom near z = 1")
    axes[1].text(0.905, 0.17, f"pole radius {abs(poles[0]):.4f}", fontsize=8.5)
    h, l = axes[0].get_legend_handles_labels()
    fig.legend(h, l, loc="lower center", ncol=2, frameon=False, fontsize=8.5)
    fig.suptitle("RBJ lowpass: f0 = 1 kHz, Q = 2, fs = 48 kHz", fontsize=11)
    fig.tight_layout(rect=(0, 0.08, 1, 0.96))
    out = os.path.join(os.path.dirname(os.path.abspath(sys.argv[0])), "pole-zero-lowpass.svg")
    fig.savefig(out, format="svg", metadata={"Date": None})
    ps.plt.close(fig)
    print("wrote", os.path.relpath(out))


if __name__ == "__main__":
    unit_circle()
    moving_average()
    pole_zero()
