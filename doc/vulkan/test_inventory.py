"""Regression checks for lexical inventory scope, not renderer correctness."""
import pathlib
import unittest
from inventory_helpers import gl_candidates, mask_source, shader_interfaces


class InventoryTests(unittest.TestCase):
    def test_global_interfaces_and_blocks(self):
        source = '''layout (std140, binding = 0) uniform GLTFMaterials { vec4 value; };
flat in vec4 varying_color;
layout (std430) readonly buffer GLTFNodes { mat4 nodes[]; };
void helper(
    in vec4 parameter,
    out vec4 result)
{
    vec4 local;
}
uniform sampler2D tex;
'''
        rows = shader_interfaces(source)
        self.assertEqual(len(rows), 4)
        self.assertIn('flat in vec4 varying_color;', rows)
        self.assertTrue(any('GLTFMaterials' in row for row in rows))
        self.assertTrue(any('GLTFNodes' in row for row in rows))
        self.assertFalse(any('parameter' in row or 'result' in row for row in rows))

    def test_literals_comments_and_gl_names(self):
        source = '''"glTF ("; "https://example/glClear("; 'x';
R"tag(glClear( // LLGLState)tag";
// glClear( LLGLState
LLGLTFMaterial material; glReady(); LLUI::glPointToScreen();
glClear(mask); LLGLState state;'''
        calls, wrapper = gl_candidates(source, {'glClear'}, {'LLGLState'})
        self.assertEqual(calls, ['glClear'])
        self.assertTrue(wrapper)
        self.assertEqual(gl_candidates('"glClear("; LLGLTFMaterial m; glReady();', {'glClear'}, {'LLGLState'}), ([], False))
        self.assertEqual(mask_source('"https://example" // comment\n'), '"https://example"           \n')

    def test_actual_baseline_shader_cases(self):
        root = pathlib.Path('indra/newview/app_settings/shaders')
        if not root.exists():
            self.skipTest('Run from the pinned viewer repository root')
        rows = {path: shader_interfaces(path.read_text(encoding='utf-8')) for path in root.rglob('*.glsl')}
        for name in ('CASF.glsl', 'SMAA.glsl'):
            for path, declarations in rows.items():
                if path.name == name:
                    self.assertFalse(any('outColor' in row or 'pix' in row for row in declarations))
        all_rows = '\n'.join(row for declarations in rows.values() for row in declarations)
        for block in ('GLTFMaterials', 'GLTFJoints', 'GLTFNodes', 'ReflectionProbes'):
            self.assertTrue(block in all_rows, block)
        self.assertIn('flat in', all_rows)
        self.assertIn('flat out', all_rows)


if __name__ == '__main__':
    unittest.main()
