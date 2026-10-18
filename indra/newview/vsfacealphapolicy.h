// Original LLFace::canRenderAsMask branch and lazy getter order. LGPL-2.1.
#pragma once
template<class Face> bool vs_face_can_render_as_mask(const Face& face)
{
    if(!face.present() || face.pbr())return false;
    if(face.noAlpha())return true;
    if(face.rigged() || face.legacyBlend())return false;
    if(face.alpha()==1.f && face.glow()==0.f && !face.hud() && face.imageMask())
        return face.fullbright()?face.autoMaskNonDeferred():face.autoMaskDeferred();
    return false;
}
