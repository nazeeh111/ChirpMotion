"""Render the preserved ChirpMotion controls for a narrow README.

No analyzer or measurement is run. Every stored sample/bin is retained.
Uses the same manifest-checked PCM/CSV decoding and units as render.py.
"""
import argparse
import csv
import hashlib
import json
import platform
import struct
import xml.etree.ElementTree as ET
from pathlib import Path

import numpy as np
import matplotlib as mpl
mpl.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.text import Text

HERE = Path(__file__).resolve().parent
WIDTH, HEIGHT = 254, 1160
BACKGROUND = "#fffdf8"
INK = "#253642"
COLORS = ("#245d89", "#94551a")


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", type=Path, default=HERE / "data",
                        help="Manifest-checked preserved data directory.")
    parser.add_argument("--out", type=Path, default=HERE,
                        help="Existing destination directory for phone exports.")
    args = parser.parse_args()
    data, out = args.data.resolve(), args.out.resolve()
    if not out.is_dir():
        parser.error("--out must be an existing directory")
    manifest = json.loads((data / "manifest.json").read_text())
    for name, record in manifest["files"].items():
        path = data / name
        assert path.stat().st_size == record["bytes"], name
        assert sha256(path) == record["sha256"], name
    assert manifest["sample_rate_hz"] == 48000
    series = {}
    for name in ("single", "two"):
        raw = np.array(struct.unpack("<961h", (data / name / "input.pcm").read_bytes()))
        rows = list(csv.DictReader((data / name / "spectra.csv").open()))
        cycle_rows = list(csv.DictReader((data / name / "cycles.csv").open()))
        assert len(rows) == 513 and len(cycle_rows) == 1
        assert cycle_rows[0] == manifest["rows"][name]
        frequency = np.array([float(row["frequency_hz"]) for row in rows])
        magnitude = np.array([float(row["magnitude"]) for row in rows])
        assert frequency[0] == 0 and frequency[-1] == 1500
        assert np.all(np.diff(frequency) > 0)
        series[name] = (raw, frequency, magnitude)
    assert manifest["rows"]["single"]["quality"] == "usable"
    assert manifest["rows"]["two"]["quality"] == "ambiguous"
    assert manifest["rows"]["two"]["relative_delay_seconds"] == "null"
    assert manifest["rows"]["two"]["relative_path_meters"] == "null"

    style = {
        "font.family": ["DejaVu Sans", "Arial", "sans-serif"],
        "font.size": 13, "svg.fonttype": "none",
        "svg.hashsalt": "chirpmotion-readable-phone",
        "path.simplify": False, "axes.spines.top": False,
        "axes.spines.right": False, "axes.labelcolor": INK,
        "text.color": INK, "xtick.color": INK, "ytick.color": INK,
        "axes.edgecolor": INK,
    }
    with mpl.rc_context(style):
        fig = plt.figure(figsize=(WIDTH / 72, HEIGHT / 72), dpi=144)
        fig.set_facecolor(BACKGROUND)

        def text(y, value, size=13, weight="normal", color=INK):
            return fig.text(14 / WIDTH, 1 - y / HEIGHT, value,
                            fontsize=size, weight=weight, color=color,
                            va="top", linespacing=1.15)

        def axis(top, height):
            ax = fig.add_axes([64 / WIDTH, (HEIGHT - top - height) / HEIGHT,
                               172 / WIDTH, height / HEIGHT])
            ax.set_facecolor(BACKGROUND)
            ax.tick_params(labelsize=13, length=3, pad=3)
            ax.xaxis.label.set_size(13)
            ax.yaxis.get_offset_text().set_size(13)
            ax.spines["left"].set_linewidth(.7)
            ax.spines["bottom"].set_linewidth(.7)
            return ax

        text(18, "A peak is not\nyet a path.", 17, "bold")
        text(68, "Generated native PCM controls.\nNo physical calibration.")
        plots = []
        for i, name in enumerate(("single", "two")):
            raw, frequency, magnitude = series[name]
            color, row = COLORS[i], manifest["rows"][name]
            first = i == 0
            text(124 if first else 605,
                 "One delayed component" if first else "Two competing\ncomponents",
                 15, "bold")
            text(151 if first else 650, "PCM int16 units")
            wave = axis(174 if first else 673, 118)
            time_ms = np.arange(961) / 48000 * 1000
            line, = wave.plot(time_ms, raw, color=color, lw=.8)
            line.set_gid(name + "-waveform")
            wave.set(xlim=(0, 20), ylim=(-13000, 13000),
                     xticks=[0, 5, 10, 15, 20],
                     yticks=[-10000, 0, 10000], xlabel="Time (ms)")
            wave.axhline(0, color="#aab5b9", lw=.6, zorder=0)
            status = ("usable\n" + row["peak_hz"] + " Hz") if first else "ambiguous\ndelay/path: null"
            text(340 if first else 839, status, 14, "bold", color)
            text(387 if first else 886, "Unnormalized magnitude")
            spectrum = axis(417 if first else 916, 118)
            line, = spectrum.plot(frequency, magnitude, color=color, lw=1.2)
            line.set_gid(name + "-spectrum")
            spectrum.set(xlim=(0, 1500), ylim=(0, 1200000),
                         xticks=[0, 500, 1000, 1500],
                         yticks=[0, 600000, 1200000], xlabel="Beat frequency (Hz)")
            spectrum.ticklabel_format(axis="y", style="sci",
                                      scilimits=(0, 0), useMathText=False)
            spectrum.yaxis.get_offset_text().set_size(13)
            peak = float(row["peak_hz"])
            value = float(row["peak_magnitude"])
            spectrum.plot([peak], [value], marker="o", color=color, ms=4)
            assert np.array_equal(wave.lines[0].get_xdata(), time_ms)
            assert np.array_equal(wave.lines[0].get_ydata(), raw)
            assert np.array_equal(spectrum.lines[0].get_xdata(), frequency)
            assert np.array_equal(spectrum.lines[0].get_ydata(), magnitude)
            plots.extend((wave, spectrum))
        text(1100, "All 961 samples and 513 bins\nper case; no rescaling or smoothing.\nHeuristic screen, not confidence.")

        fig.canvas.draw()
        renderer = fig.canvas.get_renderer()
        text_boxes = []
        for artist in fig.findobj(match=Text):
            if not artist.get_visible() or not artist.get_text():
                continue
            box = artist.get_window_extent(renderer)
            # Figure dpi is 144: two output pixels per logical unit.
            bounds = [float(v) / 2 for v in (box.x0, box.y0, box.x1, box.y1)]
            assert min(bounds[:2]) >= -.1, (artist.get_text(), bounds)
            assert bounds[2] <= WIDTH + .1 and bounds[3] <= HEIGHT + .1, (artist.get_text(), bounds)
            text_boxes.append({"text": artist.get_text(), "font_units": artist.get_fontsize(),
                               "bounds_logical_bottom_origin": bounds})
        for suffix in ("svg", "png"):
            fig.savefig(out / ("screening-phone." + suffix),
                        dpi=144, facecolor=BACKGROUND,
                        metadata=({"Date": None, "Creator": "ChirpMotion source-derived evidence"}
                                  if suffix == "svg" else
                                  {"Software": "ChirpMotion source-derived evidence"}))
        plt.close(fig)

    path = out / "screening-phone.svg"
    ET.register_namespace("", "http://www.w3.org/2000/svg")
    root = ET.fromstring(path.read_bytes())
    ns = "{http://www.w3.org/2000/svg}"
    title = ET.Element(ns + "title", {"id": "phone-title"})
    title.text = "A peak is not yet a path: generated ChirpMotion controls"
    desc = ET.Element(ns + "desc", {"id": "phone-desc"})
    desc.text = ("All 961 raw signed PCM16 samples and all 513 unnormalized spectrum bins "
                 "per generated case. One component is usable at 298.828125 Hz. "
                 "Competing components are ambiguous with null delay and path. "
                 "The declared start is supplied; this is heuristic screening, not physical calibration.")
    root.insert(0, title)
    root.insert(1, desc)
    root.set("role", "img")
    root.set("aria-labelledby", "phone-title phone-desc")
    for node in root.iter():
        if node.tag in (ns + "text", ns + "tspan"):
            node.set("font-family", "DejaVu Sans, Arial, Helvetica, sans-serif")
    path.write_bytes(ET.tostring(root, encoding="utf-8", xml_declaration=True))
    result = {
        "source_commit": manifest["source_commit"],
        "input_manifest_sha256": sha256(data / "manifest.json"),
        "renderer_sha256": sha256(Path(__file__).resolve()),
        "runtime": {"python": platform.python_version(), "numpy": np.__version__,
                    "matplotlib": mpl.__version__, "backend": "Agg"},
        "logical_canvas": {"width": WIDTH, "height": HEIGHT},
        "minimum_font_units": min(item["font_units"] for item in text_boxes),
        "effective_minimum_font_css_px": {str(w): min(item["font_units"] for item in text_boxes) * w / WIDTH for w in (238, 254, 324)},
        "encoded_samples_per_input": 961, "encoded_spectrum_vertices_per_case": 513,
        "transformations": ["PCM16LE decoded as raw signed counts",
                            "time is sample index / 48000 * 1000 in milliseconds",
                            "all stored bins plotted without normalization, smoothing, filtering or path simplification"],
        "uncertainty": "None estimated; two generated controls, not physical observations or confidence.",
        "text_boxes": text_boxes,
        "figures": {name: {"bytes": (out / name).stat().st_size, "sha256": sha256(out / name)}
                    for name in ("screening-phone.svg", "screening-phone.png")},
    }
    (out / "screening-phone-manifest.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({key: value for key, value in result.items() if key != "text_boxes"}, indent=2))


if __name__ == "__main__":
    main()
