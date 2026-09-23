// SPDX-License-Identifier: LGPL-2.1-or-later
#include "linden_common.h"
#include "lltut.h"
#include "lltextureentry.h"
#include "llgltfmaterial.h"

// The viewer supplies TinyGLTF in llgltfloader.cpp. This focused executable
// links the real material implementation without linking the viewer.
#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_USE_CPP14
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define TINYGLTF_NO_EXTERNAL_IMAGE 1
#include "tinygltf/tiny_gltf.h"

namespace tut
{
struct textureentry_material_data {};
typedef test_group<textureentry_material_data> material_group;
typedef material_group::object material_object;
material_group material_tests("lltextureentry_material");

template<> template<> void material_object::test<1>()
{
    LLTextureEntry entry;
    ensure("legacy face is ready", entry.isGLTFRenderMaterialReady());
    ensure("legacy has no PBR material", entry.getGLTFRenderMaterial() == nullptr);
    LLPointer<LLGLTFMaterial> base = new LLGLTFMaterial;
    entry.setGLTFMaterial(base);
    ensure("base alone is ready", entry.isGLTFRenderMaterialReady());
    ensure("base used directly", entry.getGLTFRenderMaterial() == base.get());
}

template<> template<> void material_object::test<2>()
{
    LLTextureEntry entry;
    LLPointer<LLGLTFMaterial> base = new LLGLTFMaterial;
    LLPointer<LLGLTFMaterial> overrides = new LLGLTFMaterial;
    overrides->mMetallicFactor = 0.3f;
    entry.setGLTFMaterial(base);
    entry.setGLTFMaterialOverride(overrides);
    ensure("pending override defers geometry", !entry.isGLTFRenderMaterialReady());
    LLPointer<LLGLTFMaterial> resolved = new LLGLTFMaterial(*base);
    resolved->applyOverride(*overrides);
    entry.setGLTFRenderMaterial(resolved);
    ensure("resolved override resumes geometry", entry.isGLTFRenderMaterialReady());
    ensure("resolved material retained", entry.getGLTFRenderMaterial() == resolved.get());
    entry.setGLTFRenderMaterial(nullptr);
    ensure("later update defers again", !entry.isGLTFRenderMaterialReady());
    entry.setGLTFRenderMaterial(resolved);
    ensure("later completion resumes", entry.isGLTFRenderMaterialReady());
}

template<> template<> void material_object::test<3>()
{
    LLTextureEntry entry;
    LLPointer<LLGLTFMaterial> base = new LLGLTFMaterial;
    LLPointer<LLGLTFMaterial> overrides = new LLGLTFMaterial;
    overrides->mMetallicFactor = 0.3f;
    entry.setGLTFMaterial(base);
    entry.setGLTFMaterialOverride(overrides);
    entry.setBaseMaterial();
    ensure("cleared overrides permit base material", entry.isGLTFRenderMaterialReady());
    ensure("base fallback restored", entry.getGLTFRenderMaterial() == base.get());
}
}
