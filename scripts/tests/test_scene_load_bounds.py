# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Text guards for the scene load memory bounds (LLVOCacheEntry::updateDebugSettings, FIRE-32688).

A machine with less than 8 GB of RAM uses LL's bounds (768 and 2048 MB) instead of SceneLoadLowMemoryBound and
SceneLoadHighMemoryBound when they are larger. The test reads the installed RAM (LLMemory::getMaxMemKB), and the
LL bounds apply to the update that computes them only: the test read the available memory (under the 64-bit heap
cap, any working set above about 8 GB) and wrote the LL bounds into the user's settings, where they stayed.

    python3 scripts/tests/test_scene_load_bounds.py
    VKSTORM_TEST_ROOT=PATH python3 scripts/tests/test_scene_load_bounds.py   (another source tree)"""
import os
import re
import unittest
from pathlib import Path

ROOT = Path(os.environ.get('VKSTORM_TEST_ROOT') or Path(__file__).resolve().parents[2])
VOCACHE = (ROOT / 'indra/newview/llvocache.cpp').read_text(encoding='utf-8', errors='replace')


def body(text, signature):
    """The text of the function whose definition starts with `signature`, to its closing brace at column 0."""
    start = text.index(signature)
    end = text.index('\n}\n', start)
    return text[start:end]


def code(text):
    """`text` without its // comments."""
    return re.sub(r'//[^\n]*', '', text)


UPDATE = code(body(VOCACHE, 'void LLVOCacheEntry::updateDebugSettings()'))


class SceneLoadBounds(unittest.TestCase):
    def test_no_code_writes_the_bounds_into_the_settings(self):
        for path in sorted((ROOT / 'indra/newview').glob('*.cpp')):
            text = code(path.read_text(encoding='utf-8', errors='replace'))
            for name in ('SceneLoadLowMemoryBound', 'SceneLoadHighMemoryBound'):
                self.assertNotRegex(text, r'gSavedSettings\s*\.\s*set\w*\(\s*"' + name + '"',
                                    f'{path.name} writes {name}')

    def test_the_installed_ram_decides(self):
        test = re.search(r'if\s*\(\s*LLMemory::(\w+)\(\)\s*\*\s*KB_to_MB\s*<\s*8096\s*\)', UPDATE)
        self.assertIsNotNone(test)
        self.assertEqual(test.group(1), 'getMaxMemKB')
        self.assertNotIn('getAvailableMemKB', UPDATE)

    def test_the_ll_bounds_clamp_the_settings_locally(self):
        test = UPDATE.index('LLMemory::getMaxMemKB() * KB_to_MB < 8096')
        branch = UPDATE[test:UPDATE.index('}', test)]
        self.assertRegex(branch, r'low_bound_MB\s*=\s*llmin\(\s*low_bound_MB\s*,\s*768U\s*\)')
        self.assertRegex(branch, r'high_bound_MB\s*=\s*llmin\(\s*high_bound_MB\s*,\s*2048U\s*\)')
        self.assertRegex(UPDATE[:test], r'U32 low_bound_MB\s*=\s*low_mem_bound_MB;')
        self.assertRegex(UPDATE[:test], r'U32 high_bound_MB\s*=\s*high_mem_bound_MB;')

    def test_the_restrictions_use_the_clamped_bounds(self):
        test = UPDATE.index('LLMemory::getMaxMemKB() * KB_to_MB < 8096')
        after = UPDATE[test:]
        self.assertRegex(after, r'llclamp\(\s*allocated_mem \* KB_to_MB\s*,\s*\(F32\)\s*low_bound_MB\s*,'
                                r'\s*\(F32\)\s*high_bound_MB\s*\)')
        self.assertRegex(after, r'adjust_range\s*=\s*\(F32\)\(\s*high_bound_MB\s*-\s*low_bound_MB\s*\)')
        self.assertRegex(after, r'adjust_factor\s*=\s*\(\s*high_bound_MB\s*-\s*clamped_memory\s*\)')
        # the settings themselves are read once, into the local bounds
        self.assertEqual(len(re.findall(r'\blow_mem_bound_MB\b', after)), 0)
        self.assertEqual(len(re.findall(r'\bhigh_mem_bound_MB\b', after)), 0)


if __name__ == '__main__':
    unittest.main()
