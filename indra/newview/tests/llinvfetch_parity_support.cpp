/**
 * @file llinvfetch_parity_support.cpp
 * @brief The driver of the inventory fetch wire scenarios (recorder R3b).
 *
 * See llinvfetch_parity_support.h.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#include "../llviewerprecompiledheaders.h"

#include "llinvfetch_parity_support.h"

#include "../llinventorymodelbackgroundfetch.h"
#include "../llinventorymodel.h"
#include "../llinventoryobserver.h"
#include "../llviewerinventory.h"
#include "../llaisapi.h"
#include "../llagent.h"
#include "../llappearancemgr.h"
#include "../llstartup.h"
#include "../llviewercontrol.h"
#include "../llviewerwindow.h"
#include "../llappviewer.h"
#include "vkstorm_ffi.h"
#include "llnotificationsutil.h"
#include "llcallbacklist.h"
#include "llcoproceduremanager.h"
#include "llcoros.h"
#include "lleventcoro.h"
#include "llevents.h"
#include "llframetimer.h"
#include "lltimer.h"
#include "llapp.h"
#include "llwindow.h"
#include "llfile.h"
#include "llsdserialize.h"
#include "message.h"
#include "httprequest.h"
#include "httpoptions.h"
#include "httpheaders.h"
#include "httphandler.h"
#include "httpresponse.h"
#include "bufferarray.h"
#include "_httpservice.h"
#include "llapr.h"
#include "apr_network_io.h"

#include <boost/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <thread>

#if LL_WINDOWS
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")
#endif

// ---- access to private state ---------------------------------------------
// As in llinventorymodel_parity_stubs.cpp: an explicit instantiation may name
// a private member; the friend it defines hands the pointer out.
namespace
{
    template<typename Tag, typename Tag::type M>
    struct Steal
    {
        friend typename Tag::type steal(Tag) { return M; }
    };

    // LLApp::setStatus is protected.
    struct AppStatus : public LLApp
    {
        static void running() { setStatus(APP_STATUS_RUNNING); }
    };

#define VKS_STEAL(TagName, Type) \
    struct TagName { typedef Type type; friend type steal(TagName); }

    // The background fetch's scheduler is Rust (sub-rung R3b): its state is
    // read through vks_inv_fetch_state.
    VKS_STEAL(BgfFetch, VksInvFetch* LLInventoryModelBackgroundFetch::*);
    VKS_STEAL(FTFrameTime, F64*);
    VKS_STEAL(FTTotalTime, U64*);
    VKS_STEAL(FTTotalSeconds, F64*);
    VKS_STEAL(FTStartTotal, U64*);
#undef VKS_STEAL
}
template struct Steal<BgfFetch, &LLInventoryModelBackgroundFetch::mFetch>;
template struct Steal<FTFrameTime, &LLFrameTimer::sFrameTime>;
template struct Steal<FTTotalTime, &LLFrameTimer::sTotalTime>;
template struct Steal<FTTotalSeconds, &LLFrameTimer::sTotalSeconds>;
template struct Steal<FTStartTotal, &LLFrameTimer::sStartTotalTime>;

namespace vks_fetch
{
    const LLUUID AGENT(vks_inv::pid(0xa9e00000, 1));
    const LLUUID SESSION(vks_inv::pid(0x5e550000, 1));
    const LLUUID LIBRARY_OWNER("ba2a564a-f0f1-4b82-9c61-b7520bfcd09f");

    namespace
    {
        std::string g_port;
        std::string g_log_path;
        U32 g_udp_port = 0;
        U32 g_pool_size = 0;
        const S32 BUSY_BASE = 1000000;
        // The fake clock: LLFrameTimer's frame time, in seconds since a fixed
        // start, and LLTimer's total time frozen at START_US + that.
        const U64 START_US = 1000000000000ULL;   // 1e12 us after the epoch
        const int CLOCK_SHIFT = 20;
        F64 g_now = 0.0;

        std::string pct(const std::string& s)
        {
            // Spaces and anything outside printable ASCII percent-encoded
            // (the mock unquotes with urllib.parse.unquote).
            static const char* hexd = "0123456789ABCDEF";
            std::string out;
            for (unsigned char c : s)
            {
                if (c <= 0x20 || c >= 0x7f || c == '%')
                {
                    out.push_back('%');
                    out.push_back(hexd[c >> 4]);
                    out.push_back(hexd[c & 15]);
                }
                else
                {
                    out.push_back((char)c);
                }
            }
            return out;
        }

        // ---- a blocking control request to the mock ---------------------------
        class DoneHandler : public LLCore::HttpHandler
        {
        public:
            void onCompleted(LLCore::HttpHandle, LLCore::HttpResponse* response) override
            {
                mStatus = response->getStatus().getType();
                mDone = true;
            }
            bool mDone = false;
            int mStatus = 0;
        };

        // A control request on a socket of its own (APR), never through
        // llcorehttp: held replies may occupy every llcorehttp connection.
        int control(const std::string& path, const std::string& body, std::string* reply = NULL)
        {
            apr_pool_t* pool = NULL;
            apr_pool_create(&pool, NULL);
            int status = -1;
            apr_sockaddr_t* sa = NULL;
            apr_socket_t* sock = NULL;
            if (apr_sockaddr_info_get(&sa, "127.0.0.1", APR_INET, (apr_port_t)atoi(g_port.c_str()), 0, pool) == APR_SUCCESS
                && apr_socket_create(&sock, APR_INET, SOCK_STREAM, APR_PROTO_TCP, pool) == APR_SUCCESS)
            {
                apr_socket_timeout_set(sock, (apr_interval_time_t)60 * 1000000);
                if (apr_socket_connect(sock, sa) == APR_SUCCESS)
                {
                    std::string req = "POST " + path + " HTTP/1.1\r\nHost: 127.0.0.1:" + g_port
                        + "\r\nContent-Type: text/plain\r\nContent-Length: " + std::to_string(body.size())
                        + "\r\n\r\n" + body;
                    size_t sent = 0;
                    bool ok = true;
                    while (sent < req.size())
                    {
                        apr_size_t n = req.size() - sent;
                        if (apr_socket_send(sock, req.data() + sent, &n) != APR_SUCCESS || n == 0) { ok = false; break; }
                        sent += n;
                    }
                    std::string in;
                    size_t need = std::string::npos;
                    while (ok)
                    {
                        char buf[4096];
                        apr_size_t n = sizeof(buf);
                        apr_status_t rv = apr_socket_recv(sock, buf, &n);
                        if (n) in.append(buf, n);
                        size_t he = in.find("\r\n\r\n");
                        if (he != std::string::npos && need == std::string::npos)
                        {
                            size_t cl = in.find("Content-Length: ");
                            size_t len = cl == std::string::npos ? 0 : (size_t)atoi(in.c_str() + cl + 16);
                            need = he + 4 + len;
                        }
                        if (need != std::string::npos && in.size() >= need) break;
                        if (rv != APR_SUCCESS) { ok = false; break; }
                    }
                    if (ok && in.compare(0, 9, "HTTP/1.1 ") == 0)
                    {
                        status = atoi(in.c_str() + 9);
                        if (reply) *reply = in.substr(in.find("\r\n\r\n") + 4);
                    }
                }
                apr_socket_close(sock);
            }
            apr_pool_destroy(pool);
            if (status < 0) fprintf(stderr, "[r3b] control request %s failed\n", path.c_str());
            return status;
        }

        U32 pool_size_query(const std::string& name)
        {
            if (name == "PoolSizeAIS")
            {
                return g_pool_size;
            }
            return 0;
        }
        void pool_size_define(const std::string&, U32, const std::string&) {}
    }

    // ---- world ----------------------------------------------------------------

    Folder& World::add_folder(const LLUUID& id, const LLUUID& parent, S32 type, S32 version, const std::string& name, bool library)
    {
        Folder f;
        f.id = id;
        f.parent = parent;
        f.owner = library ? libowner : agent;
        f.type = type;
        f.version = version;
        f.name = name;
        folders.push_back(f);
        return folders.back();
    }

    Item& World::add_item(const LLUUID& id, const LLUUID& parent, S32 type, S32 inv_type, const std::string& name, bool library)
    {
        Item it;
        it.id = id;
        it.parent = parent;
        it.owner = library ? libowner : agent;
        it.creator = library ? libowner : agent;
        // Assets: the item id with the first byte replaced (patterned).
        it.asset = id;
        it.asset.mData[0] = 0xe8;
        it.type = type;
        it.inv_type = inv_type;
        it.name = name;
        it.desc = "";
        items.push_back(it);
        return items.back();
    }

    Item& World::add_link(const LLUUID& id, const LLUUID& parent, const LLUUID& linked, bool folder_link, const std::string& name)
    {
        Item it;
        it.id = id;
        it.parent = parent;
        it.owner = agent;
        it.creator = agent;
        it.link = true;
        it.linked = linked;
        it.type = folder_link ? LLAssetType::AT_LINK_FOLDER : LLAssetType::AT_LINK;
        it.inv_type = folder_link ? LLInventoryType::IT_CATEGORY : LLInventoryType::IT_WEARABLE;
        const Item* target = item(linked);
        if (target && !folder_link)
        {
            it.inv_type = target->inv_type;
        }
        it.name = name;
        items.push_back(it);
        return items.back();
    }

    const Folder* World::folder(const LLUUID& id) const
    {
        for (const Folder& f : folders) if (f.id == id) return &f;
        return NULL;
    }
    Folder* World::folder(const LLUUID& id)
    {
        for (Folder& f : folders) if (f.id == id) return &f;
        return NULL;
    }
    const Item* World::item(const LLUUID& id) const
    {
        for (const Item& i : items) if (i.id == id) return &i;
        return NULL;
    }

    std::string World::text() const
    {
        std::ostringstream s;
        s << "# vksinventory world\n";
        s << "agent " << agent << "\n";
        s << "libowner " << libowner << "\n";
        s << "root " << root << "\n";
        s << "libroot " << libroot << "\n";
        for (const Folder& f : folders)
        {
            if (!f.server) continue;
            s << "F " << f.id << " " << f.parent << " " << f.type << " " << f.version << " " << f.owner
              << " " << (f.name.empty() ? std::string("") : pct(f.name)) << "\n";
        }
        for (const Item& i : items)
        {
            if (i.link)
            {
                s << "L " << i.id << " " << i.parent << " " << i.owner << " " << i.linked << " " << i.type << " "
                  << i.inv_type << " " << i.created << " " << pct(i.name) << " " << pct(i.desc) << "\n";
            }
            else
            {
                s << "I " << i.id << " " << i.parent << " " << i.owner << " " << i.creator << " " << i.asset << " "
                  << i.type << " " << i.inv_type << " " << i.flags << " " << i.created << " "
                  << (S32)i.base << " " << (S32)i.owner_mask << " " << (S32)i.group_mask << " "
                  << (S32)i.everyone << " " << (S32)i.next << " " << i.sale_type << " " << i.sale_price << " "
                  << pct(i.name) << " " << pct(i.desc) << "\n";
            }
        }
        return s.str();
    }

    LLSD World::skeleton(bool library) const
    {
        // As the login response's inventory-skeleton (XML-RPC): ids as strings.
        LLSD arr = LLSD::emptyArray();
        for (const Folder& f : folders)
        {
            if ((f.owner == libowner) != library) continue;
            LLSD e;
            e["folder_id"] = f.id.asString();
            e["parent_id"] = f.parent.asString();
            e["name"] = f.name;
            e["type_default"] = f.type;
            e["version"] = f.version;
            arr.append(e);
        }
        return arr;
    }

    LLSD World::login_response() const
    {
        LLSD r;
        r["inventory-skeleton"] = skeleton(false);
        if (libroot.notNull())
        {
            r["inventory-skel-lib"] = skeleton(true);
            LLSD lr;
            lr["folder_id"] = libroot;
            r["inventory-lib-root"].append(lr);
            LLSD lo;
            lo["agent_id"] = libowner;
            r["inventory-lib-owner"].append(lo);
        }
        return r;
    }

    void World::collect_ids(std::set<LLUUID>& ids) const
    {
        ids.insert(agent);
        ids.insert(libowner);
        ids.insert(root);
        ids.insert(libroot);
        ids.insert(LLUUID::null);
        for (const Folder& f : folders) { ids.insert(f.id); ids.insert(f.parent); ids.insert(f.owner); }
        for (const Item& i : items)
        {
            ids.insert(i.id); ids.insert(i.parent); ids.insert(i.owner); ids.insert(i.creator);
            ids.insert(i.asset); ids.insert(i.linked);
        }
    }

    // ---- environment -------------------------------------------------------------

    std::string base_url() { return "http://127.0.0.1:" + g_port; }
    std::string cap_url(const std::string& cap) { return base_url() + "/cap/" + cap; }

    bool init(const std::string& template_file, const std::string& work_dir, std::string& error)
    {
#if LL_WINDOWS
        timeBeginPeriod(1);
#endif
        const char* port = getenv("LL_TEST_PORT");
        const char* log = getenv("VKS_HTTP_MOCK_LOG");
        if (!port || !log || !*port || !*log)
        {
            error = "LL_TEST_PORT / VKS_HTTP_MOCK_LOG not set: the test must run under vksinventory_mock_server.py";
            return false;
        }
        g_port = port;
        g_log_path = log;

        vks_inv::init_environment(template_file, work_dir);
        LLInventoryModel::registerCallbacks(gMessageSystem);
        gAgentID = AGENT;
        gAgentSessionID = SESSION;
        g_udp_port = gMessageSystem->mPort;

        // The coroutines check the application status (LLCoros::checkStop).
        AppStatus::running();
        // The default class's connection limit (8) raised to 32: with
        // replies held (LOCKSTEP) every request the scheduler dispatched
        // must reach the mock. Transport timing only; what the scheduler
        // sends does not depend on it.
        LLCore::HttpRequest::setStaticPolicyOption(LLCore::HttpRequest::PO_CONNECTION_LIMIT,
                                                   LLCore::HttpRequest::DEFAULT_POLICY_ID, 32, NULL);
        LLCore::HttpRequest::setStaticPolicyOption(LLCore::HttpRequest::PO_PER_HOST_CONNECTION_LIMIT,
                                                   LLCore::HttpRequest::DEFAULT_POLICY_ID, 32, NULL);
        LLCore::HttpRequest::startThread();

        // Initialise llcommon's clock, then freeze LLTimer's total time.
        (void)totalTime();
        clock_set(0.0);
        // The mock must answer.
        if (control("/control/mark/init", "") != 200)
        {
            error = "the mock server at " + base_url() + " does not answer";
            return false;
        }
        std::ifstream in(g_log_path.c_str(), std::ios::binary);
        if (!in)
        {
            error = "the mock server's log " + g_log_path + " cannot be read";
            return false;
        }
        return true;
    }

    void shutdown()
    {
        if (LLInventoryModelBackgroundFetch::instanceExists())
        {
            LLInventoryModelBackgroundFetch::deleteSingleton();
        }
        if (LLCoprocedureManager::instanceExists())
        {
            LLCoprocedureManager::instance().close();
            for (int i = 0; i < 50; ++i)
            {
                LLEventPumps::instance().obtain("mainloop").post(LLSD());
                llcoro::suspend();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            LLCoprocedureManager::deleteSingleton();
        }
        // The model's legacy HTTP requests go with the model.
        vks_inv::model::reset();
        LLCore::HttpRequest* req = new LLCore::HttpRequest;
        std::shared_ptr<DoneHandler> stopper = std::make_shared<DoneHandler>();
        req->requestStopThread(stopper);
        auto t0 = std::chrono::steady_clock::now();
        while (!LLCore::HttpService::isStopped()
               && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(10))
        {
            req->update(0);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        delete req;
        LLCore::HttpRequest::destroyService();
    }

    namespace bgf_clock_state { extern F64 now; extern F64 step; extern S64 reads; }
    namespace ais_stop_state { extern S64 calls; extern S64 abort_at; extern S64 thrown; }
    namespace ais_timer_state { extern S64 force_expired; }
    void bgf_clock(F64 now, F64 step) { bgf_clock_state::now = now; bgf_clock_state::step = step; bgf_clock_state::reads = 0; }
    S64 bgf_clock_reads() { return bgf_clock_state::reads; }
    void ais_stop_arm(S64 abort_at) { ais_stop_state::calls = 0; ais_stop_state::abort_at = abort_at; }
    void ais_timer_force_expired(S64 n) { ais_timer_state::force_expired = n; }
    S64 ais_stop_calls() { return ais_stop_state::calls; }
    S64 ais_stop_thrown() { return ais_stop_state::thrown; }

    void set_ais_pool(U32 n, bool force)
    {
        gSavedSettings.setU32("PoolSizeAIS", n);
        if (!force && g_pool_size == n && LLCoprocedureManager::instanceExists())
        {
            return;
        }
        if (LLCoprocedureManager::instanceExists())
        {
            // Close the pools, let their coroutines see the closed queue and
            // end, then drop the manager.
            LLCoprocedureManager::instance().close();
            for (int i = 0; i < 50; ++i)
            {
                LLEventPumps::instance().obtain("mainloop").post(LLSD());
                llcoro::suspend();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            LLCoprocedureManager::deleteSingleton();
        }
        g_pool_size = n;
        LLCoprocedureManager::instance().setPropertyMethods(pool_size_query, pool_size_define);
    }

    void reset_scenario(const std::string& cache_dir)
    {
        if (LLInventoryModelBackgroundFetch::instanceExists())
        {
            LLInventoryModelBackgroundFetch::deleteSingleton();
        }
        vks_inv::model::reset();
        vks_inv::reset_config();
        vks_inv::set_cache_dir(cache_dir);
        vks_inv::callbacks_reset();
        vks_inv::clear_journal();
        vks_inv::install_recorder();
        vks_inv::set_startup_state(STATE_STARTED);
        vks_inv::set_busy_count(BUSY_BASE);
        gSavedSettings.setS32("BatchSizeAIS3", 20);
        gSavedSettings.setBOOL("FSUseAis3Api", true);
        gSavedSettings.setBOOL("UseHTTPInventory", true);
        gSavedSettings.setBOOL("DebugAvatarAppearanceMessage", false);
        gSavedSettings.setU32("InventorySortOrder", 7);
        gDisconnected = false;
        clock_set(1000.0);
        bgf_clock(1000.0, 0.0);
        ais_stop_arm(0);
        ais_timer_force_expired(0);
        // Created now so it exists before anything posts the completion
        // signal; the scenarios record every completion.
        LLInventoryModelBackgroundFetch::instance().setFetchCompletionCallback([]()
        {
            vks_inv::note("fetch.completed");
        });
    }

    void set_region(bool present, int caps, bool received)
    {
        vks_inv::Config& c = vks_inv::config();
        c.region_present = present;
        c.capabilities_received = received;
        c.capabilities.clear();
        if (caps & CAPS_AIS)
        {
            c.capabilities["InventoryAPIv3"] = cap_url("inv");
            c.capabilities["LibraryAPIv3"] = cap_url("lib");
        }
        if (caps & CAPS_LEGACY)
        {
            c.capabilities["FetchInventoryDescendents2"] = cap_url("fid2");
            c.capabilities["FetchLibDescendents2"] = cap_url("fld2");
            c.capabilities["FetchInventory2"] = cap_url("fi2");
            c.capabilities["FetchLib2"] = cap_url("fl2");
            c.capabilities["CreateInventoryCategory"] = cap_url("cic");
        }
    }

    // ---- fake clock ------------------------------------------------------------

    void clock_set(F64 s)
    {
        g_now = s;
        const U64 total_us = START_US + (U64)(s * 1000000.0);
        *steal(FTFrameTime()) = s;
        *steal(FTStartTotal()) = START_US;
        *steal(FTTotalTime()) = total_us;
        *steal(FTTotalSeconds()) = (F64)total_us * 1e-6;
        TimerInfo& ti = get_timer_info();
        ti.mClocksToMicroseconds = F64Microseconds(1.0 / (F64)(1ULL << CLOCK_SHIFT));
        ti.mTotalTimeClockCount = total_us << CLOCK_SHIFT;
        ti.mLastTotalTimeClockCount = get_clock_count();
    }

    void clock_advance(F64 seconds) { clock_set(g_now + seconds); }
    F64 clock_now() { return g_now; }

    // ---- scheduler state ----------------------------------------------------------

    SchedState sched_state()
    {
        LLInventoryModelBackgroundFetch& b = LLInventoryModelBackgroundFetch::instance();
        VksInvFetchState* st = vks_inv_fetch_state(b.*steal(BgfFetch()));
        llassert_always(st);
        const VksInvFetchStateView v = vks_inv_fetch_state_view(st);
        auto id_at = [](const uint8_t* p) { LLUUID id; memcpy(id.mData, p, UUID_BYTES); return id; };
        SchedState s;
        s.bg_active = (v.flags & VKS_INV_FETCH_BG_ACTIVE) != 0;
        s.folder_active = (v.flags & VKS_INV_FETCH_FOLDER_ACTIVE) != 0;
        s.inv_started = (v.flags & VKS_INV_FETCH_INV_STARTED) != 0;
        s.lib_started = (v.flags & VKS_INV_FETCH_LIB_STARTED) != 0;
        s.mkt_started = (v.flags & VKS_INV_FETCH_MKT_STARTED) != 0;
        s.all_fetched = (v.flags & VKS_INV_FETCH_EVERYTHING) != 0;
        s.fetch_count = v.fetch_count;
        s.folder_count = v.folder_fetch_count;
        for (size_t i = 0; i < v.folder_queue_len; ++i) s.folder_queue.push_back(std::make_pair(id_at(v.folder_queue[i].id), (S32)v.folder_queue[i].fetch_type));
        for (size_t i = 0; i < v.item_queue_len; ++i) s.item_queue.push_back(std::make_pair(id_at(v.item_queue[i].id), (S32)v.item_queue[i].fetch_type));
        for (size_t i = 0; i < v.expected_len; ++i) s.expected.push_back(id_at(v.expected + i * UUID_BYTES));
        for (size_t i = 0; i < v.forced_len; ++i) s.forced.push_back(id_at(v.forced + i * UUID_BYTES));
        vks_inv_fetch_state_free(st);
        return s;
    }

    std::string sched_text(const SchedState& s)
    {
        std::ostringstream o;
        o << "act=" << s.bg_active << s.folder_active
          << " st=" << s.inv_started << s.lib_started << s.mkt_started << s.all_fetched
          << " fc=" << s.fetch_count << " ffc=" << s.folder_count << " fq=[";
        for (size_t i = 0; i < s.folder_queue.size(); ++i) o << (i ? "," : "") << s.folder_queue[i].first << ":" << s.folder_queue[i].second;
        o << "] iq=[";
        for (size_t i = 0; i < s.item_queue.size(); ++i) o << (i ? "," : "") << s.item_queue[i].first << ":" << s.item_queue[i].second;
        o << "] exp=[";
        for (size_t i = 0; i < s.expected.size(); ++i) o << (i ? "," : "") << s.expected[i];
        o << "] force=[";
        for (size_t i = 0; i < s.forced.size(); ++i) o << (i ? "," : "") << s.forced[i];
        o << "]";
        return o.str();
    }

    std::string public_state()
    {
        LLInventoryModelBackgroundFetch& b = LLInventoryModelBackgroundFetch::instance();
        std::ostringstream o;
        o << "folder_active=" << b.folderFetchActive()
          << " everything=" << b.isEverythingFetched()
          << " bulk_complete=" << b.isBulkFetchProcessingComplete()
          << " inv_started=" << b.inventoryFetchStarted()
          << " inv_completed=" << b.inventoryFetchCompleted()
          << " inv_in_progress=" << b.inventoryFetchInProgress()
          << " lib_started=" << b.libraryFetchStarted()
          << " lib_completed=" << b.libraryFetchCompleted()
          << " lib_in_progress=" << b.libraryFetchInProgress();
        return o.str();
    }

    S32 item_handlers_done()
    {
        return BUSY_BASE - vks_inv::window()->getBusyCount();
    }

    void mock_world(const World& w)
    {
        int st = control("/control/world", w.text());
        llassert_always(st == 200);
    }

    void mock_mode(const std::string& lines)
    {
        int st = control("/control/mode", lines);
        llassert_always(st == 200);
    }

    // ---- normalisation ------------------------------------------------------------

    namespace
    {
        bool is_hex(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
        bool uuid_at(const std::string& s, size_t i)
        {
            if (i + 36 > s.size()) return false;
            for (size_t k = 0; k < 36; ++k)
            {
                char c = s[i + k];
                if (k == 8 || k == 13 || k == 18 || k == 23) { if (c != '-') return false; }
                else if (!is_hex(c)) return false;
            }
            if (i > 0 && (is_hex(s[i - 1]) || s[i - 1] == '-')) return false;
            if (i + 36 < s.size() && (is_hex(s[i + 36]) || s[i + 36] == '-')) return false;
            return true;
        }
        bool patterned(const std::string& u)
        {
            // <tag8>-0000-4000-8000-<n12>: ids the test and the mock make.
            return u.compare(8, 15, "-0000-4000-8000") == 0;
        }
    }

    std::string Norm::apply(const std::string& in)
    {
        std::string s = in;
        // The port of the mock and the UDP listen port.
        LLStringUtil::replaceString(s, "127.0.0.1:" + g_port, "127.0.0.1:{PORT}");
        LLStringUtil::replaceString(s, "X-SecondLife-UDP-Listen-Port: " + std::to_string(g_udp_port), "X-SecondLife-UDP-Listen-Port: {UDPPORT}");
        std::string out;
        out.reserve(s.size());
        size_t i = 0;
        while (i < s.size())
        {
            if (uuid_at(s, i))
            {
                std::string u = s.substr(i, 36);
                LLStringUtil::toLower(u);
                bool tid = i >= 4 && s.compare(i - 4, 4, "tid=") == 0;
                if (tid)
                {
                    mTids.insert(u);
                    ++mTidUses;
                    out += "{tid}";
                }
                else if (mKnown.count(LLUUID(u)) || patterned(u))
                {
                    out += s.substr(i, 36);
                }
                else
                {
                    auto it = mGen.find(u);
                    if (it == mGen.end())
                    {
                        std::string name = "{gen" + std::to_string(mGen.size() + 1) + "}";
                        it = mGen.insert(std::make_pair(u, name)).first;
                    }
                    out += it->second;
                }
                i += 36;
                continue;
            }
            out.push_back(s[i]);
            ++i;
        }
        return out;
    }

    // ---- utilities ------------------------------------------------------------------

    std::string escape_text(const std::string& s)
    {
        std::string out;
        out.reserve(s.size() + 16);
        for (unsigned char c : s)
        {
            if (c == '\r') out += "\\r";
            else if (c == '\n') out += "\\n";
            else if (c == '\t') out += "\\t";
            else if (c == '\\') out += "\\\\";
            else if (c < 0x20 || c >= 0x7f)
            {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\x%02x", c);
                out += buf;
            }
            else out.push_back((char)c);
        }
        return out;
    }

    std::string short_sha(const std::string& bytes)
    {
        return vks_inv::sha256_hex(bytes).substr(0, 16);
    }

    // ---- recording hooks -----------------------------------------------------------------

    namespace
    {
        int g_callbacks = 0;
        class CountingCallback : public LLInventoryCallback
        {
        public:
            explicit CountingCallback(const std::string& tag) : mTag(tag) {}
            void fire(const LLUUID& id) override
            {
                ++g_callbacks;
                vks_inv::note("callback " + mTag + " " + id.asString());
            }
            std::string mTag;
        };
    }

    int callbacks_fired() { return g_callbacks; }

    LLPointer<LLInventoryCallback> cb(const std::string& tag)
    {
        return new CountingCallback(tag);
    }

    std::function<void(const LLUUID&)> ais_cb(const std::string& tag)
    {
        return [tag](const LLUUID& id) { ++g_callbacks; vks_inv::note("callback " + tag + " " + id.asString()); };
    }

    std::function<void()> nullary(const std::string& tag)
    {
        return [tag]() { ++g_callbacks; vks_inv::note("callback " + tag); };
    }

    // ---- Run ----------------------------------------------------------------------------

    namespace
    {
        std::string b64decode(const std::string& in)
        {
            static int table[256];
            static bool made = false;
            if (!made)
            {
                for (int i = 0; i < 256; ++i) table[i] = -1;
                const char* a = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
                for (int i = 0; i < 64; ++i) table[(unsigned char)a[i]] = i;
                made = true;
            }
            std::string out;
            out.reserve(in.size() * 3 / 4);
            int val = 0, bits = -8;
            for (unsigned char c : in)
            {
                if (table[c] < 0) continue;
                val = (val << 6) + table[c];
                bits += 6;
                if (bits >= 0)
                {
                    out.push_back((char)((val >> bits) & 0xff));
                    bits -= 8;
                }
            }
            return out;
        }

        std::string jstr(const boost::json::object& o, const char* k)
        {
            auto it = o.find(k);
            if (it == o.end() || !it->value().is_string()) return std::string();
            return std::string(it->value().as_string().c_str());
        }
        double jnum(const boost::json::object& o, const char* k)
        {
            auto it = o.find(k);
            if (it == o.end()) return 0;
            return it->value().to_number<double>();
        }
    }

    Run::Run(const std::string& id, const World* world, Mode mode, bool large)
        : mId(id), mWorld(world), mMode(mode), mConcurrent(mode == CONC), mLarge(large)
    {
        // Start reading the log where it ends now: earlier scenarios are
        // settled, their records written before their responses were sent.
        std::ifstream in(g_log_path.c_str(), std::ios::binary | std::ios::ate);
        mLogOffset = in ? (size_t)in.tellg() : 0;
        if (world)
        {
            std::set<LLUUID> ids;
            world->collect_ids(ids);
            ids.insert(AGENT);
            ids.insert(SESSION);
            ids.insert(LIBRARY_OWNER);
            mNorm.known(ids);
        }
        mItemHandlersDone = item_handlers_done();
        mStart = std::chrono::steady_clock::now();
        if (large) mBudget = 900.0;
        if (const char* b = getenv("VKS_INVFETCH_BUDGET"))
        {
            // Diagnostics only (VKS_INVFETCH_DEV=1): a shorter budget.
            if (getenv("VKS_INVFETCH_DEV")) mBudget = atof(b);
        }
        vks_inv::note("step begin " + id);
        snapshot("begin");
    }

    Run::~Run() {}

    void Run::read_log()
    {
        std::ifstream in(g_log_path.c_str(), std::ios::binary);
        if (!in) return;
        in.seekg(0, std::ios::end);
        size_t end = (size_t)in.tellg();
        if (end <= mLogOffset) return;
        in.seekg((std::streamoff)mLogOffset);
        std::string chunk(end - mLogOffset, '\0');
        in.read(&chunk[0], (std::streamsize)chunk.size());
        // Only whole lines.
        size_t last = chunk.rfind('\n');
        if (last == std::string::npos) return;
        mLogOffset += last + 1;
        std::istringstream lines(chunk.substr(0, last + 1));
        std::string line;
        while (std::getline(lines, line))
        {
            if (line.empty()) continue;
            boost::system::error_code ec;
            boost::json::value v = boost::json::parse(line, ec);
            if (ec || !v.is_object()) continue;
            const boost::json::object& o = v.as_object();
            std::string ev = jstr(o, "ev");
            if (ev == "arr")
            {
                Wire w;
                w.seq = (int)jnum(o, "seq");
                w.conn = (int)jnum(o, "conn");
                w.t_in = jnum(o, "t_in");
                w.method = jstr(o, "method");
                w.target = jstr(o, "target");
                auto h = o.find("held");
                w.held = h != o.end() && h->value().is_bool() && h->value().as_bool();
                w.round = mRound;
                mBySeq[w.seq] = mWire.size();
                mWire.push_back(w);
                ++mArrivals;
            }
            else if (ev == "bad")
            {
                fail("the mock could not parse a request head: " + jstr(o, "error"));
            }
            else if (ev == "req")
            {
                int seq = (int)jnum(o, "seq");
                auto it = mBySeq.find(seq);
                if (it == mBySeq.end()) continue;   // an earlier scenario's
                Wire& w = mWire[it->second];
                w.t_out = jnum(o, "t_out");
                w.head = b64decode(jstr(o, "head"));
                w.body = b64decode(jstr(o, "body"));
                w.status = (int)jnum(o, "status");
                w.rlen = (size_t)jnum(o, "rlen");
                w.rtype = jstr(o, "rtype");
                if (o.find("rbody") != o.end())
                {
                    w.rbody = b64decode(jstr(o, "rbody"));
                    w.have_rbody = true;
                }
                w.answered = true;
                if (o.find("mock_error") != o.end())
                {
                    fail("mock server error on " + w.method + " " + w.target + ": " + jstr(o, "mock_error"));
                }
            }
        }
    }

    size_t Run::requests()
    {
        read_log();
        return mArrivals;
    }

    void Run::note(const std::string& line) { vks_inv::note(line); }

    void Run::step(const std::string& name)
    {
        if (mMode == PHASED)
        {
            // Requests issued before the step must be counted in its phase:
            // wait until no request has arrived for 300 ms (no frames run
            // meanwhile, so nothing new is issued).
            size_t last = requests();
            auto quiet = std::chrono::steady_clock::now();
            while (std::chrono::steady_clock::now() - quiet < std::chrono::milliseconds(300))
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                size_t now = requests();
                if (now != last) { last = now; quiet = std::chrono::steady_clock::now(); }
            }
        }
        size_t n = requests();
        vks_inv::note("step " + name + " requests=" + std::to_string(n));
        if (mMode == PHASED)
        {
            mPhaseStarts.push_back(n);
            vks_inv::note("sch@step " + sched_text(sched_state()));
        }
    }

    void Run::snapshot(const std::string& why)
    {
        std::string s = sched_text(sched_state());
        if (s != mLastSnap)
        {
            mLastSnap = s;
            ++mSnapCount;
            if (mMode == SEQ || mMode == LOCKSTEP)
            {
                vks_inv::note("sch " + s);
            }
            static const bool trace = getenv("VKS_INVFETCH_TRACE") != NULL;
            if (trace)
            {
                fprintf(stderr, "[r3b] %s sch %s\n", mId.c_str(), s.substr(0, 1500).c_str());
            }
        }
    }

    namespace
    {
        // What a stalled scenario was doing, on stderr before it aborts.
        void stall_report(const std::string& id, const std::string& why)
        {
            fprintf(stderr, "[r3b] %s: %s\n[r3b]   scheduler: %s\n[r3b]   ais pool: %d pending+active\n",
                    id.c_str(), why.c_str(), sched_text(sched_state()).substr(0, 2000).c_str(),
                    (int)LLCoprocedureManager::instance().count("AIS"));
            std::vector<std::string> j = vks_inv::take_journal();
            size_t from = j.size() > 15 ? j.size() - 15 : 0;
            for (size_t i = from; i < j.size(); ++i) fprintf(stderr, "[r3b]   journal: %s\n", j[i].substr(0, 300).c_str());
            for (const std::string& l : j) vks_inv::note(l);
            LLInventoryModel::cat_array_t* cats = NULL;
            LLInventoryModel::item_array_t* items = NULL;
            gInventory.getDirectDescendentsOf(gInventory.getRootFolderID(), cats, items);
            for (size_t i = 0; cats && i < cats->size() && i < 30; ++i)
            {
                fprintf(stderr, "[r3b]   root child: %s\n", vks_inv::dump_category((*cats)[i]).substr(0, 400).c_str());
            }
        }
    }

    void Run::check_budget()
    {
        F64 used = std::chrono::duration<double>(std::chrono::steady_clock::now() - mStart).count();
        if (used > mBudget)
        {
            fail("scenario budget of " + std::to_string((int)mBudget) + " s exhausted");
            stall_report(mId, "budget exhausted");
            throw Abort(mId + ": budget exhausted");
        }
    }

    std::vector<int> Run::held()
    {
        read_log();
        std::vector<std::pair<std::string, int> > h;
        for (const Wire& w : mWire)
        {
            if (w.held && !w.answered) h.push_back(std::make_pair(w.method + " " + mNorm.apply(w.target), w.seq));
        }
        std::sort(h.begin(), h.end());
        std::vector<int> out;
        for (const auto& p : h) out.push_back(p.second);
        return out;
    }

    void Run::release(const std::vector<int>& seqs)
    {
        std::string body;
        for (int q : seqs) body += std::to_string(q) + "\n";
        if (!body.empty()) control("/control/release", body);
    }

    void Run::release_all()
    {
        control("/control/release", "all");
    }

    void Run::release_one(int seq)
    {
        release(std::vector<int>(1, seq));
        auto t0 = std::chrono::steady_clock::now();
        // Processed when the mock answered it and no AIS coroutine is
        // left on a reply that has come back.
        while (true)
        {
            frame(false);
            read_log();
            auto it = mBySeq.find(seq);
            bool answered = it != mBySeq.end() && mWire[it->second].answered;
            size_t unanswered = 0;
            for (const Wire& w : mWire) if (!w.answered) ++unanswered;
            if (answered && (size_t)LLCoprocedureManager::instance().countActive("AIS") <= unanswered)
            {
                // A few more frames: the update's yields and the callback.
                for (int i = 0; i < 4; ++i) frame(false);
                if ((size_t)LLCoprocedureManager::instance().countActive("AIS") <= unanswered) break;
            }
            if (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() > 60.0)
            {
                fail("release of request " + std::to_string(seq) + " not processed in 60 s");
                throw Abort(mId + ": release timeout");
            }
            check_budget();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    void Run::await_arrivals(const std::string& label)
    {
        // Every active coroutine's request at the mock.
        auto t0 = std::chrono::steady_clock::now();
        while (held().size() < (size_t)LLCoprocedureManager::instance().countActive("AIS"))
        {
            if (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() > 60.0)
            {
                fail("round " + label + ": requests did not reach the mock");
                throw Abort(mId + ": round timeout");
            }
            for (int i = 0; i < 2; ++i) llcoro::suspend();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        // The quiet wait as step() does: nothing more arrives.
        size_t last = requests();
        auto quiet = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - quiet < std::chrono::milliseconds(100))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            size_t now = requests();
            if (now != last) { last = now; quiet = std::chrono::steady_clock::now(); }
        }
    }

    bool Run::round(const std::string& label)
    {
        ++mRound;
        // A coroutine the previous round's replies freed takes the next queued
        // request and sends it while that round is still releasing; which one
        // it sends is fixed (FIFO queue, sorted releases), when it reaches the
        // mock is not. Wait for those arrivals before the phase starts, so
        // each request is counted in the round that caused it.
        await_arrivals(label);
        mPhaseStarts.push_back(requests());
        frame(true);
        vks_inv::note("round " + label + " " + std::to_string(mRound) + " dispatched=" + std::to_string(mLastAfter - mLastBefore)
                      + " sch=" + sched_text(sched_state()));
        await_arrivals(label);
        std::vector<int> h = held();
        for (int q : h) release_one(q);
        return !h.empty();
    }

    void Run::drain(const std::string& label, int max_rounds)
    {
        for (int k = 0; k < max_rounds; ++k)
        {
            bool any = round(label);
            SchedState st = sched_state();
            if (!any && st.folder_queue.empty() && st.item_queue.empty() && st.fetch_count == 0) return;
        }
        fail("drain " + label + ": more than " + std::to_string(max_rounds) + " rounds");
        throw Abort(mId + ": drain");
    }

    void Run::frame(bool idle)
    {
        LLFrameTimer::updateFrameCount();
        S32 before = sched_state().fetch_count;
        if (idle)
        {
            gIdleCallbacks.callFunctions();
        }
        S32 after = sched_state().fetch_count;
        mLastBefore = before;
        mLastAfter = after;
        mMaxDispatch = llmax(mMaxDispatch, after - before);
        mMaxInFlight = llmax(mMaxInFlight, after);
        gInventory.idleNotifyObservers();
        LLEventPumps::instance().obtain("mainloop").post(LLSD());
        llcoro::suspend();
        llcoro::suspend();
        mMaxPool = llmax(mMaxPool, (S32)LLCoprocedureManager::instance().count("AIS"));
        ++mFrames;
        snapshot();
    }

    void Run::frame_note(const std::string& label)
    {
        frame(true);
        vks_inv::note("frame " + label + " count_before=" + std::to_string(mLastBefore)
                      + " count_after_idle=" + std::to_string(mLastAfter)
                      + " pool=" + std::to_string(LLCoprocedureManager::instance().count("AIS")));
    }

    void Run::frames(int n, bool idle)
    {
        for (int i = 0; i < n; ++i)
        {
            frame(idle);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    bool Run::idle_now(bool fetch_idle)
    {
        read_log();
        if (LLCoprocedureManager::instance().count("AIS") != 0) return false;
        if (fetch_idle && sched_state().fetch_count != 0) return false;
        size_t answered = 0, item_ok = 0;
        for (const Wire& w : mWire)
        {
            if (!w.answered) continue;
            ++answered;
            if (w.status == 200 && (w.target.compare(0, 9, "/cap/fi2") == 0 || w.target.compare(0, 9, "/cap/fl2") == 0))
            {
                ++item_ok;
            }
        }
        if (answered != mWire.size()) return false;
        if ((size_t)(item_handlers_done() - mItemHandlersDone) < item_ok) return false;
        return true;
    }

    bool Run::until(const std::function<bool()>& pred, bool idle, F64 timeout)
    {
        auto t0 = std::chrono::steady_clock::now();
        while (true)
        {
            frame(idle);
            check_budget();
            if (pred()) return true;
            if (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() > timeout)
            {
                fail("until() timed out after " + std::to_string((int)timeout) + " s");
                stall_report(mId, "until timed out");
                fprintf(stderr, "[r3b] %s: until timed out\n", mId.c_str());
                throw Abort(mId + ": until timeout");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    bool Run::settle(bool idle, bool queues_empty, F64 timeout, bool fetch_idle)
    {
        auto t0 = std::chrono::steady_clock::now();
        auto quiet_since = t0;
        int stable = 0;
        std::string last;
        while (true)
        {
            frame(idle);
            check_budget();
            bool ok = idle_now(fetch_idle);
            if (ok && queues_empty)
            {
                SchedState s = sched_state();
                ok = s.folder_queue.empty() && s.item_queue.empty();
            }
            std::string now = mLastSnap + "|" + std::to_string(mArrivals) + "|" + std::to_string(vks_inv::window()->getBusyCount())
                + "|" + std::to_string(callbacks_fired()) + "|" + std::to_string(vks_inv::model::categories().size())
                + "|" + std::to_string(vks_inv::model::items().size()) + "|" + std::to_string(gInventory.getChangedIDs().size());
            // Stable for 8 frames and 150 ms: a foreground item request
            // (fetchFromServer, the item observer) has no counter the test
            // can read, so the quiet time covers its trip to the mock.
            if (ok && now == last)
            {
                ++stable;
                if (stable >= 8 && std::chrono::steady_clock::now() - quiet_since >= std::chrono::milliseconds(150))
                {
                    return true;
                }
            }
            else
            {
                stable = 0;
                quiet_since = std::chrono::steady_clock::now();
            }
            last = now;
            if (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() > timeout)
            {
                fail("settle timed out after " + std::to_string((int)timeout) + " s");
                stall_report(mId, "settle timed out");
                fprintf(stderr, "[r3b] %s: settle timed out\n", mId.c_str());
                throw Abort(mId + ": settle timeout");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    namespace
    {
        // Sorts the model dump so ids the viewer generated (normalised to
        // {genN}) do not move lines around: each block keeps its place, the
        // lines inside a block sorted by their normalised text.
        int block_rank(const std::string& l)
        {
            static const char* order[] = { "model ", "cat ", "item ", "children ", "backlink ", "broken ", "rebuild ", "locks ", "lock ", "pending " };
            for (int i = 0; i < 10; ++i) if (l.compare(0, strlen(order[i]), order[i]) == 0) return i;
            return 10;
        }
        // Sorts the comma-separated elements inside every [...] of a line.
        std::string sort_lists(const std::string& l)
        {
            std::string out;
            size_t i = 0;
            while (i < l.size())
            {
                size_t a = l.find('[', i);
                if (a == std::string::npos) { out += l.substr(i); break; }
                size_t b = l.find(']', a);
                if (b == std::string::npos) { out += l.substr(i); break; }
                out += l.substr(i, a + 1 - i);
                std::vector<std::string> parts;
                std::string inner = l.substr(a + 1, b - a - 1);
                size_t p = 0;
                while (!inner.empty())
                {
                    size_t c = inner.find(',', p);
                    parts.push_back(inner.substr(p, c == std::string::npos ? std::string::npos : c - p));
                    if (c == std::string::npos) break;
                    p = c + 1;
                }
                std::sort(parts.begin(), parts.end());
                for (size_t k = 0; k < parts.size(); ++k) out += (k ? "," : "") + parts[k];
                out += "]";
                i = b + 1;
            }
            return out;
        }

        // A digest of the whole block, a digest per chunk of CHUNK lines (a
        // mismatch names the line range), and the first and last lines as
        // samples; sample lines carry their line number, so they align with
        // the chunk they belong to.
        const size_t CHUNK = 500;
        void digest_block(std::vector<std::string>& out, const std::string& tag, const std::vector<std::string>& lines, size_t head, size_t tail)
        {
            std::string all;
            for (const std::string& l : lines) { all += l; all.push_back('\n'); }
            out.push_back(tag + "-digest lines=" + std::to_string(lines.size()) + " sha256=" + vks_inv::sha256_hex(all)
                          + " chunk=" + std::to_string(CHUNK));
            for (size_t c = 0; c * CHUNK < lines.size(); ++c)
            {
                std::string chunk;
                size_t end = std::min(lines.size(), (c + 1) * CHUNK);
                for (size_t i = c * CHUNK; i < end; ++i) { chunk += lines[i]; chunk.push_back('\n'); }
                out.push_back(tag + "-chunk " + std::to_string(c) + " lines=" + std::to_string(c * CHUNK + 1) + "-"
                              + std::to_string(end) + " sha256=" + vks_inv::sha256_hex(chunk));
            }
            for (size_t i = 0; i < lines.size() && i < head; ++i)
            {
                const std::string& l = lines[i];
                out.push_back(tag + "-line " + std::to_string(i + 1) + " " + (l.size() > 600 ? l.substr(0, 600) + "...(" + std::to_string(l.size()) + ")" : l));
            }
            size_t start = lines.size() > tail ? lines.size() - tail : 0;
            if (start < head) start = head;
            for (size_t i = start; i < lines.size(); ++i)
            {
                const std::string& l = lines[i];
                out.push_back(tag + "-line " + std::to_string(i + 1) + " " + (l.size() > 600 ? l.substr(0, 600) + "...(" + std::to_string(l.size()) + ")" : l));
            }
        }
    }

    namespace
    {
        // An AIS update that runs past its 8 ms (real-time) budget yields to
        // the next frame; the model's idle notification then flushes the
        // changes made so far as a batch of its own. Where batches split, and
        // so how often addChangedMask's marketplace hook fires for an id
        // changed again after a flush, depends on timing. Every run of
        // consecutive "batch" / "market.update" lines therefore becomes one
        // "batch*" line (the masks OR'd, the changed and added sets united)
        // and one "market*" line (the ids, sorted and unique).
        std::vector<std::string> canon_notifications(const std::vector<std::string>& in)
        {
            std::vector<std::string> out;
            U32 mask = 0;
            std::set<std::string> changed, added, market;
            bool any = false;
            auto ids_of = [](const std::string& l, const std::string& key, std::set<std::string>& into)
            {
                size_t a = l.find(key + "=[");
                if (a == std::string::npos) return;
                a += key.size() + 2;
                size_t b = l.find(']', a);
                std::string inner = l.substr(a, b - a);
                size_t p = 0;
                while (p < inner.size())
                {
                    size_t c = inner.find(',', p);
                    into.insert(inner.substr(p, c == std::string::npos ? std::string::npos : c - p));
                    if (c == std::string::npos) break;
                    p = c + 1;
                }
            };
            auto flush = [&]()
            {
                if (!any) return;
                if (mask || !changed.empty() || !added.empty())
                {
                    std::string l = "batch* mask=" + vks_inv::hexmask(mask) + " changed=[";
                    bool f = true;
                    for (const std::string& i : changed) { l += (f ? "" : ",") + i; f = false; }
                    l += "] added=[";
                    f = true;
                    for (const std::string& i : added) { l += (f ? "" : ",") + i; f = false; }
                    out.push_back(l + "]");
                }
                if (!market.empty())
                {
                    std::string l = "market* [";
                    bool f = true;
                    for (const std::string& i : market) { l += (f ? "" : ",") + i; f = false; }
                    out.push_back(l + "]");
                }
                mask = 0; changed.clear(); added.clear(); market.clear(); any = false;
            };
            for (const std::string& l : in)
            {
                if (l.compare(0, 6, "batch ") == 0)
                {
                    any = true;
                    mask |= (U32)strtoul(l.c_str() + l.find("mask=") + 5, NULL, 16);
                    ids_of(l, "changed", changed);
                    ids_of(l, "added", added);
                    continue;
                }
                if (l.compare(0, 14, "market.update ") == 0)
                {
                    any = true;
                    std::string rest = l.substr(14);
                    market.insert(rest.substr(0, rest.find(' ')) + rest.substr(rest.find(' ')));
                    continue;
                }
                flush();
                out.push_back(l);
            }
            flush();
            return out;
        }
    }

    std::string Run::format_wire(const Wire& w)
    {
        std::string rb = w.have_rbody ? mNorm.apply(w.rbody) : std::string();
        std::string line = w.method + " " + mNorm.apply(w.target)
            + " status=" + std::to_string(w.status)
            + " head=" + escape_text(mNorm.apply(w.head))
            + " body=" + escape_text(mNorm.apply(w.body))
            + " resp=" + std::to_string(w.rlen) + ":" + (w.have_rbody ? short_sha(rb) : std::string("unlogged"))
            + " rtype=" + w.rtype;
        if (!w.answered) line += " UNANSWERED";
        return line;
    }

    std::vector<std::string> Run::wire_lines()
    {
        read_log();
        std::vector<std::string> out;
        for (const Wire& w : mWire) out.push_back(format_wire(w));
        return out;
    }

    Case Run::finish(std::string& responses)
    {
        read_log();
        Case c;
        c.id = mId;
        std::vector<std::string>& out = c.lines;

        // Request counts first: server load is the main risk.
        std::map<std::string, int> by_cap, by_method;
        int retries_503 = 0;
        for (const Wire& w : mWire)
        {
            std::string t = w.target;
            std::string cap = "other";
            if (t.compare(0, 5, "/cap/") == 0)
            {
                size_t e = t.find_first_of("/?", 5);
                cap = t.substr(5, e == std::string::npos ? std::string::npos : e - 5);
            }
            by_cap[cap]++;
            by_method[w.method]++;
            if (w.status == 503) ++retries_503;
        }
        std::string counts = "count requests=" + std::to_string(mWire.size()) + " caps=";
        bool first = true;
        for (const auto& kv : by_cap) { counts += (first ? "" : ",") + kv.first + ":" + std::to_string(kv.second); first = false; }
        counts += " methods=";
        first = true;
        for (const auto& kv : by_method) { counts += (first ? "" : ",") + kv.first + ":" + std::to_string(kv.second); first = false; }
        counts += " status503=" + std::to_string(retries_503);
        out.push_back(counts);
        // The scenario's own compared lines.
        for (const std::string& f : mFacts) out.push_back("fact " + mNorm.apply(f));

        // The wire log: arrival order (sequence mode) or a sorted multiset
        // (concurrency mode).
        std::vector<std::string> wire_lines;
        std::string resp_text;
        int n = 0;
        // Every tid-bearing request must carry a tid of its own.
        size_t tid_requests = 0;
        std::set<std::string> tid_values;
        for (const Wire& w : mWire)
        {
            size_t k = w.target.find("tid=");
            if (k == std::string::npos) continue;
            ++tid_requests;
            tid_values.insert(w.target.substr(k + 4, 36));
        }
        for (const Wire& w : mWire)
        {
            ++n;
            std::string rb = w.have_rbody ? mNorm.apply(w.rbody) : std::string();
            wire_lines.push_back(format_wire(w));
            if (w.have_rbody && !mLarge && mMode == SEQ)
            {
                resp_text += "--- " + mId + " " + std::to_string(n) + " " + w.method + " " + mNorm.apply(w.target)
                    + " status=" + std::to_string(w.status) + " len=" + std::to_string(w.rlen) + "\n" + rb + "\n";
            }
        }
        if (mConcurrent)
        {
            std::map<std::string, int> multiset;
            for (const std::string& l : wire_lines) multiset[l]++;
            for (const auto& kv : multiset) out.push_back("wire* " + std::to_string(kv.second) + "x " + kv.first);
        }
        else if (mMode == PHASED || mMode == LOCKSTEP)
        {
            size_t begin = 0;
            std::vector<size_t> ends(mPhaseStarts);
            ends.push_back(wire_lines.size());
            for (size_t p = 0; p < ends.size(); ++p)
            {
                size_t end = std::min(ends[p], wire_lines.size());
                std::vector<std::string> phase(wire_lines.begin() + std::min(begin, end), wire_lines.begin() + end);
                std::sort(phase.begin(), phase.end());
                for (const std::string& l : phase) out.push_back("wire@" + std::to_string(p) + " " + l);
                begin = std::max(begin, end);
            }
        }
        else
        {
            for (size_t i = 0; i < wire_lines.size(); ++i) out.push_back("wire " + std::to_string(i + 1) + " " + wire_lines[i]);
        }
        out.push_back("tids requests=" + std::to_string(tid_requests) + " distinct=" + std::to_string(tid_values.size())
                      + " fresh=" + (tid_requests == tid_values.size() ? "yes" : "NO"));

        // The journal (messages, notifications, observer batches, callbacks,
        // steps, scheduler snapshots), normalised.
        std::vector<std::string> journal;
        for (const std::string& l : vks_inv::take_journal())
        {
            std::string nl = mNorm.apply(l);
            if (nl.find("{gen") != std::string::npos) nl = sort_lists(nl);
            journal.push_back(nl);
        }
        journal = canon_notifications(journal);
        if (mConcurrent)
        {
            // Frame-dependent: only what is order-free is compared.
            std::map<std::string, int> kinds;
            for (const std::string& l : journal)
            {
                if (l.compare(0, 4, "msg ") == 0 || l.compare(0, 7, "notify ") == 0 || l.compare(0, 9, "callback ") == 0)
                {
                    kinds[l]++;
                }
            }
            for (const auto& kv : kinds) out.push_back("jrn* " + std::to_string(kv.second) + "x " + kv.first);
        }
        else if (mMode == PHASED)
        {
            // Sorted within each phase; the scheduler only at the steps.
            std::vector<std::string> seg;
            auto flush = [&]()
            {
                std::sort(seg.begin(), seg.end());
                for (const std::string& l : seg) out.push_back("jrn " + l);
                seg.clear();
            };
            // Observer batches: how replies arriving together split into
            // batches depends on timing; their union per phase does not.
            U32 umask = 0;
            std::set<std::string> uchanged, uadded, umarket;
            bool any_batch = false;
            auto split_ids = [](const std::string& l, const std::string& key, std::set<std::string>& into)
            {
                size_t a = l.find(key + "=[");
                if (a == std::string::npos) return;
                a += key.size() + 2;
                size_t b = l.find(']', a);
                std::string inner = l.substr(a, b - a);
                if (inner.empty()) return;
                size_t p = 0;
                while (p < inner.size())
                {
                    size_t c = inner.find(',', p);
                    into.insert(inner.substr(p, c == std::string::npos ? std::string::npos : c - p));
                    if (c == std::string::npos) break;
                    p = c + 1;
                }
            };
            auto flush_batches = [&]()
            {
                if (!any_batch) return;
                std::string line = "batch* mask=" + vks_inv::hexmask(umask) + " changed=[";
                bool f = true;
                for (const std::string& i : uchanged) { line += (f ? "" : ",") + i; f = false; }
                line += "] added=[";
                f = true;
                for (const std::string& i : uadded) { line += (f ? "" : ",") + i; f = false; }
                line += "]";
                seg.push_back(line);
                if (!umarket.empty())
                {
                    std::string m = "market* [";
                    bool g = true;
                    for (const std::string& i : umarket) { m += (g ? "" : ",") + i; g = false; }
                    seg.push_back(m + "]");
                }
                umask = 0; uchanged.clear(); uadded.clear(); umarket.clear(); any_batch = false;
            };
            for (const std::string& l : journal)
            {
                if (l.compare(0, 4, "sch ") == 0) continue;
                if (l.compare(0, 7, "batch* ") == 0)
                {
                    any_batch = true;
                    umask |= (U32)strtoul(l.c_str() + l.find("mask=") + 5, NULL, 16);
                    split_ids(l, "changed", uchanged);
                    split_ids(l, "added", uadded);
                    continue;
                }
                if (l.compare(0, 8, "market* ") == 0)
                {
                    split_ids("x=" + l.substr(7), "x", umarket);
                    any_batch = true;
                    continue;
                }
                if (l.compare(0, 5, "step ") == 0 || l.compare(0, 9, "sch@step ") == 0)
                {
                    flush_batches();
                    flush();
                    out.push_back("jrn " + l);
                    continue;
                }
                seg.push_back(l);
            }
            flush_batches();
            flush();
        }
        else if (mLarge)
        {
            std::vector<std::string> tagged;
            for (const std::string& l : journal) tagged.push_back(l);
            digest_block(out, "jrn", tagged, 40, 20);
        }
        else
        {
            for (const std::string& l : journal) out.push_back("jrn " + l);
        }

        out.push_back("pub " + public_state());

        // The final model.
        std::vector<std::string> model;
        vks_inv::dump_model(model, true);
        std::vector<std::pair<int, std::string> > sorted;
        for (const std::string& l : model)
        {
            std::string nl = mNorm.apply(l);
            if (nl.find("{gen") != std::string::npos) nl = sort_lists(nl);
            sorted.push_back(std::make_pair(block_rank(nl), nl));
        }
        std::stable_sort(sorted.begin(), sorted.end());
        std::vector<std::string> mdl;
        for (const auto& p : sorted)
        {
            if (mConcurrent)
            {
                // Insertion order of child vectors and the lock maps follow
                // the order replies arrived in: compared sorted / not at all.
                if (p.first == 3) { mdl.push_back(sort_lists(p.second)); continue; }
                if (p.first >= 7) continue;
                if (p.first == 1)
                {
                    // A folder's fetch state and request timer depend on
                    // which reply touched it last (a parent's reply copies a
                    // fresh timer over the child's): not compared.
                    std::string l = p.second;
                    size_t f = l.find(" fetching=");
                    if (f != std::string::npos) l = l.substr(0, f);
                    mdl.push_back(l);
                    continue;
                }
            }
            else if (mMode == PHASED)
            {
                // Replies arriving together may fill a child vector in
                // either order: compared sorted.
                if (p.first == 3) { mdl.push_back(sort_lists(p.second)); continue; }
            }
            mdl.push_back(p.second);
        }
        if (mLarge)
        {
            digest_block(out, "mdl", mdl, 30, 10);
        }
        else
        {
            for (const std::string& l : mdl) out.push_back("mdl " + l);
        }

        // Measurements of the concurrency scenarios: the bounds are compared,
        // the values are informational (# lines are not compared).
        if (mConcurrent)
        {
            S32 mock_overlap = 0;
            for (const Wire& a : mWire)
            {
                S32 k = 0;
                for (const Wire& b : mWire)
                {
                    if (b.t_in <= a.t_in && b.t_out > a.t_in) ++k;
                }
                mock_overlap = llmax(mock_overlap, k);
            }
            out.push_back(std::string("bound viewer_in_flight<=19 ") + (mMaxInFlight <= 19 ? "yes" : "NO"));
            if (by_cap.count("inv") == 0 && by_cap.count("lib") == 0)
            {
                // Legacy capabilities only: at most 12 + 4 handlers.
                out.push_back(std::string("bound legacy_in_flight<=16 ") + (mMaxInFlight <= 16 ? "yes" : "NO"));
            }
            out.push_back(std::string("bound ais_pool<=20 ") + (mMaxPool <= 20 ? "yes" : "NO"));
            out.push_back("# measured viewer_max_in_flight=" + std::to_string(mMaxInFlight)
                          + " max_dispatch_per_frame=" + std::to_string(mMaxDispatch)
                          + " ais_pool_max=" + std::to_string(mMaxPool)
                          + " mock_max_overlap=" + std::to_string(mock_overlap)
                          + " frames=" + std::to_string(mFrames)
                          + " scheduler_states=" + std::to_string(mSnapCount));
        }

        for (const std::string& f : mFailures) out.push_back("FAILURE " + f);
        responses = resp_text;
        return c;
    }

    // Measurements a concurrency scenario compares exactly when it is built
    // to make them deterministic.
}

// ---- frozen copies -----------------------------------------------------------------
// LLAppearanceMgr's COF id (llappearancemgr.cpp is not linked; R2's stubs
// define only its constructor and getBaseOutfitUUID).

// FROZEN COPY begin: indra/newview/llappearancemgr.cpp, LLAppearanceMgr::getCOF, lines 1444-1447, blob a6c2adad3137a2e60378b8c6990090c8c3b1e099
const LLUUID LLAppearanceMgr::getCOF() const
{
    return mCOFID;
}
// FROZEN COPY end

// FROZEN COPY begin: indra/newview/llappearancemgr.cpp, LLAppearanceMgr::initCOFID, lines 1462-1465, blob a6c2adad3137a2e60378b8c6990090c8c3b1e099
void LLAppearanceMgr::initCOFID()
{
    mCOFID = gInventory.findCategoryUUIDForType(LLFolderType::FT_CURRENT_OUTFIT);
}
// FROZEN COPY end

namespace vks_fetch
{
namespace startup
{
    // llstartup.cpp's do_startup_frame() drains the UDP socket and draws;
    // neither is part of these scenarios.
    static void do_startup_frame() {}

    void inventory_send(const LLSD& response)
    {
// FROZEN COPY begin: indra/newview/llstartup.cpp, STATE_INVENTORY_SEND (library root and owner), lines 2654-2675, blob 54e82edeaf1e42d11fe86b0d2248797e2979842d
        LLSD inv_lib_root = response["inventory-lib-root"];
        if(inv_lib_root.isDefined())
        {
            // should only be one
            LLSD id = inv_lib_root[0]["folder_id"];
            if(id.isDefined())
            {
                gInventory.setLibraryRootFolderID(id.asUUID());
            }
        }
        do_startup_frame();

        LLSD inv_lib_owner = response["inventory-lib-owner"];
        if(inv_lib_owner.isDefined())
        {
            // should only be one
            LLSD id = inv_lib_owner[0]["agent_id"];
            if(id.isDefined())
            {
                gInventory.setLibraryOwnerID(LLUUID(id.asUUID()));
            }
        }
// FROZEN COPY end
    }

    void inventory_skel(const LLSD& response)
    {
// FROZEN COPY begin: indra/newview/llstartup.cpp, STATE_INVENTORY_SKEL, lines 2688-2707, blob 54e82edeaf1e42d11fe86b0d2248797e2979842d
        LLSD inv_skel_lib = response["inventory-skel-lib"];
        if (inv_skel_lib.isDefined() && gInventory.getLibraryOwnerID().notNull())
        {
            LL_PROFILE_ZONE_NAMED("load library inv")
            if (!gInventory.loadSkeleton(inv_skel_lib, gInventory.getLibraryOwnerID()))
            {
                LL_WARNS("AppInit") << "Problem loading inventory-skel-lib" << LL_ENDL;
            }
        }
        do_startup_frame();

        LLSD inv_skeleton = response["inventory-skeleton"];
        if (inv_skeleton.isDefined())
        {
            LL_PROFILE_ZONE_NAMED("load personal inv")
            if (!gInventory.loadSkeleton(inv_skeleton, gAgent.getID()))
            {
                LL_WARNS("AppInit") << "Problem loading inventory-skel-targets" << LL_ENDL;
            }
        }
// FROZEN COPY end
    }

    void inventory_send2()
    {
// FROZEN COPY begin: indra/newview/llstartup.cpp, STATE_INVENTORY_SEND2 (inventory part), lines 2828-2841, blob 54e82edeaf1e42d11fe86b0d2248797e2979842d
        gInventory.buildParentChildMap();

        // If buildParentChildMap succeeded, inventory will now be in
        // a usable state and gInventory.isInventoryUsable() will be
        // true.

        // if inventory is unusable, show warning.
        if (!gInventory.isInventoryUsable())
        {
            LLNotificationsUtil::add("InventoryUnusable");
        }

        LLInventoryModelBackgroundFetch::instance().start();
        gInventory.createCommonSystemCategories();
// FROZEN COPY end
    }

    bool inventory_callbacks()
    {
// FROZEN COPY begin: indra/newview/llstartup.cpp, STATE_INVENTORY_CALLBACKS, lines 2853-2870, blob 54e82edeaf1e42d11fe86b0d2248797e2979842d
        if (!LLInventoryModel::isSysFoldersReady())
        {
            do_startup_frame();
            return false;
        }

        LLInventoryModelBackgroundFetch::instance().start();
        LLAppearanceMgr::instance().initCOFID();
        LLUUID cof_id = LLAppearanceMgr::instance().getCOF();
        LLViewerInventoryCategory* cof = gInventory.getCategory(cof_id);
        if (cof
            && cof->getVersion() == LLViewerInventoryCategory::VERSION_UNKNOWN)
        {
            // Special case, dupplicate request prevention.
            // Cof folder will be requested via FetchCOF
            // in appearance manager, prevent recursive fetch
            cof->setFetching(LLViewerInventoryCategory::FETCH_RECURSIVE);
        }
// FROZEN COPY end
        return true;
    }

    void find_lost_items()
    {
// FROZEN COPY begin: indra/newview/llstartup.cpp, STATE_STARTED (lost items), lines 3147-3148, blob 54e82edeaf1e42d11fe86b0d2248797e2979842d
        //DEV-17797.  get null folder.  Any items found here moved to Lost and Found
        LLInventoryModelBackgroundFetch::instance().findLostItems();
// FROZEN COPY end
    }
}

namespace appearance
{
    // The COF fetch observer of llappearancemgr.cpp: its startFetch() is the
    // frozen copy; done() records instead of starting stage 2 (wearing).
    class CallAfterCategoryFetchStage1 : public LLInventoryFetchDescendentsObserver
    {
    public:
// FROZEN COPY begin: indra/newview/llappearancemgr.cpp, CallAfterCategoryFetchStage1::CallAfterCategoryFetchStage1, lines 5109-5113, blob a6c2adad3137a2e60378b8c6990090c8c3b1e099
    CallAfterCategoryFetchStage1(const LLUUID& cat_id, nullary_func_t callable) :
        LLInventoryFetchDescendentsObserver(cat_id),
        mCallable(callable)
    {
    }
// FROZEN COPY end
        static void* operator new(size_t size) { return vks_inv::observer_slot(1, size); }
        static void operator delete(void*) {}

// FROZEN COPY begin: indra/newview/llappearancemgr.cpp, CallAfterCategoryFetchStage1::startFetch, lines 5117-5183, blob a6c2adad3137a2e60378b8c6990090c8c3b1e099
    /*virtual*/ void startFetch()
    {
        bool ais3 = AISAPI::isAvailable();
        for (uuid_vec_t::const_iterator it = mIDs.begin(); it != mIDs.end(); ++it)
        {
            LLViewerInventoryCategory* cat = gInventory.getCategory(*it);
            if (!cat) continue;
            if (cat->getVersion() == LLViewerInventoryCategory::VERSION_UNKNOWN)
            {
                // CHECK IT: isCategoryComplete() checks both version and descendant count but
                // fetch() only works for Unknown version and doesn't care about descentants,
                // as result fetch won't start and folder will potentially get stuck as
                // incomplete in observer.
                // Likely either both should use only version or both should check descendants.
                cat->fetch();       //blindly fetch it without seeing if anything else is fetching it.
                mIncomplete.push_back(*it); //Add to list of things being downloaded for this observer.
            }
            else if (!isCategoryComplete(cat))
            {
                LL_DEBUGS("Inventory") << "Categoty " << *it << " incomplete despite having version" << LL_ENDL;
                LLInventoryModelBackgroundFetch::instance().scheduleFolderFetch(*it, true);
                mIncomplete.push_back(*it);
            }
            else if (ais3)
            {
                LLInventoryModel::cat_array_t* cats;
                LLInventoryModel::item_array_t* items;
                gInventory.getDirectDescendentsOf(cat->getUUID(), cats, items);

                if (items)
                {
                    S32 complete_count = 0;
                    S32 incomplete_count = 0;
                    for (LLInventoryModel::item_array_t::const_iterator it = items->begin(); it < items->end(); ++it)
                    {
                        if (!(*it)->isFinished())
                        {
                            incomplete_count++;
                        }
                        else
                        {
                            complete_count++;
                        }
                    }
                    // AIS can fetch couple items, but if there
                    // is more than a dozen it will be very slow
                    // it's faster to get whole folder in such case
                    if (incomplete_count > LLInventoryFetchItemsObserver::MAX_INDIVIDUAL_ITEM_REQUESTS
                        || (incomplete_count > 1 && complete_count == 0))
                    {
                        LLInventoryModelBackgroundFetch::instance().scheduleFolderFetch(*it, true);
                        mIncomplete.push_back(*it);
                    }
                    else
                    {
                        // let stage2 handle incomplete ones
                        mComplete.push_back(*it);
                    }
                }
                // else should have been handled by isCategoryComplete
            }
            else
            {
                mComplete.push_back(*it);
            }
        }
    }
