# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""scripts/ci_neoj2k_pin.py: the placeholder and the released states of the neoj2k package entry."""
import importlib.util
import json
import pathlib
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("pin", ROOT / "scripts" / "ci_neoj2k_pin.py")
pin = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pin)

ZERO = "0" * 64
REAL = "ab" * 32


def xml(version, linux, windows):
    def plat(name, digest):
        return (f"<key>{name}</key><map><key>archive</key><map><key>hash</key><string>{digest}</string>"
                f"<key>url</key><string>https://github.com/anne-skydancer/3p-neoj2k/releases/download/v{version[:-2]}/"
                f"neoj2k-{version}-{name}-1.tar.zst</string></map></map>")
    return ("<llsd><map><key>installables</key><map><key>neoj2k</key><map><key>version</key>"
            f"<string>{version}</string><key>platforms</key><map>{plat('linux64', linux)}{plat('windows64', windows)}"
            "</map></map></map></map></llsd>")


def lts(version, digest):
    return json.dumps({"packages": {"neoj2k": {"version": version, "linux64": {
        "url": f"https://github.com/anne-skydancer/3p-neoj2k/releases/download/v{version[:-2]}/neoj2k-{version}-linux64-lts-1.tar.zst",
        "hash": digest, "hash_algorithm": "sha256"}}}})


def run(autobuild, lts_json):
    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp)
        (root / "autobuild.xml").write_text(autobuild, encoding="utf-8")
        (root / "scripts").mkdir()
        if lts_json is not None:
            (root / "scripts" / "linux_lts_packages.json").write_text(lts_json, encoding="utf-8")
        return pin.check(root)


class PinTest(unittest.TestCase):
    def test_the_placeholder_is_not_ready(self):
        ready, problems = run(xml("0.0.0.0", ZERO, ZERO), lts("0.0.0.0", "0" * 63 + "1"))
        self.assertFalse(ready)
        self.assertEqual(problems, [])

    def test_a_release_is_ready(self):
        ready, problems = run(xml("0.1.0.0", REAL, REAL), lts("0.1.0.0", "cd" * 32))
        self.assertTrue(ready, problems)

    def test_the_repository_state_is_consistent(self):
        ready, problems = pin.check(ROOT)
        self.assertEqual(problems, [])

    def test_a_real_version_with_a_placeholder_hash_fails(self):
        ready, problems = run(xml("0.1.0.0", ZERO, REAL), lts("0.1.0.0", "cd" * 32))
        self.assertFalse(ready)
        self.assertTrue(any("placeholder hash" in p for p in problems))

    def test_a_placeholder_version_with_a_real_hash_fails(self):
        ready, problems = run(xml("0.0.0.0", REAL, ZERO), lts("0.0.0.0", "0" * 63 + "1"))
        self.assertFalse(ready)
        self.assertTrue(any("the hash is not" in p for p in problems))

    def test_the_lts_list_must_agree(self):
        ready, problems = run(xml("0.1.0.0", REAL, REAL), lts("0.0.0.0", "0" * 63 + "1"))
        self.assertFalse(ready)
        self.assertTrue(any("linux-lts list says version" in p for p in problems))

    def test_a_missing_lts_entry_fails(self):
        ready, problems = run(xml("0.0.0.0", ZERO, ZERO), None)
        self.assertFalse(ready)
        self.assertTrue(any("no neoj2k entry" in p for p in problems))


if __name__ == "__main__":
    unittest.main()
