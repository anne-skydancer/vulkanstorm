# SPDX-License-Identifier: LGPL-2.1-or-later OR GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Point autobuild.xml's entries at the archives of a checked-in override.

usage: ci_autobuild_override.py <override.json> [--check]

The override (scripts/linux_lts_packages.json) lists, per package, its
version and, per platform, the archive URL and hash a CI build uses instead
of autobuild.xml's own: the linux-lts builds take the linux64-lts archives,
built in Ubuntu 24.04, of the same releases whose linux64 archives (built on
ubuntu-26.04) autobuild.xml names. The version must equal autobuild.xml's. Every URL and hash
is in the repository; nothing is computed in CI. It runs
`autobuild installables edit` in the checkout, so only that CI job's working
copy changes. With --check it only validates the override against
autobuild.xml.
"""
import json
import pathlib
import subprocess
import sys
import xml.etree.ElementTree as ElementTree

ROOT = pathlib.Path(__file__).resolve().parent.parent
ARCHIVE_KEYS = ("url", "hash", "hash_algorithm")


def _value(element):
    if element.tag == "map":
        children = list(element)
        return {key.text or "": _value(value) for key, value in zip(children[::2], children[1::2])}
    if element.tag == "array":
        return [_value(child) for child in element]
    return element.text or ""


def installables():
    """autobuild.xml's installables. A small LLSD-XML reader (maps, arrays and
    scalars as text), so that the check-publication job needs no llsd module."""
    return _value(ElementTree.parse(ROOT / "autobuild.xml").getroot()[0])["installables"]


def load(override_path):
    override = json.loads(pathlib.Path(override_path).read_text(encoding="utf-8"))
    packages = installables()
    edits = []
    for name, entry in sorted(override["packages"].items()):
        if name not in packages:
            raise SystemExit(f"{override_path}: {name} is not an installable in autobuild.xml")
        # The LTS archive is the same library version as autobuild.xml's.
        if entry.get("version") != packages[name].get("version"):
            raise SystemExit(f"{override_path}: {name} is version {entry.get('version')}, "
                             f"autobuild.xml has {packages[name].get('version')}")
        for platform, archive in sorted((key, value) for key, value in entry.items() if key != "version"):
            if platform not in packages[name].get("platforms", {}):
                raise SystemExit(f"{override_path}: autobuild.xml has no {platform} archive for {name}")
            if sorted(archive) != sorted(ARCHIVE_KEYS) or not all(archive.values()):
                raise SystemExit(f"{override_path}: {name} {platform} needs exactly {', '.join(ARCHIVE_KEYS)}")
            edits.append((name, platform, archive))
    return edits


def main():
    args = sys.argv[1:]
    check = "--check" in args
    args = [arg for arg in args if arg != "--check"]
    if len(args) != 1:
        raise SystemExit(__doc__)
    edits = load(args[0])
    for name, platform, archive in edits:
        if check:
            continue
        subprocess.run(["autobuild", "installables", "edit", name, f"platform={platform}",
                        *(f"{key}={archive[key]}" for key in ARCHIVE_KEYS)], cwd=ROOT, check=True)
        print(f"{name} {platform}: {archive['url']}")
    if not check:
        # Read back what autobuild wrote.
        packages = installables()
        for name, platform, archive in edits:
            actual = packages[name]["platforms"][platform]["archive"]
            if any(actual.get(key) != archive[key] for key in ARCHIVE_KEYS):
                raise SystemExit(f"autobuild.xml did not take the override for {name} {platform}")
    print(f"{len(edits)} archive(s) {'valid' if check else 'overridden'} from {args[0]}")


if __name__ == "__main__":
    main()
