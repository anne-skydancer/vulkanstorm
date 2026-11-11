# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Report on a perf_check.py run folder (or on one window JSON written by GLSubmitStats.dump).

  python analyze_perf_check.py RUNDIR                 -> RUNDIR/report.txt (also printed)
  python analyze_perf_check.py RUNDIR --compare BASE  -> the totals of each preset against the same preset of BASE
                                                         (delta = RUNDIR minus BASE)

All numbers are per-frame means over the window. Counters are exclusive (attributed to the innermost view and
pass); cpu_ms and gpu_ms are inclusive of nested passes. Offline: needs only the standard library.
"""

import argparse
import json
import pathlib
import sys

TOTAL_COLUMNS = [
    ("draws", "draws"), ("drawTriangles", "tris"), ("vbChanges", "vbo chg"), ("attribRespecs", "attr resp"),
    ("iboBinds", "ibo"), ("shaderBinds", "shader"), ("texBindFast", "texFast"), ("texBindFastRedundant", "texRed"),
    ("texGLBind", "glBindTex"), ("uniformGL", "uni gl"), ("uniformBytes", "uni B"), ("paletteUploads", "palette"),
    ("paletteDistinct", "pal dist"), ("mdvSyncs", "mdv sync"), ("bufSubData", "bufSub"), ("stateToggles", "state"),
    ("occBlocking", "occ blk"), ("runs", "runs"), ("mergeableDraws", "mergeable"), ("cpu_ms", "cpu ms"), ("gpu_ms", "gpu ms"),
]


def load(path):
    p = pathlib.Path(path)
    if p.is_dir():
        idx = json.loads((p / "perf.json").read_text(encoding="utf-8"))
        out = {}
        for pid, info in idx.get("presets", {}).items():
            f = p / info.get("window_file", "window_%s.json" % pid)
            if f.exists():
                out[pid] = {"window": json.loads(f.read_text(encoding="utf-8")), "info": info}
        return out, idx
    doc = json.loads(p.read_text(encoding="utf-8"))
    return {p.stem: {"window": doc, "info": {}}}, {}


def num(v):
    return 0.0 if v is None else float(v)


def fmt(v):
    if v is None:
        return "-"
    v = float(v)
    if abs(v) >= 1000:
        return "%.0f" % v
    if abs(v) >= 10:
        return "%.1f" % v
    return "%.2f" % v


def table(rows, header):
    widths = [max(len(str(r[i])) for r in [header] + rows) for i in range(len(header))]
    lines = ["  ".join(str(h).rjust(w) if i else str(h).ljust(w) for i, (h, w) in enumerate(zip(header, widths)))]
    for r in rows:
        lines.append("  ".join(str(c).rjust(w) if i else str(c).ljust(w) for i, (c, w) in enumerate(zip(r, widths))))
    return "\n".join(lines)


def ratios(t):
    d = num(t.get("draws"))
    if not d:
        return []
    r = [("vertex buffer changes per draw", num(t.get("vbChanges")) / d),
         ("attribute respecifications per draw", num(t.get("attribRespecs")) / d),
         ("glBindTexture per draw", num(t.get("texGLBind")) / d),
         ("uniform uploads reaching GL per draw", num(t.get("uniformGL")) / d),
         ("GL state toggles per draw", num(t.get("stateToggles")) / d)]
    fast = num(t.get("texBindFast"))
    if fast:
        r.append(("redundant share of bindFast", num(t.get("texBindFastRedundant")) / fast))
    runs = num(t.get("runs"))
    if runs:
        r.append(("mean mergeable run length (draws per run)", d / runs))
        r.append(("share of draws that could join the previous draw", num(t.get("mergeableDraws")) / d))
    return r


def report_preset(pid, entry):
    w = entry["window"]
    t = w.get("totals", {})
    lines = ["=" * 100, "preset %s: %s frames (%s with GPU time)" % (pid, w.get("frames"), w.get("gpu_frames")), ""]
    lines.append(table([[label, fmt(t.get(key))] for key, label in TOTAL_COLUMNS], ["per frame", "mean"]))
    lines.append("")
    lines.append("ratios (what each draw costs the driver today):")
    for label, val in ratios(t):
        lines.append("  %-55s %s" % (label, fmt(val)))
    hist = t.get("run_hist")
    if hist:
        lines.append("  mergeable run lengths (1, 2, 3-4, 5-8, 9-16, 17+ draws): " + ", ".join(fmt(h) for h in hist))
    lines.append("")
    views = w.get("views", [])
    lines.append("views (counters exclusive, times inclusive):")
    lines.append(table([[v.get("view"), fmt(v.get("calls")), fmt(v.get("draws")), fmt(v.get("cpu_ms")), fmt(v.get("gpu_ms")),
                         fmt(v.get("vbChanges")), fmt(v.get("uniformGL")), fmt(v.get("paletteUploads"))] for v in views],
                       ["view", "calls", "draws", "cpu ms", "gpu ms", "vbo chg", "uni gl", "palette"]))
    lines.append("")
    passes = [p for p in w.get("passes", []) if p.get("pass")]
    top = sorted(passes, key=lambda p: -num(p.get("gpu_ms")) if p.get("gpu_ms") is not None else -num(p.get("cpu_ms")))[:25]
    lines.append("passes by GPU ms (CPU ms if no GPU time), top %d of %d:" % (len(top), len(passes)))
    lines.append(table([[p.get("view"), p.get("pass"), fmt(p.get("calls")), fmt(p.get("draws")), fmt(p.get("cpu_ms")),
                         fmt(p.get("gpu_ms")), fmt(p.get("vbChanges")), fmt(p.get("uniformGL")), fmt(p.get("runs"))] for p in top],
                       ["view", "pass", "calls", "draws", "cpu ms", "gpu ms", "vbo chg", "uni gl", "runs"]))
    return lines


def report_compare(idx):
    if not idx:
        return []
    lines = ["=" * 100, "identical-image check"]
    proof = idx.get("proof")
    if proof is not None:
        lines.append("  proof (one bit flipped in the second G-buffer readback): %s" % ("CAUGHT" if proof.get("ok") else "NOT CAUGHT, the check is not trustworthy"))
    for pid, info in idx.get("presets", {}).items():
        for p, results in (info.get("compare") or {}).items():
            for r in results:
                state = "IDENTICAL" if r.get("identical") else ("not run: %s" % r.get("note", "") if not r.get("ran") else "DIFFERENT")
                lines.append("  %-10s pass %-3s %-16s %s" % (pid, p, r.get("label"), state))
                for a in r.get("attachments", []):
                    if a.get("mismatched_pixels"):
                        lines.append("      %s: %s of %s pixels, max diff %s, first at (%s, %s)" % (
                            a.get("name"), a.get("mismatched_pixels"), a.get("pixels"), a.get("max_diff"), a.get("first_x"), a.get("first_y")))
    return lines


def report_delta(new, base):
    lines = ["=" * 100, "against the baseline run (delta = this run minus baseline)"]
    for pid, entry in new.items():
        if pid not in base:
            lines.append("preset %s: not in the baseline" % pid)
            continue
        a, b = entry["window"].get("totals", {}), base[pid]["window"].get("totals", {})
        rows = []
        for key, label in TOTAL_COLUMNS:
            x, y = num(a.get(key)), num(b.get(key))
            rel = "%+.1f%%" % (100.0 * (x - y) / y) if y else "-"
            rows.append([label, fmt(y), fmt(x), fmt(x - y), rel])
        lines.append("preset %s" % pid)
        lines.append(table(rows, ["per frame", "baseline", "this", "delta", "rel"]))
        lines.append("")
    return lines


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("run", help="run folder of perf_check.py, or one window JSON")
    ap.add_argument("--compare", default=None, help="a baseline run folder (or window JSON) to put the totals against")
    args = ap.parse_args(argv)
    entries, idx = load(args.run)
    if not entries:
        print("no window JSON found in %s" % args.run, file=sys.stderr)
        return 1
    lines = []
    for pid, entry in entries.items():
        lines += report_preset(pid, entry)
    lines += report_compare(idx)
    if args.compare:
        base, _ = load(args.compare)
        lines += report_delta(entries, base)
    text = "\n".join(lines) + "\n"
    print(text)
    p = pathlib.Path(args.run)
    if p.is_dir():
        (p / "report.txt").write_text(text, encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
