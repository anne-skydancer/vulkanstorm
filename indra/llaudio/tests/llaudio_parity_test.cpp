/**
 * @file llaudio_parity_test.cpp
 * @brief Parity of the audio engine and the sound asset decoder against the
 *        corpora recorded from the C++ (SoLoud) implementation before the
 *        port of the engine to Rust (vkstorm-rust rung six).
 *
 * Two corpora, both under tests/fixtures:
 *
 *  - Decoded sound assets (llaudio_decode_parity.txt, inputs under
 *    decode/inputs, a few complete outputs under decode/expected): every
 *    input is written to a scratch asset cache and decoded through the
 *    viewer's decode path (the DECODE ADAPTER); the outcome (failure or
 *    success, the cache and LLAudioData state) and the SHA-256 of the .dsf
 *    the path wrote are compared with the record. libvorbis 1.3.7 decodes a
 *    few samples differently on Linux than on Windows (the platform's float
 *    M_PI), so on a platform other than the recording one the bytes are
 *    compared with an in-process oracle instead, oracleDecode below, which
 *    calls libvorbisfile directly and mirrors llaudiodecodemgr.cpp step for
 *    step; outcomes are always compared with the record. Each result says
 *    which reference it was checked against.
 *
 *  - Mixed scenes (llaudio_parity_scenes.txt, the script; llaudio_mix_2ch,
 *    _6ch, _8ch.vksa, the rendered output; llaudio_mix_observables.txt):
 *    the script is replayed on a fresh engine opened on a null output device
 *    at 2, 6 and 8 channels and 44100 Hz (the ENGINE ADAPTER), driving it
 *    only through LLAudioEngine, LLAudioSource, LLAudioChannel and the
 *    listener; the rendered float output is compared sample for sample and
 *    the per-step observables (channel assignment, isPlaying, loop flags,
 *    queued data, priorities, voice counts) line for line. The wind scene
 *    is noise from the viewer's thread-local ll_frand generator, seeded from
 *    the clock, so it is compared as per-segment RMS, averaged over six
 *    renders, within a tolerance. The corpus was recorded on Windows; on
 *    another platform a record made there (llaudio_mix_<n>ch.<platform>.vksa,
 *    which VKS_AUDIO_RECORD writes on that platform) is preferred when
 *    present. SoLoud crashes in Soloud::mix once more than 64 voices play,
 *    so the scenes peak at 62 voices.
 *
 * VKS_AUDIO_RECORD=<dir>         records both corpora into <dir> (then checks
 *                                that a second run reproduces the record)
 * VKS_AUDIO_MIX_TOLERANCE=<abs>  accepts mixed samples within <abs> (default 0)
 * VKS_AUDIO_BENCH=<blocks>       times the mixer (64 moving 3D voices, 1024-frame
 *                                blocks, 2/6/8 channels) and the decode corpus
 * VKS_AUDIO_BENCH_OUT=<csv>      appends the timings to <csv>
 * VKS_AUDIO_DECODE_REFERENCE=oracle  compares decoded bytes with the oracle, not the record
 * VKS_AUDIO_TRACE=1              prints each scene command as it runs
 * VKS_AUDIO_SCENES=<script>      replays another script instead of the fixture (exploration)
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "llaudio_parity_adapter.h"

#include "llaudioengine.h"
#include "lllistener.h"
#include "llvorbisencode.h"
#include "lldir.h"
#include "lldiskcache.h"
#include "llfile.h"
#include "llfilesystem.h"
#include "lllfsthread.h"
#include "llframetimer.h"
#include "llmath.h"
#include "threadpool.h"
#include "workqueue.h"

#include "lltut.h"

#include "vorbis/codec.h"
#include "vorbis/vorbisfile.h"

#ifdef LL_USESYSTEMLIBS
# include <zlib.h>
#else
# include "zlib-ng/zlib.h"
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#ifndef VKS_AUDIO_FIXTURE_DIR
#error VKS_AUDIO_FIXTURE_DIR must name the directory of the recorded corpora
#endif

using namespace vks_audio_parity;

namespace
{
    using tut::ensure;
    using tut::ensure_equals;

    const std::string FIXTURES = VKS_AUDIO_FIXTURE_DIR;
    const unsigned RATE = 44100;
    const unsigned CHANNEL_COUNTS[] = {2, 6, 8};

#if LL_WINDOWS
    const char* PLATFORM = "windows-msvc";
#elif LL_LINUX
    const char* PLATFORM = "linux";
#elif LL_DARWIN
    const char* PLATFORM = "macos";
#else
    const char* PLATFORM = "other";
#endif

    // ------------------------------------------------------------------
    // Small utilities

    std::string env(const char* name)
    {
        const char* value = getenv(name);
        return value ? std::string(value) : std::string();
    }

    std::string path_join(const std::string& dir, const std::string& name)
    {
        return dir + gDirUtilp->getDirDelimiter() + name;
    }

    bool read_file(const std::string& path, std::vector<U8>& data)
    {
        llifstream in(path.c_str(), std::ios::binary);
        if (!in)
        {
            return false;
        }
        data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        return true;
    }

    bool write_file(const std::string& path, const void* data, size_t size)
    {
        llofstream out(path.c_str(), std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(data), (std::streamsize)size);
        return (bool)out;
    }

    std::vector<std::string> read_lines(const std::string& path)
    {
        std::vector<std::string> lines;
        llifstream in(path.c_str());
        std::string line;
        while (std::getline(in, line))
        {
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            lines.push_back(line);
        }
        return lines;
    }

    std::vector<std::string> split(const std::string& line)
    {
        std::vector<std::string> out;
        std::istringstream in(line);
        std::string token;
        while (in >> token)
        {
            out.push_back(token);
        }
        return out;
    }

    void make_dirs(const std::string& path)
    {
        // LLFile::mkdir does one level; create each prefix in turn.
        for (size_t at = path.find_first_of("/\\", 3); at != std::string::npos; at = path.find_first_of("/\\", at + 1))
        {
            LLFile::mkdir(path.substr(0, at));
        }
        LLFile::mkdir(path);
    }

    std::string fmt(const char* format, double value)
    {
        char buf[64];
        snprintf(buf, sizeof(buf), format, value);
        return buf;
    }

    // SHA-256 (FIPS 180-4), for the record of decoded bytes.
    struct Sha256
    {
        U32 h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
        U8 block[64];
        size_t used = 0;
        U64 total = 0;

        static U32 rotr(U32 x, int n) { return (x >> n) | (x << (32 - n)); }

        void compress(const U8* p)
        {
            static const U32 k[64] = {
                0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
                0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
                0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
                0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
                0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
                0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
                0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
                0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
            U32 w[64];
            for (int i = 0; i < 16; ++i)
            {
                w[i] = (U32(p[i * 4]) << 24) | (U32(p[i * 4 + 1]) << 16) | (U32(p[i * 4 + 2]) << 8) | U32(p[i * 4 + 3]);
            }
            for (int i = 16; i < 64; ++i)
            {
                const U32 s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
                const U32 s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
                w[i] = w[i - 16] + s0 + w[i - 7] + s1;
            }
            U32 a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
            for (int i = 0; i < 64; ++i)
            {
                const U32 t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
                const U32 t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
                hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
            }
            h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
        }

        void update(const U8* p, size_t n)
        {
            total += n;
            while (n)
            {
                const size_t take = std::min(n, sizeof(block) - used);
                memcpy(block + used, p, take);
                used += take; p += take; n -= take;
                if (used == sizeof(block))
                {
                    compress(block);
                    used = 0;
                }
            }
        }

        std::string hex()
        {
            const U64 bits = total * 8;
            const U8 pad = 0x80;
            update(&pad, 1);
            const U8 zero = 0;
            while (used != 56)
            {
                update(&zero, 1);
            }
            U8 length[8];
            for (int i = 0; i < 8; ++i)
            {
                length[i] = U8(bits >> (56 - 8 * i));
            }
            update(length, 8);
            std::string out;
            char buf[9];
            for (U32 word : h)
            {
                snprintf(buf, sizeof(buf), "%08x", word);
                out += buf;
            }
            return out;
        }
    };

    std::string sha256(const std::vector<U8>& data)
    {
        Sha256 s;
        s.update(data.data(), data.size());
        return s.hex();
    }

    // Float output as stored: little-endian float32, byte-planar (all first
    // bytes, then all second bytes, ...), zlib level 9.
    std::vector<U8> pack_floats(const std::vector<float>& samples)
    {
        const size_t n = samples.size();
        std::vector<U8> planar(n * 4);
        for (size_t i = 0; i < n; ++i)
        {
            U32 bits;
            memcpy(&bits, &samples[i], 4);
            for (size_t b = 0; b < 4; ++b)
            {
                planar[b * n + i] = U8(bits >> (8 * b));
            }
        }
        uLongf z_len = compressBound((uLong)planar.size());
        std::vector<U8> z(z_len);
        if (compress2(z.data(), &z_len, planar.data(), (uLong)planar.size(), 9) != Z_OK)
        {
            return {};
        }
        z.resize(z_len);
        return z;
    }

    bool unpack_floats(const std::vector<U8>& z, size_t count, std::vector<float>& samples)
    {
        std::vector<U8> planar(count * 4);
        uLongf len = (uLongf)planar.size();
        if (uncompress(planar.data(), &len, z.data(), (uLong)z.size()) != Z_OK || len != planar.size())
        {
            return false;
        }
        samples.resize(count);
        for (size_t i = 0; i < count; ++i)
        {
            U32 bits = 0;
            for (size_t b = 0; b < 4; ++b)
            {
                bits |= U32(planar[b * count + i]) << (8 * b);
            }
            memcpy(&samples[i], &bits, 4);
        }
        return true;
    }

    // ------------------------------------------------------------------
    // Protected engine interfaces, reached through pointers to members
    // formed in derived classes (no object of these classes is created).
    // These are the base classes' own virtuals and fields, not a backend's.

    // The statics are named do* so they do not hide the members they reach;
    // the member pointers must name the derived class to pass the protected
    // access rule, and then call the base class virtuals on any channel.
    struct ChannelAccess : LLAudioChannel
    {
        static void doPlay(LLAudioChannel* c) { (c->*(&ChannelAccess::play))(); }
        static void doPlaySynced(LLAudioChannel* c, LLAudioChannel* m) { (c->*(&ChannelAccess::playSynced))(m); }
        static void doCleanup(LLAudioChannel* c) { (c->*(&ChannelAccess::cleanup))(); }
        static bool doIsPlaying(LLAudioChannel* c) { return (c->*(&ChannelAccess::isPlaying))(); }
        static bool doUpdateBuffer(LLAudioChannel* c) { return (c->*(&ChannelAccess::updateBuffer))(); }
        static void doUpdate3DPosition(LLAudioChannel* c) { (c->*(&ChannelAccess::update3DPosition))(); }
        static void doUpdateLoop(LLAudioChannel* c) { (c->*(&ChannelAccess::updateLoop))(); }
        static bool& looped(LLAudioChannel* c) { return c->*(&ChannelAccess::mLoopedThisFrame); }
    };

    struct EngineAccess : LLAudioEngine
    {
        static LLAudioChannel* doCreateChannel(LLAudioEngine* e) { return (e->*(&EngineAccess::createChannel))(); }
        static void doCommit(LLAudioEngine* e) { (e->*(&EngineAccess::commitDeferredChanges))(); }
        static std::array<LLAudioChannel*, LL_MAX_AUDIO_CHANNELS>& channels(LLAudioEngine* e)
        {
            return e->*(&EngineAccess::mChannels);
        }
    };

    // A source whose velocity the script can set (the viewer's object
    // sources set it from the object; LLAudioSource has no setter).
    class TestSource : public LLAudioSource
    {
    public:
        TestSource(const LLUUID& id, F32 gain, S32 type) : LLAudioSource(id, LLUUID::null, gain, type) {}
        void setVelocity(const LLVector3& velocity) { mVelocity = velocity; }
        LLAudioChannel* channel() const { return getChannel(); }
    };

    // The frame clock every LLFrameTimer reads: advanced only by the script,
    // so source ages, buffer ages and the wind update interval are
    // deterministic.
    struct FrameClock : LLFrameTimer
    {
        static void set(F64 seconds) { sFrameTime = seconds; }
        static F64 get() { return sFrameTime; }
    };

    // ------------------------------------------------------------------
    // The viewer runtime the engine and the decoder need: an asset cache
    // and a sound cache on scratch directories, the LFS thread, the
    // "mainloop" work queue and the "General" thread pool.

    std::string dsf_path(const LLUUID& id)
    {
        return gDirUtilp->getExpandedFilename(LL_PATH_FS_SOUND_CACHE, id.asString()) + ".dsf";
    }

    // One per test: the directories are made (and the disk cache singleton
    // initialised) once per process, the threads and queues per test.
    struct Runtime
    {
        std::unique_ptr<LL::WorkQueue> mainloop;
        std::unique_ptr<LL::ThreadPool> general;

        Runtime()
        {
            static bool directories = false;
            if (!directories)
            {
                const std::string root = path_join(LLFile::tmpdir(), "vks_audio_parity");
                gDirUtilp->deleteDirAndContents(root);
                make_dirs(root);
                const std::string assets = path_join(root, "assets");
                const std::string sounds = path_join(root, "sounds");
                make_dirs(assets);
                make_dirs(sounds);
                LLDiskCache::initParamSingleton(assets, (uintmax_t)1024 * 1024 * 1024, false, 95.f, 85.f);
                gDirUtilp->setSoundCacheDir(sounds);
                directories = true;
            }
            LLLFSThread::initClass(true);
            mainloop.reset(new LL::WorkQueue("mainloop", 1024 * 1024));
            general.reset(new LL::ThreadPool("General", 2));
            general->start();
        }

        ~Runtime()
        {
            general->close();
            general.reset();
            mainloop->close();
            mainloop.reset();
            LLLFSThread::cleanupClass();
        }
    };

    // ------------------------------------------------------------------
    // Decode corpus

    // The decode path of llaudiodecodemgr.cpp, step for step, over an
    // in-memory asset with libvorbisfile directly: the reference on a
    // platform the corpus was not recorded on. The callbacks reproduce the
    // LLFileSystem semantics the viewer's cache_* callbacks see (partial
    // reads succeed, a seek outside the file clamps and fails).
    struct OracleFile
    {
        const std::vector<U8>* data;
        S32 position = 0;
    };

    size_t oracle_read(void* ptr, size_t size, size_t nmemb, void* datasource)
    {
        auto* file = static_cast<OracleFile*>(datasource);
        const S32 size_total = (S32)file->data->size();
        const S32 want = (S32)(size * nmemb);
        const S32 available = llmax(0, size_total - file->position);
        const S32 got = llmin(llmax(want, 0), available);
        if (got <= 0)
        {
            return 0;
        }
        memcpy(ptr, file->data->data() + file->position, got);
        file->position += got;
        return got / size;
    }

    int oracle_seek(void* datasource, ogg_int64_t offset, int whence)
    {
        auto* file = static_cast<OracleFile*>(datasource);
        if (offset > S32_MAX)
        {
            return -1;
        }
        const S32 size = (S32)file->data->size();
        S32 origin = 0;
        switch (whence)
        {
        case SEEK_SET: origin = 0; break;
        case SEEK_END: origin = size; break;
        case SEEK_CUR: origin = file->position; break;
        default: return -1;
        }
        const S64 target = (S64)origin + (S64)(S32)offset;
        if (target < 0 || target > size)
        {
            file->position = (S32)llclamp(target, (S64)0, (S64)size);
            return -1;
        }
        file->position = (S32)target;
        return 0;
    }

    int oracle_close(void*) { return 0; }

    long oracle_tell(void* datasource)
    {
        return static_cast<OracleFile*>(datasource)->position;
    }

    // Returns true and the .dsf bytes when the viewer would write one.
    bool oracleDecode(const std::vector<U8>& asset, std::vector<U8>& wav)
    {
        const S32 WAV_HEADER_SIZE = 44;
        wav.clear();
        if (asset.empty())
        {
            return false; // initDecode: !getSize()
        }
        OracleFile file{&asset, 0};
        ov_callbacks callbacks;
        callbacks.read_func = oracle_read;
        callbacks.seek_func = oracle_seek;
        callbacks.close_func = oracle_close;
        callbacks.tell_func = oracle_tell;
        OggVorbis_File vf;
        memset(&vf, 0, sizeof(vf));
        if (ov_open_callbacks(&file, &vf, NULL, 0, callbacks) < 0)
        {
            return false;
        }
        struct Clear { OggVorbis_File* vf; ~Clear() { ov_clear(vf); } } clear{&vf};

        const S32 sample_count = (S32)ov_pcm_total(&vf, -1);
        size_t size_guess = (size_t)sample_count;
        vorbis_info* vi = ov_info(&vf, -1);
        size_guess *= (vi ? vi->channels : 1);
        size_guess *= 2;
        size_guess += 2048;
        bool abort_decode = false;
        if (vi)
        {
            if (vi->channels < 1 || vi->channels > LLVORBIS_CLIP_MAX_CHANNELS)
            {
                abort_decode = true;
            }
        }
        else
        {
            abort_decode = true;
        }
        if ((size_t)sample_count > LLVORBIS_CLIP_REJECT_SAMPLES || (size_t)sample_count <= 0)
        {
            abort_decode = true;
        }
        if (size_guess > LLVORBIS_CLIP_REJECT_SIZE)
        {
            abort_decode = true;
        }
        if (abort_decode)
        {
            return false;
        }

        std::vector<U8> buffer(WAV_HEADER_SIZE);
        static const U8 header[WAV_HEADER_SIZE] = {
            0x52, 0x49, 0x46, 0x46, 0, 0, 0, 0, 0x57, 0x41, 0x56, 0x45, 0x66, 0x6D, 0x74, 0x20,
            0x10, 0, 0, 0, 0x01, 0, 0x01, 0, 0x44, 0xAC, 0, 0, 0x88, 0x58, 0x01, 0,
            0x02, 0, 0x10, 0, 0x64, 0x61, 0x74, 0x61, 0, 0, 0, 0};
        memcpy(buffer.data(), header, WAV_HEADER_SIZE);

        int section = 0;
        for (;;)
        {
            char pcmout[4096];
            const long ret = ov_read(&vf, pcmout, sizeof(pcmout), 0, 2, 1, &section);
            if (ret == 0)
            {
                break;
            }
            if (ret < 0)
            {
                return false; // decodeSection: invalid, flushed
            }
            buffer.insert(buffer.end(), pcmout, pcmout + ret);
        }

        // finishDecode, including its conditions as written.
        S32 data_length = static_cast<S32>(buffer.size()) - WAV_HEADER_SIZE;
        buffer[40] = (data_length) & 0x000000FF;
        buffer[41] = (data_length >> 8) & 0x000000FF;
        buffer[42] = (data_length >> 16) & 0x000000FF;
        buffer[43] = (data_length >> 24) & 0x000000FF;
        data_length += 36;
        buffer[4] = (data_length) & 0x000000FF;
        buffer[5] = (data_length >> 8) & 0x000000FF;
        buffer[6] = (data_length >> 16) & 0x000000FF;
        buffer[7] = (data_length >> 24) & 0x000000FF;
        {
            char pcmout[4096] = {};
            const S32 fade_length = llmin((S32)128, (S32)(data_length - 36) / 8);
            if ((S32)buffer.size() >= (WAV_HEADER_SIZE + 2 * fade_length))
            {
                memcpy(pcmout, &buffer[WAV_HEADER_SIZE], (2 * fade_length));
            }
            S16* samplep = (S16*)pcmout;
            for (S32 i = 0; i < fade_length; i++)
            {
                *samplep = llfloor((F32)*samplep * ((F32)i / (F32)fade_length));
                samplep++;
            }
            if ((WAV_HEADER_SIZE + (2 * fade_length)) < (S32)buffer.size())
            {
                memcpy(&buffer[WAV_HEADER_SIZE], pcmout, (2 * fade_length));
            }
            const S32 near_end = static_cast<S32>(buffer.size()) - (2 * fade_length);
            if ((S32)buffer.size() >= (near_end + 2 * fade_length))
            {
                memcpy(pcmout, &buffer[near_end], (2 * fade_length));
            }
            samplep = (S16*)pcmout;
            for (S32 i = fade_length - 1; i >= 0; i--)
            {
                *samplep = llfloor((F32)*samplep * ((F32)i / (F32)fade_length));
                samplep++;
            }
            // As written, the tail is never copied back (the condition
            // cannot hold); the record reflects that.
            if (near_end + (2 * fade_length) < (S32)buffer.size())
            {
                memcpy(&buffer[near_end], pcmout, (2 * fade_length));
            }
        }
        if (36 == data_length)
        {
            return false;
        }
        wav.swap(buffer);
        return true;
    }

    struct DecodeCase
    {
        std::string rel;            // path under decode/inputs
        std::string input_sha;
        DecodeOutcome viewer;
        bool oracle_ok = false;
        std::vector<U8> oracle_wav;
    };

    std::string decode_line(const DecodeCase& c)
    {
        const DecodeOutcome& o = c.viewer;
        std::ostringstream line;
        line << c.rel << " in=" << c.input_sha << " began=" << o.began << " finished=" << o.finished
             << " dsf=" << o.dsf_present << " asset=" << o.asset_present
             << " data=" << o.data_completed << o.data_failed << o.data_decoded
             << " len=" << o.dsf.size() << " sha256=" << (o.dsf_present ? sha256(o.dsf) : std::string("-"));
        return line.str();
    }

    // Outcome fields only (no bytes): compared on every platform.
    std::string decode_outcome(const std::string& line)
    {
        const size_t at = line.find(" len=");
        return at == std::string::npos ? line : line.substr(0, at);
    }

    std::string flat_name(const std::string& rel)
    {
        std::string out = rel;
        std::replace(out.begin(), out.end(), '/', '_');
        if (out.size() > 4 && out.compare(out.size() - 4, 4, ".ogg") == 0)
        {
            out.resize(out.size() - 4);
        }
        return out + ".dsf";
    }

    std::vector<DecodeCase> run_decode_corpus(const std::vector<std::string>& inputs, double* elapsed_ms = nullptr,
                                              double* decode_ms = nullptr)
    {
        EngineInfo info;
        LLAudioEngine* engine = adapterCreateEngine(2, RATE, info);
        ensure("engine for the decode corpus", engine != nullptr);
        std::vector<DecodeCase> cases;
        double total_ms = 0.0, step_ms = 0.0;
        for (const std::string& rel : inputs)
        {
            DecodeCase c;
            c.rel = rel;
            std::vector<U8> asset;
            ensure("decode input present: " + rel, read_file(path_join(path_join(FIXTURES, "decode/inputs"), rel), asset));
            c.input_sha = sha256(asset);
            const LLUUID id = [&] { LLUUID u; u.generate("vks-audio-parity/asset/" + rel); return u; }();
            LLFileSystem::removeFile(id, LLAssetType::AT_SOUND);
            LLFile::remove(dsf_path(id));
            {
                LLFileSystem file(id, LLAssetType::AT_SOUND, LLFileSystem::WRITE);
                file.write(asset.data(), (S32)asset.size());
            }
            const auto t0 = std::chrono::steady_clock::now();
            c.viewer = adapterDecodeAsset(id);
            total_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            step_ms += c.viewer.decode_ms;
            c.oracle_ok = oracleDecode(asset, c.oracle_wav);
            cases.push_back(std::move(c));
            LLFileSystem::removeFile(id, LLAssetType::AT_SOUND);
            LLFile::remove(dsf_path(id));
        }
        adapterDestroyEngine(engine);
        if (elapsed_ms)
        {
            *elapsed_ms = total_ms;
        }
        if (decode_ms)
        {
            *decode_ms = step_ms;
        }
        return cases;
    }

    std::vector<std::string> decode_inputs()
    {
        std::vector<std::string> inputs;
        for (const std::string& line : read_lines(path_join(FIXTURES, "decode/inputs.txt")))
        {
            if (!line.empty() && line[0] != '#')
            {
                inputs.push_back(line);
            }
        }
        return inputs;
    }

    // ------------------------------------------------------------------
    // Mixed scenes: the script

    struct SoundDef
    {
        std::string name;
        std::vector<std::string> spec;
        LLUUID id;
    };

    struct Command
    {
        int line = 0;
        std::vector<std::string> args;
        std::string text() const
        {
            std::string out;
            for (const std::string& a : args)
            {
                out += (out.empty() ? "" : " ") + a;
            }
            return out;
        }
    };

    struct SceneDef
    {
        std::string name;
        unsigned block = 512;
        bool stat = false;
        std::vector<unsigned> channels; // empty: every output channel count
        std::vector<Command> commands;
    };

    struct Script
    {
        std::vector<SoundDef> sounds;
        std::vector<SceneDef> scenes;
    };

    Script parse_script(const std::string& path)
    {
        Script script;
        SceneDef* scene = nullptr;
        int number = 0;
        for (const std::string& raw : read_lines(path))
        {
            ++number;
            std::vector<std::string> args = split(raw.substr(0, raw.find('#')));
            if (args.empty())
            {
                continue;
            }
            if (args[0] == "sound")
            {
                ensure("sound definition: line " + std::to_string(number), args.size() >= 4 && !scene);
                SoundDef def;
                def.name = args[1];
                def.spec.assign(args.begin() + 2, args.end());
                def.id.generate("vks-audio-parity/sound/" + def.name);
                script.sounds.push_back(def);
            }
            else if (args[0] == "scene")
            {
                ensure("scene definition: line " + std::to_string(number), args.size() >= 2 && !scene);
                script.scenes.emplace_back();
                scene = &script.scenes.back();
                scene->name = args[1];
                for (size_t i = 2; i < args.size(); ++i)
                {
                    if (args[i].rfind("block=", 0) == 0) scene->block = (unsigned)atoi(args[i].c_str() + 6);
                    else if (args[i] == "stat") scene->stat = true;
                    else if (args[i].rfind("channels=", 0) == 0)
                    {
                        std::string list = args[i].substr(9);
                        std::replace(list.begin(), list.end(), ',', ' ');
                        for (const std::string& n : split(list)) scene->channels.push_back((unsigned)atoi(n.c_str()));
                    }
                    else ensure("scene option: " + args[i], false);
                }
            }
            else if (args[0] == "end")
            {
                ensure("end without scene: line " + std::to_string(number), scene != nullptr);
                scene = nullptr;
            }
            else
            {
                ensure("command outside a scene: line " + std::to_string(number), scene != nullptr);
                Command c;
                c.line = number;
                c.args = args;
                scene->commands.push_back(c);
            }
        }
        ensure("script ends its last scene", scene == nullptr);
        return script;
    }

    // Integer synthesis of the scene sounds, so they are identical on every
    // platform and compiler: 16-bit PCM in the .dsf layout the decoder
    // writes (44-byte header, mono, 44100 Hz).
    S64 floor_div(S64 a, S64 b)
    {
        S64 q = a / b;
        if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
        return q;
    }

    std::vector<U8> synthesize(const SoundDef& def)
    {
        const std::vector<std::string>& s = def.spec;
        const std::string& kind = s[0];
        auto num = [&](size_t i) { ensure("sound " + def.name + " argument " + std::to_string(i), i < s.size()); return atof(s[i].c_str()); };
        auto seconds_at = [&](size_t i) { return (S64)std::llround(num(i) * RATE); };
        auto amp_at = [&](size_t i) { return (S64)std::llround(num(i) * 32767.0); };
        auto k_of = [&](double hz) { return (S64)std::llround(2.0 * 3.14159265358979323846 * hz / RATE * 65536.0); };
        std::vector<S16> pcm;
        if (kind == "tone" || kind == "sweep")
        {
            const bool sweep = kind == "sweep";
            const S64 k0 = k_of(num(1));
            const S64 k1 = sweep ? k_of(num(2)) : k0;
            const S64 n = seconds_at(sweep ? 3 : 2);
            const S64 amp = amp_at(sweep ? 4 : 3);
            // Minsky's circle oscillator in Q16.
            S64 x = amp << 16, y = 0;
            for (S64 i = 0; i < n; ++i)
            {
                pcm.push_back((S16)llclamp(floor_div(y, 65536), (S64)-32768, (S64)32767));
                const S64 k = k0 + floor_div((k1 - k0) * i, llmax(n, (S64)1));
                x -= floor_div(k * y, 65536);
                y += floor_div(k * x, 65536);
            }
        }
        else if (kind == "noise")
        {
            U32 state = (U32)atoi(s[1].c_str()) | 1u;
            const S64 n = seconds_at(2), amp = amp_at(3);
            for (S64 i = 0; i < n; ++i)
            {
                state ^= state << 13; state ^= state >> 17; state ^= state << 5;
                pcm.push_back((S16)floor_div((S64)(S16)(state >> 16) * amp, 32767));
            }
        }
        else if (kind == "square")
        {
            const S64 period = llmax((S64)2, (S64)std::llround(RATE / num(1)));
            const S64 n = seconds_at(2), amp = amp_at(3);
            for (S64 i = 0; i < n; ++i)
            {
                pcm.push_back((S16)((i % period) < period / 2 ? amp : -amp));
            }
        }
        else if (kind == "clicks")
        {
            const S64 period = llmax((S64)1, (S64)std::llround(num(1) * RATE / 1000.0));
            const S64 n = seconds_at(2), amp = amp_at(3);
            for (S64 i = 0; i < n; ++i)
            {
                pcm.push_back((S16)(i % period == 0 ? amp : 0));
            }
        }
        else if (kind == "dc")
        {
            const S64 n = seconds_at(1), amp = amp_at(2);
            pcm.assign((size_t)n, (S16)amp);
        }
        else
        {
            ensure("sound kind: " + kind, false);
        }
        const U32 data = (U32)(pcm.size() * 2);
        std::vector<U8> wav(44 + data);
        auto put32 = [&](size_t at, U32 v) { for (int i = 0; i < 4; ++i) wav[at + i] = U8(v >> (8 * i)); };
        auto put16 = [&](size_t at, U16 v) { wav[at] = U8(v); wav[at + 1] = U8(v >> 8); };
        memcpy(&wav[0], "RIFF", 4); put32(4, data + 36); memcpy(&wav[8], "WAVEfmt ", 8);
        put32(16, 16); put16(20, 1); put16(22, 1); put32(24, RATE); put32(28, RATE * 2); put16(32, 2); put16(34, 16);
        memcpy(&wav[36], "data", 4); put32(40, data);
        for (size_t i = 0; i < pcm.size(); ++i)
        {
            put16(44 + 2 * i, (U16)pcm[i]);
        }
        return wav;
    }

    void install_sounds(const Script& script)
    {
        for (const SoundDef& def : script.sounds)
        {
            const std::vector<U8> wav = synthesize(def);
            ensure("sound written: " + def.name, write_file(dsf_path(def.id), wav.data(), wav.size()));
        }
    }

    // ------------------------------------------------------------------
    // Mixed scenes: the replay

    struct SceneOutput
    {
        std::string name;
        unsigned channels = 0;
        unsigned block = 0;
        bool stat = false;
        std::vector<float> pcm;               // interleaved
        std::vector<std::string> observables; // one line per observation
        std::vector<std::pair<std::string, size_t>> marks; // label, first frame
    };

    class Replay
    {
    public:
        Replay(const Script& script, const SceneDef& scene, unsigned channels)
            : mScene(scene), mChannels(channels)
        {
            for (const SoundDef& def : script.sounds)
            {
                mSoundNames[def.id] = def.name;
                mSoundIds[def.name] = def.id;
            }
        }

        SceneOutput run()
        {
            SceneOutput out;
            out.name = mScene.name;
            out.channels = mChannels;
            out.block = mScene.block;
            out.stat = mScene.stat;
            FrameClock::set(1000.0);
            EngineInfo info;
            mEngine = adapterCreateEngine(mChannels, RATE, info);
            ensure("engine on a null device: " + mScene.name, mEngine != nullptr);
            const bool trace = !env("VKS_AUDIO_TRACE").empty();
            for (size_t i = 0; i < mScene.commands.size(); ++i)
            {
                if (trace)
                {
                    std::cout << mScene.name << " " << mChannels << "ch line " << mScene.commands[i].line << ": "
                              << mScene.commands[i].text() << std::endl;
                }
                execute(mScene.commands[i], (int)i, out);
            }
            // Direct channels and sources belong to the test; engine sources
            // and channels to the engine.
            for (auto& ch : mDirectChannels)
            {
                delete ch.second;
            }
            for (auto& src : mSources)
            {
                if (!src.second.engine_owned)
                {
                    delete src.second.direct;
                }
            }
            adapterDestroyEngine(mEngine);
            mEngine = nullptr;
            return out;
        }

    private:
        struct SourceRec
        {
            LLUUID id;
            bool engine_owned = true;
            TestSource* direct = nullptr;
        };

        void fail(const Command& c, const std::string& why)
        {
            ensure(mScene.name + " line " + std::to_string(c.line) + " (" + c.text() + "): " + why, false);
        }

        F32 f(const Command& c, size_t i)
        {
            if (i >= c.args.size()) fail(c, "missing argument");
            return (F32)atof(c.args[i].c_str());
        }

        LLVector3 v3(const Command& c, size_t i) { return LLVector3(f(c, i), f(c, i + 1), f(c, i + 2)); }

        const LLUUID& sound(const Command& c, size_t i)
        {
            auto it = mSoundIds.find(i < c.args.size() ? c.args[i] : std::string());
            if (it == mSoundIds.end()) fail(c, "unknown sound");
            return it->second;
        }

        // Null when an engine source has been retired by the engine.
        TestSource* source(const Command& c, size_t i)
        {
            auto it = mSources.find(i < c.args.size() ? c.args[i] : std::string());
            if (it == mSources.end()) fail(c, "unknown source");
            return lookup(it->second);
        }

        TestSource* lookup(const SourceRec& rec)
        {
            if (!rec.engine_owned)
            {
                return rec.direct;
            }
            return static_cast<TestSource*>(mEngine->findAudioSource(rec.id));
        }

        LLAudioChannel* direct_channel(const Command& c, size_t i)
        {
            auto it = mDirectChannels.find(i < c.args.size() ? c.args[i] : std::string());
            if (it == mDirectChannels.end()) fail(c, "unknown channel");
            return it->second;
        }

        std::string data_name(LLAudioData* adp)
        {
            if (!adp) return "-";
            auto it = mSoundNames.find(adp->getID());
            return it == mSoundNames.end() ? adp->getID().asString() : it->second;
        }

        void observe(const Command& c, int index, SceneOutput& out)
        {
            std::ostringstream line;
            line << mScene.name << " " << mChannels << "ch #" << index << " " << c.args[0];
            const int voices = adapterVoiceCount(mEngine);
            line << " voices=" << (voices < 0 ? std::string("?") : std::to_string(voices));
            int allocated = 0, playing = 0;
            for (LLAudioChannel* ch : EngineAccess::channels(mEngine))
            {
                if (ch)
                {
                    ++allocated;
                    playing += ChannelAccess::doIsPlaying(ch) ? 1 : 0;
                }
            }
            line << " engch=" << allocated << "/" << playing;
            for (const std::string& name : mSourceOrder)
            {
                const SourceRec& rec = mSources[name];
                TestSource* src = lookup(rec);
                line << " " << name << "=";
                if (!src)
                {
                    line << "gone";
                    continue;
                }
                LLAudioChannel* ch = src->channel();
                line << "c" << (ch ? 1 : 0) << "p" << (ch && ChannelAccess::doIsPlaying(ch) ? 1 : 0)
                     << ":" << data_name(src->getCurrentData()) << ":" << data_name(src->getQueuedData())
                     << ":" << fmt("%.9g", src->getPriority());
            }
            for (const std::string& name : mChannelOrder)
            {
                LLAudioChannel* ch = mDirectChannels[name];
                line << " " << name << "=p" << (ChannelAccess::doIsPlaying(ch) ? 1 : 0) << "l" << (mLooped[name] ? 1 : 0);
            }
            out.observables.push_back(line.str());
        }

        void render(unsigned frames, SceneOutput& out)
        {
            const unsigned chunk_max = llmin(mScene.block, adapterMaxRenderFrames());
            std::vector<float> block;
            while (frames)
            {
                const unsigned chunk = llmin(frames, chunk_max);
                block.assign((size_t)chunk * mChannels, 0.f);
                adapterRender(mEngine, block.data(), chunk);
                out.pcm.insert(out.pcm.end(), block.begin(), block.end());
                FrameClock::set(FrameClock::get() + (F64)chunk / RATE);
                frames -= chunk;
            }
        }

        void execute(const Command& c, int index, SceneOutput& out)
        {
            const std::string& op = c.args[0];
            if (op == "master") mEngine->setMasterGain(f(c, 1));
            else if (op == "mute") mEngine->setMuted(f(c, 1) != 0.f);
            else if (op == "secondary") mEngine->setSecondaryGain((S32)f(c, 1), f(c, 2));
            else if (op == "doppler") mEngine->setDopplerFactor(f(c, 1));
            else if (op == "rolloff") mEngine->setRolloffFactor(f(c, 1));
            else if (op == "wind") mEngine->enableWind(f(c, 1) != 0.f);
            else if (op == "maxwind") mEngine->setMaxWindGain(f(c, 1));
            else if (op == "windvec") mEngine->updateWind(v3(c, 1), f(c, 4));
            else if (op == "listener")
            {
                // LLAudioEngine::setListener(pos, vel, up, at); the viewer
                // commits it in idle (commitDeferredChanges).
                mEngine->setListener(v3(c, 1), v3(c, 4), v3(c, 7), v3(c, 10));
            }
            else if (op == "source")
            {
                if (c.args.size() < 3 || mSources.count(c.args[1])) fail(c, "source name");
                SourceRec rec;
                rec.id.generate("vks-audio-parity/source/" + c.args[1]);
                rec.engine_owned = c.args[2] == "engine";
                if (!rec.engine_owned && c.args[2] != "direct") fail(c, "engine or direct");
                F32 gain = 1.f;
                S32 type = LLAudioEngine::AUDIO_TYPE_SFX;
                auto* src = new TestSource(rec.id, 1.f, type);
                for (size_t i = 3; i < c.args.size(); ++i)
                {
                    const std::string& a = c.args[i];
                    if (a.rfind("gain=", 0) == 0) gain = (F32)atof(a.c_str() + 5);
                    else if (a.rfind("type=", 0) == 0) type = atoi(a.c_str() + 5);
                    else if (a == "loop") src->setLoop(true);
                    else if (a == "forced") src->setForcedPriority(true);
                    else if (a == "master") src->setSyncMaster(true);
                    else if (a == "slave") src->setSyncSlave(true);
                    else if (a == "queue") src->setQueueSounds(true);
                    else fail(c, "source option " + a);
                }
                src->setGain(gain);
                src->setType(type);
                if (rec.engine_owned) mEngine->addAudioSource(src);
                else rec.direct = src;
                mSources[c.args[1]] = rec;
                mSourceOrder.push_back(c.args[1]);
            }
            else if (op == "pos")
            {
                if (TestSource* src = source(c, 1))
                {
                    src->setPositionGlobal(LLVector3d(f(c, 2), f(c, 3), f(c, 4)));
                    src->setVelocity(c.args.size() >= 8 ? v3(c, 5) : LLVector3::zero);
                }
            }
            else if (op == "gain") { if (TestSource* src = source(c, 1)) src->setGain(f(c, 2)); }
            else if (op == "loop") { if (TestSource* src = source(c, 1)) src->setLoop(f(c, 2) != 0.f); }
            else if (op == "play") { if (TestSource* src = source(c, 1)) src->play(sound(c, 2)); }
            else if (op == "stop") { if (TestSource* src = source(c, 1)) src->stop(); }
            else if (op == "data")
            {
                // A direct source's current data with its buffer loaded,
                // as LLAudioSource::play would set it up without a channel.
                TestSource* src = source(c, 1);
                const LLUUID& id = sound(c, 2);
                LLAudioData* adp = mEngine->getAudioData(id);
                if (!src || !adp) fail(c, "direct source data");
                src->addAudioData(adp, true);
                if (!mEngine->updateBufferForData(adp, id) || !adp->getBuffer()) fail(c, "buffer load");
            }
            else if (op == "channel")
            {
                if (c.args.size() < 3 || mDirectChannels.count(c.args[1])) fail(c, "channel name");
                LLAudioChannel* ch = EngineAccess::doCreateChannel(mEngine);
                mDirectChannels[c.args[1]] = ch;
                mChannelOrder.push_back(c.args[1]);
                mLooped[c.args[1]] = false;
                ch->setSource(source(c, 2));
            }
            else if (op == "chplay") ChannelAccess::doPlay(direct_channel(c, 1));
            else if (op == "chsynced") ChannelAccess::doPlaySynced(direct_channel(c, 1), direct_channel(c, 2));
            else if (op == "chcleanup") ChannelAccess::doCleanup(direct_channel(c, 1));
            else if (op == "chupdate")
            {
                // LLAudioEngine::updateChannels for one channel, then the
                // end-of-idle clear of its loop flag.
                LLAudioChannel* ch = direct_channel(c, 1);
                ChannelAccess::doUpdateBuffer(ch);
                ChannelAccess::doUpdate3DPosition(ch);
                ChannelAccess::doUpdateLoop(ch);
                mLooped[c.args[1]] = ChannelAccess::looped(ch);
                ChannelAccess::looped(ch) = false;
            }
            else if (op == "commit") EngineAccess::doCommit(mEngine);
            else if (op == "idle")
            {
                adapterIdle(mEngine);
                observe(c, index, out);
                for (auto& looped : mLooped) looped.second = false;
            }
            else if (op == "observe") observe(c, index, out);
            else if (op == "render") render((unsigned)atoi(c.args.at(1).c_str()), out);
            else if (op == "advance") FrameClock::set(FrameClock::get() + atof(c.args.at(1).c_str()));
            else if (op == "mark") out.marks.push_back({c.args.at(1), out.pcm.size() / mChannels});
            else fail(c, "unknown command");
        }

        const SceneDef& mScene;
        unsigned mChannels;
        LLAudioEngine* mEngine = nullptr;
        std::map<LLUUID, std::string> mSoundNames;
        std::map<std::string, LLUUID> mSoundIds;
        std::map<std::string, SourceRec> mSources;
        std::vector<std::string> mSourceOrder;
        std::map<std::string, LLAudioChannel*> mDirectChannels;
        std::vector<std::string> mChannelOrder;
        std::map<std::string, bool> mLooped;
    };

    // Per-channel mean square over each marked segment (to the next mark or
    // the end), for the scenes whose source is noise (wind).
    struct StatSegment
    {
        std::string label;
        size_t frames = 0;
        std::vector<double> mean_square;
    };

    std::vector<StatSegment> stat_segments(const SceneOutput& s)
    {
        std::vector<StatSegment> segments;
        const size_t frames = s.pcm.size() / s.channels;
        for (size_t m = 0; m < s.marks.size(); ++m)
        {
            StatSegment seg;
            const size_t start = s.marks[m].second;
            const size_t end = m + 1 < s.marks.size() ? s.marks[m + 1].second : frames;
            seg.label = s.marks[m].first;
            seg.frames = end - start;
            for (unsigned ch = 0; ch < s.channels; ++ch)
            {
                double sum = 0.0;
                for (size_t i = start; i < end; ++i)
                {
                    const double v = s.pcm[i * s.channels + ch];
                    sum += v * v;
                }
                seg.mean_square.push_back(end > start ? sum / (double)(end - start) : 0.0);
            }
            segments.push_back(seg);
        }
        return segments;
    }

    std::vector<std::string> stat_lines(const std::string& scene, unsigned channels, const std::vector<StatSegment>& segments)
    {
        std::vector<std::string> lines;
        for (const StatSegment& seg : segments)
        {
            std::ostringstream line;
            line << scene << " " << channels << "ch rms:" << seg.label << " " << seg.frames;
            for (double ms : seg.mean_square)
            {
                line << " " << fmt("%.6g", std::sqrt(ms));
            }
            lines.push_back(line.str());
        }
        return lines;
    }

    // ------------------------------------------------------------------
    // The mix corpus files

    struct MixRecord
    {
        std::string engine;
        unsigned channels = 0;
        unsigned rate = 0;
        struct Scene { std::string name; unsigned block = 0; std::vector<float> pcm; };
        std::vector<Scene> scenes;
    };

    void put_u32(std::vector<U8>& out, U32 v) { for (int i = 0; i < 4; ++i) out.push_back(U8(v >> (8 * i))); }
    void put_bytes(std::vector<U8>& out, const void* p, size_t n) { out.insert(out.end(), (const U8*)p, (const U8*)p + n); }

    bool write_mix(const std::string& path, const MixRecord& rec)
    {
        std::vector<U8> out;
        put_bytes(out, "VKSAMIX1", 8);
        put_u32(out, (U32)rec.engine.size());
        put_bytes(out, rec.engine.data(), rec.engine.size());
        put_u32(out, rec.channels);
        put_u32(out, rec.rate);
        put_u32(out, (U32)rec.scenes.size());
        for (const auto& s : rec.scenes)
        {
            const std::vector<U8> z = pack_floats(s.pcm);
            if (z.empty()) return false;
            put_u32(out, (U32)s.name.size());
            put_bytes(out, s.name.data(), s.name.size());
            put_u32(out, s.block);
            put_u32(out, (U32)(s.pcm.size() / rec.channels));
            put_u32(out, (U32)z.size());
            put_bytes(out, z.data(), z.size());
        }
        return write_file(path, out.data(), out.size());
    }

    bool read_mix(const std::string& path, MixRecord& rec)
    {
        std::vector<U8> in;
        if (!read_file(path, in) || in.size() < 8 || memcmp(in.data(), "VKSAMIX1", 8) != 0) return false;
        size_t at = 8;
        auto u32 = [&](U32& v) { if (at + 4 > in.size()) return false; v = 0; for (int i = 0; i < 4; ++i) v |= U32(in[at + i]) << (8 * i); at += 4; return true; };
        auto bytes = [&](size_t n, std::vector<U8>& v) { if (at + n > in.size()) return false; v.assign(in.begin() + at, in.begin() + at + n); at += n; return true; };
        U32 len, count;
        std::vector<U8> tmp;
        if (!u32(len) || !bytes(len, tmp)) return false;
        rec.engine.assign(tmp.begin(), tmp.end());
        if (!u32(rec.channels) || !u32(rec.rate) || !u32(count)) return false;
        for (U32 i = 0; i < count; ++i)
        {
            MixRecord::Scene s;
            U32 frames, z_len;
            if (!u32(len) || !bytes(len, tmp)) return false;
            s.name.assign(tmp.begin(), tmp.end());
            if (!u32(s.block) || !u32(frames) || !u32(z_len) || !bytes(z_len, tmp)) return false;
            if (!unpack_floats(tmp, (size_t)frames * rec.channels, s.pcm)) return false;
            rec.scenes.push_back(std::move(s));
        }
        return at == in.size();
    }

    // Compares observation lines token by token; "voices=?" (a backend
    // without a voice count) matches any count.
    bool same_observation(const std::string& got, const std::string& want)
    {
        if (got == want) return true;
        std::vector<std::string> a = split(got), b = split(want);
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
        {
            if (a[i] == b[i]) continue;
            if ((a[i] == "voices=?" || b[i] == "voices=?") && a[i].rfind("voices=", 0) == 0 && b[i].rfind("voices=", 0) == 0) continue;
            return false;
        }
        return true;
    }

    // Wind is pinked, resonant-filtered noise from a clock-seeded generator
    // (ll_frand), so a 1 s segment's RMS varies by up to 55 % between two
    // runs of the same engine and the balance between channels by up to
    // 0.13 (measured over 8 runs on Windows; averaged over 6 runs,
    // 20 % and 0.053). Each stat scene is therefore
    // rendered STAT_RUNS times and the mean squares averaged, in the record
    // and in the comparison; then each segment's RMS per channel must be
    // within STAT_LEVEL_TOLERANCE of the record (relative, or STAT_FLOOR
    // absolute near silence) and the balance (each channel's RMS over the
    // loudest channel's) within STAT_BALANCE_TOLERANCE. The segment labels
    // and lengths are exact. This catches a missing, misrouted, mis-panned
    // or mis-scaled wind, not sample-level differences.
    const int STAT_RUNS = 6;
    const double STAT_LEVEL_TOLERANCE = 0.35;
    const double STAT_BALANCE_TOLERANCE = 0.10;
    const double STAT_FLOOR = 2e-4;

    bool same_stat(const std::string& got, const std::string& want, std::string& why)
    {
        std::vector<std::string> a = split(got), b = split(want);
        if (a.size() != b.size() || a.size() < 5) { why = "shape"; return false; }
        for (size_t i = 0; i < 4; ++i) if (a[i] != b[i]) { why = "segment"; return false; }
        double top_a = 0.0, top_b = 0.0;
        for (size_t i = 4; i < a.size(); ++i)
        {
            const double x = atof(a[i].c_str()), y = atof(b[i].c_str());
            if (std::fabs(x - y) > llmax(STAT_FLOOR, STAT_LEVEL_TOLERANCE * std::fabs(y))) { why = "level " + a[i] + " vs " + b[i]; return false; }
            top_a = llmax(top_a, x);
            top_b = llmax(top_b, y);
        }
        if (top_a > STAT_FLOOR && top_b > STAT_FLOOR)
        {
            for (size_t i = 4; i < a.size(); ++i)
            {
                const double x = atof(a[i].c_str()) / top_a, y = atof(b[i].c_str()) / top_b;
                if (std::fabs(x - y) > STAT_BALANCE_TOLERANCE) { why = "balance " + fmt("%.4f", x) + " vs " + fmt("%.4f", y); return false; }
            }
        }
        return true;
    }

    struct MixRun
    {
        std::map<unsigned, MixRecord> records;  // by channel count
        std::vector<std::string> observables;
        std::vector<std::string> stats;
    };

    MixRun run_scenes(const Script& script)
    {
        MixRun run;
        for (unsigned channels : CHANNEL_COUNTS)
        {
            MixRecord& rec = run.records[channels];
            rec.channels = channels;
            rec.rate = RATE;
            EngineInfo info;
            LLAudioEngine* probe = adapterCreateEngine(channels, RATE, info);
            ensure("engine on a null device at " + std::to_string(channels) + " channels", probe != nullptr);
            adapterDestroyEngine(probe);
            rec.engine = info.name;
            for (const SceneDef& scene : script.scenes)
            {
                if (!scene.channels.empty() && std::find(scene.channels.begin(), scene.channels.end(), channels) == scene.channels.end())
                {
                    continue;
                }
                SceneOutput out = Replay(script, scene, channels).run();
                run.observables.insert(run.observables.end(), out.observables.begin(), out.observables.end());
                if (scene.stat)
                {
                    std::vector<StatSegment> mean = stat_segments(out);
                    for (int r = 1; r < STAT_RUNS; ++r)
                    {
                        const std::vector<StatSegment> more = stat_segments(Replay(script, scene, channels).run());
                        ensure("stat segments repeat", more.size() == mean.size());
                        for (size_t k = 0; k < mean.size(); ++k)
                            for (size_t ch = 0; ch < mean[k].mean_square.size(); ++ch)
                                mean[k].mean_square[ch] += more[k].mean_square[ch];
                    }
                    for (StatSegment& seg : mean)
                        for (double& ms : seg.mean_square)
                            ms /= STAT_RUNS;
                    const std::vector<std::string> lines = stat_lines(scene.name, channels, mean);
                    run.stats.insert(run.stats.end(), lines.begin(), lines.end());
                }
                else
                {
                    rec.scenes.push_back({scene.name, scene.block, std::move(out.pcm)});
                }
            }
        }
        return run;
    }

    // The record from Windows is llaudio_mix_<n>ch.vksa; a record made on
    // another platform (its compiler and libm round SoLoud's 3D and mixing
    // floats their own way) is llaudio_mix_<n>ch.<platform>.vksa and is
    // preferred there when it exists.
    std::string mix_path(const std::string& dir, unsigned channels, bool platform_record)
    {
        const std::string suffix = platform_record ? std::string(".") + PLATFORM : std::string();
        return path_join(dir, "llaudio_mix_" + std::to_string(channels) + "ch" + suffix + ".vksa");
    }

    bool is_record_platform() { return std::string(PLATFORM) == "windows-msvc"; }

    void write_lines(const std::string& path, const std::vector<std::string>& header, const std::vector<std::string>& lines)
    {
        llofstream out(path.c_str(), std::ios::binary | std::ios::trunc);
        for (const std::string& l : header) out << l << "\n";
        for (const std::string& l : lines) out << l << "\n";
    }

    std::vector<std::string> data_lines(const std::string& path)
    {
        std::vector<std::string> out;
        for (const std::string& l : read_lines(path))
        {
            if (!l.empty() && l[0] != '#') out.push_back(l);
        }
        return out;
    }

    // Compares one run of the scenes with a record; returns the problems.
    std::vector<std::string> compare_mix(const MixRun& got, const std::map<unsigned, MixRecord>& want,
                                         const std::vector<std::string>& want_obs, const std::vector<std::string>& want_stats,
                                         double tolerance, std::vector<std::string>& summary)
    {
        std::vector<std::string> problems;
        for (const auto& w : want)
        {
            auto g = got.records.find(w.first);
            if (g == got.records.end()) { problems.push_back("no run at " + std::to_string(w.first) + " channels"); continue; }
            if (g->second.scenes.size() != w.second.scenes.size()) { problems.push_back("scene count"); continue; }
            for (size_t i = 0; i < w.second.scenes.size(); ++i)
            {
                const auto& ws = w.second.scenes[i];
                const auto& gs = g->second.scenes[i];
                const std::string where = ws.name + " " + std::to_string(w.first) + "ch";
                if (gs.name != ws.name || gs.pcm.size() != ws.pcm.size())
                {
                    problems.push_back(where + ": shape (" + gs.name + ", " + std::to_string(gs.pcm.size()) + " samples vs " + std::to_string(ws.pcm.size()) + ")");
                    continue;
                }
                size_t differ = 0, first = 0;
                double max_diff = 0.0;
                for (size_t k = 0; k < ws.pcm.size(); ++k)
                {
                    const bool same = memcmp(&ws.pcm[k], &gs.pcm[k], 4) == 0;
                    if (!same)
                    {
                        const double d = std::fabs((double)ws.pcm[k] - (double)gs.pcm[k]);
                        if (!(d <= tolerance)) { if (!differ) first = k; ++differ; }
                        if (!(d <= max_diff)) max_diff = d;
                    }
                }
                std::ostringstream s;
                s << where << ": " << ws.pcm.size() / w.first << " frames, max |diff| " << max_diff << ", beyond tolerance " << differ;
                summary.push_back(s.str());
                if (differ)
                {
                    std::ostringstream p;
                    p << where << ": " << differ << " samples differ beyond " << tolerance << " (first at frame " << first / w.first
                      << " channel " << first % w.first << ": " << gs.pcm[first] << " vs " << ws.pcm[first] << "), max " << max_diff;
                    problems.push_back(p.str());
                }
            }
        }
        if (got.observables.size() != want_obs.size())
        {
            problems.push_back("observation count " + std::to_string(got.observables.size()) + " vs " + std::to_string(want_obs.size()));
        }
        for (size_t i = 0; i < llmin(got.observables.size(), want_obs.size()); ++i)
        {
            if (!same_observation(got.observables[i], want_obs[i]))
            {
                problems.push_back("observation\n  got:  " + got.observables[i] + "\n  want: " + want_obs[i]);
            }
        }
        if (got.stats.size() != want_stats.size())
        {
            problems.push_back("rms window count " + std::to_string(got.stats.size()) + " vs " + std::to_string(want_stats.size()));
        }
        for (size_t i = 0; i < llmin(got.stats.size(), want_stats.size()); ++i)
        {
            std::string why;
            if (!same_stat(got.stats[i], want_stats[i], why))
            {
                problems.push_back("wind " + why + "\n  got:  " + got.stats[i] + "\n  want: " + want_stats[i]);
            }
        }
        return problems;
    }

    void report(const std::vector<std::string>& problems)
    {
        size_t shown = 0;
        for (const std::string& p : problems)
        {
            if (++shown > 60) { std::cout << "PARITY: ... " << problems.size() - 60 << " more" << std::endl; break; }
            std::cout << "PARITY: " << p << std::endl;
        }
    }

    // ------------------------------------------------------------------
    // Timing

    void bench_out(const std::string& line)
    {
        std::cout << "VKS_AUDIO_BENCH " << line << std::endl;
        const std::string out = env("VKS_AUDIO_BENCH_OUT");
        if (!out.empty())
        {
            llofstream o(out.c_str(), std::ios::app);
            o << line << "\n";
        }
    }

    void bench_mixer(int blocks)
    {
        Script sounds;
        for (int i = 0; i < 16; ++i)
        {
            SoundDef def;
            def.name = "bench" + std::to_string(i);
            def.spec = {"tone", std::to_string(180 + 61 * i), "0.75", "0.4"};
            def.id.generate("vks-audio-parity/sound/" + def.name);
            sounds.sounds.push_back(def);
        }
        install_sounds(sounds);
        for (unsigned channels : CHANNEL_COUNTS)
        {
            double best_mix = 1e30, best_update = 1e30;
            for (int run = 0; run < 3; ++run)
            {
                FrameClock::set(1000.0);
                EngineInfo info;
                LLAudioEngine* engine = adapterCreateEngine(channels, RATE, info);
                ensure("bench engine", engine != nullptr);
                engine->setMasterGain(1.f);
                engine->setListener(LLVector3::zero, LLVector3::zero, LLVector3(0.f, 0.f, 1.f), LLVector3(1.f, 0.f, 0.f));
                EngineAccess::doCommit(engine);
                std::vector<TestSource*> srcs;
                std::vector<LLAudioChannel*> chans;
                for (int v = 0; v < 64; ++v)
                {
                    LLUUID id;
                    id.generate("vks-audio-parity/bench-source/" + std::to_string(v));
                    auto* src = new TestSource(id, 0.5f + 0.0078f * v, LLAudioEngine::AUDIO_TYPE_SFX);
                    src->setLoop(true);
                    const LLUUID& sid = sounds.sounds[v % 16].id;
                    LLAudioData* adp = engine->getAudioData(sid);
                    src->addAudioData(adp, true);
                    engine->updateBufferForData(adp, sid);
                    const double angle = v * 0.4;
                    const double radius = 3.0 + v * 0.5;
                    src->setPositionGlobal(LLVector3d(radius * cos(angle), radius * sin(angle), 1.0));
                    LLAudioChannel* ch = EngineAccess::doCreateChannel(engine);
                    ch->setSource(src);
                    ChannelAccess::doPlay(ch);
                    srcs.push_back(src);
                    chans.push_back(ch);
                }
                std::vector<float> out(1024 * channels);
                adapterRender(engine, out.data(), 1024); // warm-up
                double mix_s = 0.0, update_s = 0.0;
                for (int b = 0; b < blocks; ++b)
                {
                    const auto t0 = std::chrono::steady_clock::now();
                    for (int v = 0; v < 64; ++v)
                    {
                        const double angle = v * 0.4 + b * 0.02 * (1.0 + v % 3);
                        const double radius = 3.0 + v * 0.5;
                        srcs[v]->setPositionGlobal(LLVector3d(radius * cos(angle), radius * sin(angle), 1.0));
                        srcs[v]->setVelocity(LLVector3((F32)(-radius * sin(angle)), (F32)(radius * cos(angle)), 0.f));
                        ChannelAccess::doUpdate3DPosition(chans[v]);
                    }
                    EngineAccess::doCommit(engine);
                    const auto t1 = std::chrono::steady_clock::now();
                    adapterRender(engine, out.data(), 1024);
                    const auto t2 = std::chrono::steady_clock::now();
                    update_s += std::chrono::duration<double>(t1 - t0).count();
                    mix_s += std::chrono::duration<double>(t2 - t1).count();
                }
                ensure("64 voices held", adapterVoiceCount(engine) < 0 || adapterVoiceCount(engine) == 64);
                for (LLAudioChannel* ch : chans) delete ch;
                for (TestSource* src : srcs) delete src;
                adapterDestroyEngine(engine);
                best_mix = llmin(best_mix, mix_s / blocks);
                best_update = llmin(best_update, update_s / blocks);
            }
            bench_out("mix," + std::to_string(channels) + "ch,64 moving 3D voices,1024-frame block,us," + fmt("%.2f", best_mix * 1e6));
            bench_out("update," + std::to_string(channels) + "ch,64 voices update3DPosition+commit,per block,us," + fmt("%.2f", best_update * 1e6));
        }
    }

    void bench_decode()
    {
        const std::vector<std::string> inputs = decode_inputs();
        std::vector<std::string> clean;
        for (const std::string& rel : inputs)
        {
            if (rel.rfind("clean/", 0) == 0) clean.push_back(rel);
        }
        double best = 1e30, best_step = 1e30, best_oracle = 1e30;
        size_t bytes = 0;
        for (int run = 0; run < 3; ++run)
        {
            double ms = 0.0, step = 0.0;
            std::vector<DecodeCase> cases = run_decode_corpus(clean, &ms, &step);
            best = llmin(best, ms);
            best_step = llmin(best_step, step);
            bytes = 0;
            const auto t0 = std::chrono::steady_clock::now();
            for (const std::string& rel : clean)
            {
                std::vector<U8> asset, wav;
                read_file(path_join(path_join(FIXTURES, "decode/inputs"), rel), asset);
                oracleDecode(asset, wav);
                bytes += wav.size();
            }
            best_oracle = llmin(best_oracle, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        }
        bench_out("decode,viewer decode step (beginDecodingAndWritingAudio on the General pool)," + std::to_string(clean.size()) + " clean assets (" + std::to_string(bytes) + " WAV bytes),total ms," + fmt("%.2f", best_step));
        bench_out("decode,viewer path end to end (queue hops + LFS write + 0.2 ms completion polling)," + std::to_string(clean.size()) + " clean assets,total ms," + fmt("%.2f", best));
        bench_out("decode,in-memory libvorbisfile oracle," + std::to_string(clean.size()) + " clean assets,total ms," + fmt("%.2f", best_oracle));
    }
}

namespace tut
{
    struct llaudio_parity_data {};
    typedef test_group<llaudio_parity_data> llaudio_parity_group;
    typedef llaudio_parity_group::object llaudio_parity_object;
    tut::llaudio_parity_group llaudio_parity_instance("llaudio_parity");

    template<> template<>
    void llaudio_parity_object::test<1>()
    {
        set_test_name("decoded sound assets identical to the record");
        Runtime runtime;
        const std::vector<std::string> inputs = decode_inputs();
        ensure("decode inputs listed", !inputs.empty());
        std::vector<DecodeCase> cases = run_decode_corpus(inputs);
        const std::string record_path = path_join(FIXTURES, "llaudio_decode_parity.txt");

        // The in-process oracle must agree with the viewer path where it ran.
        std::vector<std::string> problems;
        for (const DecodeCase& c : cases)
        {
            const bool viewer_ok = c.viewer.began && c.viewer.finished && c.viewer.dsf_present;
            if (viewer_ok != c.oracle_ok || (viewer_ok && c.viewer.dsf != c.oracle_wav))
            {
                problems.push_back(c.rel + ": the oracle disagrees with the viewer path (" + std::to_string(viewer_ok) + " vs " + std::to_string(c.oracle_ok) + ")");
            }
        }

        const std::string record = env("VKS_AUDIO_RECORD");
        if (!record.empty())
        {
            report(problems);
            ensure("oracle mirrors the viewer path on the recording platform", problems.empty());
            make_dirs(path_join(record, "decode/expected"));
            std::vector<std::string> lines;
            size_t full = 0;
            for (const DecodeCase& c : cases)
            {
                lines.push_back(decode_line(c));
                if (c.viewer.dsf_present && c.viewer.dsf.size() <= 12000 && full < 6)
                {
                    ++full;
                    write_file(path_join(path_join(record, "decode/expected"), flat_name(c.rel)), c.viewer.dsf.data(), c.viewer.dsf.size());
                }
            }
            write_lines(path_join(record, "llaudio_decode_parity.txt"),
                        {"# INTEGRATION_TEST_llaudio_parity: decoded sound assets, recorded from the C++ decode path before the Rust port.",
                         "# <input under decode/inputs> in=<sha256 of the asset> began finished dsf asset data=<completed failed decoded> len=<.dsf bytes> sha256=<of the .dsf>",
                         std::string("platform ") + PLATFORM,
                         std::string("path ") + adapterDecodePath()},
                        lines);
            std::vector<DecodeCase> again = run_decode_corpus(inputs);
            for (size_t i = 0; i < cases.size(); ++i)
            {
                ensure_equals("decode reproduces its record: " + cases[i].rel, decode_line(again[i]), decode_line(cases[i]));
            }
            std::cout << "recorded " << cases.size() << " decode cases (" << full << " in full) to " << record << std::endl;
            return;
        }

        std::vector<std::string> want_all = read_lines(record_path);
        ensure("decode record present: " + record_path, !want_all.empty());
        std::string recorded_platform;
        std::vector<std::string> want;
        for (const std::string& l : want_all)
        {
            if (l.rfind("platform ", 0) == 0) recorded_platform = l.substr(9);
            else if (!l.empty() && l[0] != '#' && l.rfind("path ", 0) != 0) want.push_back(l);
        }
        ensure_equals("decode case count", cases.size(), want.size());
        // VKS_AUDIO_DECODE_REFERENCE=oracle checks the bytes against the
        // in-process oracle even on the recording platform.
        const bool same_platform = recorded_platform == PLATFORM && env("VKS_AUDIO_DECODE_REFERENCE") != "oracle";
        size_t by_record = 0, by_oracle = 0, ok = 0;
        for (size_t i = 0; i < cases.size(); ++i)
        {
            const DecodeCase& c = cases[i];
            const std::string got = decode_line(c);
            if (decode_outcome(got) != decode_outcome(want[i]))
            {
                problems.push_back("outcome\n  got:  " + got + "\n  want: " + want[i]);
                continue;
            }
            if (same_platform)
            {
                ++by_record;
                if (got != want[i]) problems.push_back("bytes (reference: record)\n  got:  " + got + "\n  want: " + want[i]);
                else ++ok;
            }
            else
            {
                ++by_oracle;
                if (c.viewer.dsf_present && c.viewer.dsf != c.oracle_wav) problems.push_back(c.rel + ": bytes differ from the in-process oracle");
                else ++ok;
            }
            // The complete outputs recorded for the smallest cases.
            std::vector<U8> full;
            if (same_platform && read_file(path_join(path_join(FIXTURES, "decode/expected"), flat_name(c.rel)), full) && full != c.viewer.dsf)
            {
                problems.push_back(c.rel + ": differs from decode/expected/" + flat_name(c.rel));
            }
        }
        report(problems);
        std::cout << "decode: " << cases.size() << " cases, " << ok << " identical (" << by_record << " against the "
                  << recorded_platform << " record, " << by_oracle << " against the in-process libvorbisfile oracle), "
                  << problems.size() << " problems" << std::endl;
        ensure("decoded sound assets identical", problems.empty());
    }

    template<> template<>
    void llaudio_parity_object::test<2>()
    {
        set_test_name("mixed scenes identical to the record");
        Runtime runtime;
        const std::string scenes_override = env("VKS_AUDIO_SCENES");
        const Script script = parse_script(scenes_override.empty() ? path_join(FIXTURES, "llaudio_parity_scenes.txt") : scenes_override);
        ensure("scenes in the script", !script.scenes.empty());
        install_sounds(script);
        MixRun got = run_scenes(script);

        const std::string record = env("VKS_AUDIO_RECORD");
        if (!record.empty())
        {
            make_dirs(record);
            for (const auto& r : got.records)
            {
                ensure("mix record written", write_mix(mix_path(record, r.first, !is_record_platform()), r.second));
            }
            std::vector<std::string> obs = got.observables;
            obs.insert(obs.end(), got.stats.begin(), got.stats.end());
            write_lines(path_join(record, "llaudio_mix_observables.txt"),
                        {"# INTEGRATION_TEST_llaudio_parity: per-step observables of llaudio_parity_scenes.txt, recorded from the C++ (SoLoud) engine before the Rust port.",
                         "# <scene> <channels> #<command index> <command> voices=<backend voices> engch=<engine channels allocated/playing> <source>=c<channel>p<playing>:<current>:<queued>:<priority> <direct channel>=p<playing>l<looped>",
                         "# <scene> <channels> rms:<segment> <frames> <per-channel RMS over the segment, mean of 6 runs> (noise scenes)",
                         std::string("platform ") + PLATFORM,
                         "engine " + got.records.begin()->second.engine},
                        obs);
            // The replay must reproduce itself before it can be a record.
            MixRun again = run_scenes(script);
            std::vector<std::string> summary;
            std::vector<std::string> problems = compare_mix(again, got.records, got.observables, got.stats, 0.0, summary);
            report(problems);
            ensure("the scenes reproduce their own recording", problems.empty());
            std::cout << "recorded " << script.scenes.size() << " scenes at 2, 6 and 8 channels to " << record << std::endl;
            return;
        }

        std::map<unsigned, MixRecord> want;
        for (unsigned channels : CHANNEL_COUNTS)
        {
            std::string path = mix_path(FIXTURES, channels, !is_record_platform());
            if (!is_record_platform() && !LLFile::isfile(path))
            {
                path = mix_path(FIXTURES, channels, false);
            }
            std::cout << "mix reference at " << channels << " channels: " << path << std::endl;
            ensure("mix record present: " + path, read_mix(path, want[channels]));
        }
        std::vector<std::string> want_obs, want_stats;
        for (const std::string& l : data_lines(path_join(FIXTURES, "llaudio_mix_observables.txt")))
        {
            if (l.rfind("platform ", 0) == 0 || l.rfind("engine ", 0) == 0) continue;
            const std::vector<std::string> t = split(l);
            (t.size() > 2 && t[2].rfind("rms:", 0) == 0 ? want_stats : want_obs).push_back(l);
        }
        const std::string tol = env("VKS_AUDIO_MIX_TOLERANCE");
        const double tolerance = tol.empty() ? 0.0 : atof(tol.c_str());
        std::vector<std::string> summary;
        std::vector<std::string> problems = compare_mix(got, want, want_obs, want_stats, tolerance, summary);
        for (const std::string& s : summary) LL_INFOS() << s << LL_ENDL;
        report(problems);
        size_t frames = 0, renders = 0;
        for (const auto& w : want) for (const auto& s : w.second.scenes) { frames += s.pcm.size() / w.first; ++renders; }
        std::cout << "mix: " << script.scenes.size() << " scenes, " << renders << " recorded renders at 2/6/8 channels, " << frames << " frames, "
                  << want_obs.size() << " observations, " << want_stats.size() << " rms windows; recorded with "
                  << want.begin()->second.engine << ", replayed with " << got.records.begin()->second.engine << "; "
                  << problems.size() << " problems" << std::endl;
        ensure("mixed scenes identical", problems.empty());
    }

    template<> template<>
    void llaudio_parity_object::test<3>()
    {
        set_test_name("mixer and decode timing");
        const std::string bench = env("VKS_AUDIO_BENCH");
        if (bench.empty())
        {
            return;
        }
        Runtime runtime;
        bench_mixer(llmax(1, atoi(bench.c_str())));
        bench_decode();
    }
}
