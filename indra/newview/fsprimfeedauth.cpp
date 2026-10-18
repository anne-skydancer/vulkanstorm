/**
 * @file fsprimfeedauth.cpp
 * @file fsprimfeedauth.h
 * @brief Primfeed Authorisation workflow class
 * @author beq@firestorm
 * $LicenseInfo:firstyear=2025&license=fsviewerlgpl$
 * Phoenix Firestorm Viewer Source Code
 * Copyright (C) 2025, Beq Janus
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

/* 
 * Handles Primfeed authentication and authorisation through a multi-factor OAuth flow.
 *
 * This module integrates with Primfeed’s Third Party Viewers API.
 * The authentication flow is as follows:
 *   1. Initiate a login request:
 *      POST https://api.primfeed.com/pf/viewer/create-login-request
 *      Headers:
 *         pf-viewer-api-key: <viewer_api_key>
 *         pf-user-uuid: <avatar_uuid>
 *      Response:
 *         { "requestId": "<64-char string>" }
 *
 *   2. Redirect the user to:
 *      https://www.primfeed.com/oauth/viewer?r=<requestId>&v=<viewer_api_key>
 *
 *   3. The user is shown an approval screen. When they click Authorize,
 *      an in-world message is sent:
 *         #PRIMFEED_OAUTH: <oauth_token>
 *      We intercept this code through an onChat handle then call onOauthTokenReceived().
 *
 *   4. Validate the login request:
 *      POST https://api.primfeed.com/pf/viewer/validate-request
 *      Headers:
 *         Authorization: Bearer <oauth_token>
 *         pf-viewer-api-key: <viewer_api_key>
 *         pf-viewer-request-id: <requestId>
 *      Response: HTTP 204
 *
 *   5. Optionally, check user status:
 *      GET https://api.primfeed.com/pf/viewer/user
 *      Headers:
 *         Authorization: Bearer <oauth_token>
 *         pf-viewer-api-key: <viewer_api_key>
 *      Response: { "plan": "free" } (or "pro")
 */
#include "llviewerprecompiledheaders.h"
#include "fsprimfeedauth.h"
#include "vsnativeim.h"
#include "fsprimfeedconnect.h"
#include "llimview.h"
#include "llnotificationsutil.h"
#include "llfloaterimnearbychathandler.h"
#include "llnotificationmanager.h"
#include "llagent.h"
#include "llevents.h"
#include "fscorehttputil.h"
#include "llwindow.h"
#include "llviewerwindow.h"
#include "lluri.h"
#include "llsdjson.h"
#include <string_view>

using Callback = FSPrimfeedAuth::authorized_callback_t;

// private instance variable
std::shared_ptr<FSPrimfeedAuth> FSPrimfeedAuth::sPrimfeedAuth;
std::unique_ptr<LLEventPump> FSPrimfeedAuth::sPrimfeedAuthPump = std::make_unique<LLEventStream>("PrimfeedAuthResponse");

// Helper callback that unpacks HTTP POST response data.
void FSPrimfeedAuthResponse(LLSD const &aData, Callback callback)
{
    LLSD header = aData[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS][LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS_HEADERS];
    LLCore::HttpStatus status = LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(
        aData[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS]);

    const LLSD::Binary &rawData = aData[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS_RAW].asBinary();
    std::string result;
    result.assign(rawData.begin(), rawData.end());

    // Assume JSON response.

    LLSD resultLLSD;
    if(!result.empty())
    {
        boost::system::error_code error;
        const auto json = boost::json::parse(result, error);
        if (error) { callback(false, LLSD()); return; }
        resultLLSD = LlsdFromJson(json);
    }
    callback((status.getType() == HTTP_OK ||
            status.getType() == HTTP_NO_CONTENT), resultLLSD);
}

void FSPrimfeedAuth::initiateAuthRequest()
{
    // This function is called to initiate the authentication request.
    // It should be called when the user clicks the "Authenticate" button.
    // Also triggered on opening the floater.
    // The actual implementation is in the create() method.

    if (sPrimfeedAuth)
    {
        LLNotificationsUtil::add("PrimfeedAuthorizationAlreadyInProgress");
        return;
    }
    // If no token stored, begin the login request; otherwise check user status.
    FSPrimfeedAuth::create(
        [](bool success, const LLSD &response) 
        {
            LLSD event_data = response;
            event_data["responseType"] = "primfeed_auth_response";
            event_data["success"] = success;
            sPrimfeedAuthPump->post(event_data);

        }
    );
}

