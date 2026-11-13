#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Particle stage-0 report: one row per capture.

Inputs per capture (see README.md):
  --zones   tracy-csvexport aggregate CSV of the capture (statistics per zone)
  --frames  tracy-csvexport -u -f "df MainLoop" CSV (one row per frame)
  --log     viewer log with "ParticleProbe:" lines (one per second), optionally --log-tail N lines

Zone columns are read by header name (name, total_ns, counts), so a different column order is fine.
Output is a Markdown table row set on stdout (or --csv FILE).
"""
import argparse, csv, re, statistics, sys

ZONES = [  # (label, [names to match, case-insensitive; first hit wins])
    ("simulate", ["FTM_SIMULATE_PARTICLES", "Simulate Particles"]),
    ("put", ["part put"]),
    ("updateGeometry", ["LLVOPartGroup::updateGeometry"]),
    ("rebuildGeom", ["LLParticlePartition::rebuildGeom"]),
    ("getGeometry", ["LLParticlePartition::getGeometry"]),
    ("sort", ["part sort"]),
    ("upload", ["part upload"]),
    ("push batch", ["ra - push batch"]),
    ("glBufferSubData", ["glBufferSubData"]),
]
LOG_KEYS = ["parts", "parts_max", "groups", "puts", "put_scanned", "groups_created_s", "groups_deleted_s",
            "sgroups_rebuilt", "faces", "drawinfos", "fill_us", "drawinfo_us", "upload_us", "upload_bytes",
            "upload_buffers"]


def read_zones(path):
    out = {}
    with open(path, newline="", encoding="utf-8", errors="replace") as fh:
        for row in csv.DictReader(fh):
            out[row["name"].strip().lower()] = (float(row["total_ns"]), int(row["counts"]))
    return out


def read_frames(path):
    vals = []
    with open(path, newline="", encoding="utf-8", errors="replace") as fh:
        for row in csv.DictReader(fh):
            vals.append(float(row["exec_time_ns"]) / 1e6)
    return vals


def read_log(path, tail):
    rows = []
    pat = re.compile(r"(\w+)=([-+0-9.eE]+)")
    with open(path, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            if "ParticleProbe:" in line:
                rows.append({k: float(v) for k, v in pat.findall(line.split("ParticleProbe:", 1)[1])})
    if tail:
        rows = rows[-tail:]
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--name", required=True, help="capture label, e.g. S0-D-still")
    ap.add_argument("--zones", required=True)
    ap.add_argument("--frames", required=True)
    ap.add_argument("--log", required=True)
    ap.add_argument("--log-tail", type=int, default=60, help="use the last N ParticleProbe lines (default 60 = 60 s)")
    ap.add_argument("--csv", help="append a CSV row to this file instead of printing Markdown")
    a = ap.parse_args()

    zones = read_zones(a.zones)
    frames = read_frames(a.frames)
    if not frames:
        sys.exit("no frames in " + a.frames)
    nf = len(frames)
    srt = sorted(frames)
    res = {"capture": a.name, "frames": nf, "mean_frame_ms": statistics.fmean(frames),
           "p99_frame_ms": srt[min(nf - 1, int(nf * 0.99))]}
    for label, names in ZONES:
        hit = next((zones[n.lower()] for n in names if n.lower() in zones), None)
        res[label + "_ms"] = hit[0] / nf / 1e6 if hit else float("nan")
        res[label + "_calls"] = hit[1] / nf if hit else float("nan")
    lg = read_log(a.log, a.log_tail)
    for k in LOG_KEYS:
        vals = [r[k] for r in lg if k in r]
        if k == "parts_max":
            res[k] = max(vals) if vals else float("nan")
        elif k.endswith("_s"):
            res[k] = statistics.fmean(vals) if vals else float("nan")   # per second
        else:
            res[k] = statistics.fmean(vals) if vals else float("nan")   # per frame
    for k in ("fill_us", "drawinfo_us", "upload_us"):
        res[k.replace("_us", "_ms")] = res[k] / 1000.0
    cpu = sum(res[k] for k in ("updateGeometry_ms", "rebuildGeom_ms") if res[k] == res[k]) + res["simulate_ms"]
    res["particle_cpu_ms"] = cpu
    res["particle_share_of_frame_pct"] = 100.0 * cpu / res["mean_frame_ms"]
    res["draw_share_of_particle_pct"] = 100.0 * res["push batch_ms"] / (cpu + res["push batch_ms"]) \
        if res["push batch_ms"] == res["push batch_ms"] else float("nan")

    if a.csv:
        new = False
        try:
            open(a.csv).close()
        except OSError:
            new = True
        with open(a.csv, "a", newline="") as fh:
            w = csv.DictWriter(fh, fieldnames=list(res))
            if new:
                w.writeheader()
            w.writerow(res)
    else:
        for k, v in res.items():
            print("| %-30s | %s |" % (k, ("%.4f" % v) if isinstance(v, float) else v))


if __name__ == "__main__":
    main()
