# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Text guards for the render-path hotfixes of 2026-11-18 (phase-2b research, reports/gl-modernisation-2026-11-17).

- H1 (b-5 F3): a texture-animation matrix is shared between its face and the draw batches that point at it
  (upstream ae465f3fb6 and 9e965d5288), so removing the animation or destroying the face cannot free it under a batch
  that a mirror's hero-probe faces or a freeze frame still draw.
- H2 (b-9 F8, audit A1): a skin whose joint bindings were dropped has empty joint and inverse-bind lists. The matrix
  palette (LLVOAvatar::updateSkinInfoMatrixPalette, LLSkinningUtil::initSkinningMatrixPalette) takes the lists' data
  with data(), which is valid when empty, instead of &v[0]; LLMeshSkinInfo::updateHash takes no element of an empty
  inverse-bind list and converts no out-of-range float to an integer (upstream 8a55a86b81). Outputs and hashes of
  well-formed skins are unchanged.
- H3 (b-6 F9): when a vertex buffer cannot be allocated, the 1-vertex, 3-index dummy of an avatar mesh face or a tree
  is zeroed through LLVertexBuffer::zeroFill, which marks both whole buffers, so the zeros reach GL (a pooled buffer
  kept its previous owner's bytes there); if the dummy cannot be allocated either, the face gets no buffer and a
  warning once, instead of a write through a null copy. LLViewerJointMesh::drawShape draws nothing outside its face's
  buffer (the joint meshes keep the full mesh's ranges), after the first pass's joint matrix upload.

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
VOAVATAR = code(read('indra/newview/llvoavatar.cpp'))
SKINNING = code(read('indra/newview/llskinningutil.cpp'))
VERTEXBUFFER_H = code(read('indra/llrender/llvertexbuffer.h'))
VERTEXBUFFER = code(read('indra/llrender/llvertexbuffer.cpp'))
VOTREE = code(read('indra/newview/llvotree.cpp'))
JOINTMESH = code(read('indra/newview/llviewerjointmesh.cpp'))


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


class SkinPalettes(unittest.TestCase):
    def test_the_avatar_palette_takes_data_pointers(self):
        palette = body(VOAVATAR, 'const LLVOAvatar::MatrixPaletteCache& LLVOAvatar::updateSkinInfoMatrixPalette(')
        self.assertIn('LLSkinningUtil::initSkinningMatrixPalette(entry.mMatrixPalette.data(), count, skin, this);', palette)
        self.assertIn('const LLMatrix4a* mat = entry.mMatrixPalette.data();', palette)
        self.assertIn('F32* mp = entry.mGLMp.data();', palette)
        self.assertNotRegex(palette, r'&\s*\(?\s*entry\.m(?:MatrixPalette|GLMp)\s*\[')

    def test_the_inverse_binds_are_taken_with_data(self):
        init = body(SKINNING, 'void LLSkinningUtil::initSkinningMatrixPalette(')
        self.assertIn('const LLMatrix4a* invBind = skin->mInvBindMatrix.data();', init)
        self.assertNotIn('mInvBindMatrix[', init)


class AllocationFallbacks(unittest.TestCase):
    def test_zero_fill_marks_both_whole_buffers(self):
        self.assertRegex(VERTEXBUFFER_H, r'void\s+zeroFill\(\);')
        fill = body(VERTEXBUFFER, 'void LLVertexBuffer::zeroFill()')
        self.assertLess(fill.index('_mapBuffer();'), fill.index('memset('))
        self.assertIn('memset(mMappedData, 0, mSize);', fill)
        self.assertIn('mMappedVertexRegions.assign(1, MappedRegion{ 0, mSize - 1 });', fill)
        self.assertIn('memset(mMappedIndexData, 0, mIndicesSize);', fill)
        self.assertIn('mMappedIndexRegions.assign(1, MappedRegion{ 0, mIndicesSize - 1 });', fill)

    def test_no_fallback_zeroes_the_cpu_copy_alone(self):
        for name, text in (('llvoavatar.cpp', VOAVATAR), ('llvotree.cpp', VOTREE)):
            self.assertNotIn('getMappedData()', text, name)
            self.assertNotIn('getMappedIndices()', text, name)

    def test_the_avatar_dummy_is_checked_and_uploaded(self):
        update = body(VOAVATAR, 'void LLVOAvatar::updateMeshData()')
        self.assertIn('LLPointer<LLVertexBuffer> buff = facep->getVertexBuffer();', update)
        self.assertIn('attach = buff->allocateBuffer(1, 3);', update)
        self.assertIn('if (buff->allocateBuffer(1, 3))', update)
        self.assertEqual(update.count('buff->zeroFill();'), 2)
        self.assertIn('facep->setVertexBuffer(attach ? buff.get() : nullptr);', update)
        self.assertEqual(update.count('LL_WARNS_ONCE("Avatar")'), 3)
        self.assertEqual(len(re.findall(r'\bLL_WARNS\(', update)), 1)  # the "Substituting" branch, which no face reaches

    def test_the_tree_dummy_is_checked_and_uploaded(self):
        mesh = body(VOTREE, 'void LLVOTree::updateMesh()')
        self.assertIn('LLPointer<LLVertexBuffer> buff = new LLVertexBuffer(LLDrawPoolTree::VERTEX_DATA_MASK);', mesh)
        start = mesh.index('if (!buff->allocateBuffer(vert_count, index_count))')
        fallback = mesh[start:mesh.index('return;', start)]
        self.assertRegex(fallback, r'if \(buff->allocateBuffer\(1, 3\)\)\s*\{\s*buff->zeroFill\(\);\s*'
                                   r'facep->setVertexBuffer\(buff\);\s*\}\s*else\s*\{[^}]*facep->setVertexBuffer\(nullptr\);')

    def test_joint_meshes_draw_only_inside_their_buffer(self):
        draw = body(JOINTMESH, 'U32 LLViewerJointMesh::drawShape(')
        guard = draw.index('if (end >= buff->getNumVerts() || offset + count > buff->getNumIndices())')
        self.assertLess(guard, draw.index('buff->setBuffer();'))
        block = draw[guard:draw.index('return 0;', guard)]
        self.assertIn('if (first_pass && mMesh->hasWeights())', block)
        self.assertIn('uploadJointMatrices();', block)


if __name__ == '__main__':
    unittest.main()
