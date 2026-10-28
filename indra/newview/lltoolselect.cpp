/**
 * @file lltoolselect.cpp
 * @brief LLToolSelect class implementation
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
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

#include "lltoolselect.h"
#include "vsselectionflow.h"
#if VS_NATIVE_VULKAN
#include "vsnativesession.h"
#include "vsobjectselection.h"
#include "vsobjectcontext.h"
#include "vsworldavatar.h"
#include "vsvulkancontext.h"
#endif

#include "llagent.h"
#include "llagentcamera.h"
#include "llviewercontrol.h"
#include "lldrawable.h"
#include "llhudicon.h"
#include "llmanip.h"
#include "llmenugl.h"
#include "llselectmgr.h"
#include "llviewermediafocus.h"
#include "lltoolmgr.h"
#include "llfloaterscriptdebug.h"
#include "llviewercamera.h"
#include "llviewermenu.h"
#include "llviewerobject.h"
#include "llviewerobjectlist.h"
#include "llviewerregion.h"
#include "llviewerwindow.h"
#include "llvoavatarself.h"
#include "llworld.h"
// [RLVa:KB] - Checked: RLVa-2.0.0
#include "rlvactions.h"
#include "rlvmodifiers.h"
#include "llfloaterreg.h"
// [/RLVa:KB]

constexpr F32 SELECTION_ROTATION_TRESHOLD = 0.1f;
constexpr F32 SELECTION_SITTING_ROTATION_TRESHOLD = 3.2f; //radian

LLToolSelect::LLToolSelect( LLToolComposite* composite )
:   LLTool( std::string("Select"), composite ),
    mIgnoreGroup( false )
{
}

// True if you selected an object.
bool LLToolSelect::handleMouseDown(S32 x, S32 y, MASK mask)
{
    // do immediate pick query
    bool pick_rigged = false; //gSavedSettings.getBOOL("AnimatedObjectsAllowLeftClick");
    static LLCachedControl<bool> select_invisible_objects(gSavedSettings, "SelectInvisibleObjects");
    static LLCachedControl<bool> select_reflection_probes(gSavedSettings, "SelectReflectionProbes");

    mPick = gViewerWindow->pickImmediate(x, y, select_invisible_objects, pick_rigged, false, true, select_reflection_probes);

    // Pass mousedown to agent
    LLTool::handleMouseDown(x, y, mask);

    #if VS_NATIVE_VULKAN
    if(gViewerWindow->isNativeVulkan())return mPick.mObjectID.notNull() &&
        (!mPick.mNativeObjectCurrent || mPick.mNativeObjectCurrent());
    #endif
    return mPick.getObject().notNull();
}

// static
namespace
{
// This peer supplies the original owners lazily. The branch kernel is shared
// with native scene handles; no legacy object is manufactured by native code.
struct ReferenceSelectionPeer
{
    bool current()const{return true;}
    LLViewerObject* object(const LLPickInfo& pick)const{return pick.getObject();}
    LLViewerObject* rootEdit(LLViewerObject* object)const{return object?object->getRootEdit():nullptr;}
    bool rlvEnabled()const{return RlvActions::isRlvEnabled();}
    bool canEdit(LLViewerObject* object)const{return RlvActions::canEdit(object);}
    bool inBuildMode()const{return LLToolMgr::instance().inBuildMode();}
    void leaveBuildMode()const{LLToolMgr::instance().leaveBuildMode();}
    bool fartouch()const{return RlvActions::hasBehaviour(RLV_BHVR_FARTOUCH);}
    bool attachment(LLViewerObject* object)const{return object->isAttachment();}
    bool youOwner(LLViewerObject* object)const{return object->permYouOwner();}
    F32 distanceToObjectSquared(LLViewerObject* object)const{return dist_vec_squared(gAgent.getPositionAgent(),object->getPositionAgent());} // FIRE-37061: agent space
    F32 fartouchDistanceSquared()const
    {
        static RlvCachedBehaviourModifier<float> distance(RLV_MODIFIER_FARTOUCHDIST);
        return distance*distance;
    }
    F32 distanceToSurfaceSquared(const LLPickInfo& pick)const{return dist_vec_squared(gAgent.getPositionAgent(),pick.mIntersection);}
    bool buildVisible()const{return LLFloaterReg::instanceVisible("build");}
    MASK mask(const LLPickInfo& pick)const{return pick.mKeyMask;}
    LLObjectSelectionHandle selection()const{return LLSelectMgr::getInstance()->getSelection();}
    bool setting(const char* name)const{return gSavedSettings.getBOOL(name);}
    void setting(const char* name,bool value)const{gSavedSettings.setBOOL(name,value);}
    bool allowSelectAvatar()const{return LLSelectMgr::getInstance()->mAllowSelectAvatar;}
    void forceSelection(bool value)const{LLSelectMgr::getInstance()->setForceSelection(value);}
    bool scriptIcon(const LLPickInfo& pick)const{return pick.mHUDIcon && pick.mHUDIcon->getSourceObject();}
    void showScriptDebug(const LLPickInfo& pick)const{LLFloaterScriptDebug::show(pick.mHUDIcon->getSourceObject()->getID());}
    bool selected(LLViewerObject* object)const{return object->isSelected();}
    S32 faceCount(LLViewerObject* object)const{return object->getNumTEs();}
    bool containsAllFaces(LLViewerObject* object)const{return selection()->contains(object,SELECT_ALL_TES);}
    std::optional<bool> faceSelection(LLViewerObject* object,const LLPickInfo& pick)const
    {
        const auto* face=object->getTE(pick.mObjectFace);
        return face?std::optional<bool>(face->isSelected()):std::nullopt;
    }
    bool hasMediaFocus()const{return !LLViewerMediaFocus::getInstance()->getFocusedObjectID().isNull();}
    void clearMediaFocus()const{LLViewerMediaFocus::getInstance()->clearFocus();}
    void deselectOnly(LLViewerObject* object)const{LLSelectMgr::getInstance()->deselectObjectOnly(object);}
    void deselectFamily(LLViewerObject* object)const{LLSelectMgr::getInstance()->deselectObjectAndFamily(object,true,true);}
    void deselectAll()const{LLSelectMgr::getInstance()->deselectAll();}
    void selectOnly(LLViewerObject* object,S32 face,S32 node,S32 primitive)const{LLSelectMgr::getInstance()->selectObjectOnly(object,face,node,primitive);}
    void selectFamily(LLViewerObject* object)const{LLSelectMgr::getInstance()->selectObjectAndFamily(object);}
    std::pair<F32,F32> hudZoom()const
    {
        std::pair<F32,F32> result;
        LLSelectMgr::getInstance()->getAgentHUDZoom(result.first,result.second);
        return result;
    }
    void hudZoom(const std::pair<F32,F32>& zoom)const{LLSelectMgr::getInstance()->setAgentHUDZoom(zoom.first,zoom.second);}
    S32 gltfNode(const LLPickInfo& pick)const{return pick.mGLTFNodeIndex;}
    S32 gltfPrimitive(const LLPickInfo& pick)const{return pick.mGLTFPrimitiveIndex;}
    bool focusOnAvatar()const{return gAgentCamera.getFocusOnAvatar();}
    bool selfAttachment(LLViewerObject* object)const{return LLVOAvatar::findAvatarFromAttachment(object)==gAgentAvatarp;}
    bool self(LLViewerObject* object)const{return object==gAgentAvatarp;}
    void turnToSelection(LLViewerObject* object)const
    {
        LLVector3d selection_center=LLSelectMgr::getInstance()->getSelectionCenterGlobal();
        selection_center=selection_center-gAgent.getPositionGlobal();
        LLVector3 selection_dir;
        selection_dir.setVec(selection_center);
        selection_dir.mV[VZ]=0.f;
        selection_dir.normVec();
        if(!object->isAvatar() && gAgent.getAtAxis()*selection_dir<.6f)
        {
            LLQuaternion target_rot;
            target_rot.shortestArc(LLVector3::x_axis,selection_dir);
            gAgent.startAutoPilotGlobal(gAgent.getPositionGlobal(),"",&target_rot,NULL,NULL,MAX_FAR_CLIP,
                gAgentAvatarp->isSitting()?SELECTION_SITTING_ROTATION_TRESHOLD:SELECTION_ROTATION_TRESHOLD);
        }
    }
    void transientRootAndChildren(LLViewerObject* object)const
    {
        auto* root=object->getRootEdit();
        auto selection=LLSelectMgr::getInstance()->getSelection();
        if(auto* node=selection->findNode(root))node->setTransient(true);
        for(const auto& child:root->getChildren())if(auto* node=selection->findNode(child))node->setTransient(true);
    }
};
}


LLObjectSelectionHandle LLToolSelect::handleObjectSelection(const LLPickInfo& pick, bool ignore_group, bool temp_select, bool select_root)
{
    ReferenceSelectionPeer peer;
    return vs_handle_object_selection(peer,pick,ignore_group,temp_select,select_root);
}

bool LLToolSelect::handleMouseUp(S32 x, S32 y, MASK mask)
{
    mIgnoreGroup = gSavedSettings.getBOOL("EditLinkedParts");

    #if VS_NATIVE_VULKAN
    if(gViewerWindow->isNativeVulkan())handleNativeObjectSelection(mPick,mIgnoreGroup,false);
    else
    #endif
    handleObjectSelection(mPick, mIgnoreGroup, false);

    return LLTool::handleMouseUp(x, y, mask);
}

void LLToolSelect::handleDeselect()
{
    if( hasMouseCapture() )
    {
        setMouseCapture( false );  // Calls onMouseCaptureLost() indirectly
    }
}


void LLToolSelect::stopEditing()
{
    if( hasMouseCapture() )
    {
        setMouseCapture( false );  // Calls onMouseCaptureLost() indirectly
    }
}

void LLToolSelect::onMouseCaptureLost()
{
    // Finish drag

    LLSelectMgr::getInstance()->enableSilhouette(true);

    // Clean up drag-specific variables
    mIgnoreGroup = false;
}





#if VS_NATIVE_VULKAN
namespace
{
struct NativeSelectionPeer
{
    using Object=std::optional<VSWorldScene::ObjectHandle>;
    std::shared_ptr<VSNativeSession> session;
    std::shared_ptr<VSObjectSelection> lifetime;
    VSWorldScene& world;
    VSObjectSelection& owner;
    std::function<bool()> account;
    bool current()const{return account && account() && session->phase()==VSNativeSession::Phase::Connected &&
        session->existingObjectSelection()==lifetime.get() && bool(owner.retainSelection());}
    Object object(const LLPickInfo& pick)const{return current()?world.object(pick.mObjectID):Object();}
    Object rootEdit(const Object& object)const{return object?world.rootObject(*object,true):Object();}
    bool rlvEnabled()const{return RlvActions::isRlvEnabled();}
    bool canEdit(const Object& object)const
    {
        const auto context=object?VSObjectContext::resolve(object->id):std::nullopt;
        return context && context->current() && RlvActions::canEdit(*context);
    }
    bool inBuildMode()const{return LLToolMgr::instance().inBuildMode();}
    void leaveBuildMode()const{LLToolMgr::instance().leaveBuildMode();}
    bool fartouch()const{return RlvActions::hasBehaviour(RLV_BHVR_FARTOUCH);}
    bool attachment(const Object& object)const{const auto row=world.objectAdmission(*object);return row && row->attachment;}
    bool youOwner(const Object& object)const{const auto row=world.objectAdmission(*object);return row && VSObjectContext::ownedByAgent(row->rootUpdateFlags);}
    F32 distanceToObjectSquared(const Object& object)const
    {
        const auto context=VSObjectContext::resolve(object->id);
        return context && context->current()?F32(dist_vec_squared(gAgent.getPositionGlobal(),context->position)):F32_MAX;
    }
    F32 fartouchDistanceSquared()const
    {
        static RlvCachedBehaviourModifier<float> distance(RLV_MODIFIER_FARTOUCHDIST);
        return distance*distance;
    }
    F32 distanceToSurfaceSquared(const LLPickInfo& pick)const{return dist_vec_squared(gAgent.getPositionAgent(),pick.mIntersection);}
    bool buildVisible()const{return LLFloaterReg::instanceVisible("build");}
    MASK mask(const LLPickInfo& pick)const{return pick.mKeyMask;}
    std::shared_ptr<const void> selection()const{return current()?owner.retainSelection():std::shared_ptr<const void>();}
    bool setting(const char* name)const{return gSavedSettings.getBOOL(name);}
    void setting(const char* name,bool value)const{gSavedSettings.setBOOL(name,value);}
    bool allowSelectAvatar()const{return owner.allowSelectAvatar();}
    void forceSelection(bool value)const{owner.setForceSelection(value);}
    bool scriptIcon(const LLPickInfo& pick)const
    {
        if(!current() || !pick.mHUDIcon)return false;
        const auto id=pick.mHUDIcon->getSourceID();
        return current() && id.notNull();
    }
    void showScriptDebug(const LLPickInfo& pick)const
    {
        if(!current() || !pick.mHUDIcon)return;
        const auto id=pick.mHUDIcon->getSourceID();
        if(current() && id.notNull())LLFloaterScriptDebug::show(id);
    }
    bool selected(const Object& object)const{return owner.node(*object).has_value();}
    S32 faceCount(const Object& object)const{return S32(world.faceCount(*object).value_or(0));}
    bool containsAllFaces(const Object& object)const{return owner.contains(*object);}
    std::optional<bool> faceSelection(const Object& object,const LLPickInfo& pick)const
    {
        const auto count=world.faceCount(*object);
        if(!count || pick.mObjectFace<0 || U32(pick.mObjectFace)>=*count)return {};
        return owner.contains(*object,pick.mObjectFace);
    }
    bool hasMediaFocus()const{return !LLViewerMediaFocus::getInstance()->getFocusedObjectID().isNull();}
    void clearMediaFocus()const{LLViewerMediaFocus::getInstance()->clearFocus();}
    void deselectOnly(const Object& object)const{owner.deselectOnly(*object);}
    void deselectFamily(const Object& object)const{owner.deselectFamily(*object,true,true);}
    void deselectAll()const{owner.deselectAll();}
    void selectOnly(const Object& object,S32 face,S32 node,S32 primitive)const{owner.selectOnly(*object,face,node,primitive);}
    void selectFamily(const Object& object)const{owner.selectFamily(*object);}
    std::pair<F32,F32> hudZoom()const{return {gAgentCamera.mHUDTargetZoom,gAgentCamera.mHUDCurZoom};}
    void hudZoom(const std::pair<F32,F32>& value)const{gAgentCamera.mHUDTargetZoom=value.first;gAgentCamera.mHUDCurZoom=value.second;}
    S32 gltfNode(const LLPickInfo& pick)const{return pick.mGLTFNodeIndex;}
    S32 gltfPrimitive(const LLPickInfo& pick)const{return pick.mGLTFPrimitiveIndex;}
    bool focusOnAvatar()const{return gAgentCamera.getFocusOnAvatar();}
    bool selfAttachment(const Object& object)const{const auto root=world.rootObject(*object,false);return root && root->id==gAgent.getID();}
    bool self(const Object& object)const{return object->id==gAgent.getID();}
    void turnToSelection(const Object& object)const
    {
        if(!owner.boundingBox())return;
        const auto row=world.objectAdmission(*object);
        if(!row || row->pcode==LL_PCODE_LEGACY_AVATAR)return;
        LLVector3 selection_dir(owner.centerGlobal()-gAgent.getPositionGlobal());
        selection_dir.mV[VZ]=0.f;
        selection_dir.normVec();
        if(gAgent.getAtAxis()*selection_dir<.6f)
        {
            LLQuaternion target_rot;target_rot.shortestArc(LLVector3::x_axis,selection_dir);
            const auto selfAvatar=world.avatarForObject(gAgent.getID());
            if(!selfAvatar)return;
            gAgent.startAutoPilotGlobal(gAgent.getPositionGlobal(),"",&target_rot,NULL,NULL,MAX_FAR_CLIP,
                selfAvatar->sitting()?SELECTION_SITTING_ROTATION_TRESHOLD:SELECTION_ROTATION_TRESHOLD);
        }
    }
    void transientRootAndChildren(const Object& object)const
    {
        const auto root=world.rootObject(*object,true);
        if(!root)return;
        owner.setTransient(*root,true);
        if(!current())return;
        const auto children=world.directChildren(*root);
        if(children)for(const auto& child:*children)
        {
            if(!current())break;
            owner.setTransient(child,true);
        }
    }
};
}
std::shared_ptr<const void> LLToolSelect::handleNativeObjectSelection(const LLPickInfo& pick,bool ignore_group,bool temp_select,bool select_root)
{
    if((pick.mNativeRequestCurrent && !pick.mNativeRequestCurrent()) ||
       (pick.mNativeObjectCurrent && !pick.mNativeObjectCurrent()))return {};
    const auto session=VSNativeSession::active();
    if(!session || !gViewerWindow || !gViewerWindow->isNativeVulkan() || !gViewerWindow->nativeContext())return {};
    auto* owner=session->objectSelection();
    if(!owner)return {};
    const auto lifetime=session->retainObjectSelection();
    if(!lifetime)return {};
    const auto account=session->accountGuard();
    const auto current=[account,request=pick.mNativeRequestCurrent,source=pick.mNativeObjectCurrent]
    {return account() && (!request || request()) && (!source || source());};
    NativeSelectionPeer peer{session,lifetime,gViewerWindow->nativeContext()->world(),*owner,current};
    return vs_handle_object_selection(peer,pick,ignore_group,temp_select,select_root);
}

#endif
