# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Says whether autobuild.xml and the linux-lts list name a released neoj2k package yet.

usage: ci_neoj2k_pin.py [--output] [--require]

The viewer builds with neo-j2k (USE_NEOJ2K, on by default), which needs the 3p-neoj2k package
(https://github.com/anne-skydancer/3p-neoj2k): its release is pinned in autobuild.xml (linux64 and windows64) and in
scripts/linux_lts_packages.json (linux64-lts). A placeholder entry (version 0.0.0.0, zero hashes) means the package
has no release yet. With --require the script fails on a placeholder, so that a build stops at once with the reason
instead of on a download that cannot work.

The linux64-lts placeholder hash ends in 1, not 0: scripts/tests/test_linux_lts_packages.py refuses a linux-lts
archive that repeats the linux64 one, and a placeholder is the only case where both would be the zero hash.

Prints `ready=true` or `ready=false`; with --output also appends it to $GITHUB_OUTPUT; with --require exits 1 when it
is false. Exits 1 when an entry is
missing or inconsistent: a real version with a placeholder hash, a placeholder version with a real hash, or the two
files disagreeing.
"""
import json
import os
import pathlib
import sys
import xml.etree.ElementTree as ElementTree

ROOT = pathlib.Path(__file__).resolve().parent.parent
PLACEHOLDER_VERSION = "0.0.0.0"


def _value(element):
    if element.tag == "map":
        children = list(element)
        return {key.text or "": _value(value) for key, value in zip(children[::2], children[1::2])}
    if element.tag == "array":
        return [_value(child) for child in element]
    return element.text or ""


def is_placeholder_hash(digest):
    """63 zeros and a last digit of 0 or 1."""
    return len(digest) == 64 and set(digest[:63]) == {"0"} and digest[63] in "01"


def check(root):
    """(ready, problems) for the autobuild.xml and scripts/linux_lts_packages.json under `root`."""
    root = pathlib.Path(root)
    problems = []
    installables = _value(ElementTree.parse(root / "autobuild.xml").getroot()[0])["installables"]
    entry = installables.get("neoj2k")
    if not entry:
        return False, ["autobuild.xml has no neoj2k entry"]
    version = entry.get("version", "")
    placeholder = version == PLACEHOLDER_VERSION
    archives = {name: platform["archive"] for name, platform in entry["platforms"].items()}
    lts_path = root / "scripts" / "linux_lts_packages.json"
    lts = json.loads(lts_path.read_text(encoding="utf-8"))["packages"].get("neoj2k") if lts_path.exists() else None
    if lts is None:
        problems.append("scripts/linux_lts_packages.json has no neoj2k entry")
    else:
        archives["linux64-lts"] = lts["linux64"]
        if lts.get("version") != version:
            problems.append(f"the linux-lts list says version {lts.get('version')}, autobuild.xml {version}")
    for name, archive in sorted(archives.items()):
        digest = archive.get("hash", "")
        if placeholder and not is_placeholder_hash(digest):
            problems.append(f"{name}: version {version} is the placeholder but the hash is not")
        if not placeholder and is_placeholder_hash(digest):
            problems.append(f"{name}: a placeholder hash with the released version {version}")
        if not placeholder and f"/v{version.rsplit('.', 1)[0]}" not in archive.get("url", "") \
                and f"-{version}-" not in archive.get("url", ""):
            problems.append(f"{name}: the URL does not name version {version}")
    return not placeholder and not problems, problems


def main():
    ready, problems = check(ROOT)
    for problem in problems:
        print(problem, file=sys.stderr)
    if problems:
        return 1
    print(f"ready={'true' if ready else 'false'}")
    if "--require" in sys.argv and not ready:
        print("the neoj2k package is still the placeholder: no 3p-neoj2k release is pinned", file=sys.stderr)
        return 1
    if "--output" in sys.argv and os.environ.get("GITHUB_OUTPUT"):
        with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as out:
            out.write(f"neoj2k_ready={'true' if ready else 'false'}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
