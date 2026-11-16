#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""RenderShadowLightSize's default in settings.xml is LLShadowMethod::SOFTNESS_CENTRE (llshadowmethod.h), the centre of the
Shadow softness slider and the one named constant every C++ user takes (kSoftnessDefault in llprojectorshadowpool.h is that
constant, not a copy)."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
NEWVIEW = ROOT / 'indra' / 'newview'


class SoftnessDefault(unittest.TestCase):
    def test_settings_default_is_the_slider_centre(self):
        header = (NEWVIEW / 'llshadowmethod.h').read_text(encoding='utf-8')
        centre = float(re.search(r'SOFTNESS_CENTRE\s*=\s*([0-9.]+)f', header).group(1))
        settings = (NEWVIEW / 'app_settings' / 'settings.xml').read_text(encoding='utf-8')
        block = re.search(r'<key>RenderShadowLightSize</key>\s*<map>.*?</map>', settings, re.S).group(0)
        self.assertEqual(float(re.search(r'<real>([0-9.]+)</real>', block).group(1)), centre)

    def test_the_pool_header_does_not_repeat_the_number(self):
        pool = (NEWVIEW / 'llprojectorshadowpool.h').read_text(encoding='utf-8')
        self.assertIn('kSoftnessDefault = LLShadowMethod::SOFTNESS_CENTRE', pool)


if __name__ == '__main__':
    unittest.main()
