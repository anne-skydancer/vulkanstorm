# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
"""Text guards for the render-path hotfixes of 2026-11-18 (the phase-2b research of the OpenGL modernisation; each
fix's commit message gives its finding, callers and evidence).

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
  buffer (the joint meshes keep the full mesh's ranges) and uploads no joint matrices for it (every joint mesh uploads
  its own). The vertex-buffer pool releases a buffer whose CPU copy could not be allocated instead of pooling it, so
  the next allocation of its size class (the avatar's retry among them) does not fail with it once memory is back.
- Impostors (owner decision Q1; b-28 F1/F2 = H28, X2 F3 = FG3c): generateImpostor sets sShadowRender for its other
  rules, but an impostor is a picture: LLFetchedGLTFMaterial::bind gives each item its full material (normal,
  roughness/metal, emissive, factors, transforms, its own cut-off; a blended item keeps its pool's impostor cut-off),
  real shadow passes are unchanged; the impostor's blended groups are sorted back to front for the impostor's own
  camera (with calcDistance's key, LLSpatialPartition::calcAlphaDepth) and its rigged ones in attachment order; and
  the "depth-only" second render keeps the caller's colour mask in renderGeomPostDeferred, the glow pools and
  LLDrawPoolAlpha::forwardRender, so blended faces are blended once.
- Grass (owner decision Q8; X1 F11): a blade's second base vertex is the mirror of the first about the blade's root,
  (x - xf, y - yf), in LLVOGrass::getGeometry and in its picking twin lineSegmentIntersect; the y offset was -xf.
- Grass draw infos (b-5 F2, audit N3): LLGrassPartition::getGeometry passes the face's fullbright flag as LLDrawInfo's
  fullbright, as the batch merge test compares it, instead of the object's selection as fullbright and fullbright as
  bump.

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
JOINT = code(read('indra/newview/llviewerjoint.cpp'))
GLTF_MATERIAL = code(read('indra/newview/llfetchedgltfmaterial.cpp'))
PIPELINE = code(read('indra/newview/pipeline.cpp'))
SPATIAL = code(read('indra/newview/llspatialpartition.cpp'))
POOL_ALPHA = code(read('indra/newview/lldrawpoolalpha.cpp'))
POOL_SIMPLE = code(read('indra/newview/lldrawpoolsimple.cpp'))
POOL_PBR = code(read('indra/newview/lldrawpoolpbropaque.cpp'))
GRASS = code(read('indra/newview/llvograss.cpp'))


def guarded_colour_masks(text):
    """Each gGL.setColorMask( call in `text` with the 3 lines before it."""
    lines = text.split('\n')
    return [(i, '\n'.join(lines[max(0, i - 3):i + 1])) for i, line in enumerate(lines) if 'gGL.setColorMask(' in line]


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
        self.assertLess(guard, draw.index('uploadJointMatrices();'))
        self.assertLess(guard, draw.index('buff->setBuffer();'))
        # nothing is drawn, so nothing is uploaded: every joint mesh uploads its own matrices on its first pass
        self.assertRegex(draw[guard:], r'^if \(end >= buff->getNumVerts\(\) \|\| offset \+ count > '
                                       r'buff->getNumIndices\(\)\)\s*\{\s*return 0;\s*\}')
        self.assertEqual(draw.count('uploadJointMatrices();'), 1)
        # LLViewerJoint::render draws each child mesh with first_pass set
        self.assertIn('joint->render( pixelArea, true, is_dummy );', JOINT)

    def test_the_pool_does_not_keep_a_buffer_without_a_cpu_copy(self):
        pool = VERTEXBUFFER[VERTEXBUFFER.index('class LLDefaultVBOPool'):]
        start = pool.index('void free(GLenum type, U32 size, GLuint name, U8* data) override')
        free = pool[start:pool.index('\n    }\n', start)]
        self.assertNotIn('llassert(data != nullptr);', free)
        release = free.index('if (!data)')
        self.assertLess(free.index('mAllocated -= size;'), release)
        self.assertLess(release, free.index('mReserved += size;'))
        self.assertRegex(free[release:], r'^if \(!data\)\s*\{\s*if \(name\)\s*\{\s*delete_buffers\(1, &name\);\s*\}\s*'
                                         r'return;\s*\}')


class Impostors(unittest.TestCase):
    def test_impostor_items_bind_their_full_material(self):
        bind = body(GLTF_MATERIAL, 'void LLFetchedGLTFMaterial::bind(')
        self.assertIn('const bool shadow_pass = LLPipeline::sShadowRender && !LLPipeline::sImpostorRender;', bind)
        self.assertIn('const bool pool_cutoff = LLPipeline::sImpostorRender && '
                      'mAlphaMode == LLGLTFMaterial::ALPHA_MODE_BLEND;', bind)
        self.assertRegex(bind, r'if \(\(!shadow_pass \|\| \(mAlphaMode == LLGLTFMaterial::ALPHA_MODE_MASK\)\) '
                               r'&& !pool_cutoff\)\s*\{\s*if \(mAlphaMode == LLGLTFMaterial::ALPHA_MODE_MASK\)')
        full = bind[bind.index('if (!shadow_pass)'):]
        for name in ('BUMP_MAP', 'SPECULAR_MAP', 'EMISSIVE_MAP', 'ROUGHNESS_FACTOR', 'METALLIC_FACTOR', 'EMISSIVE_COLOR',
                     'TEXTURE_NORMAL_TRANSFORM', 'TEXTURE_METALLIC_ROUGHNESS_TRANSFORM', 'TEXTURE_EMISSIVE_TRANSFORM'):
            self.assertIn('LLShaderMgr::' + name, full)
        # the shadow flag is read once, into shadow_pass; devel's GL statistics block (VKS_GL_STATS, diagnostics only)
        # reads it for its own counters and binds nothing
        binding = re.sub(r'#if VKS_GL_STATS\b.*?#endif', '', bind, flags=re.S)
        self.assertEqual(binding.count('LLPipeline::sShadowRender'), 1)

    def test_the_depth_key_is_one_formula(self):
        depth = body(SPATIAL, 'F32 LLSpatialPartition::calcAlphaDepth(')
        self.assertIn('t.mul(0.25f);', depth)
        self.assertIn('return v.dot3(ata).getF32();', depth)
        distance = body(SPATIAL, 'F32 LLSpatialPartition::calcDistance(')
        self.assertIn('group->mDepth = calcAlphaDepth(group, camera);', distance)
        self.assertNotIn('dot3(', distance)

    def test_impostors_sort_their_blended_groups_for_their_own_camera(self):
        generate = body(PIPELINE, 'void LLPipeline::generateImpostor(')
        sort = generate.index('LLSpatialPartition::calcAlphaDepth(group, bridge ? bridge->transformCamera(camera) : camera)')
        self.assertLess(generate.index('camera.lookAt(viewer_camera->getOrigin(), pos, viewer_camera->getUpAxis());'), sort)
        self.assertLess(sort, generate.index('renderGeomDeferred(camera);'))
        block = generate[sort:generate.index('renderGeomDeferred(camera);')]
        self.assertIn('return lhs.first > rhs.first;', block)
        self.assertIn('*out++ = entry.second;', block)
        self.assertIn('std::sort(result.beginRiggedAlphaGroups(), result.endRiggedAlphaGroups(), '
                      'LLSpatialGroup::CompareRenderOrder());', block)
        # real shadow passes keep their blended groups in cull order, as before
        post_sort = body(PIPELINE, 'void LLPipeline::postSort(LLCamera &camera)')
        self.assertIsNotNone(re.search(r'if \(!sShadowRender\)\s*\{\s*'
                                       r'std::sort\(sCull->beginAlphaGroups\(\), sCull->endAlphaGroups\(\), '
                                       r'LLSpatialGroup::CompareDepthGreater\(\)\);', post_sort))

    def test_the_second_render_writes_depth_only(self):
        post = body(PIPELINE, 'void LLPipeline::renderGeomPostDeferred(LLCamera& camera)')
        masks = guarded_colour_masks(post)
        self.assertEqual(len(masks), 1)
        self.assertIn('if (!sImpostorRenderAlphaDepthPass)', masks[0][1])
        functions = ((POOL_SIMPLE, 'void LLDrawPoolGlow::renderPostDeferred(S32 pass)', 2),
                     (POOL_PBR, 'void LLDrawPoolGLTFPBR::renderPostDeferred(S32 pass)', 2),
                     (POOL_ALPHA, 'void LLDrawPoolAlpha::forwardRender(', 2))
        for text, signature, count in functions:
            function = body(text, signature)
            self.assertIn('const bool own_mask = !LLPipeline::sImpostorRenderAlphaDepthPass;', function, signature)
            masks = guarded_colour_masks(function)
            self.assertEqual(len(masks), count, signature)
            for _, context in masks:
                self.assertIn('if (own_mask)', context, signature)
        # generateImpostor's second render still masks colour itself and writes the blended faces' depth
        generate = body(PIPELINE, 'void LLPipeline::generateImpostor(')
        second = generate[generate.index('sImpostorRenderAlphaDepthPass = true;'):generate.index('sImpostorRenderAlphaDepthPass = false;')]
        self.assertIn('gGL.setColorMask(false,false);', second)
        self.assertIn('|| LLPipeline::sImpostorRenderAlphaDepthPass', body(POOL_ALPHA, 'void LLDrawPoolAlpha::forwardRender('))


class GrassBlades(unittest.TestCase):
    def test_the_second_base_vertex_mirrors_the_first(self):
        for signature in ('void LLVOGrass::getGeometry(', 'bool LLVOGrass::lineSegmentIntersect('):
            blade = body(GRASS, signature)
            self.assertRegex(blade, r'position\.mV\[0\]\s*=\s*mPosition\.mV\[VX\] \+ x \+ xf;\s*'
                                    r'position\.mV\[1\]\s*=\s*mPosition\.mV\[VY\] \+ y \+ yf;', signature)
            self.assertRegex(blade, r'position\.mV\[0\]\s*=\s*mPosition\.mV\[VX\] \+ x - xf;\s*'
                                    r'position\.mV\[1\]\s*=\s*mPosition\.mV\[VY\] \+ y - yf;', signature)
            self.assertNotIn('y - xf', blade, signature)


class GrassDrawInfo(unittest.TestCase):
    def test_the_batch_takes_the_fullbright_flag_it_is_merged_by(self):
        geometry = body(GRASS, 'void LLGrassPartition::getGeometry(LLSpatialGroup* group)')
        self.assertIn('draw_vec[idx]->mFullbright == fullbright)', geometry)
        self.assertRegex(geometry, r'new LLDrawInfo\(start,end,count,offset,facep->getTexture\(\),\s*buffer, fullbright\);')
        self.assertNotIn('isSelected()', geometry)


if __name__ == '__main__':
    unittest.main()
