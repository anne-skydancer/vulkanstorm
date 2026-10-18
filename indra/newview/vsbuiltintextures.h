// Reserved local image identities and formats shared by both renderer peers.
// Derived from LLViewerTextureList::init. SPDX-License-Identifier: LGPL-2.1-only
#pragma once
#include "indra_constants.h"
#include <array>
struct VSBuiltinTexture
{
    LLUUID id;
    const char* file;
    bool alphaOnly,clamp;
};
inline const std::array<VSBuiltinTexture,3>& vs_builtin_textures()
{
    static const std::array<VSBuiltinTexture,3> values{{
        {IMG_TRANSPARENT,"transparent.j2c",false,false},
        {IMG_ALPHA_GRAD,"alpha_gradient.tga",true,true},
        {IMG_ALPHA_GRAD_2D,"alpha_gradient_2d.j2c",true,true}}};
    return values;
}
inline const VSBuiltinTexture* vs_builtin_texture(const LLUUID& id)
{
    for(const auto& value:vs_builtin_textures())if(value.id==id)return &value;
    return nullptr;
}
