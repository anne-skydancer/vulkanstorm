/**
 * @file llaisapi.cpp
 * @brief classes and functions for interfacing with the v3+ ais inventory service.
 *
 * $LicenseInfo:firstyear=2013&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2013, Linden Research, Inc.
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
 *
 */

#include "llviewerprecompiledheaders.h"
#include "llaisapi.h"
#include "llaisreply.h" // <Vulkanstorm/>
#include "llinventory_vks.h" // <Vulkanstorm/>

#include "llagent.h"
#include "llappviewer.h"
#include "llcallbacklist.h"
#include "llinventorymodel.h"
#include "llinventoryobserver.h"
#include "llnotificationsutil.h"
#include "llsdutil.h"
#include "llviewerregion.h"
#include "llvoavatar.h"
#include "llvoavatarself.h"
#include "llviewercontrol.h"

#include "llviewernetwork.h"

///----------------------------------------------------------------------------
/// Classes for AISv3 support.
///----------------------------------------------------------------------------

//=========================================================================
const std::string AISAPI::INVENTORY_CAP_NAME("InventoryAPIv3");
const std::string AISAPI::LIBRARY_CAP_NAME("LibraryAPIv3");
const S32 AISAPI::HTTP_TIMEOUT = 180;

std::list<AISAPI::ais_query_item_t> AISAPI::sPostponedQuery;

const S32 MAX_SIMULTANEOUS_COROUTINES = 2048;

// AIS3 allows '*' requests, but in reality those will be cut at some point
// Specify own depth to be able to anticipate it and mark folders as incomplete
const S32 MAX_FOLDER_DEPTH_REQUEST = 50;


//-------------------------------------------------------------------------
/*static*/
bool AISAPI::isAvailable()
{
    // <FS:Ansariel> Add AIS3 debug setting
    //if (gAgent.getRegion())
    if (gAgent.getRegion() && (gSavedSettings.getBOOL("FSUseAis3Api") || LLGridManager::instance().isInSecondLife()))
    // </FS:Ansariel>
    {
        return gAgent.getRegion()->isCapabilityAvailable(INVENTORY_CAP_NAME);
    }
    return false;
}

/*static*/
void AISAPI::getCapNames(LLSD& capNames)
{
    capNames.append(INVENTORY_CAP_NAME);
    capNames.append(LIBRARY_CAP_NAME);
}

/*static*/
std::string AISAPI::getInvCap()
{
    if (gAgent.getRegion())
    {
        return gAgent.getRegion()->getCapability(INVENTORY_CAP_NAME);
    }
    return std::string();
}

/*static*/
std::string AISAPI::getLibCap()
{
    if (gAgent.getRegion())
    {
        return gAgent.getRegion()->getCapability(LIBRARY_CAP_NAME);
    }
    return std::string();
}

/*static*/
void AISAPI::CreateInventory(const LLUUID& parentId, const LLSD& newInventory, completion_t callback)
{
    std::string cap = getInvCap();
    if (cap.empty())
    {
        LL_WARNS("Inventory") << "Inventory cap not found!" << LL_ENDL;
        if (callback)
        {
            callback(LLUUID::null);
        }
        return;
    }

    LLUUID tid;
    tid.generate();

    std::string url = cap + std::string("/category/") + parentId.asString() + "?tid=" + tid.asString();
    LL_DEBUGS("Inventory") << "url: " << url << " parentID " << parentId << " newInventory " << newInventory << LL_ENDL;

    // I may be suffering from golden hammer here, but the first part of this bind
    // is actually a static cast for &HttpCoroutineAdapter::postAndSuspend so that
    // the compiler can identify the correct signature to select.
    //
    // Reads as follows:
    // LLSD     - method returning LLSD
    // (LLCoreHttpUtil::HttpCoroutineAdapter::*) - pointer to member function of HttpCoroutineAdapter
    // (LLCore::HttpRequest::ptr_t, const std::string &, const LLSD &, LLCore::HttpOptions::ptr_t, LLCore::HttpHeaders::ptr_t) - signature of method
    //
    invokationFn_t postFn = boost::bind(
        // Humans ignore next line.  It is just a cast.
        static_cast<LLSD (LLCoreHttpUtil::HttpCoroutineAdapter::*)(LLCore::HttpRequest::ptr_t, const std::string &, const LLSD &, LLCore::HttpOptions::ptr_t, LLCore::HttpHeaders::ptr_t)>
        //----
        // _1 -> httpAdapter
        // _2 -> httpRequest
        // _3 -> url
        // _4 -> body
        // _5 -> httpOptions
        // _6 -> httpHeaders
        (&LLCoreHttpUtil::HttpCoroutineAdapter::postAndSuspend), _1, _2, _3, _4, _5, _6);

    LLCoprocedureManager::CoProcedure_t proc(boost::bind(&AISAPI::InvokeAISCommandCoro,
        _1, postFn, url, parentId, newInventory, callback, CREATEINVENTORY));
    EnqueueAISCommand("CreateInventory", proc);
}

/*static*/
void AISAPI::SlamFolder(const LLUUID& folderId, const LLSD& newInventory, completion_t callback)
{
    std::string cap = getInvCap();
    if (cap.empty())
    {
        LL_WARNS("Inventory") << "Inventory cap not found!" << LL_ENDL;
        if (callback)
        {
            callback(LLUUID::null);
        }
        return;
    }

    LLUUID tid;
    tid.generate();

    std::string url = cap + std::string("/category/") + folderId.asString() + "/links?tid=" + tid.asString();

    // see comment above in CreateInventoryCommand
    invokationFn_t putFn = boost::bind(
        // Humans ignore next line.  It is just a cast to specify which LLCoreHttpUtil::HttpCoroutineAdapter routine overload.
        static_cast<LLSD(LLCoreHttpUtil::HttpCoroutineAdapter::*)(LLCore::HttpRequest::ptr_t, const std::string &, const LLSD &, LLCore::HttpOptions::ptr_t, LLCore::HttpHeaders::ptr_t)>
        //----
        // _1 -> httpAdapter
        // _2 -> httpRequest
        // _3 -> url
        // _4 -> body
        // _5 -> httpOptions
        // _6 -> httpHeaders
        (&LLCoreHttpUtil::HttpCoroutineAdapter::putAndSuspend), _1, _2, _3, _4, _5, _6);

    LLCoprocedureManager::CoProcedure_t proc(boost::bind(&AISAPI::InvokeAISCommandCoro,
        _1, putFn, url, folderId, newInventory, callback, SLAMFOLDER));

    EnqueueAISCommand("SlamFolder", proc);
}

void AISAPI::RemoveCategory(const LLUUID &categoryId, completion_t callback)
{
    std::string cap;

    cap = getInvCap();
    if (cap.empty())
    {
        LL_WARNS("Inventory") << "Inventory cap not found!" << LL_ENDL;
        if (callback)
        {
            callback(LLUUID::null);
        }
        return;
    }

    std::string url = cap + std::string("/category/") + categoryId.asString();
    LL_DEBUGS("Inventory") << "url: " << url << LL_ENDL;

    invokationFn_t delFn = boost::bind(
        // Humans ignore next line.  It is just a cast to specify which LLCoreHttpUtil::HttpCoroutineAdapter routine overload.
        static_cast<LLSD(LLCoreHttpUtil::HttpCoroutineAdapter::*)(LLCore::HttpRequest::ptr_t, const std::string &, LLCore::HttpOptions::ptr_t, LLCore::HttpHeaders::ptr_t)>
        //----
        // _1 -> httpAdapter
        // _2 -> httpRequest
        // _3 -> url
        // _4 -> body
        // _5 -> httpOptions
        // _6 -> httpHeaders
        (&LLCoreHttpUtil::HttpCoroutineAdapter::deleteAndSuspend), _1, _2, _3, _5, _6);

    LLCoprocedureManager::CoProcedure_t proc(boost::bind(&AISAPI::InvokeAISCommandCoro,
        _1, delFn, url, categoryId, LLSD(), callback, REMOVECATEGORY));

    EnqueueAISCommand("RemoveCategory", proc);
}

