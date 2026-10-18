/**
 * @file fsfloateravatarrendersettings.cpp
 * @brief Floater for showing persisted avatar render settings
 *
 * $LicenseInfo:firstyear=2016&license=fsviewerlgpl$
 * Phoenix Firestorm Viewer Source Code
 * Copyright (C) 2016, Ansariel Hiller
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
 * The Phoenix Firestorm Project, Inc., 1831 Oakwood Drive, Fairmont, Minnesota 56031-3225 USA
 * http://www.firestormviewer.org
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"
#include "vsnativeim.h"
#include "llviewerwindow.h"
#include "llmenugl.h"
#if VS_NATIVE_VULKAN
#include "vsnativesession.h"
#include "vsvulkancontext.h"
#include "vsworldscene.h"
#endif

#include "fsfloateravatarrendersettings.h"

#include "fsavatarrenderpersistence.h"
#include "fscommon.h"
#include "llfiltereditor.h"
#include "llfloateravatarpicker.h"
#include "llnamelistctrl.h"
#include "lltrans.h"
#include "llviewerobjectlist.h"
#include "llvoavatar.h"


FSFloaterAvatarRenderSettings::FSFloaterAvatarRenderSettings(const LLSD& key)
 : LLFloater(key),
 mAvatarList(nullptr),
 mRenderSettingChangedCallbackConnection(),
 mFilterSubString(LLStringUtil::null),
 mFilterSubStringOrig(LLStringUtil::null)
{
    mCommitCallbackRegistrar.add("Settings.AddNewEntry", boost::bind(&FSFloaterAvatarRenderSettings::onClickAdd, this, _2));
}

FSFloaterAvatarRenderSettings::~FSFloaterAvatarRenderSettings()
{
    removePicker();
    if (mRenderSettingChangedCallbackConnection.connected())
    {
        mRenderSettingChangedCallbackConnection.disconnect();
    }
}


void FSFloaterAvatarRenderSettings::addElementToList(const LLUUID& avatar_id, LLVOAvatar::VisualMuteSettings render_setting)
{
    static const std::string av_render_never = getString("av_render_never");
    static const std::string av_render_always = getString("av_render_always");
    static const std::string av_name_waiting = LLTrans::getString("AvatarNameWaiting");

    LLNameListCtrl::NameItem item_params;
    item_params.value = avatar_id;
    item_params.target = LLNameListCtrl::INDIVIDUAL;

    item_params.columns.add().column("name");
    item_params.name = av_name_waiting;

    std::string render_value = (render_setting == LLVOAvatar::AV_DO_NOT_RENDER ? av_render_never : av_render_always);
    item_params.columns.add().value(render_value).column("render_setting");

    mAvatarList->addNameItemRow(item_params);
}

bool FSFloaterAvatarRenderSettings::postBuild()
{
    mAvatarList = getChild<LLNameListCtrl>("avatar_list");
    mAvatarList->setContextMenu(&FSFloaterAvatarRenderPersistenceMenu::gFSAvatarRenderPersistenceMenu);
    mAvatarList->setFilterColumn(0);

    childSetAction("close_btn", boost::bind(&FSFloaterAvatarRenderSettings::onCloseBtn, this));

    getChild<LLFilterEditor>("filter_input")->setCommitCallback(boost::bind(&FSFloaterAvatarRenderSettings::onFilterEdit, this, _2));

    mRenderSettingChangedCallbackConnection = FSAvatarRenderPersistence::instance().setAvatarRenderSettingChangedCallback(boost::bind(&FSFloaterAvatarRenderSettings::onAvatarRenderSettingChanged, this, _1, _2));

    this->setVisibleCallback(boost::bind(&FSFloaterAvatarRenderSettings::removePicker, this));

    loadInitialList();

    return true;
}

void FSFloaterAvatarRenderSettings::removePicker()
{
    if (mPicker.get())
    {
        mPicker.get()->closeFloater();
    }
}

void FSFloaterAvatarRenderSettings::onCloseBtn()
{
    closeFloater();
}

void FSFloaterAvatarRenderSettings::loadInitialList()
{
    FSAvatarRenderPersistence::avatar_render_setting_t avatar_render_map = FSAvatarRenderPersistence::instance().getAvatarRenderMap();
    for (FSAvatarRenderPersistence::avatar_render_setting_t::iterator it = avatar_render_map.begin(); it != avatar_render_map.end(); ++it)
    {
        addElementToList(it->first, it->second);
    }
}

void FSFloaterAvatarRenderSettings::onAvatarRenderSettingChanged(const LLUUID& avatar_id, LLVOAvatar::VisualMuteSettings render_setting)
{
    mAvatarList->removeNameItem(avatar_id);
    if (render_setting != LLVOAvatar::AV_RENDER_NORMALLY)
    {
        addElementToList(avatar_id, render_setting);
    }
}

void FSFloaterAvatarRenderSettings::onFilterEdit(const std::string& search_string)
{
    mFilterSubStringOrig = search_string;
    LLStringUtil::trimHead(mFilterSubStringOrig);
    // Searches are case-insensitive
    std::string search_upper = mFilterSubStringOrig;
    LLStringUtil::toUpper(search_upper);

    if (mFilterSubString == search_upper)
    {
        return;
    }

    mFilterSubString = search_upper;

    // Apply new filter.
    mAvatarList->setFilterString(mFilterSubStringOrig);
}

bool FSFloaterAvatarRenderSettings::handleKeyHere(KEY key, MASK mask)
{
    if (FSCommon::isFilterEditorKeyCombo(key, mask))
    {
        getChild<LLFilterEditor>("filter_input")->setFocus(true);
        return true;
    }

    return LLFloater::handleKeyHere(key, mask);
}

void FSFloaterAvatarRenderSettings::onClickAdd(const LLSD& userdata)
{
    const std::string command_name = userdata.asString();
    LLVOAvatar::VisualMuteSettings render_setting = LLVOAvatar::AV_RENDER_NORMALLY;
    if ("never" == command_name)
    {
        render_setting = LLVOAvatar::AV_DO_NOT_RENDER;
    }
    else if ("always" == command_name)
    {
        render_setting = LLVOAvatar::AV_ALWAYS_RENDER;
    }

    LLView* button = findChild<LLButton>("plus_btn", true);
    LLFloater* root_floater = gFloaterView->getParentFloater(this);
    const LLHandle<LLFloater> weak=getHandle();const auto current=vs_native_im_guard();
    LLFloaterAvatarPicker* picker = LLFloaterAvatarPicker::show([weak,current,render_setting](const uuid_vec_t& ids,const std::vector<LLAvatarName>&)
    {
        if(current())if(auto* owner=dynamic_cast<FSFloaterAvatarRenderSettings*>(weak.get()))owner->callbackAvatarPicked(ids,render_setting);
    },true,true,true,root_floater?root_floater->getName():getName(),button);

    if (root_floater)
    {
        root_floater->addDependentFloater(picker);
    }

    mPicker = picker->getHandle();
}

void FSFloaterAvatarRenderSettings::callbackAvatarPicked(const uuid_vec_t& ids, LLVOAvatar::VisualMuteSettings render_setting)
{
    for (uuid_vec_t::const_iterator it = ids.begin(); it != ids.end(); ++it)
    {
        LLUUID avatar_id = *it;

        LLVOAvatar *avatarp = dynamic_cast<LLVOAvatar*>(gObjectList.findObject(avatar_id));
        if (avatarp)
        {
            avatarp->setVisualMuteSettings(render_setting);
        }
        else
        {
            FSAvatarRenderPersistence::instance().setAvatarRenderSettings(avatar_id, render_setting);
        }
    }
#if VS_NATIVE_VULKAN
    if(VSNativeSession::active() && gViewerWindow && gViewerWindow->nativeContext())gViewerWindow->nativeContext()->world().refreshAvatarRenderSettings();
#endif

}


//---------------------------------------------------------------------------
// Context menu
//---------------------------------------------------------------------------
namespace FSFloaterAvatarRenderPersistenceMenu
{

    FSAvatarRenderPersistenceMenu gFSAvatarRenderPersistenceMenu;

    LLContextMenu* FSAvatarRenderPersistenceMenu::createMenu()
    {
        // set up the callbacks for all of the avatar menu items
        LLUICtrl::CommitCallbackRegistry::ScopedRegistrar registrar;

        mIntent=std::make_shared<char>();const std::weak_ptr<const void> intent=mIntent;
        registrar.add("Avatar.ChangeRenderSetting", vs_native_im_lifetime_callback([intent]{return !intent.expired();},boost::bind(&FSAvatarRenderPersistenceMenu::changeRenderSetting, this, _2)));

        // create the context menu from the XUI
        return createFromFile("menu_fs_avatar_render_setting.xml");
    }

    void FSAvatarRenderPersistenceMenu::changeRenderSetting(const LLSD& param)
    {
        LLVOAvatar::VisualMuteSettings render_setting = (LLVOAvatar::VisualMuteSettings)param.asInteger();

        for (uuid_vec_t::iterator it = mUUIDs.begin(); it != mUUIDs.end(); ++it)
        {
            LLUUID avatar_id = *it;

            LLVOAvatar* avatar = dynamic_cast<LLVOAvatar*>(gObjectList.findObject(avatar_id));
            if (avatar)
            {
                // Set setting via the LLVOAvatar instance if it's available; will also call FSAvatarRenderPersistence::setAvatarRenderSettings()
                avatar->setVisualMuteSettings(render_setting);
            }
            else
            {
                FSAvatarRenderPersistence::instance().setAvatarRenderSettings(avatar_id, render_setting);
            }
        }

#if VS_NATIVE_VULKAN
        if(VSNativeSession::active())
        {
            if(gViewerWindow && gViewerWindow->nativeContext())gViewerWindow->nativeContext()->world().refreshAvatarRenderSettings();
        }
        else
#endif
        LLVOAvatar::cullAvatarsByPixelArea();
    }
}

void FSFloaterAvatarRenderPersistenceMenu::FSAvatarRenderPersistenceMenu::resetAccount()
{
    mIntent.reset();if(auto* menu=mMenuHandle.get())menu->die();mMenuHandle.markDead();mUUIDs.clear();
}
bool FSFloaterAvatarRenderSettings::isNativeCallback(std::string_view name,const LLSD& parameter)
{
    if(name=="Settings.AddNewEntry")return parameter.asString()=="never" || parameter.asString()=="always";
    if(name!="Avatar.ChangeRenderSetting")return false;
    const auto value=parameter.asInteger();return value>=LLVOAvatar::AV_RENDER_NORMALLY && value<=LLVOAvatar::AV_ALWAYS_RENDER;
}
