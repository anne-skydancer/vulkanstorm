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

    def test_stage_identity_distinguishes_the_position_writers(self):
        position = 'void main() { gl_Position = vec4(0); }'
        self.assertEqual(gen.emitted_stage(position), 'vertex')
        self.assertEqual(gen.emitted_stage('layout(max_vertices = 2) out;\n' + position), 'geometry')
        self.assertEqual(gen.emitted_stage('layout(local_size_x = 1) in;'), 'compute')
        self.assertEqual(gen.emitted_stage('void main() {}'), 'fragment')


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
    """A tripwire, not a behaviour test: what the loader must not do and must keep, read from the source (the viewer
    build compiles it, not this suite)."""

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
        # the binary-cache key follows the file's text, and a hook sees the viewer-wide defines
        self.assertIn('readGeneratedShaderText(shdr_pair.first', shader)
        self.assertIn('LLGLSLShader::sGlobalDefines', body)


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

    def test_geometry_stream_keeps_linked_interfaces_and_positions(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'src'
            src.mkdir()
            shutil.copy2(FIXTURES / 'position_mod.slang', src)
            shutil.copy2(FIXTURES.parent / 'geometry.slang', src)
            programs = {
                'position': {'module': 'position_mod', 'prefix': 'vkPosition'},
                'programs': [{'name': 'lines', 'source': 'geometry.slang',
                              'entries': {'geometry': 'gsMain', 'fragment': 'fsMain', 'vertex': 'vsMain'}}],
            }
            (src / 'program_list.json').write_text(json.dumps(programs), encoding='utf-8')
            out = Path(tmp) / 'generated'
            manifest = gen.generate(out, src, strict=True)
            stages = manifest['programs'][0]['stages']
            self.assertEqual(list(stages), ['vertex', 'geometry', 'fragment'])
            geom = stages['geometry']['interface']
            self.assertEqual([(i['location'], i['type']) for i in geom['inputs']], [(2, 'float32x4'), (5, 'float32x2[2]')])
            self.assertEqual([(i['location'], i['type']) for i in geom['outputs']], [(7, 'float32x4')])
            self.assertEqual(check.check_manifest(out), [])
            self.assertEqual(check.check_interface(out), [])
            self.assertEqual(check.check_position(src, out), [])
            glslang = check.find_glslang()
            if glslang:
                self.assertEqual(check.check_compile(out, glslang), [])

            geom['inputs'][0]['type'] = 'float32x3'
            stages['fragment']['interface']['inputs'][0]['location'] = 8
            (out / 'programs.json').write_text(json.dumps(manifest), encoding='utf-8')
            errors = check.check_interface(out)
            self.assertEqual(len(errors), 2, errors)
            self.assertTrue(any('vertex writes' in e and 'geometry reads' in e for e in errors), errors)
            self.assertTrue(any('fragment input' in e and 'no geometry output' in e for e in errors), errors)

            geometry_file = out / stages['geometry']['file']
            text = geometry_file.read_text(encoding='utf-8')
            text, removed = re.subn(r'\bgl_Position\s*=\s*[^;]+;', '', text, count=1)
            self.assertEqual(removed, 1)
            geometry_file.write_text(text, encoding='utf-8')
            errors = check.check_position(src, out)
            self.assertTrue(any('lines.geom.glsl' in e and 'before each emitted vertex' in e for e in errors), errors)

            module = src / 'position_mod.slang'
            module.write_text(module.read_text(encoding='utf-8') + '\n#define VK_POSITION_INVARIANT 1\n', encoding='utf-8')
            errors = check.check_position(src, out)
            for stage in ('vert', 'geom'):
                self.assertTrue(any(f'lines.{stage}.glsl' in e and 'invariant' in e for e in errors), errors)

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

    def test_gate_catches_a_program_that_imports_the_module_but_computes_the_position(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'src'
            shutil.copytree(FIXTURES, src)
            text = (src / 'tint.slang').read_text(encoding='utf-8')
            line = 'o.pos = vkPositionClip(modelview_projection_matrix, v.position);'
            self.assertIn(line, text)
            for rogue in ('o.pos = modelview_projection_matrix * vec4(v.position, 1.0);',
                          'o.pos = vkPositionClip(modelview_projection_matrix, v.position); o.pos.z = 0.0;',
                          'o.pos = vkPositionClip(modelview_projection_matrix, v.position); o.pos *= 2.0;'):
                (src / 'tint.slang').write_text(text.replace(line, rogue), encoding='utf-8')
                errors = check.check_position(src, self.out)
                self.assertTrue(any('SV_Position field' in e for e in errors), (rogue, errors))
            (src / 'tint.slang').write_text(text.replace('Varyings vsMain(VertexIn v)', 'vec4 vsMain(VertexIn v) : SV_Position'),
                                            encoding='utf-8')
            self.assertTrue(any('straight from a function' in e for e in check.check_position(src, self.out)))

    def test_gate_catches_a_position_altered_after_the_module_call(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'src'
            shutil.copytree(FIXTURES, src)
            text = (src / 'tint.slang').read_text(encoding='utf-8')
            line = 'o.pos = vkPositionClip(modelview_projection_matrix, v.position);'
            for rogue in ('o.pos = vkPositionClip(modelview_projection_matrix, v.position) * 2.0;',
                          'o.pos = vkPositionClip(modelview_projection_matrix, v.position) + vec4(1);'):
                (src / 'tint.slang').write_text(text.replace(line, rogue), encoding='utf-8')
                self.assertTrue(any('SV_Position field' in e for e in check.check_position(src, self.out)), rogue)

    def test_gate_catches_a_position_built_by_an_initialiser_list(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'src'
            shutil.copytree(FIXTURES, src)
            text = (src / 'tint.slang').read_text(encoding='utf-8')
            line = 'o.pos = vkPositionClip(modelview_projection_matrix, v.position);'
            self.assertIn(line, text)
            rogue = line + ' Varyings p = { vec4(0), o.color };'
            (src / 'tint.slang').write_text(text.replace(line, rogue), encoding='utf-8')
            self.assertTrue(any('initialiser list' in e for e in check.check_position(src, self.out)))

    def test_interface_check_finds_block_and_attribute_problems(self):
        with tempfile.TemporaryDirectory() as tmp:
            copy = Path(tmp) / 'generated'
            shutil.copytree(self.out, copy)
            vert = copy / 'tint__DOUBLE0.vert.glsl'
            vert.write_text(vert.read_text(encoding='utf-8').replace(f'binding = {common.GLOBALS_BINDING}', 'binding = 0'), encoding='utf-8')
            errors = check.check_interface(copy)
            self.assertTrue(any('inside the viewer' in e for e in errors), errors)
            shutil.copy(self.out / 'tint__DOUBLE0.vert.glsl', vert)
            frag = copy / 'tint__DOUBLE0.frag.glsl'
            frag.write_text(frag.read_text(encoding='utf-8').replace('vec4 tint;', 'vec4 tint2;'), encoding='utf-8')
            self.assertTrue(any('different uniform blocks' in e for e in check.check_interface(copy)))
            shutil.copy(self.out / 'tint__DOUBLE0.frag.glsl', frag)
            mf = json.loads((copy / 'programs.json').read_text(encoding='utf-8'))
            for prog in mf['programs']:
                prog['stages']['vertex']['interface']['inputs'][1]['location'] = 3
            (copy / 'programs.json').write_text(json.dumps(mf), encoding='utf-8')
            self.assertTrue(any('reserved table' in e for e in check.check_interface(copy)))

    def test_texture_unit_order_is_checked(self):
        with tempfile.TemporaryDirectory() as tmp:
            copy = Path(tmp) / 'generated'
            shutil.copytree(self.out, copy)
            self.assertEqual(check.check_interface(copy), [])
            mf = json.loads((copy / 'programs.json').read_text(encoding='utf-8'))
            for prog in mf['programs']:
                for info in prog['stages'].values():
                    for tex in info['interface']['textures']:
                        tex['binding'] += 1   # a gap at unit 0, and diffuseMap off unit 0
            (copy / 'programs.json').write_text(json.dumps(mf), encoding='utf-8')
            errors = check.check_interface(copy)
            self.assertTrue(any('in a row' in e for e in errors), errors)
            self.assertTrue(any('diffuseMap' in e for e in errors), errors)

    def test_block_name_is_the_one_the_loader_asks_for(self):
        header = (ROOT / 'indra/llrender/llgeneratedshader.h').read_text(encoding='utf-8')
        name = re.search(r'kBlockName\s*=\s*"(\w+)"', header).group(1)
        for path in self.out.glob('*.glsl'):
            block = check.block_of(path.read_text(encoding='utf-8'))
            if block:
                self.assertEqual(block[1], name, path.name)

    def test_generated_block_clears_the_viewers_own_bindings(self):
        self.assertEqual(common.GLOBALS_BINDING, common.reserved_block_count())
        vert = (self.out / 'tint__DOUBLE0.vert.glsl').read_text(encoding='utf-8')
        self.assertEqual(check.block_of(vert)[0], common.GLOBALS_BINDING)

    def test_gate_is_armed_even_with_no_program(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'src'
            src.mkdir()
            (src / 'program_list.json').write_text('{"hooks": {}, "programs": []}', encoding='utf-8')
            self.assertTrue(check.check_position(src, self.out))  # no "position" entry
            (src / 'program_list.json').write_text(json.dumps({'hooks': {}, 'position': {'module': 'position'},
                                                               'programs': []}), encoding='utf-8')
            self.assertTrue(check.check_position(src, self.out))  # the module file is missing
            (src / 'program_list.json').write_text(json.dumps({'hooks': {}, 'programs': [
                {'name': 'a', 'source': 'a.slang', 'entries': {'vertex': 'vs'}}]}), encoding='utf-8')
            self.assertTrue(check.check_position(src, self.out))  # a vertex program but no position module

    def test_glsl_baseline_only_shrinks(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / 'shaders'
            (root / 'class1').mkdir(parents=True)
            (root / 'generated').mkdir()
            (root / 'class1' / 'oldV.glsl').write_text('// gl_Position = x;\nvoid main() { gl_Position = vec4(0); }\n',
                                                       encoding='utf-8')
            (root / 'class1' / 'noV.glsl').write_text('void main() { bool b = gl_Position.x == 1.0; }\n', encoding='utf-8')
            (root / 'generated' / 'a.vert.glsl').write_text('void main() { gl_Position = vec4(0); }\n', encoding='utf-8')
            base = Path(tmp) / 'baseline.txt'
            base.write_text('# comment\nclass1/oldV.glsl\n', encoding='utf-8')
            self.assertEqual(check.check_glsl_baseline(root, base), [])
            (root / 'class1' / 'newV.glsl').write_text('void main() { gl_Position.z = 0.0; }\n', encoding='utf-8')
            errors = check.check_glsl_baseline(root, base)
            self.assertEqual(len(errors), 1)
            self.assertIn('newV.glsl', errors[0])
            (root / 'class1' / 'newV.glsl').unlink()
            base.write_text('class1/oldV.glsl\nclass1/goneV.glsl\n', encoding='utf-8')
            errors = check.check_glsl_baseline(root, base)
            self.assertEqual(len(errors), 1)
            self.assertIn('goneV.glsl', errors[0])

    def test_invariant_position_is_declared_by_every_vertex_stage(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'src'
            shutil.copytree(FIXTURES, src)
            module = src / 'position_mod.slang'
            module.write_text(module.read_text(encoding='utf-8') + '\n#define VK_POSITION_INVARIANT 1\n', encoding='utf-8')
            errors = check.check_position(src, self.out)
            self.assertTrue(any('invariant' in e for e in errors), errors)

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
        baseline = ROOT / 'scripts/shaders/glsl_position_baseline.txt'
        self.assertEqual(check.check_position(common.SLANG_SRC, common.GENERATED, common.LEGACY_SHADERS, baseline), [])

    def test_the_command_line_runs_the_glsl_ratchet(self):
        with tempfile.TemporaryDirectory() as tmp:
            glsl = Path(tmp) / 'glsl'
            glsl.mkdir()
            (glsl / 'rogue.glsl').write_text('void main() { gl_Position = vec4(0); }\n', encoding='utf-8')
            baseline = Path(tmp) / 'baseline.txt'
            baseline.write_text('', encoding='utf-8')
            argv = ['slang_check.py', 'position', '--glsl-root', str(glsl), '--glsl-baseline', str(baseline)]
            old = sys.argv
            sys.argv = argv
            try:
                with self.assertRaises(SystemExit) as cm:
                    check.main()
            finally:
                sys.argv = old
            self.assertEqual(cm.exception.code, 1)

    def test_the_position_module_agrees_with_the_viewers_joint_limit(self):
        limit = re.search(r'LL_MAX_JOINTS_PER_MESH_OBJECT\s*=\s*(\d+)',
                          (ROOT / 'indra/llcharacter/lljoint.h').read_text(encoding='utf-8')).group(1)
        module = (common.SLANG_SRC / 'position.slang').read_text(encoding='utf-8')
        self.assertEqual(re.search(r'VK_MAX_JOINTS\s*=\s*(\d+)', module).group(1), limit)

    def test_deferred_avatar_palette_matches_the_viewer(self):
        header = (ROOT / 'indra/llcharacter/lljoint.h').read_text(encoding='utf-8')
        joint_limit = int(re.search(r'LL_CHARACTER_MAX_JOINTS_PER_MESH\s*=\s*(\d+)', header).group(1))
        row_count = 3 * joint_limit
        manifest = check.load_manifest(common.GENERATED)
        programs = [program for program in manifest['programs'] if program['program'] == 'avatarDeferred']
        self.assertEqual(len(programs), 2)
        for program in programs:
            uniforms = program['stages']['vertex']['interface']['uniforms']
            palette = next(uniform for uniform in uniforms if uniform['name'] == 'matrixPalette')
            self.assertEqual(palette['type'], f'float32x4[{row_count}]', program['id'])
            self.assertEqual(palette['size'], 16 * row_count, program['id'])

    def test_the_module_makes_the_position_invariant_and_every_vertex_stage_says_so(self):
        module = (common.SLANG_SRC / 'position.slang').read_text(encoding='utf-8')
        default = re.search(r'(?m)^#define\s+VK_POSITION_INVARIANT\s+(\d)', module).group(1)
        want = default == '1'
        stages = list(common.GENERATED.glob('*.vert.glsl'))
        self.assertTrue(stages)
        for path in stages:
            self.assertEqual('\ninvariant gl_Position;\n' in path.read_text(encoding='utf-8'), want, path.name)

    def test_every_program_id_the_loader_asks_for_is_in_the_manifest(self):
        source = (ROOT / 'indra/newview/llviewershadermgr.cpp').read_text(encoding='utf-8')
        known = {p['id'] for p in check.load_manifest(common.GENERATED)['programs']}
        ids = set(re.findall(r'generatedStages\(\s*\w+\s*,\s*"([\w]+)"', source))
        ids |= set(re.findall(r'make_rigged_variant\(\s*[\w*]+\s*,\s*[\w*]+\s*,\s*"(\w+)"\s*\)', source))
        ids |= set(re.findall(r'"(glowextract__HAS_NOISE\d)"', source))
        for base, skin in re.findall(r'emissiveId\(\s*"(\w+)"(?:\s*,\s*"(\w+)")?\s*\)', source):
            ids |= {f'{base}__HAS_EMISSIVE{e}{skin}' for e in (0, 1)}
        self.assertGreater(len(ids), 40)   # the scan itself found the calls
        self.assertEqual(sorted(i for i in ids if i not in known), [])

    def test_gbuffer_module_packs_like_the_hand_written_shaders(self):
        module = (common.SLANG_SRC / 'gbuffer.slang').read_text(encoding='utf-8')
        loader = (ROOT / 'indra/llrender/llshadermgr.cpp').read_text(encoding='utf-8')
        glsl = (ROOT / 'indra/newview/app_settings/shaders/class1/deferred/globalF.glsl').read_text(encoding='utf-8')
        for name in ('HAS_ATMOS', 'HAS_PBR', 'SKIP_ATMOS', 'HAS_HDRI'):
            slang_value = float(re.search(rf'GBUFFER_FLAG_{name}\s*=\s*([\d.]+)', module).group(1))
            loader_value = float(re.search(rf'#define GBUFFER_FLAG_{name}\s+([\d.]+)', loader).group(1))
            self.assertEqual(slang_value, loader_value, name)
        self.assertIn('sqrt(8 * n.z + 8)', module)
        self.assertIn('sqrt(8 * n.z + 8)', glsl)

    def test_batch_texture_bindings_match_the_viewer(self):
        source = (ROOT / 'indra/newview/llviewershadermgr.cpp').read_text(encoding='utf-8')
        assignment = re.findall(r'^\s*LLGLSLShader::sIndexedTextureChannels\s*=\s*(\d+)\s*;', source, re.M)
        self.assertEqual(len(assignment), 1)
        indexed_units = [(f'tex{i}', i) for i in range(int(assignment[0]))]
        bindings = {
            'ddiffuseIndexed': indexed_units,
            'ddiffuseSingle': [('diffuseMap', 0)],
            'fullbrightEmissive': indexed_units,
            'fullbrightHud': indexed_units,
            'fullbrightMaskBlendHud': indexed_units,
            'fullbrightShinyHud': indexed_units,
            'fullbrightSurface': indexed_units,
        }
        manifest = check.load_manifest(common.GENERATED)
        seen = set()
        for program in manifest['programs']:
            name = program['program']
            if name in bindings:
                seen.add(name)
                textures = program['stages']['fragment']['interface']['textures']
                self.assertEqual([(t['name'], t['binding']) for t in textures], bindings[name], program['id'])
        self.assertEqual(seen, set(bindings))

    def test_rlva_license_notice_is_kept_in_generated_stages(self):
        for stage in ('vert', 'frag'):
            text = (common.GENERATED / f'rlvSphere.{stage}.glsl').read_text(encoding='utf-8')
            self.assertIn('SPDX-License-Identifier: LGPL-2.1-only', text)
            self.assertIn('Copyright (c) 2018-2020, Kitty Barnett', text)
            self.assertIn('Copyright (c) 2018, Kitty Barnett', text)
            self.assertIn('GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY', text)

    def test_block_name_and_manifest_keys_match_the_loader(self):
        header = (ROOT / 'indra/llrender/llgeneratedshader.h').read_text(encoding='utf-8')
        name = re.search(r'kBlockName\s*=\s*"(\w+)"', header).group(1)
        manifest = check.load_manifest(common.GENERATED)
        for prog in manifest['programs']:
            for stage, info in prog['stages'].items():
                text = (common.GENERATED / info['file']).read_text(encoding='utf-8')
                block = check.block_of(text)
                if block:
                    self.assertEqual(block[1], name, info['file'])
                iface = info['interface']   # every key LLGeneratedShader::parseManifest reads with .at()
                for key, fields in (('inputs', ('name', 'location')), ('uniforms', ('name', 'offset', 'size')), ('textures', ('name', 'binding'))):
                    for entry in iface[key]:
                        for field in fields:
                            self.assertIn(field, entry, f'{info["file"]} {key}')

    def test_every_slang_source_carries_the_licence_line(self):
        for path in common.SLANG_SRC.rglob('*.slang'):
            with self.subTest(path=str(path.relative_to(ROOT))):
                head = path.read_text(encoding='utf-8')[:4000]
                self.assertTrue('SPDX-License-Identifier' in head or 'LicenseInfo' in head)
    def test_the_licence_check_accepts_the_tree_and_catches_a_missing_header(self):
        self.assertEqual(check.check_licence(common.SLANG_SRC, common.GENERATED), [])
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'src'
            gen = Path(tmp) / 'gen'
            src.mkdir()
            gen.mkdir()
            (src / 'a.slang').write_text('// SPDX-License-Identifier: GPL-3.0-or-later\nimport glsl;\n', encoding='utf-8')
            (gen / 'a.frag.glsl').write_text('void main() {}\n', encoding='utf-8')
            self.assertEqual(len(check.check_licence(src, gen)), 1)


if __name__ == '__main__':
    unittest.main()
