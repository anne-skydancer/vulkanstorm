# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Text guards for stochastic transparency's capability gate (LLStochasticEngine::transparencySupported).

- The composite (alphaStochasticCompositeF.glsl) rounds the smoothed colour to half floats with packHalf2x16 and
  unpackHalf2x16, which are GLSL 4.20 (GL_ARB_shading_language_packing below it). LLShaderMgr::loadShaderFile compiles
  at #version 400 below GLSL 4.20, so on a GL 4.0 or 4.1 device the composite does not build. The transparency's gate
  is deviceSupported() (GL 4.0) and the shader manager's condition for #version 420.
- Both places that decide on the transparency take that gate: the target allocation (LLPipeline::allocateAlphaStochastic,
  sRenderAlphaStochasticSupported) and the shader set (stochastic_transparency_shaders in llviewershadermgr.cpp).
- The Stochastic shadows keep deviceSupported() (GL 4.0): their shaders build at #version 400.

    python3 scripts/tests/test_stochastic_transparency_gate.py
    VKSTORM_TEST_ROOT=PATH python3 scripts/tests/test_stochastic_transparency_gate.py   (another source tree)"""
import os
import re
import unittest
from pathlib import Path

ROOT = Path(os.environ.get('VKSTORM_TEST_ROOT') or Path(__file__).resolve().parents[2])


def read(relative):
    return (ROOT / relative).read_text(encoding='utf-8', errors='replace')


def body(text, signature):
    """The text of the function whose definition starts with `signature`, to its closing brace at column 0."""
    start = text.index(signature)
    end = text.index('\n}\n', start)
    return text[start:end]


def code(text):
    """`text` without its // comments."""
    return re.sub(r'//[^\n]*', '', text)


SHADERS = 'indra/newview/app_settings/shaders/class1/deferred/'
COMPOSITE = code(read(SHADERS + 'alphaStochasticCompositeF.glsl'))
HISTORY_H = code(read('indra/newview/llstochastichistory.h'))
HISTORY = code(read('indra/newview/llstochastichistory.cpp'))
PIPELINE = code(read('indra/newview/pipeline.cpp'))
VIEWER_SHADERS = code(read('indra/newview/llviewershadermgr.cpp'))
SHADER_MGR = code(read('indra/llrender/llshadermgr.cpp'))
# GLSL 4.20 built-ins: the 16-bit packing functions (GLSL 4.00 has packUnorm2x16, packUnorm4x8 and packSnorm4x8)
GLSL_420_PACKING = re.compile(r'\b(packHalf2x16|unpackHalf2x16|packSnorm2x16|unpackSnorm2x16)\s*\(')


class TransparencyGate(unittest.TestCase):
    def test_the_gate_is_the_engine_gate_and_glsl_420(self):
        self.assertRegex(HISTORY_H, r'\bbool\s+transparencySupported\(\);')
        gate = body(HISTORY, 'bool transparencySupported()\n{')
        self.assertLess(gate.index('if (!deviceSupported())'), gate.index('return ok;'))
        self.assertIn('const bool ok = gGLManager.mGLSLVersionMajor >= 4 && gGLManager.mGLSLVersionMinor >= 20;', gate)

    def test_glsl_420_is_where_the_shader_manager_compiles_at_420(self):
        # the gate mirrors loadShaderFile: from GLSL 4.20 a fragment shader is compiled at #version 420, below at 400
        version = SHADER_MGR[SHADER_MGR.index('if (major_version >= 4)'):]
        version = version[:version.index('else if (major_version == 3)')]
        self.assertRegex(version, r'else if \(minor_version >= 20\)\s*\{\s*version = '
                                  r'"#version 420\\n";\s*\}\s*else\s*\{\s*version = '
                                  r'"#version 400\\n";')

    def test_the_composite_needs_what_the_gate_admits(self):
        # packHalf2x16 is why the gate takes GLSL 4.20; if the composite stops using the 4.20 packing, the gate can go back
        self.assertRegex(COMPOSITE, GLSL_420_PACKING)
        self.assertNotIn('#extension', COMPOSITE)

    def test_the_other_transparency_shaders_need_no_glsl_420_packing(self):
        for name in ('alphaStochasticF.glsl', 'alphaStochasticExpandF.glsl', 'alphaStochasticCurrentF.glsl',
                     'alphaStochasticResolveF.glsl', 'stochasticResolveUtilF.glsl', 'stochasticSampleF.glsl'):
            self.assertIsNone(GLSL_420_PACKING.search(code(read(SHADERS + name))), name)

    def test_both_transparency_decisions_take_the_gate(self):
        allocate = body(PIPELINE, 'void LLPipeline::allocateAlphaStochastic(U32 w, U32 h)\n{')
        self.assertIn('sRenderAlphaStochasticSupported = LLStochasticEngine::transparencySupported();', allocate)
        self.assertNotIn('deviceSupported', allocate)
        shaders = body(VIEWER_SHADERS, 'static bool stochastic_transparency_shaders()\n{')
        self.assertIn('return LLStochasticEngine::transparencySupported();', shaders)
        self.assertNotIn('deviceSupported', shaders)

    def test_the_stochastic_shadows_keep_the_gl40_gate(self):
        self.assertIn('inline bool usable() { return deviceSupported(); }', HISTORY_H)
        resolve = body(VIEWER_SHADERS, 'static bool stochastic_shadow_resolve_shaders()\n{')
        self.assertIn('return use_stochastic_shadows() && LLStochasticEngine::deviceSupported();', resolve)
        begin = body(PIPELINE, 'bool LLPipeline::beginShadowStochastic()\n{')
        self.assertIn('!LLStochasticEngine::deviceSupported()', begin)
        self.assertNotIn('transparencySupported', begin)
        for name in ('shadowStochasticResolveF.glsl', 'shadowStochasticResolveUtilF.glsl', 'shadowStochasticComposeF.glsl',
                     'shadowStochasticUtil.glsl'):
            self.assertIsNone(GLSL_420_PACKING.search(code(read(SHADERS + name))), name)


if __name__ == '__main__':
    unittest.main()
