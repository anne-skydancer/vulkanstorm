# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""mc1_sl_launch.ps1: the autologin profile rules (marker, reparse points, delete order, running viewer). Every case uses a scratch
profile folder named by VKS_AUTOLOGIN_PROFILE and never starts a viewer or touches the real profile. Needs pwsh and Windows."""
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
LAUNCHER = HERE.parent / "mc1_sl_launch.ps1"
PWSH = shutil.which("pwsh")
MARKER = ".vulkanstorm-autologin-profile"
CRED = pathlib.Path("Roaming") / "Vulkanstorm_x64" / "user_settings" / "bin_conf.dat"


@unittest.skipUnless(sys.platform == "win32" and PWSH, "needs Windows and pwsh")
class LauncherProfileTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = pathlib.Path(self.tmp.name)
        self.prof = self.root / "profile"

    def run_launcher(self, *args, **env):
        e = dict(os.environ, VKS_AUTOLOGIN_PROFILE=str(self.prof), **env)
        r = subprocess.run([PWSH, "-NoProfile", "-File", str(LAUNCHER), *args], env=e, capture_output=True, text=True, timeout=120)
        return r.returncode, r.stdout + r.stderr

    def make_profile(self, marker=True, cred=True):
        (self.prof / CRED.parent).mkdir(parents=True)
        (self.prof / "Local").mkdir()
        if marker:
            (self.prof / MARKER).write_text("x")
        if cred:
            (self.prof / CRED).write_bytes(b"not a real credential store")      # a stand-in: its content is never read

    def test_delete_refuses_a_folder_without_the_marker(self):
        self.make_profile(marker=False)
        code, out = self.run_launcher("-DeleteProfile")
        self.assertNotEqual(code, 0)
        self.assertIn("marker", out)
        self.assertTrue((self.prof / CRED).exists())

    def test_autologin_refuses_without_marker_and_without_a_credential_store(self):
        self.make_profile(marker=False)
        code, out = self.run_launcher("-AutoLogin")
        self.assertNotEqual(code, 0)
        self.assertIn("marker", out)
        shutil.rmtree(self.prof)
        self.make_profile(cred=False)
        code, out = self.run_launcher("-AutoLogin")
        self.assertNotEqual(code, 0)
        self.assertIn("no credential store", out)

    def test_autologin_needs_both_region_names_before_anything_starts(self):
        self.make_profile()
        code, out = self.run_launcher("-AutoLogin", "-Scenario", "mc1b_rooftops", "-BusyRegion", "Busy")
        self.assertNotEqual(code, 0)
        self.assertIn("-StartRegion", out)

    def test_busy_pos_is_validated(self):
        code, out = self.run_launcher("-AutoLogin", "-BusyPos", "1, 2, 3")
        self.assertNotEqual(code, 0)
        self.assertIn("-BusyPos", out)

    def test_setup_refuses_a_foreign_non_empty_folder(self):
        self.prof.mkdir()
        (self.prof / "something.txt").write_text("x")
        code, out = self.run_launcher("-SetupLogin")
        self.assertNotEqual(code, 0)
        self.assertIn("not ours", out)

    def test_delete_refuses_a_junction(self):
        target = self.root / "target"
        (target / "Roaming").mkdir(parents=True)
        (target / MARKER).write_text("x")
        subprocess.run(["cmd", "/c", "mklink", "/J", str(self.prof), str(target)], check=True, capture_output=True)
        code, out = self.run_launcher("-DeleteProfile")
        self.assertNotEqual(code, 0)
        self.assertIn("junction", out)
        self.assertTrue((target / MARKER).exists())

    def test_delete_refuses_while_the_viewer_runs(self):
        self.make_profile()
        fake = self.root / "vkfakebin.exe"
        shutil.copy(pathlib.Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32" / "PING.EXE", fake)
        proc = subprocess.Popen([str(fake), "-n", "60", "127.0.0.1"], stdout=subprocess.DEVNULL)
        try:
            code, out = self.run_launcher("-DeleteProfile", VKS_AUTOLOGIN_PROCESS="vkfakebin")
        finally:
            proc.terminate()
            proc.wait()
        self.assertNotEqual(code, 0)
        self.assertIn("is running", out)
        self.assertTrue((self.prof / CRED).exists())

    def test_delete_removes_everything(self):
        self.make_profile()
        code, out = self.run_launcher("-DeleteProfile")
        self.assertEqual(code, 0, out)
        self.assertFalse(self.prof.exists())

    def test_a_locked_file_cannot_keep_the_credential_files_and_the_leftovers_are_reported(self):
        self.make_profile()
        (self.prof / "Roaming" / "Vulkanstorm_x64" / "user_settings" / "password.dat").write_bytes(b"x")
        locked = self.prof / "Local" / "cache.bin"
        with open(locked, "wb") as held:       # held open: it cannot be deleted
            held.write(b"x")
            code, out = self.run_launcher("-DeleteProfile")
            self.assertEqual(code, 2, out)
            self.assertIn("LEFTOVER", out)
            self.assertIn("cache.bin", out)
            self.assertNotIn("CREDENTIAL FILE STILL PRESENT", out)
            self.assertFalse((self.prof / CRED).exists())
            self.assertFalse((self.prof / CRED.parent / "password.dat").exists())
            self.assertTrue(locked.exists())


if __name__ == "__main__":
    unittest.main()
