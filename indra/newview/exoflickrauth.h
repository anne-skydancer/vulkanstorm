/**
 * @file exoflickrauth.h
 * @brief Handles all Flickr authentication issues
 *
 * $LicenseInfo:firstyear=2012&license=viewerlgpl$
 * Copyright (C) 2012 Katharine Berry
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * $/LicenseInfo$
 */

#include <functional>
#include <memory>

// This class's function is described by the flow chart at
// https://www.flickr.com/services/api/auth.oauth.html
class exoFlickrAuth : public std::enable_shared_from_this<exoFlickrAuth>
{
public:
    typedef std::function<void(bool success, const LLSD& params)> authorized_callback_t;

    static void create(authorized_callback_t callback);
    static void retireAccount();
    ~exoFlickrAuth();

private:
    authorized_callback_t mCallback;
    explicit exoFlickrAuth(authorized_callback_t callback);
    std::function<bool()> mCurrent;
    bool mRetired = false;
    static std::shared_ptr<exoFlickrAuth> sOwner;
    bool current() const { return !mRetired && mCurrent(); }
    void complete(bool success, const LLSD& response);

    void beginAuthorisation();
    void checkAuthorisation();
    void checkResult(bool success, const LLSD& response);
    void explanationCallback(const LLSD& notification, const LLSD& response);

    void gotRequestToken(bool success, const LLSD& params); // Stage 1 response (HTTP)
    void gotVerifier(const LLSD& notification, const LLSD& response); // Stage 2 response (LLNotification)
    void gotAccessToken(bool success, const LLSD& params); // Stage 3 response (HTTP)
};
