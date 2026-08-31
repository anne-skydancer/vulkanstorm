#!/usr/bin/env python3
"""Per-pixel diff harness for the GL<->Vulkan render-parity checks.

Compares two raw RGBA8 frame captures (8-byte little-endian width/height header
followed by w*h*4 bytes) produced by the viewer's capture hooks:
  - Vulkan:  VULKANSTORM_CAPTURE=1    -> logs/vulkan_capture.rgba
  - OpenGL:  VULKANSTORM_CAPTURE_GL=1 -> logs/gl_reference_capture.rgba

Usage:
  python fsutils/vulkan_frame_diff.py <a.rgba> <b.rgba> [--max-delta N] [--write-diff out.png]

Exit code 0 when the frames match within tolerance (default: byte-exact),
1 otherwise. Prints a summary plus the first few differing pixels.
"""

import argparse
import struct
import sys


def load(path):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 8:
        raise ValueError(f"{path}: too small ({len(data)} bytes)")
    w, h = struct.unpack_from("<II", data, 0)
    expect = 8 + w * h * 4
    if len(data) != expect:
        raise ValueError(f"{path}: size {len(data)} != expected {expect} ({w}x{h})")
    return w, h, data[8:]


def main():
    ap = argparse.ArgumentParser(
        description=(
            "Per-pixel diff of two RGBA8 frame captures. Parity policy: opaque "
            "content must be byte-exact (--max-delta 0); alpha-blended content may "
            "differ by up to 1 per channel (--mode alpha) because the GL and Vulkan "
            "fixed-function blend units round fixed-point blends differently."
        ))
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--mode", choices=["opaque", "alpha"], default="opaque",
                    help="parity policy: 'opaque' = byte-exact (tol 0); "
                         "'alpha' = blended content (tol 1). Sets --max-delta.")
    ap.add_argument("--max-delta", type=int, default=None,
                    help="max per-channel delta tolerated (overrides --mode)")
    ap.add_argument("--write-diff", metavar="PNG", default=None,
                    help="write a heatmap PNG of per-pixel deltas (requires Pillow)")
    args = ap.parse_args()

    # Resolve tolerance: explicit --max-delta wins; otherwise the mode's policy.
    if args.max_delta is None:
        args.max_delta = 0 if args.mode == "opaque" else 1

    try:
        aw, ah, a = load(args.a)
        bw, bh, b = load(args.b)
    except ValueError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 1

    if (aw, ah) != (bw, bh):
        print(f"DIMENSION MISMATCH: {aw}x{ah} vs {bw}x{bh}")
        return 1

    total = aw * ah
    identical = 0
    diff_count = 0
    max_delta = 0
    samples = []
    diff_map = bytearray(total * 4) if args.write_diff else None

    for px in range(total):
        o = px * 4
        dr = abs(a[o] - b[o])
        dg = abs(a[o + 1] - b[o + 1])
        db = abs(a[o + 2] - b[o + 2])
        da = abs(a[o + 3] - b[o + 3])
        md = max(dr, dg, db, da)
        if md == 0:
            identical += 1
        else:
            diff_count += 1
            if md > max_delta:
                max_delta = md
            if len(samples) < 8:
                samples.append(
                    f"  ({px % aw},{px // aw}) A=({a[o]},{a[o+1]},{a[o+2]},{a[o+3]}) "
                    f"B=({b[o]},{b[o+1]},{b[o+2]},{b[o+3]})")
            if diff_map is not None:
                v = min(255, md * 4)
                diff_map[o:o + 4] = bytes((v, 0, 0, 255))

    pct = 100.0 * diff_count / total
    within = max_delta <= args.max_delta
    print(f"dimensions: {aw}x{ah}")
    print(f"total pixels:      {total}")
    print(f"identical:         {identical}")
    print(f"differing:         {diff_count} ({pct:.4f}%)")
    print(f"max channel delta: {max_delta}")
    print(f"tolerance:         {args.max_delta} (mode={args.mode}) -> {'PASS' if within else 'FAIL'}")
    if samples:
        print("first differing pixels (A=first file, B=second file):")
        for s in samples:
            print(s)

    if args.write_diff and diff_map is not None:
        try:
            from PIL import Image
            Image.frombytes("RGBA", (aw, ah), bytes(diff_map)).save(args.write_diff)
            print(f"diff heatmap written to {args.write_diff}")
        except ImportError:
            print("(Pillow not installed; skipping --write-diff)")

    return 0 if within else 1


if __name__ == "__main__":
    sys.exit(main())
