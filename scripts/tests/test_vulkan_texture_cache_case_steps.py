"""Bound cache cases separately without accepting partial or changed evidence."""
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
import copy
import json
import sys
import tempfile
import unittest
from uuid import uuid4

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import run_vulkan_texture_cache_tests as cache

class CacheCaseSteps(unittest.TestCase):
    invocation=dict(source_commit='source',executable=dict(sha256='a'*64))

    def test_each_actual_main_case_keeps_full_matrix_acceptance(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory=Path(temporary)
            commands=[]
            def driver(command,folder):
                case=folder.name;commands.append(case)
                root=Path(command[command.index('--cache-directory')+1])
                (root/'texturecache').mkdir(parents=True,exist_ok=True)
                (root/'texturecache/body').write_bytes(b'actual persistent cache')
                child=dict(passed=True,source_commit='source',viewer_executable_sha256='a'*64,
                    texture_cache=dict(process_instance=str(uuid4()),startup_policy=dict(LocalCacheVersion=1,LastJ2CVersion="codec"),configured_cache_size_mb=256,
                        grid_identity='util.aditi.lindenlab.com' if case=='warm' else 'util.agni.lindenlab.com',
                        account_identity='10000000-0000-0000-0000-000000000002' if case=='warm' else '10000000-0000-0000-0000-000000000001',
                        maximum_body_bytes=2 if case=='resize' else 1,maximum_entries=2 if case=='resize' else 1,
                        images=[dict(id=identity,gpu_rgba_sha256='b'*64) for identity in (cache.PROFILE,cache.STREAM)]))
                requests=[] if case in ('warm','read-only','resize') else [
                    dict(query=dict(texture_id=[cache.STREAM]),range_start=0),
                    dict(query=dict(texture_id=[cache.STREAM]),range_start=600)]
                (folder/'results.json').write_text(json.dumps(child),encoding='utf-8')
                (folder/'http-replay.json').write_text(json.dumps(requests),encoding='utf-8')
                (folder/'driver.json').write_text(json.dumps(dict(return_code=0,timed_out=False,
                    output_errors=[],output_file='driver-output.log')),encoding='utf-8')
                (folder/'driver-output.log').write_text('preserved driver output',encoding='utf-8')
                return 0
            with patch.object(cache,'matrix_invocation',return_value=self.invocation), \
                 patch.object(cache,'run_driver',side_effect=driver), \
                 patch.object(cache,'validate_case',side_effect=lambda child,*args:cache.image_rows(child)), \
                 patch.object(cache,'validate_pressure') as pressure, \
                 patch.object(cache,'validate_shutdown') as shutdown, \
                 patch.object(cache,'validate_restart') as restart, \
                 patch.object(cache,'mutate_entry',return_value={'intentional_mutation':True}):
                for index,case in enumerate(cache.CASES):
                    command=['cache','--build-directory',str(directory/'build'),'--runtime',str(directory/'runtime'),
                        '--harness-evidence',str(directory/'harness'),'--evidence',str(directory),'--case',case]
                    with patch.object(sys,'argv',command):self.assertEqual(cache.main(),0)
                    report=json.loads((directory/'results.json').read_text(encoding='utf-8'))
                    self.assertEqual(report['complete'],index==len(cache.CASES)-1)
                    self.assertEqual(report['passed'],index==len(cache.CASES)-1)
                    self.assertEqual(report['planned_cases'],list(cache.CASES))
                    self.assertEqual(list(report['cases']),list(cache.CASES[:index+1]))
                    self.assertEqual(commands,list(cache.CASES[:index+1]))
                self.assertGreater(pressure.call_count,0)
                self.assertGreater(shutdown.call_count,0)
                self.assertGreater(restart.call_count,0)
                state=json.loads((directory/'progress.json').read_text(encoding='utf-8'))
                with self.assertRaisesRegex(ValueError,'next unexecuted'):
                    cache.resume_matrix(directory,self.invocation,'codec-version')
                changed=copy.deepcopy(self.invocation);changed['executable']['sha256']='c'*64
                with self.assertRaisesRegex(ValueError,'changed'):
                    cache.resume_matrix(directory,changed,'codec-version')
                # Retain just the genuine cold prefix to exercise strict resume rejection.
                state['cases']={'cold':state['cases']['cold']}
                progress=directory/'progress.json'
                progress.write_text(json.dumps(state),encoding='utf-8')
                with self.assertRaisesRegex(ValueError,'next unexecuted'):
                    cache.resume_matrix(directory,self.invocation,'read-only')
                state['cases']['cold']['passed']=False
                progress.write_text(json.dumps(state),encoding='utf-8')
                with self.assertRaisesRegex(ValueError,'failed'):
                    cache.resume_matrix(directory,self.invocation,'warm')
                state['cases']['cold']['passed']=True
                progress.write_text(json.dumps(state),encoding='utf-8')
                cold=directory/state['cases']['cold']['evidence']
                (cold/'driver-output.log').unlink()
                with self.assertRaisesRegex(ValueError,'driver output'):
                    cache.resume_matrix(directory,self.invocation,'warm')
                (cold/'driver-output.log').write_text('preserved output',encoding='utf-8')
                report_path=cold/'results.json';original_report=report_path.read_bytes()
                changed=json.loads(original_report);changed['source_commit']='different-source'
                report_path.write_text(json.dumps(changed),encoding='utf-8')
                with self.assertRaisesRegex(ValueError,'identity changed'):
                    cache.resume_matrix(directory,self.invocation,'warm')
                report_path.write_bytes(original_report)
                (directory/state['run']/'shared-cache/texturecache/body').write_bytes(b'corrupt between cases')
                with self.assertRaisesRegex(ValueError,'Shared cache changed'):
                    cache.resume_matrix(directory,self.invocation,'warm')

    def test_invocation_binds_actual_staged_inputs_and_binary_bytes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);build=root/'build'
            stage=build/'newview'/('RelWithDebInfo' if sys.platform=='win32' else 'packaged/bin')
            stage.mkdir(parents=True)
            executable=stage/('viewer-bin.exe' if sys.platform=='win32' else 'viewer-bin')
            files=[executable,build/'parity-build-provenance.json',build/'newview/vulkan-ci-staging.json',
                   root/'runtime.json',root/'harness.json']
            for path in files:path.write_bytes(b'original staged input')
            args=SimpleNamespace(build_directory=build,runtime=files[-2],harness_evidence=files[-1],
                register_windows_manifests=False)
            original=cache.matrix_invocation(args)
            for path in files:
                path.write_bytes(b'changed staged input')
                self.assertNotEqual(cache.matrix_invocation(args),original)
                path.write_bytes(b'original staged input')
            self.assertEqual(cache.matrix_invocation(args),original)

    def test_each_workflow_case_retains_bounds_and_final_archive_validation(self):
        import yaml
        workflow=yaml.safe_load((cache.ROOT/'.github/workflows/software_vulkan.yml').read_text(encoding='utf-8'))
        source_steps=workflow['jobs']['source-evidence']['steps']
        dependency=next(line for line in (cache.ROOT/'scripts/ci_build_requirements.txt').read_text(encoding='utf-8').splitlines()
                        if line.startswith('PyYAML=='))
        installer=next(index for index,step in enumerate(source_steps)
                       if dependency in step.get('run','') and 'pip install' in step['run'])
        regression=next(index for index,step in enumerate(source_steps)
                        if 'test_vulkan_texture_cache_case_steps.py' in step.get('run',''))
        self.assertLess(installer,regression)
        steps=workflow['jobs']['graphics']['steps']
        cases=[step for step in steps if step.get('name','').startswith('Production R3 texture cache ')]
        self.assertEqual(len(cases),len(cache.CASES))
        for step,case in zip(cases,cache.CASES):
            self.assertEqual(step['timeout-minutes'],15)
            self.assertIn('--case '+case+' ',step['run'])
            self.assertIn('--evidence .ci/r3-texture-cache-evidence',step['run'])
        self.assertTrue(any('qualification evidence' in step.get('name','').lower() for step in steps))

if __name__=='__main__':unittest.main()
