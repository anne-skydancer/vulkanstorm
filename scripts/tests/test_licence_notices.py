# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Check that every bundled third-party library ships its notice and no unshipped one does.

Source checks only. With a staged viewer folder as the argument
(`test_licence_notices.py <staged dir>`) it also checks the staged licenses
folder and licenses.txt of a finished build.
"""
from pathlib import Path
import os
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
NEWVIEW = ROOT / 'indra/newview'
sys.path.insert(0, str(NEWVIEW))
from licence_texts import (PACKAGE_LICENCES_NOT_SHIPPED, PACKAGE_ROOT_LICENCES,
                           package_licence_files)

# A staged viewer folder as the first argument also checks a finished build.
STAGED = sys.argv.pop(1) if len(sys.argv) > 1 and not sys.argv[1].startswith('-') else None

# Texts of components Vulkanstorm never ships (doc/licensing.md, "Off in our builds").
UNSHIPPED_WORDS = ('Kakadu', 'KDU', 'Vivox', 'FMOD', 'Havok', 'BugSplat', 'Discord',
                   'Logitech', 'FontShop', 'SSLeay', 'JPEG Library 6b', 'JPEG2000 License',
                   'xmlrpc-epi', 'ELFIO')
# Licence field values of autobuild.xml packages that are not free software.
PROPRIETARY = {'Proprietary', 'internal', 'havok', 'fmod', 'Kakadu', 'discord_sdk'}


def installables():
    """{package: (licence, licence file path)} from autobuild.xml."""
    def to_dict(element):
        kids = list(element)
        return {kids[i].text: kids[i + 1] for i in range(0, len(kids), 2)}
    top = to_dict(ET.parse(ROOT / 'autobuild.xml').getroot().find('map'))
    out = {}
    for name, value in to_dict(top['installables']).items():
        fields = to_dict(value)
        licence = fields.get('license')
        licence_file = fields.get('license_file')
        out[name] = (licence.text if licence is not None else None,
                     licence_file.text if licence_file is not None else None)
    return out


class NoticeTextTest(unittest.TestCase):
    def read(self, name):
        return (NEWVIEW / name).read_text(encoding='utf-8')

    def test_no_text_of_an_unshipped_component(self):
        for name in ('licenses-win32.txt', 'licenses-linux.txt'):
            text = self.read(name)
            for word in UNSHIPPED_WORDS:
                with self.subTest(file=name, word=word):
                    self.assertNotIn(word, text)

    def test_openjpeg_bsd2_notice_with_clause_2(self):
        for name in ('licenses-win32.txt', 'licenses-linux.txt'):
            text = self.read(name)
            with self.subTest(file=name):
                self.assertIn('OpenJPEG License', text)
                self.assertIn('Universite catholique de Louvain', text)
                self.assertIn('2. Redistributions in binary form must reproduce the above copyright', text)

    def test_libjpeg_turbo_replaces_libjpeg_6b(self):
        for name in ('licenses-win32.txt', 'licenses-linux.txt'):
            text = self.read(name)
            with self.subTest(file=name):
                self.assertIn('libjpeg-turbo', text)
                self.assertIn('Independent JPEG Group', text)

    def test_openssl_is_apache_not_the_1x_licence(self):
        for name in ('licenses-win32.txt', 'licenses-linux.txt'):
            text = self.read(name)
            with self.subTest(file=name):
                self.assertIn('Apache License, Version 2.0', text)
                self.assertNotIn('This product includes cryptographic software written by Eric Young', text)


class ManifestListTest(unittest.TestCase):
    def test_every_installed_notice_is_copied_except_the_unshipped(self):
        with tempfile.TemporaryDirectory() as d:
            names = ['openjpeg.txt', 'libjpeg-turbo.txt', 'kdu.txt', 'havok.txt', 'soloud.txt',
                     'fmodstudio.txt', 'BUGSPLAT_LICENSE.txt', 'curl-buildinfo.json', 'zlib-ng.txt']
            for name in names:
                Path(d, name).write_text('x')
            os.mkdir(os.path.join(d, 'sub.txt'))
            self.assertEqual(package_licence_files(d),
                             ['libjpeg-turbo.txt', 'openjpeg.txt', 'zlib-ng.txt'])

    def test_manifest_stages_the_package_notices_on_every_platform(self):
        manifest = (NEWVIEW / 'viewer_manifest.py').read_text(encoding='utf-8')
        # Text check: the loop is called once, in ViewerManifest.construct (common to all platforms).
        self.assertEqual(manifest.count('package_licence_files('), 1)
        self.assertIn('PACKAGE_ROOT_LICENCES', manifest)

    def test_autobuild_licence_files_are_all_covered(self):
        root_names = set(PACKAGE_ROOT_LICENCES)
        for name, (licence, path) in installables().items():
            if path is None:
                continue
            base = path.split('/')[-1]
            with self.subTest(package=name):
                if licence in PROPRIETARY:
                    # A proprietary notice is never staged.
                    self.assertTrue(base in PACKAGE_LICENCES_NOT_SHIPPED or not base.endswith('.txt'),
                                    f'{name}: proprietary {base} would be shipped')
                elif base in PACKAGE_LICENCES_NOT_SHIPPED:
                    pass  # an off-by-default option: explicit in licence_texts.py
                else:
                    # staged by the *.txt rule, the root list, or the LGPL text (viewer-manager)
                    self.assertTrue(base.endswith('.txt') and (path.startswith('LICENSES/') or base in root_names)
                                    or name == 'viewer-manager',
                                    f'{name}: {path} is not staged by the manifest')

    def test_unshipped_names_are_not_listed_as_shipped(self):
        self.assertFalse(set(PACKAGE_ROOT_LICENCES) & PACKAGE_LICENCES_NOT_SHIPPED)


@unittest.skipUnless(STAGED, 'no staged folder given')
class StagedTest(unittest.TestCase):
    def test_staged_licenses(self):
        lic = Path(STAGED) / 'licenses'
        have = {p.name for p in lic.iterdir()}
        # soloud*: staged by option; the Russian dictionary name is the manifest's own LibreOffice licence
        for banned in PACKAGE_LICENCES_NOT_SHIPPED - {'soloud.txt', 'soloud-miniaudio.txt', 'ru_ru-dictionary-license.txt'}:
            self.assertNotIn(banned, have)
        for needed in ('openjpeg.txt', 'libjpeg-turbo.txt', 'openssl.txt', 'zlib-ng.txt', 'curl.txt',
                       'mikktspace.txt'):
            self.assertIn(needed, have)
        text = (Path(STAGED) / 'licenses.txt').read_text(encoding='utf-8')
        for word in UNSHIPPED_WORDS:
            self.assertNotIn(word, text)
        self.assertIn('OpenJPEG License', text)


if __name__ == '__main__':
    unittest.main()
