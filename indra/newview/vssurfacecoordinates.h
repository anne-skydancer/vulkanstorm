// Shared reference surface projection and texture-entry coordinates. LGPL-2.1.
#pragma once
#include "v2math.h"
#include "llvector4a.h"
#include "lltextureentry.h"
#include "m4math.h"
#include <array>
namespace VSSurfaceCoordinates
{
inline void planarProjection(LLVector2 &tc, const LLVector4a& normal,
                      const LLVector4a &center, const LLVector4a& vec)
{
    LLVector4a binormal;
    F32 d = normal[0];

    if (d >= 0.5f || d <= -0.5f)
    {
        if (d < 0)
        {
            binormal.set(0,-1,0);
        }
        else
        {
            binormal.set(0, 1, 0);
        }
    }
    else
    {
        if (normal[1] > 0)
        {
            binormal.set(-1,0,0);
        }
        else
        {
            binormal.set(1,0,0);
        }
    }
    LLVector4a tangent;
    tangent.setCross3(binormal,normal);

    tc.mV[1] = -((tangent.dot3(vec).getF32())*2 - 0.5f);
    tc.mV[0] = 1.0f+((binormal.dot3(vec).getF32())*2 - 0.5f);
}
inline void xform(LLVector2 &tex_coord, F32 cosAng, F32 sinAng, F32 offS, F32 offT, F32 magS, F32 magT)
{
    // New, good way
    F32 s = tex_coord.mV[0];
    F32 t = tex_coord.mV[1];

    // Texture transforms are done about the center of the face.
    s -= 0.5;
    t -= 0.5;

    // Handle rotation
    F32 temp = s;
    s  = s     * cosAng + t * sinAng;
    t  = -temp * sinAng + t * cosAng;

    // Then scale
    s *= magS;
    t *= magT;

    // Then offset
    s += offS + 0.5f;
    t += offT + 0.5f;

    tex_coord.mV[0] = s;
    tex_coord.mV[1] = t;
}

template<class Face>
LLVector2 surfaceToTexture(const Face& face,LLVector2 surface_coord,const LLVector4a& position,const LLVector4a& normal)
{
    LLVector2 tc = surface_coord;

    const LLTextureEntry *tep = face.textureEntry();

    if (tep == NULL)
    {
        // can't do much without the texture entry
        return surface_coord;
    }

    //VECTORIZE THIS
    // see if we have a non-default mapping
    U8 texgen = tep->getTexGen();
    if (texgen != LLTextureEntry::TEX_GEN_DEFAULT)
    {
        const LLVector4a& center = face.center();

        LLVector4a volume_position;
        LLVector3 v_position(position.getF32ptr());

        volume_position.load3(face.positionToVolume(v_position).mV);

        if (!face.volumeGlobal())
        {
            LLVector4a scale;
            scale.load3(face.scale().mV);
            volume_position.mul(scale);
        }

        LLVector4a volume_normal;
        LLVector3 v_normal(normal.getF32ptr());
        volume_normal.load3(face.directionToVolume(v_normal).mV);
        volume_normal.normalize3fast();

        if (texgen == LLTextureEntry::TEX_GEN_PLANAR)
        {
            VSSurfaceCoordinates::planarProjection(tc, volume_normal, center, volume_position);
        }
    }

    if (const auto* matrix=face.textureMatrix()) // if we have a texture matrix, use it
    {
        LLVector3 tc3(tc);
        tc3 = tc3 * *matrix;
        tc = LLVector2(tc3);
    }

    else // otherwise use the texture entry parameters
    {
        VSSurfaceCoordinates::xform(tc, cos(tep->getRotation()), sin(tep->getRotation()),
              tep->mOffsetS, tep->mOffsetT, tep->mScaleS, tep->mScaleT);
    }


    return tc;
}

// Original LLPanelPrimMediaControls source-face corner ordering and volume transform.
template<class VolumePositionToAgent>
std::array<LLVector3,8> volumeBounds(const LLVector3& minimum,const LLVector3& maximum,const VolumePositionToAgent& transform)
{
    const LLVector3 center=(minimum+maximum)*.5f;
    const LLVector3 size=(maximum-minimum)*.5f;
    std::array<LLVector3,8> vertices{
        center+size.scaledVec(LLVector3(1,1,1)),center+size.scaledVec(LLVector3(-1,1,1)),
        center+size.scaledVec(LLVector3(1,-1,1)),center+size.scaledVec(LLVector3(-1,-1,1)),
        center+size.scaledVec(LLVector3(1,1,-1)),center+size.scaledVec(LLVector3(-1,1,-1)),
        center+size.scaledVec(LLVector3(1,-1,-1)),center+size.scaledVec(LLVector3(-1,-1,-1))};
    for(auto& vertex:vertices)vertex=transform(vertex);
    return vertices;
}
}
