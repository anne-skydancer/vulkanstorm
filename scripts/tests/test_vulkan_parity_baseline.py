"""Independent failures for capture evidence, image comparisons and timing data."""
from pathlib import Path
import json
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import run_vulkan_parity_baseline as baseline
import vulkan_parity_inventory as inventory


class BaselineTest(unittest.TestCase):
    def image(self, directory, name, pixels, width=2, height=1):
        path = Path(directory)/name
        path.write_bytes(f'P6\n{width} {height}\n255\n'.encode()+bytes(pixels))
        return path

    def test_independent_pixel_tolerance(self):
        with tempfile.TemporaryDirectory() as d:
            a = self.image(d,'a',[0,10,255,50,100,150])
            b = self.image(d,'b',[1,11,254,51,99,150])
            self.assertTrue(baseline.compare(a,b,1)['matched'])
            b = self.image(d,'b',[2,11,254,51,99,150])
            result = baseline.compare(a,b,1)
            self.assertFalse(result['matched'])
            self.assertEqual(result['channels_over_tolerance'],1)

    def test_orientation_is_not_silently_corrected(self):
        with tempfile.TemporaryDirectory() as d:
            a = self.image(d,'a',[255,0,0,0,0,255],1,2)
            b = self.image(d,'b',[0,0,255,255,0,0],1,2)
            self.assertFalse(baseline.compare(a,b,1)['matched'])

    def test_incomplete_readback_fails(self):
        with tempfile.TemporaryDirectory() as d:
            path = self.image(d,'bad',[0,0,0])
            with self.assertRaisesRegex(ValueError,'Incomplete'): baseline.read_ppm(path)

    def test_extent_mismatch_fails(self):
        with tempfile.TemporaryDirectory() as d:
            a = self.image(d,'a',[0]*6)
            b = self.image(d,'b',[0]*6,1,2)
            with self.assertRaisesRegex(ValueError,'extents'): baseline.compare(a,b,1)

    def test_finite_metrics_and_unavailable_gpu(self):
        frame = {n:2 for n in ('pre_render_cpu_ms','render_submit_cpu_ms','capture_wait_and_io_ms')}
        result = baseline.metrics([frame]*5)
        self.assertIsNone(result['gpu_ms'])
        self.assertEqual(result['render_submit_cpu_ms']['count'],5)
        for invalid in (-1,float('nan'),float('inf')):
            frame['pre_render_cpu_ms'] = invalid
            with self.assertRaises(ValueError): baseline.metrics([frame])

    def test_semantic_ids_preserve_variant_identity(self):
        self.assertNotEqual(inventory.stable_id('DRAW','PASS_SIMPLE'),
                            inventory.stable_id('DRAW','PASS_SIMPLE_RIGGED'))
        self.assertEqual(inventory.stable_id('UI','Avatar.Profile'),'UI.Avatar.Profile')

    def test_contract_has_no_hidden_masks_or_performance_claim(self):
        contract = json.loads((inventory.OUT/'baseline-contract.json').read_text())
        self.assertEqual(contract['comparison']['masked_regions'],[])
        self.assertIn('not a sustained',contract['metrics']['statistics'])
        self.assertIn('fixed connected-world',contract['required_r0_witnesses'][3])


if __name__ == '__main__': unittest.main()
