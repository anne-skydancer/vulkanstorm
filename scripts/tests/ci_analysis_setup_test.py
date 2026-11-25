# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Drift guard: the analysis configure (.github/actions/analysis-setup) keeps the build options and the Windows
LL_BUILD flags of the viewer build (.github/actions/viewer-build), except the ones listed here on purpose."""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
VIEWER = ROOT / ".github" / "actions" / "viewer-build" / "action.yml"
SETUP = ROOT / ".github" / "actions" / "analysis-setup" / "action.yml"

# Options the analysis sets differently on purpose (a sanitizer build has no LTO or precompiled headers and builds
# tests; it never packages). Any other option must be identical, and present in both.
DIFFERENT = {"USE_LTO", "USE_PRECOMPILED_HEADERS", "LL_TESTS", "PACKAGE", "CMAKE_BUILD_TYPE"}


def configure_options(path):
    """{option: value} of the `cmake -S indra -B ...` command of a composite action."""
    text = path.read_text(encoding="utf-8")
    start = text.index("cmake -S indra -B")
    block = []
    for line in text[start:].splitlines():
        block.append(line)
        if not line.rstrip().endswith("\\"):
            break
    return dict(re.findall(r"-D(\w+)(?::\w+)?=(\S*)", "\n".join(block)))


def windows_ll_build(path):
    match = re.search(r"LL_BUILD='([^']+)'", path.read_text(encoding="utf-8"))
    return match.group(1) if match else None


class SetupDriftTest(unittest.TestCase):
    def test_configure_options_match(self):
        viewer, setup = configure_options(VIEWER), configure_options(SETUP)
        self.assertGreater(len(viewer), 10)
        for name in sorted(set(viewer) | set(setup)):
            if name in DIFFERENT:
                continue
            self.assertIn(name, setup, f"viewer-build sets {name}; analysis-setup does not")
            self.assertIn(name, viewer, f"analysis-setup sets {name}; viewer-build does not")
            self.assertEqual(viewer[name], setup[name], f"{name} differs between viewer-build and analysis-setup")

    def test_windows_flags_extend_the_viewer_flags(self):
        viewer, setup = windows_ll_build(VIEWER), windows_ll_build(SETUP)
        self.assertTrue(viewer and setup, "an LL_BUILD literal is missing")
        self.assertTrue(setup.startswith(viewer + " "), "the analysis LL_BUILD must be the viewer's plus /analyze flags")
        self.assertIn("/analyze", setup[len(viewer):])


if __name__ == "__main__":
    unittest.main()
