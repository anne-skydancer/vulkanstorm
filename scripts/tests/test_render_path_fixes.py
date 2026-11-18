# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Text guards for the render-path hotfixes of 2026-11-18 (phase-2b research, reports/gl-modernisation-2026-11-17).

- H1 (b-5 F3): a texture-animation matrix is shared between its face and the draw batches that point at it
  (upstream ae465f3fb6 and 9e965d5288), so removing the animation or destroying the face cannot free it under a batch
  that a mirror's hero-probe faces or a freeze frame still draw.
- H2 (audit A1): LLMeshSkinInfo::updateHash takes no element of an empty inverse-bind list and converts no
  out-of-range float to an integer (upstream 8a55a86b81); the hashes of well-formed skins are unchanged.

    python3 scripts/tests/test_render_path_fixes.py
    VKSTORM_TEST_ROOT=PATH python3 scripts/tests/test_render_path_fixes.py   (another source tree)"""
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
    """`text` without its // comments (upstream keeps replaced lines as comments)."""
    return re.sub(r'//[^\n]*', '', text)


FACE_H = code(read('indra/newview/llface.h'))
FACE = code(read('indra/newview/llface.cpp'))
SPATIAL_H = code(read('indra/newview/llspatialpartition.h'))
VOVOLUME = code(read('indra/newview/llvovolume.cpp'))
MODEL = code(read('indra/llprimitive/llmodel.cpp'))


class TextureMatrixOwnership(unittest.TestCase):
    def test_face_and_batch_share_the_matrix(self):
        self.assertRegex(FACE_H, r'std::shared_ptr<LLMatrix4>\s+mTextureMatrix;')
        self.assertNotRegex(FACE_H, r'LLMatrix4\s*\*\s*mTextureMatrix\s*;')
        self.assertRegex(SPATIAL_H, r'std::shared_ptr<const LLMatrix4>\s+mTextureMatrix;')
        self.assertNotRegex(SPATIAL_H, r'const LLMatrix4\s*\*\s*mTextureMatrix\b')

    def test_no_code_deletes_a_texture_matrix(self):
        for name, text in (('llface.cpp', FACE), ('llvovolume.cpp', VOVOLUME)):
            self.assertNotRegex(text, r'\bdelete\s+(?:\w+->)?mTextureMatrix\b', name)
        self.assertEqual(len(re.findall(r'facep->mTextureMatrix\.reset\(\);', VOVOLUME)), 2)
        self.assertIn('mTextureMatrix.reset();', body(FACE, 'void LLFace::destroy()'))

    def test_animation_makes_a_shared_matrix(self):
        animate = body(VOVOLUME, 'void LLVOVolume::animateTextures()')
        self.assertIn('facep->mTextureMatrix = std::make_shared<LLMatrix4>();', animate)

    def test_a_new_batch_takes_ownership(self):
        register = body(VOVOLUME, 'void LLVolumeGeometryManager::registerFace(')
        self.assertRegex(register, r'std::shared_ptr<const LLMatrix4>\s+tex_mat;')
        self.assertIn('tex_mat = facep->mTextureMatrix;', register)
        self.assertIn('draw_info->mTextureMatrix = std::move(tex_mat);', register)
        # the move is the local's last use
        self.assertEqual(register[register.index('std::move(tex_mat)'):].count('tex_mat'), 1)


class SkinHash(unittest.TestCase):
    def setUp(self):
        self.update = body(MODEL, 'void LLMeshSkinInfo::updateHash()')

    def test_an_empty_inverse_bind_list_is_not_indexed(self):
        self.assertIn('F32* src = mInvBindMatrix.empty() ? nullptr : mInvBindMatrix[0].getF32ptr();', self.update)

    def test_out_of_range_values_hash_as_int_min(self):
        self.assertIn('F64 scaled = src[i] * 10000.f + 0.5f;', self.update)
        self.assertIn('S32 t = (scaled >= S32_MIN && scaled <= S32_MAX) ? (S32)floor(scaled) : S32_MIN;', self.update)
        self.assertNotIn('ll_round(', self.update)


if __name__ == '__main__':
    unittest.main()
