# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Tests of the Slang shader tooling (scripts/shaders): the pin, the generator's post steps, the checks and the loader format.

The tests that run slangc use the pinned compiler from the tool cache and are skipped when it is not there, unless
SLANG_REQUIRED=1 (the CI job sets it, after scripts/shaders/slang_fetch.py)."""
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts/shaders'))
import slang_check as check  # noqa: E402
import slang_common as common  # noqa: E402
import slang_gen as gen  # noqa: E402

FIXTURES = ROOT / 'scripts/shaders/fixtures/src'
PIN = common.read_pin()


def have_slangc():
    try:
        common.find_slangc(PIN)
        return True
    except SystemExit:
        return False


REQUIRED = os.environ.get('SLANG_REQUIRED') == '1'
needs_slangc = unittest.skipUnless(have_slangc() or REQUIRED, 'pinned slangc not fetched (scripts/shaders/slang_fetch.py)')


class PinTest(unittest.TestCase):
    def test_pin_is_exact_and_checksummed(self):
        self.assertRegex(PIN['version'], r'^\d{4}\.\d+(\.\d+)?$')
        self.assertEqual(set(PIN['platforms']), {'windows-x86_64', 'linux-x86_64'})
        for key, entry in PIN['platforms'].items():
            with self.subTest(platform=key):
                self.assertRegex(entry['sha256'], r'^[0-9a-f]{64}$')
                self.assertTrue(entry['url'].startswith('https://github.com/shader-slang/slang/releases/download/'))
                self.assertIn(f'v{PIN["version"]}/', entry['url'])
                self.assertIn(PIN['version'], Path(entry['url']).name)

    def test_licence_row_names_the_pinned_version(self):
        text = (ROOT / 'doc/licensing.md').read_text(encoding='utf-8')
        row = next(line for line in text.split('\n') if line.startswith('| Slang'))
        self.assertIn('Apache-2.0 WITH LLVM-exception', row)
        self.assertIn(PIN['version'], row)
        self.assertIn('not shipped', row)


class PostprocessTest(unittest.TestCase):
    def test_version_and_core_extensions_are_dropped(self):
        body, hooks = gen.postprocess('#version 460\n#extension GL_ARB_texture_cube_map_array : require\nvoid main() {}\n', {})
        self.assertEqual(body, 'void main() {}\n')
        self.assertEqual(hooks, [])

    def test_unknown_extension_fails(self):
        with self.assertRaises(gen.GenError):
            gen.postprocess('#version 460\n#extension GL_KHR_vulkan_glsl : require\nvoid main() {}\n', {})

    def test_hooks_must_be_registered(self):
        body, hooks = gen.postprocess('#version 460\nint a = ((SSR));\n', {'SSR': 'x'})
        self.assertEqual(hooks, ['SSR'])
        with self.assertRaises(gen.GenError):
            gen.postprocess('#version 460\nint a = ((SSR));\n', {})

    def test_stage_split(self):
        self.assertEqual(len(gen.split_stages('#version 460\na\n#version 460\nb\n', 2)), 2)
        with self.assertRaises(gen.GenError):
            gen.split_stages('#version 460\na\n', 2)


class LoaderFormatTest(unittest.TestCase):
    """The loader's text (llgeneratedshader.h) and assemble_source() are one format; both test this literal."""
    FILE = '// Generated\n// hooks: SSR IS_AMD_CARD HERO\n\nvoid main(){}\n'
    EXPECTED = ('#version 460\n#define SSR 1\n#define IS_AMD_CARD 0\n#define HERO 0\n'
                '// Generated\n// hooks: SSR IS_AMD_CARD HERO\n\nvoid main(){}\n')

    def test_assemble_matches_the_cpp_literal(self):
        self.assertEqual(check.assemble_source('fragment', self.FILE, {'SSR': '1'}), self.EXPECTED)
        cpp = (ROOT / 'indra/llrender/tests/llgeneratedshader_test.cpp').read_text(encoding='utf-8')
        self.assertIn('"#version 460\\n#define SSR 1\\n#define IS_AMD_CARD 0\\n#define HERO 0\\n"', cpp)
        self.assertIn('"// Generated\\n// hooks: SSR IS_AMD_CARD HERO\\n\\nvoid main(){}\\n"', cpp)

    def test_hooks_only_in_the_leading_comment_block(self):
        self.assertEqual(common.header_hooks('// a\n// hooks: A B\n\nx'), ['A', 'B'])
        self.assertEqual(common.header_hooks('// a\n\n// hooks: A\n'), [])
        self.assertEqual(common.header_hooks('// hooks:\n'), [])


