"""Figures for Chapter 11 (metering and FFT). Run: python lessons/11-metering-fft/images/make_figures.py

Every curve is computed from the same equations as the chapter's README / dsp/include/dawlab/dsp:
- LevelMeter peak + hold logic (README section 2.2, decay g = 10^(-D / (20 fs)))
- the fs/4 inter-sample-peak sine (section 2.5)
- BS.1770 400 ms blocks with 75% overlap (section 2.6)
- a non-integer number of periods repeated by the DFT (section 2.9)
- Fft::makeWindow + coherent-gain normalisation (section 2.9, same test signal as l11_window_leakage)
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)) + "/../../../docs/tools")

import numpy as np  # noqa: E402

import plotstyle  # noqa: E402
from plotstyle import COLORS, figure, save  # noqa: E402


def peak_meter_hold_decay():
    fs = 48000
    decay_db_per_s = 24.0
    hold_s = 1.0
    amp = 10 ** (-6.0 / 20)              # -6 dBFS sine, 1 kHz, 0.5 s, then silence
    n_total = int(2.2 * fs)
    n_sig = int(0.5 * fs)
    t = np.arange(n_total) / fs
    x = np.zeros(n_total)
    x[:n_sig] = amp * np.sin(2 * np.pi * 1000.0 * t[:n_sig])

    g = 10.0 ** (-decay_db_per_s / (20.0 * fs))          # per-sample decay coefficient
    hold_samples = int(hold_s * fs)
    peak = hold = 0.0
    counter = 0
    peak_db = np.empty(n_total)
    hold_db = np.empty(n_total)
    for i, v in enumerate(np.abs(x)):
        peak = v if v >= peak else peak * g
        if v >= hold:
            hold, counter = v, hold_samples
        elif counter > 0:
            counter -= 1
        else:
            hold = max(hold * g, peak)
        peak_db[i] = 20 * np.log10(max(peak, 1e-5))
        hold_db[i] = 20 * np.log10(max(hold, 1e-5))

    fig, ax = figure(6.6, 3.4)
    ax.plot(t, hold_db, color=COLORS[1], label="peak hold (1 s, then falls)")
    ax.plot(t, peak_db, color=COLORS[0], label="peak (rises instantly, falls 24 dB/s)")
    ax.axvline(0.5, color="#888888", ls=":", lw=1.2)
    ax.text(0.52, -3.2, "signal stops (0.5 s)", fontsize=8.5, color="#555555")
    ax.axvline(1.5, color="#888888", ls=":", lw=1.2)
    ax.text(1.52, -3.2, "hold time ends (1.5 s)", fontsize=8.5, color="#555555")
    ax.set_xlim(0, 2.2)
    ax.set_ylim(-50, 0)
    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Level (dBFS)")
    ax.set_title("Peak meter ballistics: instant rise, peak hold, 24 dB/s decay")
    ax.legend(loc="lower left")
    save(fig, "peak-meter-hold-decay.svg")


def intersample_peak():
    n = np.arange(0, 8)
    tt = np.linspace(0, 7, 1400)
    cont = np.sin(np.pi * tt / 2 + np.pi / 4)
    samples = np.sin(np.pi * n / 2 + np.pi / 4)
    peaks_t = np.array([0.5, 2.5, 4.5, 6.5])
    peaks = np.sin(np.pi * peaks_t / 2 + np.pi / 4)

    fig, ax = figure(6.6, 3.4)
    ax.plot(tt, cont, color=COLORS[0], label="continuous waveform")
    ax.plot(n, samples, "o", color=COLORS[1], ms=7, ls="none", label="samples x[n] (always +/-0.707)")
    ax.plot(peaks_t, peaks, "*", color=COLORS[2], ms=12, ls="none", label="true peak (1.0, between samples)")
    ax.axhline(0.707, color="#888888", ls=":", lw=1)
    ax.axhline(-0.707, color="#888888", ls=":", lw=1)
    ax.axhline(0, color="#000000", lw=0.8)
    ax.set_xticks(n)
    ax.set_xlabel("n (samples)")
    ax.set_ylabel("Amplitude")
    ax.set_ylim(-1.4, 1.9)
    ax.set_title("x[n] = sin(pi n / 2 + pi / 4): sample peak -3.01 dBFS, true peak 0 dBTP")
    ax.title.set_fontsize(10)
    ax.legend(loc="upper center", ncol=3, fontsize=7.5, framealpha=0.9, columnspacing=1.0, handletextpad=0.4)
    save(fig, "intersample-peak.svg")


def loudness_blocks():
    block_ms, step_ms, n_blocks = 400, 100, 4
    fig, ax = figure(6.6, 2.9)
    for b in range(n_blocks):
        start = b * step_ms
        ax.barh(b, block_ms, left=start, height=0.62, color=COLORS[0], alpha=0.25 + 0.0 * b,
                edgecolor=COLORS[0])
        ax.text(start + block_ms / 2, b, f"400 ms block {b + 1}", ha="center", va="center", fontsize=9)
    end = (n_blocks - 1) * step_ms + block_ms
    for k in range(end // step_ms + 1):
        ax.axvline(k * step_ms, color="#888888", lw=0.8, ls=":")
    ax.set_xlim(0, end + 20)
    ax.set_ylim(n_blocks - 0.4, -0.7)
    ax.set_yticks([])
    ax.set_xticks(range(0, end + 1, step_ms))
    ax.set_xlabel("Time (ms); dotted lines = 100 ms sub-blocks")
    ax.set_title("Gating blocks: 400 ms long, a new one every 100 ms (75% overlap)")
    ax.grid(False)
    ax.spines["left"].set_visible(False)
    save(fig, "loudness-block-overlap.svg")


def leakage_discontinuity():
    n = 128                    # frame length
    periods = 3.3              # not a whole number of periods
    k = np.arange(3 * n)
    x = np.sin(2 * np.pi * periods * (k % n) / n)

    fig, ax = figure(6.6, 3.0)
    ax.plot(k[:n + 1], x[:n + 1], color=COLORS[0], lw=2.2, label="the analysed frame")
    ax.plot(k[n:], x[n:], color=COLORS[0], lw=1.2, alpha=0.5, label="implied repetition")
    for s in (n, 2 * n):
        ax.axvline(s, color=COLORS[1], ls="--", lw=1.2)
        ax.annotate("jump", (s, 0.45), (s + 8, 1.1), fontsize=9, color=COLORS[1],
                    arrowprops=dict(arrowstyle="-", color=COLORS[1], lw=0.8))
    ax.axhline(0, color="#000000", lw=0.8)
    ax.set_xlim(0, 3 * n)
    ax.set_ylim(-1.4, 1.4)
    ax.set_xlabel("n (samples)")
    ax.set_ylabel("Amplitude")
    ax.set_title("3.3 periods per frame: repeating the frame creates a discontinuity")
    ax.legend(loc="lower right", fontsize=8)
    save(fig, "dft-implied-periodicity.svg")


def window_leakage_spectra():
    fs, N = 48000.0, 2048
    t = np.arange(N) / fs
    sig = np.sin(2 * np.pi * 1000.0 * t) + 10 ** (-60 / 20) * np.sin(2 * np.pi * 3300.0 * t)
    n = np.arange(N)
    tt = 2 * np.pi * n / (N - 1)                      # Fft::makeWindow (symmetric)
    windows = {
        "Rectangular": np.ones(N),
        "Hann": 0.5 - 0.5 * np.cos(tt),
        "Blackman-Harris": 0.35875 - 0.48829 * np.cos(tt) + 0.14128 * np.cos(2 * tt) - 0.01168 * np.cos(3 * tt),
    }
    freqs = np.arange(N // 2 + 1) * fs / N
    fig, ax = figure(6.6, 3.6)
    for color, (name, w) in zip((COLORS[1], COLORS[0], COLORS[2]), windows.items()):
        cg = w.sum() / N
        spec = np.fft.rfft(sig * w)
        mag = 2 * np.abs(spec) / (N * cg)
        mag[0] /= 2
        mag[-1] /= 2
        db = 20 * np.log10(np.maximum(mag, 1e-5))      # floor at -100 dB like the example
        m = freqs <= 5000
        ax.plot(freqs[m], db[m], color=color, lw=1.3, label=name)
    ax.annotate("-60 dB tone at 3.3 kHz", (3300, -60), (3550, -35), fontsize=9,
                arrowprops=dict(arrowstyle="->", color="#555555"))
    ax.set_xlim(0, 5000)
    ax.set_ylim(-110, 5)
    ax.set_xlabel("Frequency (Hz)")
    ax.set_ylabel("Level (dBFS)")
    ax.set_title("1 kHz (0 dBFS) + 3.3 kHz (-60 dBFS), N = 2048, fs = 48 kHz")
    ax.legend(loc="upper right")
    save(fig, "window-leakage-spectra.svg")


if __name__ == "__main__":
    peak_meter_hold_decay()
    intersample_peak()
    loudness_blocks()
    leakage_discontinuity()
    window_leakage_spectra()
