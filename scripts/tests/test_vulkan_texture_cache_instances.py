"""Concurrent cache qualification must require real, distinct, correctly owned viewers."""
import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from run_vulkan_texture_cache_instances import validate_instances, require_live_process
from package_vulkan_evidence import concurrent_cache_completeness
import json
import tempfile
import os


class ConcurrentCacheTests(unittest.TestCase):
    def test_actual_current_process_is_alive_and_invalid_ids_are_rejected(self):
        require_live_process(os.getpid())
        for value in (0,-1,True,'process'):
            with self.assertRaises(ValueError):require_live_process(value)

    def test_archive_requires_the_concurrent_case_and_safe_artifact_paths(self):
        self.assertTrue(concurrent_cache_completeness({},'revision','binary'))
        with tempfile.TemporaryDirectory() as temporary:
            path=Path(temporary)/'results.json'
            key='.ci/r3-texture-cache-instances/results.json'
            result=dict(passed=True,source_commit='revision',viewer_executable_sha256='binary')
            for escaped in ('../outside/primary','/outside/primary','C:/outside/primary'):
                result['primary']=dict(evidence=escaped)
                path.write_text(json.dumps(result))
                self.assertIn('Unsafe',concurrent_cache_completeness({key:path},'revision','binary')[0])

    def test_marker_ownership_and_cache_immutability_are_decisive(self):
        primary=dict(source_commit='revision',viewer_executable_sha256='binary',texture_cache=dict(
            second_instance=False,read_only=False,process_id=1,process_instance='first'))
        sibling=dict(source_commit='revision',viewer_executable_sha256='binary',texture_cache=dict(
            second_instance=True,read_only=True,process_id=2,process_instance='second'))
        files={'texturecache/header':dict(bytes=4,sha256='header')}
        validate_instances(primary,sibling,files,files,True)
        for values in (dict(second_instance=False),dict(read_only=False),dict(process_id=1),dict(process_instance='first')):
            invalid=copy.deepcopy(sibling);invalid['texture_cache'].update(values)
            with self.subTest(values=values),self.assertRaises(ValueError):
                validate_instances(primary,invalid,files,files,True)
        for before,after,held in (({},files,True),(files,{},True),(files,files,False)):
            with self.assertRaises(ValueError):
                validate_instances(primary,sibling,before,after,held)
        for key in ('source_commit','viewer_executable_sha256'):
            invalid=copy.deepcopy(sibling);invalid[key]='different'
            with self.assertRaises(ValueError):
                validate_instances(primary,invalid,files,files,True)


if __name__=='__main__':unittest.main()
