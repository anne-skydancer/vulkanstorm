# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""delete_buffers frees a bucket last written three frames ago, as its comment says (upstream #6155)."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


class VertexBufferFreeDelayTest(unittest.TestCase):
    def test_bucket_freed_is_the_oldest(self):
        text = (ROOT / 'indra/llrender/llvertexbuffer.cpp').read_text()
        start = text.index('static void delete_buffers')
        body = text[start:text.index('\n}\n', start)]
        count = int(re.search(r'BUCKET_COUNT\s*=\s*(\d+)', body).group(1))
        write = re.search(r'idx = LLImageGL::sFrameCount % BUCKET_COUNT', body)
        free = re.search(r'idx = \(LLImageGL::sFrameCount \+ (\d+)\) % BUCKET_COUNT', body)
        self.assertTrue(write and free)
        # The bucket freed at frame f holds frame f + offset - count (mod count): three frames back for 4 buckets.
        age = count - int(free.group(1))
        self.assertEqual(age, 3, 'the freed bucket must be 3 frames old, not %d' % age)


if __name__ == '__main__':
    unittest.main()
