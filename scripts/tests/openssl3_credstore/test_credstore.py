# SPDX-License-Identifier: GPL-3.0-or-later
"""Credential-store compatibility gate for the OpenSSL 3 move.

Builds credstore.cpp against the viewer's OpenSSL 1.1.1 package and
against OpenSSL 3.5 (static, no-module, legacy provider built in; see
build_openssl35.cmd), then checks that bin_conf.dat files cross-read in both
directions and are byte-identical for the same salt, and that the proposed
OpenSSL 3 code fails cleanly without the legacy provider.

Inputs (environment):
  CREDSTORE_PACKAGES   viewer build packages dir holding OpenSSL 1.1.1
                       (default: the first build-vc180-64/packages with
                       OpenSSL 1.1.1 in this checkout or a sibling one)
  CREDSTORE_OPENSSL35  OpenSSL 3.5 install prefix
                       (default: work/openssl-3.5/install beside the repos)
  CREDSTORE_REAL=1     also check the real store of this machine, read-only,
                       on a temporary copy; prints only digests and counts

Run: python -m unittest scripts/tests/openssl3_credstore/test_credstore.py -v
Skips (with the reason) when not on Windows or when either library is missing.
"""

import base64
import glob
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[2]
# Folders that may hold sibling checkouts/worktrees and the work/ build area.
SEARCH_ROOTS = [REPO_ROOT, REPO_ROOT.parent, REPO_ROOT.parent.parent]

# indra/newview/tests/llsechandler_basic_test.cpp, test 3.
FIXTURE_B64 = (
    "sUSh3wj77NG9oAMyt3XIhaej3KLZhLZWFZvI6rIGmwUUOmmelrRg0NI9rkOj8ZDpTPxpwToaBT5u"
    "GQhakdaGLJznr9bHr4/6HIC1bouKj4n2rs4TL6j2WSjto114QdlNfLsE8cbbE+ghww58g8SeyLQO"
    "nyzXoz+/PBz0HD5SMFDuObccoPW24gmqYySz8YoEWhSwO0pUtEEqOjVRsAJgF5wLAtJZDeuilGsq"
    "4ZT9Y4wZ9Rh8nnF3fDUL6IGamHe1ClXM1jgBu10F6UMhZbnH4C3aJ2E9+LiOntU+l3iCb2MpkEpr"
    "82r2ZAMwIrpnirL/xoYoyz7MJQYwUuMvBPToZJrxNSsjI+S2Z+I3iEJAELMAAA=="
)
FIXTURE_ID = bytes([77, 21, 46, 31, 89, 2]).hex()
TEST_ID = "a1b2c3d4e5f6"
FIXED_SALT = "00112233445566778899aabbccddeeff"


def _credential_llsd(n):
    """A realistic store: n saved logins in the shape LLSecAPIBasicHandler
    writes (credential map per grid, with identifier and authenticator)."""
    parts = ['<?xml version="1.0" ?>\n<llsd>\n<map>\n<key>credential</key>\n<map>\n']
    for i in range(n):
        parts.append(
            f"<key>util.agni.lindenlab.com#Resident{i:03d}</key>\n<map>\n"
            "<key>authenticator</key>\n<map>\n<key>algorithm</key>\n<string>md5</string>\n"
            f"<key>secret</key>\n<string>{hashlib.md5(str(i).encode()).hexdigest()}</string>\n"
            "<key>type</key>\n<string>hash</string>\n</map>\n"
            "<key>identifier</key>\n<map>\n<key>first_name</key>\n"
            f"<string>Resident{i:03d}</string>\n<key>last_name</key>\n<string>Resident</string>\n"
            "<key>type</key>\n<string>agent</string>\n</map>\n"
            f"<key>mfa_hash</key>\n<string>{hashlib.sha256(str(i).encode()).hexdigest()}</string>\n"
            "</map>\n"
        )
    parts.append("</map>\n</map>\n</llsd>\n")
    return "".join(parts).encode()


