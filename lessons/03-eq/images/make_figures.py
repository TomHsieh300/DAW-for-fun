"""Figures for Chapter 03 (EQ). Run: python lessons/03-eq/images/make_figures.py

Every curve is computed from the RBJ cookbook formulas used by
dsp/include/dawlab/dsp/Biquad.h (makePeak, makeLowShelf, makeHighShelf, makeHighpass,
makeLowpass, makeNotch) and butterworthQ, at fs = 48 kHz.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)) + "/../../../docs/tools")

import numpy as np  # noqa: E402
import plotstyle as ps  # noqa: E402

FS = 48000.0
TWO_PI = 2.0 * np.pi


def _angles(f0, q):
    w0 = TWO_PI * f0 / FS
    return np.cos(w0), np.sin(w0) / (2.0 * q)


def _norm(b0, b1, b2, a0, a1, a2):
    return (b0 / a0, b1 / a0, b2 / a0), (1.0, a1 / a0, a2 / a0)


def peak(f0, q, gain_db):
    c, al = _angles(f0, q)
    A = 10 ** (gain_db / 40.0)
    return _norm(1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A)


def low_shelf(f0, q, gain_db):
    c, al = _angles(f0, q)
    A = 10 ** (gain_db / 40.0)
    sa = 2 * np.sqrt(A) * al
    return _norm(A * ((A + 1) - (A - 1) * c + sa), 2 * A * ((A - 1) - (A + 1) * c),
                 A * ((A + 1) - (A - 1) * c - sa), (A + 1) + (A - 1) * c + sa,
                 -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sa)


def high_shelf(f0, q, gain_db):
    c, al = _angles(f0, q)
    A = 10 ** (gain_db / 40.0)
    sa = 2 * np.sqrt(A) * al
    return _norm(A * ((A + 1) + (A - 1) * c + sa), -2 * A * ((A - 1) + (A + 1) * c),
                 A * ((A + 1) + (A - 1) * c - sa), (A + 1) - (A - 1) * c + sa,
                 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sa)


def highpass(f0, q):
    c, al = _angles(f0, q)
    return _norm((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al)


def lowpass(f0, q):
    c, al = _angles(f0, q)
    return _norm((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al)


def notch(f0, q):
    c, al = _angles(f0, q)
    return _norm(1, -2 * c, 1, 1 + al, -2 * c, 1 - al)


def butterworth_q(order, stage):
    phi = (2.0 * stage + 1.0) * np.pi / (2.0 * order)
    return 1.0 / (2.0 * np.cos(phi))


def mag(coefs, freqs):
    return ps.biquad_mag_db(coefs[0], coefs[1], freqs, FS)


def graphic_vs_parametric():
    fig, (axg, axp) = ps.figure(9.0, 3.4, ncols=2, gridspec_kw={"width_ratios": [1, 1.15]})

    # Left: a 10-band graphic EQ. Only the gain of each fixed band moves
    # (the fader positions are an illustrative example).
    labels = ["31", "63", "125", "250", "500", "1k", "2k", "4k", "8k", "16k"]
    gains = [6, 4, 2, -3, -6, -2, 3, 8, 5, 1]
    x = np.arange(len(labels))
    axg.bar(x, gains, width=0.55, color=ps.COLORS[0])
    axg.axhline(0, color="black", linewidth=0.8)
    axg.set_xticks(x)
    axg.set_xticklabels(labels, fontsize=8)
    axg.set_xlabel("Band centre (Hz), fixed")
    ps.db_axis(axg, -12, 12, 6, "Gain (dB)")
    axg.set_title("Graphic EQ: 10 bands, gain only")

    # Right: one parametric band, Peak, 3000 Hz, Q = 1.0, +5 dB.
    f = ps.log_freqs(20, 20000)
    axp.plot(f, mag(peak(3000.0, 1.0, 5.0), f), color=ps.COLORS[1])
    axp.axvline(3000, color="black", linestyle=":", linewidth=1)
    ps.freq_axis(axp)
    ps.db_axis(axp, -12, 12, 6, "Gain (dB)")
    axp.set_title("Parametric EQ: three knobs per band")
    axp.text(0.03, 0.97,
             "Band 3\nType: Peak\nFreq: 3000 Hz  (10 .. ~0.49 fs)\nQ: 1.0  (0.05 .. 40)\n"
             "Gain: +5 dB  (+/-36 dB)",
             transform=axp.transAxes, va="top", ha="left", fontsize=8.5,
             bbox=dict(boxstyle="round", facecolor="white", edgecolor="#888888"))
    ps.save(fig, "graphic-vs-parametric-eq.svg")


def band_types():
    fig, axes = ps.figure(10.0, 5.6, ncols=3, nrows=2, sharex=True)
    f = ps.log_freqs(20, 20000)
    f0 = 1000.0

    def decorate(ax, title, lo, hi, step):
        ax.axvline(f0, color="black", linestyle=":", linewidth=1)
        ax.axhline(0, color="#888888", linewidth=0.8)
        ps.freq_axis(ax, label="Frequency (Hz)")
        ax.set_xticks([20, 100, 1000, 10000])
        ax.set_xticklabels(["20", "100", "1k", "10k"])
        ps.db_axis(ax, lo, hi, step)
        ax.set_title(title, fontsize=10)

    ax = axes[0, 0]
    ax.plot(f, mag(peak(f0, 1.0, 6.0), f), color=ps.COLORS[0])
    ax.text(f0 * 1.1, 6.3, "+g at f0", fontsize=8, va="bottom")
    decorate(ax, "Peak / Bell (+6 dB, Q = 1)", -9, 9, 3)

    ax = axes[0, 1]
    ax.plot(f, mag(low_shelf(f0, 0.7071, 6.0), f), color=ps.COLORS[0])
    ax.plot([f0], [3.0], "o", color=ps.COLORS[1])
    ax.annotate("f0 = midpoint (+g/2)", (f0, 3.0), (1500, 0.8), fontsize=8,
                arrowprops=dict(arrowstyle="-", color="#555555"))
    decorate(ax, "Low Shelf (+6 dB, Q = 0.707)", -9, 9, 3)

    ax = axes[0, 2]
    ax.plot(f, mag(high_shelf(f0, 0.7071, 6.0), f), color=ps.COLORS[0])
    ax.plot([f0], [3.0], "o", color=ps.COLORS[1])
    ax.annotate("f0 = midpoint (+g/2)", (f0, 3.0), (30, 0.8), fontsize=8,
                arrowprops=dict(arrowstyle="-", color="#555555"))
    decorate(ax, "High Shelf (+6 dB, Q = 0.707)", -9, 9, 3)

    ax = axes[1, 0]
    ax.plot(f, mag(highpass(f0, 0.7071), f), color=ps.COLORS[0], label="12 dB/oct (1 biquad)")
    c24 = [highpass(f0, butterworth_q(4, k)) for k in range(2)]
    ax.plot(f, mag(c24[0], f) + mag(c24[1], f), color=ps.COLORS[2], label="24 dB/oct (2 biquads)")
    ax.plot([f0], [-3.01], "o", color=ps.COLORS[1])
    ax.text(f0 * 1.15, -6, "-3 dB @ f0", fontsize=8, va="top")
    ax.legend(loc="lower right", fontsize=8)
    decorate(ax, "Low Cut (highpass)", -36, 6, 6)

    ax = axes[1, 1]
    ax.plot(f, mag(lowpass(f0, 0.7071), f), color=ps.COLORS[0], label="12 dB/oct (1 biquad)")
    c24 = [lowpass(f0, butterworth_q(4, k)) for k in range(2)]
    ax.plot(f, mag(c24[0], f) + mag(c24[1], f), color=ps.COLORS[2], label="24 dB/oct (2 biquads)")
    ax.plot([f0], [-3.01], "o", color=ps.COLORS[1])
    ax.text(f0 * 0.85, -6, "-3 dB @ f0", fontsize=8, va="top", ha="right")
    ax.legend(loc="lower left", fontsize=8)
    decorate(ax, "High Cut (lowpass)", -36, 6, 6)

    ax = axes[1, 2]
    # Grid that includes f0 exactly, so the true notch depth is reached (clipped by the axis).
    fn = np.unique(np.concatenate([f, [f0]]))
    ax.plot(fn, mag(notch(f0, 2.0), fn), color=ps.COLORS[0])
    ax.text(f0 * 1.2, -34, "-inf dB at f0", fontsize=8, va="bottom")
    decorate(ax, "Notch (Q = 2)", -36, 6, 6)

    ps.save(fig, "eq-band-types.svg")


if __name__ == "__main__":
    graphic_vs_parametric()
    band_types()
