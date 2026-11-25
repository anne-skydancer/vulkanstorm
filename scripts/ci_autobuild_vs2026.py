#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Add Visual Studio 2026 (vsver 180, toolset v145) to the installed autobuild's toolset table.

autobuild 3.10.2, its newest release, maps Visual Studio 2015 to 2022 only. With AUTOBUILD_VSVER=180 every autobuild
command then logs "Unable to determine a suggested AUTOBUILD_WIN_VSTOOLSET for vsver 180". The development machines
carry the same one-line addition. Idempotent: does nothing when the entry is already there.
"""

import importlib.util
import pathlib
import sys

ENTRY_KEY = '"18"'
ANCHOR = '    "17": "v143", # 2022'
ADDED = '    "18": "v145", # 2026'


def main():
    spec = importlib.util.find_spec("autobuild.autobuild_tool_source_environment")
    if spec is None or not spec.origin:
        print("autobuild is not installed", file=sys.stderr)
        return 1
    path = pathlib.Path(spec.origin)
    lines = path.read_text(encoding="utf-8").splitlines(keepends=True)
    start = next((i for i, line in enumerate(lines) if line.startswith("_VSTOOLSETS = {")), None)
    if start is None:
        print(f"{path}: no _VSTOOLSETS table; update {__file__}", file=sys.stderr)
        return 1
    end = next(i for i in range(start, len(lines)) if lines[i].startswith("}"))
    if any(ENTRY_KEY in line for line in lines[start:end]):
        print("autobuild already knows Visual Studio 2026")
        return 0
    anchor = next((i for i in range(start, end) if lines[i].rstrip() == ANCHOR), None)
    if anchor is None:
        print(f"{path}: the _VSTOOLSETS table changed; update {__file__}", file=sys.stderr)
        return 1
    ending = lines[anchor][len(lines[anchor].rstrip("\r\n")):]
    lines.insert(anchor + 1, ADDED + ending)
    path.write_text("".join(lines), encoding="utf-8")
    print("added Visual Studio 2026 (v145) to autobuild's toolset table")
    return 0


if __name__ == "__main__":
    sys.exit(main())
