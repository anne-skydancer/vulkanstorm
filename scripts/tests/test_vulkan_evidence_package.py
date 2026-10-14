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
from package_vulkan_evidence import package, completeness
from run_vulkan_geometry_tests import required_checks


class EvidencePackageTest(unittest.TestCase):
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
                        if 'geometry' in error.lower()]
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


if __name__ == '__main__':
    unittest.main()
