# SPDX-License-Identifier: GPL-3.0-or-later
"""Fetch the pinned libdeflate release and unpack it into a new directory.

The tarball is the release asset of the official repository
(github.com/ebiggers/libdeflate). Its SHA-256 is pinned here and matches the
digest GitHub publishes for the asset. The unpacked tree is third-party data:
nothing from it is committed, and it is only compiled by build_bench.cmd.

Usage: python -I fetch_libdeflate.py --out <new empty dir>
"""
import argparse
import hashlib
import os
import sys
import tarfile
import urllib.request

VERSION = "1.26"
URL = f"https://github.com/ebiggers/libdeflate/releases/download/v{VERSION}/libdeflate-{VERSION}.tar.gz"
SHA256 = "125856d4656e0feab660f94842f835923410c9281fedbcee64598c918da42b5a"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    args = ap.parse_args()
    out = os.path.realpath(args.out)
    if os.path.exists(out) and os.listdir(out):
        print(f"refusing: {out} is not empty", file=sys.stderr)
        return 2
    os.makedirs(out, exist_ok=True)

    with urllib.request.urlopen(URL) as resp:
        data = resp.read()
    digest = hashlib.sha256(data).hexdigest()
    if digest != SHA256:
        print(f"SHA-256 mismatch: got {digest}, want {SHA256}", file=sys.stderr)
        return 1
    tar_path = os.path.join(out, f"libdeflate-{VERSION}.tar.gz")
    with open(tar_path, "wb") as fh:
        fh.write(data)
    with tarfile.open(tar_path, "r:gz") as tf:
        tf.extractall(out, filter="data")
    print(f"libdeflate {VERSION} ({digest}) -> {os.path.join(out, 'libdeflate-' + VERSION)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
