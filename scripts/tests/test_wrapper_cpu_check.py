# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""The Linux launcher refuses a processor without x86-64-v3, and only then.

Runs indra/newview/linux_tools/wrapper.sh with fake ld.so outputs and fake
/proc/cpuinfo files (VKSTORM_LD_SO, VKSTORM_CPUINFO, VKSTORM_ARCH) and
VKSTORM_CPU_CHECK_ONLY=1, so the host's own processor never matters and the
viewer is never started.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
WRAPPER = ROOT / 'indra/newview/linux_tools/wrapper.sh'
MESSAGE = ("Vulkanstorm needs a processor with AVX2 (x86-64-v3). Most Intel Core "
           "processors from Haswell (2013) on and AMD Zen or Excavator and newer "
           "have it; some Pentium, Celeron and Atom models and some virtual machines "
           "do not. This computer's processor does not support it.")
FLAGS_V3 = 'fpu sse sse2 avx avx2 bmi1 bmi2 f16c fma movbe abm'
BASH = shutil.which('bash')

GLIBC_SUPPORTED = ('Legacy HWCAP subdirectories under library search path directories:\n'
                   '  x86-64-v4\n  x86-64-v3 (supported, searched)\n  x86-64-v2 (supported, searched)\n')
GLIBC_NOT_SUPPORTED = ('Subdirectories of glibc-hwcaps directories, in priority order:\n'
                       '  x86-64-v4\n  x86-64-v3\n  x86-64-v2 (supported, searched)\n')
UNRECOGNISED = 'ld.so: usage text in some future wording\n'


@unittest.skipUnless(BASH, 'needs bash')
class WrapperCpuCheck(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp(prefix='cpucheck', dir=os.environ.get('TMPDIR') or None))
        self.addCleanup(shutil.rmtree, self.dir, True)

    def ld_so(self, output):
        path = self.dir / 'ld.so'
        path.write_bytes(('#!/bin/sh\ncat <<\'EOT\'\n%sEOT\n' % output).encode())
        path.chmod(0o755)
        return str(path)

    def cpuinfo(self, flags):
        path = self.dir / 'cpuinfo'
        path.write_text('processor : 0\nflags : %s\n\nprocessor : 1\nflags : fpu\n' % flags
                        if flags is not None else '')
        return str(path)

    def run_check(self, ld_so='', cpuinfo=None, arch='x86_64'):
        env = dict(os.environ, VKSTORM_CPU_CHECK_ONLY='1', VKSTORM_ARCH=arch,
                   VKSTORM_LD_SO=ld_so, DISPLAY='', WAYLAND_DISPLAY='')
        if cpuinfo is not None:
            env['VKSTORM_CPUINFO'] = cpuinfo
        return subprocess.run([BASH, str(WRAPPER)], env=env, capture_output=True, text=True)

    def assertOk(self, result):
        self.assertEqual(result.returncode, 0, result.stderr)

    def assertRefused(self, result):
        self.assertEqual(result.returncode, 1)
        self.assertIn(MESSAGE, result.stderr)

    def test_glibc_supported_passes_even_with_bad_cpuinfo(self):
        self.assertOk(self.run_check(self.ld_so(GLIBC_SUPPORTED), self.cpuinfo('fpu')))

    def test_glibc_not_supported_falls_through_to_cpuinfo(self):
        self.assertOk(self.run_check(self.ld_so(GLIBC_NOT_SUPPORTED), self.cpuinfo(FLAGS_V3)))
        self.assertRefused(self.run_check(self.ld_so(GLIBC_NOT_SUPPORTED),
                                          self.cpuinfo(FLAGS_V3.replace('avx2', ''))))

    def test_unrecognised_ld_so_output_never_refuses_by_itself(self):
        self.assertOk(self.run_check(self.ld_so(UNRECOGNISED), self.cpuinfo(FLAGS_V3)))
        self.assertOk(self.run_check(self.ld_so(''), self.cpuinfo(FLAGS_V3)))
        self.assertRefused(self.run_check(self.ld_so(UNRECOGNISED), self.cpuinfo('fpu sse2 avx')))

    def test_cpuinfo_without_ld_so(self):
        self.assertOk(self.run_check('', self.cpuinfo(FLAGS_V3)))
        self.assertRefused(self.run_check('', self.cpuinfo(FLAGS_V3.replace('avx2', ''))))
        self.assertRefused(self.run_check('', self.cpuinfo(FLAGS_V3.replace('abm', ''))))
        self.assertRefused(self.run_check('', self.cpuinfo(FLAGS_V3.replace('bmi2', ''))))

    def test_flag_names_must_match_whole_words(self):
        self.assertRefused(self.run_check('', self.cpuinfo(FLAGS_V3.replace('avx2', 'avx2_fake'))))

    def test_nothing_to_check_does_not_refuse(self):
        self.assertOk(self.run_check('', self.cpuinfo(None)))              # empty file
        self.assertOk(self.run_check('', str(self.dir / 'missing')))       # unreadable
        self.assertOk(self.run_check(str(self.dir / 'no-such-ld.so'), self.cpuinfo(None)))

    def test_other_architectures_are_not_checked(self):
        self.assertOk(self.run_check('', self.cpuinfo('fpu'), arch='aarch64'))


if __name__ == '__main__':
    unittest.main()
