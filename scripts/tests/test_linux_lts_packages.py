# SPDX-License-Identifier: LGPL-2.1-or-later OR GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""The linux-lts package override matches autobuild.xml.

Every package of ours (an anne-skydancer/3p-* release) with a linux64
archive in autobuild.xml has a linux64-lts archive in
scripts/linux_lts_packages.json, or is listed in its "without_lts" with the
reason no CI build installs it: the same version,
from the same release as autobuild.xml's linux64 archive, so the linux-lts
builds use the same library versions as linux-latest, only built in Ubuntu
24.04. Fails when the two diverge, e.g. after a package update that changed
only autobuild.xml.
"""
import importlib.util
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
OVERRIDE = ROOT / "scripts" / "linux_lts_packages.json"
OURS = "https://github.com/anne-skydancer/3p-"


def release_dir(url):
    return url.rsplit("/", 1)[0]


def main():
    spec = importlib.util.spec_from_file_location("override", ROOT / "scripts" / "ci_autobuild_override.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.load(OVERRIDE)  # versions, platforms and archive fields

    installables = module.installables()
    override = json.loads(OVERRIDE.read_text(encoding="utf-8"))
    packages = override["packages"]
    without = override.get("without_lts", {})
    errors = []
    for name, reason in sorted(without.items()):
        if not reason:
            errors.append(f"{name}: without_lts needs the reason no CI build installs it")
        if name in packages:
            errors.append(f"{name}: both overridden and listed in without_lts")
    ours = sorted(name for name, package in installables.items()
                  if package.get("platforms", {}).get("linux64", {}).get("archive", {}).get("url", "").startswith(OURS))
    for name in ours:
        if name not in packages and name not in without:
            errors.append(f"{name}: autobuild.xml's linux64 archive has no linux64-lts counterpart")
    for name, entry in sorted(packages.items()):
        latest = installables[name]["platforms"]["linux64"]["archive"]["url"]
        lts = entry["linux64"]["url"]
        if release_dir(lts) != release_dir(latest):
            errors.append(f"{name}: {lts} is not in the release of autobuild.xml's {latest}")
        if "-linux64-lts" not in lts.rsplit("/", 1)[1]:
            errors.append(f"{name}: {lts} is not a linux64-lts archive")
        if lts == latest or entry["linux64"]["hash"] == installables[name]["platforms"]["linux64"]["archive"].get("hash"):
            errors.append(f"{name}: the override repeats autobuild.xml's linux64 archive")
    if errors:
        sys.exit("\n".join(errors))
    print(f"linux-lts override matches autobuild.xml for {len(packages)} package(s): {', '.join(sorted(packages))}")


if __name__ == "__main__":
    main()
