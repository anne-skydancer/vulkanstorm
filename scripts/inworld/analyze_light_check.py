# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Offline analysis of a light_check.py run folder (manifest.csv, run.json, snapshots/).

Per sky and per sweep step it reports the mean luminance of three regions (ground, the sky band just above
the horizon, a cloud region) and of the whole frame, the largest step-to-step changes (a "pop" is a change whose rate per degree is much
larger than its neighbours'), and writes a contact sheet per sky. Needs Pillow and numpy.

  python analyze_light_check.py RUNDIR [--pop-factor 4] [--pop-floor 0.01] [--compare OTHER_RUNDIR]

Luminance is Rec.709 luma of the sRGB-encoded image, scaled 0..1 (what you see), and the linear-light mean
is given beside it. Output: RUNDIR/analysis.json, analysis_<sky>.csv, contact_<sky>.png, report.txt.
"""

import argparse
import csv
import json
import pathlib
import statistics
import sys

import numpy as np
from PIL import Image, ImageDraw

REGION_NAMES = ["ground", "horizon_sky", "cloud", "frame"]


def srgb_to_linear(a):
    return np.where(a <= 0.04045, a / 12.92, ((a + 0.055) / 1.055) ** 2.4)


def luma(rgb):
    """Rec.709 luma of an HxWx3 float array (0..1)."""
    return 0.2126 * rgb[..., 0] + 0.7152 * rgb[..., 1] + 0.0722 * rgb[..., 2]


def region_slice(shape, box):
    h, w = shape[:2]
    x0, y0, x1, y1 = box
    xa, xb = int(round(x0 * w)), int(round(x1 * w))
    ya, yb = int(round(y0 * h)), int(round(y1 * h))
    if xb <= xa or yb <= ya:
        raise ValueError("empty region %r for an image of %dx%d" % (box, w, h))
    return slice(ya, yb), slice(xa, xb)


def measure(path, regions):
    """Return ({region: (luma, linear_luma)}, small luma image for frame differences)."""
    with Image.open(path) as im:
        rgb = np.asarray(im.convert("RGB"), dtype=np.float64) / 255.0
    y = luma(rgb)
    lin = luma(srgb_to_linear(rgb))
    out = {}
    for name in ("ground", "horizon_sky", "cloud"):
        sl = region_slice(rgb.shape, regions[name])
        out[name] = (float(y[sl].mean()), float(lin[sl].mean()))
    out["frame"] = (float(y.mean()), float(lin.mean()))
    # 64 columns wide luma thumbnail for frame differences
    h, w = y.shape
    step = max(1, w // 64)
    small = y[: h - h % step, : w - w % step].reshape(h // step, step, w // step, step).mean(axis=(1, 3))
    return out, small


def find_pops(elevs, values, factor, floor):
    """Step-to-step changes of `values` along `elevs`. The steps are not all the same size (0.5 degree outside the
    horizon band, 0.1 inside), so a change is judged by its rate per degree: it is flagged when its size is above
    `floor` and its rate is more than `factor` times the median rate of the neighbouring (+-3) steps.
    Returns (changes, flagged)."""
    changes = []
    for i in range(1, len(values)):
        d_el = abs(elevs[i] - elevs[i - 1]) or 1e-9
        delta = values[i] - values[i - 1]
        changes.append({"from_deg": elevs[i - 1], "to_deg": elevs[i], "delta": delta, "rate_per_deg": delta / d_el})
    rates = [abs(c["rate_per_deg"]) for c in changes]
    flagged = []
    for i, c in enumerate(changes):
        lo, hi = max(0, i - 3), min(len(changes), i + 4)
        neigh = [rates[j] for j in range(lo, hi) if j != i]
        med = statistics.median(neigh) if neigh else 0.0
        c["neighbour_median_rate"] = med
        if abs(c["delta"]) > floor and rates[i] > factor * max(med, 1e-9):
            c["flag"] = True
            flagged.append(c)
        else:
            c["flag"] = False
    return changes, flagged


def read_manifest(rundir):
    with open(rundir / "manifest.csv", newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def contact_sheet(rows, rundir, out_path, thumb_w=240, cols=8):
    ims = []
    for r in rows:
        with Image.open(rundir / r["snapshot"]) as im:
            t = im.convert("RGB")
            t.thumbnail((thumb_w, thumb_w))
            ims.append((r, t.copy()))
    if not ims:
        return False
    tw = max(t.width for _, t in ims)
    th = max(t.height for _, t in ims)
    label_h = 14
    n = len(ims)
    nrows = (n + cols - 1) // cols
    sheet = Image.new("RGB", (cols * tw, nrows * (th + label_h)), (24, 24, 24))
    draw = ImageDraw.Draw(sheet)
    for i, (r, t) in enumerate(ims):
        x, y = (i % cols) * tw, (i // cols) * (th + label_h)
        sheet.paste(t, (x, y + label_h))
        tag = "noon" if r["kind"] == "noon" else "%+.2f" % float(r["sun_elevation_deg"])
        draw.text((x + 3, y + 1), "%s %s" % (r["step"], tag), fill=(235, 235, 235))
    sheet.save(out_path)
    return True


def analyse(rundir, factor=4.0, floor=0.01):
    rundir = pathlib.Path(rundir)
    run = json.loads((rundir / "run.json").read_text(encoding="utf-8"))
    regions = run.get("regions")
    if not regions:
        regions = {"ground": [0.05, 0.75, 0.95, 0.95], "horizon_sky": [0.05, 0.50, 0.95, 0.62],
                   "cloud": [0.05, 0.08, 0.95, 0.35]}
    rows = read_manifest(rundir)
    skies = []
    for r in rows:
        if r["sky"] not in skies:
            skies.append(r["sky"])
    result = {"run": str(rundir), "regions": regions, "skies": {}}
    lines = ["Light check analysis of %s" % rundir,
             "regions (x0, y0, x1, y1 as fractions of the image): %s" % {k: [round(x, 3) for x in regions[k]] for k in ("ground", "horizon_sky", "cloud")},
             "pop = a step change above %.3f whose rate per degree is more than %.1fx the median rate of the neighbouring steps" % (floor, factor),
             ""]
    for sky in skies:
        srows = [r for r in rows if r["sky"] == sky]
        sweep = [r for r in srows if r["kind"] == "sweep"]
        series = {n: [] for n in REGION_NAMES}
        series_lin = {n: [] for n in REGION_NAMES}
        smalls = []
        table = []
        for r in srows:
            m, small = measure(rundir / r["snapshot"], regions)
            entry = {"step": int(r["step"]), "kind": r["kind"], "sun_elevation_deg": float(r["sun_elevation_deg"]),
                     "fps": r["fps"], "snapshot": r["snapshot"]}
            for n in REGION_NAMES:
                entry[n] = m[n][0]
                entry[n + "_linear"] = m[n][1]
            table.append(entry)
            if r["kind"] == "sweep":
                for n in REGION_NAMES:
                    series[n].append(m[n][0])
                    series_lin[n].append(m[n][1])
                smalls.append(small)
        elevs = [float(r["sun_elevation_deg"]) for r in sweep]
        sky_res = {"steps": len(sweep), "regions": {}, "pops": [], "frame_diff": None}
        lines.append("== %s (%d snapshots, %d sweep steps)" % (sky, len(srows), len(sweep)))
        for n in REGION_NAMES:
            if len(series[n]) < 2:
                continue
            changes, flagged = find_pops(elevs, series[n], factor, floor)
            top = sorted(changes, key=lambda c: -abs(c["delta"]))[:3]
            sky_res["regions"][n] = {"min": min(series[n]), "max": max(series[n]), "largest_changes": top,
                                     "pops": flagged}
            lines.append("  %-12s luma %.3f .. %.3f; largest step changes: %s" % (
                n, min(series[n]), max(series[n]),
                "; ".join("%+.3f at %.1f -> %.1f%s" % (c["delta"], c["from_deg"], c["to_deg"], " POP" if c["flag"] else "")
                          for c in top)))
            for c in flagged:
                sky_res["pops"].append({"region": n, **c})
        if len(smalls) >= 2:
            diffs = [float(np.abs(smalls[i] - smalls[i - 1]).mean()) for i in range(1, len(smalls))]
            changes, flagged = find_pops(elevs, [0.0] + list(np.cumsum(diffs)), factor, floor)
            order = sorted(range(len(diffs)), key=lambda i: -diffs[i])[:3]
            sky_res["frame_diff"] = {"largest": [{"from_deg": elevs[i], "to_deg": elevs[i + 1], "mean_abs_diff": diffs[i]}
                                                 for i in order],
                                     "pops": [{"from_deg": c["from_deg"], "to_deg": c["to_deg"], "mean_abs_diff": c["delta"]}
                                              for c in flagged]}
            lines.append("  frame diff   largest: %s" % "; ".join(
                "%.4f at %.1f -> %.1f" % (diffs[i], elevs[i], elevs[i + 1]) for i in order))
            for c in flagged:
                lines.append("  POP frame difference %.4f between %.1f and %.1f degrees" % (c["delta"], c["from_deg"], c["to_deg"]))
        for p in sky_res["pops"]:
            lines.append("  POP %s: %+.3f between %.1f and %.1f degrees (rate %.3f per degree against a neighbours' median of %.3f)" % (
                p["region"], p["delta"], p["from_deg"], p["to_deg"], p["rate_per_deg"], p["neighbour_median_rate"]))
        if not sky_res["pops"] and not (sky_res["frame_diff"] or {}).get("pops"):
            lines.append("  no pops flagged")
        with open(rundir / ("analysis_%s.csv" % sky), "w", newline="", encoding="utf-8") as f:
            cols = ["step", "kind", "sun_elevation_deg", "fps", "snapshot"] + REGION_NAMES + [n + "_linear" for n in REGION_NAMES]
            w = csv.DictWriter(f, fieldnames=cols)
            w.writeheader()
            w.writerows(table)
        contact_sheet(srows, rundir, rundir / ("contact_%s.png" % sky))
        sky_res["contact_sheet"] = "contact_%s.png" % sky
        result["skies"][sky] = sky_res
        lines.append("")
    (rundir / "analysis.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    (rundir / "report.txt").write_text("\n".join(lines), encoding="utf-8")
    return result, "\n".join(lines)


def compare(rundir_a, rundir_b):
    """Compare the noon rows of two runs (e.g. the 82080 baseline against #447): luma difference per sky and region."""
    out = ["Noon comparison %s (A) against %s (B)" % (rundir_a, rundir_b)]
    ra = json.loads((pathlib.Path(rundir_a) / "analysis.json").read_text(encoding="utf-8"))
    rb = json.loads((pathlib.Path(rundir_b) / "analysis.json").read_text(encoding="utf-8"))
    for sky in ra["skies"]:
        if sky not in rb["skies"]:
            continue
        ta = {r["step"]: r for r in csv.DictReader(open(pathlib.Path(rundir_a) / ("analysis_%s.csv" % sky), encoding="utf-8"))}
        tb = {r["step"]: r for r in csv.DictReader(open(pathlib.Path(rundir_b) / ("analysis_%s.csv" % sky), encoding="utf-8"))}
        for step in sorted(set(ta) & set(tb), key=int):
            if ta[step]["kind"] != "noon":
                continue
            diffs = ["%s %+.4f" % (n, float(tb[step][n]) - float(ta[step][n])) for n in REGION_NAMES]
            out.append("  %s noon: %s" % (sky, ", ".join(diffs)))
    return "\n".join(out)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("rundir")
    ap.add_argument("--pop-factor", type=float, default=4.0)
    ap.add_argument("--pop-floor", type=float, default=0.01)
    ap.add_argument("--compare", default=None, help="another analysed run folder to compare the noon snapshots with")
    args = ap.parse_args(argv)
    _, text = analyse(args.rundir, args.pop_factor, args.pop_floor)
    print(text)
    if args.compare:
        print(compare(args.rundir, args.compare))
    return 0


if __name__ == "__main__":
    sys.exit(main())
