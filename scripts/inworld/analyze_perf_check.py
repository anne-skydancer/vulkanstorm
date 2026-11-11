# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Report on a perf_check.py run folder (or on one window JSON written by GLSubmitStats.dump).

  python analyze_perf_check.py RUNDIR                 -> RUNDIR/report.txt (also printed)
  (a run with "variants", e.g. alpha_crowd.json, also gets the sorted alpha vs stochastic table and the verdict)
  python analyze_perf_check.py RUNDIR --compare BASE  -> the totals of each preset against the same preset of BASE
                                                         (delta = RUNDIR minus BASE)

All numbers are per-frame means over the window. Counters are exclusive (attributed to the innermost view and
pass); cpu_ms and gpu_ms are inclusive of nested passes. Offline: needs only the standard library.
"""

import argparse
import json
import pathlib
import re
import statistics
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


# -- sorted alpha (RenderAlphaSortMethod 0) against stochastic (1), from a run made with a scenario that has variants -------------
METHOD_KEY = "RenderAlphaSortMethod"
SLOWER_PCT = 3.0       # stochastic counts as slower when its median frame time is more than this above method 0 ...
EXPLAINS_SHARE = 0.5   # ... and the alpha passes' CPU time delta is at least this share of the frame time delta
ALPHA_PASS = re.compile(r"(^|/)ALPHA(_PRE_WATER|_POST_WATER)?( \d+)?$")
ALPHA_COLUMNS = [("cpu_ms", "alpha cpu ms"), ("gpu_ms", "alpha gpu ms"), ("draws", "alpha draws"), ("shaderBinds", "alpha shader binds"),
                 ("paletteUploads", "alpha palette uploads"), ("uniformBytes", "alpha uniform B"), ("samples", "alpha samples")]


def frame_ms(info):
    """Frame time of a window in ms: from the frame rate with nothing collecting, else from the collecting window."""
    fps = info.get("fps_stats_off") or info.get("fps_wall")
    return 1000.0 / float(fps) if fps else None


def alpha_sums(window):
    """Per-frame sums over the alpha passes of the main view (these passes do not nest in each other); None if none found.
    A key that no alpha pass reports is left out (the dump drops zero counters; 'samples' is absent when not measured)."""
    out = {}
    found = False
    for p in window.get("passes", []):
        if p.get("view") == "main" and ALPHA_PASS.search(p.get("pass") or ""):
            found = True
            for key, _ in ALPHA_COLUMNS:
                if key in p:
                    out[key] = out.get(key, 0.0) + num(p.get(key))
    return out if found else None


def main_view(window):
    for v in window.get("views", []):
        if v.get("view") == "main":
            return v
    return {}


def method_of(info):
    val = (info.get("variant_settings") or {}).get(METHOD_KEY)
    return None if val is None else int(val)


def method_runs(entries):
    """{preset: {method: entry}} for the entries that carry a variant with a RenderAlphaSortMethod setting."""
    out = {}
    for entry in entries.values():
        info = entry["info"]
        m = method_of(info)
        if info.get("variant") is not None and m is not None:
            out.setdefault(info.get("preset"), {})[m] = entry
    return out


def median(vals):
    vals = [v for v in vals if v is not None]
    return statistics.median(vals) if vals else None


def paired(m, getter):
    """getter(method 1 entry) - getter(method 0 entry) for one preset, None when either is missing."""
    a, b = getter(m[0]), getter(m[1])
    return None if a is None or b is None else b - a


def alpha_value(key):
    def get(e):
        a = alpha_sums(e["window"])
        return None if a is None or key not in a else a[key]
    return get


def main_gpu(e):
    v = main_view(e["window"]).get("gpu_ms")
    return None if v is None else float(v)


def method_warnings(both):
    """Presets in which method 1 looks like it did not run stochastic: its alpha draws are 0 or the same as method 0's."""
    out = []
    for pid, m in both.items():
        d0 = (alpha_sums(m[0]["window"]) or {}).get("draws", 0.0)
        d1 = (alpha_sums(m[1]["window"]) or {}).get("draws", 0.0)
        if d1 == 0 or (d0 and d1 == d0):
            out.append("preset %s: method 1 has %s alpha draws against %s for method 0; check that stochastic transparency was "
                       "really in use (RenderAlphaSortMethod=1 needs OpenGL 4 and the stochastic path enabled)" % (
                           pid, fmt(d1), fmt(d0)))
    return out


