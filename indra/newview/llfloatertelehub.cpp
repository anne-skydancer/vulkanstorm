/**
 * @file llfloatertelehub.cpp
 * @author James Cook
 * @brief LLFloaterTelehub class implementation
 *
 * $LicenseInfo:firstyear=2005&license=viewerlgpl$
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

#include "llfloatertelehub.h"
#include "vsnativeim.h"
#include "vsnativesession.h"
#include "vsobjectselection.h"
#include "vsobjectcontext.h"
#include "vsregioncontext.h"
#include "llviewerwindow.h"
#include "vsvulkancontext.h"
#include <map>
#include <set>
#include <array>

#include "message.h"
#include "llfontgl.h"

#include "llagent.h"
#include "llfloaterreg.h"
#include "llfloatertools.h"
#include "llscrolllistctrl.h"
#include "llselectmgr.h"
#include "lltoolcomp.h"
#include "lltoolmgr.h"
#include "llviewerobject.h"
#include "llviewerobjectlist.h"
#include "lluictrlfactory.h"

namespace
{
bool nativeTelehub(){return gViewerWindow && gViewerWindow->isNativeVulkan();}
std::vector<VSObjectContext> telehubSelection()
{
    const auto session=VSNativeSession::active();
    if(!session || !session->regionReady() || !session->objectSelection())return {};
    std::vector<VSObjectContext> result;std::set<LLUUID> roots;
    for(const auto& selected:session->objectSelection()->selected())
    {
        const auto object=VSObjectContext::resolve(selected.id);
        if(!object || !object->current || !object->current())return {};
        const auto root=VSObjectContext::resolve(object->rootEditID);
        if(!root || !root->current || !root->current())return {};
        if(roots.insert(root->id).second)result.push_back(*root);
    }
    return result;
}
void sendTelehubRequest(const std::string& parameter)
{
    const auto session=VSNativeSession::active();
    if(!session || !session->regionReady())return;
    const auto selection=telehubSelection();
    std::map<LLHost,std::vector<U32>> peers;
    for(const auto& object:selection)peers[object.host].push_back(object.localID);
    if(peers.empty())peers[session->host()]={};
    for(const auto& peer:peers)
    {
        LLMessageSystem* msg=gMessageSystem;
        auto head=[&]
        {
            msg->newMessage(gAgent.isGodlike()?"GodlikeMessage":"EstateOwnerMessage");
            msg->nextBlockFast(_PREHASH_AgentData);msg->addUUIDFast(_PREHASH_AgentID,gAgent.getID());
            msg->addUUIDFast(_PREHASH_SessionID,gAgent.getSessionID());msg->addUUID("TransactionID",LLUUID::null);
            msg->nextBlock("MethodData");msg->addString("Method","telehub");msg->addUUID("Invoice",LLUUID::null);
            if(!parameter.empty()){msg->nextBlock("ParamList");msg->addString("Parameter",parameter);}
        };
        head();
        for(std::size_t i=0;i<peer.second.size();++i)
        {
            msg->nextBlock("ParamList");msg->addString("Parameter",llformat("%u",peer.second[i]));
            if(msg->isSendFull("ParamList") && i+1<peer.second.size())
            {msg->sendReliable(peer.first);head();}
        }
        msg->sendReliable(peer.first);
    }
}
}

LLFloaterTelehub::LLFloaterTelehub(const LLSD& key)
:   LLFloater(key),
    mTelehubObjectID(),
    mTelehubObjectName(),
    mTelehubPos(),
    mTelehubRot(),
    mNumSpawn(0)
{
}

bool LLFloaterTelehub::postBuild()
{
    if(!nativeTelehub())gMessageSystem->setHandlerFunc("TelehubInfo", processTelehubInfo);

    getChild<LLUICtrl>("connect_btn")->setCommitCallback(boost::bind(&LLFloaterTelehub::onClickConnect, this));
    getChild<LLUICtrl>("disconnect_btn")->setCommitCallback(boost::bind(&LLFloaterTelehub::onClickDisconnect, this));
    getChild<LLUICtrl>("add_spawn_point_btn")->setCommitCallback(boost::bind(&LLFloaterTelehub::onClickAddSpawnPoint, this));
    getChild<LLUICtrl>("remove_spawn_point_btn")->setCommitCallback(boost::bind(&LLFloaterTelehub::onClickRemoveSpawnPoint, this));

    LLScrollListCtrl* list = getChild<LLScrollListCtrl>("spawn_points_list");
    if (list)
    {
        // otherwise you can't walk with arrow keys while floater is up
        list->setAllowKeyboardMovement(false);
    }

    return true;
}
void LLFloaterTelehub::onOpen(const LLSD& key)
{
    if(nativeTelehub())
    {
        const auto session=VSNativeSession::active();const auto region=vs_current_region_context();
        if(!session || !region)return;
        if(!mNativeCurrent || !mNativeCurrent())
        {mTelehubObjectID.setNull();mNumSpawn=0;getChild<LLScrollListCtrl>("spawn_points_list")->deleteAllItems();}
        const auto weak=getHandle();const auto account=vs_native_im_guard();
        mNativeCurrent=[weak,account,region]
        {
            const auto current=vs_current_region_context();
            return weak.get() && account() && region->current() && current && *current==*region;
        };
        mNativeRegionConnection=session->observeCurrentRegion(vs_native_im_account_callback([weak]
        {
            if(auto* floater=dynamic_cast<LLFloaterTelehub*>(weak.get()))floater->onOpen(LLSD());
        }));
        // The actual native selection owner is consumed below. The normal
        // translate/select mouse frontend remains a prerequisite for admission.
    }
    else
    {
        LLToolMgr::getInstance()->setCurrentToolset(gBasicToolset);
        LLToolMgr::getInstance()->getCurrentToolset()->selectTool(LLToolCompTranslate::getInstance());
        mObjectSelection=LLSelectMgr::getInstance()->getEditSelection();
    }
    sendTelehubInfoRequest();
}

LLFloaterTelehub::~LLFloaterTelehub()
{
    // no longer interested in this message
    if(!nativeTelehub())gMessageSystem->setHandlerFunc("TelehubInfo", NULL);
}

void LLFloaterTelehub::draw()
{
    if (!isMinimized())
    {
        refresh();
    }
    LLFloater::draw();
}

// Per-frame updates, because we don't have a selection manager observer.
void LLFloaterTelehub::refresh()
{
    bool have_selection=false,all_volume=false;
    if(nativeTelehub())
    {
        const auto selected=telehubSelection();
        have_selection=mNativeCurrent && mNativeCurrent() && !selected.empty();
        all_volume=have_selection && std::all_of(selected.begin(),selected.end(),[](const auto& object){return object.pcode==LL_PCODE_VOLUME;});
    }
    else
    {
        constexpr bool children_ok=true;
        have_selection=mObjectSelection->getFirstRootObject(children_ok)!=nullptr;
        all_volume=LLSelectMgr::getInstance()->selectionAllPCode(LL_PCODE_VOLUME);
    }
    getChildView("connect_btn")->setEnabled(have_selection && all_volume);

    bool have_telehub=mTelehubObjectID.notNull() && (!nativeTelehub() || (mNativeCurrent && mNativeCurrent()));
    getChildView("disconnect_btn")->setEnabled(have_telehub);

    bool space_avail = (mNumSpawn < MAX_SPAWNPOINTS_PER_TELEHUB);
    getChildView("add_spawn_point_btn")->setEnabled(have_selection && all_volume && space_avail);

    LLScrollListCtrl* list = getChild<LLScrollListCtrl>("spawn_points_list");
    if (list)
    {
        bool enable_remove = (list->getFirstSelected() != NULL);
        getChildView("remove_spawn_point_btn")->setEnabled(enable_remove);
    }
}

// static
bool LLFloaterTelehub::renderBeacons()
{
    // only render if we've got a telehub
    LLFloaterTelehub* floater = LLFloaterReg::findTypedInstance<LLFloaterTelehub>("telehubs");
    return floater && floater->mTelehubObjectID.notNull() && (!nativeTelehub() || (floater->mNativeCurrent && floater->mNativeCurrent()));
}

// static
void LLFloaterTelehub::addBeacons()
{
    LLFloaterTelehub* floater = LLFloaterReg::findTypedInstance<LLFloaterTelehub>("telehubs");
    if (!floater)
        return;

    // Find the telehub position, either our cached old position, or
    // an updated one based on the actual object position.
    LLVector3 hub_pos_region = floater->mTelehubPos;
    LLQuaternion hub_rot = floater->mTelehubRot;
    if(nativeTelehub())
    {
        if(!floater->mNativeCurrent || !floater->mNativeCurrent())return;
        auto& scene=gViewerWindow->nativeContext()->world();
        const auto object=scene.object(floater->mTelehubObjectID);
        const auto transform=object?scene.objectTransform(*object):std::nullopt;
        const auto region=vs_current_region_context();
        if(transform && region){hub_pos_region=region->getPosRegionFromGlobal(transform->position);hub_rot=transform->rotation;}
    }
    else if(auto* obj=gObjectList.findObject(floater->mTelehubObjectID))
    {hub_pos_region=obj->getPositionRegion();hub_rot=obj->getRotationRegion();}
    // Draw nice thick 3-pixel lines.
    gObjectList.addDebugBeacon(hub_pos_region, "", LLColor4::yellow, LLColor4::white, 4);

    LLScrollListCtrl* list = floater->getChild<LLScrollListCtrl>("spawn_points_list");
    if (list)
    {
        S32 spawn_index = list->getFirstSelectedIndex();
        if (spawn_index >= 0)
        {
            LLVector3 spawn_pos = hub_pos_region  + (floater->mSpawnPointPos[spawn_index] * hub_rot);
            gObjectList.addDebugBeacon(spawn_pos, "", LLColor4::orange, LLColor4::white, 4);
        }
    }
}

void LLFloaterTelehub::sendTelehubInfoRequest()
{
    if(nativeTelehub())
    {
        if(mNativeCurrent && mNativeCurrent())sendTelehubRequest("info ui");
        return;
    }
    LLSelectMgr::getInstance()->sendGodlikeRequest("telehub", "info ui");
}

void LLFloaterTelehub::onClickConnect()
{
    if(nativeTelehub())
    {
        if(mNativeCurrent && mNativeCurrent())sendTelehubRequest("connect");
        return;
    }
    LLSelectMgr::getInstance()->sendGodlikeRequest("telehub", "connect");
}

void LLFloaterTelehub::onClickDisconnect()
{
    if(nativeTelehub())
    {
        if(mNativeCurrent && mNativeCurrent())sendTelehubRequest("delete");
        return;
    }
    LLSelectMgr::getInstance()->sendGodlikeRequest("telehub", "delete");
}

void LLFloaterTelehub::onClickAddSpawnPoint()
{
    if(nativeTelehub())
    {
        if(mNativeCurrent && mNativeCurrent())sendTelehubRequest("spawnpoint add");
        if(const auto session=VSNativeSession::active();session && session->objectSelection())session->objectSelection()->select({});
        return;
    }
    LLSelectMgr::getInstance()->sendGodlikeRequest("telehub", "spawnpoint add");
    LLSelectMgr::getInstance()->deselectAll();
}

void LLFloaterTelehub::onClickRemoveSpawnPoint()
{
    if(nativeTelehub() && (!mNativeCurrent || !mNativeCurrent()))return;
    LLScrollListCtrl* list = getChild<LLScrollListCtrl>("spawn_points_list");
    if (!list)
        return;

    S32 spawn_index = list->getFirstSelectedIndex();
    if (spawn_index < 0) return;  // nothing selected

    LLMessageSystem* msg = gMessageSystem;

    // Could be god or estate owner.  If neither, server will reject message.
    if (gAgent.isGodlike())
    {
        msg->newMessage("GodlikeMessage");
    }
    else
    {
        msg->newMessage("EstateOwnerMessage");
    }
    msg->nextBlock("AgentData");
    msg->addUUID("AgentID", gAgent.getID());
    msg->addUUID("SessionID", gAgent.getSessionID());
    msg->addUUIDFast(_PREHASH_TransactionID, LLUUID::null); //not used
    msg->nextBlock("MethodData");
    msg->addString("Method", "telehub");
    msg->addUUID("Invoice", LLUUID::null);

    msg->nextBlock("ParamList");
    msg->addString("Parameter", "spawnpoint remove");

    std::string buffer;
    buffer = llformat("%d", spawn_index);
    msg->nextBlock("ParamList");
    msg->addString("Parameter", buffer);

    gAgent.sendReliableMessage();
}

// static
void LLFloaterTelehub::processTelehubInfo(LLMessageSystem* msg, void**)
{
    LLFloaterTelehub* floater = LLFloaterReg::findTypedInstance<LLFloaterTelehub>("telehubs");
    if (floater && (!nativeTelehub() || (floater->mNativeCurrent && floater->mNativeCurrent())))
    {
        floater->unpackTelehubInfo(msg);
    }
}

void LLFloaterTelehub::unpackTelehubInfo(LLMessageSystem* msg)
{
    const S32 count=msg->getNumberOfBlocks("SpawnPointBlock");
    if(count<0 || count>MAX_SPAWNPOINTS_PER_TELEHUB)return;
    LLUUID object;std::string name;LLVector3 position;LLQuaternion rotation;
    std::array<LLVector3,MAX_SPAWNPOINTS_PER_TELEHUB> spawns;
    msg->getUUID("TelehubBlock","ObjectID",object);msg->getString("TelehubBlock","ObjectName",name);
    msg->getVector3("TelehubBlock","TelehubPos",position);msg->getQuat("TelehubBlock","TelehubRot",rotation);
    if(!position.isFinite() || !rotation.isFinite())return;
    for(S32 i=0;i<count;++i)
    {
        msg->getVector3("SpawnPointBlock","SpawnPointPos",spawns[i],i);
        if(!spawns[i].isFinite())return;
    }
    mTelehubObjectID=object;mTelehubObjectName=std::move(name);mTelehubPos=position;mTelehubRot=rotation;mNumSpawn=count;
    std::copy(spawns.begin(),spawns.begin()+count,mSpawnPointPos);

    // Update parts of the UI that change only when message received.

    if (mTelehubObjectID.isNull())
    {
        getChildView("status_text_connected")->setVisible( false);
        getChildView("status_text_not_connected")->setVisible( true);
        getChildView("help_text_connected")->setVisible( false);
        getChildView("help_text_not_connected")->setVisible( true);
    }
    else
    {
        getChild<LLUICtrl>("status_text_connected")->setTextArg("[OBJECT]", mTelehubObjectName);
        getChildView("status_text_connected")->setVisible( true);
        getChildView("status_text_not_connected")->setVisible( false);
        getChildView("help_text_connected")->setVisible( true);
        getChildView("help_text_not_connected")->setVisible( false);
    }

    LLScrollListCtrl* list = getChild<LLScrollListCtrl>("spawn_points_list");
    if (list)
    {
        list->deleteAllItems();
        for (S32 i = 0; i < mNumSpawn; i++)
        {
            std::string pos = llformat("%.1f, %.1f, %.1f",
                                    mSpawnPointPos[i].mV[VX],
                                    mSpawnPointPos[i].mV[VY],
                                    mSpawnPointPos[i].mV[VZ]);
            list->addSimpleElement(pos);
        }
        list->selectNthItem(mNumSpawn - 1);
    }
}
