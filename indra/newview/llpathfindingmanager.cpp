/**
* @file llpathfindingmanager.cpp
* @brief Implementation of llpathfindingmanager
* @author Stinson@lindenlab.com
*
* $LicenseInfo:firstyear=2012&license=viewerlgpl$
* Second Life Viewer Source Code
* Copyright (C) 2012, Linden Research, Inc.
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

#include "llpathfindingmanager.h"
#include "vsnativeim.h"
#include "llregionhandle.h"
#if VS_NATIVE_VULKAN
#include "vsnativesession.h"
#endif

#include <string>
#include <map>

#include <boost/bind.hpp>
#include <boost/signals2.hpp>

#include "llagent.h"
#include "llhttpnode.h"
#include "llnotificationsutil.h"
#include "llpathfindingcharacterlist.h"
#include "llpathfindinglinkset.h"
#include "llpathfindinglinksetlist.h"
#include "llpathfindingnavmesh.h"
#include "llpathfindingnavmeshstatus.h"
#include "llpathfindingobject.h"
#include "llpathinglib.h"
#include "llsingleton.h"
#include "llsd.h"
#include "lltrans.h"
#include "lluuid.h"
#include "llviewerregion.h"
#include "llweb.h"
#include "llcorehttputil.h"
#include "llworld.h"

#define CAP_SERVICE_RETRIEVE_NAVMESH        "RetrieveNavMeshSrc"

#define CAP_SERVICE_NAVMESH_STATUS          "NavMeshGenerationStatus"

#define CAP_SERVICE_GET_OBJECT_LINKSETS     "RegionObjects"
#define CAP_SERVICE_SET_OBJECT_LINKSETS     "ObjectNavMeshProperties"
#define CAP_SERVICE_TERRAIN_LINKSETS        "TerrainNavMeshProperties"

#define CAP_SERVICE_CHARACTERS              "CharacterProperties"

#define SIM_MESSAGE_NAVMESH_STATUS_UPDATE   "/message/NavMeshStatusUpdate"
#define SIM_MESSAGE_AGENT_STATE_UPDATE      "/message/AgentStateUpdate"
#define SIM_MESSAGE_BODY_FIELD              "body"

#define CAP_SERVICE_AGENT_STATE             "AgentState"

#define AGENT_STATE_CAN_REBAKE_REGION_FIELD "can_modify_navmesh"

//---------------------------------------------------------------------------
// LLNavMeshSimStateChangeNode
//---------------------------------------------------------------------------

class LLNavMeshSimStateChangeNode : public LLHTTPNode
{
public:
    virtual void post(ResponsePtr pResponse, const LLSD &pContext, const LLSD &pInput) const;
};

LLHTTPRegistration<LLNavMeshSimStateChangeNode> gHTTPRegistrationNavMeshSimStateChangeNode(SIM_MESSAGE_NAVMESH_STATUS_UPDATE);


//---------------------------------------------------------------------------
// LLAgentStateChangeNode
//---------------------------------------------------------------------------
class LLAgentStateChangeNode : public LLHTTPNode
{
public:
    virtual void post(ResponsePtr pResponse, const LLSD &pContext, const LLSD &pInput) const;
};

LLHTTPRegistration<LLAgentStateChangeNode> gHTTPRegistrationAgentStateChangeNode(SIM_MESSAGE_AGENT_STATE_UPDATE);

//---------------------------------------------------------------------------
// LinksetsResponder
//---------------------------------------------------------------------------

class LinksetsResponder
{
public:
    LinksetsResponder(LLPathfindingManager::request_id_t pRequestId, LLPathfindingManager::object_request_callback_t pLinksetsCallback, bool pIsObjectRequested, bool pIsTerrainRequested);
    virtual ~LinksetsResponder();

    void handleObjectLinksetsResult(const LLSD &pContent);
    void handleObjectLinksetsError();
    void handleTerrainLinksetsResult(const LLSD &pContent);
    void handleTerrainLinksetsError();

    typedef std::shared_ptr<LinksetsResponder> ptr_t;

protected:

private:
    void sendCallback();

    typedef enum
    {
        kNotRequested,
        kWaiting,
        kReceivedGood,
        kReceivedError
    } EMessagingState;

    LLPathfindingManager::request_id_t              mRequestId;
    LLPathfindingManager::object_request_callback_t mLinksetsCallback;

    EMessagingState                                 mObjectMessagingState;
    EMessagingState                                 mTerrainMessagingState;

    LLPathfindingObjectListPtr                      mObjectLinksetListPtr;
    LLPathfindingObjectPtr                          mTerrainLinksetPtr;
};

typedef std::shared_ptr<LinksetsResponder> LinksetsResponderPtr;

//---------------------------------------------------------------------------
// LLPathfindingManager
//---------------------------------------------------------------------------

LLPathfindingManager::LLPathfindingManager():
    mNavMeshMap(),
    mAgentStateSignal()
{
}

LLPathfindingManager::~LLPathfindingManager()
{
    *mLifetime = false;
    quitSystem();
}

void LLPathfindingManager::initSystem()
{
    if (LLPathingLib::getInstance() == NULL)
    {
        LLPathingLib::initSystem();
    }
}

void LLPathfindingManager::quitSystem()
{
    if (LLPathingLib::getInstance() != NULL)
    {
        LLPathingLib::quitSystem();
    }
}

bool LLPathfindingManager::isPathfindingViewEnabled() const
{
    return (LLPathingLib::getInstance() != NULL);
}

bool LLPathfindingManager::isPathfindingEnabledForCurrentRegion() const
{
    return isPathfindingEnabledForRegion(getCurrentRegion());
}

bool LLPathfindingManager::isPathfindingEnabledForRegion(const std::optional<VSRegionContext>& pRegion) const
{
    std::string retrieveNavMeshURL = getRetrieveNavMeshURLForRegion(pRegion);
    return !retrieveNavMeshURL.empty();
}

bool LLPathfindingManager::isAllowViewTerrainProperties() const
{
    const auto region = getCurrentRegion();
    return (gAgent.isGodlike() || ((bool(region)) && region->canManageEstate()));
}

LLPathfindingNavMesh::navmesh_slot_t LLPathfindingManager::registerNavMeshListenerForRegion(const std::optional<VSRegionContext>& pRegion, LLPathfindingNavMesh::navmesh_callback_t pNavMeshCallback)
{
    LLPathfindingNavMeshPtr navMeshPtr = getNavMeshForRegion(pRegion);
    return navMeshPtr->registerNavMeshListener(pNavMeshCallback);
}

void LLPathfindingManager::requestGetNavMeshForRegion(const std::optional<VSRegionContext>& pRegion, bool pIsGetStatusOnly)
{
    const auto accountCurrent = requestGuard(pRegion);
    std::weak_ptr<bool> query;
    if (pRegion)
    {
        auto& active = mNavMeshIntents[pRegion->getRegionID()];
        if (active) *active = false;
        active = std::make_shared<bool>(true);
        query = active;
    }
    const auto requestCurrent = [accountCurrent, query]
    { const auto active = query.lock(); return accountCurrent() && active && *active; };
    if (pRegion && !requestCurrent()) return;
    LLPathfindingNavMeshPtr navMeshPtr = getNavMeshForRegion(pRegion);

    if (!pRegion)
    {
        navMeshPtr->handleNavMeshNotEnabled();
    }
    else if (!pRegion->capabilitiesReceived())
    {
        navMeshPtr->handleNavMeshWaitForRegionLoad();
        pRegion->whenCapabilitiesReceived(guardedDeferred(pRegion, boost::bind(&LLPathfindingManager::handleDeferredGetNavMeshForRegion, this, _1, pIsGetStatusOnly), requestCurrent), requestCurrent);
    }
    else if (!isPathfindingEnabledForRegion(pRegion))
    {
        navMeshPtr->handleNavMeshNotEnabled();
    }
    else
    {
        std::string navMeshStatusURL = getNavMeshStatusURLForRegion(pRegion);
        llassert(!navMeshStatusURL.empty());
        navMeshPtr->handleNavMeshCheckVersion();

        if (!requestCurrent()) return;
        U64 regionHandle = pRegion->getHandle();
        std::string coroname = LLCoros::instance().launch("LLPathfindingManager::navMeshStatusRequestCoro",
            boost::bind(&LLPathfindingManager::navMeshStatusRequestCoro, navMeshStatusURL, regionHandle, pIsGetStatusOnly, requestCurrent));
    }
}

void LLPathfindingManager::requestGetLinksets(request_id_t pRequestId, object_request_callback_t pLinksetsCallback) const
{
    const auto requestCurrent = requestGuard(getCurrentRegion());
    LLPathfindingObjectListPtr emptyLinksetListPtr;
    const auto currentRegion = getCurrentRegion();

    if (!currentRegion)
    {
        pLinksetsCallback(pRequestId, kRequestNotEnabled, emptyLinksetListPtr);
            if (!requestCurrent()) return;
    }
    else if (!currentRegion->capabilitiesReceived())
    {
        pLinksetsCallback(pRequestId, kRequestStarted, emptyLinksetListPtr);
            if (!requestCurrent()) return;
        currentRegion->whenCapabilitiesReceived(guardedDeferred(currentRegion, boost::bind(&LLPathfindingManager::handleDeferredGetLinksetsForRegion, this, _1, pRequestId, pLinksetsCallback), requestCurrent), requestCurrent);
    }
    else
    {
        std::string objectLinksetsURL = getRetrieveObjectLinksetsURLForCurrentRegion();
        std::string terrainLinksetsURL = getTerrainLinksetsURLForCurrentRegion();
        if (objectLinksetsURL.empty() || terrainLinksetsURL.empty())
        {
            pLinksetsCallback(pRequestId, kRequestNotEnabled, emptyLinksetListPtr);
            if (!requestCurrent()) return;
        }
        else
        {
            pLinksetsCallback(pRequestId, kRequestStarted, emptyLinksetListPtr);
            if (!requestCurrent()) return;

            bool doRequestTerrain = isAllowViewTerrainProperties();
            LinksetsResponder::ptr_t linksetsResponderPtr(new LinksetsResponder(pRequestId, pLinksetsCallback, true, doRequestTerrain));

            std::string coroname = LLCoros::instance().launch("LLPathfindingManager::linksetObjectsCoro",
                boost::bind(&LLPathfindingManager::linksetObjectsCoro, objectLinksetsURL, linksetsResponderPtr, LLSD(), requestCurrent));

            if (doRequestTerrain)
            {
                std::string coroname = LLCoros::instance().launch("LLPathfindingManager::linksetTerrainCoro",
                    boost::bind(&LLPathfindingManager::linksetTerrainCoro, terrainLinksetsURL, linksetsResponderPtr, LLSD(), requestCurrent));
            }
        }
    }
}

void LLPathfindingManager::requestSetLinksets(request_id_t pRequestId, const LLPathfindingObjectListPtr &pLinksetListPtr, LLPathfindingLinkset::ELinksetUse pLinksetUse, S32 pA, S32 pB, S32 pC, S32 pD, object_request_callback_t pLinksetsCallback) const
{
    const auto requestCurrent = requestGuard(getCurrentRegion());
    LLPathfindingObjectListPtr emptyLinksetListPtr;

    std::string objectLinksetsURL = getChangeObjectLinksetsURLForCurrentRegion();
    std::string terrainLinksetsURL = getTerrainLinksetsURLForCurrentRegion();
    if (objectLinksetsURL.empty() || terrainLinksetsURL.empty())
    {
        pLinksetsCallback(pRequestId, kRequestNotEnabled, emptyLinksetListPtr);
            if (!requestCurrent()) return;
    }
    else if ((pLinksetListPtr == NULL) || pLinksetListPtr->isEmpty())
    {
        pLinksetsCallback(pRequestId, kRequestCompleted, emptyLinksetListPtr);
            if (!requestCurrent()) return;
    }
    else
    {
        const LLPathfindingLinksetList *linksetList = dynamic_cast<const LLPathfindingLinksetList *>(pLinksetListPtr.get());

        LLSD objectPostData = linksetList->encodeObjectFields(pLinksetUse, pA, pB, pC, pD);
        LLSD terrainPostData;
        if (isAllowViewTerrainProperties())
        {
            terrainPostData = linksetList->encodeTerrainFields(pLinksetUse, pA, pB, pC, pD);
        }

        if (objectPostData.isUndefined() && terrainPostData.isUndefined())
        {
            pLinksetsCallback(pRequestId, kRequestCompleted, emptyLinksetListPtr);
            if (!requestCurrent()) return;
        }
        else
        {
            pLinksetsCallback(pRequestId, kRequestStarted, emptyLinksetListPtr);
            if (!requestCurrent()) return;

            LinksetsResponder::ptr_t linksetsResponderPtr(new LinksetsResponder(pRequestId, pLinksetsCallback, !objectPostData.isUndefined(), !terrainPostData.isUndefined()));

            if (!objectPostData.isUndefined())
            {
                std::string coroname = LLCoros::instance().launch("LLPathfindingManager::linksetObjectsCoro",
                    boost::bind(&LLPathfindingManager::linksetObjectsCoro, objectLinksetsURL, linksetsResponderPtr, objectPostData, requestCurrent));
            }

            if (!terrainPostData.isUndefined())
            {
                std::string coroname = LLCoros::instance().launch("LLPathfindingManager::linksetTerrainCoro",
                    boost::bind(&LLPathfindingManager::linksetTerrainCoro, terrainLinksetsURL, linksetsResponderPtr, terrainPostData, requestCurrent));
            }
        }
    }
}

void LLPathfindingManager::requestGetCharacters(request_id_t pRequestId, object_request_callback_t pCharactersCallback) const
{
    const auto requestCurrent = requestGuard(getCurrentRegion());
    LLPathfindingObjectListPtr emptyCharacterListPtr;

    const auto currentRegion = getCurrentRegion();

    if (!currentRegion)
    {
        pCharactersCallback(pRequestId, kRequestNotEnabled, emptyCharacterListPtr);
            if (!requestCurrent()) return;
    }
    else if (!currentRegion->capabilitiesReceived())
    {
        pCharactersCallback(pRequestId, kRequestStarted, emptyCharacterListPtr);
            if (!requestCurrent()) return;
        currentRegion->whenCapabilitiesReceived(guardedDeferred(currentRegion, boost::bind(&LLPathfindingManager::handleDeferredGetCharactersForRegion, this, _1, pRequestId, pCharactersCallback), requestCurrent), requestCurrent);
    }
    else
    {
        std::string charactersURL = getCharactersURLForCurrentRegion();
        if (charactersURL.empty())
        {
            pCharactersCallback(pRequestId, kRequestNotEnabled, emptyCharacterListPtr);
            if (!requestCurrent()) return;
        }
        else
        {
            pCharactersCallback(pRequestId, kRequestStarted, emptyCharacterListPtr);
            if (!requestCurrent()) return;

            std::string coroname = LLCoros::instance().launch("LLPathfindingManager::charactersCoro",
                boost::bind(&LLPathfindingManager::charactersCoro, charactersURL, pRequestId, pCharactersCallback, requestCurrent));
        }
    }
}

LLPathfindingManager::agent_state_slot_t LLPathfindingManager::registerAgentStateListener(agent_state_callback_t pAgentStateCallback)
{
    return mAgentStateSignal.connect(pAgentStateCallback);
}

void LLPathfindingManager::requestGetAgentState()
{
    const auto requestCurrent = requestGuard(getCurrentRegion());
    const auto currentRegion = getCurrentRegion();

    if (!currentRegion)
    {
        mAgentStateSignal(false);
    }
    else
    {
        if (!currentRegion->capabilitiesReceived())
        {
            currentRegion->whenCapabilitiesReceived(guardedDeferred(currentRegion, boost::bind(&LLPathfindingManager::handleDeferredGetAgentStateForRegion, this, _1), requestCurrent), requestCurrent);
        }
        else if (!isPathfindingEnabledForRegion(currentRegion))
        {
            mAgentStateSignal(false);
        }
        else
        {
            std::string agentStateURL = getAgentStateURLForRegion(currentRegion);
            llassert(!agentStateURL.empty());

            std::string coroname = LLCoros::instance().launch("LLPathfindingManager::navAgentStateRequestCoro",
                boost::bind(&LLPathfindingManager::navAgentStateRequestCoro, agentStateURL, requestCurrent));
        }
    }
}

void LLPathfindingManager::requestRebakeNavMesh(rebake_navmesh_callback_t pRebakeNavMeshCallback)
{
    const auto requestCurrent = requestGuard(getCurrentRegion());
    const auto currentRegion = getCurrentRegion();

    if (!currentRegion)
    {
        pRebakeNavMeshCallback(false);
            if (!requestCurrent()) return;
    }
    else if (!isPathfindingEnabledForRegion(currentRegion))
    {
        pRebakeNavMeshCallback(false);
            if (!requestCurrent()) return;
    }
    else
    {
        std::string navMeshStatusURL = getNavMeshStatusURLForCurrentRegion();
        llassert(!navMeshStatusURL.empty());

        std::string coroname = LLCoros::instance().launch("LLPathfindingManager::navMeshRebakeCoro",
                boost::bind(&LLPathfindingManager::navMeshRebakeCoro, navMeshStatusURL, pRebakeNavMeshCallback, requestCurrent));
    }
}

void LLPathfindingManager::handleDeferredGetAgentStateForRegion(const LLUUID &pRegionUUID)
{
    const auto currentRegion = getCurrentRegion();

    if ((bool(currentRegion)) && (currentRegion->getRegionID() == pRegionUUID))
    {
        requestGetAgentState();
    }
}

void LLPathfindingManager::handleDeferredGetNavMeshForRegion(const LLUUID &pRegionUUID, bool pIsGetStatusOnly)
{
    const auto currentRegion = getCurrentRegion();

    if ((bool(currentRegion)) && (currentRegion->getRegionID() == pRegionUUID))
    {
        requestGetNavMeshForRegion(currentRegion, pIsGetStatusOnly);
    }
}

void LLPathfindingManager::handleDeferredGetLinksetsForRegion(const LLUUID &pRegionUUID, request_id_t pRequestId, object_request_callback_t pLinksetsCallback) const
{
    const auto currentRegion = getCurrentRegion();

    if ((bool(currentRegion)) && (currentRegion->getRegionID() == pRegionUUID))
    {
        requestGetLinksets(pRequestId, pLinksetsCallback);
    }
}

void LLPathfindingManager::handleDeferredGetCharactersForRegion(const LLUUID &pRegionUUID, request_id_t pRequestId, object_request_callback_t pCharactersCallback) const
{
    const auto currentRegion = getCurrentRegion();

    if ((bool(currentRegion)) && (currentRegion->getRegionID() == pRegionUUID))
    {
        requestGetCharacters(pRequestId, pCharactersCallback);
    }
}

void LLPathfindingManager::navMeshStatusRequestCoro(std::string url, U64 regionHandle, bool isGetStatusOnly, std::function<bool()> current)
{
    if (!current()) return;
    LLCore::HttpRequest::policy_t httpPolicy(LLCore::HttpRequest::DEFAULT_POLICY_ID);
    LLCoreHttpUtil::HttpCoroutineAdapter::ptr_t
        httpAdapter = std::make_shared<LLCoreHttpUtil::HttpCoroutineAdapter>("NavMeshStatusRequest", httpPolicy);
    LLCore::HttpRequest::ptr_t httpRequest = std::make_shared<LLCore::HttpRequest>();

    auto region = vs_region_context(from_region_handle(regionHandle));
    if (!region)
    {
        LL_WARNS("PathfindingManager") << "Attempting to retrieve navmesh status for region that has gone away." << LL_ENDL;
        return;
    }
    LLUUID regionUUID = region->getRegionID();

    LLSD result = httpAdapter->getAndSuspend(httpRequest, url);
    if (!current()) return;

    region = vs_region_context(from_region_handle(regionHandle));

    LLSD httpResults = result[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS];
    LLCore::HttpStatus status = LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(httpResults);

    LLPathfindingNavMeshStatus navMeshStatus(regionUUID);
    if (!status)
    {
        LL_WARNS("PathfindingManager") << "HTTP status, " << status.toTerseString() <<
            ". Building using empty status." << LL_ENDL;
    }
    else
    {
        result.erase(LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS);
        navMeshStatus = LLPathfindingNavMeshStatus(regionUUID, result);
    }

    LLPathfindingNavMeshPtr navMeshPtr = LLPathfindingManager::instance().getNavMeshForRegion(regionUUID);

    if (!navMeshStatus.isValid())
    {
        navMeshPtr->handleNavMeshError();
        return;
    }
    else if (navMeshPtr->hasNavMeshVersion(navMeshStatus))
    {
        navMeshPtr->handleRefresh(navMeshStatus);
        return;
    }
    else if (isGetStatusOnly)
    {
        navMeshPtr->handleNavMeshNewVersion(navMeshStatus);
        return;
    }

    if ((!region) || !region->current())
    {
        LL_WARNS("PathfindingManager") << "About to update navmesh status for region that has gone away." << LL_ENDL;
        navMeshPtr->handleNavMeshNotEnabled();
        return;
    }

    std::string navMeshURL = LLPathfindingManager::instance().getRetrieveNavMeshURLForRegion(region);

    if (navMeshURL.empty())
    {
        navMeshPtr->handleNavMeshNotEnabled();
        return;
    }

    navMeshPtr->handleNavMeshStart(navMeshStatus);
    if (!current()) return;

    LLSD postData;
    result = httpAdapter->postAndSuspend(httpRequest, navMeshURL, postData);
    if (!current()) return;

    httpResults = result[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS];
    status = LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(httpResults);
    U32 navMeshVersion = navMeshStatus.getVersion();

    if (!status)
    {
        LL_WARNS("PathfindingManager") << "HTTP status, " << status.toTerseString() <<
            ". reporting error." << LL_ENDL;
        navMeshPtr->handleNavMeshError(navMeshVersion);
    }
    else
    {
        result.erase(LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS);
        navMeshPtr->handleNavMeshResult(result, navMeshVersion);

    }

}

void LLPathfindingManager::navAgentStateRequestCoro(std::string url, std::function<bool()> current)
{
    if (!current()) return;
    LLCore::HttpRequest::policy_t httpPolicy(LLCore::HttpRequest::DEFAULT_POLICY_ID);
    LLCoreHttpUtil::HttpCoroutineAdapter::ptr_t
        httpAdapter = std::make_shared<LLCoreHttpUtil::HttpCoroutineAdapter>("NavAgentStateRequest", httpPolicy);
    LLCore::HttpRequest::ptr_t httpRequest = std::make_shared<LLCore::HttpRequest>();

    LLSD result = httpAdapter->getAndSuspend(httpRequest, url);
    if (!current()) return;

    LLSD httpResults = result[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS];
    LLCore::HttpStatus status = LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(httpResults);

    bool canRebake = false;
    if (!status)
    {
        LL_WARNS("PathfindingManager") << "HTTP status, " << status.toTerseString() <<
            ". Building using empty status." << LL_ENDL;
    }
    else
    {
        llassert(result.has(AGENT_STATE_CAN_REBAKE_REGION_FIELD));
        llassert(result.get(AGENT_STATE_CAN_REBAKE_REGION_FIELD).isBoolean());
        canRebake = result.get(AGENT_STATE_CAN_REBAKE_REGION_FIELD).asBoolean();
    }

    LLPathfindingManager::instance().handleAgentState(canRebake);
}

void LLPathfindingManager::navMeshRebakeCoro(std::string url, rebake_navmesh_callback_t rebakeNavMeshCallback, std::function<bool()> current)
{
    if (!current()) return;
    LLCore::HttpRequest::policy_t httpPolicy(LLCore::HttpRequest::DEFAULT_POLICY_ID);
    LLCoreHttpUtil::HttpCoroutineAdapter::ptr_t
        httpAdapter = std::make_shared<LLCoreHttpUtil::HttpCoroutineAdapter>("NavMeshRebake", httpPolicy);
    LLCore::HttpRequest::ptr_t httpRequest = std::make_shared<LLCore::HttpRequest>();


    LLSD postData = LLSD::emptyMap();
    postData["command"] = "rebuild";

    LLSD result = httpAdapter->postAndSuspend(httpRequest, url, postData);
    if (!current()) return;

    LLSD httpResults = result[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS];
    LLCore::HttpStatus status = LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(httpResults);

    bool success = true;
    if (!status)
    {
        LL_WARNS("PathfindingManager") << "HTTP status, " << status.toTerseString() <<
            ". Rebake failed." << LL_ENDL;
        success = false;
    }

    rebakeNavMeshCallback(success);
}

// If called with putData undefined this coroutine will issue a get.  If there
// is data in putData it will be PUT to the URL.
void LLPathfindingManager::linksetObjectsCoro(std::string url, LinksetsResponder::ptr_t linksetsResponsderPtr, LLSD putData, std::function<bool()> current)
{
    if (!current()) return;
    LLCore::HttpRequest::policy_t httpPolicy(LLCore::HttpRequest::DEFAULT_POLICY_ID);
    LLCoreHttpUtil::HttpCoroutineAdapter::ptr_t
        httpAdapter = std::make_shared<LLCoreHttpUtil::HttpCoroutineAdapter>("LinksetObjects", httpPolicy);
    LLCore::HttpRequest::ptr_t httpRequest = std::make_shared<LLCore::HttpRequest>();

    LLSD result;

    if (putData.isUndefined())
    {
        result = httpAdapter->getAndSuspend(httpRequest, url);
    if (!current()) return;
    }
    else
    {
        result = httpAdapter->putAndSuspend(httpRequest, url, putData);
    if (!current()) return;
    }

    LLSD httpResults = result[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS];
    LLCore::HttpStatus status = LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(httpResults);

    if (!status)
    {
        LL_WARNS("PathfindingManager") << "HTTP status, " << status.toTerseString() <<
            ". linksetObjects failed." << LL_ENDL;
        linksetsResponsderPtr->handleObjectLinksetsError();
    }
    else
    {
        result.erase(LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS);
        linksetsResponsderPtr->handleObjectLinksetsResult(result);
    }
}

// If called with putData undefined this coroutine will issue a GET.  If there
// is data in putData it will be PUT to the URL.
void LLPathfindingManager::linksetTerrainCoro(std::string url, LinksetsResponder::ptr_t linksetsResponsderPtr, LLSD putData, std::function<bool()> current)
{
    if (!current()) return;
    LLCore::HttpRequest::policy_t httpPolicy(LLCore::HttpRequest::DEFAULT_POLICY_ID);
    LLCoreHttpUtil::HttpCoroutineAdapter::ptr_t
        httpAdapter = std::make_shared<LLCoreHttpUtil::HttpCoroutineAdapter>("LinksetTerrain", httpPolicy);
    LLCore::HttpRequest::ptr_t httpRequest = std::make_shared<LLCore::HttpRequest>();

    LLSD result;

    if (putData.isUndefined())
    {
        result = httpAdapter->getAndSuspend(httpRequest, url);
    if (!current()) return;
    }
    else
    {
        result = httpAdapter->putAndSuspend(httpRequest, url, putData);
    if (!current()) return;
    }

    LLSD httpResults = result[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS];
    LLCore::HttpStatus status = LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(httpResults);

    if (!status)
    {
        LL_WARNS("PathfindingManager") << "HTTP status, " << status.toTerseString() <<
            ". linksetTerrain failed." << LL_ENDL;
        linksetsResponsderPtr->handleTerrainLinksetsError();
    }
    else
    {
        result.erase(LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS);
        linksetsResponsderPtr->handleTerrainLinksetsResult(result);
    }

}

void LLPathfindingManager::charactersCoro(std::string url, request_id_t requestId, object_request_callback_t callback, std::function<bool()> current)
{
    if (!current()) return;
    LLCore::HttpRequest::policy_t httpPolicy(LLCore::HttpRequest::DEFAULT_POLICY_ID);
    LLCoreHttpUtil::HttpCoroutineAdapter::ptr_t
        httpAdapter = std::make_shared<LLCoreHttpUtil::HttpCoroutineAdapter>("charactersCoro", httpPolicy);
    LLCore::HttpRequest::ptr_t httpRequest = std::make_shared<LLCore::HttpRequest>();

    LLSD result = httpAdapter->getAndSuspend(httpRequest, url);
    if (!current()) return;

    LLSD httpResults = result[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS];
    LLCore::HttpStatus status = LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(httpResults);

    if (!status)
    {
        LL_WARNS("PathfindingManager") << "HTTP status, " << status.toTerseString() <<
            ". characters failed." << LL_ENDL;

        LLPathfindingObjectListPtr characterListPtr = LLPathfindingObjectListPtr(new LLPathfindingCharacterList());
        callback(requestId, LLPathfindingManager::kRequestError, characterListPtr);
    }
    else
    {
        result.erase(LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS);
        LLPathfindingObjectListPtr characterListPtr = LLPathfindingObjectListPtr(new LLPathfindingCharacterList(result));
        callback(requestId, LLPathfindingManager::kRequestCompleted, characterListPtr);
    }
}

void LLPathfindingManager::handleNavMeshStatusUpdate(const LLPathfindingNavMeshStatus &pNavMeshStatus)
{
    if (const auto found = mNavMeshIntents.find(pNavMeshStatus.getRegionUUID()); found != mNavMeshIntents.end())
        *found->second = false;
    LLPathfindingNavMeshPtr navMeshPtr = getNavMeshForRegion(pNavMeshStatus.getRegionUUID());

    if (!pNavMeshStatus.isValid())
    {
        navMeshPtr->handleNavMeshError();
    }
    else
    {
        navMeshPtr->handleNavMeshNewVersion(pNavMeshStatus);
    }
}

void LLPathfindingManager::handleAgentState(bool pCanRebakeRegion)
{
    mAgentStateSignal(pCanRebakeRegion);
}

LLPathfindingNavMeshPtr LLPathfindingManager::getNavMeshForRegion(const LLUUID &pRegionUUID)
{
    LLPathfindingNavMeshPtr navMeshPtr;
    NavMeshMap::iterator navMeshIter = mNavMeshMap.find(pRegionUUID);
    if (navMeshIter == mNavMeshMap.end())
    {
        navMeshPtr = LLPathfindingNavMeshPtr(new LLPathfindingNavMesh(pRegionUUID));
        mNavMeshMap.insert(std::pair<LLUUID, LLPathfindingNavMeshPtr>(pRegionUUID, navMeshPtr));
    }
    else
    {
        navMeshPtr = navMeshIter->second;
    }

    return navMeshPtr;
}

LLPathfindingNavMeshPtr LLPathfindingManager::getNavMeshForRegion(const std::optional<VSRegionContext>& pRegion)
{
    LLUUID regionUUID;
    if (bool(pRegion))
    {
        regionUUID = pRegion->getRegionID();
    }

    return getNavMeshForRegion(regionUUID);
}

std::string LLPathfindingManager::getNavMeshStatusURLForCurrentRegion() const
{
    return getNavMeshStatusURLForRegion(getCurrentRegion());
}

std::string LLPathfindingManager::getNavMeshStatusURLForRegion(const std::optional<VSRegionContext>& pRegion) const
{
    return getCapabilityURLForRegion(pRegion, CAP_SERVICE_NAVMESH_STATUS);
}

std::string LLPathfindingManager::getRetrieveNavMeshURLForRegion(const std::optional<VSRegionContext>& pRegion) const
{
    return getCapabilityURLForRegion(pRegion, CAP_SERVICE_RETRIEVE_NAVMESH);
}

std::string LLPathfindingManager::getRetrieveObjectLinksetsURLForCurrentRegion() const
{
    return getCapabilityURLForCurrentRegion(CAP_SERVICE_GET_OBJECT_LINKSETS);
}

std::string LLPathfindingManager::getChangeObjectLinksetsURLForCurrentRegion() const
{
    return getCapabilityURLForCurrentRegion(CAP_SERVICE_SET_OBJECT_LINKSETS);
}

std::string LLPathfindingManager::getTerrainLinksetsURLForCurrentRegion() const
{
    return getCapabilityURLForCurrentRegion(CAP_SERVICE_TERRAIN_LINKSETS);
}

std::string LLPathfindingManager::getCharactersURLForCurrentRegion() const
{
    return getCapabilityURLForCurrentRegion(CAP_SERVICE_CHARACTERS);
}

std::string LLPathfindingManager::getAgentStateURLForRegion(const std::optional<VSRegionContext>& pRegion) const
{
    return getCapabilityURLForRegion(pRegion, CAP_SERVICE_AGENT_STATE);
}

std::string LLPathfindingManager::getCapabilityURLForCurrentRegion(const std::string &pCapabilityName) const
{
    return getCapabilityURLForRegion(getCurrentRegion(), pCapabilityName);
}

std::string LLPathfindingManager::getCapabilityURLForRegion(const std::optional<VSRegionContext>& pRegion, const std::string &pCapabilityName) const
{
    std::string capabilityURL("");

    if (bool(pRegion))
    {
        capabilityURL = pRegion->getCapability(pCapabilityName);
    }

    if (capabilityURL.empty())
    {
        LL_WARNS() << "cannot find capability '" << pCapabilityName << "' for current region '"
            << ((bool(pRegion)) ? pRegion->getName() : "<null>") << "'" << LL_ENDL;
    }

    return capabilityURL;
}

std::optional<VSRegionContext> LLPathfindingManager::getCurrentRegion() const
{
    return vs_current_region_context();
}

//---------------------------------------------------------------------------
// LLNavMeshSimStateChangeNode
//---------------------------------------------------------------------------

void LLNavMeshSimStateChangeNode::post(ResponsePtr pResponse, const LLSD &pContext, const LLSD &pInput) const
{
#if VS_NATIVE_VULKAN
    if (const auto owner = VSNativeSession::active())
    {
        const auto region = vs_region_context(LLHost(pInput["sender"].asString()));
        const auto& body = pInput[SIM_MESSAGE_BODY_FIELD];
        if (!region || !region->current() || !body.isMap() ||
            body["region_id"].asUUID() != region->getRegionID() ||
            !body["version"].isInteger() || body["version"].asInteger() < 0 || !body["status"].isString()) return;
    }
#endif

    llassert(pInput.has(SIM_MESSAGE_BODY_FIELD));
    llassert(pInput.get(SIM_MESSAGE_BODY_FIELD).isMap());
    LLPathfindingNavMeshStatus navMeshStatus(pInput.get(SIM_MESSAGE_BODY_FIELD));
    LLPathfindingManager::getInstance()->handleNavMeshStatusUpdate(navMeshStatus);
}

//---------------------------------------------------------------------------
// LLAgentStateChangeNode
//---------------------------------------------------------------------------

void LLAgentStateChangeNode::post(ResponsePtr pResponse, const LLSD &pContext, const LLSD &pInput) const
{
#if VS_NATIVE_VULKAN
    if (const auto owner = VSNativeSession::active())
    {
        if (!owner->regionReady() || LLHost(pInput["sender"].asString()) != owner->host() ||
            !pInput[SIM_MESSAGE_BODY_FIELD].isMap() ||
            !pInput[SIM_MESSAGE_BODY_FIELD][AGENT_STATE_CAN_REBAKE_REGION_FIELD].isBoolean()) return;
    }
#endif

    llassert(pInput.has(SIM_MESSAGE_BODY_FIELD));
    llassert(pInput.get(SIM_MESSAGE_BODY_FIELD).isMap());
    llassert(pInput.get(SIM_MESSAGE_BODY_FIELD).has(AGENT_STATE_CAN_REBAKE_REGION_FIELD));
    llassert(pInput.get(SIM_MESSAGE_BODY_FIELD).get(AGENT_STATE_CAN_REBAKE_REGION_FIELD).isBoolean());
    bool canRebakeRegion = pInput.get(SIM_MESSAGE_BODY_FIELD).get(AGENT_STATE_CAN_REBAKE_REGION_FIELD).asBoolean();

    LLPathfindingManager::getInstance()->handleAgentState(canRebakeRegion);
}

//---------------------------------------------------------------------------
// LinksetsResponder
//---------------------------------------------------------------------------
LinksetsResponder::LinksetsResponder(LLPathfindingManager::request_id_t pRequestId, LLPathfindingManager::object_request_callback_t pLinksetsCallback, bool pIsObjectRequested, bool pIsTerrainRequested)
    : mRequestId(pRequestId),
    mLinksetsCallback(pLinksetsCallback),
    mObjectMessagingState(pIsObjectRequested ? kWaiting : kNotRequested),
    mTerrainMessagingState(pIsTerrainRequested ? kWaiting : kNotRequested),
    mObjectLinksetListPtr(),
    mTerrainLinksetPtr()
{
}

LinksetsResponder::~LinksetsResponder()
{
}

void LinksetsResponder::handleObjectLinksetsResult(const LLSD &pContent)
{
    mObjectLinksetListPtr = LLPathfindingObjectListPtr(new LLPathfindingLinksetList(pContent));

    mObjectMessagingState = kReceivedGood;
    if (mTerrainMessagingState != kWaiting)
    {
        sendCallback();
    }
}

void LinksetsResponder::handleObjectLinksetsError()
{
    LL_WARNS() << "LinksetsResponder object linksets error" << LL_ENDL;
    mObjectMessagingState = kReceivedError;
    if (mTerrainMessagingState != kWaiting)
    {
        sendCallback();
    }
}

void LinksetsResponder::handleTerrainLinksetsResult(const LLSD &pContent)
{
    mTerrainLinksetPtr = LLPathfindingObjectPtr(new LLPathfindingLinkset(pContent));

    mTerrainMessagingState = kReceivedGood;
    if (mObjectMessagingState != kWaiting)
    {
        sendCallback();
    }
}

void LinksetsResponder::handleTerrainLinksetsError()
{
    LL_WARNS() << "LinksetsResponder terrain linksets error" << LL_ENDL;
    mTerrainMessagingState = kReceivedError;
    if (mObjectMessagingState != kWaiting)
    {
        sendCallback();
    }
}

void LinksetsResponder::sendCallback()
{
    llassert(mObjectMessagingState != kWaiting);
    llassert(mTerrainMessagingState != kWaiting);
    LLPathfindingManager::ERequestStatus requestStatus =
        ((((mObjectMessagingState == kReceivedGood) || (mObjectMessagingState == kNotRequested)) &&
          ((mTerrainMessagingState == kReceivedGood) || (mTerrainMessagingState == kNotRequested))) ?
         LLPathfindingManager::kRequestCompleted : LLPathfindingManager::kRequestError);

    if (mObjectMessagingState != kReceivedGood)
    {
        mObjectLinksetListPtr = LLPathfindingObjectListPtr(new LLPathfindingLinksetList());
    }

    if (mTerrainMessagingState == kReceivedGood)
    {
        mObjectLinksetListPtr->update(mTerrainLinksetPtr);
    }

    mLinksetsCallback(mRequestId, requestStatus, mObjectLinksetListPtr);
}


std::function<void(const LLUUID&)> LLPathfindingManager::guardedDeferred(
    const std::optional<VSRegionContext>& region, std::function<void(const LLUUID&)> callback, std::function<bool()> original) const
{
    const auto current = original ? std::move(original) : requestGuard(region);
    return [current, callback = std::move(callback)](const LLUUID& id) { if (current()) callback(id); };
}

void LLPathfindingManager::resetAccount()
{
    *mLifetime = false;
    mLifetime = std::make_shared<bool>(true);
    mNavMeshIntents.clear();
    mNavMeshMap.clear();
}

std::function<bool()> LLPathfindingManager::requestGuard(const std::optional<VSRegionContext>& region) const
{
    const std::weak_ptr<bool> lifetime = mLifetime;
    const auto account = vs_native_im_guard();
    return [lifetime, account, region]
    {
        const auto state = lifetime.lock();
        return state && *state && account() && region && region->current();
    };
}

namespace
{
std::optional<VSRegionContext> pathfindingRegion(LLViewerRegion* region)
{
    if (region) return vs_region_context(region->getHost());
#if VS_NATIVE_VULKAN
    if (VSNativeSession::active()) return vs_current_region_context();
#endif
    return {};
}
}
bool LLPathfindingManager::isPathfindingEnabledForRegion(LLViewerRegion* region) const
{ return isPathfindingEnabledForRegion(pathfindingRegion(region)); }
LLPathfindingNavMesh::navmesh_slot_t LLPathfindingManager::registerNavMeshListenerForRegion(LLViewerRegion* region,
    LLPathfindingNavMesh::navmesh_callback_t callback)
{ return registerNavMeshListenerForRegion(pathfindingRegion(region), std::move(callback)); }
void LLPathfindingManager::requestGetNavMeshForRegion(LLViewerRegion* region, bool statusOnly)
{ requestGetNavMeshForRegion(pathfindingRegion(region), statusOnly); }