def verdict(runs):
    """The survey's rule over the presets that have both methods. Per preset the paired delta (method 1 minus method 0) of the
    frame time and of the alpha passes' CPU time; the medians of the deltas decide. Stochastic is slower when the median frame
    time delta is more than SLOWER_PCT of the median sorted frame time and the median alpha CPU delta is at least
    EXPLAINS_SHARE of it. The result is 'not_slower', 'slower_explained' (by the alpha pass CPU time), 'slower_not_explained',
    'inconclusive' (the presets disagree in sign) or 'no_verdict'. Returns (text, facts)."""
    both = {pid: m for pid, m in runs.items() if 0 in m and 1 in m}
    if not both:
        return "no preset has both methods: no verdict", {"result": "no_verdict"}
    fts0 = {pid: frame_ms(m[0]["info"]) for pid, m in both.items()}
    fts1 = {pid: frame_ms(m[1]["info"]) for pid, m in both.items()}
    ids = [pid for pid in both if fts0[pid] and fts1[pid]]
    if not ids:
        return "no frame rate in the windows: no verdict", {"result": "no_verdict"}
    d_ft = {pid: fts1[pid] - fts0[pid] for pid in ids}
    ft0 = median([fts0[pid] for pid in ids])
    med_d = median(list(d_ft.values()))
    pcts = [100.0 * d_ft[pid] / fts0[pid] for pid in ids]
    pct = round(100.0 * med_d / ft0, 6)
    cpu_delta = median([paired(both[pid], alpha_value("cpu_ms")) for pid in ids])
    gpu_main = median([paired(both[pid], main_gpu) for pid in ids])
    gpu_alpha = median([paired(both[pid], alpha_value("gpu_ms")) for pid in ids])
    facts = {"frame_ms_0": ft0, "frame_delta_ms": med_d, "frame_pct": pct, "alpha_cpu_delta_ms": cpu_delta, "main_gpu_delta_ms": gpu_main,
             "alpha_gpu_delta_ms": gpu_alpha, "presets": len(ids), "pct_min": min(pcts), "pct_max": max(pcts),
             "warnings": method_warnings(both)}
    spread = "per preset %+.1f%% to %+.1f%%" % (min(pcts), max(pcts))
    gpu = "main view GPU %s ms, alpha passes GPU %s ms (median deltas)" % (
        "n/a" if gpu_main is None else "%+.2f" % gpu_main, "n/a" if gpu_alpha is None else "%+.2f" % gpu_alpha)
    head = "median frame time delta %+.2f ms on %.2f ms (%+.1f%%, limit +%.0f%%; %s; %d presets)" % (
        med_d, ft0, pct, SLOWER_PCT, spread, len(ids))
    if pct <= SLOWER_PCT:
        return "stochastic is NOT slower: %s; %s" % (head, gpu), dict(facts, result="not_slower")
    if len(ids) > 1 and min(d_ft.values()) < 0 < max(d_ft.values()):
        return ("INCONCLUSIVE: %s; the presets disagree in sign, so the median is not a finding; %s" % (head, gpu),
                dict(facts, result="inconclusive"))
    if cpu_delta is None:
        return ("stochastic is slower but the alpha passes were not found in the windows, so the alpha pass CPU time cannot be "
                "checked: %s; %s" % (head, gpu), dict(facts, result="slower_not_explained"))
    explains = cpu_delta >= EXPLAINS_SHARE * med_d
    return ("stochastic is %s: %s; the alpha passes' CPU time changes by %+.2f ms (median), which %s (limit: at least half of the "
            "frame time delta); %s" % (
                "SLOWER in this crowd" if explains else "slower but NOT because of the alpha pass CPU time", head, cpu_delta,
                "explains it" if explains else "does not explain it", gpu),
            dict(facts, result="slower_explained" if explains else "slower_not_explained"))


def report_methods(entries):
    runs = method_runs(entries)
    if not runs:
        return []
    lines = ["=" * 100, "sorted alpha (method 0) against stochastic (method 1), per preset (medians over the presets below)",
             "frame ms = 1000 / fps with nothing collecting (the clean figure: the sample queries can perturb early-Z while collecting);",
             "alpha columns are the main view's ALPHA passes, per frame; '-' = not reported", ""]
    head = ["preset", "method", "frame ms", "main cpu", "main gpu", "draws", "shader"] + [lab for _, lab in ALPHA_COLUMNS]
    rows = []
    for pid, m in runs.items():
        for method in sorted(m):
            e = m[method]
            a = alpha_sums(e["window"]) or {}
            mv = main_view(e["window"])
            t = e["window"].get("totals", {})
            rows.append([pid, method, fmt(frame_ms(e["info"])), fmt(mv.get("cpu_ms")), fmt(mv.get("gpu_ms")), fmt(t.get("draws")),
                         fmt(t.get("shaderBinds"))] + [fmt(a.get(k)) for k, _ in ALPHA_COLUMNS])
    lines.append(table(rows, head))
    lines.append("")
    lines.append("median over the presets:")
    med = []
    for method in (0, 1):
        es = [m[method] for m in runs.values() if method in m]
        if not es:
            continue
        alphas = [alpha_sums(e["window"]) or {} for e in es]
        med.append(["", method, fmt(median([frame_ms(e["info"]) for e in es])),
                    fmt(median([main_view(e["window"]).get("cpu_ms") for e in es])),
                    fmt(median([main_view(e["window"]).get("gpu_ms") for e in es])),
                    fmt(median([e["window"].get("totals", {}).get("draws") for e in es])),
                    fmt(median([e["window"].get("totals", {}).get("shaderBinds") for e in es]))] +
                   [fmt(median([a.get(k) for a in alphas])) for k, _ in ALPHA_COLUMNS])
    lines.append(table(med, head))
    lines.append("")
    text, facts = verdict(runs)
    lines.append("verdict (stochastic is slower when its median frame time is more than %.0f%% above sorted and the alpha pass "
                 "CPU time explains it, i.e. is at least half of the frame time delta):" % SLOWER_PCT)
    lines.append("  [%s] %s" % (facts.get("result"), text))
    for w in facts.get("warnings", []):
        lines.append("  WARNING " + w)
    lines.append("  The ms figures come from one GPU. Only method 0's counters (draws, shader binds, palette uploads, uniform bytes,")
    lines.append("  alpha samples) are the gate for weak GPUs: method 1 draws other passes into multisample targets, so its samples are")
    lines.append("  not comparable to method 0's. Alpha samples are GL_SAMPLES_PASSED (samples, not fragments).")
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
    lines += report_methods(entries)
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
