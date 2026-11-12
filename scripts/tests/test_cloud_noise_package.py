# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Check scripts/clouds/check_cloud_noise.py on good and bad synthetic files, and that the viewer's pieces agree
on the cloud noise package (CloudNoise.cmake, autobuild.xml, the manifest and the start-up check)."""
from pathlib import Path
import hashlib
import re
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts/clouds'))
import check_cloud_noise as chk


def make_container(mutate=None):
    """A valid container with synthetic payloads (the layout of 3p-cloudnoise's cloudnoise_gen)."""
    payloads = [bytes([(i * 7 + k) & 255 for i in range(256)]) * (4 * w * h * d // 256)
                for k, (_, w, h, d) in enumerate(chk.ENTRIES)]
    header = bytearray(chk.MAGIC + struct.pack('<III', 1, chk.HEADER_BYTES, 3) + b'TVN 1f879a26 glm 0.9.7 gen 1.0.0')
    offset = chk.HEADER_BYTES
    for (kind, w, h, d), payload in zip(chk.ENTRIES, payloads):
        header += struct.pack('<IIIIIQQ', kind, 1, w, h, d, offset, len(payload)) + hashlib.sha256(payload).digest()
        offset += len(payload)
    header += bytes(chk.HEADER_BYTES - len(header))
    data = bytearray(bytes(header) + b''.join(payloads))
    if mutate:
        mutate(data)
    return bytes(data)


def flip(offset):
    def f(data):
        data[offset] ^= 1
    return f


class CloudNoiseCheckTest(unittest.TestCase):
    def check(self, data, expect=None, licences=None):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'cloud_noise.vkcn'
            path.write_bytes(data)
            return chk.check(path, expect or hashlib.sha256(data).hexdigest(), licences)

    def test_good_file(self):
        self.assertEqual(len(make_container()), chk.FILE_BYTES)
        self.assertEqual(self.check(make_container()), hashlib.sha256(make_container()).hexdigest())

    def rejected(self, data, expect=None):
        with self.assertRaises(chk.NoiseError):
            self.check(data, expect)

    def test_bad_magic_version_header(self):
        self.rejected(make_container(flip(0)))
        self.rejected(make_container(flip(8)))   # format version
        self.rejected(make_container(flip(12)))  # header size
        self.rejected(make_container(flip(16)))  # entry count

    def test_bad_entry_fields(self):
        self.rejected(make_container(flip(chk.FIRST_ENTRY)))                       # kind
        self.rejected(make_container(flip(chk.FIRST_ENTRY + 4)))                   # format
        self.rejected(make_container(flip(chk.FIRST_ENTRY + 8)))                   # width
        self.rejected(make_container(flip(chk.FIRST_ENTRY + chk.ENTRY_BYTES + 20)))  # offset
        self.rejected(make_container(flip(chk.FIRST_ENTRY + 2 * chk.ENTRY_BYTES + 28)))  # length
        self.rejected(make_container(flip(chk.FIRST_ENTRY + 36)))                  # an entry's hash

    def test_truncated_or_extended(self):
        good = make_container()
        self.rejected(good[:-1])
        self.rejected(good[:chk.HEADER_BYTES - 1])
        self.rejected(b'')
        self.rejected(good + b'\0')

    def test_flipped_payload_byte_in_each_entry(self):
        offset = chk.HEADER_BYTES
        for _, w, h, d in chk.ENTRIES:
            data = make_container(flip(offset + 5))
            # the whole-file hash can be made to match, the entry's own hash cannot
            self.rejected(data, hashlib.sha256(data).hexdigest())
            offset += 4 * w * h * d

    def test_wrong_expected_hash(self):
        self.rejected(make_container(), '0' * 64)

    def test_read_only_requirement(self):
        import os
        import stat
        data = make_container()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'cloud_noise.vkcn'
            path.write_bytes(data)
            sha = hashlib.sha256(data).hexdigest()
            with self.assertRaises(chk.NoiseError):
                chk.check(path, sha, read_only=True)
            os.chmod(path, stat.S_IREAD)
            try:
                chk.check(path, sha, read_only=True)
            finally:
                os.chmod(path, stat.S_IREAD | stat.S_IWRITE)

    def test_licences_required(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(chk.NoiseError):
                self.check(make_container(), licences=directory)
            for name in chk.LICENCES:
                (Path(directory) / name).write_text('licence')
            self.check(make_container(), licences=directory)


class CloudNoisePackageWiringTest(unittest.TestCase):
    def test_cmake_names_a_hash_and_the_autobuild_entry_matches_the_release(self):
        sha = chk.expected_from_cmake(chk.DEFAULT_CMAKE)
        self.assertRegex(sha, r'^[0-9a-f]{64}$')
        autobuild = (ROOT / 'autobuild.xml').read_text(encoding='utf-8')
        entry = autobuild[autobuild.index('<key>cloudnoise</key>'):]
        entry = entry[:entry.index('<key>colladadom</key>')]
        self.assertIn('<key>common</key>', entry)
        self.assertRegex(entry, r'<key>hash_algorithm</key>\s*<string>sha1</string>')
        self.assertRegex(entry, r'<key>hash</key>\s*<string>[0-9a-f]{40}</string>')
        self.assertIn('github.com/anne-skydancer/3p-cloudnoise/releases/download/', entry)
        self.assertIn('<string>MIT</string>', entry)

    def test_cmake_source_has_both_fatal_checks(self):
        text = (ROOT / 'indra/cmake/CloudNoise.cmake').read_text(encoding='utf-8')
        self.assertIn('use_prebuilt_binary(cloudnoise)', text)
        self.assertEqual(len(re.findall(r'message\(FATAL_ERROR', text)), 2)
        self.assertIn('file(SHA256', text)
        header = (ROOT / 'indra/newview/llcloudnoisehash.h.in').read_text(encoding='utf-8')
        self.assertIn('@CLOUD_NOISE_SHA256@', header)

    def test_manifest_source_mentions_the_file_and_the_licences(self):
        text = (ROOT / 'indra/newview/viewer_manifest.py').read_text(encoding='utf-8')
        self.assertIn("CLOUD_NOISE_FILE_NAME = 'cloud_noise.vkcn'", text)
        self.assertIn("dst='app_settings/clouds'", text)
        for name in chk.LICENCES:
            self.assertIn(name, text)
        self.assertIn('Attribs: readonly', text)
        self.assertIn('0o444', text)
        self.assertNotIn('isfile(noise_file)', text)  # a missing file is an error, not a skip

    def test_startup_check_source_shape(self):
        text = (ROOT / 'indra/newview/llappviewer.cpp').read_text(encoding='utf-8')
        start = text.index('LLCloudNoise::loadAndVerify')
        block = text[start:text.index('// </Vulkanstorm>', start)]
        # until L7-2 draws the clouds, a missing or damaged file is a warning, not an exit
        self.assertIn('LL_WARNS("InitInfo")', block)
        self.assertNotIn('OSMessageBox', block)
        self.assertNotIn('return false;', block)
        self.assertNotIn('LL_ERRS', block)


if __name__ == '__main__':
    unittest.main()
