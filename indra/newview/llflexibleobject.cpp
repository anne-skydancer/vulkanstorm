/**
 * @file llflexibleobject.cpp
 * @brief Flexible object implementation
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

#include "pipeline.h"
#include "lldrawpoolbump.h"
#include "llface.h"
#include "llflexibleobject.h"
#include "llglheaders.h"
#include "llrendersphere.h"
#include "llviewerobject.h"
#include "llagent.h"
#include "llsky.h"
#include "llviewercamera.h"
#include "llviewertexturelist.h"
#include "llviewercontrol.h"
#include "llviewerobjectlist.h"
#include "llviewerregion.h"
#include "llworld.h"
#include "llvoavatar.h"

static const F32 SEC_PER_FLEXI_FRAME = 1.f / 60.f; // 60 flexi updates per second
/*static*/ F32 LLVolumeImplFlexible::sUpdateFactor = 1.0f;
std::vector<LLVolumeImplFlexible*> LLVolumeImplFlexible::sInstanceList;

// LLFlexibleObjectData::pack/unpack now in llprimitive.cpp

//-----------------------------------------------
// constructor
//-----------------------------------------------
LLVolumeImplFlexible::LLVolumeImplFlexible(LLViewerObject* vo, LLFlexibleObjectData* attributes) :
        mVO(vo),
        mAttributes(attributes),
        mLastFrameNum(0),
        mLastUpdatePeriod(0)
{
    static U32 seed = 0;
    mID = seed++;
    mInitialized = false;
    mUpdated = false;
    mInitializedRes = -1;
    mSimulateRes = 0;
    mCollisionSphereRadius = 0.f;
    mRenderRes = -1;

    if(mVO->mDrawable.notNull())
    {
        mVO->mDrawable->makeActive() ;
    }

    mInstanceIndex = static_cast<S32>(sInstanceList.size());
    sInstanceList.push_back(this);
}//-----------------------------------------------

LLVolumeImplFlexible::~LLVolumeImplFlexible()
{
    S32 end_idx = static_cast<S32>(sInstanceList.size()) - 1;

    if (end_idx != mInstanceIndex)
    {
        sInstanceList[mInstanceIndex] = sInstanceList[end_idx];
        sInstanceList[mInstanceIndex]->mInstanceIndex = mInstanceIndex;
    }

    sInstanceList.pop_back();
}

//static
void LLVolumeImplFlexible::updateClass()
{
    LL_PROFILE_ZONE_SCOPED;

    U64 virtual_frame_num = (U64)(LLTimer::getElapsedSeconds() / SEC_PER_FLEXI_FRAME);
    for (std::vector<LLVolumeImplFlexible*>::iterator iter = sInstanceList.begin();
            iter != sInstanceList.end();
            ++iter)
    {
        // Note: by now update period might have changed
        if ((*iter)->mRenderRes == -1
            || (*iter)->mLastFrameNum + (*iter)->mLastUpdatePeriod <= virtual_frame_num
            || (*iter)->mLastFrameNum > virtual_frame_num) //time issues, overflow
        {
            (*iter)->doIdleUpdate();
        }
    }
}

LLVector3 LLVolumeImplFlexible::getFramePosition() const
{
    return mVO->getRenderPosition();
}

LLQuaternion LLVolumeImplFlexible::getFrameRotation() const
{
    return mVO->getRenderRotation();
}

void LLVolumeImplFlexible::onParameterChanged(U16 param_type, LLNetworkData *data, bool in_use, bool local_origin)
{
    if (param_type == LLNetworkData::PARAMS_FLEXIBLE)
    {
        mAttributes = (LLFlexibleObjectData*)data;
        setAttributesOfAllSections();
    }
}

void LLVolumeImplFlexible::onShift(const LLVector4a &shift_vector)
{
    //VECTORIZE THIS
    LLVector3 shift(shift_vector.getF32ptr());
    for (int section = 0; section < (1<<FLEXIBLE_OBJECT_MAX_SECTIONS)+1; ++section)
    {
        mSection[section].mPosition += shift;
    }
}

//-----------------------------------------------------------------------------------------------
void LLVolumeImplFlexible::setParentPositionAndRotationDirectly( LLVector3 p, LLQuaternion r )
{
    mParentPosition = p;
    mParentRotation = r;

}//-----------------------------------------------------------------------------------------------------

void LLVolumeImplFlexible::remapSections(LLFlexibleObjectSection *source, S32 source_sections,
                                         LLFlexibleObjectSection *dest, S32 dest_sections)
{ vs_flexible_remap(source, source_sections, dest, dest_sections, mVO->mDrawable->getScale()); }

