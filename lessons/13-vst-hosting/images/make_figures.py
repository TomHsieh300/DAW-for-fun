"""Figures for chapter 13 (VST hosting): python lessons/13-vst-hosting/images/make_figures.py"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)) + "/../../../docs/tools")

import numpy as np  # noqa: E402
import plotstyle as ps  # noqa: E402


def vst3_parameter_queue():
    """A VST3 parameter queue in one 512-sample block: (sampleOffset, value) points with
    linear interpolation between them (section 2.3.4)."""
    pts = [(0, 0.60), (256, 0.40), (511, 0.45)]
    xs = np.array([p[0] for p in pts], dtype=float)
    ys = np.array([p[1] for p in pts])
    n = np.arange(512)
    v = np.interp(n, xs, ys)

    fig, ax = ps.figure(6.4, 3.0)
    ax.plot(n, v, color=ps.COLORS[0], label="linear interpolation between points")
    ax.plot(xs, ys, "o", color=ps.COLORS[1], markersize=7, label="queue points (sampleOffset, value)")
    for x, y in pts:
        ax.annotate(f"({x}, {y:.2f})", (x, y), textcoords="offset points", xytext=(0, 9),
                    ha="center", fontsize=9)
    ax.set_xticks([0, 128, 256, 384, 511])
    ax.set_xlim(-10, 521)
    ax.set_ylim(0.30, 0.72)
    ax.set_xlabel("Sample offset within the block (block = 512 samples)")
    ax.set_ylabel("Normalized value (0..1)")
    ax.set_title("VST3 parameter queue inside one block")
    ax.legend(loc="lower right")
    ps.save(fig, "vst3-parameter-queue.svg")


if __name__ == "__main__":
    vst3_parameter_queue()
