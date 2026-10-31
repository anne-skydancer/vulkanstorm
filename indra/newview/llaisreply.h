/**
 * @file llaisreply.h
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

#ifndef LL_LLAISREPLY_H
#define LL_LLAISREPLY_H

#include "llcorehttputil.h"
#include "llsd.h"
#include "vkstorm_ffi.h"

#include <memory>

// Inventory rung R3a (vkstorm-rust rung seven). AIS replies are not parsed
// into C++ LLSD trees: the AIS adapter variant hands the raw response to
// Rust (vks_inv_ais_reply_build), which builds the result the adapter's LLSD
// handler would have posted (the http_result block, the 499 of an
// unparsable application/llsd+xml body, the depth cap) as a Rust value. The
// coroutine and AISUpdate read that value through vkstorm-ffi; a C++ LLSD
// tree is built from it only where C++ code wants one (logging).

// The depth limit of an AIS reply (LLSDParser's max_depth): a value nested
// this many containers deep makes the body unparsable, as malformed XML does
// (an empty result; status 499 on a success with the application/llsd+xml
// content type). A depth-50 fetch nests about 160 levels; the limit keeps a
// hostile reply from building, walking and freeing a tree too deep for the
// stack (approved fix, 2026-10-29).
constexpr S32 AIS_MAX_REPLY_DEPTH = 256;
// HttpCoroHandler::onCompleted's MAX_BODY_SIZE_THRESHOLD: a success body
// larger than this is parsed on the General work queue.
constexpr size_t AIS_WORKER_THRESHOLD = 65536;

// The result of one AIS request, Rust-owned (move-only).
class AISReply
{
public:
    AISReply() = default;
    explicit AISReply(VksInvAisReply* reply) : mReply(reply) {}
    AISReply(AISReply&& other) noexcept : mReply(other.mReply) { other.mReply = nullptr; }
    AISReply& operator=(AISReply&& other) noexcept;
    AISReply(const AISReply&) = delete;
    AISReply& operator=(const AISReply&) = delete;
    ~AISReply();

    explicit operator bool() const { return mReply != nullptr; }
    VksInvAisReply* get() const { return mReply; }
    // Gives up ownership (to cross a work queue as plain data).
    VksInvAisReply* release()
    {
        VksInvAisReply* r = mReply;
        mReply = nullptr;
        return r;
    }

    // The status the reply carries, for logging.
    LLCore::HttpStatus status() const;
    // The value as an LLSD tree (built on demand: logging, tests).
    LLSD asLLSD() const;

    // What the adapter's LLSD handler would have posted for this response,
    // with the AIS depth cap; empty when it posts nothing (the reply to a
    // canceled request). Safe on any thread.
    static AISReply fromResponse(LLCore::HttpResponse* response, S32 max_depth = AIS_MAX_REPLY_DEPTH);
    // A result the adapter produced as LLSD without calling its handler (an
    // immediate error), or a scripted one (the R3a oracle).
    static AISReply fromLLSD(const LLSD& result);

private:
    VksInvAisReply* mReply = nullptr;
};

// The AIS variant of the adapter's LLSD handler: it keeps the reply in
// Rust and posts an empty map to the coroutine's pump, which reads the
// reply from the handler (AISReplySlot). The scheduling is the base
// handler's: a success body over AIS_WORKER_THRESHOLD is parsed on the
// General work queue (the reply is Rust data, safe to build there) and the
// pump fires from the main loop; the late reply to a canceled request is
// ignored.
class AISReplyHandler : public LLCoreHttpUtil::HttpCoroHandler
{
public:
    explicit AISReplyHandler(LLEventStream& reply);

    void onCompleted(LLCore::HttpHandle handle, LLCore::HttpResponse* response) override;

    // The reply, once the pump has fired; empty before.
    AISReply takeReply() { return std::move(mReply); }

protected:
    // Not used: onCompleted does the work.
    LLSD handleSuccess(LLCore::HttpResponse* response, LLCore::HttpStatus& status) const override { return LLSD(); }
    LLSD parseBody(LLCore::HttpResponse* response, bool& success) const override
    {
        success = true;
        return LLSD();
    }

private:
    void deliver(LLCore::HttpResponse* response, AISReply reply);

    AISReply mReply;
};

// Where a coroutine finds the handler of its request: install factory() on
// the adapter before the request, reset it after.
struct AISReplySlot
{
    std::shared_ptr<AISReplyHandler> handler;

    static LLCoreHttpUtil::HttpCoroutineAdapter::llsd_handler_factory_t factory(
        const std::shared_ptr<AISReplySlot>& slot);
};

#endif // LL_LLAISREPLY_H
