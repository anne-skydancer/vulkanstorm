# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Says whether autobuild.xml names a released neoj2k package yet.

usage: ci_neoj2k_pin.py [--output]

The USE_NEOJ2K build jobs of the build-*.yaml workflows need the 3p-neoj2k package
(https://github.com/anne-skydancer/3p-neoj2k). Until a release exists, autobuild.xml and
scripts/linux_lts_packages.json carry a placeholder entry (version 0.0.0.0, a zero hash), and those jobs are
skipped with a notice instead of failing on a download that cannot work. Once a release is pinned (the real
URLs, hashes and version), the jobs run on every pull request and every build that is not a release.

Prints `ready=true` or `ready=false`; with --output also appends it to $GITHUB_OUTPUT. Exits 1 when the entry
is missing or inconsistent (a real version with a zero hash, or the other way round).
"""
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


def main():
    entry = _value(ElementTree.parse(ROOT / "autobuild.xml").getroot()[0])["installables"].get("neoj2k")
    if not entry:
        print("autobuild.xml has no neoj2k entry", file=sys.stderr)
        return 1
    version = entry.get("version", "")
    hashes = [archive["archive"]["hash"] for archive in entry["platforms"].values()]
    zero = [h for h in hashes if h and set(h) == {"0"}]
    placeholder = version == PLACEHOLDER_VERSION
    if placeholder != (len(zero) == len(hashes)) or (not placeholder and zero):
        print(f"neoj2k entry is inconsistent: version {version}, {len(zero)} of {len(hashes)} hashes are zero",
              file=sys.stderr)
        return 1
    ready = "false" if placeholder else "true"
    print(f"ready={ready}")
    if "--output" in sys.argv and os.environ.get("GITHUB_OUTPUT"):
        with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as out:
            out.write(f"neoj2k_ready={ready}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