/*static*/
void AISAPI::RemoveItem(const LLUUID &itemId, completion_t callback)
{
    std::string cap;

    cap = getInvCap();
    if (cap.empty())
    {
        LL_WARNS("Inventory") << "Inventory cap not found!" << LL_ENDL;
        if (callback)
        {
            callback(LLUUID::null);
        }
        return;
    }

    std::string url = cap + std::string("/item/") + itemId.asString();
    LL_DEBUGS("Inventory") << "url: " << url << LL_ENDL;

    invokationFn_t delFn = boost::bind(
        // Humans ignore next line.  It is just a cast to specify which LLCoreHttpUtil::HttpCoroutineAdapter routine overload.
        static_cast<LLSD(LLCoreHttpUtil::HttpCoroutineAdapter::*)(LLCore::HttpRequest::ptr_t, const std::string &, LLCore::HttpOptions::ptr_t, LLCore::HttpHeaders::ptr_t)>
        //----
        // _1 -> httpAdapter
        // _2 -> httpRequest
        // _3 -> url
        // _4 -> body
        // _5 -> httpOptions
        // _6 -> httpHeaders
        (&LLCoreHttpUtil::HttpCoroutineAdapter::deleteAndSuspend), _1, _2, _3, _5, _6);

    LLCoprocedureManager::CoProcedure_t proc(boost::bind(&AISAPI::InvokeAISCommandCoro,
        _1, delFn, url, itemId, LLSD(), callback, REMOVEITEM));

    EnqueueAISCommand("RemoveItem", proc);
}

void AISAPI::CopyLibraryCategory(const LLUUID& sourceId, const LLUUID& destId, bool copySubfolders, completion_t callback)
{
    std::string cap;

    cap = getLibCap();
    if (cap.empty())
    {
        LL_WARNS("Inventory") << "Library cap not found!" << LL_ENDL;
        if (callback)
        {
            callback(LLUUID::null);
        }
        return;
    }

    LL_DEBUGS("Inventory") << "Copying library category: " << sourceId << " => " << destId << LL_ENDL;

    LLUUID tid;
    tid.generate();

    std::string url = cap + std::string("/category/") + sourceId.asString() + "?tid=" + tid.asString();
    if (!copySubfolders)
    {
        url += ",depth=0";
    }
    LL_INFOS() << url << LL_ENDL;

    std::string destination = destId.asString();

    invokationFn_t copyFn = boost::bind(
        // Humans ignore next line.  It is just a cast to specify which LLCoreHttpUtil::HttpCoroutineAdapter routine overload.
        static_cast<LLSD(LLCoreHttpUtil::HttpCoroutineAdapter::*)(LLCore::HttpRequest::ptr_t, const std::string &, const std::string, LLCore::HttpOptions::ptr_t, LLCore::HttpHeaders::ptr_t)>
        //----
        // _1 -> httpAdapter
        // _2 -> httpRequest
        // _3 -> url
        // _4 -> body
        // _5 -> httpOptions
        // _6 -> httpHeaders
        (&LLCoreHttpUtil::HttpCoroutineAdapter::copyAndSuspend), _1, _2, _3, destination, _5, _6);

    LLCoprocedureManager::CoProcedure_t proc(boost::bind(&AISAPI::InvokeAISCommandCoro,
        _1, copyFn, url, destId, LLSD(), callback, COPYLIBRARYCATEGORY));

    EnqueueAISCommand("CopyLibraryCategory", proc);
}

/*static*/
void AISAPI::PurgeDescendents(const LLUUID &categoryId, completion_t callback)
{
    std::string cap;

    cap = getInvCap();
    if (cap.empty())
    {
        LL_WARNS("Inventory") << "Inventory cap not found!" << LL_ENDL;
        if (callback)
        {
            callback(LLUUID::null);
        }
        return;
    }

    std::string url = cap + std::string("/category/") + categoryId.asString() + "/children";
    LL_DEBUGS("Inventory") << "url: " << url << LL_ENDL;

    invokationFn_t delFn = boost::bind(
        // Humans ignore next line.  It is just a cast to specify which LLCoreHttpUtil::HttpCoroutineAdapter routine overload.
        static_cast<LLSD(LLCoreHttpUtil::HttpCoroutineAdapter::*)(LLCore::HttpRequest::ptr_t, const std::string &, LLCore::HttpOptions::ptr_t, LLCore::HttpHeaders::ptr_t)>
        //----
        // _1 -> httpAdapter
        // _2 -> httpRequest
        // _3 -> url
        // _4 -> body
        // _5 -> httpOptions
        // _6 -> httpHeaders
        (&LLCoreHttpUtil::HttpCoroutineAdapter::deleteAndSuspend), _1, _2, _3, _5, _6);

    LLCoprocedureManager::CoProcedure_t proc(boost::bind(&AISAPI::InvokeAISCommandCoro,
        _1, delFn, url, categoryId, LLSD(), callback, PURGEDESCENDENTS));

    EnqueueAISCommand("PurgeDescendents", proc);
}


/*static*/
void AISAPI::UpdateCategory(const LLUUID &categoryId, const LLSD &updates, completion_t callback)
{
    std::string cap;

    cap = getInvCap();
    if (cap.empty())
    {
        LL_WARNS("Inventory") << "Inventory cap not found!" << LL_ENDL;
        if (callback)
        {
            callback(LLUUID::null);
        }
        return;
    }
    std::string url = cap + std::string("/category/") + categoryId.asString();

    invokationFn_t patchFn = boost::bind(
        // Humans ignore next line.  It is just a cast to specify which LLCoreHttpUtil::HttpCoroutineAdapter routine overload.
        static_cast<LLSD(LLCoreHttpUtil::HttpCoroutineAdapter::*)(LLCore::HttpRequest::ptr_t, const std::string &, const LLSD &, LLCore::HttpOptions::ptr_t, LLCore::HttpHeaders::ptr_t)>
        //----
        // _1 -> httpAdapter
        // _2 -> httpRequest
        // _3 -> url
        // _4 -> body
        // _5 -> httpOptions
        // _6 -> httpHeaders
        (&LLCoreHttpUtil::HttpCoroutineAdapter::patchAndSuspend), _1, _2, _3, _4, _5, _6);

    LLCoprocedureManager::CoProcedure_t proc(boost::bind(&AISAPI::InvokeAISCommandCoro,
        _1, patchFn, url, categoryId, updates, callback, UPDATECATEGORY));

    EnqueueAISCommand("UpdateCategory", proc);
}

