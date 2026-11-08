# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""The tools are GPL-3.0-or-later: licence text, headers, Cargo licence and bundled notices."""
from pathlib import Path
import hashlib
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[3]
EDITOR = ROOT / "tools" / "skin-editor"
# sha256 of https://www.gnu.org/licenses/gpl-3.0.txt
GPL3_SHA256 = "3972dc9744f6499f0f9b2dbf76696f2ae7ad8af9b23dde66d6af86c9dfb36986"


class LicenceTest(unittest.TestCase):
    def test_licence_is_the_gpl3_text(self):
        for path in (ROOT / "LICENSE", EDITOR / "licenses" / "GPL-3.0-license.txt"):
            with self.subTest(path=path.name):
                self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), GPL3_SHA256)

    def test_lgpl_text_is_kept_for_the_library_headers(self):
        for path in (ROOT / "LICENSE-LGPL-2.1", EDITOR / "licenses" / "LGPL-2.1-license.txt"):
            with self.subTest(path=path.name):
                self.assertIn(b"Version 2.1, February 1999", path.read_bytes())
        self.assertFalse((ROOT / "LICENSE-GPL-3.0").exists())

    def test_cargo_licence(self):
        cargo = (EDITOR / "desktop" / "Cargo.toml").read_text(encoding="utf-8")
        self.assertIn('license = "GPL-3.0-or-later"', cargo)

    def test_no_dual_licence_header_is_left(self):
        names = subprocess.run(["git", "-C", str(ROOT), "ls-files", "-z", "tools/skin-editor"],
                               check=True, capture_output=True).stdout.decode().split("\0")
        offenders = []
        for name in names:
            path = ROOT / name
            if (not name or "/library/" in name or path.suffix in (".png", ".lock", ".json")
                    or name.endswith("test_licence.py")):
                continue
            if "LGPL-2.1-or-later OR GPL" in path.read_bytes()[:2000].decode("utf-8", "replace"):
                offenders.append(name)
        self.assertEqual(offenders, [])


if __name__ == "__main__":
    unittest.main()
