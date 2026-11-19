# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""LLUUID::getRandomSeed keeps its seed buffer on the stack: every thread's first random number calls it."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


class UuidSeedLocalTest(unittest.TestCase):
    def test_seed_buffer_is_not_static(self):
        text = (ROOT / 'indra/llcommon/lluuid.cpp').read_text()
        start = text.index('U32 LLUUID::getRandomSeed()')
        body = text[start:text.index('\n}\n', start)]
        self.assertNotRegex(body, r'\bstatic\b')
        self.assertRegex(body, r'unsigned char seed\[16\]\s*=\s*\{\}')
        self.assertNotIn('(uuid_time_t*)', body)


if __name__ == '__main__':
    unittest.main()