/*static*/
void AISAPI::UpdateItem(const LLUUID &itemId, const LLSD &updates, completion_t callback)
{

    std::string cap;

    cap = getInvCap();
    if (cap.empty())
    {
        LL_WARNS("Inventory") << "Inventory cap not found!" << LL_ENDL;
        if (callback)
        {
            callback(LLUUID::null);
        }
        return;
    }
    std::string url = cap + std::string("/item/") + itemId.asString();

    invokationFn_t patchFn = boost::bind(
        // Humans ignore next line.  It is just a cast to specify which LLCoreHttpUtil::HttpCoroutineAdapter routine overload.
        static_cast<LLSD(LLCoreHttpUtil::HttpCoroutineAdapter::*)(LLCore::HttpRequest::ptr_t, const std::string &, const LLSD &, LLCore::HttpOptions::ptr_t, LLCore::HttpHeaders::ptr_t)>
        //----
        // _1 -> httpAdapter
        // _2 -> httpRequest
        // _3 -> url
        // _4 -> body
        // _5 -> httpOptions
        // _6 -> httpHeaders
        (&LLCoreHttpUtil::HttpCoroutineAdapter::patchAndSuspend), _1, _2, _3, _4, _5, _6);

    LLCoprocedureManager::CoProcedure_t proc(boost::bind(&AISAPI::InvokeAISCommandCoro,
        _1, patchFn, url, itemId, updates, callback, UPDATEITEM));

    EnqueueAISCommand("UpdateItem", proc);
}

/*static*/
void AISAPI::FetchItem(const LLUUID &itemId, ITEM_TYPE type, completion_t callback)
{
    std::string cap;

    cap = (type == INVENTORY) ? getInvCap() : getLibCap();
    if (cap.empty())
    {
        LL_WARNS("Inventory") << "Inventory cap not found!" << LL_ENDL;
        if (callback)
        {
            callback(LLUUID::null);
        }
        return;
    }
    std::string url = cap + std::string("/item/") + itemId.asString();

    invokationFn_t getFn = boost::bind(
        // Humans ignore next line.  It is just a cast to specify which LLCoreHttpUtil::HttpCoroutineAdapter routine overload.
        static_cast<LLSD(LLCoreHttpUtil::HttpCoroutineAdapter::*)(LLCore::HttpRequest::ptr_t, const std::string &, LLCore::HttpOptions::ptr_t, LLCore::HttpHeaders::ptr_t)>
        //----
        // _1 -> httpAdapter
        // _2 -> httpRequest
        // _3 -> url
        // _4 -> body
        // _5 -> httpOptions
        // _6 -> httpHeaders
        (&LLCoreHttpUtil::HttpCoroutineAdapter::getAndSuspend), _1, _2, _3, _5, _6);

    LLCoprocedureManager::CoProcedure_t proc(boost::bind(&AISAPI::InvokeAISCommandCoro,
        _1, getFn, url, itemId, LLSD(), callback, FETCHITEM));

    EnqueueAISCommand("FetchItem", proc);
}

/*static*/
void AISAPI::FetchCategoryChildren(const LLUUID &catId, ITEM_TYPE type, bool recursive, completion_t callback, S32 depth)
{
    std::string cap;

    cap = (type == INVENTORY) ? getInvCap() : getLibCap();
    if (cap.empty())
    {
        LL_WARNS("Inventory") << "Inventory cap not found!" << LL_ENDL;
        if (callback)
        {
            callback(LLUUID::null);
        }
        return;
    }
    std::string url = cap + std::string("/category/") + catId.asString() + "/children";

    if (recursive)
    {
        // can specify depth=*, but server side is going to cap requests
        // and reject everything 'over the top',.
        depth = MAX_FOLDER_DEPTH_REQUEST;
    }
    else
    {
        depth = llmin(depth, MAX_FOLDER_DEPTH_REQUEST);
    }

    url += "?depth=" + std::to_string(depth);

    invokationFn_t getFn = boost::bind(
        // Humans ignore next line.  It is just a cast to specify which LLCoreHttpUtil::HttpCoroutineAdapter routine overload.
        static_cast<LLSD(LLCoreHttpUtil::HttpCoroutineAdapter::*)(LLCore::HttpRequest::ptr_t, const std::string &, LLCore::HttpOptions::ptr_t, LLCore::HttpHeaders::ptr_t)>
        //----
        // _1 -> httpAdapter
        // _2 -> httpRequest
        // _3 -> url
        // _4 -> body
        // _5 -> httpOptions
        // _6 -> httpHeaders
        (&LLCoreHttpUtil::HttpCoroutineAdapter::getAndSuspend), _1, _2, _3, _5, _6);

    // get doesn't use body, can pass additional data
    LLSD body;
    body["depth"] = depth;
    LLCoprocedureManager::CoProcedure_t proc(boost::bind(&AISAPI::InvokeAISCommandCoro,
        _1, getFn, url, catId, body, callback, FETCHCATEGORYCHILDREN));

    EnqueueAISCommand("FetchCategoryChildren", proc);
}

// some folders can be requested by name, like
// animatn | bodypart | clothing | current | favorite | gesture | inbox | landmark | lsltext
// lstndfnd | my_otfts | notecard | object | outbox | root | snapshot | sound | texture | trash
void AISAPI::FetchCategoryChildren(const std::string &identifier, bool recursive, completion_t callback, S32 depth)
{
    std::string cap;

    cap = getInvCap();
    if (cap.empty())
    {
        LL_WARNS("Inventory") << "Inventory cap not found!" << LL_ENDL;
        if (callback)
        {
            callback(LLUUID::null);
        }
        return;
    }
    std::string url = cap + std::string("/category/") + identifier + "/children";

    if (recursive)
    {
        // can specify depth=*, but server side is going to cap requests
        // and reject everything 'over the top',.
        depth = MAX_FOLDER_DEPTH_REQUEST;
    }
    else
    {
        depth = llmin(depth, MAX_FOLDER_DEPTH_REQUEST);
    }

    url += "?depth=" + std::to_string(depth);

    invokationFn_t getFn = boost::bind(
        // Humans ignore next line.  It is just a cast to specify which LLCoreHttpUtil::HttpCoroutineAdapter routine overload.
        static_cast<LLSD(LLCoreHttpUtil::HttpCoroutineAdapter::*)(LLCore::HttpRequest::ptr_t, const std::string &, LLCore::HttpOptions::ptr_t, LLCore::HttpHeaders::ptr_t)>
        //----
        // _1 -> httpAdapter
        // _2 -> httpRequest
        // _3 -> url
        // _4 -> body
        // _5 -> httpOptions
        // _6 -> httpHeaders
        (&LLCoreHttpUtil::HttpCoroutineAdapter::getAndSuspend), _1, _2, _3, _5, _6);

    // get doesn't use body, can pass additional data
    LLSD body;
    body["depth"] = depth;
    LLCoprocedureManager::CoProcedure_t proc(boost::bind(&AISAPI::InvokeAISCommandCoro,
        _1, getFn, url, LLUUID::null, body, callback, FETCHCATEGORYCHILDREN));

    EnqueueAISCommand("FetchCategoryChildren", proc);
}