void FSPrimfeedAuth::resetAuthStatus()
{
    retireAccount();
    gSavedPerAccountSettings.setString("FSPrimfeedOAuthToken", "");
    gSavedPerAccountSettings.setString("FSPrimfeedProfileLink", "");
    gSavedPerAccountSettings.setString("FSPrimfeedPlan", "");
    gSavedPerAccountSettings.setString("FSPrimfeedUsername", "");
    LLSD event_data;
    event_data["responseType"] = "primfeed_auth_reset";
    event_data["success"] = "false";
    sPrimfeedAuthPump->post(event_data);
    FSPrimfeedConnect::instance().setConnectionState(FSPrimfeedConnect::PRIMFEED_DISCONNECTED);
}


FSPrimfeedAuth::FSPrimfeedAuth(authorized_callback_t callback)
    : mCallback(std::move(callback)), mCurrent(vs_native_im_guard())
{
}

FSPrimfeedAuth::~FSPrimfeedAuth()
{
    retire();
}

void FSPrimfeedAuth::retire()
{
    mRetired = true;
    mCallback = {};
    mOauthToken.clear();
    mRequestId.clear();
    mInstantMessageConnection.disconnect();
    mChatMessageConnection.disconnect();
}

void FSPrimfeedAuth::retireAccount()
{
    const auto previous = std::exchange(sPrimfeedAuth, {});
    if (previous) previous->retire();
    if (FSPrimfeedConnect::instanceExists())
        FSPrimfeedConnect::instance().setConnectionState(FSPrimfeedConnect::PRIMFEED_DISCONNECTED);
}

void FSPrimfeedAuth::complete(bool success, const LLSD& response)
{
    if (!current()) return;
    const auto keep = shared_from_this();
    mCompleted = true;
    mInstantMessageConnection.disconnect();
    mChatMessageConnection.disconnect();
    auto callback = std::exchange(mCallback, {});
    if (sPrimfeedAuth.get() == this) sPrimfeedAuth.reset();
    if (callback) callback(success, response);
}

// Factory method to create a shared pointer to FSPrimfeedAuth.
std::shared_ptr<FSPrimfeedAuth> FSPrimfeedAuth::create(authorized_callback_t callback)
{
    // Ensure only one authentication attempt is in progress.
    if (sPrimfeedAuth)
    {
        // Already in progress; return the existing instance.
        return sPrimfeedAuth;
    }
    auto auth = std::shared_ptr<FSPrimfeedAuth>(new FSPrimfeedAuth(callback));
    if (!auth)
    {
        return nullptr;
    }

    // Publish the owner before a request can synchronously complete or reenter.
    sPrimfeedAuth = auth;
    const auto weak = std::weak_ptr<FSPrimfeedAuth>(auth);
    auth->mChatMessageConnection = LLNotificationsUI::LLNotificationManager::instance().getChatHandler()->addNewChatCallback(
        [weak](const LLSD& message)
        {
            if (const auto owner=weak.lock(); owner && owner->current()) owner->onChatMessage(message);
        });
    FSPrimfeedConnect::instance().setConnectionState(FSPrimfeedConnect::PRIMFEED_CONNECTING);

    // If no token stored, begin the login request; otherwise check user status.
    auth->mOauthToken = gSavedPerAccountSettings.getString("FSPrimfeedOAuthToken");
    if (auth->mOauthToken.empty())
    {
        auth->beginLoginRequest();
    }
    else
    {
        auth->checkUserStatus();
    }
    return auth;
}