// FROZEN COPY end

        void done() override
        {
            vks_inv::note("cof.stage1.done complete=" + std::to_string(mComplete.size())
                          + " incomplete=" + std::to_string(mIncomplete.size()));
            gInventory.removeObserver(this);
            if (mCallable) mCallable();
            delete this;
        }
    protected:
        nullary_func_t mCallable;
    };

// FROZEN COPY begin: indra/newview/llappearancemgr.cpp, callAfterCOFFetch, lines 5247-5266, blob a6c2adad3137a2e60378b8c6990090c8c3b1e099
void callAfterCOFFetch(nullary_func_t cb)
{
    if (AISAPI::isAvailable())
    {
        // For reliability assume that we have no relevant cache, so
        // fetch cof along with items cof's links point to.
        LLInventoryModelBackgroundFetch::getInstance()->fetchCOF(cb);
    }
    else
    {
        LL_INFOS() << "AIS API v3 not available, using callAfterCategoryFetch" << LL_ENDL;
        LLUUID cat_id = LLAppearanceMgr::instance().getCOF();
        LLViewerInventoryCategory* cat = gInventory.getCategory(cat_id);

        // Special case, startup should have marked cof as FETCH_RECURSIVE
        // to prevent dupplicate request, remove that
        cat->setFetching(LLViewerInventoryCategory::FETCH_NONE);
        callAfterCategoryFetch(cat_id, cb);
    }
}
// FROZEN COPY end