/*static*/
void AISAPI::FetchCategoryCategories(const LLUUID &catId, ITEM_TYPE type, bool recursive, completion_t callback, S32 depth)
{
    std::string cap;

    cap = (type == INVENTORY) ? getInvCap() : getLibCap();
    if (cap.empty())
    {
        LL_WARNS("Inventory") << "Inventory cap not found!" << LL_ENDL;
        if (callback)
        {
            callback(LLUUID::null);
        }
        return;
    }
    std::string url = cap + std::string("/category/") + catId.asString() + "/categories";

    if (recursive)
    {
        // can specify depth=*, but server side is going to cap requests
        // and reject everything 'over the top',.
        depth = MAX_FOLDER_DEPTH_REQUEST;
    }
    else
    {
        depth = llmin(depth, MAX_FOLDER_DEPTH_REQUEST);
    }

    url += "?depth=" + std::to_string(depth);

    invokationFn_t getFn = boost::bind(
        // Humans ignore next line.  It is just a cast to specify which LLCoreHttpUtil::HttpCoroutineAdapter routine overload.
        static_cast<LLSD(LLCoreHttpUtil::HttpCoroutineAdapter::*)(LLCore::HttpRequest::ptr_t, const std::string &, LLCore::HttpOptions::ptr_t, LLCore::HttpHeaders::ptr_t)>
        //----
        // _1 -> httpAdapter
        // _2 -> httpRequest
        // _3 -> url
        // _4 -> body
        // _5 -> httpOptions
        // _6 -> httpHeaders
        (&LLCoreHttpUtil::HttpCoroutineAdapter::getAndSuspend), _1, _2, _3, _5, _6);

    // get doesn't use body, can pass additional data
    LLSD body;
    body["depth"] = depth;
    LLCoprocedureManager::CoProcedure_t proc(boost::bind(&AISAPI::InvokeAISCommandCoro,
        _1, getFn, url, catId, body, callback, FETCHCATEGORYCATEGORIES));

    EnqueueAISCommand("FetchCategoryCategories", proc);
}

void AISAPI::FetchCategorySubset(const LLUUID& catId,
                                   const uuid_vec_t specificChildren,
                                   ITEM_TYPE type,
                                   bool recursive,
                                   completion_t callback,
                                   S32 depth)
{
    std::string cap = (type == INVENTORY) ? getInvCap() : getLibCap();
    if (cap.empty())
    {
        LL_WARNS("Inventory") << "Inventory cap not found!" << LL_ENDL;
        if (callback)
        {
            callback(LLUUID::null);
        }
        return;
    }
    if (specificChildren.empty())
    {
        LL_WARNS("Inventory") << "Empty request!" << LL_ENDL;
        if (callback)
        {
            callback(LLUUID::null);
        }
        return;
    }
    // category/any_folder_id/children?depth=*&children=child_id1,child_id2,child_id3
    std::string url = cap + std::string("/category/") + catId.asString() + "/children";

    if (recursive)
    {
        depth = MAX_FOLDER_DEPTH_REQUEST;
    }
    else
    {
        depth = llmin(depth, MAX_FOLDER_DEPTH_REQUEST);
    }

    uuid_vec_t::const_iterator iter = specificChildren.begin();
    uuid_vec_t::const_iterator end = specificChildren.end();

    url += "?depth=" + std::to_string(depth) + "&children=" + iter->asString();
    iter++;

    while (iter != end)
    {
        url += "," + iter->asString();
        iter++;
    }

    const S32 MAX_URL_LENGH = 2000; // RFC documentation specifies a maximum length of 2048
    if (url.length() > MAX_URL_LENGH)
    {
        LL_WARNS("Inventory") << "Request url is too long, url: " << url << LL_ENDL;
    }

    invokationFn_t getFn = boost::bind(
        // Humans ignore next line.  It is just a cast to specify which LLCoreHttpUtil::HttpCoroutineAdapter routine overload.
        static_cast<LLSD(LLCoreHttpUtil::HttpCoroutineAdapter::*)(LLCore::HttpRequest::ptr_t, const std::string&, LLCore::HttpOptions::ptr_t, LLCore::HttpHeaders::ptr_t)>
        //----
        // _1 -> httpAdapter
        // _2 -> httpRequest
        // _3 -> url
        // _4 -> body
        // _5 -> httpOptions
        // _6 -> httpHeaders
        (&LLCoreHttpUtil::HttpCoroutineAdapter::getAndSuspend), _1, _2, _3, _5, _6);

    // get doesn't use body, can pass additional data
    LLSD body;
    body["depth"] = depth;
    LLCoprocedureManager::CoProcedure_t proc(boost::bind(&AISAPI::InvokeAISCommandCoro,
                                                         _1, getFn, url, catId, body, callback, FETCHCATEGORYSUBSET));

    EnqueueAISCommand("FetchCategorySubset", proc);
}

/*static*/
// Will get COF folder, links in it and items those links point to
void AISAPI::FetchCOF(completion_t callback)
{
    std::string cap = getInvCap();
    if (cap.empty())
    {
        LL_WARNS("Inventory") << "Inventory cap not found!" << LL_ENDL;
        if (callback)
        {
            callback(LLUUID::null);
        }
        return;
    }
    std::string url = cap + std::string("/category/current/links");

    invokationFn_t getFn = boost::bind(
        // Humans ignore next line.  It is just a cast to specify which LLCoreHttpUtil::HttpCoroutineAdapter routine overload.
        static_cast<LLSD(LLCoreHttpUtil::HttpCoroutineAdapter::*)(LLCore::HttpRequest::ptr_t, const std::string&, LLCore::HttpOptions::ptr_t, LLCore::HttpHeaders::ptr_t)>
        //----
        // _1 -> httpAdapter
        // _2 -> httpRequest
        // _3 -> url
        // _4 -> body
        // _5 -> httpOptions
        // _6 -> httpHeaders
        (&LLCoreHttpUtil::HttpCoroutineAdapter::getAndSuspend), _1, _2, _3, _5, _6);

    LLSD body;
    // Only cof folder will be full, but cof can contain an outfit
    // link with embedded outfit folder for request to parse
    body["depth"] = 0;
    LLCoprocedureManager::CoProcedure_t proc(boost::bind(&AISAPI::InvokeAISCommandCoro,
                                                         _1, getFn, url, LLUUID::null, body, callback, FETCHCOF));

    EnqueueAISCommand("FetchCOF", proc);
}

void AISAPI::FetchCategoryLinks(const LLUUID &catId, completion_t callback)
{
    std::string cap = getInvCap();
    if (cap.empty())
    {
        LL_WARNS("Inventory") << "Inventory cap not found!" << LL_ENDL;
        if (callback)
        {
            callback(LLUUID::null);
        }
        return;
    }
    std::string url = cap + std::string("/category/") + catId.asString() + "/links";

    invokationFn_t getFn = boost::bind(
        // Humans ignore next line.  It is just a cast to specify which LLCoreHttpUtil::HttpCoroutineAdapter routine overload.
        static_cast<LLSD (LLCoreHttpUtil::HttpCoroutineAdapter::*)(LLCore::HttpRequest::ptr_t, const std::string &,
                                                                   LLCore::HttpOptions::ptr_t, LLCore::HttpHeaders::ptr_t)>
        //----
        // _1 -> httpAdapter
        // _2 -> httpRequest
        // _3 -> url
        // _4 -> body
        // _5 -> httpOptions
        // _6 -> httpHeaders
        (&LLCoreHttpUtil::HttpCoroutineAdapter::getAndSuspend),
        _1, _2, _3, _5, _6);

    LLSD body;
    body["depth"] = 0;
    LLCoprocedureManager::CoProcedure_t proc(
        boost::bind(&AISAPI::InvokeAISCommandCoro, _1, getFn, url, LLUUID::null, body, callback, FETCHCATEGORYLINKS));

    EnqueueAISCommand("FetchCategoryLinks", proc);
}

