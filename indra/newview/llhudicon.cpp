/**
 * @file llhudicon.cpp
 * @brief LLHUDIcon class implementation
 *
 * $LicenseInfo:firstyear=2006&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"

#include "llhudicon.h"
#include "llagent.h"
#include <utility>

#include "llgl.h"
#include "llrender.h"

#include "llviewerobject.h"
#include "lldrawable.h"
#include "llvector4a.h"
#include "llviewercamera.h"
#include "llviewertexture.h"
#include "llviewerwindow.h"

//-----------------------------------------------------------------------------
// Local consts
//-----------------------------------------------------------------------------
const F32 ANIM_TIME = 0.4f;
const F32 DIST_START_FADE = 15.f;
const F32 DIST_END_FADE = 30.f;
const F32 MAX_VISIBLE_TIME = 15.f;
const F32 FADE_OUT_TIME = 1.f;

//-----------------------------------------------------------------------------
// Utility functions
//-----------------------------------------------------------------------------
static F32 calc_bouncy_animation(F32 x)
{
    return -(cosf(x * F_PI * 2.5f - F_PI_BY_TWO))*(0.4f + x * -0.1f) + x * 1.3f;
}


//-----------------------------------------------------------------------------
// static declarations
//-----------------------------------------------------------------------------
LLHUDIcon::icon_instance_t LLHUDIcon::sIconInstances;

// <FS:Ansariel> Mark script error icons
LLHUDIcon::icon_instance_t LLHUDIcon::sScriptErrorIconInstances;
// </FS:Ansariel> Mark script error icons

LLHUDIcon::LLHUDIcon(const U8 type) :
            LLHUDObject(type),
            mImagep(NULL),
            mDistance(0.f),
            mScale(0.1f),
            mHidden(false),
            mScriptError(false) // <FS:Ansariel> Mark script error icons
{
    sIconInstances.push_back(this);
}

LLHUDIcon::~LLHUDIcon()
{
    mImagep = NULL;
}

void LLHUDIcon::render()
{
    LLGLSUIDefault texture_state;
    LLGLDepthTest gls_depth(GL_TRUE);
    //LLGLDisable gls_stencil(GL_STENCIL_TEST);

    if (mHidden)
        return;

    if (mSourceObject.isNull() || mImagep.isNull())
    {
        markDead();
        return;
    }

    LLVector3 obj_position = mSourceObject->getRenderPosition();

    // put icon above object, and in front
    // RN: don't use drawable radius, it's fricking HUGE
    LLViewerCamera* camera = LLViewerCamera::getInstance();
    LLVector3 icon_relative_pos = (camera->getUpAxis() * ~mSourceObject->getRenderRotation());
    icon_relative_pos.abs();

    F32 distance_scale = llmin(mSourceObject->getScale().mV[VX] / icon_relative_pos.mV[VX],
        mSourceObject->getScale().mV[VY] / icon_relative_pos.mV[VY],
        mSourceObject->getScale().mV[VZ] / icon_relative_pos.mV[VZ]);
    F32 up_distance = 0.5f * distance_scale;
    LLVector3 icon_position = obj_position + (up_distance * camera->getUpAxis()) * 1.2f;

    LLVector3 icon_to_cam = LLViewerCamera::getInstance()->getOrigin() - icon_position;
    icon_to_cam.normVec();

    icon_position += icon_to_cam * mSourceObject->mDrawable->getRadius() * 1.1f;

    mDistance = dist_vec(icon_position, camera->getOrigin());

    F32 alpha_factor = clamp_rescale(mDistance, DIST_START_FADE, DIST_END_FADE, 1.f, 0.f);

    LLVector3 x_pixel_vec;
    LLVector3 y_pixel_vec;

    camera->getPixelVectors(icon_position, y_pixel_vec, x_pixel_vec);

    F32 scale_factor = 1.f;
    if (mAnimTimer.getElapsedTimeF32() < ANIM_TIME)
    {
        scale_factor = llmax(0.f, calc_bouncy_animation(mAnimTimer.getElapsedTimeF32() / ANIM_TIME));
    }

    F32 time_elapsed = mLifeTimer.getElapsedTimeF32();
    if (time_elapsed > MAX_VISIBLE_TIME)
    {
        markDead();
        return;
    }

    if (time_elapsed > MAX_VISIBLE_TIME - FADE_OUT_TIME)
    {
        alpha_factor *= clamp_rescale(time_elapsed, MAX_VISIBLE_TIME - FADE_OUT_TIME, MAX_VISIBLE_TIME, 1.f, 0.f);
    }

    F32 image_aspect = (F32)mImagep->getFullWidth() / (F32)mImagep->getFullHeight() ;
    LLVector3 x_scale = image_aspect * (F32)gViewerWindow->getWindowHeightScaled() * mScale * scale_factor * x_pixel_vec;
    LLVector3 y_scale = (F32)gViewerWindow->getWindowHeightScaled() * mScale * scale_factor * y_pixel_vec;

    LLVector3 lower_left = icon_position - (x_scale * 0.5f);
    LLVector3 lower_right = icon_position + (x_scale * 0.5f);
    LLVector3 upper_left = icon_position - (x_scale * 0.5f) + y_scale;
    LLVector3 upper_right = icon_position + (x_scale * 0.5f) + y_scale;

    {
        LLColor4 icon_color = LLColor4::white;
        icon_color.mV[VALPHA] = alpha_factor;
        gGL.color4fv(icon_color.mV);
        gGL.getTexUnit(0)->bind(mImagep);
    }

    gGL.begin(LLRender::TRIANGLES);
    {
        gGL.texCoord2f(0.f, 1.f);
        gGL.vertex3fv(upper_left.mV);
        gGL.texCoord2f(0.f, 0.f);
        gGL.vertex3fv(lower_left.mV);
        gGL.texCoord2f(1.f, 0.f);
        gGL.vertex3fv(lower_right.mV);

        gGL.texCoord2f(0.f, 1.f);
        gGL.vertex3fv(upper_left.mV);
        gGL.texCoord2f(1.f, 0.f);
        gGL.vertex3fv(lower_right.mV);
        gGL.texCoord2f(1.f, 1.f);
        gGL.vertex3fv(upper_right.mV);
    }
    gGL.end();
}

void LLHUDIcon::setImage(LLViewerTexture* imagep)
{
    mImagep = imagep;
    mImagep->setAddressMode(LLTexUnit::TAM_CLAMP);
}

void LLHUDIcon::setScale(F32 fraction_of_fov)
{
    mScale = fraction_of_fov;
}

void LLHUDIcon::markDead()
{
    LLPointer<LLHUDIcon> keepAlive(this);auto retire=std::exchange(mNativeRetire,{});
    mNativeSource={};mNativeImageQuery={};mNativeImage.reset();mNativeMesh.reset();mNativeQuadKnown=false;
    if(retire)retire();
    if (mSourceObject)
    {
        mSourceObject->clearIcon();
    }
    LLHUDObject::markDead();
}

bool LLHUDIcon::lineSegmentIntersect(const LLVector4a& start, const LLVector4a& end, LLVector4a* intersection)
{
    if (mHidden)
        return false;

    if (mSourceObject.isNull() || mImagep.isNull())
    {
        markDead();
        return false;
    }

    LLVector3 obj_position = mSourceObject->getRenderPosition();

    // put icon above object, and in front
    // RN: don't use drawable radius, it's fricking HUGE
    LLViewerCamera* camera = LLViewerCamera::getInstance();
    LLVector3 icon_relative_pos = (camera->getUpAxis() * ~mSourceObject->getRenderRotation());
    icon_relative_pos.abs();

    F32 distance_scale = llmin(mSourceObject->getScale().mV[VX] / icon_relative_pos.mV[VX],
        mSourceObject->getScale().mV[VY] / icon_relative_pos.mV[VY],
        mSourceObject->getScale().mV[VZ] / icon_relative_pos.mV[VZ]);
    F32 up_distance = 0.5f * distance_scale;
    LLVector3 icon_position = obj_position + (up_distance * camera->getUpAxis()) * 1.2f;

    LLVector3 icon_to_cam = LLViewerCamera::getInstance()->getOrigin() - icon_position;
    icon_to_cam.normVec();

    icon_position += icon_to_cam * mSourceObject->mDrawable->getRadius() * 1.1f;

    mDistance = dist_vec(icon_position, camera->getOrigin());

    LLVector3 x_pixel_vec;
    LLVector3 y_pixel_vec;

    camera->getPixelVectors(icon_position, y_pixel_vec, x_pixel_vec);

    F32 scale_factor = 1.f;
    if (mAnimTimer.getElapsedTimeF32() < ANIM_TIME)
    {
        scale_factor = llmax(0.f, calc_bouncy_animation(mAnimTimer.getElapsedTimeF32() / ANIM_TIME));
    }

    F32 time_elapsed = mLifeTimer.getElapsedTimeF32();
    if (time_elapsed > MAX_VISIBLE_TIME)
    {
        markDead();
        return false;
    }

    F32 image_aspect = (F32)mImagep->getFullWidth() / (F32)mImagep->getFullHeight() ;
    LLVector3 x_scale = image_aspect * (F32)gViewerWindow->getWindowHeightScaled() * mScale * scale_factor * x_pixel_vec;
    LLVector3 y_scale = (F32)gViewerWindow->getWindowHeightScaled() * mScale * scale_factor * y_pixel_vec;

    LLVector4a x_scalea;
    LLVector4a icon_positiona;
    LLVector4a y_scalea;

    x_scalea.load3(x_scale.mV);
    x_scalea.mul(0.5f);
    y_scalea.load3(y_scale.mV);

    icon_positiona.load3(icon_position.mV);

    LLVector4a lower_left;
    lower_left.setSub(icon_positiona, x_scalea);
    LLVector4a lower_right;
    lower_right.setAdd(icon_positiona, x_scalea);
    LLVector4a upper_left;
    upper_left.setAdd(lower_left, y_scalea);
    LLVector4a upper_right;
    upper_right.setAdd(lower_right, y_scalea);

    LLVector4a dir;
    dir.setSub(end, start);

    F32 a,b,t;

    if (LLTriangleRayIntersect(upper_right, upper_left, lower_right, start, dir, a,b,t) ||
        LLTriangleRayIntersect(upper_left, lower_left, lower_right, start, dir, a,b,t))
    {
        if (intersection)
        {
            dir.mul(t);
            intersection->setAdd(start, dir);
        }
        return true;
    }

    return false;
}

//static
LLHUDIcon* LLHUDIcon::lineSegmentIntersectAll(const LLVector4a& start, const LLVector4a& end, LLVector4a* intersection)
{
    icon_instance_t::iterator icon_it;

    LLVector4a local_end = end;
    LLVector4a position;

    LLHUDIcon* ret = NULL;
    for(icon_it = sIconInstances.begin(); icon_it != sIconInstances.end(); ++icon_it)
    {
        LLHUDIcon* icon = *icon_it;
        if (icon->lineSegmentIntersect(start, local_end, &position))
        {
            ret = icon;
            if (intersection)
            {
                *intersection = position;
            }
            local_end = position;
        }
    }

    return ret;
}


 //static
void LLHUDIcon::updateAll()
{
    cleanupDeadIcons();
}

//static
bool LLHUDIcon::iconsNearby()
{
    return !sIconInstances.empty();
}

//static
void LLHUDIcon::cleanupDeadIcons()
{
    icon_instance_t::iterator icon_it;

    icon_instance_t icons_to_erase;
    for(icon_it = sIconInstances.begin(); icon_it != sIconInstances.end(); ++icon_it)
    {
        if ((*icon_it)->mDead)
        {
            icons_to_erase.push_back(*icon_it);
        }
    }

    for(icon_it = icons_to_erase.begin(); icon_it != icons_to_erase.end(); ++icon_it)
    {
        icon_instance_t::iterator found_it = std::find(sIconInstances.begin(), sIconInstances.end(), *icon_it);
        if (found_it != sIconInstances.end())
        {
            sIconInstances.erase(found_it);
        }

        // <FS:Ansariel> Mark script error icons
        if ((*icon_it)->mScriptError)
        {
            icon_instance_t::iterator found_script_it = std::find(sScriptErrorIconInstances.begin(), sScriptErrorIconInstances.end(), *icon_it);
            if (found_script_it != sScriptErrorIconInstances.end())
            {
                sScriptErrorIconInstances.erase(found_script_it);
            }
        }
        // </FS:Ansariel> Mark script error icons
    }
}

//static
S32 LLHUDIcon::getNumInstances()
{
    return (S32)sIconInstances.size();
}

// <FS:Ansariel> Mark script error icons
void LLHUDIcon::setScriptError()
{
    if (std::find(sScriptErrorIconInstances.begin(), sScriptErrorIconInstances.end(), this) == sScriptErrorIconInstances.end())
    {
        mScriptError = true;
        sScriptErrorIconInstances.push_back(this);
    }
}

//static
bool LLHUDIcon::scriptIconsNearby()
{
    return !sScriptErrorIconInstances.empty();
}
// </FS:Ansariel> Mark script error icons

#if VS_NATIVE_VULKAN
void LLHUDIcon::setNativeSource(NativeSourceQuery source,const VSUIRenderer::Image& image,std::shared_ptr<const void> residency,std::function<void()> retire,std::function<VSUIRenderer::Image()> imageQuery)
{
    mNativeImageQuery=std::move(imageQuery);
    mNativeResidency=std::move(residency);mNativeRetire=std::move(retire);
    mNativeSource=std::move(source);mNativeImage=image;mNativeQuadKnown=false;
}
bool LLHUDIcon::nativeQuad(std::array<LLVector3,4>& quad,F32& alpha)
{
    if(mDead || mHidden)return false;
    const auto source=mNativeSource?mNativeSource():std::optional<NativeSource>{};
    if(!source){markDead();return false;}
    if(mLifeTimer.getElapsedTimeF32()>MAX_VISIBLE_TIME){markDead();return false;}
    if(!mNativeImage && mNativeImageQuery)mNativeImage=mNativeImageQuery();
    if(!mNativeImage)return false;
    setPositionGlobal(source->globalPosition);
    auto* camera=LLViewerCamera::getInstance();
    LLVector3 icon_relative_pos=camera->getUpAxis()*~source->rotation;icon_relative_pos.abs();
    const F32 distance_scale=llmin(source->scale.mV[VX]/icon_relative_pos.mV[VX],
        source->scale.mV[VY]/icon_relative_pos.mV[VY],source->scale.mV[VZ]/icon_relative_pos.mV[VZ]);
    const F32 up_distance=.5f*distance_scale;
    LLVector3 icon_position=source->position+(up_distance*camera->getUpAxis())*1.2f;
    LLVector3 icon_to_cam=camera->getOrigin()-icon_position;icon_to_cam.normVec();
    icon_position+=icon_to_cam*source->radius*1.1f;
    mDistance=dist_vec(icon_position,camera->getOrigin());
    alpha=clamp_rescale(mDistance,DIST_START_FADE,DIST_END_FADE,1.f,0.f);
    LLVector3 x_pixel_vec,y_pixel_vec;camera->getPixelVectors(icon_position,y_pixel_vec,x_pixel_vec);
    F32 scale_factor=1.f;
    if(mAnimTimer.getElapsedTimeF32()<ANIM_TIME)
        scale_factor=llmax(0.f,calc_bouncy_animation(mAnimTimer.getElapsedTimeF32()/ANIM_TIME));
    const F32 time_elapsed=mLifeTimer.getElapsedTimeF32();
    if(time_elapsed>MAX_VISIBLE_TIME){markDead();return false;}
    if(time_elapsed>MAX_VISIBLE_TIME-FADE_OUT_TIME)
        alpha*=clamp_rescale(time_elapsed,MAX_VISIBLE_TIME-FADE_OUT_TIME,MAX_VISIBLE_TIME,1.f,0.f);
    const auto extent=VSUIRenderer::imageExtent(mNativeImage);
    const F32 image_aspect=F32(extent[0])/F32(extent[1]);
    const LLVector3 x_scale=image_aspect*F32(gViewerWindow->getWindowHeightScaled())*mScale*scale_factor*x_pixel_vec;
    const LLVector3 y_scale=F32(gViewerWindow->getWindowHeightScaled())*mScale*scale_factor*y_pixel_vec;
    quad={icon_position-x_scale*.5f,icon_position+x_scale*.5f,
        icon_position-x_scale*.5f+y_scale,icon_position+x_scale*.5f+y_scale};
    return true;
}
void LLHUDIcon::appendNative(VSWorldRenderer& renderer,const LLVector3d& regionOrigin,std::vector<VSWorldRenderer::Packet>& packets)
{
    const auto agentOrigin=gAgent.getPosGlobalFromAgent(LLVector3::zero);
    const LLVector3 agentToRegion(agentOrigin-regionOrigin);
    std::vector<LLHUDIcon*> visible;visible.reserve(sIconInstances.size());
    for(auto& icon:sIconInstances)
    {
        if(!icon->mNativeSource)continue;
        std::array<LLVector3,4> quad;F32 alpha;
        if(!icon->nativeQuad(quad,alpha))continue;
        // Publication refusal retains both the rendered and picked generation.
        bool changed=!icon->mNativeQuadKnown || icon->mNativeRegionOrigin!=regionOrigin;
        for(unsigned i=0;i<4;++i)changed=changed || quad[i]!=icon->mNativeQuad[i];
        if(changed)
        {
            std::vector<VSWorldRenderer::Vertex> vertices;vertices.reserve(4);
            constexpr float u[]={0,1,0,1},v[]={1,1,0,0};
            for(unsigned i=0;i<4;++i){const auto p=quad[i]+agentToRegion;vertices.push_back({p.mV[0],p.mV[1],p.mV[2],0,0,1,u[i],v[i]});}
            VSWorldRenderer::Mesh next;
            if(renderer.publishMesh(next,vertices,{2,0,1,2,1,3}))
            {icon->mNativeMesh=std::move(next);icon->mNativeQuad=quad;icon->mNativeQuadKnown=true;icon->mNativeRegionOrigin=regionOrigin;icon->mNativeAgentOrigin=agentOrigin;}
            else if(!icon->mNativeMesh)continue;
        }
        icon->mNativeAlpha=alpha;
        visible.push_back(icon.get());
    }
    std::stable_sort(visible.begin(),visible.end(),[](const auto* a,const auto* b){return a->mDistance>b->mDistance;});
    for(const auto* icon:visible)if(auto packet=icon->nativePacket(regionOrigin))packets.push_back(std::move(*packet));
    cleanupDeadIcons();LLHUDObject::cleanupUnreferenced();
}
LLHUDIcon* LLHUDIcon::intersectNative(const LLVector4a& start,const LLVector4a& end,LLVector4a* intersection)
{
    LLHUDIcon* result=nullptr;LLVector4a localEnd=end;
    for(auto& icon:sIconInstances)
    {
        if(icon->mDead || icon->mHidden || !icon->mNativeQuadKnown || !icon->mNativeSource)continue;
        if(!icon->mNativeSource()){icon->markDead();continue;}
        if(icon->mLifeTimer.getElapsedTimeF32()>MAX_VISIBLE_TIME){icon->markDead();continue;}
        LLVector4a lowerLeft,lowerRight,upperLeft,upperRight,direction;
        std::array<LLVector3,4> quad;
        for(unsigned i=0;i<4;++i)quad[i]=gAgent.getPosAgentFromGlobal(icon->mNativeAgentOrigin+LLVector3d(icon->mNativeQuad[i]));
        lowerLeft.load3(quad[0].mV);lowerRight.load3(quad[1].mV);
        upperLeft.load3(quad[2].mV);upperRight.load3(quad[3].mV);
        direction.setSub(localEnd,start);F32 a,b,t;
        if(LLTriangleRayIntersect(upperRight,upperLeft,lowerRight,start,direction,a,b,t) ||
           LLTriangleRayIntersect(upperLeft,lowerLeft,lowerRight,start,direction,a,b,t))
        {
            direction.mul(t);localEnd.setAdd(start,direction);result=icon;
            if(intersection)*intersection=localEnd;
        }
    }
    return result;
}

std::optional<LLVector3> LLHUDIcon::nativePickPoint() const
{
    if(mDead || mHidden || !mNativeQuadKnown || !mNativeSource || !mNativeSource())return {};
    return gAgent.getPosAgentFromGlobal(mNativeAgentOrigin+LLVector3d((mNativeQuad[0]+mNativeQuad[3])*.5f));
}
LLPointer<LLHUDIcon> LLHUDIcon::findNative(const LLUUID& id)
{
    for(auto& icon:sIconInstances)if(icon->mNativeSource && icon->getSourceID()==id)return icon;
    return nullptr;
}
std::optional<VSWorldRenderer::Packet> LLHUDIcon::nativePacket(const LLVector3d& regionOrigin) const
{
    if(mDead || mHidden || !mNativeQuadKnown || !mNativeMesh || !mNativeImage || !mNativeSource)return {};
    const auto source=mNativeSource();if(!source)return {};
    VSWorldRenderer::Packet packet;packet.mesh=mNativeMesh;packet.image=mNativeImage;
    const LLVector3 regionOffset(mNativeRegionOrigin-regionOrigin);
    packet.regionOffset={regionOffset.mV[0],regionOffset.mV[1],regionOffset.mV[2],0};
    packet.imageSampler.u=packet.imageSampler.v=VSWorldContracts::AddressMode::Clamp;
    packet.imageSampler.mipmaps=true;
    packet.color={1,1,1,mNativeAlpha};packet.fullbright=true;packet.alpha=true;packet.overlay=true;
    packet.depthTest=true;packet.depthWriteAlpha=true;packet.doubleSided=true;
    packet.distanceSquared=mDistance*mDistance;
    std::copy_n(source->id.mData,16,packet.objectIdentity.begin());
    return packet;
}

#endif // VS_NATIVE_VULKAN

LLUUID LLHUDIcon::getSourceID() const
{
    if(mDead)return LLUUID::null;
    if(mNativeSource){const auto source=mNativeSource();return source?source->id:LLUUID::null;}
    return mSourceObject?mSourceObject->getID():LLUUID::null;
}
