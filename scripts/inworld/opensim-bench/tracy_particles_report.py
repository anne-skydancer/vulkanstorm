#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Particle stage-0 report: one row per capture.

Inputs per capture (see README.md):
  --zones   tracy-csvexport aggregate CSV of the capture (columns name,src_file,src_line,total_ns,total_perc,counts,
            mean_ns,min_ns,max_ns,std_ns)
  --frames  tracy-csvexport -u -f "df MainLoop" CSV (one row per frame; column exec_time_ns)
  --log     viewer log with "ParticleProbe:" lines (one per second)
  --log-from / --log-to   ISO UTC times (2026-11-13T04:39:26Z) bounding the capture; if only --log-from is given the
            window is --log-seconds (default 60) long. The probe lines are selected by their own timestamps, so a
            session log that holds earlier captures does not leak into this row.

CSV files are read as utf-8-sig (PowerShell 5.1 redirects write UTF-16: use Git Bash, cmd or PowerShell 7).

Notes on the numbers:
  * fill_ms and drawinfo_ms (from the log) are summed TSC ticks, four rdtsc reads per face. The probe's own overhead
    (about 0.1 ms/frame at 4096 faces) is inside them: they are upper bounds, and the same probe is on the "after"
    build, so before/after differences are unaffected.
  * alpha_push_ms (zone "ra - push batch") wraps every alpha batch in the frame (NPC hair and clothing included), not
    only particles: an upper bound. Subtract the same row of a baseline capture with the emitters stopped.
  * The spawn rate depends on the frame rate (the fractional spawn remainder is dropped), so compare before/after at
    equal mean parts; the *_per_1000 columns give ms per 1000 particles.
"""
import argparse, csv, math, re, statistics, sys
from datetime import datetime, timedelta, timezone

NAN = float("nan")
ZONES = [  # (label, [names to match, case-insensitive; first hit wins])
    ("simulate", ["FTM_SIMULATE_PARTICLES", "Simulate Particles"]),
    ("put", ["part put"]),
    ("updateGeometry", ["LLVOPartGroup::updateGeometry"]),
    ("rebuildGeom", ["LLParticlePartition::rebuildGeom"]),
    ("getGeometry", ["LLParticlePartition::getGeometry"]),
    ("sort", ["part sort"]),
    ("upload_zone", ["part upload"]),
    ("alpha_push", ["ra - push batch"]),
    ("glBufferSubData", ["glBufferSubData"]),
]
LOG_KEYS = ["parts", "parts_max", "groups", "puts", "put_scanned", "groups_created_s", "groups_deleted_s",
            "sgroups_rebuilt", "faces", "drawinfos", "fill_us", "drawinfo_us", "upload_us", "upload_bytes",
            "upload_buffers"]
NUM = re.compile(r"(\w+)=(-?\d+(?:\.\d+)?(?:[eE][-+]?\d+)?)(?=\s|$)")
TS = re.compile(r"^(\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d)Z")


def read_zones(path):
    out = {}
    with open(path, newline="", encoding="utf-8-sig", errors="replace") as fh:
        for row in csv.DictReader(fh):
            out[row["name"].strip().lower()] = (float(row["total_ns"]), int(row["counts"]))
    return out


def read_frames(path):
    with open(path, newline="", encoding="utf-8-sig", errors="replace") as fh:
        return [float(r["exec_time_ns"]) / 1e6 for r in csv.DictReader(fh)]


def parse_iso(s):
    return datetime.strptime(s.rstrip("Z"), "%Y-%m-%dT%H:%M:%S").replace(tzinfo=timezone.utc)


def read_log(path, t0, t1):
    rows = []
    with open(path, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            if "ParticleProbe:" not in line:
                continue
            m = TS.match(line)
            if not m:
                continue
            t = parse_iso(m.group(1))
            if t0 <= t <= t1:
                rows.append({k: float(v) for k, v in NUM.findall(line.split("ParticleProbe:", 1)[1])})
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--name", required=True, help="capture label, e.g. S0-D-still")
    ap.add_argument("--zones", required=True)
    ap.add_argument("--frames", required=True)
    ap.add_argument("--log", required=True)
    ap.add_argument("--log-from", required=True, help="capture start, ISO UTC, e.g. 2026-11-13T04:39:26Z")
    ap.add_argument("--log-to", help="capture end, ISO UTC (default: --log-from + --log-seconds)")
    ap.add_argument("--log-seconds", type=int, default=60)
    ap.add_argument("--csv", help="append a CSV row to this file instead of printing Markdown")
    a = ap.parse_args()

    t0 = parse_iso(a.log_from)
    t1 = parse_iso(a.log_to) if a.log_to else t0 + timedelta(seconds=a.log_seconds)
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
        if hit is None:
            print("WARNING: zone not found in %s: %s (value is NaN)" % (a.zones, " / ".join(names)), file=sys.stderr)
        res[label + "_ms"] = hit[0] / nf / 1e6 if hit else NAN
        res[label + "_calls"] = hit[1] / nf if hit else NAN
    lg = read_log(a.log, t0, t1)
    if not lg:
        print("WARNING: no ParticleProbe lines between %s and %s in %s" % (t0, t1, a.log), file=sys.stderr)
    res["log_lines"] = len(lg)
    for k in LOG_KEYS:
        vals = [r[k] for r in lg if k in r]
        if not vals:
            res[k] = NAN
        elif k == "parts_max":
            res[k] = max(vals)
        else:
            res[k] = statistics.fmean(vals)   # per frame; groups_created_s and groups_deleted_s are per second
    for k in ("fill", "drawinfo", "upload"):
        res[k + "_ms"] = res[k + "_us"] / 1000.0   # from the log (upload_zone_ms is the Tracy zone)

    # update = spawn + particle update without the group scan (survey 9.7)
    res["update_ms"] = res["simulate_ms"] - res["put_ms"]
    parts_needed = ("simulate_ms", "updateGeometry_ms", "rebuildGeom_ms")
    res["particle_cpu_ms"] = sum(res[k] for k in parts_needed)          # NaN if any zone is missing
    res["pooled_rule_ms"] = res["update_ms"] + res["updateGeometry_ms"] + res["rebuildGeom_ms"]  # >= 1.0 -> go
    res["fill_upload_ms"] = res["fill_ms"] + res["upload_zone_ms"]       # instancing reading
    res["particle_share_of_frame_pct"] = 100.0 * res["particle_cpu_ms"] / res["mean_frame_ms"]
    p = res["parts"]
    for k in ("particle_cpu_ms", "fill_ms", "upload_zone_ms", "alpha_push_ms"):
        res[k.replace("_ms", "") + "_per_1000"] = res[k] / (p / 1000.0) if p and p == p else NAN

    if a.csv:
        try:
            with open(a.csv, newline="", encoding="utf-8-sig") as fh:
                header = next(csv.reader(fh), None)
        except OSError:
            header = None
        if header is not None and header != list(res):
            sys.exit("%s has a different header (report layout changed); use a new file" % a.csv)
        with open(a.csv, "a", newline="", encoding="utf-8") as fh:
            w = csv.DictWriter(fh, fieldnames=list(res))
            if header is None:
                w.writeheader()
            w.writerow(res)
    else:
        print("| metric | value |")
        print("|---|---|")
        for k, v in res.items():
            print("| %s | %s |" % (k, ("%.4f" % v) if isinstance(v, float) else v))


if __name__ == "__main__":
    main()