void FSPrimfeedAuth::beginLoginRequest()
{
    if (!current()) return;
    // Get our API key and user UUID.
    std::string viewer_api_key = gSavedSettings.getString("FSPrimfeedViewerApiKey");
    std::string user_uuid = gAgent.getID().asString();

    std::string url = "https://api.primfeed.com/pf/viewer/create-login-request";
    std::string post_data = ""; // No body parameters required.

    // Create the headers object.
    LLCore::HttpHeaders::ptr_t pHeader = std::make_shared<LLCore::HttpHeaders>();
    LLCore::HttpOptions::ptr_t options = std::make_shared<LLCore::HttpOptions>();

    pHeader->append(HTTP_OUT_HEADER_USER_AGENT, FS_PF_USER_AGENT);
    pHeader->append("pf-viewer-api-key", viewer_api_key);
    pHeader->append("pf-user-uuid", user_uuid);

    // Set up HTTP options
    options->setWantHeaders(true);
    options->setRetries(0);
    options->setTimeout(PRIMFEED_CONNECT_TIMEOUT);

    // Capture shared_ptr to self
    auto self = shared_from_this();

    const auto end(pHeader->end());
    for (auto it(pHeader->begin()); end != it; ++it)
    {
        LL_DEBUGS("Primfeed") << "Header: " << it->first << " = " << it->second << LL_ENDL;
    }

    // Pass both success and failure callbacks
    FSCoreHttpUtil::callbackHttpPostRaw(
        url, 
        post_data, 
        [self](LLSD const &aData) {
            if (!self->current()) return;
            LL_DEBUGS("FSPrimfeedAuth") << "Login request response(OK): " << aData << LL_ENDL;
            FSPrimfeedAuthResponse(aData,
                [self](bool success, const LLSD &response) {
                    self->gotRequestId(success, response);
                }
            );
        },
        [self](LLSD const &aData) {
            if (!self->current()) return;
            LL_DEBUGS("FSPrimfeedAuth") << "Login request response(FAIL): " << aData << LL_ENDL;
            FSPrimfeedAuthResponse(aData,
                [self](bool success, const LLSD &response) {
                    self->gotRequestId(success, response);
                }
            );
        },
        pHeader, 
        options
    );
}

void FSPrimfeedAuth::gotRequestId(bool success, const LLSD &response)
{
    if (!current()) return;
    if (!success)
    {
        LLNotificationsUtil::add("PrimfeedLoginRequestFailed");
        complete(false, LLSD());
        return;
    }
    mRequestId = response["requestId"].asString();
    if (mRequestId.empty())
    {
        LLNotificationsUtil::add("PrimfeedLoginRequestFailed");
        complete(false, LLSD());
        return;
    }
    // Open the browser for user approval.
    std::string viewer_api_key = gSavedSettings.getString("FSPrimfeedViewerApiKey");
    std::string auth_url = "https://www.primfeed.com/oauth/viewer?r=" + mRequestId + "&v=" + viewer_api_key;
    gViewerWindow->getWindow()->spawnWebBrowser(auth_url, true);
    
}

/// This function is called by the chat interceptor when the message
/// "#PRIMFEED_OAUTH: <oauth_token>" is intercepted.
void FSPrimfeedAuth::onOauthTokenReceived(const std::string_view& oauth_token)
{
    if (!current()) return;
    if (oauth_token.empty())
    {
        complete(false, LLSD());
        return;
    }
    mOauthToken = oauth_token;
    validateRequest();
}

void FSPrimfeedAuth::onChatMessage(const LLSD& message)
{
    if (!current()) return;
    constexpr std::string_view oauth_msg_prefix = "#PRIMFEED_OAUTH: ";
    const std::string msg = message["message"].asString();
    if (msg.find(std::string(oauth_msg_prefix)) == 0)
    {
        std::string_view oauth_token(msg.data() + oauth_msg_prefix.size(), msg.size() - oauth_msg_prefix.size());
        LL_DEBUGS("Primfeed") << "Received OAuth token: " << msg << "extracted:<" << oauth_token << ">" << LL_ENDL;
        onOauthTokenReceived(oauth_token);
    }
}


void FSPrimfeedAuth::validateRequest()
{
    if (!current()) return;
    // No POST body needed.
    std::string post_data = "";
    std::string url = "https://api.primfeed.com/pf/viewer/validate-request";

    // Retrieve the viewer API key.
    std::string viewer_api_key = gSavedSettings.getString("FSPrimfeedViewerApiKey");

    // Create and populate the headers.
    LLCore::HttpHeaders::ptr_t pHeader = std::make_shared<LLCore::HttpHeaders>();
    pHeader->append(HTTP_OUT_HEADER_USER_AGENT, FS_PF_USER_AGENT);
    pHeader->append("Authorization", "Bearer " + mOauthToken);
    pHeader->append("pf-viewer-api-key", viewer_api_key);
    pHeader->append("pf-viewer-request-id", mRequestId);

    // Set HTTP options
    LLCore::HttpOptions::ptr_t options = std::make_shared<LLCore::HttpOptions>();
    options->setWantHeaders(true);
    options->setRetries(0);
    options->setTimeout(PRIMFEED_CONNECT_TIMEOUT); 

    // print out pHeader for debuging using iterating over pHeader and using LL_DEBUGS
    const auto end(pHeader->end());
    for (auto it(pHeader->begin()); end != it; ++it)
    {
        LL_DEBUGS("Primfeed") << "Header: " << it->first << " = " << it->second << LL_ENDL;
    }
    
    auto self = shared_from_this();
    try
    {
        FSCoreHttpUtil::callbackHttpPostRaw(
            url,
            post_data,
            [self](LLSD const &aData) {
            if (!self->current()) return;
                LL_DEBUGS("FSPrimfeedAuth") << "Validation-request response(OK): " << aData << LL_ENDL;
                FSPrimfeedAuthResponse(aData,
                    [self](bool success, const LLSD &response) {
                        self->gotValidateResponse(success, response);
                    }
                );
            },
            [self](LLSD const &aData) {
            if (!self->current()) return;
                LL_INFOS("FSPrimfeedAuth") << "Validation-request response(FAIL): " << aData << LL_ENDL;
                FSPrimfeedAuthResponse(aData,
                    [self](bool success, const LLSD &response) {
                        self->gotValidateResponse(success, response);
                    }
                );
            },
            pHeader,
            options
        );
    }
    catch(const std::exception& e)
    {
        LL_WARNS("Primfeed") << "Primfeed validation failed " << e.what() << LL_ENDL;
    }
    
}


