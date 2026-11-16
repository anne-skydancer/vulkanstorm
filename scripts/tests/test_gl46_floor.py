# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""The OpenGL 4.6 floor: the viewer refuses to start below it, and the GL3-era code paths are gone.

Static checks over the source tree (nothing is built): the floor hook is in the viewer window's constructor, no
feature table has a GL3 / OpenGLPre30 / TexUnit8orLess list, the feature manager does not mask them or downgrade
Intel to GL 3.3, the shader header is one #version 420 (430 for compute), and the removed capability gates
(LLStochasticEngine::deviceSupported, LLShadowMethod::effective, the CPU texture-format conversion buffer) have no
user left in code or docs."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
INDRA = ROOT / 'indra'
NEWVIEW = INDRA / 'newview'


def read(path):
    return path.read_text(encoding='utf-8', errors='replace')


def code_files():
    for base in (INDRA,):
        for path in base.rglob('*'):
            if path.suffix in ('.cpp', '.h') and 'tests' not in path.parts and path.is_file():
                yield path


class GL46Floor(unittest.TestCase):
    def test_floor_hook_in_viewer_window(self):
        text = read(NEWVIEW / 'llviewerwindow.cpp')
        self.assertIn('LLGLFloor::enforce(mWindow)', text)
        # after the window is created, before the failed-window branch and the first shader load
        self.assertLess(text.index('LLWindowManager::createWindow(this'), text.index('LLGLFloor::enforce(mWindow)'))
        self.assertLess(text.index('LLGLFloor::enforce(mWindow)'), text.index('immediately initialize shaders'))
        self.assertIn('llglfloor.cpp', read(NEWVIEW / 'CMakeLists.txt'))
        self.assertIn('llglfloor.h', read(NEWVIEW / 'CMakeLists.txt'))

    def test_floor_is_4_6(self):
        header = read(NEWVIEW / 'llglfloor.h')
        self.assertIn('REQUIRED_MAJOR = 4', header)
        self.assertIn('REQUIRED_MINOR = 6', header)

    def test_no_gl3_lists(self):
        for table in sorted(NEWVIEW.glob('featuretable*.txt')):
            lists = [l.split()[1] for l in read(table).splitlines() if l.startswith('list ')]
            for gone in ('GL3', 'OpenGLPre30', 'TexUnit8orLess'):
                self.assertNotIn(gone, lists, table.name)

    def test_feature_manager_has_no_gl3_masks(self):
        text = read(NEWVIEW / 'llfeaturemanager.cpp')
        for gone in ('OpenGLPre30', '"GL3"', 'TexUnit8orLess', '3.3 fallback', 'mGLVersion'):
            self.assertNotIn(gone, text)

    def test_shader_header_is_one_version(self):
        text = read(INDRA / 'llrender/llshadermgr.cpp')
        versions = sorted(set(re.findall(r'#version (\d+)', text)))
        self.assertEqual(['420', '430'], versions)
        self.assertNotIn('precision mediump', text)

    def test_removed_gates_have_no_user(self):
        for path in code_files():
            text = read(path)
            for gone in ('deviceSupported', 'LLShadowMethod::effective', 'sManualScratch', 'allocateConversionBuffer',
                         'OpenGLPre30'):
                self.assertNotIn(gone, text, '%s: %s' % (path.relative_to(ROOT), gone))

    def test_docs_do_not_name_removed_gates(self):
        for path in list((ROOT / 'doc').rglob('*.md')) + [NEWVIEW / 'featuretable.txt', NEWVIEW / 'featuretable_linux.txt',
                                                         NEWVIEW / 'featuretable_mac.txt']:
            text = read(path)
            for gone in ('deviceSupported', 'LLShadowMethod::effective'):
                self.assertNotIn(gone, text, '%s: %s' % (path.relative_to(ROOT), gone))


if __name__ == '__main__':
    unittest.main()
