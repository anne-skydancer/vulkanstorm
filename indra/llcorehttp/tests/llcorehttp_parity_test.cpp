/**
 * @file llcorehttp_parity_test.cpp
 * @brief Behavioural parity of the HTTP layer against recorded scenarios.
 *
 * Drives LLCore::HttpRequest through a scripted set of scenarios against the
 * local mock server (tests/vkshttp_mock_server.py, which runs this
 * executable and records every request it receives) and compares the
 * request sequences on the wire, the retry timings, the per-class
 * concurrency and the responses delivered to the handlers with the record
 * made from the C++ implementation before its port to Rust (vkstorm-rust):
 * tests/fixtures/llcorehttp_parity.xml.
 *
 * VKS_HTTP_RECORD=<file>   writes the record instead of comparing
 * VKS_HTTP_SCENARIOS=a,b   runs only the named scenarios
 * VKS_HTTP_TRACE=<level>   sets the global trace policy option (0-3)
 * VKS_HTTP_BENCH=<n>       runs the timing scenarios with n iterations
 * VKS_HTTP_BENCH_OUT=<csv> appends the timing results to a CSV file
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#include "linden_common.h"
#include "lltut.h"

#include "httpcommon.h"
#include "httprequest.h"
#include "httphandler.h"
#include "httpresponse.h"
#include "httpoptions.h"
#include "httpheaders.h"
#include "bufferarray.h"
#include "_httpservice.h"
#include "llmemory.h"
#include "llmd5.h"
#include "llsd.h"
#include "llsdserialize.h"
#include "llsdjson.h"
#include "llstring.h"
#include "llbase64.h"
#include "llproxy.h"
#include "llcleanup.h"

#include <boost/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <set>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <curl/curl.h>
#if LL_WINDOWS
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")
#endif

#ifndef VKS_HTTP_FIXTURE_DIR
#error VKS_HTTP_FIXTURE_DIR must name the directory of the recorded scenarios
#endif
#define VKS_HTTP_FIXTURE VKS_HTTP_FIXTURE_DIR "/llcorehttp_parity.xml"

using namespace LLCore;

namespace
{
    using tut::ensure;
    using tut::ensure_equals;

    typedef std::chrono::steady_clock steady;

    double ms_since(steady::time_point t0)
    {
        return std::chrono::duration<double, std::milli>(steady::now() - t0).count();
    }

    void sleep_ms(int ms)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }

    std::string env_or(const char* name, const std::string& dflt)
    {
        const char* v = getenv(name);
        return v ? std::string(v) : dflt;
    }

    bool wanted(const std::string& scenario)
    {
        const std::string only = env_or("VKS_HTTP_SCENARIOS", "");
        if (only.empty()) return true;
        std::stringstream ss(only);
        std::string item;
        while (std::getline(ss, item, ','))
        {
            if (item == scenario) return true;
        }
        return false;
    }

    std::string port_string()
    {
        const char* env = getenv("LL_TEST_PORT");
        ensure("LL_TEST_PORT set in environment (run under vkshttp_mock_server.py)", env != NULL);
        return env;
    }

    std::string base_url()
    {
        return "http://127.0.0.1:" + port_string() + "/";
    }

    // The mock server's port changes per run; every recorded string carries
    // "{PORT}" in its place.
    std::string normalise_port(std::string s)
    {
        LLStringUtil::replaceString(s, port_string(), "{PORT}");
        return s;
    }

    // Request heads carry CRLF, which XML normalises away; keep them on one
    // line with the line ends spelled out.
    std::string escape_crlf(const std::string& s)
    {
        std::string out;
        out.reserve(s.size() + 16);
        for (size_t i = 0; i < s.size(); ++i)
        {
            if (s[i] == '\r') out += "\\r";
            else if (s[i] == '\n') out += "\\n";
            else out += s[i];
        }
        return out;
    }

    std::string md5_hex(const std::string& bytes)
    {
        if (bytes.empty()) return "d41d8cd98f00b204e9800998ecf8427e";
        LLMD5 md5;
        md5.update(reinterpret_cast<const unsigned char*>(bytes.data()), static_cast<unsigned int>(bytes.size()));
        md5.finalize();
        char hex[33];
        md5.hex_digest(hex);
        return hex;
    }

    std::string body_bytes(BufferArray* ba)
    {
        std::string out;
        if (ba && ba->size())
        {
            out.resize(ba->size());
            ba->read(0, &out[0], out.size());
        }
        return out;
    }

    BufferArray* make_body(const std::string& bytes)
    {
        BufferArray* ba = new BufferArray();
        ba->append(bytes.data(), bytes.size());
        return ba;
    }

    // What a handler received, in the order of delivery.
    struct Outcome
    {
        std::string name;
        HttpHandle handle;
        HttpStatus status;
        std::string body;
        bool hasHeaders;
        HttpHeaders::container_t headers;
        std::string contentType;
        unsigned int offset, length, full;
        unsigned int retries, retries503;
        std::string url;
        std::string method;
        double doneMs;
    };

    class Collector : public HttpHandler
    {
    public:
        Collector(steady::time_point t0) : mT0(t0) {}

        virtual void onCompleted(HttpHandle handle, HttpResponse* response)
        {
            Outcome o;
            o.name = mNames[handle];
            o.handle = handle;
            o.status = response->getStatus();
            o.body = body_bytes(response->getBody());
            HttpHeaders::ptr_t hdrs = response->getHeaders();
            o.hasHeaders = static_cast<bool>(hdrs);
            if (hdrs)
            {
                o.headers.assign(hdrs->begin(), hdrs->end());
            }
            o.contentType = response->getContentType();
            response->getRange(&o.offset, &o.length, &o.full);
            response->getRetries(&o.retries, &o.retries503);
            o.url = response->getRequestURL();
            o.method = response->getRequestMethod();
            o.doneMs = ms_since(mT0);
            mOutcomes.push_back(o);
        }

        void name(HttpHandle handle, const std::string& n) { mNames[handle] = n; }
        size_t count() const { return mOutcomes.size(); }

        std::vector<Outcome> mOutcomes;
        std::map<HttpHandle, std::string> mNames;
        steady::time_point mT0;
    };

    struct NoOpDeleter { void operator()(HttpHandler*) const {} };

    // One recorded request on the mock server.
    struct WireRequest
    {
        int conn, seq;
        double t, rspT;
        bool answered;
        std::string method, target, version, rawHead, group;
        std::vector<std::pair<std::string, std::string> > headers;
        std::string body;
        bool chunkedUpload;
    };

    // Reads the mock server's log and returns the requests whose arrival
    // time lies between the two markers.
    std::vector<WireRequest> read_wire(const std::string& markBegin, const std::string& markEnd)
    {
        std::vector<WireRequest> out;
        const std::string path = env_or("VKS_HTTP_MOCK_LOG", "");
        ensure("VKS_HTTP_MOCK_LOG set in environment", !path.empty());
        std::ifstream in(path.c_str(), std::ios::binary);
        ensure("mock log readable", in.good());
        double tBegin = -1, tEnd = -1;
        std::vector<LLSD> reqs;
        std::map<std::pair<int, int>, double> rsps;
        std::string line;
        while (std::getline(in, line))
        {
            if (line.empty()) continue;
            boost::system::error_code ec;
            boost::json::value v = boost::json::parse(line, ec);
            if (ec.failed()) continue;
            LLSD rec = LlsdFromJson(v);
            if (rec["ev"].asString() == "rsp")
            {
                rsps[std::make_pair(rec["conn"].asInteger(), rec["seq"].asInteger())] = rec["t"].asReal();
                continue;
            }
            if (rec.has("marker"))
            {
                if (rec["marker"].asString() == markBegin) tBegin = rec["t"].asReal();
                if (rec["marker"].asString() == markEnd) tEnd = rec["t"].asReal();
                continue;
            }
            reqs.push_back(rec);
        }
        ensure("begin marker " + markBegin + " found", tBegin >= 0);
        ensure("end marker " + markEnd + " found", tEnd >= 0);
        for (size_t i = 0; i < reqs.size(); ++i)
        {
            const LLSD& rec = reqs[i];
            const double t = rec["t"].asReal();
            if (t <= tBegin || t >= tEnd) continue;
            WireRequest w;
            w.conn = rec["conn"].asInteger();
            w.seq = rec["seq"].asInteger();
            w.t = t;
            w.method = rec["method"].asString();
            w.target = normalise_port(rec["target"].asString());
            w.version = rec["version"].asString();
            w.rawHead = escape_crlf(normalise_port(LLBase64::decodeAsString(rec["raw_head"].asString())));
            w.body = LLBase64::decodeAsString(rec["body"].asString());
            w.chunkedUpload = rec["chunked_upload"].asBoolean();
            for (LLSD::array_const_iterator h = rec["headers"].beginArray(); h != rec["headers"].endArray(); ++h)
            {
                w.headers.push_back(std::make_pair((*h)[0].asString(), normalise_port((*h)[1].asString())));
            }
            std::map<std::pair<int, int>, double>::const_iterator r = rsps.find(std::make_pair(w.conn, w.seq));
            w.answered = r != rsps.end();
            w.rspT = w.answered ? r->second : 0.0;
            // The policy-class group of a /slow/<ms>/<group>/... target.
            std::vector<std::string> parts;
            std::string rest = w.target;
            size_t pos;
            while ((pos = rest.find('/')) != std::string::npos)
            {
                if (pos) parts.push_back(rest.substr(0, pos));
                rest = rest.substr(pos + 1);
            }
            if (!rest.empty()) parts.push_back(rest);
            if (parts.size() >= 3 && parts[0] == "slow") w.group = parts[2];
            out.push_back(w);
        }
        std::sort(out.begin(), out.end(), [](const WireRequest& a, const WireRequest& b) { return a.t < b.t; });
        // Connections are numbered per scenario in order of first use; the
        // server's own numbering runs across the whole run.
        std::map<int, int> renumber;
        for (size_t i = 0; i < out.size(); ++i)
        {
            std::map<int, int>::iterator it(renumber.find(out[i].conn));
            if (it == renumber.end())
            {
                it = renumber.insert(std::make_pair(out[i].conn, static_cast<int>(renumber.size()) + 1)).first;
            }
            out[i].conn = it->second;
        }
        return out;
    }

    LLSD outcome_to_llsd(const Outcome& o, bool withTime)
    {
        LLSD r;
        r["name"] = o.name;
        r["type"] = static_cast<LLSD::Integer>(o.status.getType());
        r["code"] = static_cast<LLSD::Integer>(o.status.getStatus());
        r["body_len"] = static_cast<LLSD::Integer>(o.body.size());
        r["body_md5"] = md5_hex(o.body);
        if (o.body.size() <= 256) r["body"] = LLSD::Binary(o.body.begin(), o.body.end());
        r["content_type"] = o.contentType;
        r["range"] = LLSD::emptyArray();
        r["range"].append(static_cast<LLSD::Integer>(o.offset));
        r["range"].append(static_cast<LLSD::Integer>(o.length));
        r["range"].append(static_cast<LLSD::Integer>(o.full));
        r["retries"] = static_cast<LLSD::Integer>(o.retries);
        r["retries_503"] = static_cast<LLSD::Integer>(o.retries503);
        r["url"] = normalise_port(o.url);
        r["method"] = o.method;
        if (o.hasHeaders)
        {
            r["headers"] = LLSD::emptyArray();
            for (size_t i = 0; i < o.headers.size(); ++i)
            {
                LLSD h = LLSD::emptyArray();
                h.append(o.headers[i].first);
                h.append(normalise_port(o.headers[i].second));
                r["headers"].append(h);
            }
        }
        if (withTime) r["done_ms"] = o.doneMs;
        return r;
    }

    LLSD wire_to_llsd(const WireRequest& w, double t0)
    {
        LLSD r;
        r["conn"] = w.conn;
        r["seq"] = w.seq;
        r["t"] = w.t - t0;
        r["answered"] = w.answered;
        if (w.answered) r["rsp_t"] = w.rspT - t0;
        r["method"] = w.method;
        r["target"] = w.target;
        r["version"] = w.version;
        r["raw_head"] = w.rawHead;
        r["body_len"] = static_cast<LLSD::Integer>(w.body.size());
        r["body_md5"] = md5_hex(w.body);
        if (w.body.size() <= 256) r["body"] = LLSD::Binary(w.body.begin(), w.body.end());
        r["chunked_upload"] = w.chunkedUpload;
        r["group"] = w.group;
        return r;
    }

    // Largest number of requests of one group in flight at the same moment.
    LLSD max_overlap(const std::vector<WireRequest>& wire)
    {
        LLSD out = LLSD::emptyMap();
        std::map<std::string, std::vector<std::pair<double, double> > > byGroup;
        for (size_t i = 0; i < wire.size(); ++i)
        {
            if (!wire[i].answered) continue;
            byGroup[wire[i].group].push_back(std::make_pair(wire[i].t, wire[i].rspT));
        }
        for (std::map<std::string, std::vector<std::pair<double, double> > >::const_iterator g = byGroup.begin(); g != byGroup.end(); ++g)
        {
            int best = 0;
            for (size_t i = 0; i < g->second.size(); ++i)
            {
                int n = 0;
                for (size_t j = 0; j < g->second.size(); ++j)
                {
                    if (g->second[j].first <= g->second[i].first && g->second[j].second > g->second[i].first) ++n;
                }
                best = std::max(best, n);
            }
            out[g->first] = best;
        }
        return out;
    }

    // The record of one scenario, written to the fixture or compared with it.
    class Scenario
    {
    public:
        enum Mode { SEQUENTIAL, CONCURRENT };

        Scenario(const std::string& name, Mode mode)
            : mName(name), mMode(mode), mT0(steady::now()), mHandler(mT0), mRequest(NULL), mStarted(false), mIssued(0)
        {
            mHandlerPtr = HttpHandler::ptr_t(&mHandler, NoOpDeleter());
        }

        ~Scenario()
        {
            if (mStarted) stop();
        }

        // Creates the service; static policy is set through the returned
        // request on the caller's side before start().
        void create()
        {
            HttpRequest::createService();
            const std::string trace = env_or("VKS_HTTP_TRACE", "");
            if (!trace.empty())
            {
                HttpRequest::setStaticPolicyOption(HttpRequest::PO_TRACE, HttpRequest::GLOBAL_POLICY_ID, atol(trace.c_str()), NULL);
            }
        }

        HttpRequest::policy_t createClass()
        {
            return HttpRequest::createPolicyClass();
        }

        void start()
        {
            HttpRequest::startThread();
            mRequest = new HttpRequest();
            mStarted = true;
            mark(mName + "/begin");
        }

        HttpRequest* request() { return mRequest; }
        Collector& handler() { return mHandler; }
        HttpHandler::ptr_t handlerPtr() { return mHandlerPtr; }

        // Runs update() until the handler has seen `count` completions.
        void waitFor(size_t count, int limitMs = 60000)
        {
            const steady::time_point t0 = steady::now();
            while (mHandler.count() < count)
            {
                mRequest->update(0);
                if (mHandler.count() >= count) break;
                ensure("scenario " + mName + " completed in time", ms_since(t0) < limitMs);
                sleep_ms(1);
            }
        }

        // Handles are recycled once delivered (the C++ used the operation's
        // address), so the count of issued requests is kept separately.
        void waitAll(int limitMs = 60000)
        {
            waitFor(mIssued, limitMs);
        }

        HttpHandle name(HttpHandle handle, const std::string& n)
        {
            ensure("valid handle for " + n + " (" + mRequest->getStatus().toTerseString() + ")", handle != LLCORE_HTTP_HANDLE_INVALID);
            mHandler.name(handle, n);
            ++mIssued;
            return handle;
        }

        // A marker request, awaited, excluded from the record.
        void mark(const std::string& m)
        {
            Collector marker(mT0);
            HttpHandler::ptr_t ptr(&marker, NoOpDeleter());
            HttpHandle h = mRequest->requestGet(HttpRequest::DEFAULT_POLICY_ID, base_url() + "mark/" + m,
                                                HttpOptions::ptr_t(), HttpHeaders::ptr_t(), ptr);
            ensure("marker request issued", h != LLCORE_HTTP_HANDLE_INVALID);
            const steady::time_point t0 = steady::now();
            while (marker.count() < 1)
            {
                mRequest->update(0);
                ensure("marker " + m + " answered", ms_since(t0) < 30000);
                sleep_ms(1);
            }
            ensure("marker " + m + " succeeded", static_cast<bool>(marker.mOutcomes[0].status));
        }

        void stop()
        {
            if (mRequest)
            {
                mark(mName + "/end");
                Collector stopper(mT0);
                HttpHandler::ptr_t ptr(&stopper, NoOpDeleter());
                mRequest->requestStopThread(ptr);
                const steady::time_point t0 = steady::now();
                while (!HttpService::isStopped() && ms_since(t0) < 10000)
                {
                    mRequest->update(0);
                    sleep_ms(1);
                }
                ensure("service thread stopped", HttpService::isStopped());
                delete mRequest;
                mRequest = NULL;
            }
            HttpRequest::destroyService();
            mStarted = false;
        }

        // Ends the scenario: stops the service, gathers the wire record and
        // either writes it to the fixture or compares it.
        void finish()
        {
            stop();
            std::vector<WireRequest> wire = read_wire(mName + "/begin", mName + "/end");
            LLSD record = LLSD::emptyMap();
            record["mode"] = mMode == SEQUENTIAL ? "sequential" : "concurrent";
            const double t0 = wire.empty() ? 0.0 : wire.front().t;
            record["requests"] = LLSD::emptyArray();
            for (size_t i = 0; i < wire.size(); ++i) record["requests"].append(wire_to_llsd(wire[i], t0));
            record["results"] = LLSD::emptyArray();
            for (size_t i = 0; i < mHandler.mOutcomes.size(); ++i) record["results"].append(outcome_to_llsd(mHandler.mOutcomes[i], true));
            std::set<int> conns;
            for (size_t i = 0; i < wire.size(); ++i) conns.insert(wire[i].conn);
            record["connections"] = static_cast<LLSD::Integer>(conns.size());
            record["max_overlap"] = max_overlap(wire);
            for (size_t i = 0; i < mExtra.size(); ++i) record["extra"].append(mExtra[i]);

            const std::string recordPath = env_or("VKS_HTTP_RECORD", "");
            if (!recordPath.empty())
            {
                writeRecord(recordPath, record);
            }
            else
            {
                compare(record);
            }
        }

        // Free-form results of API calls that make no request.
        void extra(const LLSD& e) { mExtra.push_back(e); }

    private:
        static LLSD& fixture()
        {
            static LLSD sFixture;
            static bool sLoaded = false;
            if (!sLoaded)
            {
                sLoaded = true;
                std::ifstream in(VKS_HTTP_FIXTURE, std::ios::binary);
                if (in.good())
                {
                    LLSDSerialize::fromXML(sFixture, in);
                }
            }
            return sFixture;
        }

        // The scenarios of one run accumulate in memory; the file is
        // rewritten whole after each so a failing scenario loses nothing.
        void writeRecord(const std::string& path, const LLSD& record)
        {
            static LLSD all;
            all[mName] = record;
            std::ofstream out(path.c_str(), std::ios::binary | std::ios::trunc);
            ensure("record file writable: " + path, out.good());
            LLSDSerialize::toPrettyXML(all, out);
        }

        static std::string llsd_text(const LLSD& v)
        {
            std::ostringstream s;
            LLSDSerialize::toNotation(v, s);
            return s.str();
        }

        static std::string request_key(const LLSD& r)
        {
            return r["method"].asString() + " " + r["target"].asString() + " " + r["raw_head"].asString()
                + " body:" + r["body_md5"].asString();
        }

        static std::string result_key(const LLSD& r)
        {
            // A body cut short by the server's close is CURLE_PARTIAL_FILE
            // when libcurl sees the FIN before it reads, CURLE_RECV_ERROR
            // when the close arrives as a reset first; the same libcurl
            // gives either from run to run, both are retryable.
            LLSD code = r["code"];
            if (r["type"].asInteger() == 0 && code.asInteger() == 56 && r["name"].asString().find("partial") == 0)
            {
                code = 18;
            }
            return r["name"].asString() + " " + llsd_text(r["type"]) + "/" + llsd_text(code)
                + " " + r["method"].asString() + " " + r["url"].asString()
                + " body:" + r["body_md5"].asString() + " ct:" + r["content_type"].asString()
                + " range:" + llsd_text(r["range"]) + " retries:" + llsd_text(r["retries"]) + "/" + llsd_text(r["retries_503"])
                + " headers:" + llsd_text(r["headers"]);
        }

        static bool close_enough(double a, double b)
        {
            const double tol = std::max(300.0, 0.3 * std::max(std::fabs(a), std::fabs(b)));
            return std::fabs(a - b) <= tol;
        }

        void compare(const LLSD& actual)
        {
            const LLSD& all = fixture();
            ensure("fixture " VKS_HTTP_FIXTURE " has scenario " + mName, all.has(mName));
            const LLSD& expected = all[mName];
            std::ostringstream diff;

            const LLSD& ereq = expected["requests"];
            const LLSD& areq = actual["requests"];
            if (ereq.size() != areq.size())
            {
                diff << "request count: recorded " << ereq.size() << ", now " << areq.size() << "\n";
            }
            if (mMode == SEQUENTIAL)
            {
                const size_t n = std::min(ereq.size(), areq.size());
                for (size_t i = 0; i < n; ++i)
                {
                    if (request_key(ereq[i]) != request_key(areq[i]))
                    {
                        diff << "request " << i << " differs:\n  recorded: " << llsd_text(ereq[i]) << "\n  now:      " << llsd_text(areq[i]) << "\n";
                    }
                    if (ereq[i]["conn"].asInteger() != areq[i]["conn"].asInteger() || ereq[i]["seq"].asInteger() != areq[i]["seq"].asInteger())
                    {
                        diff << "request " << i << " connection/sequence: recorded " << ereq[i]["conn"].asInteger() << "/" << ereq[i]["seq"].asInteger()
                             << ", now " << areq[i]["conn"].asInteger() << "/" << areq[i]["seq"].asInteger() << "\n";
                    }
                    // Timing is the layer's own only between the attempts of
                    // one request (the retry backoff), recognised by an
                    // identical request head; the gaps between different
                    // requests are the test's and the platform's.
                    if (i > 0 && ereq[i]["raw_head"].asString() == ereq[i - 1]["raw_head"].asString()
                        && ereq[i]["body_md5"].asString() == ereq[i - 1]["body_md5"].asString())
                    {
                        const double egap = ereq[i]["t"].asReal() - ereq[i - 1]["t"].asReal();
                        const double agap = areq[i]["t"].asReal() - areq[i - 1]["t"].asReal();
                        if (!close_enough(egap, agap))
                        {
                            diff << "request " << i << " retry gap: recorded " << egap << " ms, now " << agap << " ms\n";
                        }
                    }
                }
            }
            else
            {
                std::multiset<std::string> e, a;
                for (size_t i = 0; i < ereq.size(); ++i) e.insert(request_key(ereq[i]));
                for (size_t i = 0; i < areq.size(); ++i) a.insert(request_key(areq[i]));
                if (e != a)
                {
                    diff << "request multiset differs:\n";
                    for (std::multiset<std::string>::const_iterator it = e.begin(); it != e.end(); ++it)
                        if (!a.count(*it)) diff << "  recorded only: " << *it << "\n";
                    for (std::multiset<std::string>::const_iterator it = a.begin(); it != a.end(); ++it)
                        if (!e.count(*it)) diff << "  now only: " << *it << "\n";
                }
                if (llsd_text(expected["max_overlap"]) != llsd_text(actual["max_overlap"]))
                {
                    diff << "max overlap per class: recorded " << llsd_text(expected["max_overlap"]) << ", now " << llsd_text(actual["max_overlap"]) << "\n";
                }
                // Per group, the arrival times in order.
                std::map<std::string, std::vector<double> > eg, ag;
                for (size_t i = 0; i < ereq.size(); ++i) eg[ereq[i]["group"].asString()].push_back(ereq[i]["t"].asReal());
                for (size_t i = 0; i < areq.size(); ++i) ag[areq[i]["group"].asString()].push_back(areq[i]["t"].asReal());
                for (std::map<std::string, std::vector<double> >::const_iterator g = eg.begin(); g != eg.end(); ++g)
                {
                    std::vector<double> ev = g->second, av = ag[g->first];
                    std::sort(ev.begin(), ev.end());
                    std::sort(av.begin(), av.end());
                    for (size_t i = 0; i < std::min(ev.size(), av.size()); ++i)
                    {
                        if (!close_enough(ev[i], av[i]))
                        {
                            diff << "group " << g->first << " arrival " << i << ": recorded " << ev[i] << " ms, now " << av[i] << " ms\n";
                        }
                    }
                }
            }
            if (expected["connections"].asInteger() != actual["connections"].asInteger())
            {
                diff << "connections used: recorded " << expected["connections"].asInteger() << ", now " << actual["connections"].asInteger() << "\n";
            }

            const LLSD& eres = expected["results"];
            const LLSD& ares = actual["results"];
            if (eres.size() != ares.size())
            {
                diff << "result count: recorded " << eres.size() << ", now " << ares.size() << "\n";
            }
            if (mMode == SEQUENTIAL)
            {
                for (size_t i = 0; i < std::min(eres.size(), ares.size()); ++i)
                {
                    if (result_key(eres[i]) != result_key(ares[i]))
                    {
                        diff << "result " << i << " differs:\n  recorded: " << result_key(eres[i]) << "\n  now:      " << result_key(ares[i]) << "\n";
                    }
                }
            }
            else
            {
                std::multiset<std::string> e, a;
                for (size_t i = 0; i < eres.size(); ++i) e.insert(result_key(eres[i]));
                for (size_t i = 0; i < ares.size(); ++i) a.insert(result_key(ares[i]));
                if (e != a)
                {
                    diff << "result multiset differs:\n";
                    for (std::multiset<std::string>::const_iterator it = e.begin(); it != e.end(); ++it)
                        if (!a.count(*it)) diff << "  recorded only: " << *it << "\n";
                    for (std::multiset<std::string>::const_iterator it = a.begin(); it != a.end(); ++it)
                        if (!e.count(*it)) diff << "  now only: " << *it << "\n";
                }
            }
            if (llsd_text(expected["extra"]) != llsd_text(actual["extra"]))
            {
                diff << "extra results differ:\n  recorded: " << llsd_text(expected["extra"]) << "\n  now:      " << llsd_text(actual["extra"]) << "\n";
            }
            const std::string d = diff.str();
            if (!d.empty())
            {
                std::cerr << "scenario " << mName << " differs from the record:\n" << d << std::endl;
            }
            ensure("scenario " + mName + " matches the record (see stderr)", d.empty());
        }

        std::string mName;
        Mode mMode;
        steady::time_point mT0;
        Collector mHandler;
        HttpHandler::ptr_t mHandlerPtr;
        HttpRequest* mRequest;
        bool mStarted;
        size_t mIssued;
        std::vector<LLSD> mExtra;
    };

    HttpOptions::ptr_t options(unsigned int retries = 5, bool wantHeaders = false)
    {
        HttpOptions::ptr_t o(new HttpOptions());
        o->setRetries(retries);
        o->setWantHeaders(wantHeaders);
        return o;
    }

    LLSD status_llsd(const std::string& what, const HttpStatus& s)
    {
        LLSD r;
        r["what"] = what;
        r["type"] = static_cast<LLSD::Integer>(s.getType());
        r["code"] = static_cast<LLSD::Integer>(s.getStatus());
        return r;
    }

    const std::string LLSD_BODY("<?xml version=\"1.0\" ?><llsd><map><key>agent_id</key><uuid>0f1e2d3c-4b5a-6978-8796-a5b4c3d2e1f0</uuid>"
                                "<key>message</key><string>parity</string><key>count</key><integer>42</integer></map></llsd>");

    struct CurlInit
    {
        CurlInit() { curl_global_init(CURL_GLOBAL_ALL); LLProxy::getInstance(); }
        ~CurlInit() { SUBSYSTEM_CLEANUP(LLProxy); }
    };
}

namespace tut
{
    struct parity_data
    {
        CurlInit mCurl;
    };
    typedef test_group<parity_data> parity_group;
    typedef parity_group::object parity_object;
    parity_group parity("llcorehttp parity");

    // GET with headers wanted.
    template<> template<>
    void parity_object::test<1>()
    {
        set_test_name("get_ok");
        if (!wanted("get_ok")) return;
        Scenario s("get_ok", Scenario::SEQUENTIAL);
        s.create();
        s.start();
        HttpRequest* req = s.request();
        s.name(req->requestGet(HttpRequest::DEFAULT_POLICY_ID, base_url() + "ok/", options(5, true), HttpHeaders::ptr_t(), s.handlerPtr()), "get");
        s.waitAll();
        s.name(req->requestGet(HttpRequest::DEFAULT_POLICY_ID, base_url() + "ok/", options(5, false), HttpHeaders::ptr_t(), s.handlerPtr()), "get_noheaders");
        s.waitAll();
        s.name(req->requestNoOp(s.handlerPtr()), "noop");
        s.waitAll();
        s.finish();
    }

    // Every method, with and without bodies, and HEAD.
    template<> template<>
    void parity_object::test<2>()
    {
        set_test_name("methods");
        if (!wanted("methods")) return;
        Scenario s("methods", Scenario::SEQUENTIAL);
        s.create();
        s.start();
        HttpRequest* req = s.request();
        HttpHeaders::ptr_t llsdHeaders(new HttpHeaders());
        llsdHeaders->append("Content-Type", "application/llsd+xml");
        llsdHeaders->append("Accept", "application/llsd+xml");
        BufferArray* body = make_body(LLSD_BODY);
        s.name(req->requestPost(HttpRequest::DEFAULT_POLICY_ID, base_url() + "echo/", body, options(), llsdHeaders, s.handlerPtr()), "post");
        s.waitAll();
        s.name(req->requestPut(HttpRequest::DEFAULT_POLICY_ID, base_url() + "echo/", body, options(), llsdHeaders, s.handlerPtr()), "put");
        s.waitAll();
        s.name(req->requestPatch(HttpRequest::DEFAULT_POLICY_ID, base_url() + "echo/", body, options(), llsdHeaders, s.handlerPtr()), "patch");
        s.waitAll();
        body->release();
        BufferArray* empty = new BufferArray();
        s.name(req->requestPost(HttpRequest::DEFAULT_POLICY_ID, base_url() + "echo/", empty, options(), HttpHeaders::ptr_t(), s.handlerPtr()), "post_empty");
        s.waitAll();
        s.name(req->requestPut(HttpRequest::DEFAULT_POLICY_ID, base_url() + "echo/", NULL, options(), HttpHeaders::ptr_t(), s.handlerPtr()), "put_null");
        s.waitAll();
        empty->release();
        s.name(req->requestDelete(HttpRequest::DEFAULT_POLICY_ID, base_url() + "echo/", options(), HttpHeaders::ptr_t(), s.handlerPtr()), "delete");
        s.waitAll();
        HttpHeaders::ptr_t dest(new HttpHeaders());
        dest->append("Destination", base_url() + "echo/copy");
        s.name(req->requestCopy(HttpRequest::DEFAULT_POLICY_ID, base_url() + "echo/", options(), dest, s.handlerPtr()), "copy");
        s.waitAll();
        s.name(req->requestMove(HttpRequest::DEFAULT_POLICY_ID, base_url() + "echo/", options(), dest, s.handlerPtr()), "move");
        s.waitAll();
        HttpOptions::ptr_t head = options(5, true);
        head->setHeadersOnly(true);
        s.name(req->requestGet(HttpRequest::DEFAULT_POLICY_ID, base_url() + "ok/", head, HttpHeaders::ptr_t(), s.handlerPtr()), "head");
        s.waitAll();
        // A large POST (chunked upload behaviour, if any, shows on the wire).
        std::string big(300000, 'x');
        for (size_t i = 0; i < big.size(); ++i) big[i] = static_cast<char>('a' + (i * 31) % 26);
        BufferArray* bigBody = make_body(big);
        s.name(req->requestPost(HttpRequest::DEFAULT_POLICY_ID, base_url() + "echo/", bigBody, options(), HttpHeaders::ptr_t(), s.handlerPtr()), "post_large");
        s.waitAll();
        s.name(req->requestPut(HttpRequest::DEFAULT_POLICY_ID, base_url() + "echo/", bigBody, options(), HttpHeaders::ptr_t(), s.handlerPtr()), "put_large");
        s.waitAll();
        bigBody->release();
        s.finish();
    }

    // Byte ranges, small.
    template<> template<>
    void parity_object::test<3>()
    {
        set_test_name("range_small");
        if (!wanted("range_small")) return;
        Scenario s("range_small", Scenario::SEQUENTIAL);
        s.create();
        s.start();
        HttpRequest* req = s.request();
        s.name(req->requestGetByteRange(HttpRequest::DEFAULT_POLICY_ID, base_url() + "large/1000", 100, 200, options(), HttpHeaders::ptr_t(), s.handlerPtr()), "middle");
        s.waitAll();
        s.name(req->requestGetByteRange(HttpRequest::DEFAULT_POLICY_ID, base_url() + "large/1000", 900, 500, options(), HttpHeaders::ptr_t(), s.handlerPtr()), "past_end");
        s.waitAll();
        s.name(req->requestGetByteRange(HttpRequest::DEFAULT_POLICY_ID, base_url() + "large/1000", 2000, 10, options(2), HttpHeaders::ptr_t(), s.handlerPtr()), "unsatisfiable");
        s.waitAll();
        s.name(req->requestGetByteRange(HttpRequest::DEFAULT_POLICY_ID, base_url() + "large/1000", 500, 0, options(), HttpHeaders::ptr_t(), s.handlerPtr()), "open_ended");
        s.waitAll();
        s.name(req->requestGetByteRange(HttpRequest::DEFAULT_POLICY_ID, base_url() + "large/1000", 0, 0, options(), HttpHeaders::ptr_t(), s.handlerPtr()), "whole");
        s.waitAll();
        s.finish();
    }

    // Byte ranges, large.
    template<> template<>
    void parity_object::test<4>()
    {
        set_test_name("range_large");
        if (!wanted("range_large")) return;
        Scenario s("range_large", Scenario::SEQUENTIAL);
        s.create();
        s.start();
        HttpRequest* req = s.request();
        s.name(req->requestGetByteRange(HttpRequest::DEFAULT_POLICY_ID, base_url() + "large/1048576", 0, 1048576, options(), HttpHeaders::ptr_t(), s.handlerPtr()), "full_mib");
        s.waitAll();
        s.name(req->requestGetByteRange(HttpRequest::DEFAULT_POLICY_ID, base_url() + "large/1048576", 262144, 524288, options(), HttpHeaders::ptr_t(), s.handlerPtr()), "middle_half");
        s.waitAll();
        s.name(req->requestGet(HttpRequest::DEFAULT_POLICY_ID, base_url() + "large/1048576", options(), HttpHeaders::ptr_t(), s.handlerPtr()), "plain_get");
        s.waitAll();
        s.finish();
    }

    // 4xx: delivered as errors, never retried.
    template<> template<>
    void parity_object::test<5>()
    {
        set_test_name("errors_4xx");
        if (!wanted("errors_4xx")) return;
        Scenario s("errors_4xx", Scenario::SEQUENTIAL);
        s.create();
        s.start();
        HttpRequest* req = s.request();
        s.name(req->requestGet(HttpRequest::DEFAULT_POLICY_ID, base_url() + "status/404", options(), HttpHeaders::ptr_t(), s.handlerPtr()), "404");
        s.waitAll();
        s.name(req->requestGet(HttpRequest::DEFAULT_POLICY_ID, base_url() + "status/403", options(), HttpHeaders::ptr_t(), s.handlerPtr()), "403");
        s.waitAll();
        s.name(req->requestGet(HttpRequest::DEFAULT_POLICY_ID, base_url() + "status/400", options(), HttpHeaders::ptr_t(), s.handlerPtr()), "400");
        s.waitAll();
        s.name(req->requestGet(HttpRequest::DEFAULT_POLICY_ID, base_url() + "status/204", options(), HttpHeaders::ptr_t(), s.handlerPtr()), "204");
        s.waitAll();
        s.name(req->requestGet(HttpRequest::DEFAULT_POLICY_ID, base_url() + "missing/", options(), HttpHeaders::ptr_t(), s.handlerPtr()), "missing");
        s.waitAll();
        s.finish();
    }

    // 5xx with retries and backoff.
    template<> template<>
    void parity_object::test<6>()
    {
        set_test_name("retry_5xx");
        if (!wanted("retry_5xx")) return;
        Scenario s("retry_5xx", Scenario::SEQUENTIAL);
        s.create();
        s.start();
        HttpRequest* req = s.request();
        s.name(req->requestGet(HttpRequest::DEFAULT_POLICY_ID, base_url() + "status/500", options(2), HttpHeaders::ptr_t(), s.handlerPtr()), "500_twice");
        s.waitAll();
        s.name(req->requestGet(HttpRequest::DEFAULT_POLICY_ID, base_url() + "status/502", options(1), HttpHeaders::ptr_t(), s.handlerPtr()), "502_once");
        s.waitAll();
        s.name(req->requestGet(HttpRequest::DEFAULT_POLICY_ID, base_url() + "status/499", options(0), HttpHeaders::ptr_t(), s.handlerPtr()), "499_noretry");
        s.waitAll();
        s.finish();
    }

    // 503 recovering, with and without Retry-After.
    template<> template<>
    void parity_object::test<7>()
    {
        set_test_name("retry_503");
        if (!wanted("retry_503")) return;
        Scenario s("retry_503", Scenario::SEQUENTIAL);
        s.create();
        s.start();
        HttpRequest* req = s.request();
        s.name(req->requestGet(HttpRequest::DEFAULT_POLICY_ID, base_url() + "flaky/r1/2", options(), HttpHeaders::ptr_t(), s.handlerPtr()), "recover_after_two");
        s.waitAll();
        HttpOptions::ptr_t ra = options();
        ra->setUseRetryAfter(true);
        s.name(req->requestGet(HttpRequest::DEFAULT_POLICY_ID, base_url() + "flaky/r2/1/retry/2", ra, HttpHeaders::ptr_t(), s.handlerPtr()), "retry_after_2");
        s.waitAll();
        HttpOptions::ptr_t noRa = options();
        noRa->setUseRetryAfter(false);
        s.name(req->requestGet(HttpRequest::DEFAULT_POLICY_ID, base_url() + "flaky/r3/1/retry/2", noRa, HttpHeaders::ptr_t(), s.handlerPtr()), "retry_after_ignored");
        s.waitAll();
        HttpOptions::ptr_t fast = options();
        fast->setMinBackoff(200000);
        fast->setMaxBackoff(500000);
        s.name(req->requestGet(HttpRequest::DEFAULT_POLICY_ID, base_url() + "flaky/r4/3", fast, HttpHeaders::ptr_t(), s.handlerPtr()), "custom_backoff");
        s.waitAll();
        s.finish();
    }

    // Timeouts.
    template<> template<>
    void parity_object::test<8>()
    {
        set_test_name("timeout");
        if (!wanted("timeout")) return;
        Scenario s("timeout", Scenario::SEQUENTIAL);
        s.create();
        s.start();
        HttpRequest* req = s.request();
        HttpOptions::ptr_t o = options(1);
        o->setTimeout(2);
        s.name(req->requestGet(HttpRequest::DEFAULT_POLICY_ID, base_url() + "timeout/", o, HttpHeaders::ptr_t(), s.handlerPtr()), "timeout_retry_once");
        s.waitAll();
        HttpOptions::ptr_t x = options(0);
        x->setTimeout(10);
        x->setTransferTimeout(1);
        s.name(req->requestGet(HttpRequest::DEFAULT_POLICY_ID, base_url() + "timeout/", x, HttpHeaders::ptr_t(), s.handlerPtr()), "transfer_timeout");
        s.waitAll();
        s.finish();
    }

    // Connection closed or reset in the body.
    template<> template<>
    void parity_object::test<9>()
    {
        set_test_name("partial_body");
        if (!wanted("partial_body")) return;
        Scenario s("partial_body", Scenario::SEQUENTIAL);
        s.create();
        s.start();
        HttpRequest* req = s.request();
        s.name(req->requestGet(HttpRequest::DEFAULT_POLICY_ID, base_url() + "partial/1000", options(1), HttpHeaders::ptr_t(), s.handlerPtr()), "partial_retry_once");
        s.waitAll();
        s.name(req->requestGet(HttpRequest::DEFAULT_POLICY_ID, base_url() + "reset/1000", options(0), HttpHeaders::ptr_t(), s.handlerPtr()), "reset_noretry");
        s.waitAll();
        s.finish();
    }

    // Redirects.
    template<> template<>
    void parity_object::test<10>()
    {
        set_test_name("redirects");
        if (!wanted("redirects")) return;
        Scenario s("redirects", Scenario::SEQUENTIAL);
        s.create();
        s.start();
        HttpRequest* req = s.request();
        s.name(req->requestGet(HttpRequest::DEFAULT_POLICY_ID, base_url() + "redirect/2", options(), HttpHeaders::ptr_t(), s.handlerPtr()), "follow_two");
        s.waitAll();
        HttpOptions::ptr_t noFollow = options();
        noFollow->setFollowRedirects(false);
        s.name(req->requestGet(HttpRequest::DEFAULT_POLICY_ID, base_url() + "redirect/1", noFollow, HttpHeaders::ptr_t(), s.handlerPtr()), "no_follow");
        s.waitAll();
        BufferArray* body = make_body("posted");
        s.name(req->requestPost(HttpRequest::DEFAULT_POLICY_ID, base_url() + "redirect/1", body, options(), HttpHeaders::ptr_t(), s.handlerPtr()), "post_redirect");
        body->release();
        s.waitAll();
        s.finish();
    }

    // Concurrency per policy class and throttling.
    template<> template<>
    void parity_object::test<11>()
    {
        set_test_name("concurrency");
        if (!wanted("concurrency")) return;
        Scenario s("concurrency", Scenario::CONCURRENT);
        s.create();
        HttpRequest::policy_t a = s.createClass();
        HttpRequest::policy_t b = s.createClass();
        HttpRequest::policy_t c = s.createClass();
        HttpRequest::setStaticPolicyOption(HttpRequest::PO_CONNECTION_LIMIT, a, 2, NULL);
        HttpRequest::setStaticPolicyOption(HttpRequest::PO_CONNECTION_LIMIT, b, 4, NULL);
        HttpRequest::setStaticPolicyOption(HttpRequest::PO_THROTTLE_RATE, c, 5, NULL);
        s.start();
        HttpRequest* req = s.request();
        for (int i = 0; i < 8; ++i)
        {
            s.name(req->requestGet(a, base_url() + "slow/300/A/" + std::to_string(i), options(), HttpHeaders::ptr_t(), s.handlerPtr()), "A" + std::to_string(i));
            s.name(req->requestGet(b, base_url() + "slow/300/B/" + std::to_string(i), options(), HttpHeaders::ptr_t(), s.handlerPtr()), "B" + std::to_string(i));
            s.name(req->requestGet(c, base_url() + "slow/300/C/" + std::to_string(i), options(), HttpHeaders::ptr_t(), s.handlerPtr()), "C" + std::to_string(i));
        }
        s.waitAll();
        s.finish();
    }

    // Cancellation before and after the request went out. The queued case
    // sits behind a class with one connection held by a slower request, so
    // it cannot have been issued when the cancel arrives.
    template<> template<>
    void parity_object::test<12>()
    {
        set_test_name("cancel");
        if (!wanted("cancel")) return;
        Scenario s("cancel", Scenario::SEQUENTIAL);
        s.create();
        HttpRequest::policy_t one = s.createClass();
        HttpRequest::setStaticPolicyOption(HttpRequest::PO_CONNECTION_LIMIT, one, 1, NULL);
        s.start();
        HttpRequest* req = s.request();
        s.name(req->requestGet(one, base_url() + "slow/2500/B/0", options(), HttpHeaders::ptr_t(), s.handlerPtr()), "blocker");
        HttpHandle queued = s.name(req->requestGet(one, base_url() + "slow/1500/Q/0", options(), HttpHeaders::ptr_t(), s.handlerPtr()), "queued");
        s.name(req->requestCancel(queued, s.handlerPtr()), "cancel_queued");
        s.waitAll();
        HttpHandle active = s.name(req->requestGet(HttpRequest::DEFAULT_POLICY_ID, base_url() + "slow/1500/R/0", options(), HttpHeaders::ptr_t(), s.handlerPtr()), "active");
        sleep_ms(400);
        req->update(0);
        s.name(req->requestCancel(active, s.handlerPtr()), "cancel_active");
        s.waitAll();
        s.name(req->requestCancel(reinterpret_cast<HttpHandle>(0x7777), s.handlerPtr()), "cancel_unknown");
        s.waitAll();
        s.finish();
    }

    // Request headers and cookies.
    template<> template<>
    void parity_object::test<13>()
    {
        set_test_name("headers_cookies");
        if (!wanted("headers_cookies")) return;
        Scenario s("headers_cookies", Scenario::SEQUENTIAL);
        s.create();
        s.start();
        HttpRequest* req = s.request();
        HttpHeaders::ptr_t h(new HttpHeaders());
        h->append("Accept", "image/x-j2c");
        h->append("X-Custom-Header", "custom value");
        h->append("Cache-Control", "no-cache");
        s.name(req->requestGet(HttpRequest::DEFAULT_POLICY_ID, base_url() + "headers/", options(5, true), h, s.handlerPtr()), "custom_headers");
        s.waitAll();
        HttpHeaders::ptr_t over(new HttpHeaders());
        over->append("Connection", "close");
        over->append("User-Agent", "parity-agent");
        s.name(req->requestGet(HttpRequest::DEFAULT_POLICY_ID, base_url() + "headers/", options(5, true), over, s.handlerPtr()), "override_headers");
        s.waitAll();
        s.name(req->requestGet(HttpRequest::DEFAULT_POLICY_ID, base_url() + "cookie/set", options(), HttpHeaders::ptr_t(), s.handlerPtr()), "cookie_set");
        s.waitAll();
        s.name(req->requestGet(HttpRequest::DEFAULT_POLICY_ID, base_url() + "cookie/get", options(), HttpHeaders::ptr_t(), s.handlerPtr()), "cookie_get");
        s.waitAll();
        HttpOptions::ptr_t lm = options();
        lm->setLastModified(1700000000);
        s.name(req->requestGet(HttpRequest::DEFAULT_POLICY_ID, base_url() + "headers/", lm, HttpHeaders::ptr_t(), s.handlerPtr()), "if_modified_since");
        s.waitAll();
        s.finish();
    }

    // Body encodings and odd responses.
    template<> template<>
    void parity_object::test<14>()
    {
        set_test_name("bodies");
        if (!wanted("bodies")) return;
        Scenario s("bodies", Scenario::SEQUENTIAL);
        s.create();
        s.start();
        HttpRequest* req = s.request();
        s.name(req->requestGet(HttpRequest::DEFAULT_POLICY_ID, base_url() + "chunked/", options(), HttpHeaders::ptr_t(), s.handlerPtr()), "chunked");
        s.waitAll();
        s.name(req->requestGet(HttpRequest::DEFAULT_POLICY_ID, base_url() + "gzip/", options(), HttpHeaders::ptr_t(), s.handlerPtr()), "gzip");
        s.waitAll();
        // A body ended by the server's close (no Content-Length) is covered
        // by the crate's own test: on Windows loopback the Python server's
        // close reaches libcurl as a reset in a fair share of runs (curl.exe
        // reproduces it), which libcurl's retry turns into extra requests.
        s.name(req->requestGetByteRange(HttpRequest::DEFAULT_POLICY_ID, base_url() + "badrange/", 0, 3, options(1), HttpHeaders::ptr_t(), s.handlerPtr()), "bad_content_range");
        s.waitAll();
        s.name(req->requestGet(HttpRequest::DEFAULT_POLICY_ID, base_url() + "ok/", options(), HttpHeaders::ptr_t(), s.handlerPtr()), "after");
        s.waitAll();
        s.finish();
    }

    // Policy options: static, dynamic and refused.
    template<> template<>
    void parity_object::test<15>()
    {
        set_test_name("policy_options");
        if (!wanted("policy_options")) return;
        Scenario s("policy_options", Scenario::SEQUENTIAL);
        s.create();
        HttpRequest::policy_t a = s.createClass();
        long ret = 0;
        s.extra(status_llsd("static_conn_limit", HttpRequest::setStaticPolicyOption(HttpRequest::PO_CONNECTION_LIMIT, a, 3, &ret)));
        s.extra(LLSD(static_cast<LLSD::Integer>(ret)));
        s.extra(status_llsd("static_conn_limit_clamped", HttpRequest::setStaticPolicyOption(HttpRequest::PO_CONNECTION_LIMIT, a, 1000, &ret)));
        s.extra(LLSD(static_cast<LLSD::Integer>(ret)));
        s.extra(status_llsd("static_per_host_above_limit", HttpRequest::setStaticPolicyOption(HttpRequest::PO_PER_HOST_CONNECTION_LIMIT, a, 500, &ret)));
        s.extra(LLSD(static_cast<LLSD::Integer>(ret)));
        s.extra(status_llsd("static_global_ca_on_class", HttpRequest::setStaticPolicyOption(HttpRequest::PO_CA_FILE, a, std::string("x"), NULL)));
        s.extra(status_llsd("static_class_option_on_global", HttpRequest::setStaticPolicyOption(HttpRequest::PO_THROTTLE_RATE, HttpRequest::GLOBAL_POLICY_ID, 5, NULL)));
        s.extra(status_llsd("static_unknown_class", HttpRequest::setStaticPolicyOption(HttpRequest::PO_CONNECTION_LIMIT, 17, 5, NULL)));
        s.extra(status_llsd("static_trace", HttpRequest::setStaticPolicyOption(HttpRequest::PO_TRACE, HttpRequest::GLOBAL_POLICY_ID, 99, &ret)));
        s.extra(LLSD(static_cast<LLSD::Integer>(ret)));
        s.extra(status_llsd("static_trace_reset", HttpRequest::setStaticPolicyOption(HttpRequest::PO_TRACE, HttpRequest::GLOBAL_POLICY_ID, 0, &ret)));
        s.extra(status_llsd("static_string_as_long", HttpRequest::setStaticPolicyOption(HttpRequest::PO_CA_FILE, HttpRequest::GLOBAL_POLICY_ID, 5, NULL)));
        s.start();
        HttpRequest* req = s.request();
        s.extra(status_llsd("running_static_refused", HttpRequest::setStaticPolicyOption(HttpRequest::PO_CONNECTION_LIMIT, a, 3, NULL)));
        s.extra(LLSD(static_cast<LLSD::Integer>(HttpRequest::createPolicyClass())));
        HttpHandle h = req->setPolicyOption(HttpRequest::PO_CA_FILE, HttpRequest::GLOBAL_POLICY_ID, std::string("x"), s.handlerPtr());
        s.extra(status_llsd("dynamic_ca_file_refused", h == LLCORE_HTTP_HANDLE_INVALID ? req->getStatus() : HttpStatus(999)));
        h = req->setPolicyOption(HttpRequest::PO_CONNECTION_LIMIT, a, std::string("x"), s.handlerPtr());
        s.extra(status_llsd("dynamic_wrong_type", h == LLCORE_HTTP_HANDLE_INVALID ? req->getStatus() : HttpStatus(999)));
        s.name(req->setPolicyOption(HttpRequest::PO_CONNECTION_LIMIT, a, 6, s.handlerPtr()), "dynamic_conn_limit");
        s.waitAll();
        s.name(req->setPolicyOption(HttpRequest::PO_PER_HOST_CONNECTION_LIMIT, a, 2, s.handlerPtr()), "dynamic_per_host");
        s.waitAll();
        s.name(req->setPolicyOption(HttpRequest::PO_PIPELINING_DEPTH, a, 3, s.handlerPtr()), "dynamic_pipelining");
        s.waitAll();
        s.name(req->setPolicyOption(HttpRequest::PO_THROTTLE_RATE, a, 10, s.handlerPtr()), "dynamic_throttle");
        s.waitAll();
        s.name(req->setPolicyOption(HttpRequest::PO_CONNECTION_LIMIT, 17, 6, s.handlerPtr()), "dynamic_unknown_class");
        s.waitAll();
        s.name(req->setPolicyOption(HttpRequest::PO_HTTP_PROXY, HttpRequest::GLOBAL_POLICY_ID, std::string(""), s.handlerPtr()), "dynamic_proxy_empty");
        s.waitAll();
        s.name(req->requestGet(a, base_url() + "ok/", options(), HttpHeaders::ptr_t(), s.handlerPtr()), "get_on_class");
        s.waitAll();
        s.finish();
    }

    // Pipelining depth on a class: several requests on few connections.
    template<> template<>
    void parity_object::test<16>()
    {
        set_test_name("pipelining");
        if (!wanted("pipelining")) return;
        Scenario s("pipelining", Scenario::CONCURRENT);
        s.create();
        HttpRequest::policy_t p = s.createClass();
        HttpRequest::setStaticPolicyOption(HttpRequest::PO_CONNECTION_LIMIT, p, 4, NULL);
        HttpRequest::setStaticPolicyOption(HttpRequest::PO_PER_HOST_CONNECTION_LIMIT, p, 1, NULL);
        HttpRequest::setStaticPolicyOption(HttpRequest::PO_PIPELINING_DEPTH, p, 5, NULL);
        s.start();
        HttpRequest* req = s.request();
        for (int i = 0; i < 6; ++i)
        {
            s.name(req->requestGetByteRange(p, base_url() + "slow/100/P/" + std::to_string(i), 0, 0, options(), HttpHeaders::ptr_t(), s.handlerPtr()), "P" + std::to_string(i));
        }
        s.waitAll();
        s.finish();
    }

    // Timing: requests per second and latency per request, and memory per
    // in-flight request. Runs only with VKS_HTTP_BENCH=<iterations>.
    template<> template<>
    void parity_object::test<17>()
    {
        set_test_name("bench");
        const std::string n = env_or("VKS_HTTP_BENCH", "");
        if (n.empty())
        {
            return; // timing only when asked for
        }
        const int iterations = std::max(1, atoi(n.c_str()));
#if LL_WINDOWS
        // The viewer runs with the 1 ms scheduler period (LLAppViewerWin32);
        // the service loop's 2 ms sleeps would otherwise round to 15.6 ms.
        timeBeginPeriod(1);
#endif
        HttpRequest::createService();
        HttpRequest::startThread();
        HttpRequest* req = new HttpRequest();
        const steady::time_point t0 = steady::now();
        Collector handler(t0);
        HttpHandler::ptr_t handlerPtr(&handler, NoOpDeleter());
        std::ostringstream table;
        std::ostringstream csv;

        struct Row { std::string name; double rps, meanMs, medianMs, p95Ms; };
        std::vector<Row> rows;

        // Warm-up: a connection and a cached handle.
        for (int i = 0; i < 3; ++i)
        {
            req->requestGet(HttpRequest::DEFAULT_POLICY_ID, base_url() + "ok/", options(), HttpHeaders::ptr_t(), handlerPtr);
            while (handler.count() < static_cast<size_t>(i + 1)) { req->update(0); std::this_thread::yield(); }
        }
        handler.mOutcomes.clear();

        // Sequential: one request in flight at a time, measuring submit to
        // handler latency through update() polled continuously.
        auto run = [&](const std::string& name, std::function<HttpHandle()> issue, int count)
        {
            std::vector<double> lat;
            lat.reserve(count);
            const steady::time_point start = steady::now();
            for (int i = 0; i < count; ++i)
            {
                const steady::time_point s = steady::now();
                HttpHandle h = issue();
                ensure("bench request issued", h != LLCORE_HTTP_HANDLE_INVALID);
                const size_t want = handler.count() + 1;
                while (handler.count() < want) { req->update(0); std::this_thread::yield(); }
                ensure("bench request succeeded", static_cast<bool>(handler.mOutcomes.back().status));
                lat.push_back(ms_since(s));
                handler.mOutcomes.clear();
            }
            const double total = ms_since(start);
            std::sort(lat.begin(), lat.end());
            double sum = 0;
            for (size_t i = 0; i < lat.size(); ++i) sum += lat[i];
            Row r;
            r.name = name;
            r.rps = count / (total / 1000.0);
            r.meanMs = sum / lat.size();
            r.medianMs = lat[lat.size() / 2];
            r.p95Ms = lat[std::min(lat.size() - 1, static_cast<size_t>(lat.size() * 0.95))];
            rows.push_back(r);
        };

        BufferArray* llsdBody = make_body(LLSD_BODY);
        HttpHeaders::ptr_t llsdHeaders(new HttpHeaders());
        llsdHeaders->append("Content-Type", "application/llsd+xml");
        llsdHeaders->append("Accept", "application/llsd+xml");
        run("GET small LLSD (125 B)", [&]() { return req->requestGet(HttpRequest::DEFAULT_POLICY_ID, base_url() + "ok/", options(), llsdHeaders, handlerPtr); }, iterations);
        run("POST small LLSD (191 B, echoed)", [&]() { return req->requestPost(HttpRequest::DEFAULT_POLICY_ID, base_url() + "echo/", llsdBody, options(), llsdHeaders, handlerPtr); }, iterations);
        run("GET 1 MiB range", [&]() { return req->requestGetByteRange(HttpRequest::DEFAULT_POLICY_ID, base_url() + "large/1048576", 0, 1048576, options(), HttpHeaders::ptr_t(), handlerPtr); }, std::max(1, iterations / 10));
        llsdBody->release();

        // Memory per in-flight request: requests queued behind a class limit.
        HttpRequest::policy_t slowClass = HttpRequest::DEFAULT_POLICY_ID;
        const int inflight = 20000;
        const U64 rssBefore = LLMemory::getCurrentRSS();
        std::vector<HttpHandle> handles;
        BufferArray* kb = make_body(std::string(1024, 'k'));
        for (int i = 0; i < inflight; ++i)
        {
            handles.push_back(req->requestPost(slowClass, base_url() + "slow/4000/M/" + std::to_string(i), kb, options(), llsdHeaders, handlerPtr));
        }
        sleep_ms(500);
        const U64 rssAfter = LLMemory::getCurrentRSS();
        for (int i = 0; i < inflight; ++i) req->requestCancel(handles[i], HttpHandler::ptr_t());
        while (handler.count() < static_cast<size_t>(inflight)) { req->update(0); sleep_ms(1); }
        handler.mOutcomes.clear();
        kb->release();
        const double perRequest = static_cast<double>(rssAfter > rssBefore ? rssAfter - rssBefore : 0) / inflight;

        table << "| operation | requests/s | mean ms | median ms | p95 ms |\n|---|---:|---:|---:|---:|\n";
        for (size_t i = 0; i < rows.size(); ++i)
        {
            char line[256];
            snprintf(line, sizeof(line), "| %s | %.0f | %.3f | %.3f | %.3f |\n", rows[i].name.c_str(), rows[i].rps, rows[i].meanMs, rows[i].medianMs, rows[i].p95Ms);
            table << line;
            csv << rows[i].name << "," << rows[i].rps << "," << rows[i].meanMs << "," << rows[i].medianMs << "," << rows[i].p95Ms << "\n";
        }
        table << "memory per in-flight request (" << inflight << " queued POSTs of 1 KiB): " << static_cast<long>(perRequest) << " bytes (RSS "
              << rssBefore / 1024 << " KiB -> " << rssAfter / 1024 << " KiB)\n";
        csv << "memory per in-flight request," << static_cast<long>(perRequest) << "\n";
        std::cout << table.str() << std::flush;
        const std::string out = env_or("VKS_HTTP_BENCH_OUT", "");
        if (!out.empty())
        {
            std::ofstream f(out.c_str(), std::ios::app);
            f << csv.str();
        }

        Collector stopper(t0);
        HttpHandler::ptr_t stopPtr(&stopper, NoOpDeleter());
        req->requestStopThread(stopPtr);
        const steady::time_point ts = steady::now();
        while (!HttpService::isStopped() && ms_since(ts) < 10000) { req->update(0); sleep_ms(1); }
        delete req;
        HttpRequest::destroyService();
    }
}
