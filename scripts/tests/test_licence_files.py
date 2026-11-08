# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Check the GPL-3.0 licence texts, what the viewer ships of them, the headers and Vivox."""
from pathlib import Path
import hashlib
import re
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
VULKANSTORM = 'SPDX-License-Identifier: GPL-3.0-or-later'

# SPDX identifiers that can be part of a GPL-3.0-or-later work.
GPL3_COMPATIBLE = {
    'GPL-3.0-or-later', 'GPL-3.0-only', 'GPL-2.0-or-later', 'LGPL-2.1-or-later',
    'LGPL-2.1-only', 'LGPL-3.0-or-later', 'LGPL-3.0-only',
    'MIT', 'MIT-0', 'BSD-2-Clause', 'BSD-3-Clause', 'Zlib', 'Apache-2.0', 'ISC',
    'BSL-1.0', 'CC0-1.0', 'Unlicense', 'MPL-2.0', 'Libpng', 'FTL', 'OFL-1.1',
    'CC-BY-4.0', 'LicenseRef-Public-Domain',
}


class LicenceTextsTest(unittest.TestCase):
    def test_licence_is_the_gpl3_text_verbatim(self):
        data = (ROOT / 'LICENSE').read_bytes()
        self.assertEqual(hashlib.sha256(data).hexdigest(), GPL3_SHA256)

    def test_no_duplicate_or_dual_licence_files(self):
        for gone in ('LICENSE-GPL-3.0', 'doc/LGPL-license.txt', 'indra/newview/LGPL-license.txt'):
            with self.subTest(file=gone):
                self.assertFalse((ROOT / gone).exists())

    def test_lgpl_text_is_kept_for_the_upstream_headers(self):
        lgpl = (ROOT / 'LICENSE-LGPL-2.1').read_bytes()
        self.assertIn(b'GNU LESSER GENERAL PUBLIC LICENSE', lgpl)
        self.assertIn(b'Version 2.1, February 1999', lgpl)

    def test_notice_states_gpl3_and_section_3(self):
        text = (ROOT / 'NOTICE.md').read_text(encoding='utf-8')
        for needle in ('GPL-3.0-or-later', 'LGPL-2.1 is compatible with inclusion in a GPL-3.0 work', 'LICENSE-LGPL-2.1'):
            self.assertIn(needle, text)

    def test_shipped_texts_exist(self):
        destinations = {dst for _, dst in LICENCE_TEXTS}
        self.assertEqual(destinations, {'LICENSE.txt', 'LGPL-2.1-license.txt', 'NOTICE.md'})
        for src, _ in LICENCE_TEXTS:
            with self.subTest(src=src):
                self.assertTrue((NEWVIEW / src).resolve().is_file())

    def test_every_platform_ships_the_texts(self):
        manifest = (NEWVIEW / 'viewer_manifest.py').read_text(encoding='utf-8')
        # Windows, macOS and Linux construct() each call it once.
        self.assertEqual(manifest.count('self.path_licence_texts()'), 3)
        self.assertNotIn('LGPL-license.txt', manifest)

    def test_installer_page_holds_header_and_the_gpl3(self):
        gpl = (ROOT / 'LICENSE').read_text(encoding='ascii')
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory) / 'installer-licence.txt'
            write_installer_licence(out, ROOT / 'LICENSE')
            data = out.read_bytes()
        data.decode('ascii')
        self.assertNotIn(b'\n', data.replace(b'\r\n', b''))
        text = data.decode('ascii').replace('\r\n', '\n')
        self.assertTrue(text.startswith(INSTALLER_LICENCE_HEADER))
        self.assertIn(gpl.strip('\n'), text)
        self.assertNotIn('GNU LESSER GENERAL PUBLIC LICENSE', text)