// FROZEN COPY begin: indra/newview/llappearancemgr.cpp, callAfterCategoryFetch, lines 5268-5280, blob a6c2adad3137a2e60378b8c6990090c8c3b1e099
void callAfterCategoryFetch(const LLUUID& cat_id, nullary_func_t cb)
{
    CallAfterCategoryFetchStage1* stage1 = new CallAfterCategoryFetchStage1(cat_id, cb);
    stage1->startFetch();
    if (stage1->isFinished())
    {
        stage1->done();
    }
    else
    {
        gInventory.addObserver(stage1);
    }
}
// FROZEN COPY end

// FROZEN COPY begin: indra/newview/llappearancemgr.cpp, callAfterCategoryLinksFetch, lines 5282-5295, blob a6c2adad3137a2e60378b8c6990090c8c3b1e099
void callAfterCategoryLinksFetch(const LLUUID &cat_id, nullary_func_t cb)
{
    if (AISAPI::isAvailable())
    {
        // Assume that we have no relevant cache. Fetch folder, and items folder's links point to.
        LLInventoryModelBackgroundFetch::getInstance()->fetchFolderAndLinks(cat_id, cb);
    }
    else
    {
        LL_WARNS() << "AIS API v3 not available, can't use AISAPI::FetchCOF" << LL_ENDL;
        callAfterCategoryFetch(cat_id, cb);
    }

}
// FROZEN COPY end
}
}
