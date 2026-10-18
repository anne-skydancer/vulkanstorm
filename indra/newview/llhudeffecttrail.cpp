/**
 * @file llhudeffecttrail.cpp
 * @brief LLHUDEffectSpiral class implementation
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
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

#include "llhudeffecttrail.h"
#if VS_NATIVE_VULKAN
#include "vsparticleeffects.h"
#include "vsnativesession.h"
#include "vsobjectcontext.h"
#include "vsworldscene.h"
#include "vsvulkancontext.h"
#include "llviewerwindow.h"
#endif

#include "llviewercontrol.h"
#include "message.h"

#include "llagent.h"
#include "llbox.h"
#include "lldrawable.h"
#include "llhudrender.h"
#include "llviewertexturelist.h"
#include "llviewerobjectlist.h"
#include "llviewerpartsim.h"
#include "llviewerpartsource.h"
#include "llvoavatar.h"
#include "llworld.h"

#if VS_NATIVE_VULKAN
struct LLHUDEffectSpiral::NativeEndpoints
{
    LLUUID source,target;
    std::function<bool()> current;
    std::shared_ptr<const VSResourceBudget::Reservation> residency;
};
#endif

LLHUDEffectSpiral::LLHUDEffectSpiral(const U8 type) : LLHUDEffect(type), mbInit(false)
{
    mKillTime = 10.f;
    mVMag = 1.f;
    mVOffset = 0.f;
    mInitialRadius = 1.f;
    mFinalRadius = 1.f;
    mSpinRate = 10.f;
    mFlickerRate = 50.f;
    mScaleBase = 0.1f;
    mScaleVar = 0.f;

    mFadeInterp.setStartTime(0.f);
    mFadeInterp.setEndTime(mKillTime);
    mFadeInterp.setStartVal(1.f);
    mFadeInterp.setEndVal(1.f);
}

LLHUDEffectSpiral::~LLHUDEffectSpiral()
{
}

void LLHUDEffectSpiral::markDead()
{
    if (mPartSourcep)
    {
        mPartSourcep->setDead();
        mPartSourcep = NULL;
    }
    LLHUDEffect::markDead();
}

void LLHUDEffectSpiral::packData(LLMessageSystem *mesgsys)
{
    if (!mSourceObject)
    {
        //LL_WARNS() << "Missing object in trail pack!" << LL_ENDL;
    }
    LLHUDEffect::packData(mesgsys);

    U8 packed_data[56];
    memset(packed_data, 0, 56);

    if (mSourceObject)
    {
        htolememcpy(packed_data, mSourceObject->mID.mData, MVT_LLUUID, 16);
    }
#if VS_NATIVE_VULKAN
    else if(mNativeEndpoints)
    {
        if(!mNativeEndpoints->current()){markDead();return;}
        htolememcpy(packed_data,mNativeEndpoints->source.mData,MVT_LLUUID,16);
    }
#endif
    if (mTargetObject)
    {
        htolememcpy(packed_data + 16, mTargetObject->mID.mData, MVT_LLUUID, 16);
    }
#if VS_NATIVE_VULKAN
    else if(mNativeEndpoints)htolememcpy(packed_data+16,mNativeEndpoints->target.mData,MVT_LLUUID,16);
#endif
    if (!mPositionGlobal.isExactlyZero())
    {
        htolememcpy(packed_data + 32, mPositionGlobal.mdV, MVT_LLVector3d, 24);
    }
    mesgsys->addBinaryDataFast(_PREHASH_TypeData, packed_data, 56);
}

void LLHUDEffectSpiral::unpackData(LLMessageSystem *mesgsys, S32 blocknum)
{
    const size_t EFFECT_SIZE = 56;
    U8 packed_data[EFFECT_SIZE];

    LLHUDEffect::unpackData(mesgsys, blocknum);
    LLUUID object_id, target_object_id;
    size_t size = mesgsys->getSizeFast(_PREHASH_Effect, blocknum, _PREHASH_TypeData);
    if (size != EFFECT_SIZE)
    {
        LL_WARNS() << "Spiral effect with bad size " << size << LL_ENDL;
        return;
    }
    mesgsys->getBinaryDataFast(_PREHASH_Effect, _PREHASH_TypeData,
        packed_data, EFFECT_SIZE, blocknum, EFFECT_SIZE);

    htolememcpy(object_id.mData, packed_data, MVT_LLUUID, 16);
    htolememcpy(target_object_id.mData, packed_data + 16, MVT_LLUUID, 16);
    htolememcpy(mPositionGlobal.mdV, packed_data + 32, MVT_LLVector3d, 24);

#if VS_NATIVE_VULKAN
    if(gViewerWindow && gViewerWindow->isNativeVulkan())
    {
        if(!setNativeEndpoints(object_id,target_object_id)){markDead();return;}
        triggerLocal();return;
    }
#endif
    LLViewerObject *objp = NULL;

    if (object_id.isNull())
    {
        setSourceObject(NULL);
    }
    else
    {
        LLViewerObject *objp = gObjectList.findObject(object_id);
        if (objp)
        {
            setSourceObject(objp);
        }
        else
        {
            // We don't have this object, kill this effect
            markDead();
            return;
        }
    }

    if (target_object_id.isNull())
    {
        setTargetObject(NULL);
    }
    else
    {
        objp = gObjectList.findObject(target_object_id);
        if (objp)
        {
            setTargetObject(objp);
        }
        else
        {
            // We don't have this object, kill this effect
            markDead();
            return;
        }
    }

    triggerLocal();
}

void LLHUDEffectSpiral::triggerLocal()
{
#if VS_NATIVE_VULKAN
    if(mNativeEndpoints){triggerNative();return;}
#endif

    mKillTime = mTimer.getElapsedTimeF32() + mDuration;

    bool show_beam = gSavedSettings.getBOOL("ShowSelectionBeam");

    LLColor4 color;
    color.setVec(mColor);

    if (!mPartSourcep)
    {
        if (!mTargetObject.isNull() && !mSourceObject.isNull())
        {
            if (show_beam)
            {
                LLPointer<LLViewerPartSourceBeam> psb = new LLViewerPartSourceBeam;
                psb->setColor(color);
                psb->setSourceObject(mSourceObject);
                psb->setTargetObject(mTargetObject);
                psb->setOwnerUUID(gAgent.getID());
                LLViewerPartSim::getInstance()->addPartSource(psb);
                mPartSourcep = psb;
            }
        }
        else
        {
            if (!mSourceObject.isNull() && !mPositionGlobal.isExactlyZero())
            {
                if (show_beam)
                {
                    LLPointer<LLViewerPartSourceBeam> psb = new LLViewerPartSourceBeam;
                    psb->setSourceObject(mSourceObject);
                    psb->setTargetObject(NULL);
                    psb->setColor(color);
                    psb->mLKGTargetPosGlobal = mPositionGlobal;
                    psb->setOwnerUUID(gAgent.getID());
                    LLViewerPartSim::getInstance()->addPartSource(psb);
                    mPartSourcep = psb;
                }
            }
            else
            {
                LLVector3 pos;
                if (mSourceObject)
                {
                    pos = mSourceObject->getPositionAgent();
                }
                else
                {
                    pos = gAgent.getPosAgentFromGlobal(mPositionGlobal);
                }
                LLPointer<LLViewerPartSourceSpiral> pss = new LLViewerPartSourceSpiral(pos);
                if (!mSourceObject.isNull())
                {
                    pss->setSourceObject(mSourceObject);
                }
                pss->setColor(color);
                pss->setOwnerUUID(gAgent.getID());
                LLViewerPartSim::getInstance()->addPartSource(pss);
                mPartSourcep = pss;
            }
        }
    }
    else
    {
        LLPointer<LLViewerPartSource>& ps = mPartSourcep;
        if (mPartSourcep->getType() == LLViewerPartSource::LL_PART_SOURCE_BEAM)
        {
            LLViewerPartSourceBeam *psb = (LLViewerPartSourceBeam *)ps.get();
            psb->setSourceObject(mSourceObject);
            psb->setTargetObject(mTargetObject);
            psb->setColor(color);
            if (mTargetObject.isNull())
            {
                psb->mLKGTargetPosGlobal = mPositionGlobal;
            }
        }
        else
        {
            LLViewerPartSourceSpiral *pss = (LLViewerPartSourceSpiral *)ps.get();
            pss->setSourceObject(mSourceObject);
        }
    }

    mbInit = true;
}

void LLHUDEffectSpiral::setTargetObject(LLViewerObject *objp)
{
    if (objp == mTargetObject)
    {
        return;
    }

    mTargetObject = objp;
}

void LLHUDEffectSpiral::render()
{
#if VS_NATIVE_VULKAN
    if(mNativeEndpoints && !mNativeEndpoints->current()){markDead();return;}
#endif

    F32 time = mTimer.getElapsedTimeF32();

    static LLCachedControl<bool> showSelectionBeam(gSavedSettings, "ShowSelectionBeam"); // <FS:Ansariel> Performance tweak
    if ((!mSourceObject.isNull() && mSourceObject->isDead()) ||
        (!mTargetObject.isNull() && mTargetObject->isDead()) ||
        mKillTime < time ||
        // <FS:Ansariel> Performance tweak
        //(!mPartSourcep.isNull() && !gSavedSettings.getBOOL("ShowSelectionBeam")) )
        (!mPartSourcep.isNull() && !showSelectionBeam) )
    {
        markDead();
        return;
    }
}

void LLHUDEffectSpiral::renderForTimer()
{
    render();
}

#if VS_NATIVE_VULKAN
bool LLHUDEffectSpiral::setNativeEndpoints(const LLUUID& source,const LLUUID& target)
{
    const auto owner=VSNativeSession::active();
    if(!owner || !gViewerWindow || !gViewerWindow->nativeContext())return false;
    auto& world=gViewerWindow->nativeContext()->world();
    const auto sourceObject=source.notNull()?world.object(source):std::nullopt;
    const auto targetObject=target.notNull()?world.object(target):std::nullopt;
    if((source.notNull() && !sourceObject) || (target.notNull() && !targetObject))return false;
    if(mNativeEndpoints && mNativeEndpoints->source==source && mNativeEndpoints->target==target && mNativeEndpoints->current())return true;
    auto residency=world.reserveSpatialBytes(sizeof(NativeEndpoints)+sizeof(VSWorldScene::ObjectHandle)*2);
    if(!residency)return false;
    const auto account=owner->accountGuard();
    const auto sourceCurrent=sourceObject?world.guard(*sourceObject):std::function<bool()>([]{return true;});
    const auto targetCurrent=targetObject?world.guard(*targetObject):std::function<bool()>([]{return true;});
    auto endpoints=std::make_shared<NativeEndpoints>();endpoints->source=source;endpoints->target=target;endpoints->residency=std::move(residency);
    endpoints->current=[account,sourceCurrent,targetCurrent]{return account() && sourceCurrent() && targetCurrent();};
    if(!endpoints->current())return false;
    if(mPartSourcep){mPartSourcep->setDead();mPartSourcep=nullptr;}
    mNativeEndpoints=std::move(endpoints);return true;
}
bool LLHUDEffectSpiral::triggerNative()
{
    if(!mNativeEndpoints || !mNativeEndpoints->current()){markDead();return false;}
    mKillTime=mTimer.getElapsedTimeF32()+mDuration;
    const auto owner=VSNativeSession::active();auto* particles=owner?owner->particles():nullptr;
    if(!particles)return false;
    const LLColor4 color(mColor);
    const bool beam=mNativeEndpoints->source.notNull() && (mNativeEndpoints->target.notNull() || !mPositionGlobal.isExactlyZero());
    if(!mPartSourcep)
    {
        if(beam)
        {
            if(gSavedSettings.getBOOL("ShowSelectionBeam"))
                mPartSourcep=particles->beam(mNativeEndpoints->source,mNativeEndpoints->target,mPositionGlobal,color);
        }
        else
        {
            auto source=mNativeEndpoints->source.isNull()?std::optional<VSObjectContext>{}:VSObjectContext::resolve(mNativeEndpoints->source);
            const auto position=source?gAgent.getPosAgentFromGlobal(source->position):gAgent.getPosAgentFromGlobal(mPositionGlobal);
            LLPointer<LLViewerPartSourceSpiral> spiral=new LLViewerPartSourceSpiral(position);
            spiral->setColor(color);spiral->setOwnerUUID(gAgent.getID());spiral->mNativeSourceID=mNativeEndpoints->source;
            if(mNativeEndpoints->source.notNull())
            {
                const auto id=mNativeEndpoints->source;const auto current=mNativeEndpoints->current;
                const auto query=[id,current]()->std::optional<LLVector3>
                {
                    if(!current() || !gViewerWindow || !gViewerWindow->nativeContext())return {};
                    auto& world=gViewerWindow->nativeContext()->world();const auto object=world.object(id);
                    return object?world.particlePosition(*object):std::nullopt;
                };
                if(!particles->positionSource(spiral,query,position,particles->sourcePolicy(id,gAgent.getID())))return false;
            }
            LLViewerPartSim::instance().addPartSource(spiral);mPartSourcep=spiral;
        }
    }
    else if(auto* source=dynamic_cast<LLViewerPartSourceBeam*>(mPartSourcep.get()))
    {source->setColor(color);if(mNativeEndpoints->target.isNull())source->mLKGTargetPosGlobal=mPositionGlobal;}
    mbInit=true;return true;
}
#endif
