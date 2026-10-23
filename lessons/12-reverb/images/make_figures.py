"""Figures for Chapter 12 (reverb): the anatomy of a room impulse response and the comb spectrum.

Run:  python lessons/12-reverb/images/make_figures.py

- reverb-impulse-response.svg: a synthetic room impulse response. The early reflections follow
  the image-source idea (amplitude ~ 1/distance, one wall bounce costs 0.85), and the late tail
  is noise whose amplitude decays as 10^(-3 t / RT60) (RT60 = 1.5 s), with the reflection
  density growing with t^2 as in section 2.1.
- reverb-comb-spectrum.svg: |H| = 1 / |1 - g e^{-j w D}| of the feedback comb of section 2.3, with
  D = 1557, fs = 48 kHz, RT60 = 2 s so g = 10^(-3 D / (RT60 fs)) = 0.894.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)) + "/../../../docs/tools")

import numpy as np  # noqa: E402
import plotstyle as ps  # noqa: E402

C = ps.COLORS
C_SOUND = 343.0  # m/s


def fig_impulse_response():
    rng = np.random.default_rng(12)
    d0 = 3.0                 # direct path length (m)
    rt60 = 1.5               # s
    # Early reflections: extra path lengths (m) of the first reflections, relative to the direct sound.
    extra = np.array([4.1, 6.5, 8.9, 10.6, 15.0, 17.8, 21.5, 27.0])
    t_er = extra / C_SOUND                       # s, relative to the direct sound
    a_er = 0.85 * d0 / (d0 + extra)              # 1/distance spreading, one bounce loses 15 %
    a_er *= np.array([1, -1, 1, 1, -1, 1, -1, 1])

    # Late tail: noise sampled at 4 kHz, with density ramp ~ t^2 then exponential decay.
    fs_plot = 4000.0
    t = np.arange(0, 1.5, 1.0 / fs_plot)
    t0 = 0.04
    ramp = np.clip(((t - t0) / 0.08), 0.0, 1.0) ** 2
    decay = 10.0 ** (-3.0 * (t - t0) / rt60)
    env = 0.45 * ramp * decay
    tail = np.clip(rng.standard_normal(t.size), -3, 3) / 3.0 * env

    fig, (ax1, ax2) = ps.figure(6.6, 5.0, nrows=2)

    def minmax(time, x, bucket):
        """Min / max of every `bucket` samples, like a DAW's waveform overview: the same picture
        as one line per sample, with a few hundred points instead of tens of thousands."""
        n = (x.size // bucket) * bucket
        xs = x[:n].reshape(-1, bucket)
        return time[:n:bucket], xs.min(axis=1), xs.max(axis=1)

    # Top: first 120 ms, direct sound + early reflections + beginning of the tail
    m = t < 0.12
    tm, lo_, hi_ = minmax(t[m], tail[m], 6)  # 0.125 ms buckets
    ax1.fill_between(tm * 1000, lo_, hi_, color="#999999", lw=0, label="late reverb (start)")
    ax1.vlines([0], 0, [1.0], color=C[6], lw=2.4, label="direct sound")
    ax1.vlines(t_er * 1000, 0, a_er, color=C[1], lw=2.0, label="early reflections")
    ax1.axvspan(0, t_er[0] * 1000, color=C[4], alpha=0.12)
    ax1.text(t_er[0] * 500 + 1.5, 0.95, "pre-delay", ha="center", va="top", fontsize=8)
    ax1.set_xlim(-2, 120)
    ax1.set_ylim(-0.6, 1.1)
    ax1.set_xlabel("Time after the direct sound (ms)")
    ax1.set_ylabel("Amplitude")
    ax1.set_title("First 120 ms: direct sound, discrete early reflections, tail begins")
    ax1.axhline(0, color="#666666", lw=0.8)
    ax1.legend(loc="upper right", fontsize=8)

    # Bottom: whole tail with the exponential envelope
    tb, lo_, hi_ = minmax(t, tail, 48)  # 1 ms buckets
    ax2.fill_between(tb * 1000, lo_, hi_, color="#999999", lw=0)
    ax2.vlines([0], 0, [1.0], color=C[6], lw=2.0)
    ax2.vlines(t_er * 1000, 0, a_er, color=C[1], lw=1.6)
    tt = t[t >= t0]
    e = 0.45 * 10.0 ** (-3.0 * (tt - t0) / rt60)
    ax2.plot(tt * 1000, e, color=C[0], lw=1.4, ls="--", label="envelope $10^{-3t/RT60}$ (RT60 = 1.5 s)")
    ax2.plot(tt * 1000, -e, color=C[0], lw=1.4, ls="--")
    ax2.set_xlim(0, 1500)
    ax2.set_ylim(-1.1, 1.1)
    ax2.set_xlabel("Time after the direct sound (ms)")
    ax2.set_ylabel("Amplitude")
    ax2.set_title("Whole impulse response: the diffuse tail decays exponentially")
    ax2.axhline(0, color="#666666", lw=0.8)
    ax2.legend(loc="upper right", fontsize=8)
    ps.save(fig, "reverb-impulse-response.svg")


def fig_comb_spectrum():
    fs, D, rt60 = 48000.0, 1557, 2.0
    g = 10.0 ** (-3.0 * D / (rt60 * fs))
    f = np.linspace(0.0, 4.5 * fs / D, 4000)
    w = 2.0 * np.pi * f / fs
    h = 1.0 / np.abs(1.0 - g * np.exp(-1j * w * D))
    hdb = 20.0 * np.log10(h)
    peak = -20.0 * np.log10(1.0 - g)
    notch = -20.0 * np.log10(1.0 + g)
    spacing = fs / D

    fig, ax = ps.figure(6.6, 3.4)
    ax.plot(f, hdb, color=C[0])
    ax.axhline(peak, color=C[1], lw=0.9, ls="--")
    ax.axhline(notch, color=C[2], lw=0.9, ls="--")
    ax.text(4.5 * spacing, peak + 0.6, f"peak = 1/(1-g) = {peak:+.2f} dB", ha="right", va="bottom", fontsize=8)
    ax.text(4.5 * spacing, notch - 0.6, f"notch = 1/(1+g) = {notch:+.2f} dB", ha="right", va="top", fontsize=8)
    ax.annotate("", xy=(2 * spacing, 22.0), xytext=(1 * spacing, 22.0),
                arrowprops=dict(arrowstyle="<->", color="#333333"))
    ax.text(1.5 * spacing, 23.0, f"spacing = fs/D = {spacing:.1f} Hz", ha="center", fontsize=8)
    ax.set_xticks([k * spacing for k in range(5)])
    ax.set_xticklabels(["0", "fs/D", "2fs/D", "3fs/D", "4fs/D"])
    ax.set_xlim(0, 4.5 * spacing)
    ps.db_axis(ax, -10, 26, 5, "Gain (dB)")
    ax.set_xlabel("Frequency")
    ax.set_title(f"Feedback comb, D = {D}, fs = 48 kHz, g = {g:.3f} (RT60 = 2 s)")
    ps.save(fig, "reverb-comb-spectrum.svg")


if __name__ == "__main__":
    fig_impulse_response()
    fig_comb_spectrum()
