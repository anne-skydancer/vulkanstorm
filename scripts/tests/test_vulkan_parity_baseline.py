"""Independent failures for capture evidence, image comparisons and timing data."""
from pathlib import Path
import json
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import run_vulkan_parity_baseline as baseline
import vulkan_parity_inventory as inventory
import check_vulkan_parity_r0 as acceptance


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
        self.assertIn('fixed production-world',contract['required_r0_witnesses'][3])

    def test_discovery_cannot_accept_features_without_evidence(self):
        ledger={'entries':[dict(id='UI.Sample',reference_sources=['sample.cpp'],native_owner='owner',
            phase='R9',dependencies=['R1'],implementation='partial',remaining_gap='finish owner',
            source_qualified=True,software_runtime_qualified=False,authenticated_runtime_qualified=False,evidence=[])]}
        errors=acceptance.validate(ledger,{}, {})
        self.assertIn('Feature accepted without evidence: UI.Sample',errors)
        self.assertIn('No production world replay witness',errors)

    def test_empty_or_failed_runtime_evidence_never_closes_r0(self):
        errors=acceptance.validate({'entries':[]},{'cases':[{'backend':'OpenGL','passed':False}]},{})
        self.assertTrue(errors)
        self.assertTrue(any('independent peer' in e for e in errors))
        self.assertTrue(any('source revisions' in e for e in errors))

    def test_survey_reference_symbols_are_preserved(self):
        ledger=json.loads((inventory.OUT/'acceptance-ledger.json').read_text())
        row=next(r for r in ledger['entries'] if r['id']=='SURVEY.Standard-primitives-linksets')
        self.assertIn('LLVolume::generate',row['reference_symbols'])
        self.assertIn('LLVOVolume::updateGeometry',row['reference_seams'])

    def test_computed_setting_registrations_are_not_omitted(self):
        rows=json.loads((inventory.OUT/'acceptance-ledger.json').read_text())['entries']
        ids={r['id'] for r in rows}
        for index in range(1,5):
            self.assertIn('SETTING.gSavedSettings.LocalTerrainAsset'+str(index),ids)
            for suffix in ('ScaleU','ScaleV','Rotation','OffsetU','OffsetV'):
                self.assertIn('SETTING.gSavedSettings.LocalTerrainTransform'+str(index)+suffix,ids)
        self.assertIn('SETTING.gSavedSettings.RestrainedLove',ids)

    def test_uniform_or_single_object_cannot_satisfy_world_oracle(self):
        with tempfile.TemporaryDirectory() as d:
            for color in ([100,100,100],[180,40,40],[40,180,40]):
                path=self.image(d,'empty',color*1024,32,32)
                with self.assertRaisesRegex(ValueError,'Missing visible'):
                    baseline.fixture_pixels(path)
            pixels=[]
            for y in range(32):
                for x in range(32): pixels.extend([40,180,40] if x<16 else [180,40,40])
            self.assertGreater(min(baseline.fixture_pixels(self.image(d,'both',pixels,32,32)).values()),32)
            # Object identity survives reference gamma/tone mapping; exact peer
            # color differences still belong to the unmasked pixel comparison.
            mapped=[channel for y in range(32) for x in range(32) for channel in ([109,198,133] if x<16 else [198,109,133])]
            self.assertGreater(min(baseline.fixture_pixels(self.image(d,'mapped',mapped,32,32)).values()),32)

    def test_stale_claimed_artifact_fails_verification(self):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'baseline-results.json'
            errors=acceptance.verify_files({'artifact_sha256':{'missing.ppm':'claimed'}},path,'source')
            self.assertTrue(any('Changed or missing' in e for e in errors))
            self.assertTrue(any('Build/inventory' in e for e in errors))

    def test_zero_or_negative_timing_is_not_gpu_measurement(self):
        frame={n:0 for n in ('pre_render_cpu_ms','render_submit_cpu_ms','capture_wait_and_io_ms')}
        self.assertIsNone(baseline.metrics([frame])['gpu_ms'])


if __name__ == '__main__': unittest.main()