class LoaderSourceTest(unittest.TestCase):
    """What the loader must not do (read from the source: it is compiled by the viewer build, not here)."""

    def test_generated_path_has_no_fallback_or_limits(self):
        mgr = (ROOT / 'indra/llrender/llshadermgr.cpp').read_text(encoding='utf-8')
        start = mgr.index('GLuint LLShaderMgr::loadGeneratedShaderFile')
        body = mgr[start:mgr.index('bool LLShaderMgr::linkProgramObject')]
        for banned in ('loadShaderFile(', 'attachShaderFeatures', 'getShaderDirPrefix', '4096', 'strdup', 'shader_level'):
            self.assertNotIn(banned, body)
        self.assertIn('LLGeneratedShader::assemble', body)
        shader = (ROOT / 'indra/llrender/llglslshader.cpp').read_text(encoding='utf-8')
        self.assertIn('!generated && !LLShaderMgr::instance()->attachShaderFeatures(this)', shader)
        self.assertIn('!generated && mShaderLevel > 0', shader)


@needs_slangc
class GeneratorTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.out = Path(cls.tmp.name) / 'generated'
        cls.manifest = gen.generate(cls.out, FIXTURES, strict=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_every_permutation_and_stage_is_written(self):
        names = sorted(p.name for p in self.out.iterdir())
        self.assertEqual(names, ['programs.json', 'tint__DOUBLE0.frag.glsl', 'tint__DOUBLE0.vert.glsl',
                                 'tint__DOUBLE1.frag.glsl', 'tint__DOUBLE1.vert.glsl'])

    def test_output_is_deterministic(self):
        with tempfile.TemporaryDirectory() as again:
            gen.generate(again, FIXTURES, strict=True, jobs=1)
            self.assertEqual(gen.diff_dirs(self.out, again), [])

    def test_file_shape(self):
        frag = (self.out / 'tint__DOUBLE1.frag.glsl').read_text(encoding='utf-8')
        self.assertTrue(frag.startswith('// Generated by scripts/shaders/slang_gen.py'))
        self.assertIn('// SPDX-License-Identifier: GPL-3.0-or-later', frag)
        self.assertNotIn('#version', frag)
        self.assertEqual(common.header_hooks(frag), ['SSR'])
        self.assertIn('((SSR))', frag)
        self.assertIn('* 2.0', frag)
        self.assertNotIn('* 2.0', (self.out / 'tint__DOUBLE0.frag.glsl').read_text(encoding='utf-8'))
        self.assertNotIn('\r', frag)

    def test_checks_pass(self):
        self.assertEqual(check.check_manifest(self.out), [])
        self.assertEqual(check.check_interface(self.out), [])
        self.assertEqual(check.check_position(FIXTURES, self.out), [])

    def test_glslang_compiles_and_links(self):
        glslang = check.find_glslang()
        if glslang is None:
            self.skipTest('glslangValidator not installed')
        self.assertEqual(check.check_compile(self.out, glslang), [])

    def test_glslang_catches_a_broken_stage(self):
        glslang = check.find_glslang()
        if glslang is None:
            self.skipTest('glslangValidator not installed')
        with tempfile.TemporaryDirectory() as tmp:
            bad = Path(tmp) / 'generated'
            shutil.copytree(self.out, bad)
            frag = bad / 'tint__DOUBLE0.frag.glsl'
            frag.write_text(frag.read_text(encoding='utf-8').replace('void main()', 'void main(int x)'), encoding='utf-8')
            self.assertTrue(check.check_compile(bad, glslang))

    def test_drift_is_found(self):
        with tempfile.TemporaryDirectory() as tmp:
            copy = Path(tmp) / 'generated'
            shutil.copytree(self.out, copy)
            self.assertEqual(gen.diff_dirs(self.out, copy), [])
            frag = copy / 'tint__DOUBLE0.frag.glsl'
            frag.write_text(frag.read_text(encoding='utf-8') + '// hand edit\n', encoding='utf-8')
            (copy / 'stale.vert.glsl').write_text('x', encoding='utf-8')
            (copy / 'tint__DOUBLE1.vert.glsl').unlink()
            problems = gen.diff_dirs(self.out, copy)
            self.assertEqual(len(problems), 3)
            self.assertTrue(check.check_manifest(copy))

    def test_interface_check_finds_a_mismatch(self):
        with tempfile.TemporaryDirectory() as tmp:
            copy = Path(tmp) / 'generated'
            shutil.copytree(self.out, copy)
            mf = json.loads((copy / 'programs.json').read_text(encoding='utf-8'))
            frag = mf['programs'][0]['stages']['fragment']['interface']
            frag['inputs'][1]['type'] = 'float32x3'
            frag['inputs'].append({'name': 'extra', 'location': 7, 'type': 'float32'})
            (copy / 'programs.json').write_text(json.dumps(mf), encoding='utf-8')
            errors = check.check_interface(copy)
            self.assertEqual(len(errors), 2)

    def test_gate_catches_a_program_that_skips_the_module(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'src'
            shutil.copytree(FIXTURES, src)
            text = (src / 'tint.slang').read_text(encoding='utf-8')
            (src / 'tint.slang').write_text(text.replace('import position_mod;', ''), encoding='utf-8')
            errors = check.check_position(src, self.out)
            self.assertTrue(any('does not take its position' in e for e in errors), errors)
            (src / 'rogue.slang').write_text('// x\nimport glsl;\n[shader("vertex")] void main() { gl_Position = vec4(0); }\n',
                                             encoding='utf-8')
            errors = check.check_position(src, self.out)
            self.assertTrue(any('rogue.slang' in e and 'writes gl_Position' in e for e in errors), errors)

    def test_gate_is_allowed_empty_until_programs_exist(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'src'
            src.mkdir()
            (src / 'program_list.json').write_text('{"hooks": {}, "programs": []}', encoding='utf-8')
            self.assertEqual(check.check_position(src, self.out), [])
            (src / 'program_list.json').write_text(json.dumps({'hooks': {}, 'programs': [
                {'name': 'a', 'source': 'a.slang', 'entries': {'vertex': 'vs'}}]}), encoding='utf-8')
            self.assertTrue(check.check_position(src, self.out))  # a vertex program but no position module

    def test_source_without_a_licence_header_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'src'
            shutil.copytree(FIXTURES, src)
            text = (src / 'tint.slang').read_text(encoding='utf-8')
            (src / 'tint.slang').write_text(re.sub(r'(?m)^//.*\n', '', text), encoding='utf-8')
            with self.assertRaises(gen.GenError):
                gen.generate(Path(tmp) / 'out', src)

    def test_unregistered_hook_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'src'
            shutil.copytree(FIXTURES, src)
            plist = json.loads((src / 'program_list.json').read_text(encoding='utf-8'))
            plist['hooks'] = {}
            (src / 'program_list.json').write_text(json.dumps(plist), encoding='utf-8')
            with self.assertRaises(gen.GenError):
                gen.generate(Path(tmp) / 'out', src)


class CommittedTreeTest(unittest.TestCase):
    """The committed generated folder and its Slang sources agree (the slangc-free part of the drift check)."""

    def test_manifest_agrees_with_the_files(self):
        self.assertEqual(check.check_manifest(common.GENERATED), [])
        self.assertEqual(check.check_interface(common.GENERATED), [])
        self.assertEqual(check.check_position(common.SLANG_SRC, common.GENERATED), [])

    def test_every_slang_source_carries_the_licence_line(self):
        for path in common.SLANG_SRC.rglob('*.slang'):
            with self.subTest(path=str(path.relative_to(ROOT))):
                head = path.read_text(encoding='utf-8')[:4000]
                self.assertTrue('SPDX-License-Identifier' in head or 'LicenseInfo' in head)


if __name__ == '__main__':
    unittest.main()
