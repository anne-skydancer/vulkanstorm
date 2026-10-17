"""Evidence packaging must preserve regular bytes and expose special-file exclusions."""
import hashlib
import json
from pathlib import Path
import stat
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from package_vulkan_evidence import package, completeness, texture_cache_completeness
from run_vulkan_texture_cache_tests import CASES as TEXTURE_CACHE_CASES
from run_vulkan_geometry_tests import required_checks


class EvidencePackageTest(unittest.TestCase):
    def test_r3_matrix_rejects_missing_cases_and_unsafe_paths(self):
        self.assertTrue(texture_cache_completeness({},'revision','digest'))
        with tempfile.TemporaryDirectory() as temporary:
            summary=Path(temporary)/'results.json'
            files={'.ci/r3-texture-cache-evidence/results.json':summary}
            summary.write_text(json.dumps(dict(passed=True,complete=True,planned_cases=[],cases={})))
            self.assertIn('Incomplete',texture_cache_completeness(files,'revision','digest')[0])
            for path in ('../outside/cold','/outside/cold','C:/outside/cold'):
                cases={case:dict(passed=True,source_commit='revision',evidence=path) for case in TEXTURE_CACHE_CASES}
                summary.write_text(json.dumps(dict(passed=True,complete=True,planned_cases=list(TEXTURE_CACHE_CASES),cases=cases)))
                with patch('package_vulkan_evidence.validate_texture_cache_case') as validate:
                    self.assertIn('Unsafe',texture_cache_completeness(files,'revision','digest')[0])
                    validate.assert_not_called()

    def test_r3_readbacks_must_actually_enter_the_archive(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);prefix='.ci/r3-texture-cache-evidence';folder=prefix+'/run-unit/cold'
            files={}
            def put(name,value):
                path=root/name;path.parent.mkdir(parents=True,exist_ok=True)
                path.write_text(json.dumps(value));files[name]=path
            cases={case:dict(passed=True,source_commit='revision',evidence='run-unit/'+case,
                            process_instance='unit-process',texture_requests=1) for case in TEXTURE_CACHE_CASES}
            put(prefix+'/results.json',dict(passed=True,complete=True,planned_cases=list(TEXTURE_CACHE_CASES),cases=cases))
            stages={name:dict(gpu_rgba_file='r3/missing.rgba') for name in
                    ('coarse','full_resolution_preference','coarse_after_preference','resolution_limit','restored')}
            stages['references']={}
            put(folder+'/results.json',dict(source_commit='revision',viewer_executable_sha256='digest',
                texture_cache=dict(process_instance='unit-process',images=[]),r3_texture_streaming=stages))
            for name in ('session-replay.xml','viewer.log','vk_layer_settings.txt','r3/texture.j2c'):put(folder+'/'+name,{})
            put(folder+'/http-replay.json',[dict(query=dict(texture_id=['unit-texture']))])
            put(folder+'/driver.json',dict(return_code=0,timed_out=False,output_file='driver-output.log',output_errors=[]))
            put(folder+'/driver-output.log','actual output')
            # Isolate archive membership from the independently tested pixel validator.
            with patch('package_vulkan_evidence.validate_texture_cache_case') as validate:
                errors=texture_cache_completeness(files,'revision','digest')
                validate.assert_called_once()
                self.assertIn('omitted from archive',errors[0])
            driver=files.pop(folder+'/driver-output.log')
            self.assertIn('Missing R3 cache artifact',texture_cache_completeness(files,'revision','digest')[0])
            files[folder+'/driver-output.log']=driver

    def test_regular_bytes_and_hashes_round_trip(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / 'evidence').mkdir()
            payload = b'\x00\xff qualification image'
            (root / 'evidence/readback.rgba').write_bytes(payload)
            result = package(root, root / 'upload', ['evidence'], 'build', False)
            with zipfile.ZipFile(root / 'upload/qualification-evidence.zip') as archive:
                self.assertEqual(archive.read('evidence/readback.rgba'), payload)
                self.assertIsNone(archive.testzip())
            self.assertEqual(result['files']['evidence/readback.rgba']['sha256'],
                             hashlib.sha256(payload).hexdigest())
            self.assertIsNone(result['complete'])  # Partial failure evidence is not acceptance.

    def test_special_entries_are_named_and_never_read(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / 'evidence').mkdir()
            modes = {'socket': stat.S_IFSOCK, 'fifo': stat.S_IFIFO,
                     'symlink': stat.S_IFLNK, 'device': stat.S_IFCHR}
            for name in modes:
                (root / 'evidence' / name).touch()
            original = Path.lstat
            def lstat(path):
                result = original(path)
                if path.name in modes:
                    return type('Stat', (), {'st_mode': modes[path.name]})()
                return result
            with patch.object(Path, 'lstat', lstat):
                result = package(root, root / 'upload', ['evidence'], 'build', False)
            self.assertEqual(len(result['excluded']), 4)
            self.assertEqual(result['files'], {})
            with zipfile.ZipFile(root / 'upload/qualification-evidence.zip') as archive:
                self.assertEqual(archive.namelist(), [])
            self.assertEqual({item['type'] for item in result['excluded']},
                             {'socket', 'fifo', 'symlink', 'device-or-special'})

    def test_missing_required_evidence_fails_but_retains_diagnostics(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / 'viewer.log').write_text('crash diagnostics')
            with self.assertRaisesRegex(RuntimeError, 'Qualification evidence incomplete'):
                package(root, root / 'upload', ['viewer.log'], 'build', True)
            manifest = json.loads((root / 'upload/manifest.json').read_text())
            self.assertFalse(manifest['complete'])
            self.assertTrue(manifest['errors'])
            with zipfile.ZipFile(root / 'upload/qualification-evidence.zip') as archive:
                self.assertEqual(archive.read('viewer.log'), b'crash diagnostics')

    def test_r0_artifact_manifest_rejects_missing_corrupt_and_traversal(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp)
            (root/'.ci/runtime').mkdir(parents=True)
            (root/'.ci/runtime/runtime.json').write_text(json.dumps({'driver':'lavapipe'}))
            files={'.ci/runtime/runtime.json':root/'.ci/runtime/runtime.json'}
            digest=hashlib.sha256(b'comparison image').hexdigest()
            for folder in ('.ci/parity-evidence','.ci/world-parity-evidence'):
                directory=root/folder;directory.mkdir()
                report=directory/'baseline-results.json'
                report.write_text(json.dumps({'artifact_sha256':{'difference.ppm':digest}}))
                files[folder+'/baseline-results.json']=report
            def artifact_errors():
                return [error for error in completeness(files,'build-linux-x86_64')
                        if 'difference.ppm' in error or 'Invalid R0 artifact' in error]
            self.assertEqual(len(artifact_errors()),2)
            for folder in ('.ci/parity-evidence','.ci/world-parity-evidence'):
                image=root/folder/'difference.ppm';image.write_bytes(b'comparison image')
                files[folder+'/difference.ppm']=image
            self.assertEqual(artifact_errors(),[])
            (root/'.ci/world-parity-evidence/difference.ppm').write_bytes(b'corrupt')
            self.assertIn('R0 artifact checksum mismatch',artifact_errors()[0])
            report=root/'.ci/world-parity-evidence/baseline-results.json'
            report.write_text(json.dumps({'artifact_sha256':{'../outside.ppm':digest}}))
            self.assertIn('Invalid R0 artifact path',artifact_errors()[0])

    def test_geometry_evidence_rejects_missing_corrupt_and_substituted_checks(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp)
            directory=root/'.ci/geometry-evidence';directory.mkdir(parents=True)
            checks=required_checks(21)
            log=('\n'.join(checks)+'\n').encode()
            report=directory/'results.json'
            data=dict(passed=True,checks=checks,tree_species=21,source_commit='a'*40,
                      executable_sha256='b'*64,tree_species_sha256='c'*64,
                      log_sha256=hashlib.sha256(log).hexdigest())
            report.write_text(json.dumps(data))
            files={'.ci/geometry-evidence/results.json':report}
            def geometry_errors():
                return [error for error in completeness(files,'build')
                        if 'geometry' in error.lower() and 'R2' not in error]
            self.assertTrue(geometry_errors())
            logfile=directory/'geometry.log';logfile.write_bytes(log)
            files['.ci/geometry-evidence/geometry.log']=logfile
            self.assertEqual(geometry_errors(),[])
            logfile.write_bytes(b'corrupt')
            self.assertTrue(any('checksum' in error for error in geometry_errors()))
            logfile.write_bytes(log)
            data['checks']=[checks[0]]*5;report.write_text(json.dumps(data))
            self.assertTrue(any('Incomplete' in error for error in geometry_errors()))
            data['checks']=checks;data['tree_species']=0;report.write_text(json.dumps(data))
            self.assertTrue(geometry_errors())

    def test_output_cannot_recursively_package_itself(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / 'evidence').mkdir()
            with self.assertRaisesRegex(ValueError, 'overlaps'):
                package(root, root / 'evidence/upload', ['evidence'], 'build', False)

    def test_r2_missing_or_unsafe_artifacts_fail_before_opening_them(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            prefix = '.ci/r2-session-evidence'
            directory = root/prefix/'r2'
            directory.mkdir(parents=True)
            manifest = directory/'artifact-hashes.json'
            contracts = directory/'contracts.xml'
            contracts.write_text('<llsd><map/></llsd>')
            files = {prefix+'/r2/artifact-hashes.json': manifest, prefix+'/r2/contracts.xml': contracts}
            for artifacts in ([dict(path='../outside.rgba')], [dict(path='missing.rgba')], ['invalid record'], 'invalid manifest'):
                manifest.write_text(json.dumps(dict(artifacts=artifacts)))
                with patch('package_vulkan_evidence.r2_contracts_passed') as validate:
                    errors = completeness(files, 'build')
                    validate.assert_not_called()
                self.assertTrue(any('R2' in error for error in errors))

    def test_build_stage_and_session_provenance_cannot_be_mixed(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            files = {}
            revision = 'a'*40
            digest = 'b'*64
            def put(name, value, text=False):
                path = root/name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(value if text else json.dumps(value))
                files[name] = path
            put('.ci/evidence/source.txt', revision+'\n', True)
            provenance = dict(complete=True, source_commit=revision, executable_sha256=digest,
                              executable='C:/build/newview/RelWithDebInfo/vulkanstorm-bin.exe')
            put('build/parity-build-provenance.json', provenance)
            put('build/newview/vulkan-ci-staging.json', dict(files={'vulkanstorm-bin.exe': digest}))
            put('.ci/geometry-evidence/results.json', dict(source_commit=revision))
            session = dict(passed=True, exit_code=0, source_qualified=True, source_commit=revision,
                           viewer_executable_sha256=digest)
            for name in ('r1', 'r2'):
                put(f'.ci/{name}-session-evidence/results.json', session)
            def provenance_errors():
                return [error for error in completeness(files, 'build') if 'provenance' in error]
            self.assertEqual(provenance_errors(), [])
            put('.ci/r2-session-evidence/results.json', dict(session, viewer_executable_sha256='c'*64))
            self.assertTrue(any('Session' in error for error in provenance_errors()))
            put('.ci/r2-session-evidence/results.json', session)
            put('build/newview/vulkan-ci-staging.json', dict(files={'vulkanstorm-bin.exe': 'c'*64}))
            self.assertTrue(any('Staged' in error for error in provenance_errors()))
            put('build/newview/vulkan-ci-staging.json', dict(files={'vulkanstorm-bin.exe': digest}))
            put('.ci/geometry-evidence/results.json', dict(source_commit='c'*40))
            self.assertTrue(any('CPU producer' in error for error in provenance_errors()))
            put('build/parity-build-provenance.json', dict(provenance, complete=False))
            self.assertTrue(any('Incomplete' in error for error in provenance_errors()))


if __name__ == '__main__':
    unittest.main()