class RussianDictionaryTest(unittest.TestCase):
    """The package's ru_RU dictionary is not freely licensed; the LibreOffice one replaces it."""

    def test_replacement_files_and_licence(self):
        folder = NEWVIEW / 'dictionaries-ru'
        self.assertTrue((folder / 'ru_ru.dic').stat().st_size > 1000000)
        self.assertTrue((folder / 'ru_ru.aff').read_bytes().startswith(b'SET UTF-8'))
        text = (folder / 'LICENSE-ru_ru.txt').read_text(encoding='utf-8')
        self.assertIn('Redistribution and use in source and binary forms', text)
        self.assertNotIn('modifications in patch form', text)
        manifest = (NEWVIEW / 'viewer_manifest.py').read_text(encoding='utf-8')
        self.assertIn('app_settings/dictionaries/ru_ru.dic', manifest)


def read_text_any(path):
    """A text file in UTF-8, UTF-8 with BOM, or UTF-16 with BOM."""
    data = path.read_bytes()
    if data[:2] in (b'\xff\xfe', b'\xfe\xff'):
        return data.decode('utf-16')
    if data[:3] == b'\xef\xbb\xbf':
        data = data[3:]
    return data.decode('utf-8', errors='replace')


class VivoxRemovedTest(unittest.TestCase):
    """Vivox (SLVoice) is proprietary and cannot ship with a GPL-3.0 viewer."""

    def test_no_vivox_files(self):
        names = subprocess.run(['git', '-C', str(ROOT), 'ls-files', '-z'], check=True,
                               capture_output=True).stdout.decode('utf-8').split('\0')
        self.assertEqual([n for n in names if 'vivox' in n.lower()], [])

    def test_no_vivox_in_build_files(self):
        for name in ('autobuild.xml', 'indra/newview/CMakeLists.txt',
                     'indra/cmake/Copy3rdPartyLibs.cmake', 'indra/cmake/ViewerMiscLibs.cmake',
                     'indra/newview/viewer_manifest.py', 'indra/newview/llvoiceclient.cpp',
                     'indra/newview/llvoiceclient.h', 'indra/newview/app_settings/settings.xml',
                     'indra/newview/installers/windows/installer_template.nsi'):
            text = read_text_any(ROOT / name).lower()
            with self.subTest(file=name):
                self.assertNotIn('vivox', text)
                self.assertNotIn('slvoice.exe', text)

    def test_no_vivox_in_installer_files(self):
        # Every NSIS/Inno file of the Windows installers, decoded by its own encoding
        # (the NSIS files are UTF-16 with a byte-order mark).
        inst = ROOT / 'indra/newview/installers/windows'
        for path in sorted(list(inst.glob('*.nsi')) + list(inst.glob('*.iss'))):
            with self.subTest(file=path.name):
                self.assertNotIn('vivox', read_text_any(path).lower())

    def test_no_slvoice_settings(self):
        text = (ROOT / 'indra/newview/app_settings/settings.xml').read_text(encoding='utf-8')
        for key in ('VoiceHost', 'VoicePort', 'VoiceLogFile'):
            self.assertNotIn('<key>%s</key>' % key, text)


def tracked_text_files():
    names = subprocess.run(['git', '-C', str(ROOT), 'ls-files', '-z'], check=True,
                           capture_output=True).stdout.decode('utf-8').split('\0')
    suffixes = {'.c', '.cpp', '.h', '.glsl', '.py', '.sh', '.cjs', '.js', '.cmake', '.rs', '.md', '.txt'}
    for name in names:
        path = ROOT / name
        if name and path.suffix in suffixes and path.is_file():
            yield name, path


