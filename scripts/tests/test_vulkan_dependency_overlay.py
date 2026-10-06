"""Reject tampered patches and unrelated edits while permitting a known overlay."""
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import build_vulkan_dependencies as builder


class OverlayTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(); self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name); self.source = self.root / 'source'; self.source.mkdir()
        self.git('init')
        (self.source / 'file.cpp').write_text('before\n')
        self.git('add', 'file.cpp')
        self.git('-c', 'user.name=Overlay test', '-c', 'user.email=test@example.invalid', 'commit', '-m', 'fixture')
        revision = self.git('rev-parse', 'HEAD').strip()
        self.pin = {'url': 'unused', 'revision': revision}
        overlay = self.root / 'fix.patch'
        overlay.write_text('diff --git a/file.cpp b/file.cpp\n--- a/file.cpp\n+++ b/file.cpp\n@@ -1 +1 @@\n-before\n+after\n')
        self.overlay = {'path': 'fix.patch', 'sha256': builder.normalized_hash(overlay),
                        'files': {'file.cpp': hashlib.sha256(b'after\n').hexdigest()}}
        root = patch.object(builder, 'ROOT', self.root); root.start(); self.addCleanup(root.stop)

    def git(self, *args):
        return subprocess.check_output(['git', '-C', str(self.source), *args], text=True, stderr=subprocess.DEVNULL)

    def test_apply_and_idempotent_reuse(self):
        builder.checkout(self.source, self.pin, [self.overlay])
        builder.checkout(self.source, self.pin, [self.overlay])
        self.assertEqual('after\n', (self.source / 'file.cpp').read_text())

    def test_bad_patch_checksum(self):
        (self.root / 'fix.patch').write_text('tampered\n')
        with self.assertRaisesRegex(RuntimeError, 'Patch checksum'):
            builder.checkout(self.source, self.pin, [self.overlay])

    def test_unknown_source_edit(self):
        (self.source / 'file.cpp').write_text('unapproved\n')
        with self.assertRaisesRegex(RuntimeError, 'unapproved changes'):
            builder.checkout(self.source, self.pin, [self.overlay])

    def test_expected_result_must_match(self):
        self.overlay['files']['file.cpp'] = '0' * 64
        with self.assertRaisesRegex(RuntimeError, 'Patched source checksum'):
            builder.checkout(self.source, self.pin, [self.overlay])

    def second_overlay(self):
        overlay = self.root / 'second.patch'
        overlay.write_text('diff --git a/file.cpp b/file.cpp\n--- a/file.cpp\n+++ b/file.cpp\n@@ -1 +1 @@\n-after\n+final\n')
        return {'path': 'second.patch', 'sha256': builder.normalized_hash(overlay),
                'files': {'file.cpp': hashlib.sha256(b'final\n').hexdigest()}}

    def test_overlapping_stack_is_applied_and_reused(self):
        overlays = [self.overlay, self.second_overlay()]
        builder.checkout(self.source, self.pin, overlays)
        builder.checkout(self.source, self.pin, overlays)
        self.assertEqual('final\n', (self.source / 'file.cpp').read_text())

    def test_intermediate_hash_cannot_be_hidden_by_final_hash(self):
        overlays = [self.overlay, self.second_overlay()]
        self.overlay['files']['file.cpp'] = '0' * 64
        with self.assertRaisesRegex(RuntimeError, 'Patched source checksum'):
            builder.checkout(self.source, self.pin, overlays)


if __name__ == '__main__':
    unittest.main()