def _sized(n):
    body = b'<?xml version="1.0" ?>\n<llsd><string>'
    tail = b"</string></llsd>\n"
    fill = max(0, n - len(body) - len(tail))
    data = body + (b"x" * fill) + tail
    return data[:n] if n < len(data) else data


PLAINTEXTS = {
    "zero-bytes": b"",
    "empty-map": b'<?xml version="1.0" ?>\n<llsd><map /></llsd>\n',
    "size-255": _sized(255),
    "size-256": _sized(256),
    "size-257": _sized(257),
    "size-512": _sized(512),
    "size-1000": _sized(1000),
    "credentials-6": _credential_llsd(6),
}


def _find_packages():
    env = os.environ.get("CREDSTORE_PACKAGES")
    if env:
        return Path(env)
    patterns = ["build-vc180-64/packages", "*/build-vc180-64/packages", "*/*/build-vc180-64/packages"]
    for root in SEARCH_ROOTS:
        for pattern in patterns:
            for pkg in sorted(root.glob(pattern)):
                ver = pkg / "include" / "openssl" / "opensslv.h"
                if (pkg / "lib" / "release" / "libcrypto.lib").exists() and ver.exists()                         and "1.1.1" in ver.read_text(errors="replace"):
                    return pkg
    return None


def _find_openssl35():
    env = os.environ.get("CREDSTORE_OPENSSL35")
    if env:
        return Path(env)
    for root in SEARCH_ROOTS:
        cand = root / "work" / "openssl-3.5" / "install"
        if (cand / "lib" / "libcrypto.lib").exists():
            return cand
    return Path("work/openssl-3.5/install")


class CredStoreTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if sys.platform != "win32":
            raise unittest.SkipTest("credstore harness builds with MSVC: Windows only")
        cls.pkg = _find_packages()
        cls.o3 = _find_openssl35()
        if not cls.pkg or not (cls.pkg / "lib" / "release" / "libcrypto.lib").exists():
            raise unittest.SkipTest("OpenSSL 1.1.1 package not found (set CREDSTORE_PACKAGES)")
        if not (cls.o3 / "lib" / "libcrypto.lib").exists():
            raise unittest.SkipTest(f"OpenSSL 3.5 not found at {cls.o3} (run build_openssl35.cmd, "
                                    "or set CREDSTORE_OPENSSL35)")
        cls.tmp = Path(tempfile.mkdtemp(prefix="credstore-"))
        r = subprocess.run(["cmd", "/c", str(HERE / "build.cmd"), str(cls.pkg), str(cls.o3), str(cls.tmp / "bin")],
                           capture_output=True, text=True)
        if r.returncode != 0:
            raise RuntimeError("harness build failed:\n" + r.stdout + r.stderr)
        cls.b111 = cls.tmp / "bin" / "credstore111.exe"
        cls.b35 = cls.tmp / "bin" / "credstore35.exe"

    @classmethod
    def tearDownClass(cls):
        if getattr(cls, "tmp", None):
            shutil.rmtree(cls.tmp, ignore_errors=True)

    # helpers -------------------------------------------------------------
    def run_tool(self, exe, *args, stdin=None):
        return subprocess.run([str(exe), *map(str, args)], input=stdin, capture_output=True)

    def path(self, name):
        return self.tmp / name

    def write(self, exe, plain, store, salt="-", ident=TEST_ID, *flags):
        src = self.path(store.name + ".in")
        src.write_bytes(plain)
        return self.run_tool(exe, "write", ident, salt, src, store, *flags)

    def read(self, exe, store, ident=TEST_ID, *flags):
        out = self.path(store.name + ".out")
        if out.exists():
            out.unlink()
        r = self.run_tool(exe, "read", ident, store, out, *flags)
        return r, (out.read_bytes() if out.exists() else None)

    # tests ---------------------------------------------------------------
    def test_00_versions(self):
        v1 = self.run_tool(self.b111, "info").stdout.decode().strip()
        v3 = self.run_tool(self.b35, "info").stdout.decode().strip()
        print(f"\n  1.1.1 build: {v1}\n  3.5 build:   {v3}")
        self.assertIn("OpenSSL 1.1.1", v1)
        self.assertIn("OpenSSL 3.5", v3)

    def test_01_fixture_reads_identically(self):
        store = self.path("fixture.dat")
        store.write_bytes(base64.b64decode(FIXTURE_B64))
        r1, p1 = self.read(self.b111, store, FIXTURE_ID)
        r3, p3 = self.read(self.b35, store, FIXTURE_ID)
        self.assertEqual(r1.returncode, 0, r1.stderr)
        self.assertEqual(r3.returncode, 0, r3.stderr)
        self.assertEqual(p1, p3)
        for needle in (b"<key>test_data_type</key>", b"test_data_1", b"test_data_2", b"test element1"):
            self.assertIn(needle, p3)

    def test_02_same_salt_same_bytes_and_cross_read(self):
        for name, plain in PLAINTEXTS.items():
            with self.subTest(plaintext=name):
                s1, s3 = self.path(f"{name}-111.dat"), self.path(f"{name}-35.dat")
                self.assertEqual(self.write(self.b111, plain, s1, FIXED_SALT).returncode, 0)
                r = self.write(self.b35, plain, s3, FIXED_SALT)
                self.assertEqual(r.returncode, 0, r.stderr)
                self.assertEqual(s1.read_bytes(), s3.read_bytes(), "files differ for the same salt")
                self.assertEqual(len(s3.read_bytes()), 16 + len(plain))
                for writer, store in (("1.1.1", s1), ("3.5", s3)):
                    for reader in (self.b111, self.b35):
                        rr, back = self.read(reader, store)
                        self.assertEqual(rr.returncode, 0, rr.stderr)
                        self.assertEqual(back, plain, f"{writer} store read by {reader.name}")

    def test_03_random_salt_cross_read(self):
        plain = PLAINTEXTS["credentials-6"]
        for writer, reader in ((self.b111, self.b35), (self.b35, self.b111)):
            with self.subTest(writer=writer.name):
                store = self.path(f"random-{writer.stem}.dat")
                self.assertEqual(self.write(writer, plain, store).returncode, 0)
                rr, back = self.read(reader, store)
                self.assertEqual(rr.returncode, 0, rr.stderr)
                self.assertEqual(back, plain)

    def test_04_no_legacy_write_fails_cleanly(self):
        store = self.path("keep.dat")
        self.assertEqual(self.write(self.b111, PLAINTEXTS["credentials-6"], store).returncode, 0)
        before = store.read_bytes()
        r = self.write(self.b35, _credential_llsd(7), store, "-", TEST_ID, "--no-legacy")
        print(f"\n  --no-legacy write: rc={r.returncode} {r.stderr.decode().splitlines()[0]}")
        self.assertEqual(r.returncode, 1)
        self.assertEqual(store.read_bytes(), before, "existing store changed")
        self.assertFalse(Path(str(store) + ".tmp").exists(), "temporary file left behind")
        # and a fresh path is not created
        fresh = self.path("never.dat")
        self.assertEqual(self.write(self.b35, b"<llsd/>", fresh, "-", TEST_ID, "--no-legacy").returncode, 1)
        self.assertFalse(fresh.exists())

    def test_05_no_legacy_read_fails_cleanly(self):
        store = self.path("fixture-nl.dat")
        store.write_bytes(base64.b64decode(FIXTURE_B64))
        before = store.read_bytes()
        r, out = self.read(self.b35, store, FIXTURE_ID, "--no-legacy")
        print(f"\n  --no-legacy read: rc={r.returncode} {r.stderr.decode().splitlines()[0]}")
        self.assertEqual(r.returncode, 1)
        self.assertIsNone(out, "plaintext written despite failure")
        self.assertEqual(store.read_bytes(), before)

    def test_06_unchanged_viewer_code_on_openssl3_loses_the_store(self):
        """Control for risk 1: today's code against OpenSSL 3 without the
        legacy provider. Expected to corrupt, which is why the change is needed."""
        plain = PLAINTEXTS["credentials-6"]
        store = self.path("unchecked.dat")
        self.assertEqual(self.write(self.b111, plain, store).returncode, 0)
        r = self.write(self.b35, plain, store, "-", TEST_ID, "--unchecked")
        size = len(store.read_bytes())
        print(f"\n  unchanged code, OpenSSL 3, write: rc={r.returncode}, store now {size} bytes "
              f"(plaintext {len(plain)} bytes)")
        self.assertEqual(size, 16, "expected a salt-only store")
        store2 = self.path("unchecked-read.dat")
        self.assertEqual(self.write(self.b111, plain, store2).returncode, 0)
        rr, back = self.read(self.b35, store2, TEST_ID, "--unchecked")
        print(f"  unchanged code, OpenSSL 3, read: rc={rr.returncode}, plaintext correct: {back == plain}")
        self.assertNotEqual(back, plain)

    @unittest.skipUnless(os.environ.get("CREDSTORE_REAL") == "1", "set CREDSTORE_REAL=1 to check this machine's store")
    def test_07_real_store_read_only(self):
        src = Path(os.environ["APPDATA"]) / "Vulkanstorm_x64" / "user_settings" / "bin_conf.dat"
        if not src.exists():
            self.skipTest(f"no store at {src}")
        copy = self.path("real-copy.dat")
        shutil.copyfile(src, copy)
        src_digest = hashlib.sha256(src.read_bytes()).hexdigest()
        try:
            results = {}
            for label, ident in _machine_ids():
                d1 = self.run_tool(self.b111, "digest", "-", copy, stdin=(ident + "\n").encode())
                d3 = self.run_tool(self.b35, "digest", "-", copy, stdin=(ident + "\n").encode())
                o1, o3 = d1.stdout.decode().strip(), d3.stdout.decode().strip()
                ok = d1.returncode == 0 and d3.returncode == 0 and "llsd=yes" in o1
                results[label] = (ok, o1 == o3, o1.split()[-2] if ok else "")
                if ok:
                    break
            print("\n  real store: " + "; ".join(
                f"{k}: decrypts={v[0]} identical={v[1]} {v[2]}" for k, v in results.items()))
            self.assertTrue(any(v[0] and v[1] for v in results.values()),
                            "store not decrypted with either machine id")
            self.assertEqual(hashlib.sha256(src.read_bytes()).hexdigest(), src_digest, "original changed")
        finally:
            copy.unlink(missing_ok=True)