//-----------------------------------------------------------------------------
void LLVolumeImplFlexible::setAttributesOfAllSections(LLVector3* inScale)
{
    if (!mVO->getVolume() || !mVO->mDrawable)
        return;
    const LLVector3 scale = inScale ? *inScale : mVO->mDrawable->getScale();
    vs_flexible_parameters(mSection, mInitializedRes, mSimulateRes, mVO->getVolume()->getParams().getPathParams(), scale,
                           getFramePosition(), getFrameRotation());
}//-----------------------------------------------------------------------------------


void LLVolumeImplFlexible::onSetVolume(const LLVolumeParams &volume_params, const S32 detail)
{
}


void LLVolumeImplFlexible::updateRenderRes()
{
    if (!mAttributes)
        return;

    LLDrawable* drawablep = mVO->mDrawable;

    S32 new_res = mAttributes->getSimulateLOD();

#if 1 //optimal approximation of previous behavior that doesn't rely on atan2
    F32 app_angle = mVO->getScale().mV[2]/drawablep->mDistanceWRTCamera;

    // Rendering sections increases with visible angle on the screen
    mRenderRes = (S32) (12.f*app_angle);
#else //legacy behavior
    //number of segments only cares about z axis
    F32 app_angle = ll_round((F32) atan2( mVO->getScale().mV[2]*2.f, drawablep->mDistanceWRTCamera) * RAD_TO_DEG, 0.01f);

    // Rendering sections increases with visible angle on the screen
    mRenderRes = (S32)(FLEXIBLE_OBJECT_MAX_SECTIONS*4*app_angle*DEG_TO_RAD/LLViewerCamera::getInstance()->getView());
#endif

    mRenderRes = llclamp(mRenderRes, new_res-1, (S32) FLEXIBLE_OBJECT_MAX_SECTIONS);

    // Throttle back simulation of segments we're not rendering
    if (mRenderRes < new_res)
    {
        new_res = mRenderRes;
    }

    if (!mInitialized || (mSimulateRes != new_res))
    {
        mSimulateRes = new_res;
        setAttributesOfAllSections();
        mInitialized = true;
    }
}
//---------------------------------------------------------------------------------
// This calculates the physics of the flexible object. Note that it has to be 0
// updated every time step. In the future, perhaps there could be an
// optimization similar to what Havok does for objects that are stationary.
//---------------------------------------------------------------------------------
void LLVolumeImplFlexible::doIdleUpdate()
{
    LLDrawable* drawablep = mVO->mDrawable;

    if (drawablep)
    {
        //ensure drawable is active
        drawablep->makeActive();

        if (gPipeline.hasRenderDebugFeatureMask(LLPipeline::RENDER_DEBUG_FEATURE_FLEXIBLE))
        {
            bool visible = drawablep->isVisible();

            if (mRenderRes == -1)
            {
                updateRenderRes();
                gPipeline.markRebuild(drawablep, LLDrawable::REBUILD_POSITION);
            }
            else
            {
                F32 pixel_area = mVO->getPixelArea();

                // Note: Flexies afar will be rarely updated, closer ones will be updated more frequently.
                // But frequency differences are extremely noticeable, so consider modifying update factor,
                // or at least clamping value a bit more from both sides.
                U32 update_period = (U32) (llmax((S32) (LLViewerCamera::getInstance()->getScreenPixelArea()*0.01f/(pixel_area*(sUpdateFactor+1.f))),0)+1);
                // MAINT-1890 Clamp the update period to ensure that the update_period is no greater than 32 frames
                update_period = llclamp(update_period, 1U, 32U);

                // We control how fast flexies update, buy splitting updates among frames
                U64 virtual_frame_num = (U64)(LLTimer::getElapsedSeconds() / SEC_PER_FLEXI_FRAME);

                if  (visible)
                {
                    if (!drawablep->isState(LLDrawable::IN_REBUILD_Q) &&
                        pixel_area > 256.f)
                    {
                        U32 id;
                        if (mVO->isRootEdit())
                        {
                            id = mID;
                        }
                        else
                        {
                            LLVOVolume* parent = (LLVOVolume*)mVO->getParent();
                            id = parent->getVolumeInterfaceID();
                        }


                        // Throttle flexies and spread load by preventing flexies from updating in same frame
                        // Shows how many frames we need to wait before next update
                        U64 throttling_delay = (virtual_frame_num + id) % update_period;

                        if ((throttling_delay == 0 && mLastFrameNum < virtual_frame_num) //one or more virtual frames per frame
                            || (mLastFrameNum + update_period < virtual_frame_num) // missed virtual frame
                            || mLastFrameNum > virtual_frame_num) // overflow
                        {
                            // We need mLastFrameNum to compensate for 'unreliable time' and to filter 'duplicate' frames
                            // If happened too late, subtract throttling_delay (it is zero otherwise)
                            mLastFrameNum = virtual_frame_num - throttling_delay;

                            // Store update period for updateClass()
                            // Note: Consider substituting update_period with mLastUpdatePeriod everywhere.
                            mLastUpdatePeriod = update_period;

                            updateRenderRes();

                            mVO->shrinkWrap();
                            gPipeline.markRebuild(drawablep, LLDrawable::REBUILD_POSITION);
                        }
                    }
                }
                else
                {
                    mLastFrameNum = virtual_frame_num;
                    mLastUpdatePeriod = update_period;
                }
            }

        }
    }
}