/*static*/
void AISAPI::FetchOrphans(completion_t callback)
{
    std::string cap = getInvCap();
    if (cap.empty())
    {
        LL_WARNS("Inventory") << "Inventory cap not found!" << LL_ENDL;
        if (callback)
        {
            callback(LLUUID::null);
        }
        return;
    }
    std::string url = cap + std::string("/orphans");

    invokationFn_t getFn = boost::bind(
        // Humans ignore next line.  It is just a cast to specify which LLCoreHttpUtil::HttpCoroutineAdapter routine overload.
        static_cast<LLSD(LLCoreHttpUtil::HttpCoroutineAdapter::*)(LLCore::HttpRequest::ptr_t , const std::string& , LLCore::HttpOptions::ptr_t , LLCore::HttpHeaders::ptr_t)>
        //----
        // _1 -> httpAdapter
        // _2 -> httpRequest
        // _3 -> url
        // _4 -> body
        // _5 -> httpOptions
        // _6 -> httpHeaders
        (&LLCoreHttpUtil::HttpCoroutineAdapter::getAndSuspend) , _1 , _2 , _3 , _5 , _6);

    LLCoprocedureManager::CoProcedure_t proc(boost::bind(&AISAPI::InvokeAISCommandCoro ,
                                                         _1 , getFn , url , LLUUID::null , LLSD() , callback , FETCHORPHANS));

    EnqueueAISCommand("FetchOrphans" , proc);
}

/*static*/
void AISAPI::EnqueueAISCommand(const std::string &procName, LLCoprocedureManager::CoProcedure_t proc)
{
    LLCoprocedureManager &inst = LLCoprocedureManager::instance();
    auto pending_in_pool = inst.countPending("AIS");
    std::string procFullName = "AIS(" + procName + ")";
    if (pending_in_pool < MAX_SIMULTANEOUS_COROUTINES)
    {
        inst.enqueueCoprocedure("AIS", procFullName, proc);
    }
    else
    {
        // As I understand it, coroutines have built-in 'pending' pool
        // but unfortunately it has limited size which inventory often goes over
        // so this is a workaround to not overfill it.
        if (sPostponedQuery.empty())
        {
            sPostponedQuery.push_back(ais_query_item_t(procFullName, proc));
            gIdleCallbacks.addFunction(onIdle, NULL);
        }
        else
        {
            sPostponedQuery.push_back(ais_query_item_t(procFullName, proc));
        }
    }
}

/*static*/
void AISAPI::onIdle(void *userdata)
{
    if (!sPostponedQuery.empty())
    {
        LLCoprocedureManager &inst = LLCoprocedureManager::instance();
        auto pending_in_pool = inst.countPending("AIS");
        while (pending_in_pool < MAX_SIMULTANEOUS_COROUTINES && !sPostponedQuery.empty())
        {
            ais_query_item_t &item = sPostponedQuery.front();
            inst.enqueueCoprocedure("AIS", item.first, item.second);
            sPostponedQuery.pop_front();
            pending_in_pool++;
        }
    }

    if (sPostponedQuery.empty())
    {
        // Nothing to do anymore
        gIdleCallbacks.deleteFunction(onIdle, NULL);
    }
}

// <Vulkanstorm> Inventory rung R3a: AIS replies are Rust values (llaisreply.h),
// and the decisions of the coroutine and of AISUpdate's parse are
// vkstorm-inventory's (vks_inv_ais_*). They reach the inventory model through
// this host table, one C++ call per call, in the C++ order. A host function
// must not throw into Rust: it keeps the first exception (LLCoros::Stopping
// at a checkpoint, say), answers "not found" / stop from then on, and the
// caller rethrows it once the Rust call returned.
struct AISUpdateHost
{
    explicit AISUpdateHost(AISUpdate* update) : mUpdate(update)
    {
        mTable.ctx = this;
        mTable.get_category = &getCategory;
        mTable.get_item = &getItem;
        mTable.get_version = &getVersion;
        mTable.set_version = &setVersion;
        mTable.fetch = &fetch;
        mTable.fetch_descendents_of = &fetchDescendentsOf;
        mTable.on_object_deleted_from_server = &onObjectDeletedFromServer;
        mTable.notify = &notify;
        mTable.checkpoint = &checkpoint;
    }

    const VksInvAisHost* table() const { return &mTable; }

    void rethrow()
    {
        if (mError)
        {
            std::exception_ptr error = mError;
            mError = nullptr;
            std::rethrow_exception(error);
        }
    }

    template <typename R, typename F>
    static R guarded(void* ctx, R fail, F&& f)
    {
        AISUpdateHost* host = static_cast<AISUpdateHost*>(ctx);
        if (host->mError)
        {
            return fail;
        }
        try
        {
            return f(host);
        }
        catch (...)
        {
            host->mError = std::current_exception();
            return fail;
        }
    }

    static LLViewerInventoryCategory* category(uint64_t handle)
    {
        return reinterpret_cast<LLViewerInventoryCategory*>(static_cast<uintptr_t>(handle));
    }

    static bool getCategory(void* ctx, const uint8_t* id, VksInvAisCategory* out, uint64_t* handle)
    {
        return guarded(ctx, false, [&](AISUpdateHost*)
        {
            LLViewerInventoryCategory* cat = gInventory.getCategory(vks_uuid(id));
            if (!cat)
            {
                return false;
            }
            // The fields as the copy constructor takes them (the version
            // comes from getVersion(), which the parse calls itself).
            cat->toVks(out->category);
            vks_set_uuid(out->category.owner, cat->getOwnerID());
            out->descendents = cat->getDescendentCount();
            out->has_requested = true;
            const LLFrameTimer& timer = cat->getFetchRequestTimer();
            static_assert(sizeof(LLFrameTimer) == sizeof(VksInvAisFetchTimer) &&
                              std::is_trivially_copyable<LLFrameTimer>::value,
                          "LLFrameTimer is {F64 start, F64 expiry, bool started}");
            memcpy(&out->requested, &timer, sizeof(timer));
            *handle = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(cat));
            return true;
        });
    }

    static bool getItem(void* ctx, const uint8_t* id, VksInvAisItem* out)
    {
        return guarded(ctx, false, [&](AISUpdateHost*)
        {
            LLViewerInventoryItem* item = gInventory.getItem(vks_uuid(id));
            if (!item)
            {
                return false;
            }
            // The fields as copyViewerItem takes them.
            item->toVks(out->item);
            out->complete = item->isFinished();
            vks_set_uuid(out->transaction_id, item->getTransactionID());
            out->running = item->getIsRunning();
            out->faulted = item->getIsFaulted();
            return true;
        });
    }

    static int32_t getVersion(void* ctx, uint64_t handle)
    {
        return guarded(ctx, (int32_t)LLViewerInventoryCategory::VERSION_UNKNOWN,
                       [&](AISUpdateHost*) { return (int32_t)category(handle)->getVersion(); });
    }

    static void setVersion(void* ctx, uint64_t handle, int32_t version)
    {
        guarded(ctx, 0, [&](AISUpdateHost*)
        {
            category(handle)->setVersion(version);
            return 0;
        });
    }

    static bool fetch(void* ctx, uint64_t handle, int32_t expiry_seconds)
    {
        return guarded(ctx, false, [&](AISUpdateHost*)
        {
            LLViewerInventoryCategory* cat = category(handle);
            LL_WARNS("Inventory") << "Got stale folder, refetching '" << cat->getName() << "' " << cat->getUUID() << LL_ENDL;
            return cat->fetch(expiry_seconds);
        });
    }

    static bool fetchDescendentsOf(void* ctx, const uint8_t* folder_id)
    {
        return guarded(ctx, false, [&](AISUpdateHost*)
        {
            LL_WARNS("Inventory") << "Purge failed: the object no longer exists at the server; refetching "
                                  << vks_uuid(folder_id) << LL_ENDL;
            return gInventory.fetchDescendentsOf(vks_uuid(folder_id));
        });
    }

    static void onObjectDeletedFromServer(void* ctx, const uint8_t* id, bool fix_broken_links,
                                          bool update_parent_version, bool do_notify_observers)
    {
        guarded(ctx, 0, [&](AISUpdateHost*)
        {
            gInventory.onObjectDeletedFromServer(vks_uuid(id), fix_broken_links, update_parent_version,
                                                 do_notify_observers);
            return 0;
        });
    }

    static void notify(void* ctx, const char* name)
    {
        guarded(ctx, 0, [&](AISUpdateHost*)
        {
            LL_WARNS("Inventory") << "Fetch failed, content is over limit (" << name << ")" << LL_ENDL;
            LLNotificationsUtil::add(name);
            return 0;
        });
    }

    static bool checkpoint(void* ctx)
    {
        return guarded(ctx, false, [&](AISUpdateHost* host)
        {
            if (host->mUpdate)
            {
                host->mUpdate->checkTimeout();
            }
            return true;
        });
    }

    AISUpdate* mUpdate;
    std::exception_ptr mError;
    VksInvAisHost mTable{};
};

