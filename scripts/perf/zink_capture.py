"""External PresentMon capture and offline per-swapchain analysis (stdlib only)."""
import argparse
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import uuid


def digest(path):
    with open(path, "rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def summarize(path, pid):
    groups = {}
    with open(path, newline="", encoding="utf-8-sig") as stream:
        reader = csv.DictReader(stream)
        canonical = {name.casefold(): name for name in
                     ("ProcessID", "SwapChainAddress", "MsBetweenPresents", "PresentMode")}
        reader.fieldnames = [canonical.get(name.strip().casefold(), name)
                            for name in (reader.fieldnames or [])]
        required = {"ProcessID", "SwapChainAddress", "MsBetweenPresents"}
        if not required.issubset(reader.fieldnames or []):
            raise ValueError("Unsupported CSV: require PresentMon --v1_metrics columns")
        for row in reader:
            if row["ProcessID"] != str(pid):
                continue
            key = row["SwapChainAddress"]
            group = groups.setdefault(key, {"intervals": [], "invalid_intervals": 0,
                                            "rows": 0, "present_modes": {}})
            group["rows"] += 1
            mode = row.get("PresentMode", "unknown")
            group["present_modes"][mode] = group["present_modes"].get(mode, 0) + 1
            try:
                interval = float(row["MsBetweenPresents"])
            except (ValueError, TypeError):
                interval = math.nan
            if not math.isfinite(interval) or interval <= 0:
                group["invalid_intervals"] += 1
                continue
            group["intervals"].append(interval)
    if not groups:
        raise ValueError("No rows for requested PID; capture is not a valid benchmark")
    for group in groups.values():
        values = sorted(group.pop("intervals"))
        group["valid_intervals"] = len(values)
        if not values:
            group["status"] = "no_valid_intervals"
            continue
        group["status"] = "unqualified_measurement"
        group["mean_present_rate_hz"] = 1000 * len(values) / sum(values)
        for name, fraction in (("p50_ms", .5), ("p95_ms", .95), ("p99_ms", .99)):
            group[name] = values[math.ceil(len(values) * fraction) - 1]
        group["max_ms"] = values[-1]
        group["intervals_over_50ms"] = sum(value > 50 for value in values)
    if not any(group["valid_intervals"] for group in groups.values()):
        raise ValueError("No valid intervals; capture is not a valid benchmark")
    return {"metric": "application present intervals, not displayed FPS or GPU pass time",
            "percentile_method": "nearest rank", "process_id": pid,
            "csv_sha256": digest(path), "visual_acceptance": "not_evaluated",
            "streams": groups}


def capture(args):
    if os.name != "nt":
        raise ValueError("Live capture requires Windows; analysis is portable")
    if args.pid <= 0 or not 1 <= args.seconds <= 3600 or not 0 <= args.delay <= 600:
        raise ValueError("Invalid PID, duration (1..3600), or delay (0..600)")
    tool = args.presentmon.resolve(strict=True)
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    required = ("backend", "backend_evidence", "gpu", "driver", "mesa_revision",
                "viewer_revision", "scene", "settings", "cache_state", "scenario")
    if any(not manifest.get(key) for key in required):
        raise ValueError("Manifest requires nonempty fields: " + ", ".join(required))
    if manifest["backend"] not in ("native-opengl", "zink"):
        raise ValueError("backend must be native-opengl or zink")
    # Do not launch, stop, inspect private memory, or change the viewer.
    # The operator verifies the selected PID and records its identity in the manifest.
    args.output.mkdir(parents=True, exist_ok=False)
    output = args.output.resolve()
    command = [str(tool), "--process_id", str(args.pid), "--v1_metrics",
               "--output_file", str(output / "presents.csv"),
               "--delay", str(args.delay), "--timed", str(args.seconds),
               "--terminate_after_timed", "--terminate_on_proc_exit",
               "--no_console_stats", "--no_track_input",
               "--session_name", "Vulkanstorm-" + uuid.uuid4().hex]
    record = {"operator_manifest": manifest, "process_id": args.pid,
              "presentmon_sha256": digest(tool), "command": command,
              "status": "started", "identity_verified_automatically": False}
    record_path = output / "capture.json"
    record_path.write_text(json.dumps(record, indent=2), encoding="utf-8")
    try:
        with (output / "collector.log").open("w", encoding="utf-8") as log:
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT,
                                    creationflags=subprocess.CREATE_NO_WINDOW,
                                    timeout=args.seconds + args.delay + 60, check=False)
        record["returncode"] = result.returncode
        if result.returncode:
            raise ValueError("PresentMon failed; inspect collector.log")
        summary = summarize(output / "presents.csv", args.pid)
        (output / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
        record["status"] = "collected_not_qualified"
    except BaseException:
        record["status"] = "failed_or_interrupted"
        raise
    finally:
        record_path.write_text(json.dumps(record, indent=2), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="action", required=True)
    collect = commands.add_parser("capture")
    collect.add_argument("--presentmon", type=Path, required=True)
    collect.add_argument("--pid", type=int, required=True)
    collect.add_argument("--manifest", type=Path, required=True)
    collect.add_argument("--output", type=Path, required=True)
    collect.add_argument("--seconds", type=int, default=120)
    collect.add_argument("--delay", type=int, default=15)
    analyze = commands.add_parser("analyze")
    analyze.add_argument("csv", type=Path)
    analyze.add_argument("--pid", type=int, required=True)
    args = parser.parse_args()
    try:
        if args.action == "capture":
            capture(args)
        else:
            print(json.dumps(summarize(args.csv, args.pid), indent=2))
    except (ValueError, OSError, subprocess.TimeoutExpired) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    main()
