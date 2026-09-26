import tempfile
import unittest
from pathlib import Path

from zink_capture import summarize


class CaptureAnalysisTests(unittest.TestCase):
    def analyze(self, body, pid=42):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'sample.csv'
            path.write_text(body, encoding='utf-8')
            return summarize(path, pid)

    def test_separates_streams_and_retains_hitches(self):
        result = self.analyze(
            'ProcessID,SwapChainAddress,MsBetweenPresents\n'
            '42,A,10\n42,A,20\n42,A,100\n42,B,5\n99,A,999\n')
        self.assertAlmostEqual(result['streams']['A']['mean_present_rate_hz'], 3000 / 130)
        self.assertEqual(result['streams']['A']['p99_ms'], 100)
        self.assertEqual(result['streams']['A']['intervals_over_50ms'], 1)
        self.assertEqual(result['streams']['B']['valid_intervals'], 1)
        self.assertEqual(result['visual_acceptance'], 'not_evaluated')

    def test_counts_invalid_values(self):
        result = self.analyze(
            'ProcessID,SwapChainAddress,MsBetweenPresents\n'
            '42,A,0\n42,A,-1\n42,A,NaN\n42,A,inf\n42,A,NA\n42,A,10\n')
        self.assertEqual(result['streams']['A']['invalid_intervals'], 5)
        self.assertEqual(result['streams']['A']['valid_intervals'], 1)

    def test_presentmon_26_legacy_header_case(self):
        result = self.analyze(
            'ProcessID,SwapChainAddress,msBetweenPresents,PresentMode\n'
            '42,A,10,Hardware: Independent Flip\n')
        self.assertEqual(result['streams']['A']['mean_present_rate_hz'], 100)

    def test_rejects_missing_schema_pid_and_empty_measurement(self):
        for body in ('wrong,header\n1,2\n',
                     'ProcessID,SwapChainAddress,MsBetweenPresents\n99,A,10\n',
                     'ProcessID,SwapChainAddress,MsBetweenPresents\n42,A,0\n'):
            with self.subTest(body=body), self.assertRaises(ValueError):
                self.analyze(body)


if __name__ == '__main__':
    unittest.main()