def _machine_ids():
    """LLMachineID on Windows (indra/newview/llmachineid.cpp): the WMI
    Win32_ComputerSystemProduct UUID (unique id), else the Win32_OperatingSystem
    serial (legacy id), folded into 6 bytes by adding characters cyclically.
    The values stay in memory and are passed to the harness on stdin."""
    def wmi(cls, prop):
        r = subprocess.run(["powershell", "-NoProfile", "-Command",
                            f"(Get-CimInstance {cls}).{prop}"], capture_output=True, text=True)
        return r.stdout.strip().splitlines()[0] if r.stdout.strip() else ""

    def fold(s):
        out = [0] * 6
        j = 0
        while j < len(s):
            for i in range(6):
                if j >= len(s):
                    break
                out[i] = (out[i] + ord(s[j])) & 0xFF
                j += 1
        return bytes(out).hex()

    ids = []
    uuid = wmi("Win32_ComputerSystemProduct", "UUID")
    hexdigits = uuid.replace("-", "").upper()
    if len(uuid) == 36 and len(hexdigits) == 32 and hexdigits not in ("0" * 32, "F" * 32):
        ids.append(("unique id", fold(uuid)))
    serial = wmi("Win32_OperatingSystem", "SerialNumber")
    if serial:
        ids.append(("legacy id", fold(serial)))
    return ids


if __name__ == "__main__":
    unittest.main(verbosity=2)
