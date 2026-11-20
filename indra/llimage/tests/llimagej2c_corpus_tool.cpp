// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// A tool, not a test: decodes a folder of codestreams (the viewer's texture cache copied out, see below) through
// the J2C codec this build links (OpenJPEG, or neo-j2k with USE_NEOJ2K), to compare the two builds and to time
// them. Built on demand, in either configuration: the target llimagej2c_corpus_tool (LL_TESTS).
//
//   llimagej2c_corpus_tool hash <dir>            one line for each file: size, then for each discard level 0-5 and for
//                                                the file cut at 1/4, 1/2 and 3/4 of its bytes (levels 0 and 2) the
//                                                result and a hash of the raw image. Run it in the two builds and diff.
//   llimagej2c_corpus_tool bench <dir> [reps]    the decode (header pass and decode) of every file at level 0, on this
//                                                thread (no pool: neo-j2k gets no executor), in microseconds, by
//                                                size class
//   llimagej2c_corpus_tool pool <dir> <sparse|saturated> [reps]
//                                                the same through the viewer's ImageDecode pool: sparse posts one
//                                                request at a time (the pool is idle, so neo-j2k is lent it),
//                                                saturated posts them all at once (the queue is full, so nothing is lent)
//
// A corpus folder holds the codestreams as the viewer has them: for a texture cache entry, the first 600 bytes of
// texture.cache's slot followed by the entry's .texture body file (a whole file, or the prefix downloaded so far).
#include "linden_common.h"
#include "llimage.h"
#include "llimagej2c.h"
#include "llimageworker.h"
#include "lltimer.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    using Clock = std::chrono::steady_clock;

    struct Texture
    {
        std::string name;
        std::vector<U8> bytes;
    };

    std::vector<Texture> loadCorpus(const std::string& dir)
    {
        std::vector<fs::path> paths;
        for (const auto& entry : fs::directory_iterator(dir))
        {
            if (entry.is_regular_file())
            {
                paths.push_back(entry.path());
            }
        }
        std::sort(paths.begin(), paths.end());
        std::vector<Texture> out;
        for (const auto& path : paths)
        {
            std::ifstream in(path, std::ios::binary);
            Texture t;
            t.name = path.filename().string();
            t.bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            if (t.bytes.size() >= 16)
            {
                out.push_back(std::move(t));
            }
        }
        return out;
    }

    U64 fnv1a(const U8* data, size_t size, U64 h = 1469598103934665603ull)
    {
        for (size_t i = 0; i < size; ++i)
        {
            h = (h ^ data[i]) * 1099511628211ull;
        }
        return h;
    }

    LLPointer<LLImageJ2C> makeImage(const U8* bytes, size_t size)
    {
        LLPointer<LLImageJ2C> j2c = new LLImageJ2C();
        U8* data = j2c->allocateData((S32)size);
        if (!data)
        {
            return nullptr;
        }
        memcpy(data, bytes, size);
        return j2c;
    }

    // What ImageRequest::processRequest does for one request: the header pass, the discard level, the raw image at
    // the size of the image, the decode. The result is ok (the raw image came out), the size and a hash of it.
    struct Result
    {
        bool ok = false;
        S32 w = 0, h = 0, c = 0;
        U64 hash = 0;
    };

    Result decodeOne(const U8* bytes, size_t size, S32 discard)
    {
        Result r;
        LLPointer<LLImageJ2C> j2c = makeImage(bytes, size);
        if (j2c.isNull() || !j2c->updateData() || j2c->getWidth() * j2c->getHeight() * j2c->getComponents() == 0)
        {
            return r;
        }
        j2c->setDiscardLevel(discard);
        LLPointer<LLImageRaw> raw = new LLImageRaw(j2c->getWidth(), j2c->getHeight(), j2c->getComponents());
        if (!raw->getData() || !j2c->decode(raw, 0.f) || !raw->getData())
        {
            return r;
        }
        r.ok = true;
        r.w = raw->getWidth();
        r.h = raw->getHeight();
        r.c = raw->getComponents();
        r.hash = fnv1a(raw->getData(), (size_t)raw->getDataSize());
        return r;
    }

    int runHash(const std::string& dir)
    {
        const std::vector<Texture> corpus = loadCorpus(dir);
        for (const Texture& t : corpus)
        {
            std::printf("%s %zu", t.name.c_str(), t.bytes.size());
            for (S32 d = 0; d <= 5; ++d)
            {
                const Result r = decodeOne(t.bytes.data(), t.bytes.size(), d);
                std::printf(" | d%d %s %dx%dx%d %016llx", d, r.ok ? "ok" : "fail", r.w, r.h, r.c, (unsigned long long)r.hash);
            }
            for (int quarter = 1; quarter <= 3; ++quarter)
            {
                const size_t cut = t.bytes.size() * quarter / 4;
                for (S32 d : { 0, 2 })
                {
                    const Result r = cut >= 16 ? decodeOne(t.bytes.data(), cut, d) : Result();
                    std::printf(" | c%d/4 d%d %s %dx%dx%d %016llx", quarter, d, r.ok ? "ok" : "fail", r.w, r.h, r.c, (unsigned long long)r.hash);
                }
            }
            std::printf("\n");
        }
        return 0;
    }

    std::string sizeClass(const Texture& t)
    {
        LLPointer<LLImageJ2C> j2c = makeImage(t.bytes.data(), t.bytes.size());
        if (j2c.isNull() || !j2c->updateData())
        {
            return "unreadable";
        }
        const S32 side = std::max(j2c->getWidth(), j2c->getHeight());
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%04d", side <= 32 ? 32 : side <= 64 ? 64 : side <= 128 ? 128 : side <= 256 ? 256 : side <= 512 ? 512 : side <= 1024 ? 1024 : 2048);
        return buf;
    }

    void report(const char* title, std::map<std::string, std::vector<double>>& us)
    {
        std::printf("%s\n  class   images   mean us    p50 us    p95 us   total ms\n", title);
        double all = 0;
        size_t n = 0;
        for (auto& [name, v] : us)
        {
            std::sort(v.begin(), v.end());
            double sum = 0;
            for (double x : v) sum += x;
            std::printf("  <=%s %7zu %9.0f %9.0f %9.0f %10.1f\n", name.c_str(), v.size(), sum / v.size(), v[v.size() / 2],
                        v[std::min(v.size() - 1, v.size() * 95 / 100)], sum / 1000.0);
            all += sum;
            n += v.size();
        }
        std::printf("  all   %7zu %9.0f %30s %10.1f\n", n, n ? all / n : 0.0, "", all / 1000.0);
    }

    int runBench(const std::string& dir, int reps)
    {
        const std::vector<Texture> corpus = loadCorpus(dir);
        std::vector<std::string> cls;
        for (const Texture& t : corpus) cls.push_back(sizeClass(t));
        std::map<std::string, std::vector<double>> us;
        for (int rep = 0; rep < reps; ++rep)
        {
            for (size_t i = 0; i < corpus.size(); ++i)
            {
                if (i % 200 == 0)
                {
                    std::fprintf(stderr, "bench pass %d: %zu / %zu
", rep, i, corpus.size());
                }
                const auto t0 = Clock::now();
                const Result r = decodeOne(corpus[i].bytes.data(), corpus[i].bytes.size(), 0);
                const double elapsed = std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
                if (r.ok && rep > 0) // the first pass warms the decoder's memory, as the pool's threads are warm
                {
                    us[cls[i]].push_back(elapsed);
                }
            }
        }
        report("one thread (no executor), header pass + decode at level 0, microseconds each:", us);
        return 0;
    }

    class Done : public LLImageDecodeThread::Responder
    {
    public:
        Done(std::atomic<U32>* done, std::atomic<U32>* ok) : mDone(done), mOk(ok) {}
        void completed(bool success, const std::string&, LLImageRaw*, LLImageRaw*, U32) override
        {
            if (success)
            {
                mOk->fetch_add(1);
            }
            mDone->fetch_add(1);
        }
    private:
        std::atomic<U32>* mDone;
        std::atomic<U32>* mOk;
    };

    int runPool(const std::string& dir, bool sparse, int reps)
    {
        const std::vector<Texture> corpus = loadCorpus(dir);
        std::vector<std::string> cls;
        for (const Texture& t : corpus) cls.push_back(sizeClass(t));
        LLImageDecodeThread pool(true);
        std::printf("pool: %u workers, %s\n", pool.getWorkerCount(), sparse ? "sparse (one request at a time)" : "saturated (all at once)");
        std::map<std::string, std::vector<double>> us;
        double wall_ms = 0;
        size_t decoded = 0;
        for (int rep = 0; rep < reps; ++rep)
        {
            std::atomic<U32> done{0}, ok{0};
            std::vector<LLPointer<LLImageJ2C>> images;
            for (const Texture& t : corpus) images.push_back(makeImage(t.bytes.data(), t.bytes.size()));
            const auto t0 = Clock::now();
            for (size_t i = 0; i < corpus.size(); ++i)
            {
                const U32 before = done.load();
                const auto r0 = Clock::now();
                pool.decodeImage(images[i], 0, false, new Done(&done, &ok));
                if (sparse)
                {
                    while (done.load() == before)
                    {
                        std::this_thread::yield();
                    }
                    if (rep > 0)
                    {
                        us[cls[i]].push_back(std::chrono::duration<double, std::micro>(Clock::now() - r0).count());
                    }
                }
            }
            while (done.load() < corpus.size())
            {
                std::this_thread::yield();
            }
            if (rep > 0)
            {
                wall_ms += std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
                decoded += ok.load();
            }
        }
        if (sparse)
        {
            report("request latency (post to completion; header pass + decode at level 0), microseconds:", us);
        }
        std::printf("wall: %.1f ms for %zu decodes (%.0f us each)\n", wall_ms, decoded, decoded ? wall_ms * 1000.0 / decoded : 0.0);
        pool.shutdown();
        return 0;
    }
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: llimagej2c_corpus_tool hash <dir> | bench <dir> [reps] | pool <dir> <sparse|saturated> [reps]\n");
        return 2;
    }
    ll_init_apr();
    LLImage::initClass();
    const std::string mode = argv[1];
    int rc = 2;
    if (mode == "hash")
    {
        rc = runHash(argv[2]);
    }
    else if (mode == "bench")
    {
        rc = runBench(argv[2], argc > 3 ? std::atoi(argv[3]) : 3);
    }
    else if (mode == "pool" && argc > 3)
    {
        rc = runPool(argv[2], std::string(argv[3]) == "sparse", argc > 4 ? std::atoi(argv[4]) : 3);
    }
    LLImage::cleanupClass();
    return rc;
}