inline S32 log2(S32 x)
{
    S32 ret = 0;
    while (x > 1)
    {
        ++ret;
        x >>= 1;
    }
    return ret;
}

void LLVolumeImplFlexible::doFlexibleUpdate()
{
    LL_PROFILE_ZONE_SCOPED;
    LLVolume* volume = mVO->getVolume();
    if ((mSimulateRes == 0 || !mInitialized) && mVO->mDrawable->isVisible())
    {
        bool force_update = mSimulateRes == 0;
        doIdleUpdate();

        if (!force_update || !gPipeline.hasRenderDebugFeatureMask(LLPipeline::RENDER_DEBUG_FEATURE_FLEXIBLE))
        {
            return; // we did not get updated or initialized, proceeding without can be dangerous
        }
    }

    if (!mInitialized || !mAttributes)
    {
        //the object is not visible
        return;
    }

    // Fix for MAINT-1894
    // Skipping the flexible update if render res is negative.  If we were to continue with a negative value,
    // the subsequent S32 num_render_sections = 1<<mRenderRes; code will specify a really large number of
    // render sections which will then create a length exception in the std::vector::resize() method.
    if (mRenderRes < 0)
    {
        return;
    }

    const auto result = vs_flexible_step(
        mSection, mSimulateRes, mRenderRes, *volume, *mAttributes, getFramePosition(), getFrameRotation(), mVO->mDrawable->getScale(),
        mTimer.getElapsedTimeAndResetF32(), mVO->mDrawable->mDistanceWRTCamera, [](const LLVector3& position)
        { return gAgent.getRegion() ? gAgent.getRegion()->mWind.getVelocity(position) : LLVector3::zero; }, mUpdated, mLastSegmentRotation);
    if (result.topologyChanged)
        ((LLVOVolume*)mVO)->mVolumeChanged = true;
    if (result.positionsChanged)
        mUpdated = false;
}


void LLVolumeImplFlexible::preRebuild()
{
    if (!mUpdated)
    {
        LL_PROFILE_ZONE_SCOPED;
        doFlexibleRebuild(false);
    }
}

void LLVolumeImplFlexible::doFlexibleRebuild(bool rebuild_volume)
{
    LLVolume* volume = mVO->getVolume();
    if (volume)
    {
        if (rebuild_volume)
        {
            volume->setDirty();
        }
        volume->regen();
    }

    mUpdated = true;
}

//------------------------------------------------------------------

void LLVolumeImplFlexible::onSetScale(const LLVector3& scale, bool damped)
{
    setAttributesOfAllSections((LLVector3*) &scale);
}

bool LLVolumeImplFlexible::doUpdateGeometry(LLDrawable *drawable)
{
    LL_PROFILE_ZONE_SCOPED;
    LLVOVolume *volume = (LLVOVolume*)mVO;

    if (mVO->isAttachment())
    {   //don't update flexible attachments for impostored avatars unless the
        //impostor is being updated this frame (w00!)
        LLViewerObject* parent = (LLViewerObject*) mVO->getParent();
        while (parent && !parent->isAvatar())
        {
            parent = (LLViewerObject*) parent->getParent();
        }

        if (parent)
        {
            LLVOAvatar* avatar = (LLVOAvatar*) parent;
            if (avatar->isImpostor() && !avatar->needsImpostorUpdate())
            {
                return true;
            }
        }
    }

    if (volume->mDrawable.isNull() || volume->mDrawable->isDead())
    {
        return true; // No update to complete
    }

    if (volume->mLODChanged)
    {
        LLVolumeParams volume_params = volume->getVolume()->getParams();
        volume->setVolume(volume_params, 0);
        mUpdated = false;
    }

    volume->updateRelativeXform();

    doFlexibleUpdate();

    // Object may have been rotated, which means it needs a rebuild.  See SL-47220
    bool    rotated = false;
    LLQuaternion cur_rotation = getFrameRotation();
    if ( cur_rotation != mLastFrameRotation )
    {
        mLastFrameRotation = cur_rotation;
        rotated = true;
    }

    if (volume->mLODChanged || volume->mFaceMappingChanged ||
        volume->mVolumeChanged || drawable->isState(LLDrawable::REBUILD_MATERIAL))
    {
        volume->regenFaces();
        volume->mDrawable->setState(LLDrawable::REBUILD_VOLUME);
        volume->dirtySpatialGroup();
        {
            doFlexibleRebuild(volume->mVolumeChanged);
        }
        volume->genBBoxes(isVolumeGlobal());
    }
    else if (!mUpdated || rotated)
    {
        volume->mDrawable->setState(LLDrawable::REBUILD_POSITION);
        LLSpatialGroup* group = volume->mDrawable->getSpatialGroup();
        if (group)
        {
            group->dirtyMesh();
        }
        volume->genBBoxes(isVolumeGlobal());
    }

    volume->mVolumeChanged = false;
    volume->mLODChanged = false;
    volume->mFaceMappingChanged = false;

    // clear UV flag
    drawable->clearState(LLDrawable::UV);

    return true;
}