class SourceHeaderTest(unittest.TestCase):
    def test_vulkanstorm_files_are_gpl3_or_later(self):
        offenders = []
        for name, path in tracked_text_files():
            head = path.read_bytes()[:1500].decode('utf-8', 'replace')
            if 'Copyright (C) 2026, Anne Skydancer' in head and VULKANSTORM not in head:
                offenders.append(name)
        self.assertEqual(offenders, [])

    def test_no_lgpl_option_in_source_headers(self):
        offenders = []
        for name, path in tracked_text_files():
            for line in path.read_bytes()[:3000].decode('utf-8', 'replace').splitlines():
                if 'SPDX-License-Identifier:' not in line:
                    continue
                expression = spdx_expression(line.split('SPDX-License-Identifier:', 1)[1])
                if 'LGPL' in expression and 'GPL-3.0-or-later' in expression.split():
                    offenders.append(name)
        self.assertEqual(offenders, [])

    def test_every_spdx_expression_can_be_part_of_a_gpl3_work(self):
        offenders = []
        for name, path in tracked_text_files():
            for line in path.read_bytes()[:3000].decode('utf-8', 'replace').splitlines():
                if 'SPDX-License-Identifier:' not in line:
                    continue
                expression = spdx_expression(line.split('SPDX-License-Identifier:', 1)[1])
                if expression and not gpl3_compatible(expression):
                    offenders.append((name, expression))
        self.assertEqual(offenders, [])

    def test_gpl3_compatible(self):
        for expression, expected in [
                ('GPL-3.0-or-later', True),
                ('LGPL-2.1-or-later OR GPL-3.0-or-later', True),
                ('(LGPL-2.1-only OR GPL-3.0-or-later) AND Zlib', True),
                ('BSD-3-Clause AND GPL-3.0-or-later', True),
                ('Apache-2.0', True),
                ('GPL-2.0-only', False),
                ('LicenseRef-Proprietary', False),
                ('MIT AND GPL-2.0-only', False),
                ('GPL-2.0-only OR MIT', True),
                ('GPL-2.0-only WITH Classpath-exception-2.0', False)]:
            with self.subTest(expression=expression):
                self.assertEqual(gpl3_compatible(expression), expected)

    def test_spdx_expression_ignores_prose(self):
        self.assertEqual(spdx_expression(' Apache-2.0. The derived name list'), 'Apache-2.0')
        self.assertEqual(spdx_expression(' GPL-3.0-or-later */'), 'GPL-3.0-or-later')
        self.assertEqual(spdx_expression("' not in line:"), '')


def spdx_expression(text):
    """The SPDX expression at the start of text, without trailing prose or comment syntax."""
    tokens = []
    for token in text.replace('(', ' ( ').replace(')', ' ) ').split():
        if token in ('AND', 'OR', 'WITH', '(', ')'):
            tokens.append(token)
            continue
        identifier = token.rstrip('.,;')
        if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9.+-]*', identifier):
            break
        tokens.append(identifier)
        if identifier != token:
            break
    while tokens and tokens[-1] in ('AND', 'OR', 'WITH', '('):
        tokens.pop()
    return ' '.join(tokens)


def alternatives(expression):
    """The SPDX expression in disjunctive form: a list of sets of licence ids."""
    tokens = expression.replace('(', ' ( ').replace(')', ' ) ').split()
    position = 0

    def parse_or():
        nonlocal position
        result = parse_and()
        while position < len(tokens) and tokens[position] == 'OR':
            position += 1
            result = result + parse_and()
        return result

    def parse_and():
        nonlocal position
        result = parse_term()
        while position < len(tokens) and tokens[position] == 'AND':
            position += 1
            right = parse_term()
            result = [a | b for a in result for b in right]
        return result

    def parse_term():
        nonlocal position
        token = tokens[position]
        position += 1
        if token == '(':
            result = parse_or()
            assert tokens[position] == ')', expression
            position += 1
            return result
        if position < len(tokens) and tokens[position] == 'WITH':
            position += 2
            token += ' WITH exception'  # an exception is not in the allowed set
        return [{token}]

    result = parse_or()
    assert position == len(tokens), expression
    return result


def gpl3_compatible(expression):
    """True when at least one alternative uses only licences a GPL-3.0 work can include."""
    return any(conjunction <= GPL3_COMPATIBLE for conjunction in alternatives(expression))


if __name__ == '__main__':
    unittest.main()