namespace
{
    // The coroutine's arguments as the decisions read them: of the request
    // body only its "depth", with the C++ coercion.
    VksInvAisCall ais_call(AISAPI::COMMAND_TYPE type, const LLUUID& target_id, const LLSD& body)
    {
        VksInvAisCall call{};
        call.command = (int32_t)type;
        vks_set_uuid(call.target_id, target_id);
        vks_set_uuid(call.agent_id, gAgent.getID());
        call.in_open_sim = LLPermissions::getIsInOpenSim();
        call.has_depth = body.has("depth");
        call.depth = call.has_depth ? body["depth"].asInteger() : 0;
        return call;
    }

    struct AISUpdateFree
    {
        void operator()(VksInvAisUpdate* u) const { vks_inv_ais_update_free(u); }
    };
}
// </Vulkanstorm>

/*static*/
void AISAPI::onUpdateReceived(const AISReply& update, COMMAND_TYPE type, const LLSD& request_body,
                              uuid_vec_t& callback_ids) // <Vulkanstorm/> a Rust reply; the callback ids out
{
    LLTimer timer;
    if ( (type == UPDATECATEGORY || type == UPDATEITEM)
        && gSavedSettings.getBOOL("DebugAvatarAppearanceMessage"))
    {
        dump_sequential_xml(gAgentAvatarp->getDebugName() + "_ais_update", update.asLLSD()); // <Vulkanstorm/> asLLSD
    }

    AISUpdate ais_update(update, type, request_body);
    ais_update.doUpdate(); // execute the updates in the appropriate order.
    callback_ids = ais_update.getCallbackIds(); // <Vulkanstorm/>
    LL_DEBUGS("Inventory", "AIS3") << "Elapsed processing: " << timer.getElapsedTimeF32() << LL_ENDL;
}

/*static*/
void AISAPI::InvokeAISCommandCoro(LLCoreHttpUtil::HttpCoroutineAdapter::ptr_t httpAdapter,
        invokationFn_t invoke, std::string url,
        LLUUID targetId, LLSD body, completion_t callback, COMMAND_TYPE type)
{
    if (gDisconnected)
    {
        if (callback)
        {
            callback(LLUUID::null);
        }
        return;
    }

    LLCore::HttpOptions::ptr_t httpOptions = std::make_shared<LLCore::HttpOptions>();
    LLCore::HttpRequest::ptr_t httpRequest = std::make_shared<LLCore::HttpRequest>();
    LLCore::HttpHeaders::ptr_t httpHeaders;

    httpOptions->setTimeout(HTTP_TIMEOUT);

    LL_DEBUGS("Inventory") << "Request url: " << url << LL_ENDL;

    // <Vulkanstorm> Inventory rung R3a. The adapter's handler is the AIS
    // variant (llaisreply.h): it hands the raw response to Rust, which builds
    // the result the LLSD handler would have posted, with the depth cap (an
    // approved fix), and no C++ LLSD tree. The decisions are Rust's: the
    // status and the error branch here (the 410 refetch and delete, the 403
    // notification with its first-call flag), AISUpdate's parse, the
    // completion callback ids. A C++ tree of the result is built only for
    // the logs that print it.
    std::shared_ptr<AISReplySlot> slot = std::make_shared<AISReplySlot>();
    httpAdapter->setLLSDHandlerFactory(AISReplySlot::factory(slot));
    LLSD posted = invoke(httpAdapter , httpRequest , url , body , httpOptions , httpHeaders);
    httpAdapter->setLLSDHandlerFactory(nullptr);
    AISReply result = slot->handler ? slot->handler->takeReply() : AISReply();
    if (!result)
    {
        // The adapter answered without its handler (an immediate error), or
        // the handler could not build the reply: what the adapter returned.
        result = AISReply::fromLLSD(posted);
    }

    static VksInvAisAlert alert = { true }; // the first 403 at depth 0 gets the alert
    const VksInvAisCall call = ais_call(type, targetId, body);
    AISUpdateHost host(nullptr);
    const VksInvAisStatus status = vks_inv_ais_error_branch(result.get(), &call, &alert, host.table());
    host.rethrow();
    if (status.status != 0)
    {
        LL_WARNS("Inventory") << "Inventory error: " << result.status().toString() << " url: " << url << LL_ENDL;
        LL_WARNS("Inventory") << ll_pretty_print_sd(result.asLLSD()) << LL_ENDL;
    }

    LL_DEBUGS("Inventory", "AIS3") << "Result: " << result.asLLSD() << LL_ENDL;
    uuid_vec_t callback_ids;
    onUpdateReceived(result, type, body, callback_ids);

    if (callback != nullptr)
    {
        // The callback contract (Rust): every created or updated id for the
        // writes that report them, else the target's id or null; at least
        // one call regardless of failure.
        for (const LLUUID& id : callback_ids)
        {
            callback(id);
        }
    }
    // </Vulkanstorm>
}

//-------------------------------------------------------------------------
U32 AISUpdate::sBatchFrameCount = 0;
LLTimer AISUpdate::sBatchTimer;

AISUpdate::AISUpdate(const AISReply& update, AISAPI::COMMAND_TYPE type, const LLSD& request_body) // <Vulkanstorm/> AISReply
: mType(type)
{
    mFetch = (type == AISAPI::FETCHITEM)
        || (type == AISAPI::FETCHCATEGORYCHILDREN)
        || (type == AISAPI::FETCHCATEGORYCATEGORIES)
        || (type == AISAPI::FETCHCATEGORYSUBSET)
        || (type == AISAPI::FETCHCOF)
        || (type == AISAPI::FETCHCATEGORYLINKS)
        || (type == AISAPI::FETCHORPHANS);
    // parse update llsd into stuff to do or parse received items.
    mFetchDepth = MAX_FOLDER_DEPTH_REQUEST;
    if (mFetch && request_body.has("depth"))
    {
        mFetchDepth = request_body["depth"].asInteger();
    }

    // start() resets the timer, which clears its expiry: set the expiry after it.
    mTaskTimer.start();
    mTaskTimer.setTimerExpirySec(AIS_TASK_EXPIRY_SECONDS);

    U32 current_frame = LLFrameTimer::getFrameCount();
    if (sBatchFrameCount != current_frame)
    {
        sBatchTimer.start();
        sBatchTimer.setTimerExpirySec(AIS_BATCH_EXPIRY_SECONDS);
        sBatchFrameCount = current_frame;
    }
    parseUpdate(update, request_body); // <Vulkanstorm/> request_body
}