//----------------------------------------------------------------------------------
void LLVolumeImplFlexible::setCollisionSphere( LLVector3 p, F32 r )
{
    mCollisionSpherePosition = p;
    mCollisionSphereRadius   = r;

}//------------------------------------------------------------------


//----------------------------------------------------------------------------------
void LLVolumeImplFlexible::setUsingCollisionSphere( bool u )
{
}//------------------------------------------------------------------


//----------------------------------------------------------------------------------
void LLVolumeImplFlexible::setRenderingCollisionSphere( bool r )
{
}//------------------------------------------------------------------

//------------------------------------------------------------------
LLVector3 LLVolumeImplFlexible::getEndPosition()
{
    S32 num_sections = 1 << mAttributes->getSimulateLOD();
    return mSection[ num_sections ].mPosition;

}//------------------------------------------------------------------


//------------------------------------------------------------------
LLVector3 LLVolumeImplFlexible::getNodePosition( int nodeIndex )
{
    S32 num_sections = 1 << mAttributes->getSimulateLOD();
    if ( nodeIndex > num_sections - 1 )
    {
        nodeIndex = num_sections - 1;
    }
    else if ( nodeIndex < 0 )
    {
        nodeIndex = 0;
    }

    return mSection[ nodeIndex ].mPosition;

}//------------------------------------------------------------------

LLVector3 LLVolumeImplFlexible::getPivotPosition() const
{
    return getAnchorPosition();
}

//------------------------------------------------------------------
LLVector3 LLVolumeImplFlexible::getAnchorPosition() const
{
    LLVector3 BasePosition = getFramePosition();
    LLQuaternion parentSegmentRotation = getFrameRotation();
    LLVector3 anchorDirectionRotated = LLVector3::z_axis * parentSegmentRotation;
    LLVector3 anchorScale = mVO->mDrawable->getScale();
    return BasePosition - (anchorScale.mV[VZ]/2 * anchorDirectionRotated);

}//------------------------------------------------------------------


//------------------------------------------------------------------
LLQuaternion LLVolumeImplFlexible::getEndRotation()
{
    return mLastSegmentRotation;

}//------------------------------------------------------------------


void LLVolumeImplFlexible::updateRelativeXform(bool force_identity)
{
    LLQuaternion delta_rot;
    LLVector3 delta_pos, delta_scale;
    LLVOVolume* vo = (LLVOVolume*) mVO;

    bool use_identity = vo->mDrawable->isSpatialRoot() || force_identity;

    //matrix from local space to parent relative/global space
    delta_rot = use_identity ? LLQuaternion() : vo->mDrawable->getRotation();
    delta_pos = use_identity ? LLVector3(0,0,0) : vo->mDrawable->getPosition();
    delta_scale = LLVector3(1,1,1);

    // Vertex transform (4x4)
    LLVector3 x_axis = LLVector3(delta_scale.mV[VX], 0.f, 0.f) * delta_rot;
    LLVector3 y_axis = LLVector3(0.f, delta_scale.mV[VY], 0.f) * delta_rot;
    LLVector3 z_axis = LLVector3(0.f, 0.f, delta_scale.mV[VZ]) * delta_rot;

    vo->mRelativeXform.initRows(LLVector4(x_axis, 0.f),
                            LLVector4(y_axis, 0.f),
                            LLVector4(z_axis, 0.f),
                            LLVector4(delta_pos, 1.f));

    x_axis.normVec();
    y_axis.normVec();
    z_axis.normVec();

    vo->mRelativeXformInvTrans.setRows(x_axis, y_axis, z_axis);
}

const LLMatrix4& LLVolumeImplFlexible::getWorldMatrix(LLXformMatrix* xform) const
{
    return xform->getWorldMatrix();
}
