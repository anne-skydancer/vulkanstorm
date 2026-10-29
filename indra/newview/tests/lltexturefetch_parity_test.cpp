/**
 * @file lltexturefetch_parity_test.cpp
 * @brief The texture fetcher's decisions against a recording.
 *
 * Drives LLTextureFetch (with its own thread, the real LLTextureCache on a
 * scratch directory, the real LLImageDecodeThread and the HTTP layer)
 * against the local mock server (tests/vksassets_mock_server.py, which runs
 * this executable and records every request it receives) through a
 * scripted set of textures and fetch priorities: normal fetches at every
 * discard level, a second fetch of each from the cache asking for more,
 * missing (404) and busy (503) textures, truncated and overlapping range
 * replies, a server that ignores ranges and one that refuses them. It then
 * compares, per texture, the sequence of HTTP requests (target and Range),
 * the outcome (fetch state, decoded discard, raw image dimensions, the last
 * HTTP status) and the cache reads and writes with the record made from
 * the C++ implementation before its port to Rust (vkstorm-rust):
 * tests/fixtures/lltexturefetch_parity.xml.
 *
 * The textures are JPEG 2000 images the test encodes itself from synthetic
 * rasters (the encoder is deterministic); their bytes are part of the
 * record (tests/fixtures/textures) and served from there.
 *
 * VKS_TEXFETCH_RECORD=<file>   writes the record instead of comparing
 * VKS_TEXFETCH_TEXTURES=<dir>  the texture directory (default: the fixture's)
 * VKS_TEXFETCH_BENCH=<n>       times a 1 MiB texture's assembly from range replies
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#include "../llviewerprecompiledheaders.h"

#include "../lltexturefetch.h"
#include "../lltexturecache.h"
#include "../llviewercontrol.h"
#include "../llviewertexture.h"
#include "../llviewerstatsrecorder.h"
#include "../fsassetblacklist.h"
#include "../llviewernetwork.h"
#include "lltexturefetch_parity_stubs.h"

#include "lltut.h"
#include "llimage.h"
#include "llimagej2c.h"
#include "llimageworker.h"
#include "lldir.h"
#include "llfile.h"
#include "llsd.h"
#include "llsdserialize.h"
#include "httprequest.h"
#include "httpcommon.h"
#include "workqueue.h"

#include <boost/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if LL_WINDOWS
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")
#endif

#ifndef VKS_TEXFETCH_FIXTURE_DIR
#error VKS_TEXFETCH_FIXTURE_DIR must name the directory of the recording
#endif
#define VKS_TEXFETCH_FIXTURE VKS_TEXFETCH_FIXTURE_DIR "/lltexturefetch_parity.xml"
#define VKS_TEXFETCH_TEXTURES VKS_TEXFETCH_FIXTURE_DIR "/textures"

// The main-loop work queue the queued threads look up.
static LL::WorkQueue gMainloopWork("mainloop", 1024 * 1024);

namespace
{
    using tut::ensure;
    using tut::ensure_equals;

    std::string env_or(const char* name, const std::string& dflt)
    {
        const char* v = getenv(name);
        return v ? std::string(v) : dflt;
    }

    std::string scratch_dir()
    {
        const char* tmp = LLFile::tmpdir();
        std::string base = (tmp && *tmp) ? tmp : ".";
        return base + gDirUtilp->getDirDelimiter() + "vks_texfetch_parity";
    }

    std::string base_url()
    {
        const char* env = getenv("LL_TEST_PORT");
        ensure("LL_TEST_PORT set in environment (run under vksassets_mock_server.py)", env != NULL);
        return std::string("http://127.0.0.1:") + env;
    }

    struct Rng
    {
        U64 s;
        explicit Rng(U64 seed) : s(seed) {}
        U64 next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
        U32 below(U32 n) { return (U32)(next() % n); }
    };

    // ---- the scripted textures ---------------------------------------------

    // The first byte of the id selects the mock server's behaviour (see the
    // server's docstring); the rest is deterministic.
    struct Scripted
    {
        std::string name;
        U8 behaviour;
        S32 w, h, c;
        S32 discard;       // the first request's desired discard
        S32 discard2;      // the second pass's desired discard (-1: no second pass)
        F32 priority;
        bool lossless;
        LLUUID id;
    };

    std::vector<Scripted> script()
    {
        std::vector<Scripted> v;
        v.push_back({ "plain_64_d0", 0x10, 64, 64, 3, 0, -1, 1000.f, false, LLUUID() });
        v.push_back({ "plain_128_d2", 0x11, 128, 128, 3, 2, 0, 2000.f, false, LLUUID() });
        v.push_back({ "plain_256_d3_rgba", 0x12, 256, 256, 4, 3, 1, 3000.f, false, LLUUID() });
        v.push_back({ "plain_512_d4", 0x13, 512, 512, 3, 4, 2, 4000.f, false, LLUUID() });
        v.push_back({ "plain_512_d5_then_0", 0x14, 512, 512, 3, 5, 0, 500.f, false, LLUUID() });
        v.push_back({ "plain_1024_d2_then_1", 0x15, 1024, 1024, 3, 2, 1, 8000.f, false, LLUUID() });
        v.push_back({ "lossless_256_d1", 0x16, 256, 256, 3, 1, 0, 1500.f, true, LLUUID() });
        v.push_back({ "tiny_16_d5", 0x17, 16, 16, 3, 5, 0, 100.f, false, LLUUID() });
        v.push_back({ "missing_404", 0x4f, 128, 128, 3, 2, 2, 1000.f, false, LLUUID() });
        v.push_back({ "busy_503", 0x50, 128, 128, 3, 2, -1, 1000.f, false, LLUUID() });
        v.push_back({ "busy_once_503", 0x51, 128, 128, 3, 2, 0, 1000.f, false, LLUUID() });
        v.push_back({ "truncated_3000", 0x60, 512, 512, 3, 1, 0, 1000.f, false, LLUUID() });
        v.push_back({ "overlap_97", 0x61, 512, 512, 3, 3, 0, 1000.f, false, LLUUID() });
        v.push_back({ "full_200", 0x70, 256, 256, 3, 3, 0, 1000.f, false, LLUUID() });
        v.push_back({ "range_refused_416", 0x41, 256, 256, 3, 4, 1, 1000.f, false, LLUUID() });
        Rng rng(0xD1B54A32D192ED03ULL);
        for (Scripted& s : v)
        {
            for (int i = 0; i < 16; ++i)
            {
                s.id.mData[i] = (U8)rng.next();
            }
            s.id.mData[0] = s.behaviour;
        }
        return v;
    }

    // A deterministic raster: gradients with a hashed grain.
    LLPointer<LLImageRaw> raster(const Scripted& s)
    {
        LLPointer<LLImageRaw> raw = new LLImageRaw((U16)s.w, (U16)s.h, (S8)s.c);
        U8* p = raw->getData();
        U64 h = 0x9E3779B97F4A7C15ULL ^ (U64)s.w ^ ((U64)s.h << 20) ^ ((U64)s.c << 40);
        for (S32 y = 0; y < s.h; ++y)
        {
            for (S32 x = 0; x < s.w; ++x)
            {
                h ^= h << 13; h ^= h >> 7; h ^= h << 17;
                U8 grain = (U8)(h & 0x1f);
                U8* px = p + (y * s.w + x) * s.c;
                px[0] = (U8)((x * 255) / s.w);
                px[1] = (U8)((y * 255) / s.h);
                px[2] = (U8)(((x + y) * 127) / (s.w + s.h) + grain);
                if (s.c == 4)
                {
                    px[3] = (U8)(255 - (x * 255) / s.w);
                }
            }
        }
        return raw;
    }

    std::vector<U8> encode_j2c(const Scripted& s)
    {
        LLPointer<LLImageRaw> raw = raster(s);
        LLPointer<LLImageJ2C> j2c = new LLImageJ2C();
        j2c->setReversible(s.lossless);
        ensure("encode " + s.name, j2c->encode(raw, "parity", 0.f));
        return std::vector<U8>(j2c->getData(), j2c->getData() + j2c->getDataSize());
    }

    void write_file(const std::string& path, const std::vector<U8>& bytes)
    {
        std::ofstream o(path.c_str(), std::ios::binary);
        o.write((const char*)bytes.data(), bytes.size());
    }

    bool read_file(const std::string& path, std::vector<U8>& bytes)
    {
        std::ifstream i(path.c_str(), std::ios::binary);
        if (!i) return false;
        bytes.assign((std::istreambuf_iterator<char>(i)), std::istreambuf_iterator<char>());
        return true;
    }

    // ---- the mock server's log -------------------------------------------------

    struct ServerRequest
    {
        double t;
        std::string target;
        std::string range;
        int status;
    };

    std::vector<ServerRequest> read_server_log(size_t skip)
    {
        std::vector<ServerRequest> out;
        const char* path = getenv("VKS_HTTP_MOCK_LOG");
        if (!path) return out;
        std::ifstream in(path, std::ios::binary);
        std::string line;
        size_t n = 0;
        while (std::getline(in, line))
        {
            if (line.empty()) continue;
            boost::system::error_code ec;
            boost::json::value v = boost::json::parse(line, ec);
            if (ec) continue;
            const boost::json::object& o = v.as_object();
            if (o.at("ev").as_string() != "req") continue;
            if (n++ < skip) continue;
            ServerRequest r;
            r.t = o.at("t").to_number<double>();
            r.target = std::string(o.at("target").as_string().c_str());
            r.range = std::string(o.at("range").as_string().c_str());
            r.status = (int)o.at("status").to_number<int>();
            out.push_back(r);
        }
        return out;
    }

    size_t server_log_count()
    {
        return read_server_log(0).size();
    }

    void configure_server(const std::string& dir)
    {
        LLCore::HttpRequest req;
        std::string url = base_url() + "/config/?dir=" + LLURI::escape(dir);
        LLCore::HttpHandle h = req.requestGet(LLCore::HttpRequest::DEFAULT_POLICY_ID, url,
                                              LLCore::HttpOptions::ptr_t(), LLCore::HttpHeaders::ptr_t(), LLCore::HttpHandler::ptr_t());
        ensure("config request", h != LLCORE_HTTP_HANDLE_INVALID);
        for (int i = 0; i < 100; ++i)
        {
            req.update(0);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    // ---- driving the fetcher ---------------------------------------------------

    struct Pipeline
    {
        LLTextureCache* cache;
        LLImageDecodeThread* decode;
        LLTextureFetch* fetch;

        Pipeline(const std::string& cache_root)
        {
            cache = new LLTextureCache(true);
            cache->setReadOnly(false);
            cache->initCache(LL_PATH_CACHE, 64 * 1024 * 1024, false);
            decode = new LLImageDecodeThread(true);
            vks_test_install_app(decode, cache, NULL);
            fetch = new LLTextureFetch(cache, true, false);
            vks_test_install_app(decode, cache, fetch);
        }

        void pump()
        {
            fetch->update(1.f);
            cache->update(1.f);
            decode->update(1.f);
            gMainloopWork.runFor(std::chrono::milliseconds(1));
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }

        ~Pipeline()
        {
            fetch->shutdown();
            cache->shutdown();
            decode->shutdown();
            vks_test_install_app(NULL, NULL, NULL);
            delete fetch;
            delete cache;
            delete decode;
        }
    };

    // One pass: requests the textures with the given discards and waits for
    // every request to finish. Returns the outcomes keyed by name.
    LLSD run_pass(Pipeline& p, const std::vector<Scripted>& textures, bool second)
    {
        LLSD out;
        std::vector<size_t> active;
        for (size_t i = 0; i < textures.size(); ++i)
        {
            const Scripted& s = textures[i];
            S32 discard = second ? s.discard2 : s.discard;
            if (discard < 0) continue;
            S32 res = p.fetch->createRequest(FTT_DEFAULT, "", s.id, LLHost(), s.priority, s.w, s.h, s.c, discard, false, true);
            out[s.name]["create"] = res;
            active.push_back(i);
        }
        typedef std::chrono::steady_clock clock;
        clock::time_point t0 = clock::now();
        std::vector<bool> done(textures.size(), false);
        size_t remaining = active.size();
        while (remaining > 0 && std::chrono::duration<double>(clock::now() - t0).count() < 90.0)
        {
            p.pump();
            for (size_t i : active)
            {
                if (done[i]) continue;
                const Scripted& s = textures[i];
                S32 discard_level = -1;
                S32 worker_state = -1;
                LLPointer<LLImageRaw> raw;
                LLPointer<LLImageRaw> aux;
                LLCore::HttpStatus status;
                if (p.fetch->getRequestFinished(s.id, discard_level, worker_state, raw, aux, status))
                {
                    done[i] = true;
                    --remaining;
                    LLSD& r = out[s.name];
                    r["state"] = LLTextureFetch::getStateString(worker_state);
                    r["discard"] = discard_level;
                    r["raw_w"] = raw.notNull() ? raw->getWidth() : 0;
                    r["raw_h"] = raw.notNull() ? raw->getHeight() : 0;
                    r["raw_c"] = raw.notNull() ? (S32)raw->getComponents() : 0;
                    r["http"] = status.toTerseString();
                    r["from_cache"] = p.fetch->isFromLocalCache(s.id);
                }
            }
        }
        ensure("every request finished within the time limit", remaining == 0);
        for (size_t i : active)
        {
            p.fetch->deleteRequest(textures[i].id, true);
        }
        for (int i = 0; i < 50; ++i) p.pump();
        U32 reads = 0, writes = 0, waits = 0;
        p.fetch->getStateStats(&reads, &writes, &waits);
        out["_stats"]["cache_reads"] = (S32)reads;
        out["_stats"]["cache_writes"] = (S32)writes;
        out["_stats"]["http_requests"] = (S32)p.fetch->getTotalNumHTTPRequests();
        return out;
    }

    // The server's requests per texture, in order: "target range status".
    LLSD requests_by_texture(const std::vector<ServerRequest>& log, const std::vector<Scripted>& textures)
    {
        LLSD out;
        for (const Scripted& s : textures)
        {
            out[s.name] = LLSD::emptyArray();
        }
        for (const ServerRequest& r : log)
        {
            for (const Scripted& s : textures)
            {
                if (r.target.find(s.id.asString()) != std::string::npos)
                {
                    std::ostringstream line;
                    line << r.range << " -> " << r.status;
                    out[s.name].append(line.str());
                }
            }
        }
        return out;
    }

    void compare_llsd(const std::string& path, const LLSD& got, const LLSD& want, std::vector<std::string>& problems)
    {
        if (got.type() != want.type())
        {
            problems.push_back(path + ": type differs");
            return;
        }
        if (want.isMap())
        {
            for (LLSD::map_const_iterator it = want.beginMap(); it != want.endMap(); ++it)
            {
                if (!got.has(it->first))
                {
                    problems.push_back(path + "/" + it->first + ": missing");
                    continue;
                }
                compare_llsd(path + "/" + it->first, got[it->first], it->second, problems);
            }
            for (LLSD::map_const_iterator it = got.beginMap(); it != got.endMap(); ++it)
            {
                if (!want.has(it->first))
                {
                    problems.push_back(path + "/" + it->first + ": unexpected");
                }
            }
        }
        else if (want.isArray())
        {
            if (got.size() != want.size())
            {
                std::ostringstream s;
                s << path << ": " << got.size() << " entries, expected " << want.size();
                problems.push_back(s.str());
            }
            for (size_t i = 0; i < std::min(got.size(), want.size()); ++i)
            {
                std::ostringstream s;
                s << path << "[" << i << "]";
                compare_llsd(s.str(), got[i], want[i], problems);
            }
        }
        else if (got.asString() != want.asString())
        {
            problems.push_back(path + ": '" + got.asString() + "' expected '" + want.asString() + "'");
        }
    }
}

namespace
{
    // The viewer environment the fetcher needs, once per process.
    void setup_environment()
    {
        static bool done = false;
        if (done)
        {
            return;
        }
        done = true;
#if LL_WINDOWS
        timeBeginPeriod(1);
#endif
        gSavedSettings.declareU32("CacheValidateCounter", 0, "", LLControlVariable::PERSIST_NO);
        gSavedSettings.declareBOOL("HttpRangeRequestsDisable", false, "", LLControlVariable::PERSIST_NO);
        gSavedSettings.declareBOOL("TextureDecodeDisabled", false, "", LLControlVariable::PERSIST_NO);
        gSavedSettings.declareBOOL("LogTextureDownloadsToViewerLog", false, "", LLControlVariable::PERSIST_NO);
        gSavedSettings.declareBOOL("LogTextureDownloadsToSimulator", false, "", LLControlVariable::PERSIST_NO);
        gSavedSettings.declareBOOL("LogTextureNetworkTraffic", false, "", LLControlVariable::PERSIST_NO);
        gSavedSettings.declareBOOL("QAModeMetrics", false, "", LLControlVariable::PERSIST_NO);
        gSavedSettings.declareF32("TextureFetchFakeFailureRate", 0.f, "", LLControlVariable::PERSIST_NO);
        gSavedSettings.declareF32("TextureFetchMinTimeToLog", 2.f, "", LLControlVariable::PERSIST_NO);
        gSavedSettings.declareU32("TextureLoggingThreshold", 1, "", LLControlVariable::PERSIST_NO);
        LLImage::initClass(true, 50);
        LLViewerStatsRecorder::createInstance();
        // Singletons the fetch thread touches: construct them here, on the main
        // thread, since LLSingleton would otherwise hand their construction
        // to the main loop queue and wait for it.
        FSAssetBlacklist::getInstance();
        LLGridManager::getInstance();
    }
}

namespace tut
{
    struct texfetch_data {};
    typedef test_group<texfetch_data> texfetch_group;
    typedef texfetch_group::object texfetch_object;
    tut::texfetch_group texfetch_instance("lltexturefetch_parity");

    template<> template<>
    void texfetch_object::test<1>()
    {
        set_test_name("texture fetch decisions match the recording");
        setup_environment();

        const std::string record = env_or("VKS_TEXFETCH_RECORD", "");
        const bool recording = !record.empty();

        std::string root = scratch_dir();
        gDirUtilp->deleteDirAndContents(root);
        LLFile::mkdir(root);
        std::string cache_root = root + gDirUtilp->getDirDelimiter() + "cache";
        LLFile::mkdir(cache_root);
        ensure("scratch cache dir", gDirUtilp->setCacheDir(cache_root));

        // The textures: encoded now when recording, else the fixture's.
        std::vector<Scripted> textures = script();
        std::string tex_dir = env_or("VKS_TEXFETCH_TEXTURES", VKS_TEXFETCH_TEXTURES);
        if (recording)
        {
            LLFile::mkdir(tex_dir);
            for (const Scripted& s : textures)
            {
                write_file(tex_dir + gDirUtilp->getDirDelimiter() + s.id.asString() + ".j2c", encode_j2c(s));
            }
        }
        for (const Scripted& s : textures)
        {
            std::vector<U8> bytes;
            ensure("texture present: " + s.name, read_file(tex_dir + gDirUtilp->getDirDelimiter() + s.id.asString() + ".j2c", bytes));
        }

        LLCore::HttpRequest::createService();
        LLCore::HttpRequest::startThread();
        configure_server(tex_dir);
        const std::string region_url = base_url() + "/texture";
        LLUUID region_id("2b2c3d4e-0000-4000-8000-000000000001");
        vks_test_set_region(region_url, "", region_id);

        LLSD got;
        {
            Pipeline p(cache_root);
            size_t skip = server_log_count();
            got["pass1"] = run_pass(p, textures, false);
            got["pass1_requests"] = requests_by_texture(read_server_log(skip), textures);
            // The second pass: the same textures again from the cache, most
            // asking for more data than was fetched.
            skip = server_log_count();
            got["pass2"] = run_pass(p, textures, true);
            got["pass2_requests"] = requests_by_texture(read_server_log(skip), textures);
            got["cache_entries"] = (S32)p.cache->getEntries();
            for (const Scripted& s : textures)
            {
                got["in_cache"][s.name] = p.cache->isInCache(s.id);
            }
        }
        // A fresh pipeline over the same cache directory: everything cached
        // is served without the network; the failures are fetched again.
        {
            Pipeline p(cache_root);
            size_t skip = server_log_count();
            got["pass3"] = run_pass(p, textures, false);
            got["pass3_requests"] = requests_by_texture(read_server_log(skip), textures);
        }
        vks_test_clear_region();
        LLCore::HttpRequest::destroyService();

        if (recording)
        {
            std::ofstream o(record.c_str());
            LLSDSerialize::toPrettyXML(got, o);
            std::cout << "recorded to " << record << std::endl;
            gDirUtilp->deleteDirAndContents(root);
            return;
        }

        LLSD want;
        {
            std::ifstream i(VKS_TEXFETCH_FIXTURE);
            ensure("fixture present: " VKS_TEXFETCH_FIXTURE, (bool)i);
            ensure("fixture parses", LLSDSerialize::fromXML(want, i) > 0);
        }
        std::vector<std::string> problems;
        compare_llsd("", got, want, problems);
        for (const std::string& p : problems) std::cout << "PARITY: " << p << std::endl;
        std::cout << "compared " << textures.size() << " textures over 3 passes, " << problems.size() << " differences" << std::endl;
        ensure("texture fetch decisions identical", problems.empty());
        gDirUtilp->deleteDirAndContents(root);
    }

    // VKS_TEXFETCH_BENCH=<n>: times the assembly of one large texture from
    // range replies, fetched at discard 5 down to 0 through the real cache,
    // decode thread and HTTP layer against the mock server; the minimum of
    // n runs. VKS_TEXFETCH_BENCH_OUT=<csv> appends the timing.
    template<> template<>
    void texfetch_object::test<2>()
    {
        set_test_name("texture assembly timing (VKS_TEXFETCH_BENCH)");
        const char* bench = getenv("VKS_TEXFETCH_BENCH");
        if (!bench || !*bench)
        {
            return;
        }
        int iterations = atoi(bench);
        setup_environment();

        std::string root = scratch_dir() + "_bench";
        gDirUtilp->deleteDirAndContents(root);
        LLFile::mkdir(root);
        std::string tex_dir = root + gDirUtilp->getDirDelimiter() + "textures";
        LLFile::mkdir(tex_dir);

        Scripted big = { "bench_1024_lossless", 0x18, 1024, 1024, 3, 5, -1, 1000.f, true, LLUUID() };
        Rng rng(0xBEEF1234ULL);
        for (int i = 0; i < 16; ++i)
        {
            big.id.mData[i] = (U8)rng.next();
        }
        big.id.mData[0] = big.behaviour;
        std::vector<U8> bytes = encode_j2c(big);
        write_file(tex_dir + gDirUtilp->getDirDelimiter() + big.id.asString() + ".j2c", bytes);

        LLCore::HttpRequest::createService();
        LLCore::HttpRequest::startThread();
        configure_server(tex_dir);
        const std::string region_url = base_url() + "/texture";
        LLUUID region_id("2b2c3d4e-0000-4000-8000-000000000002");
        vks_test_set_region(region_url, "", region_id);

        typedef std::chrono::steady_clock clock;
        double best_ms = 1e18;
        S32 final_discard = -1;
        for (int it = 0; it < iterations; ++it)
        {
            std::string cache_root = root + gDirUtilp->getDirDelimiter() + "cache";
            gDirUtilp->deleteDirAndContents(cache_root);
            LLFile::mkdir(cache_root);
            ensure("scratch cache dir", gDirUtilp->setCacheDir(cache_root));
            Pipeline p(cache_root);
            clock::time_point t0 = clock::now();
            for (S32 discard = 5; discard >= 0; --discard)
            {
                p.fetch->createRequest(FTT_DEFAULT, "", big.id, LLHost(), big.priority, big.w, big.h, big.c, discard, false, true);
                clock::time_point t1 = clock::now();
                bool finished = false;
                while (!finished && std::chrono::duration<double>(clock::now() - t1).count() < 60.0)
                {
                    p.pump();
                    S32 discard_level = -1;
                    S32 worker_state = -1;
                    LLPointer<LLImageRaw> raw;
                    LLPointer<LLImageRaw> aux;
                    LLCore::HttpStatus status;
                    if (p.fetch->getRequestFinished(big.id, discard_level, worker_state, raw, aux, status))
                    {
                        finished = true;
                        final_discard = discard_level;
                    }
                }
                ensure("request finished", finished);
            }
            double ms = std::chrono::duration<double, std::milli>(clock::now() - t0).count();
            best_ms = std::min(best_ms, ms);
            p.fetch->deleteRequest(big.id, true);
            for (int i = 0; i < 50; ++i) p.pump();
        }
        vks_test_clear_region();
        LLCore::HttpRequest::destroyService();

        std::cout << "VKS_TEXFETCH_BENCH bytes=" << bytes.size() << " final_discard=" << final_discard
                  << " six_levels_min_ms=" << best_ms << std::endl;
        const char* out = getenv("VKS_TEXFETCH_BENCH_OUT");
        if (out && *out)
        {
            std::ofstream o(out, std::ios::app);
            o << "fetch,assemble_six_levels," << bytes.size() << "," << best_ms << "\n";
        }
        gDirUtilp->deleteDirAndContents(root);
    }
}
