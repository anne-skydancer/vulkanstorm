/**
 * @file llinventory_task_oracle_fakes.cpp
 * @brief The fakes behind the frozen task-inventory copies
 * (llinventory_task_oracle.h): viewer singletons, the coroutine launcher and
 * HTTP adapter on real fibers, the xfer manager (fake or real) and the
 * message-system send hook. Each fake records its side effects in order.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#include "linden_common.h"
#include "llinventory_task_oracle.h"

#include "llcorehttputil.h"
#include "lltemplatemessagereader.h"
#include "llxfermanager.h"
#include "net.h"

#include <boost/fiber/all.hpp>

#include <algorithm>
#include <cstdio>

namespace vks_task_oracle
{
    namespace
    {
        std::map<LLUUID, std::string>& aliases()
        {
            static std::map<LLUUID, std::string> a;
            return a;
        }

        std::string fmt_g(double v)
        {
            char buf[64];
            snprintf(buf, sizeof(buf), "%g", v);
            return buf;
        }

        // A compact, stable rendering of the floater keys LLViewerObject
        // passes (a UUID, or a map of UUIDs).
        std::string brief(const LLSD& sd)
        {
            if (sd.isUUID())
            {
                return uuid_name(sd.asUUID());
            }
            if (sd.isMap())
            {
                std::string out = "{";
                bool first = true;
                for (LLSD::map_const_iterator it = sd.beginMap(); it != sd.endMap(); ++it)
                {
                    out += (first ? "" : ",") + it->first + "=" + brief(it->second);
                    first = false;
                }
                return out + "}";
            }
            if (sd.isUndefined())
            {
                return "undef";
            }
            return sd.asString();
        }

        // Bytes outside printable ASCII, and the backslash, as \xHH.
        std::string printable(const std::string& in)
        {
            std::string out;
            for (unsigned char c : in)
            {
                if (c >= 0x20 && c < 0x7f && c != '\\')
                {
                    out += (char)c;
                }
                else
                {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\x%02x", c);
                    out += buf;
                }
            }
            return out;
        }

        void run_fibers()
        {
            // Let every fiber made ready by the last delivery run until it
            // finishes or suspends again (round robin, deterministic).
            for (int i = 0; i < 32; ++i)
            {
                boost::this_fiber::yield();
            }
        }
    }

    Recorder& recorder()
    {
        static Recorder r;
        return r;
    }

    std::string uuid_name(const LLUUID& id)
    {
        std::map<LLUUID, std::string>::const_iterator it = aliases().find(id);
        if (it != aliases().end())
        {
            return it->second;
        }
        if (id.isNull())
        {
            return "null";
        }
        return id.asString();
    }

    void set_uuid_alias(const LLUUID& id, const std::string& alias)
    {
        aliases()[id] = alias;
    }

    void clear_uuid_aliases()
    {
        aliases().clear();
    }

    //------------------------------------------------------------------
    // message system
    //------------------------------------------------------------------
    LLMessageSystem* gMessageSystem = NULL;

    LLMessageSystem::LLMessageSystem(const std::string& template_file) :
        ::LLMessageSystem(template_file, NET_USE_OS_ASSIGNED_PORT, 1, 0, 0, true, 5.f, 100.f)
    {
    }

    void LLMessageSystem::newMessageFast(const char* name)
    {
        mBuildingName = name ? name : "";
        ::LLMessageSystem::newMessageFast(name);
    }

    void LLMessageSystem::newMessage(const char* name)
    {
        mBuildingName = name ? name : "";
        ::LLMessageSystem::newMessage(name);
    }

    void LLMessageSystem::getUUIDFast(const char* block, const char* var, LLUUID& uuid, S32 blocknum)
    {
        if (mInjected)
        {
            mInjected->getUUID(block, var, uuid, blocknum);
            return;
        }
        ::LLMessageSystem::getUUIDFast(block, var, uuid, blocknum);
    }

    void LLMessageSystem::getS16Fast(const char* block, const char* var, S16& data, S32 blocknum)
    {
        if (mInjected)
        {
            mInjected->getS16(block, var, data, blocknum);
            return;
        }
        ::LLMessageSystem::getS16Fast(block, var, data, blocknum);
    }

    void LLMessageSystem::getStringFast(const char* block, const char* var, std::string& outstr, S32 blocknum)
    {
        if (mInjected)
        {
            mInjected->getString(block, var, outstr, blocknum);
            return;
        }
        ::LLMessageSystem::getStringFast(block, var, outstr, blocknum);
    }

    S32 LLMessageSystem::sendReliable(const LLHost& host)
    {
        recorder().add("msg.send " + mBuildingName);
        if (mCapture)
        {
            return ::LLMessageSystem::sendReliable(host);
        }
        clearMessage();
        return 0;
    }

    //------------------------------------------------------------------
    // LLViewerObject: the task-inventory state as the real constructor
    // initialises it (llviewerobject.cpp:303-308), and the parts of the real
    // destructor that touch it (:370-375, :413).
    //------------------------------------------------------------------
    LLViewerObject::LLViewerObject(const LLUUID& id, U32 local_id, LLViewerRegion* regionp) :
        mID(id),
        mLocalID(local_id),
        mInventory(NULL),
        mInventorySerialNum(0),
        mExpectedInventorySerialNum(0),
        mInvRequestState(INVENTORY_REQUEST_STOPPED),
        mInvRequestXFerId(0),
        mInventoryDirty(false),
        mRegionp(regionp)
    {
    }

    LLViewerObject::~LLViewerObject()
    {
        if (mInventory)
        {
            mInventory->clear();
            delete mInventory;
            mInventory = NULL;
        }
        clearInventoryListeners();
    }

    //------------------------------------------------------------------
    // singletons
    //------------------------------------------------------------------
    FakeAgent gAgent;
    FakeObjectList gObjectList;

    LLViewerObject* FakeObjectList::findObject(const LLUUID& id)
    {
        std::map<LLUUID, LLViewerObject*>::iterator it = mObjects.find(id);
        return it == mObjects.end() ? NULL : it->second;
    }

    std::string LLViewerRegion::getCapability(const std::string& name) const
    {
        std::map<std::string, std::string>::const_iterator it = mCaps.find(name);
        return it == mCaps.end() ? std::string() : it->second;
    }

    LLSelectMgr* LLSelectMgr::getInstance()
    {
        static LLSelectMgr mgr;
        return &mgr;
    }

    LLPermissions* LLSelectMgr::findObjectPermissions(const LLViewerObject* object)
    {
        return (object && object == mSelectedObject) ? &mSelectedPermissions : NULL;
    }

    bool LLFloaterReg::hideInstance(const std::string& name, const LLSD& key)
    {
        recorder().add("floater.hide " + name + " " + brief(key));
        return true;
    }

    std::string LLTrans::getString(const std::string& xml_desc)
    {
        return "{L10N:" + xml_desc + "}";
    }

    //------------------------------------------------------------------
    // coroutines
    //------------------------------------------------------------------
    struct PendingHttp
    {
        std::string method;
        std::string adapter;
        std::string url;
        LLSD body;
        boost::fibers::promise<LLSD> promise;
        bool abandoned = false;
    };

    struct PendingTimer
    {
        float seconds = 0.f;
        boost::fibers::promise<bool> promise;
    };

    namespace
    {
        std::deque<std::shared_ptr<PendingHttp> >& http_queue()
        {
            static std::deque<std::shared_ptr<PendingHttp> > q;
            return q;
        }

        std::deque<std::shared_ptr<PendingTimer> >& timer_queue()
        {
            static std::deque<std::shared_ptr<PendingTimer> > q;
            return q;
        }

        const std::string ABANDON_KEY("vks_task_oracle_abandoned");

        LLSD suspend_http(const std::string& method, const std::string& adapter,
                          const std::string& url, const LLSD& body)
        {
            std::shared_ptr<PendingHttp> p = std::make_shared<PendingHttp>();
            p->method = method;
            p->adapter = adapter;
            p->url = url;
            p->body = body;
            http_queue().push_back(p);
            boost::fibers::future<LLSD> f = p->promise.get_future();
            LLSD result = f.get();
            if (result.has(ABANDON_KEY))
            {
                throw Abandoned();
            }
            return result;
        }
    }

    LLCoros& LLCoros::instance()
    {
        static LLCoros coros;
        return coros;
    }

    std::string LLCoros::launch(const std::string& prefix, const std::function<void()>& callable)
    {
        recorder().add("coro.launch " + prefix);
        std::function<void()> body = callable;
        boost::fibers::fiber coro(boost::fibers::launch::dispatch,
                                  [body]()
                                  {
                                      try
                                      {
                                          body();
                                      }
                                      catch (const Abandoned&)
                                      {
                                      }
                                  });
        coro.detach();
        return prefix;
    }

    void llcoro::suspendUntilTimeout(float seconds)
    {
        recorder().add("timer.start " + fmt_g(seconds));
        std::shared_ptr<PendingTimer> t = std::make_shared<PendingTimer>();
        t->seconds = seconds;
        timer_queue().push_back(t);
        boost::fibers::future<bool> f = t->promise.get_future();
        if (!f.get())
        {
            throw Abandoned();
        }
    }

    const std::string& LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS =
        ::LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS;

    LLCoreHttpUtil::HttpCoroutineAdapter::HttpCoroutineAdapter(const std::string& name,
                                                               LLCore::HttpRequest::policy_t)
        : mName(name)
    {
    }

    LLSD LLCoreHttpUtil::HttpCoroutineAdapter::getAndSuspend(LLCore::HttpRequest::ptr_t, const std::string& url)
    {
        recorder().add("http.get " + mName + " " + url);
        return suspend_http("GET", mName, url, LLSD());
    }

    LLSD LLCoreHttpUtil::HttpCoroutineAdapter::postAndSuspend(LLCore::HttpRequest::ptr_t, const std::string& url, const LLSD& body)
    {
        recorder().add("http.post " + mName + " " + url);
        return suspend_http("POST", mName, url, body);
    }

    ::LLCore::HttpStatus LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(const LLSD& httpResults)
    {
        return ::LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(httpResults);
    }

    size_t pending_http_count() { return http_queue().size(); }
    size_t pending_timer_count() { return timer_queue().size(); }
    std::string pending_http_url(size_t index) { return http_queue().at(index)->url; }
    std::string pending_http_method(size_t index) { return http_queue().at(index)->method; }
    LLSD pending_http_body(size_t index) { return http_queue().at(index)->body; }

    void complete_http(const LLSD& body, const ::LLCore::HttpStatus& status, const char* method)
    {
        std::deque<std::shared_ptr<PendingHttp> >& q = http_queue();
        size_t index = 0;
        while (method && index < q.size() && q[index]->method != method)
        {
            ++index;
        }
        if (index >= q.size())
        {
            recorder().add(std::string("http.complete none-pending") + (method ? std::string(" ") + method : std::string()));
            return;
        }
        std::shared_ptr<PendingHttp> p = q[index];
        q.erase(q.begin() + index);
        recorder().add("http.complete " + p->method + " " + status.toTerseString());

        // What HttpCoroHandler::onCompleted and HttpCoroLLSDHandler hand
        // the coroutine (llmessage/llcorehttputil.cpp): the parsed body, made
        // a map, and the status entry. Headers are left empty; the
        // task-inventory code does not read them.
        LLSD result;
        if (!status)
        {
            result = LLSD::emptyMap();
            int type = status.getType();
            if (type >= 400 && type < 500 && body.isDefined())
            {
                if (!body.isMap())
                {
                    result[::LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS_CONTENT] = body;
                }
                else
                {
                    result = body;
                }
            }
        }
        else
        {
            result = body;
            if (result.isUndefined())
            {
                result = LLSD::emptyMap();
            }
            else if (!result.isMap())
            {
                LLSD wrapped = LLSD::emptyMap();
                wrapped[::LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS_CONTENT] = result;
                result = wrapped;
            }
        }
        LLSD httpresults = LLSD::emptyMap();
        ::LLCoreHttpUtil::HttpCoroHandler::writeStatusCodes(status, p->url, httpresults);
        httpresults[::LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS_HEADERS] = LLSD::emptyMap();
        result[::LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS] = httpresults;

        p->promise.set_value(result);
        run_fibers();
    }

    void fire_timer()
    {
        if (timer_queue().empty())
        {
            recorder().add("timer.fire none-pending");
            return;
        }
        std::shared_ptr<PendingTimer> t = timer_queue().front();
        timer_queue().pop_front();
        recorder().add("timer.fire " + fmt_g(t->seconds));
        t->promise.set_value(true);
        run_fibers();
    }

    void abandon_all()
    {
        while (!http_queue().empty() || !timer_queue().empty())
        {
            while (!http_queue().empty())
            {
                std::shared_ptr<PendingHttp> p = http_queue().front();
                http_queue().pop_front();
                LLSD marker;
                marker[ABANDON_KEY] = true;
                p->promise.set_value(marker);
            }
            while (!timer_queue().empty())
            {
                std::shared_ptr<PendingTimer> t = timer_queue().front();
                timer_queue().pop_front();
                t->promise.set_value(false);
            }
            run_fibers();
        }
    }

    //------------------------------------------------------------------
    // xfer manager
    //------------------------------------------------------------------
    namespace
    {
        LLXferManager g_fake_xfer_manager;
        ::LLXferManager* g_real_xfer = NULL;
        U64 g_next_xfer_id = 1001;
        std::vector<U64> g_real_ids;

        std::string cache_relative(const std::string& path)
        {
            std::string cache = gDirUtilp->getCacheDir();
            std::string rel = path;
            if (!cache.empty() && path.compare(0, cache.size(), cache) == 0)
            {
                rel = "<cache>" + path.substr(cache.size());
            }
            // the directory delimiter differs by platform ("\\" on Windows);
            // file names never contain one (getScrubbedFileName replaces both)
            std::replace(rel.begin(), rel.end(), '\\', '/');
            return rel;
        }

        std::string id_text(U64 id)
        {
            if (g_real_xfer)
            {
                if (id == 0)
                {
                    return "0";
                }
                for (size_t i = 0; i < g_real_ids.size(); ++i)
                {
                    if (g_real_ids[i] == id)
                    {
                        return "real#" + std::to_string(i + 1);
                    }
                }
                return "real#unknown";
            }
            return std::to_string(id);
        }
    }

    LLXferManager* gXferManager = &g_fake_xfer_manager;

    std::deque<FakeXfer>& fake_xfers()
    {
        static std::deque<FakeXfer> x;
        return x;
    }

    void reset_fake_xfers()
    {
        fake_xfers().clear();
        g_next_xfer_id = 1001;
        g_real_ids.clear();
    }

    std::string xfer_id_text(U64 id)
    {
        return id_text(id);
    }

    void set_real_xfer(::LLXferManager* real)
    {
        g_real_xfer = real;
        g_real_ids.clear();
    }

    U64 LLXferManager::requestFile(const std::string& local_filename,
                                   const std::string& remote_filename,
                                   ELLPath remote_path,
                                   const LLHost& remote_host,
                                   bool delete_remote_on_completion,
                                   void (*callback)(void**, S32, LLExtStat),
                                   void** user_data,
                                   bool is_priority,
                                   bool use_big_packets)
    {
        const std::string args = " local=" + printable(cache_relative(local_filename))
            + " remote=" + printable(remote_filename)
            + " path=" + std::to_string((int)remote_path)
            + " delete=" + std::to_string((int)delete_remote_on_completion)
            + " priority=" + std::to_string((int)is_priority)
            + " big=" + std::to_string((int)use_big_packets);
        if (g_real_xfer)
        {
            recorder().add("xfer.request" + args);
            // May throw (std::out_of_range for a 1-3 character remote name).
            U64 id = g_real_xfer->requestFile(local_filename, remote_filename, remote_path, remote_host,
                                              delete_remote_on_completion, callback, user_data,
                                              is_priority, use_big_packets);
            g_real_ids.push_back(id);
            recorder().add("xfer.requested id=" + id_text(id));
            return id;
        }
        U64 id = g_next_xfer_id++;
        FakeXfer x;
        x.id = id;
        x.local_filename = local_filename;
        x.remote_filename = remote_filename;
        x.callback = callback;
        x.user_data = user_data;
        x.aborted = false;
        fake_xfers().push_back(x);
        recorder().add("xfer.request id=" + std::to_string(id) + args);
        return id;
    }

    void LLXferManager::abortRequestById(U64 xfer_id, S32 result_code)
    {
        recorder().add("xfer.abort id=" + id_text(xfer_id) + " code=" + std::to_string(result_code));
        if (g_real_xfer)
        {
            g_real_xfer->abortRequestById(xfer_id, result_code);
            return;
        }
        // LLXferManager::abortRequestById marks an in-progress receive
        // aborted and sends AbortXfer; the completion callback runs only if
        // the simulator answers with AbortXfer, which the oracle does not
        // model (the transfer simply never completes).
        for (FakeXfer& x : fake_xfers())
        {
            if (x.id == xfer_id)
            {
                x.aborted = true;
            }
        }
    }
}
