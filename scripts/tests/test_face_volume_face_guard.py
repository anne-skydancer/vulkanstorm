# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Every LLFace read of a volume face by texture-entry index is range checked."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


class FaceVolumeFaceGuardTest(unittest.TestCase):
    def test_every_volume_face_read_by_te_offset_is_guarded(self):
        text = (ROOT / 'indra/newview/llface.cpp').read_text()
        lines = text.splitlines()
        reads = [i for i, line in enumerate(lines)
                 if re.search(r'getVolumeFace\((mTEOffset|face->getTEOffset\(\)|getTEOffset\(\))\)', line)
                 and not line.lstrip().startswith(('//', 'const LLVolumeFace& vol_face = rigged'))]
        self.assertGreaterEqual(len(reads), 3)
        for i in reads:
            window = '\n'.join(lines[max(0, i - 8):i + 1])
            self.assertRegex(window, r'te_has_volume_face\(|getNumVolumeFaces\(\)\s*<=?\s*mTEOffset|mTEOffset\s*<\s*volume->getNumVolumeFaces',
                             'unguarded getVolumeFace at llface.cpp:%d' % (i + 1))

    def test_guard_is_a_range_check(self):
        text = (ROOT / 'indra/newview/llface.cpp').read_text()
        self.assertRegex(text, r'te_offset >= 0 && te_offset < volume->getNumVolumeFaces\(\)')


if __name__ == '__main__':
    unittest.main()
