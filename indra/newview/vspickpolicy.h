// Original cursor/volume picking policies with lazy renderer-peer inputs. LGPL-2.1.
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>
namespace VSPickPolicy
{
// Drawable visibility/render-type and identity are established by the caller's
// exact accepted frame before any object or permission lookup.
template<class Object,class BuildVisible>
bool spatial(const Object& object,bool reflectionProbe,const BuildVisible& buildVisible)
{
    if(object.reflectionProbe() && !reflectionProbe)return false;
    if(object.clickIgnore() && !buildVisible())return false;
    return true;
}
template<class Object,class BuildVisible>
bool volume(const Object& object,bool rigged,bool unselectable,const BuildVisible& buildVisible)
{
    if(!object.canSelectFlag() || object.dead() || !object.rendered())return false;
    if(!unselectable && !object.canSelect())return false;
    if(object.clickIgnore() && !buildVisible())return false;
    if(object.rigged() && !(rigged || (object.hasAvatar() && object.selfAvatar() && buildVisible())))return false;
    return true;
}
template<class Face>
bool faceVisible(const Face& face,bool transparent,bool specialCursor)
{
    return specialCursor || transparent || !face.present() || face.alpha()!=0.f;
}
// Original cursorIntersect teleport/RLVa exception order, using actual tool
// identity/capture supplied by the genuine peer tool manager.
template<class Tool>
bool interaction(const Tool& tool,bool self)
{
    return tool.dragAndDrop() || tool.cameraCaptured() || (tool.pie() && self);
}
// LLVOVolume computes mask eagerly when an uploaded texture exists, even for
// transparent/opaque-material picks. Keep that ordering and the original >32
// half-resolution bit mask rather than rendered alpha-cutoff/bilinear testing.
template<class Face>
bool acceptsHit(const Face& face,bool transparent)
{
    bool ignoreAlpha=false;
    if(face.present() && face.hasLegacyMaterial())
    {
        const auto mode=face.legacyMode();
        if(mode==face.emissiveMode() || mode==face.noneMode() ||
           (mode==face.maskMode() && face.cutoff()==0))ignoreAlpha=true;
    }
    const bool noTexture=!face.hasTexture() || !face.uploaded();
    const bool mask=noTexture?false:face.mask();
    return ignoreAlpha || transparent || noTexture || mask;
}
// Directly evaluate the original createPickMask/updatePickMask/getMask result.
// Pixels are the exact immutable native top-row-first RGBA publication. No
// per-pick mask allocation or cache is needed; odd sizes preserve old indexing.
inline bool textureMask(unsigned width,unsigned height,const std::vector<std::uint8_t>& rgba,float u,float v)
{
    if(!width || !height || rgba.size()!=std::size_t(width)*height*4)return true;
    if(std::isfinite(u) && std::isfinite(v)){u-=std::floor(u);v-=std::floor(v);}else u=v=0;
    if(u<0 || u>1 || v<0 || v>1)u=v=0;
    const unsigned maskWidth=width/2,maskHeight=height/2;
    const std::size_t index=std::size_t(std::floor(v*maskHeight))*maskWidth+unsigned(std::floor(u*maskWidth));
    const unsigned samples=(width+1)/2;
    const std::size_t row=index/samples,column=index%samples;
    if(row>=(height+1)/2)return false; // untouched padding bit in the original mask
    const std::size_t pixel=(std::size_t(height-1-unsigned(row)*2)*width+column*2)*4;
    return rgba[pixel+3]>32;
}
}
