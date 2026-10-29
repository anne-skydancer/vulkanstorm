/**
 * @file lltexturecache_parity_test.cpp
 * @brief Byte-level parity of the texture cache files against a recording.
 *
 * Drives LLTextureCache (its worker thread, on a scratch cache directory, with a
 * fixed clock) through a scripted sequence of writes, reads, fast-cache
 * reads, re-writes, removals, an over-limit purge and a reopen with body
 * validation, then compares every file of the cache directory
 * (texture.entries, texture.cache, FastCache.cache, the body files) byte
 * for byte with the record made from the C++ implementation before its
 * port to Rust (vkstorm-rust): tests/fixtures/lltexturecache_parity.bin.
 *
 * VKS_TEXCACHE_RECORD=<file>  writes the record instead of comparing
 * VKS_TEXCACHE_BENCH=<n>      times the index lookup and write paths
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#include "../llviewerprecompiledheaders.h"

#include "../lltexturecache.h"
#include "../llviewercontrol.h"
#include "../llappviewer.h"

#include "lltut.h"
#include "llimage.h"
#include "llimagej2c.h"
#include "lldir.h"
#include "llfile.h"
#include "llsd.h"
#include "llsdserialize.h"
#include "vkstorm_ffi.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <cstdio>
#include <iostream>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#ifndef VKS_TEXCACHE_FIXTURE_DIR
#error VKS_TEXCACHE_FIXTURE_DIR must name the directory of the recorded cache
#endif
#define VKS_TEXCACHE_FIXTURE VKS_TEXCACHE_FIXTURE_DIR "/lltexturecache_parity.bin"
#define VKS_TEXCACHE_EVENTS VKS_TEXCACHE_FIXTURE_DIR "/lltexturecache_parity_events.xml"

// ---------------------------------------------------------------------------
// Stubs: the cache pauses the viewer's main-loop watchdog around purges and
// reads one setting; nothing else of the viewer is needed.

LLControlGroup gSavedSettings("Global");
LLAppViewer* LLAppViewer::sInstance = NULL;
void LLAppViewer::pauseMainloopTimeout() {}
void LLAppViewer::resumeMainloopTimeout(std::string_view) {}

namespace
{
    using tut::ensure;
    using tut::ensure_equals;

    // A deterministic generator for the script.
    struct Rng
    {
        U64 s;
        explicit Rng(U64 seed) : s(seed) {}
        U64 next()
        {
            s ^= s << 13;
            s ^= s >> 7;
            s ^= s << 17;
            return s;
        }
        U32 below(U32 n) { return (U32)(next() % n); }
        LLUUID uuid()
        {
            LLUUID id;
            for (int i = 0; i < 16; ++i)
            {
                id.mData[i] = (U8)next();
            }
            return id;
        }
    };

    // The fixed clock: every entry is stamped from this counter.
    U32 g_clock = 1700000000;
    U32 fixed_clock() { return g_clock; }

    std::string scratch_dir()
    {
        const char* tmp = LLFile::tmpdir();
        std::string base = (tmp && *tmp) ? tmp : ".";
        return base + gDirUtilp->getDirDelimiter() + "vks_texcache_parity";
    }

    void remove_tree(const std::string& dir)
    {
        gDirUtilp->deleteDirAndContents(dir);
    }

    // The files under a directory, relative paths with '/' separators,
    // sorted, with their bytes.
    typedef std::vector<std::pair<std::string, std::vector<U8> > > snapshot_t;

    void collect(const std::string& root, const std::string& rel, snapshot_t& out)
    {
        std::string dir = rel.empty() ? root : root + gDirUtilp->getDirDelimiter() + rel;
        std::vector<std::string> names = gDirUtilp->getFilesInDir(dir);
        for (const std::string& n : names)
        {
            std::string path = dir + gDirUtilp->getDirDelimiter() + n;
            std::string relpath = rel.empty() ? n : rel + "/" + n;
            if (LLFile::isdir(path))
            {
                collect(root, relpath, out);
            }
            else
            {
                std::ifstream f(path.c_str(), std::ios::binary);
                std::vector<U8> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                out.push_back(std::make_pair(relpath, bytes));
            }
        }
        std::sort(out.begin(), out.end());
    }

    // getFilesInDir does not list subdirectories on every platform; walk
    // the known layout explicitly.
    void snapshot(const std::string& root, snapshot_t& out)
    {
        out.clear();
        std::string tex = root + gDirUtilp->getDirDelimiter() + "texturecache";
        snapshot_t files;
        std::vector<std::string> names = gDirUtilp->getFilesInDir(tex);
        for (const std::string& n : names)
        {
            std::string path = tex + gDirUtilp->getDirDelimiter() + n;
            if (!LLFile::isdir(path))
            {
                std::ifstream f(path.c_str(), std::ios::binary);
                std::vector<U8> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                files.push_back(std::make_pair("texturecache/" + n, bytes));
            }
        }
        const char* subdirs = "0123456789abcdef";
        for (int i = 0; i < 16; ++i)
        {
            std::string sub = tex + gDirUtilp->getDirDelimiter() + subdirs[i];
            std::vector<std::string> bodies = gDirUtilp->getFilesInDir(sub);
            for (const std::string& n : bodies)
            {
                std::string path = sub + gDirUtilp->getDirDelimiter() + n;
                std::ifstream f(path.c_str(), std::ios::binary);
                std::vector<U8> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                files.push_back(std::make_pair(std::string("texturecache/") + subdirs[i] + "/" + n, bytes));
            }
        }
        std::sort(files.begin(), files.end());
        out.swap(files);
    }

    // Fixture container: magic, version, snapshot count, then per snapshot
    // the name, file count and (path, size, bytes) records.
    const U32 FIXTURE_MAGIC = 0x56435450; // "VCTP"
    const U32 FIXTURE_VERSION = 1;

    void put_u32(std::ostream& o, U32 v) { o.write((const char*)&v, 4); }
    void put_str(std::ostream& o, const std::string& s) { put_u32(o, (U32)s.size()); o.write(s.data(), s.size()); }
    U32 get_u32(std::istream& i) { U32 v = 0; i.read((char*)&v, 4); return v; }
    std::string get_str(std::istream& i) { U32 n = get_u32(i); std::string s(n, '\0'); i.read(&s[0], n); return s; }

    typedef std::vector<std::pair<std::string, snapshot_t> > record_t;

    void write_record(const std::string& path, const record_t& rec)
    {
        std::ofstream o(path.c_str(), std::ios::binary);
        put_u32(o, FIXTURE_MAGIC);
        put_u32(o, FIXTURE_VERSION);
        put_u32(o, (U32)rec.size());
        for (const auto& snap : rec)
        {
            put_str(o, snap.first);
            put_u32(o, (U32)snap.second.size());
            for (const auto& file : snap.second)
            {
                put_str(o, file.first);
                put_u32(o, (U32)file.second.size());
                o.write((const char*)file.second.data(), file.second.size());
            }
        }
    }

    bool read_record(const std::string& path, record_t& rec)
    {
        std::ifstream i(path.c_str(), std::ios::binary);
        if (!i) return false;
        if (get_u32(i) != FIXTURE_MAGIC) return false;
        if (get_u32(i) != FIXTURE_VERSION) return false;
        U32 snaps = get_u32(i);
        for (U32 s = 0; s < snaps; ++s)
        {
            std::string name = get_str(i);
            U32 files = get_u32(i);
            snapshot_t snap;
            for (U32 f = 0; f < files; ++f)
            {
                std::string rel = get_str(i);
                U32 size = get_u32(i);
                std::vector<U8> bytes(size);
                if (size) i.read((char*)bytes.data(), size);
                snap.push_back(std::make_pair(rel, bytes));
            }
            rec.push_back(std::make_pair(name, snap));
        }
        return (bool)i;
    }

    // ---- driving the cache -------------------------------------------------

    class CaptureReadResponder : public LLTextureCache::ReadResponder
    {
    public:
        CaptureReadResponder() : mDone(false), mSuccess(false) {}
        void completed(bool success) override
        {
            mDone = true;
            mSuccess = success;
            if (success && mFormattedImage.notNull())
            {
                mData.assign(mFormattedImage->getData(), mFormattedImage->getData() + mFormattedImage->getDataSize());
            }
        }
        bool mDone;
        bool mSuccess;
        std::vector<U8> mData;
    };

    class CaptureWriteResponder : public LLTextureCache::WriteResponder
    {
    public:
        CaptureWriteResponder() : mDone(false), mSuccess(false) {}
        void completed(bool success) override { mDone = true; mSuccess = success; }
        bool mDone;
        bool mSuccess;
    };

    struct Texture
    {
        LLUUID id;
        std::vector<U8> data;
        S32 image_size;
        S32 discard;
        S32 raw_w, raw_h, raw_c;
    };

    void pump(LLTextureCache& cache, int times = 4)
    {
        for (int i = 0; i < times; ++i)
        {
            cache.update(1000.f);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    LLPointer<LLImageRaw> make_raw(Rng& rng, S32 w, S32 h, S32 c)
    {
        LLPointer<LLImageRaw> raw = new LLImageRaw((U16)w, (U16)h, (S8)c);
        U8* p = raw->getData();
        for (S32 i = 0; i < w * h * c; ++i)
        {
            p[i] = (U8)rng.next();
        }
        return raw;
    }

    bool write_texture(LLTextureCache& cache, Rng& rng, const Texture& t)
    {
        LLPointer<LLImageRaw> raw = make_raw(rng, t.raw_w, t.raw_h, t.raw_c);
        CaptureWriteResponder* responder = new CaptureWriteResponder();
        LLPointer<LLTextureCache::WriteResponder> keep(responder);
        LLTextureCache::handle_t h = cache.writeToCache(t.id, t.data.data(), (S32)t.data.size(), t.image_size, raw, t.discard, responder);
        if (h == LLTextureCache::nullHandle())
        {
            return false;
        }
        for (int i = 0; i < 2000 && !cache.writeComplete(h, false); ++i)
        {
            pump(cache, 1);
        }
        pump(cache, 2);
        return responder->mSuccess;
    }

    // One read: (success, data size, image size).
    LLSD read_texture(LLTextureCache& cache, const LLUUID& id, S32 offset, S32 size)
    {
        CaptureReadResponder* responder = new CaptureReadResponder();
        LLPointer<LLTextureCache::ReadResponder> keep(responder);
        LLTextureCache::handle_t h = cache.readFromCache(id, offset, size, responder);
        for (int i = 0; i < 2000 && !cache.readComplete(h, false); ++i)
        {
            pump(cache, 1);
        }
        pump(cache, 2);
        LLSD r;
        r["success"] = responder->mSuccess;
        r["size"] = (S32)responder->mData.size();
        U32 crc = 0;
        for (U8 b : responder->mData) crc = crc * 31 + b;
        r["crc"] = (S32)crc;
        return r;
    }

    std::vector<U8> make_data(Rng& rng, S32 size)
    {
        std::vector<U8> d(size);
        for (S32 i = 0; i < size; ++i) d[i] = (U8)(rng.next() >> 8);
        return d;
    }

    // The script. Returns the event log; appends the snapshots.
    LLSD run_script(const std::string& root, record_t& snapshots)
    {
        LLSD events;
        Rng rng(0x9E3779B97F4A7C15ULL);
        g_clock = 1700000000;
        LLTextureCache::sClock = fixed_clock;

        std::vector<Texture> textures;
        for (int i = 0; i < 40; ++i)
        {
            Texture t;
            t.id = rng.uuid();
            if (i < 3)
            {
                t.id.mData[0] = 0; // the validated 1/256th on reopen
            }
            S32 size = (i % 2 == 0) ? (S32)(100 + rng.below(499)) : (S32)(601 + rng.below(19000));
            t.data = make_data(rng, size);
            t.image_size = size + ((rng.below(3) == 0) ? (S32)(1 + rng.below(5000)) : 0);
            t.discard = (S32)rng.below(6);
            S32 dims[] = { 16, 32, 64, 128 };
            t.raw_w = dims[rng.below(4)];
            t.raw_h = dims[rng.below(4)];
            t.raw_c = (rng.below(2) == 0) ? 3 : 4;
            textures.push_back(t);
        }

        {
            LLTextureCache cache(true);
            cache.setReadOnly(false);
            S64 unused = cache.initCache(LL_PATH_CACHE, 500000, false);
            events["init_unused"] = (S32)unused;
            events["max_entries"] = (S32)cache.getMaxEntries();
            events["max_usage"] = (S32)cache.getMaxUsage().value();

            // Phase 1: writes.
            for (size_t i = 0; i < textures.size(); ++i)
            {
                g_clock += 1 + (U32)rng.below(3);
                bool ok = write_texture(cache, rng, textures[i]);
                events["writes"].append(LLSD(ok));
            }
            events["entries_after_writes"] = (S32)cache.getEntries();
            events["usage_after_writes"] = (S32)cache.getUsage().value();

            // Phase 2: reads, hits and misses, whole and partial.
            for (size_t i = 0; i < textures.size(); i += 3)
            {
                events["reads"].append(read_texture(cache, textures[i].id, 0, (S32)textures[i].data.size() + 4096));
                events["reads"].append(read_texture(cache, textures[i].id, 0, 300));
            }
            events["reads"].append(read_texture(cache, rng.uuid(), 0, 4096));

            // Phase 3: fast cache reads.
            for (size_t i = 0; i < textures.size(); i += 5)
            {
                S32 discard = -1;
                LLPointer<LLImageRaw> raw = cache.readFromFastCache(textures[i].id, discard);
                LLSD r;
                r["present"] = raw.notNull();
                if (raw.notNull())
                {
                    r["w"] = raw->getWidth();
                    r["h"] = raw->getHeight();
                    r["c"] = (S32)raw->getComponents();
                    r["discard"] = discard;
                    U32 crc = 0;
                    for (S32 k = 0; k < raw->getDataSize(); ++k) crc = crc * 31 + raw->getData()[k];
                    r["crc"] = (S32)crc;
                }
                events["fast"].append(r);
            }

            // Phase 4: re-writes with more data (the update-entry paths),
            // the same data again (nothing changed), and removals.
            for (size_t i = 1; i < textures.size(); i += 4)
            {
                g_clock += 2;
                Texture t = textures[i];
                S32 more = (S32)(t.data.size() + 700 + rng.below(3000));
                t.data = make_data(rng, more);
                t.image_size = more + 1;
                textures[i] = t;
                events["rewrites"].append(LLSD(write_texture(cache, rng, t)));
                events["rewrites"].append(LLSD(write_texture(cache, rng, t)));
            }
            for (size_t i = 2; i < textures.size(); i += 7)
            {
                events["removes"].append(LLSD(cache.removeFromCache(textures[i].id)));
            }
            events["removes"].append(LLSD(cache.removeFromCache(rng.uuid())));

            // Phase 5: over the limit: large writes trigger the lazy purge,
            // then repeated no-op writes drain it.
            std::vector<Texture> big;
            for (int i = 0; i < 24; ++i)
            {
                g_clock += 1;
                Texture t;
                t.id = rng.uuid();
                S32 size = 15000 + (S32)rng.below(6000);
                t.data = make_data(rng, size);
                t.image_size = size;
                t.discard = 1;
                t.raw_w = 64; t.raw_h = 64; t.raw_c = 3;
                big.push_back(t);
                events["big_writes"].append(LLSD(write_texture(cache, rng, t)));
            }
            for (int i = 0; i < 12; ++i)
            {
                write_texture(cache, rng, textures[0]);
            }
            events["entries_after_purge"] = (S32)cache.getEntries();
            events["usage_after_purge"] = (S32)cache.getUsage().value();
            for (size_t i = 0; i < big.size(); i += 4)
            {
                events["in_cache_after_purge"].append(LLSD(cache.isInCache(big[i].id)));
            }
            // The destructor flushes the time-stamped entries.
        }
        snapshot_t s1;
        snapshot(root, s1);
        snapshots.push_back(std::make_pair("after_script", s1));

        // Phase 6: damage two body files and reopen: the validation of the
        // entries whose id starts with the validate counter's byte purges
        // the one with the bad size; the LRU is rebuilt.
        {
            std::string tex = root + gDirUtilp->getDirDelimiter() + "texturecache";
            std::string body0 = tex + gDirUtilp->getDirDelimiter() + "0" + gDirUtilp->getDirDelimiter() + textures[1].id.asString() + ".texture";
            LLFile::remove(body0);
            std::ofstream trunc((tex + gDirUtilp->getDirDelimiter() + "0" + gDirUtilp->getDirDelimiter() + textures[2].id.asString() + ".texture").c_str(), std::ios::binary | std::ios::trunc);
            trunc << "short";
            trunc.close();
            gSavedSettings.setU32("CacheValidateCounter", 0);
            LLTextureCache cache(true);
            cache.setReadOnly(false);
            cache.initCache(LL_PATH_CACHE, 500000, false);
            events["reopen_entries"] = (S32)cache.getEntries();
            events["reopen_usage"] = (S32)cache.getUsage().value();
            events["reopen_validate_counter"] = (S32)gSavedSettings.getU32("CacheValidateCounter");
            for (int i = 0; i < 3; ++i)
            {
                events["reopen_in_cache"].append(LLSD(cache.isInCache(textures[i].id)));
            }
            events["reopen_read"] = read_texture(cache, textures[2].id, 0, 4096);
            events["reopen_read_ok"] = read_texture(cache, textures[0].id, 0, 4096);
        }
        snapshot_t s2;
        snapshot(root, s2);
        snapshots.push_back(std::make_pair("after_reopen", s2));

        // Phase 7: a cache version mismatch purges everything.
        {
            LLTextureCache cache(true);
            cache.setReadOnly(false);
            cache.initCache(LL_PATH_CACHE, 500000, true);
            events["mismatch_entries"] = (S32)cache.getEntries();
        }
        snapshot_t s3;
        snapshot(root, s3);
        snapshots.push_back(std::make_pair("after_mismatch", s3));

        LLTextureCache::sClock = NULL;
        return events;
    }

    // The entries header carries the JPEG 2000 engine string, which changes
    // with the codec crate: it is compared against the running engine, not
    // the recording (bytes 8..40 of texture.entries).
    // The fast cache records are 1040 bytes: a 16-byte head (w, h, c, level)
    // and 1024 bytes of pixels, of which w*h*c are in use; the C++ filled the
    // rest from its pad buffer (stale bytes of the previous record,
    // uninitialised memory for the first), the port writes zeros. The unused
    // tail is never read; it is masked.
    // The engine string is checked on the files the cache under test
    // wrote (check_engine); the record's carries the engine of the day it
    // was made and is only masked.
    std::vector<U8> mask_engine(const std::string& rel, const std::vector<U8>& bytes, std::vector<std::string>& problems, bool check_engine)
    {
        if (rel == "texturecache/FastCache.cache")
        {
            std::vector<U8> masked(bytes);
            for (size_t off = 0; off + 1040 <= masked.size(); off += 1040)
            {
                S32 head[4];
                memcpy(head, &masked[off], 16);
                S64 used = (S64)head[0] * head[1] * head[2];
                if (used < 0 || used > 1024) used = 0;
                std::fill(masked.begin() + off + 16 + (size_t)used, masked.begin() + off + 1040, 0);
            }
            return masked;
        }
        if (rel != "texturecache/texture.entries" || bytes.size() < 44)
        {
            return bytes;
        }
        std::string engine(bytes.begin() + 8, bytes.begin() + 40);
        engine = engine.c_str();
        if (check_engine && engine != LLImageJ2C::getEngineInfo())
        {
            problems.push_back(rel + ": engine string \"" + engine + "\" is not the running engine \"" + LLImageJ2C::getEngineInfo() + "\"");
        }
        std::vector<U8> masked(bytes);
        std::fill(masked.begin() + 8, masked.begin() + 40, 0);
        return masked;
    }

    void compare_snapshots(const std::string& name, const snapshot_t& got, const snapshot_t& want, std::vector<std::string>& problems)
    {
        std::map<std::string, const std::vector<U8>*> want_map;
        for (const auto& f : want) want_map[f.first] = &f.second;
        std::map<std::string, const std::vector<U8>*> got_map;
        for (const auto& f : got) got_map[f.first] = &f.second;
        for (const auto& f : want)
        {
            auto it = got_map.find(f.first);
            if (it == got_map.end())
            {
                problems.push_back(name + ": missing " + f.first);
            }
            else if (mask_engine(f.first, *it->second, problems, true) != mask_engine(f.first, f.second, problems, false))
            {
                size_t n = std::min(it->second->size(), f.second.size());
                size_t k = 0;
                while (k < n && (*it->second)[k] == f.second[k]) ++k;
                std::ostringstream s;
                s << name << ": " << f.first << " differs at byte " << k << " (sizes " << it->second->size() << " vs " << f.second.size() << ")";
                problems.push_back(s.str());
            }
        }
        for (const auto& f : got)
        {
            if (want_map.find(f.first) == want_map.end())
            {
                problems.push_back(name + ": unexpected " + f.first);
            }
        }
    }

}

namespace
{
    // The script's outcomes (write and read results, read sizes and data
    // checksums, fast cache dimensions, levels and pixel checksums, usage
    // and entry counts) against the record.
    void compare_events(const std::string& path, const LLSD& got, const LLSD& want, std::vector<std::string>& problems)
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
                compare_events(path + "/" + it->first, got[it->first], it->second, problems);
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
                compare_events(s.str(), got[i], want[i], problems);
            }
        }
        else if (got.asString() != want.asString())
        {
            problems.push_back(path + ": '" + got.asString() + "' expected '" + want.asString() + "'");
        }
    }
}

namespace tut
{
    struct parity_data {};
    typedef test_group<parity_data> parity_group;
    typedef parity_group::object parity_object;
    tut::parity_group parity_instance("lltexturecache_parity");

    template<> template<>
    void parity_object::test<1>()
    {
        set_test_name("texture cache files and outcomes identical to the recording");
        gSavedSettings.declareU32("CacheValidateCounter", 0, "", LLControlVariable::PERSIST_NO);
        std::string root = scratch_dir();
        remove_tree(root);
        LLFile::mkdir(root);
        ensure("scratch cache dir", gDirUtilp->setCacheDir(root));

        record_t got;
        LLSD events = run_script(root, got);
        LL_INFOS() << "events: " << events << LL_ENDL;

        // VKS_TEXCACHE_RECORD=<file> records the files, VKS_TEXCACHE_RECORD_EVENTS=<file>
        // the outcomes.
        const char* record = getenv("VKS_TEXCACHE_RECORD");
        const char* record_events = getenv("VKS_TEXCACHE_RECORD_EVENTS");
        if ((record && *record) || (record_events && *record_events))
        {
            if (record && *record)
            {
                write_record(record, got);
                LL_INFOS() << "recorded " << got.size() << " snapshots to " << record << LL_ENDL;
            }
            if (record_events && *record_events)
            {
                std::ofstream o(record_events);
                LLSDSerialize::toPrettyXML(events, o);
                std::cout << "recorded the outcomes to " << record_events << std::endl;
            }
            // Run it again on a fresh directory: the script must be
            // reproducible before it can be a record.
            remove_tree(root);
            LLFile::mkdir(root);
            record_t again;
            LLSD events_again = run_script(root, again);
            std::vector<std::string> problems;
            for (size_t i = 0; i < got.size(); ++i)
            {
                compare_snapshots(got[i].first, again[i].second, got[i].second, problems);
            }
            compare_events("events", events_again, events, problems);
            for (const std::string& p : problems) { LL_WARNS() << p << LL_ENDL; std::cout << "PARITY: " << p << std::endl; }
            ensure("the script reproduces its own recording", problems.empty());
            remove_tree(root);
            return;
        }

        record_t want;
        ensure("fixture present: " VKS_TEXCACHE_FIXTURE, read_record(VKS_TEXCACHE_FIXTURE, want));
        ensure_equals("snapshot count", got.size(), want.size());
        std::vector<std::string> problems;
        for (size_t i = 0; i < want.size(); ++i)
        {
            ensure_equals("snapshot name", got[i].first, want[i].first);
            compare_snapshots(want[i].first, got[i].second, want[i].second, problems);
        }
        LLSD want_events;
        {
            std::ifstream i(VKS_TEXCACHE_EVENTS);
            ensure("outcomes present: " VKS_TEXCACHE_EVENTS, (bool)i);
            ensure("outcomes parse", LLSDSerialize::fromXML(want_events, i) > 0);
        }
        compare_events("events", events, want_events, problems);
        for (const std::string& p : problems) { LL_WARNS() << p << LL_ENDL; std::cout << "PARITY: " << p << std::endl; }
        size_t files = 0;
        for (const auto& s : want) files += s.second.size();
        LL_INFOS() << "compared " << files << " files over " << want.size() << " snapshots, " << problems.size() << " differences" << LL_ENDL;
        std::cout << "compared " << files << " files over " << want.size() << " snapshots and "
                  << want_events.size() << " outcome groups, " << problems.size() << " differences" << std::endl;
        ensure("cache files and outcomes identical", problems.empty());
        remove_tree(root);
    }

    template<> template<>
    void parity_object::test<2>()
    {
        set_test_name("index lookup and write timing");
        const char* bench = getenv("VKS_TEXCACHE_BENCH");
        if (!bench || !*bench)
        {
            return;
        }
        int iterations = atoi(bench);
        std::string root = scratch_dir() + "_bench";
        remove_tree(root);
        LLFile::mkdir(root);
        ensure("scratch cache dir", gDirUtilp->setCacheDir(root));
        Rng rng(42);
        LLTextureCache cache(true);
        cache.setReadOnly(false);
        cache.initCache(LL_PATH_CACHE, 200 * 1024 * 1024, false);
        std::vector<Texture> textures;
        for (int i = 0; i < 200; ++i)
        {
            Texture t;
            t.id = rng.uuid();
            t.data = make_data(rng, 4000);
            t.image_size = 4000;
            t.discard = 2;
            t.raw_w = 64; t.raw_h = 64; t.raw_c = 3;
            textures.push_back(t);
            write_texture(cache, rng, t);
        }
        typedef std::chrono::steady_clock clock;
        // Lookups: isInCache is the index lookup under the header lock.
        auto t0 = clock::now();
        int hits = 0;
        for (int it = 0; it < iterations; ++it)
        {
            for (const Texture& t : textures) hits += cache.isInCache(t.id) ? 1 : 0;
        }
        double lookup_us = std::chrono::duration<double, std::micro>(clock::now() - t0).count() / (double)(iterations * textures.size());
        // Writes of a new 4000-byte texture (index insert, entry write,
        // fast cache record, header record, body file), unthreaded.
        t0 = clock::now();
        int writes = 0;
        for (int it = 0; it < iterations; ++it)
        {
            Texture t;
            t.id = rng.uuid();
            t.data = textures[it % textures.size()].data;
            t.image_size = 4000;
            t.discard = 2;
            t.raw_w = 64; t.raw_h = 64; t.raw_c = 3;
            writes += write_texture(cache, rng, t) ? 1 : 0;
        }
        double write_us = std::chrono::duration<double, std::micro>(clock::now() - t0).count() / (double)iterations;
        // Reads of a cached 4000-byte texture (index lookup, header record
        // and body file reads), unthreaded.
        t0 = clock::now();
        for (int it = 0; it < iterations; ++it)
        {
            read_texture(cache, textures[it % textures.size()].id, 0, 8192);
        }
        double read_us = std::chrono::duration<double, std::micro>(clock::now() - t0).count() / (double)iterations;
        LL_INFOS() << "VKS_TEXCACHE_BENCH lookup_us=" << lookup_us << " write_us=" << write_us << " read_us=" << read_us
                   << " hits=" << hits << " writes=" << writes << LL_ENDL;
        const char* out = getenv("VKS_TEXCACHE_BENCH_OUT");
        if (out && *out)
        {
            std::ofstream o(out, std::ios::app);
            o << "texcache,lookup," << lookup_us << "\n";
            o << "texcache,write," << write_us << "\n";
            o << "texcache,read," << read_us << "\n";
        }
        remove_tree(root);
    }

    // The strings the asset FFI writes into a caller's buffer are never cut:
    // vks_ffi_string retries with the full length (an asset URL past 2 KB, a
    // cache path past 1 KB).
    template<> template<>
    void parity_object::test<3>()
    {
        set_test_name("FFI strings longer than the first buffer");
        const LLUUID id("a1234567-89ab-cdef-1032-547698badcfe");
        const std::string cap(5000, 'c');
        const std::string type("texture");
        const std::string url = vks_ffi_string([&](char* buf, size_t n)
            { return vks_asset_url(cap.data(), cap.size(), type.data(), type.size(), id.mData, buf, n); });
        ensure_equals("asset URL", url, cap + "/?texture_id=" + id.asString());
        const std::string dir(3000, 'd');
        const std::string delim("/");
        const std::string path = vks_ffi_string([&](char* buf, size_t n)
            { return vks_diskcache_metadata_to_filepath(dir.data(), dir.size(), delim.data(), delim.size(), id.mData, buf, n); });
        ensure_equals("cache path", path, dir + "/a/sl_cache_" + id.asString() + "_0.asset");
        const std::string small = vks_ffi_string([&](char* buf, size_t n)
            { return vks_asset_url("u", 1, type.data(), type.size(), id.mData, buf, n); });
        ensure_equals("short asset URL", small, "u/?texture_id=" + id.asString());
    }

    // <VulkanStorm> tryReadFromFastCache (upstream #5356): the same hits and
    // misses as readFromFastCache, never busy without contention, and a busy
    // answer that is not a miss: no image, discardlevel untouched, and the
    // same texture reads once the contention ends. The C ABI cannot hold the
    // fast cache on demand, so contention comes from a second thread reading
    // it; vkstorm-assets' try_read_fast_is_busy_while_the_fast_cache_is_held
    // covers the busy answer deterministically.
    template<> template<>
    void parity_object::test<4>()
    {
        set_test_name("fast cache read without waiting");
        std::string root = scratch_dir() + "_tryfast";
        remove_tree(root);
        LLFile::mkdir(root);
        ensure("scratch cache dir", gDirUtilp->setCacheDir(root));
        Rng rng(5356);
        {
            LLTextureCache cache(true);
            cache.setReadOnly(false);
            cache.initCache(LL_PATH_CACHE, 50 * 1024 * 1024, false);
            std::vector<Texture> textures;
            for (int i = 0; i < 40; ++i)
            {
                Texture t;
                t.id = rng.uuid();
                t.data = make_data(rng, 4000);
                t.image_size = 4000;
                t.discard = 2;
                t.raw_w = 64; t.raw_h = 64; t.raw_c = 3;
                textures.push_back(t);
                ensure("write", write_texture(cache, rng, t));
            }

            // Hits and misses match the waiting read.
            std::vector<LLUUID> hit_ids;
            for (const Texture& t : textures)
            {
                S32 want_discard = -7;
                LLPointer<LLImageRaw> want = cache.readFromFastCache(t.id, want_discard);
                S32 discard = -7;
                bool busy = true;
                LLPointer<LLImageRaw> got = cache.tryReadFromFastCache(t.id, discard, busy);
                ensure("not busy without contention", !busy);
                ensure_equals("same presence", got.notNull(), want.notNull());
                ensure_equals("same discard", discard, want_discard);
                if (want.notNull())
                {
                    hit_ids.push_back(t.id);
                    ensure_equals("same width", got->getWidth(), want->getWidth());
                    ensure_equals("same height", got->getHeight(), want->getHeight());
                    ensure_equals("same components", (S32)got->getComponents(), (S32)want->getComponents());
                    ensure_equals("same size", got->getDataSize(), want->getDataSize());
                    ensure("same data", std::equal(got->getData(), got->getData() + got->getDataSize(), want->getData()));
                }
            }
            ensure("the fast cache has two entries", hit_ids.size() >= 2);
            {
                S32 discard = -7;
                bool busy = true;
                LLPointer<LLImageRaw> got = cache.tryReadFromFastCache(rng.uuid(), discard, busy);
                ensure("miss: no image", got.isNull());
                ensure("miss: not busy", !busy);
                ensure_equals("miss: discard untouched", discard, -7);
            }

            // Contention: a second thread keeps reading the fast cache.
            const LLUUID id = hit_ids[0];
            const LLUUID other = hit_ids[1];
            std::atomic<bool> stop(false);
            std::thread reader([&]()
            {
                while (!stop.load())
                {
                    S32 d = 0;
                    cache.readFromFastCache(other, d);
                }
            });
            S32 busy_count = 0;
            auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (busy_count < 20 && std::chrono::steady_clock::now() < until)
            {
                S32 discard = -7;
                bool busy = false;
                LLPointer<LLImageRaw> got = cache.tryReadFromFastCache(id, discard, busy);
                if (busy)
                {
                    ++busy_count;
                    ensure("busy: no image", got.isNull());
                    ensure_equals("busy: discard untouched", discard, -7);
                }
                else
                {
                    ensure("not busy: the hit", got.notNull());
                }
            }
            stop = true;
            reader.join();
            std::cout << "fast cache contention: " << busy_count << " busy answers observed" << std::endl;

            S32 discard = -7;
            bool busy = true;
            LLPointer<LLImageRaw> got = cache.tryReadFromFastCache(id, discard, busy);
            ensure("after contention: not busy", !busy);
            ensure("after contention: the hit", got.notNull());
        }
        remove_tree(root);
    }
    // </VulkanStorm>
}
