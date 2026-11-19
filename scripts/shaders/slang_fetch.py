# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Fetch the pinned Slang release into the tool cache, check its SHA-256, unpack it and check `slangc -v`.

A build tool, never shipped and never linked. The archive goes to the cache outside the tree
(VKSTORM_TOOLS_CACHE, else LOCALAPPDATA or ~/.cache), so the tree stays clean."""
import argparse
import hashlib
import shutil
import sys
import tarfile
import tempfile
import time
import urllib.request
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import slang_common as common  # noqa: E402


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


def safe_extract(archive, dest):
    """Unpack, refusing any member (or link target) that would land outside dest."""
    dest = Path(dest).resolve()
    if str(archive).endswith('.zip'):
        with zipfile.ZipFile(archive) as z:
            for name in z.namelist():
                if not (dest / name).resolve().is_relative_to(dest):
                    raise SystemExit(f'archive member escapes the target folder: {name}')
            z.extractall(dest)
        return
    with tarfile.open(archive) as t:
        for m in t.getmembers():
            target = (dest / m.name).resolve()
            if m.islnk():
                target = (dest / m.linkname).resolve()
            elif m.issym():
                target = (dest / m.name).parent.joinpath(m.linkname).resolve()
            if not (dest / m.name).resolve().is_relative_to(dest) or not target.is_relative_to(dest):
                raise SystemExit(f'archive member escapes the target folder: {m.name}')
        t.extractall(dest, filter='data')


def download(url, dest, tries=3, timeout=60):
    last = None
    for attempt in range(tries):
        try:
            with urllib.request.urlopen(url, timeout=timeout) as resp, open(dest, 'wb') as out:
                shutil.copyfileobj(resp, out)
            return
        except OSError as e:
            last = e
            print(f'download attempt {attempt + 1} of {tries} failed: {e}', flush=True)
            time.sleep(2 * (attempt + 1))
    raise SystemExit(f'could not download {url}: {last}')


def fetch(pin, key=None, force=False):
    key = key or common.platform_key()
    entry = pin['platforms'][key]
    target = common.slang_dir(pin)
    if not force and common.slangc_path(pin):
        try:
            return common.find_slangc(pin)
        except SystemExit:
            pass
    target.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=target.parent) as tmp:
        archive = Path(tmp) / Path(entry['url']).name
        print(f'fetching {entry["url"]}', flush=True)
        download(entry['url'], archive)
        got = sha256_of(archive)
        if got != entry['sha256']:
            raise SystemExit(f'SHA-256 mismatch for {archive.name}: got {got}, pinned {entry["sha256"]}')
        staging = Path(tmp) / 'unpacked'
        staging.mkdir()
        safe_extract(archive, staging)
        shutil.rmtree(target, ignore_errors=True)  # only now: the download is verified and unpacked
        staging.rename(target)
    slangc = common.find_slangc(pin)
    print(f'slangc {common.slangc_version(slangc)} ready at {slangc}')
    return slangc


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--force', action='store_true', help='fetch again even when the cache holds the pinned version')
    ap.add_argument('--platform', help='pin platform key (default: this machine)')
    args = ap.parse_args()
    fetch(common.read_pin(), args.platform, args.force)


if __name__ == '__main__':
    main()