void FSPrimfeedAuth::gotValidateResponse(bool success, const LLSD &response)
{
    if (!current()) return;
    if (!success)
    {
        LLNotificationsUtil::add("PrimfeedValidateFailed");
        complete(false, response);
        return;
    }
    checkUserStatus();
}

void FSPrimfeedAuth::checkUserStatus()
{
    if (!current()) return;
    std::string viewer_api_key = gSavedSettings.getString("FSPrimfeedViewerApiKey");

    // Build the base URL without query parameters.
    std::string url = "https://api.primfeed.com/pf/viewer/user";
    LL_DEBUGS("Primfeed") << "URL: " << url << LL_ENDL;

    // Create and populate the headers.
    LLCore::HttpHeaders::ptr_t pHeader = std::make_shared<LLCore::HttpHeaders>();
    pHeader->append(HTTP_OUT_HEADER_USER_AGENT, FS_PF_USER_AGENT);
    pHeader->append("Authorization", "Bearer " + mOauthToken);
    pHeader->append("pf-viewer-api-key", viewer_api_key);

    // Set HTTP options.
    LLCore::HttpOptions::ptr_t options = std::make_shared<LLCore::HttpOptions>();
    options->setWantHeaders(true);
    options->setRetries(0);
    options->setTimeout(PRIMFEED_CONNECT_TIMEOUT);

    const auto self = shared_from_this();
    // Make the HTTP GET request, passing in the headers and options.
    FSCoreHttpUtil::callbackHttpGetRaw(
        url,
        [self](LLSD const &aData) {
            if (!self->current()) return;
            LL_DEBUGS("FSPrimfeedAuth") << "Check-user-status response: " << aData << LL_ENDL;
            FSPrimfeedAuthResponse(aData, [self](bool success, const LLSD &response) {
                self->gotUserStatus(success, response);
            });
        },
        [self](LLSD const &aData) {
            if (!self->current()) return;
            LL_INFOS("FSPrimfeedAuth") << "Check-user-status response (failure): " << aData << LL_ENDL;
            // Optionally, call the same processing for failure or handle separately.
            FSPrimfeedAuthResponse(aData, [self](bool success, const LLSD &response){
                self->gotUserStatus(success, response);
            });
        },
        pHeader,
        options
    );
}


void FSPrimfeedAuth::gotUserStatus(bool success, const LLSD &response)
{
    if (!current()) return;
    LL_INFOS("Primfeed") << "User status: " << response << "(" << success << ")" << LL_ENDL;
    if (success && response.has("plan") && response.has("username") && response.has("link"))
    {
        gSavedPerAccountSettings.setString("FSPrimfeedOAuthToken", mOauthToken);
        gSavedPerAccountSettings.setString("FSPrimfeedPlan", response["plan"].asString());
        gSavedPerAccountSettings.setString("FSPrimfeedProfileLink", response["link"].asString());
        gSavedPerAccountSettings.setString("FSPrimfeedUsername", response["username"].asString());
        FSPrimfeedConnect::instance().setConnectionState(FSPrimfeedConnect::PRIMFEED_CONNECTED);
        LLSD event_data = response;
        event_data["responseType"] = "primfeed_user_info";
        sPrimfeedAuthPump->post(event_data);        
        complete(true, response);
        return;
    }
    LLNotificationsUtil::add("PrimfeedUserStatusFailed");
    FSPrimfeedConnect::instance().setConnectionState(FSPrimfeedConnect::PRIMFEED_DISCONNECTED);
    complete(false, response);
}
