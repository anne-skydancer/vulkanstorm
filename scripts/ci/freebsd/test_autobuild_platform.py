"""Exercise the installed tool's platform selection without downloading archives."""

import os
import unittest
from unittest.mock import patch

from autobuild import common


class PlatformTest(unittest.TestCase):
    def setUp(self):
        self.addCleanup(patch.stopall)
        patch.dict(os.environ, {}, clear=True).start()
        patch.object(common, "Platform", None).start()
        patch.object(common, "_AUTOBUILD_PLATFORM", None).start()
        patch.object(common, "_AUTOBUILD_PLATFORM_OVERRIDE", None).start()
        patch.object(common.platform, "machine", return_value="amd64").start()

    def test_native_freebsd(self):
        with patch.object(common.sys, "platform", "freebsd15"):
            self.assertEqual(common.establish_platform(addrsize=64), "freebsd64")
            self.assertEqual(os.environ["AUTOBUILD_PLATFORM"], "freebsd64")
            self.assertEqual(os.environ["AUTOBUILD_ADDRSIZE"], "64")
            with self.assertRaises(common.AutobuildError):
                common.establish_platform("linux64", addrsize=64)
            with self.assertRaises(common.AutobuildError):
                common.establish_platform(addrsize=32)
            with self.assertRaises(common.AutobuildError):
                common.establish_platform("freebsd64", addrsize=32)

    @unittest.skipUnless(common.sys.platform.startswith("freebsd"), "Native FreeBSD defaults")
    def test_native_default(self):
        self.assertEqual(common.DEFAULT_ADDRSIZE, 64)
        self.assertEqual(common.establish_platform(), "freebsd64")

    def test_other_platforms_preserved(self):
        for host, expected in (("linux", "linux64"), ("win32", "windows64"), ("darwin", "darwin64")):
            with self.subTest(host=host), patch.object(common.sys, "platform", host):
                self.assertEqual(common.establish_platform(addrsize=64), expected)
                with self.assertRaises(common.AutobuildError):
                    common.establish_platform("freebsd64", addrsize=64)


if __name__ == "__main__":
    unittest.main()