void AISUpdate::clearParseResults()
{
    mCatDescendentDeltas.clear();
    mCatVersionsUpdated.clear();
    mItemsCreated.clear();
    mItemsLost.clear();
    mItemsUpdated.clear();
    mCategoriesCreated.clear();
    mCategoriesUpdated.clear();
    mObjectsDeletedIds.clear();
    mCallbackIds.clear(); // <Vulkanstorm/> was mItemIds, mCategoryIds (the parse's)
}

void AISUpdate::checkTimeout()
{
    // Loop: every waiting update resumes on the same frame event, so the
    // frame's shared batch budget may already be spent when this one runs.
    while (mTaskTimer.hasExpired() || sBatchTimer.hasExpired())
    {
        // If we are taking too long, don't starve other tasks,
        // yield to mainloop.
        // If we use normal suspend(), there will be a chance of
        // waking up from other suspends, before main coro had
        // a chance, so wait for a frame tick instead.
        llcoro::suspendUntilNextFrame();
        LLCoros::checkStop();
        mTaskTimer.setTimerExpirySec(AIS_TASK_EXPIRY_SECONDS);

        U32 current_frame = LLFrameTimer::getFrameCount();
        if (sBatchFrameCount != current_frame)
        {
            // To give other tasks a chance batch timer
            // has a longer delay.
            sBatchTimer.setTimerExpirySec(AIS_BATCH_EXPIRY_SECONDS);
            sBatchFrameCount = current_frame;
        }
    }
}

// <Vulkanstorm> Inventory rung R3a: the parse is vkstorm-inventory's
// (vks_inv_ais_parse): it reads the reply, queries the model through the
// host in the C++ order (checkTimeout() at the start of each embedded part,
// as parseEmbedded() did) and returns the parse's objects and member maps.
// Each record becomes one viewer object here (a lost item is the same object
// as its created entry, as before), with the name localized as
// unpackMessage() did; doUpdate() applies them as before.
void AISUpdate::parseUpdate(const AISReply& update, const LLSD& request_body)
{
    clearParseResults();

    const VksInvAisCall call = ais_call(mType, LLUUID::null, request_body);
    AISUpdateHost host(this);
    std::unique_ptr<VksInvAisUpdate, AISUpdateFree> parsed(vks_inv_ais_parse(update.get(), &call, host.table()));
    // LLCoros::Stopping at a checkpoint, or another exception of the model:
    // the update ends there, as it did inside the C++ parse.
    host.rethrow();
    if (!parsed)
    {
        LL_WARNS("Inventory") << "AIS update not parsed: " << vks_inv_last_error() << LL_ENDL;
        mCallbackIds.push_back(LLUUID::null);
        return;
    }
    const VksInvAisUpdateView* v = vks_inv_ais_update_view(parsed.get());

    std::vector<LLPointer<LLViewerInventoryItem> > items(v->item_count);
    for (size_t i = 0; i < v->item_count; ++i)
    {
        const VksInvAisItem& r = v->items[i];
        LLPointer<LLViewerInventoryItem> item = new LLViewerInventoryItem();
        item->fromVks(r.item);
        item->setComplete(r.complete);
        LLTransactionID transaction_id;
        memcpy(transaction_id.mData, r.transaction_id, UUID_BYTES);
        item->setTransactionID(transaction_id);
        item->setScriptState(r.running, r.faulted);
        item->localizeName();
        items[i] = item;
    }
    std::vector<LLPointer<LLViewerInventoryCategory> > categories(v->category_count);
    for (size_t i = 0; i < v->category_count; ++i)
    {
        const VksInvAisCategory& r = v->categories[i];
        LLPointer<LLViewerInventoryCategory> cat = new LLViewerInventoryCategory(vks_uuid(r.category.owner));
        cat->fromVks(r.category);
        cat->setVersion(r.category.version);
        cat->setDescendentCount(r.descendents);
        if (r.has_requested)
        {
            LLFrameTimer timer;
            memcpy(static_cast<void*>(&timer), &r.requested, sizeof(timer)); // trivially copyable (asserted above)
            cat->setFetchRequestTimer(timer);
        }
        cat->localizeParsedName();
        categories[i] = cat;
    }

    auto each = [](const VksInvAisList& list, auto&& f)
    {
        for (size_t i = 0; i < list.count; ++i)
        {
            f(vks_uuid(list.entries[i].id), list.entries[i].value);
        }
    };
    // The maps hold the deltas and versions as size_t, read back with
    // static_cast<S32> (a signed delta wraps and reads back the same).
    each(v->descendent_deltas, [this](const LLUUID& id, S32 delta) { mCatDescendentDeltas[id] = (size_t)delta; });
    each(v->versions_updated, [this](const LLUUID& id, S32 version) { mCatVersionsUpdated[id] = (size_t)version; });
    each(v->items_created, [&](const LLUUID& id, S32 i) { mItemsCreated[id] = items[i]; });
    each(v->items_lost, [&](const LLUUID& id, S32 i) { mItemsLost[id] = items[i]; });
    each(v->items_updated, [&](const LLUUID& id, S32 i) { mItemsUpdated[id] = items[i]; });
    each(v->categories_created, [&](const LLUUID& id, S32 i) { mCategoriesCreated[id] = categories[i]; });
    each(v->categories_updated, [&](const LLUUID& id, S32 i) { mCategoriesUpdated[id] = categories[i]; });
    each(v->objects_deleted, [this](const LLUUID& id, S32) { mObjectsDeletedIds.insert(id); });
    each(v->callbacks, [this](const LLUUID& id, S32) { mCallbackIds.push_back(id); });
}
// </Vulkanstorm>

