# SPDX-License-Identifier: GPL-3.0-or-later
"""Copy the mesh assets of a viewer disk cache into a scratch corpus folder.

The cache is only read: every file is opened read-only and copied byte for
byte. A file is a mesh asset when it starts with the mesh repository's cache
preamble (U32 version 1, U32 header size, U32 flags) followed by a binary
LLSD map. Nothing from the corpus belongs in git: point --out at the repo's
.fixture-tmp folder.

Usage: python -I copy_mesh_corpus.py --cache <viewer cache dir> --out <dir>
"""
import argparse
import os
import shutil
import struct
import sys


def is_mesh_asset(head: bytes) -> bool:
    if len(head) < 13:
        return False
    version, header_size, _flags = struct.unpack_from("<III", head, 0)
    if version != 1 or header_size <= 0:
        return False
    body = head[12:]
    return body.startswith(b"{") or body.startswith(b"<? LLSD/Binary ?>")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--cache", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    cache = os.path.realpath(args.cache)
    out = os.path.realpath(args.out)
    if out == cache or out.startswith(cache + os.sep):
        print("refusing: --out lies inside the cache", file=sys.stderr)
        return 2
    os.makedirs(out, exist_ok=True)

    copied = 0
    total = 0
    for root, _dirs, files in os.walk(cache):
        for name in files:
            src = os.path.join(root, name)
            try:
                with open(src, "rb") as fh:
                    head = fh.read(32)
            except OSError:
                continue
            if not is_mesh_asset(head):
                continue
            dst = os.path.join(out, name)
            if os.path.exists(dst) and os.path.getsize(dst) == os.path.getsize(src):
                copied += 1
                total += os.path.getsize(dst)
                continue
            shutil.copyfile(src, dst)
            copied += 1
            total += os.path.getsize(dst)
    print(f"mesh assets: {copied} files, {total / 1e6:.1f} MB -> {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
