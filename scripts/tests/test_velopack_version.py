"""Verify both packagers preserve the requested dotted source-count schema."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'indra/newview'))
from viewer_manifest import _velopack_pack_version


class VelopackVersionTest(unittest.TestCase):
    def test_stable_build_uses_dot_before_count(self):
        self.assertEqual(_velopack_pack_version(['1', '0', '0', '123']), '1.0.0.123')

    def test_stable_count_boundary_keeps_separate_numeric_components(self):
        for count in ('99999', '100000'):
            self.assertEqual(_velopack_pack_version(['1', '0', '0', count]), f'1.0.0.{count}')

    def test_canary_build_uses_dot_before_count(self):
        self.assertEqual(_velopack_pack_version(['1', '0', '0-canary', '123']), '1.0.0-canary.123')


if __name__ == '__main__':
    unittest.main()
