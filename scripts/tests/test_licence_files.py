# SPDX-License-Identifier: LGPL-2.1-or-later OR GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Check the dual-licence texts, what the viewer ships of them, and the headers."""
from pathlib import Path
import hashlib
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
NEWVIEW = ROOT / 'indra/newview'
sys.path.insert(0, str(NEWVIEW))
from licence_texts import (INSTALLER_LICENCE_HEADER, LICENCE_TEXTS,
                           write_installer_licence)

# sha256 of https://www.gnu.org/licenses/gpl-3.0.txt
GPL3_SHA256 = '3972dc9744f6499f0f9b2dbf76696f2ae7ad8af9b23dde66d6af86c9dfb36986'
DUAL = 'SPDX-License-Identifier: LGPL-2.1-or-later OR GPL-3.0-or-later'


class LicenceTextsTest(unittest.TestCase):
    def test_gpl3_text_is_verbatim(self):
        data = (ROOT / 'LICENSE-GPL-3.0').read_bytes()
        self.assertEqual(hashlib.sha256(data).hexdigest(), GPL3_SHA256)

    def test_lgpl_copies_are_identical(self):
        lgpl = (ROOT / 'LICENSE-LGPL-2.1').read_bytes()
        self.assertIn(b'GNU LESSER GENERAL PUBLIC LICENSE', lgpl)
        self.assertIn(b'Version 2.1, February 1999', lgpl)
        for copy in ('doc/LGPL-license.txt', 'indra/newview/LGPL-license.txt'):
            with self.subTest(copy=copy):
                self.assertEqual((ROOT / copy).read_bytes(), lgpl)

    def test_licence_statement_names_both_texts(self):
        text = (ROOT / 'LICENSE').read_text(encoding='ascii')
        for needle in ('LICENSE-LGPL-2.1', 'LICENSE-GPL-3.0', 'NOTICE.md',
                       'LGPL-2.1-only OR GPL-3.0-or-later',
                       'LGPL-2.1-or-later OR GPL-3.0-or-later'):
            self.assertIn(needle, text)

    def test_shipped_texts_exist(self):
        destinations = {dst for _, dst in LICENCE_TEXTS}
        self.assertEqual(destinations, {'LGPL-license.txt', 'GPL-3.0-license.txt', 'LICENSE.txt'})
        for src, _ in LICENCE_TEXTS:
            with self.subTest(src=src):
                self.assertTrue((NEWVIEW / src).resolve().is_file())

    def test_every_platform_ships_the_texts(self):
        manifest = (NEWVIEW / 'viewer_manifest.py').read_text(encoding='utf-8')
        # Windows, macOS and Linux construct() each call it once.
        self.assertEqual(manifest.count('self.path_licence_texts()'), 3)
        self.assertNotIn("'doc', 'LGPL-license.txt'", manifest)

    def test_installer_page_holds_header_and_both_licences(self):
        lgpl = (NEWVIEW / 'LGPL-license.txt').read_text(encoding='ascii')
        gpl = (ROOT / 'LICENSE-GPL-3.0').read_text(encoding='ascii')
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory) / 'installer-licence.txt'
            write_installer_licence(out, NEWVIEW / 'LGPL-license.txt', ROOT / 'LICENSE-GPL-3.0')
            data = out.read_bytes()
        data.decode('ascii')
        self.assertNotIn(b'\n', data.replace(b'\r\n', b''))
        text = data.decode('ascii').replace('\r\n', '\n')
        self.assertTrue(text.startswith(INSTALLER_LICENCE_HEADER))
        self.assertIn(lgpl.strip('\n'), text)
        self.assertIn(gpl.strip('\n'), text)
        self.assertLess(text.index('GNU LESSER GENERAL PUBLIC LICENSE'),
                        text.index('GNU GENERAL PUBLIC LICENSE\n                       Version 3'))


def tracked_text_files():
    names = subprocess.run(['git', '-C', str(ROOT), 'ls-files', '-z'], check=True,
                           capture_output=True).stdout.decode('utf-8').split('\0')
    suffixes = {'.c', '.cpp', '.h', '.glsl', '.py', '.sh', '.cjs', '.js', '.cmake', '.rs', '.md', '.txt'}
    for name in names:
        path = ROOT / name
        if name and path.suffix in suffixes and path.is_file():
            yield name, path


class SourceHeaderTest(unittest.TestCase):
    def test_vulkanstorm_files_do_not_claim_lgpl_only(self):
        offenders = []
        for name, path in tracked_text_files():
            head = path.read_bytes()[:1500].decode('utf-8', 'replace')
            if 'Copyright (C) 2026, Anne Skydancer' in head and DUAL not in head:
                offenders.append(name)
        self.assertEqual(offenders, [])

    def test_no_gpl_only_spdx_expression(self):
        # GPL-only code would remove the LGPL option (NOTICE.md, "GPL-only code").
        offenders = []
        for name, path in tracked_text_files():
            for line in path.read_bytes()[:3000].decode('utf-8', 'replace').splitlines():
                if 'SPDX-License-Identifier:' not in line:
                    continue
                expression = line.split('SPDX-License-Identifier:', 1)[1]
                if 'GPL' in expression.replace('LGPL', '') and 'LGPL' not in expression:
                    offenders.append(name)
        self.assertEqual(offenders, [])


if __name__ == '__main__':
    unittest.main()