void AISUpdate::doUpdate()
{
    checkTimeout();

    // Do version/descendant accounting.
    for (std::map<LLUUID,size_t>::const_iterator catit = mCatDescendentDeltas.begin();
         catit != mCatDescendentDeltas.end(); ++catit)
    {
        LL_DEBUGS("Inventory") << "descendant accounting for " << catit->first << LL_ENDL;

        const LLUUID cat_id(catit->first);
        // Don't account for update if we just created this category.
        if (mCategoriesCreated.find(cat_id) != mCategoriesCreated.end())
        {
            LL_DEBUGS("Inventory") << "Skipping version increment for new category " << cat_id << LL_ENDL;
            continue;
        }

        // Don't account for update unless AIS told us it updated that category.
        if (mCatVersionsUpdated.find(cat_id) == mCatVersionsUpdated.end())
        {
            LL_DEBUGS("Inventory") << "Skipping version increment for non-updated category " << cat_id << LL_ENDL;
            continue;
        }

        // If we have a known descendant count, set that now.
        LLViewerInventoryCategory* cat = gInventory.getCategory(cat_id);
        if (cat)
        {
            S32 descendent_delta = static_cast<S32>(catit->second);
            S32 old_count = cat->getDescendentCount();
            LL_DEBUGS("Inventory") << "Updating descendant count for "
                                   << cat->getName() << " " << cat_id
                                   << " with delta " << descendent_delta << " from "
                                   << old_count << " to " << (old_count+descendent_delta) << LL_ENDL;
            // <FS> FIRE-33455: AIS reported the new absolute version of this category (checked above) and it gets
            //      applied further down; only account for the descendent count here. Bumping the version relative to
            //      our local one as well double counts once two responses for the same folder are processed out of order.
            //LLInventoryModel::LLCategoryUpdate up(cat_id, descendent_delta);
            LLInventoryModel::LLCategoryUpdate up(cat_id, descendent_delta, false);
            // </FS>
            gInventory.accountForUpdate(up);
        }
        else
        {
            LL_DEBUGS("Inventory") << "Skipping version accounting for unknown category " << cat_id << LL_ENDL;
        }
    }

    // CREATE CATEGORIES
    const S32 MAX_UPDATE_BACKLOG = 50; // stall prevention
    for (deferred_category_map_t::const_iterator create_it = mCategoriesCreated.begin();
         create_it != mCategoriesCreated.end(); ++create_it)
    {
        LLUUID category_id(create_it->first);
        LLPointer<LLViewerInventoryCategory> new_category = create_it->second;

        gInventory.updateCategory(new_category, LLInventoryObserver::CREATE);
        LL_DEBUGS("Inventory") << "created category " << category_id << LL_ENDL;

        // fetching can receive massive amount of items and folders
        if (gInventory.getChangedIDs().size() > MAX_UPDATE_BACKLOG)
        {
            gInventory.notifyObservers();
            checkTimeout();
        }
    }

    // UPDATE CATEGORIES
    for (deferred_category_map_t::const_iterator update_it = mCategoriesUpdated.begin();
         update_it != mCategoriesUpdated.end(); ++update_it)
    {
        LLUUID category_id(update_it->first);
        LLPointer<LLViewerInventoryCategory> new_category = update_it->second;
        // Since this is a copy of the category *before* the accounting update, above,
        // we need to transfer back the updated version/descendant count.
        LLViewerInventoryCategory* curr_cat = gInventory.getCategory(new_category->getUUID());
        if (!curr_cat)
        {
            LL_WARNS("Inventory") << "Failed to update unknown category " << new_category->getUUID() << LL_ENDL;
        }
        else
        {
            new_category->setVersion(curr_cat->getVersion());
            new_category->setDescendentCount(curr_cat->getDescendentCount());
            gInventory.updateCategory(new_category);
            LL_DEBUGS("Inventory") << "updated category " << new_category->getName() << " " << category_id << LL_ENDL;
        }
    }

    // LOST ITEMS
    if (!mItemsLost.empty())
    {
        LL_INFOS("Inventory") << "Received " << (S32)mItemsLost.size() << " items without a parent" << LL_ENDL;
        const LLUUID lost_uuid(gInventory.findCategoryUUIDForType(LLFolderType::FT_LOST_AND_FOUND));
        if (lost_uuid.notNull())
        {
            for (deferred_item_map_t::const_iterator lost_it = mItemsLost.begin();
                 lost_it != mItemsLost.end(); ++lost_it)
            {
                LLPointer<LLViewerInventoryItem> new_item = lost_it->second;

                new_item->setParent(lost_uuid);
                new_item->updateParentOnServer(false);
            }
        }
    }

    // CREATE ITEMS
    for (deferred_item_map_t::const_iterator create_it = mItemsCreated.begin();
         create_it != mItemsCreated.end(); ++create_it)
    {
        LLUUID item_id(create_it->first);
        LLPointer<LLViewerInventoryItem> new_item = create_it->second;

        // FIXME risky function since it calls updateServer() in some
        // cases.  Maybe break out the update/create cases, in which
        // case this is create.
        LL_DEBUGS("Inventory") << "created item " << item_id << LL_ENDL;
        gInventory.updateItem(new_item, LLInventoryObserver::CREATE);

        // fetching can receive massive amount of items and folders
        if (gInventory.getChangedIDs().size() > MAX_UPDATE_BACKLOG)
        {
            gInventory.notifyObservers();
            checkTimeout();
        }
    }

    // UPDATE ITEMS
    for (deferred_item_map_t::const_iterator update_it = mItemsUpdated.begin();
         update_it != mItemsUpdated.end(); ++update_it)
    {
        LLUUID item_id(update_it->first);
        LLPointer<LLViewerInventoryItem> new_item = update_it->second;
        // FIXME risky function since it calls updateServer() in some
        // cases.  Maybe break out the update/create cases, in which
        // case this is update.
        LL_DEBUGS("Inventory") << "updated item " << item_id << LL_ENDL;
        //LL_DEBUGS("Inventory") << ll_pretty_print_sd(new_item->asLLSD()) << LL_ENDL;
        gInventory.updateItem(new_item);
    }

    // DELETE OBJECTS
    for (uuid_list_t::const_iterator del_it = mObjectsDeletedIds.begin();
         del_it != mObjectsDeletedIds.end(); ++del_it)
    {
        LL_DEBUGS("Inventory") << "deleted item " << *del_it << LL_ENDL;
        gInventory.onObjectDeletedFromServer(*del_it, false, false, false);
    }

    // TODO - how can we use this version info? Need to be sure all
    // changes are going through AIS first, or at least through
    // something with a reliable responder.
    for (uuid_int_map_t::iterator ucv_it = mCatVersionsUpdated.begin();
         ucv_it != mCatVersionsUpdated.end(); ++ucv_it)
    {
        const LLUUID id = ucv_it->first;
        S32 version = static_cast<S32>(ucv_it->second);
        LLViewerInventoryCategory *cat = gInventory.getCategory(id);
        // <FS> FIRE-33455: don't dereference a category we don't know about
        if (!cat)
        {
            LL_DEBUGS("Inventory") << "Skipping version update for unknown category " << id << LL_ENDL;
            continue;
        }
        // </FS>
        LL_DEBUGS("Inventory") << "cat version update " << cat->getName() << " to version " << cat->getVersion() << LL_ENDL;
        if (cat->getVersion() != version)
        {
            // the AIS version should be considered the true version. Adjust
            // our local category model to reflect this version number.  Otherwise
            // it becomes possible to get stuck with the viewer being out of
            // sync with the inventory system.  Under normal circumstances
            // inventory COF is maintained on the viewer through calls to
            // LLInventoryModel::accountForUpdate when a changing operation
            // is performed.  This occasionally gets out of sync however.
            if (version != LLViewerInventoryCategory::VERSION_UNKNOWN)
            {
                // <FS> FIRE-33455: category versions only ever go up on the server. With several AIS requests in flight
                //      (PoolSizeAIS is 20, and doUpdate() can yield to the next frame) an older response can be processed
                //      after a newer one; adopting its version rolls the viewer back below the server. For the COF that
                //      wedges server-side baking ("Cof Version Mismatch") until relog because every retry resends the
                //      same stale version.
                //LL_WARNS() << "Possible version mismatch for category " << cat->getName()
                //    << ", viewer version " << cat->getVersion()
                //    << " AIS version " << version << " !!!Adjusting local version!!!" << LL_ENDL;
                //cat->setVersion(version);
                if ( (LLViewerInventoryCategory::VERSION_UNKNOWN == cat->getVersion()) || (version > cat->getVersion()) )
                {
                    LL_DEBUGS("Inventory") << "Updating version for category " << cat->getName()
                        << " from " << cat->getVersion() << " to AIS version " << version << LL_ENDL;
                    cat->setVersion(version);
                }
                else
                {
                    LL_WARNS("Inventory") << "Ignoring stale AIS version " << version << " for category " << cat->getName()
                        << " (viewer version " << cat->getVersion() << ")" << LL_ENDL;
                }
                // </FS>
            }
            else
            {
                // We do not account for update if version is UNKNOWN, so we shouldn't rise version
                // either or viewer will get stuck on descendants count -1, try to refetch folder instead
                //
                // Todo: proper backoff?

                LL_WARNS() << "Possible version mismatch for category " << cat->getName()
                    << ", viewer version " << cat->getVersion()
                    << " AIS version " << version << " !!!Rerequesting category!!!" << LL_ENDL;
                const S32 LONG_EXPIRY = 360;
                cat->fetch(LONG_EXPIRY);
            }
        }
    }

    checkTimeout();

    gInventory.notifyObservers();
}

