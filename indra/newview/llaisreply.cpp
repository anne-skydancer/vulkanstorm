/**
 * @file llaisreply.cpp
 * @brief AIS replies in Rust: the AIS-only variant of the coroutine
 *        adapter's handler and the reply it hands the coroutine.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
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
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"

#include "llaisreply.h"

#include "bufferarray.h"
#include "httpheaders.h"
#include "httpresponse.h"
#include "llsdserialize.h"

#include <sstream>
#include <string>
#include <vector>

namespace
{
    VksInvBytes bytes_of(const std::string& s)
    {
        return VksInvBytes{ reinterpret_cast<const uint8_t*>(s.data()), s.size() };
    }

    std::string string_of(const VksInvBytes& b)
    {
        return b.len ? std::string(reinterpret_cast<const char*>(b.ptr), b.len) : std::string();
    }
}

AISReply& AISReply::operator=(AISReply&& other) noexcept
{
    if (this != &other)
    {
        vks_inv_ais_reply_free(mReply);
        mReply = other.mReply;
        other.mReply = nullptr;
    }
    return *this;
}

AISReply::~AISReply()
{
    vks_inv_ais_reply_free(mReply);
}

LLCore::HttpStatus AISReply::status() const
{
    if (!mReply)
    {
        return LLCore::HttpStatus(HTTP_INTERNAL_ERROR, "Malformed response contents");
    }
    VksInvAisStatus s{};
    vks_inv_ais_reply_status(mReply, &s);
    if (s.kind >= 100 && s.kind <= 999)
    {
        // An HTTP status: its status bits follow from the code, as the
        // adapter's were made.
        return LLCore::HttpStatus((int)s.kind, string_of(s.message));
    }
    return LLCore::HttpStatus((LLCore::HttpStatus::type_enum_t)s.kind, (short)s.status);
}

LLSD AISReply::asLLSD() const
{
    LLSD sd;
    VksInvAisBytesBlock* block = mReply ? vks_inv_ais_reply_to_binary(mReply) : nullptr;
    if (block)
    {
        const VksInvBytes b = vks_inv_ais_bytes_view(block);
        std::istringstream in(std::string(reinterpret_cast<const char*>(b.ptr), b.len));
        LLSDSerialize::fromBinary(sd, in, b.len);
        vks_inv_ais_bytes_free(block);
    }
    return sd;
}

// static
AISReply AISReply::fromResponse(LLCore::HttpResponse* response, S32 max_depth)
{
    const LLCore::HttpStatus status = response->getStatus();
    const std::string message = status.getMessage();
    const std::string url = response->getRequestURL();

    std::vector<VksInvAisHeader> headers;
    LLCore::HttpHeaders::ptr_t hdrs = response->getHeaders();
    if (hdrs)
    {
        for (LLCore::HttpHeaders::iterator it = hdrs->begin(); it != hdrs->end(); ++it)
        {
            headers.push_back(VksInvAisHeader{ bytes_of(it->first), bytes_of(it->second) });
        }
    }

    // The body as one block: BufferArray keeps it in segments.
    std::string body;
    LLCore::BufferArray* ba = response->getBody();
    if (ba && ba->size())
    {
        body.resize(ba->size());
        body.resize(ba->read(0, body.data(), body.size()));
    }

    VksInvAisResponse r{};
    r.status.kind = status.getType();
    r.status.status = status.getStatus();
    r.status.message = bytes_of(message);
    r.url = bytes_of(url);
    r.headers = headers.empty() ? nullptr : headers.data();
    r.header_count = headers.size();
    r.has_body = (ba != nullptr);
    r.body = bytes_of(body);
    r.max_depth = max_depth;
    return AISReply(vks_inv_ais_reply_build(&r));
}

// static
AISReply AISReply::fromLLSD(const LLSD& result)
{
    std::ostringstream out;
    LLSDSerialize::toBinary(result, out);
    const std::string b = out.str();
    return AISReply(vks_inv_ais_reply_from_binary(reinterpret_cast<const uint8_t*>(b.data()), b.size()));
}

AISReplyHandler::AISReplyHandler(LLEventStream& reply) :
    LLCoreHttpUtil::HttpCoroHandler(reply)
{
}

void AISReplyHandler::onCompleted(LLCore::HttpHandle handle, LLCore::HttpResponse* response)
{
    LLCore::HttpStatus status = response->getStatus();
    if (status == LLCore::HttpStatus(LLCore::HttpStatus::LLCORE, LLCore::HE_HANDLE_NOT_FOUND))
    {
        // A response came in for a canceled request and we have not processed
        // the cancel yet (the base handler's rule).
        return;
    }
    if (!status)
    {
        LL_WARNS("CoreHTTP") << "Possible failure [" << status.toTerseString() << "] cannot " << response->getRequestMethod()
                             << " url '" << response->getRequestURL() << "' because " << status.toString() << LL_ENDL;
    }

    mReply = AISReply::fromResponse(response, AIS_MAX_REPLY_DEPTH);

    LLSD posted = LLSD::emptyMap();
    if (!mReply)
    {
        // Rust failed (vks_inv_last_error): the coroutine falls back to this
        // status block, as for an adapter's immediate error.
        LL_WARNS("CoreHTTP") << "AIS reply not built: " << vks_inv_last_error() << LL_ENDL;
        LLSD httpresults = LLSD::emptyMap();
        writeStatusCodes(status, response->getRequestURL(), httpresults);
        posted[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS] = httpresults;
    }
    else if (status && !mReply.status())
    {
        LL_WARNS("CoreHTTP") << "Failed to deserialize . " << response->getRequestURL() << " [status:" << status.toString()
                             << "] " << LL_ENDL;
    }
    getReplyPump().post(posted);
}

// static
LLCoreHttpUtil::HttpCoroutineAdapter::llsd_handler_factory_t AISReplySlot::factory(
    const std::shared_ptr<AISReplySlot>& slot)
{
    return [slot](LLEventStream& reply) -> LLCoreHttpUtil::HttpCoroHandler::ptr_t
    {
        slot->handler = std::make_shared<AISReplyHandler>(reply);
        return slot->handler;
    };
}
