// SPDX-License-Identifier: GPL-3.0-or-later
//
// libdeflate vs zlib-ng for the viewer's whole-buffer inflate sites.
//
// Two builds of this file (build_bench.cmd):
//  - bench_viewer.exe links the viewer's own Release llcommon/llmath (LTO) and
//    the zlib-ng 2.2.5 package the viewer links. It reads mesh cache files,
//    proves byte identity and geometry identity with the viewer's own
//    LLUZipHelper::unzip_llsd, LLSDSerialize::fromBinary and
//    LLVolume::unpackVolumeFaces[Internal], and times the whole LOD pipeline.
//  - bench_plain.exe (-DBENCH_NO_VIEWER) needs only zlib-ng and libdeflate; it
//    runs the speed, latency, synthetic and fuzz passes from a block pack, and
//    is also built with /fsanitize=address for the fuzz pass.
//
// No owner data is compiled in or written outside the paths given.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

#ifndef BENCH_NO_VIEWER
#include "linden_common.h"
#include "llsd.h"
#include "llsdserialize.h"
#include "llvolume.h"
#include "llvolumemgr.h"
#include "lluuid.h"
#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/stream.hpp>
#include <sstream>
#endif

#include "zlib.h"        // zlib-ng 2.2.5 compat build (packages/include/zlib-ng)
#include "libdeflate.h"  // libdeflate 1.26

namespace fs = std::filesystem;
using clk = std::chrono::steady_clock;

static double ms_since(clk::time_point t0)
{
    return std::chrono::duration<double, std::milli>(clk::now() - t0).count();
}

// --------------------------------------------------------------------------
// Blocks and the pack file
// --------------------------------------------------------------------------
enum Kind : uint8_t { LOD0 = 0, LOD1, LOD2, LOD3, SKIN, CONVEX, PHYSMESH, NKINDS };
static const char* kind_name[NKINDS] = { "lowest_lod", "low_lod", "medium_lod", "high_lod", "skin", "physics_convex", "physics_mesh" };

struct Block
{
    uint8_t kind;
    std::vector<uint8_t> z;
};

static bool write_pack(const std::string& path, const std::vector<Block>& blocks)
{
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    uint32_t n = (uint32_t)blocks.size();
    fwrite("LDPK", 1, 4, f);
    fwrite(&n, 4, 1, f);
    for (const Block& b : blocks)
    {
        uint32_t len = (uint32_t)b.z.size();
        fwrite(&b.kind, 1, 1, f);
        fwrite(&len, 4, 1, f);
        fwrite(b.z.data(), 1, len, f);
    }
    fclose(f);
    return true;
}

static bool read_pack(const std::string& path, std::vector<Block>& blocks)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    char magic[4];
    uint32_t n = 0;
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "LDPK", 4) || fread(&n, 4, 1, f) != 1)
    {
        fclose(f);
        return false;
    }
    blocks.resize(n);
    for (uint32_t i = 0; i < n; ++i)
    {
        uint32_t len = 0;
        if (fread(&blocks[i].kind, 1, 1, f) != 1 || fread(&len, 4, 1, f) != 1) { fclose(f); return false; }
        blocks[i].z.resize(len);
        if (fread(blocks[i].z.data(), 1, len, f) != len) { fclose(f); return false; }
    }
    fclose(f);
    return true;
}

// --------------------------------------------------------------------------
// zlib-ng, exactly as LLUZipHelper::unzip_llsd inflates
// (indra/llcommon/llsdserialize.cpp:2249-2331): inflateInit, Z_NO_FLUSH into a
// thread_local 512 KiB chunk, realloc + memcpy per chunk, Z_STREAM_END needed.
// Returns 0 on success, else the ZR_* code the viewer would return.
// --------------------------------------------------------------------------
enum { ZR_OK = 0, ZR_MEM_ERROR, ZR_SIZE_ERROR, ZR_DATA_ERROR, ZR_PARSE_ERROR, ZR_BUFFER_ERROR, ZR_VERSION_ERROR };

static int zng_viewer_inflate(const uint8_t* in, int32_t size, uint8_t*& result, size_t& cur_size)
{
    result = nullptr;
    cur_size = 0;
    z_stream strm;
    constexpr uint32_t CHUNK = 1024 * 512;
    static thread_local std::unique_ptr<uint8_t[]> out;
    if (!out) out.reset(new (std::nothrow) uint8_t[CHUNK]);
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = size;
    strm.next_in = const_cast<uint8_t*>(in);
    int ret = inflateInit(&strm);
    do
    {
        strm.avail_out = CHUNK;
        strm.next_out = out.get();
        ret = inflate(&strm, Z_NO_FLUSH);
        switch (ret)
        {
        case Z_NEED_DICT:
        case Z_DATA_ERROR:
            inflateEnd(&strm); free(result); result = nullptr; return ZR_DATA_ERROR;
        case Z_STREAM_ERROR:
        case Z_BUF_ERROR:
            inflateEnd(&strm); free(result); result = nullptr; return ZR_BUFFER_ERROR;
        case Z_MEM_ERROR:
            inflateEnd(&strm); free(result); result = nullptr; return ZR_MEM_ERROR;
        }
        uint32_t have = CHUNK - strm.avail_out;
        uint8_t* nr = (uint8_t*)realloc(result, cur_size + have);
        if (!nr) { inflateEnd(&strm); free(result); result = nullptr; return ZR_MEM_ERROR; }
        result = nr;
        memcpy(result + cur_size, out.get(), have);
        cur_size += have;
    } while (ret == Z_OK);
    inflateEnd(&strm);
    if (ret != Z_STREAM_END) { free(result); result = nullptr; return ZR_DATA_ERROR; }
    return ZR_OK;
}

// --------------------------------------------------------------------------
// libdeflate strategies
// --------------------------------------------------------------------------
static constexpr size_t LD_CAP = 256u * 1024 * 1024;  // refuse larger outputs (bomb guard)

static libdeflate_decompressor* tls_decompressor()
{
    struct Holder
    {
        libdeflate_decompressor* d = libdeflate_alloc_decompressor();
        ~Holder() { libdeflate_free_decompressor(d); }
    };
    static thread_local Holder h;
    return h.d;
}

struct LdStats { uint64_t calls = 0, retries = 0; };

// S1 "ratio": malloc(in * k), double on LIBDEFLATE_INSUFFICIENT_SPACE.
static int ld_ratio(const uint8_t* in, size_t n, double k, std::vector<uint8_t>* keep, size_t& out_n, LdStats& st)
{
    size_t cap = std::max<size_t>(4096, (size_t)(n * k));
    for (;;)
    {
        uint8_t* buf = (uint8_t*)malloc(cap);
        if (!buf) return ZR_MEM_ERROR;
        libdeflate_result r = libdeflate_zlib_decompress(tls_decompressor(), in, n, buf, cap, &out_n);
        if (r == LIBDEFLATE_SUCCESS)
        {
            if (keep) keep->assign(buf, buf + out_n);
            free(buf);
            ++st.calls;
            return ZR_OK;
        }
        free(buf);
        if (r != LIBDEFLATE_INSUFFICIENT_SPACE) return ZR_DATA_ERROR;
        if (cap >= LD_CAP) return ZR_SIZE_ERROR;
        cap = std::min(LD_CAP, cap * 2);
        ++st.retries;
    }
}

// S2 "tls": a per-thread scratch buffer that only grows (start 512 KiB, the
// size of today's thread_local chunk), doubling on INSUFFICIENT_SPACE, and
// released back to 512 KiB after a call that needed more than 8 MiB.
// The caller parses straight out of the buffer: no per-call allocation.
struct TlsBuf
{
    std::unique_ptr<uint8_t[]> p;
    size_t cap = 0;
};
static thread_local TlsBuf tls_out;
static constexpr size_t TLS_START = 512 * 1024, TLS_KEEP = 8u * 1024 * 1024;

static int ld_tls(const uint8_t* in, size_t n, const uint8_t*& out, size_t& out_n, LdStats& st)
{
    if (!tls_out.p)
    {
        tls_out.p.reset(new (std::nothrow) uint8_t[TLS_START]);
        tls_out.cap = tls_out.p ? TLS_START : 0;
    }
    for (;;)
    {
        libdeflate_result r = libdeflate_zlib_decompress(tls_decompressor(), in, n, tls_out.p.get(), tls_out.cap, &out_n);
        if (r == LIBDEFLATE_SUCCESS) { ++st.calls; out = tls_out.p.get(); return ZR_OK; }
        if (r != LIBDEFLATE_INSUFFICIENT_SPACE) return ZR_DATA_ERROR;
        if (tls_out.cap >= LD_CAP) return ZR_SIZE_ERROR;
        size_t ncap = std::min(LD_CAP, std::max(tls_out.cap * 2, n * 8));
        std::unique_ptr<uint8_t[]> np(new (std::nothrow) uint8_t[ncap]);
        if (!np) return ZR_MEM_ERROR;
        tls_out.p = std::move(np);
        tls_out.cap = ncap;
        ++st.retries;
    }
}
static void ld_tls_release_if_big()
{
    if (tls_out.cap > TLS_KEEP)
    {
        tls_out.p.reset(new (std::nothrow) uint8_t[TLS_START]);
        tls_out.cap = tls_out.p ? TLS_START : 0;
    }
}

// S0 "exact": the output size is known (oracle; also the Milky Way map case).
static int ld_exact(const uint8_t* in, size_t n, uint8_t* out, size_t out_cap, size_t& out_n)
{
    libdeflate_result r = libdeflate_zlib_decompress(tls_decompressor(), in, n, out, out_cap, &out_n);
    return r == LIBDEFLATE_SUCCESS ? ZR_OK : ZR_DATA_ERROR;
}

// --------------------------------------------------------------------------
// Helpers
// --------------------------------------------------------------------------
static bool machine_busy(std::string* who = nullptr)
{
    // a build (cl.exe, link.exe, MSBuild) disturbs timing: refuse to time then
    static const char* names[] = { "cl.exe", "link.exe", "MSBuild.exe", "msbuild.exe", "cmake.exe", "ninja.exe" };
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    bool busy = false;
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe))
    {
        char nm[260];
        WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1, nm, sizeof(nm), nullptr, nullptr);
        for (const char* n : names)
        {
            if (_stricmp(nm, n) == 0) { busy = true; if (who) *who = nm; }
        }
    }
    CloseHandle(snap);
    return busy;
}

static double pct(std::vector<double> v, double p)
{
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    size_t i = (size_t)std::min<double>((double)(v.size() - 1), p * (double)(v.size() - 1) + 0.5);
    return v[i];
}

static size_t ref_out_size(const Block& b)
{
    uint8_t* r = nullptr;
    size_t n = 0;
    if (zng_viewer_inflate(b.z.data(), (int32_t)b.z.size(), r, n) != ZR_OK) return 0;
    free(r);
    return n;
}

// --------------------------------------------------------------------------
// Viewer-linked parts: pack, verify, pipeline
// --------------------------------------------------------------------------
#ifndef BENCH_NO_VIEWER
static constexpr S32 UNZIP_LLSD_MAX_DEPTH = 96;   // llsdserialize.cpp:58 (file-static there)

// LLVolume::unpackVolumeFacesInternal is private. An explicit instantiation may
// name a private member ([temp.explicit]); MSVC mangles access into the symbol,
// so "#define private public" would not link.
template <typename Tag, typename Tag::type M>
struct Rob { friend typename Tag::type get_ptr(Tag) { return M; } };
struct UnpackTag { using type = bool (LLVolume::*)(const LLSD&); friend type get_ptr(UnpackTag); };
template struct Rob<UnpackTag, &LLVolume::unpackVolumeFacesInternal>;

// The libdeflate twin of LLUZipHelper::unzip_llsd: same header strip, same
// parser call and depth limit, reading straight from the scratch buffer.
static int ld_unzip_llsd(LLSD& data, const uint8_t* in, S32 size, LdStats& st)
{
    const uint8_t* out = nullptr;
    size_t n = 0;
    int r = ld_tls(in, (size_t)size, out, n, st);
    if (r != ZR_OK) return r;
    llssize cur = (llssize)n;
    char* p = strip_deprecated_header((char*)out, cur);
    boost::iostreams::stream<boost::iostreams::array_source> istrm(p, cur);
    int res = LLSDSerialize::fromBinary(data, istrm, cur, UNZIP_LLSD_MAX_DEPTH) ? ZR_OK : ZR_PARSE_ERROR;
    ld_tls_release_if_big();
    return res;
}

static LLVolumeParams mesh_params(const LLUUID& id)
{
    LLVolumeParams p;
    p.setType(LL_PCODE_PROFILE_SQUARE, LL_PCODE_PATH_LINE);
    p.setSculptID(id, LL_SCULPT_TYPE_MESH);
    return p;
}

static F32 lod_detail(uint8_t kind)
{
    // lodReceived: LLVolumeLODGroup::getVolumeScaleFromDetail(lod);
    // physicsShapeReceived: detail 0
    return kind <= LOD3 ? LLVolumeLODGroup::getVolumeScaleFromDetail(kind) : 0.f;
}

static bool same_mem(const void* a, const void* b, size_t n)
{
    if (!n) return true;
    if (!a || !b) return a == b;
    return memcmp(a, b, n) == 0;
}

// Bit-for-bit comparison of everything unpackVolumeFacesInternal + cacheOptimize
// leave in the faces.
static std::string compare_volumes(LLVolume& a, LLVolume& b)
{
    if (a.getNumVolumeFaces() != b.getNumVolumeFaces()) return "face count";
    for (S32 i = 0; i < a.getNumVolumeFaces(); ++i)
    {
        const LLVolumeFace& fa = a.getVolumeFace(i);
        const LLVolumeFace& fb = b.getVolumeFace(i);
        if (fa.mNumVertices != fb.mNumVertices || fa.mNumIndices != fb.mNumIndices) return "counts";
        size_t nv = (size_t)fa.mNumVertices, ni = (size_t)fa.mNumIndices;
        if (!same_mem(fa.mPositions, fb.mPositions, nv * sizeof(LLVector4a))) return "positions";
        if (!same_mem(fa.mNormals, fb.mNormals, nv * sizeof(LLVector4a))) return "normals";
        if ((fa.mTangents == nullptr) != (fb.mTangents == nullptr) || !same_mem(fa.mTangents, fb.mTangents, nv * sizeof(LLVector4a))) return "tangents";
        if (!same_mem(fa.mTexCoords, fb.mTexCoords, nv * sizeof(LLVector2))) return "texcoords";
        if (!same_mem(fa.mIndices, fb.mIndices, ni * sizeof(U16))) return "indices";
        if ((fa.mWeights == nullptr) != (fb.mWeights == nullptr) || !same_mem(fa.mWeights, fb.mWeights, nv * sizeof(LLVector4a))) return "weights";
        if (!same_mem(fa.mExtents, fb.mExtents, 2 * sizeof(LLVector4a))) return "extents";
        if (!same_mem(fa.mTexCoordExtents, fb.mTexCoordExtents, sizeof(fa.mTexCoordExtents))) return "tc extents";
        if (!same_mem(&fa.mNormalizedScale, &fb.mNormalizedScale, sizeof(LLVector3))) return "normalized scale";
        if (fa.mOptimized != fb.mOptimized) return "optimized flag";
    }
    return "";
}

static std::string llsd_bytes(const LLSD& sd)
{
    std::ostringstream os;
    LLSDSerialize::toBinary(sd, os);
    return os.str();
}

// pack: read mesh cache files the way the mesh repository does and collect
// every block the cache flags say is present.
static int cmd_pack(const std::string& corpus, const std::string& pack)
{
    std::vector<Block> blocks;
    uint64_t files = 0, bad_header = 0, deprecated = 0, absent = 0, zero = 0, outside = 0, z_in = 0;
    uint64_t per_kind[NKINDS] = {}, absent_k[NKINDS] = {}, zero_k[NKINDS] = {}, listed_k[NKINDS] = {};
    for (const auto& e : fs::directory_iterator(corpus))
    {
        if (!e.is_regular_file()) continue;
        std::ifstream f(e.path(), std::ios::binary);
        std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), {});
        if (d.size() < 13) continue;
        ++files;
        U32 version, hsize, flags;
        memcpy(&version, &d[0], 4); memcpy(&hsize, &d[4], 4); memcpy(&flags, &d[8], 4);
        if (version != 1) { ++bad_header; continue; }
        llssize dsize = (llssize)d.size() - 12;
        char* base = (char*)&d[12];
        llssize hdr_extra = 0;
        char* p = strip_deprecated_header(base, dsize, &hdr_extra);
        if (hdr_extra) ++deprecated;
        boost::iostreams::stream<boost::iostreams::array_source> stream(p, dsize);
        LLSD header;
        if (!LLSDSerialize::fromBinary(header, stream, dsize) || !header.isMap()) { ++bad_header; continue; }
        S32 header_size = (S32)stream.tellg();   // header.mHeaderSize (llmeshrepository.cpp:2349)
        static const char* keys[NKINDS] = { "lowest_lod", "low_lod", "medium_lod", "high_lod", "skin", "physics_convex", "physics_mesh" };
        // LLMeshHeader::getFlags: LOD i is bit i; FLAG_SKIN etc. follow LLModel::NUM_LODS (5, LOD_PHYSICS included)
        static const U32 flag_bit[NKINDS] = { 1u << 0, 1u << 1, 1u << 2, 1u << 3, 1u << 5, 1u << 6, 1u << 7 };
        for (int k = 0; k < NKINDS; ++k)
        {
            if (!header.has(keys[k])) continue;
            S32 off = header[keys[k]]["offset"].asInteger();
            S32 size = header[keys[k]]["size"].asInteger();
            if (off < 0 || size <= 0) continue;
            ++listed_k[k];
            if (!(flags & flag_bit[k])) { ++absent; ++absent_k[k]; continue; }   // not in cache (LLMeshHeader::setFromFlags)
            size_t disk = 12 + (size_t)header_size + (size_t)off;  // CACHE_PREAMBLE_SIZE + mHeaderSize + offset
            if (d.size() < disk + (size_t)size) { ++outside; continue; }
            bool all_zero = true;                                  // the 1 KiB zero test (llmeshrepository.cpp:2163)
            for (S32 i = 0; i < std::min<S32>(size, 1024) && all_zero; ++i) all_zero = d[disk + i] == 0;
            if (all_zero) { ++zero; ++zero_k[k]; continue; }
            Block b;
            b.kind = (uint8_t)k;
            b.z.assign(d.begin() + disk, d.begin() + disk + size);
            z_in += size;
            ++per_kind[k];
            blocks.push_back(std::move(b));
        }
    }
    printf("files %llu, bad header %llu, deprecated header %llu\n", files, bad_header, deprecated);
    printf("blocks %zu (%.1f MB compressed); flagged absent %llu, beyond file %llu, zero %llu\n",
           blocks.size(), z_in / 1e6, absent, outside, zero);
    for (int k = 0; k < NKINDS; ++k)
        printf("  %-15s kept %llu (listed in header %llu, flagged absent %llu, zero %llu)\n",
               kind_name[k], per_kind[k], listed_k[k], absent_k[k], zero_k[k]);
    return write_pack(pack, blocks) ? 0 : 1;
}

// verify: byte identity, Adler-32 enforcement and decoded-data identity for
// every block in the pack.
static int cmd_verify(const std::string& pack, size_t limit)
{
    std::vector<Block> blocks;
    if (!read_pack(pack, blocks)) { printf("cannot read pack\n"); return 1; }
    if (limit && blocks.size() > limit) blocks.resize(limit);
    LdStats st;
    uint64_t both_ok = 0, both_fail = 0, ident = 0, differ = 0, only_z = 0, only_ld = 0;
    uint64_t adler_rejected_both = 0, adler_accepted = 0;
    uint64_t geo_blocks = 0, geo_ident = 0, geo_differ = 0, geo_both_fail = 0, geo_one_fail = 0, faces = 0, verts = 0, tris = 0;
    uint64_t llsd_blocks = 0, llsd_ident = 0, llsd_differ = 0;
    uint64_t determinism_checked = 0, determinism_bad = 0;
    uint64_t out_total = 0, in_total = 0;
    std::map<std::string, uint64_t> geo_reasons;
    std::vector<double> ratios;
    LLUUID id;
    id.generate();
    for (size_t bi = 0; bi < blocks.size(); ++bi)
    {
        const Block& b = blocks[bi];
        uint8_t* zr = nullptr;
        size_t zn = 0;
        int zres = zng_viewer_inflate(b.z.data(), (int32_t)b.z.size(), zr, zn);
        const uint8_t* lo = nullptr;
        size_t ln = 0;
        int lres = ld_tls(b.z.data(), b.z.size(), lo, ln, st);
        if (zres == ZR_OK && lres == ZR_OK)
        {
            ++both_ok;
            out_total += zn;
            in_total += b.z.size();
            ratios.push_back((double)zn / (double)b.z.size());
            if (zn == ln && memcmp(zr, lo, zn) == 0) ++ident;
            else { ++differ; printf("BYTES DIFFER block %zu kind %s\n", bi, kind_name[b.kind]); }
        }
        else if (zres != ZR_OK && lres != ZR_OK) ++both_fail;
        else if (zres == ZR_OK) { ++only_z; printf("only zlib-ng accepts block %zu\n", bi); }
        else { ++only_ld; printf("only libdeflate accepts block %zu\n", bi); }
        free(zr);
        ld_tls_release_if_big();

        // Adler-32: flip one bit in the trailer; both must refuse
        if (zres == ZR_OK && b.z.size() >= 6)
        {
            std::vector<uint8_t> m = b.z;
            m[m.size() - 1 - (bi % 4)] ^= (uint8_t)(1u << (bi % 8));
            uint8_t* r2 = nullptr;
            size_t n2 = 0;
            int z2 = zng_viewer_inflate(m.data(), (int32_t)m.size(), r2, n2);
            free(r2);
            const uint8_t* o2 = nullptr;
            size_t ln2 = 0;
            int l2 = ld_tls(m.data(), m.size(), o2, ln2, st);
            if (z2 != ZR_OK && l2 != ZR_OK) ++adler_rejected_both;
            else { ++adler_accepted; printf("ADLER NOT ENFORCED block %zu zlib %d libdeflate %d\n", bi, z2, l2); }
        }

        // one level up: what the viewer builds from the block
        if (b.kind <= LOD3 || b.kind == PHYSMESH)
        {
            ++geo_blocks;
            LLVolumeParams vp = mesh_params(id);
            LLPointer<LLVolume> va = new LLVolume(vp, lod_detail(b.kind));
            LLPointer<LLVolume> vb = new LLVolume(vp, lod_detail(b.kind));
            // A: the viewer's own path (unzip_llsd with zlib-ng, parse, unpack, cacheOptimize)
            bool ok_a = va->unpackVolumeFaces(const_cast<U8*>(b.z.data()), (S32)b.z.size());
            // B: libdeflate, then the same parse and the same private unpack
            LLSD mdl;
            bool ok_b = ld_unzip_llsd(mdl, b.z.data(), (S32)b.z.size(), st) == ZR_OK
                        && ((*vb).*get_ptr(UnpackTag{}))(mdl);
            if (ok_a && ok_b)
            {
                std::string why = compare_volumes(*va, *vb);
                if (why.empty())
                {
                    ++geo_ident;
                    faces += va->getNumVolumeFaces();
                    for (S32 i = 0; i < va->getNumVolumeFaces(); ++i)
                    {
                        verts += va->getVolumeFace(i).mNumVertices;
                        tris += va->getVolumeFace(i).mNumIndices / 3;
                    }
                }
                else { ++geo_differ; ++geo_reasons[why]; printf("GEOMETRY DIFFERS block %zu: %s\n", bi, why.c_str()); }
                // determinism control: the viewer path against itself
                if (determinism_checked < 500)
                {
                    LLPointer<LLVolume> vc = new LLVolume(vp, lod_detail(b.kind));
                    vc->unpackVolumeFaces(const_cast<U8*>(b.z.data()), (S32)b.z.size());
                    ++determinism_checked;
                    if (!compare_volumes(*va, *vc).empty()) ++determinism_bad;
                }
            }
            else if (!ok_a && !ok_b) ++geo_both_fail;
            else { ++geo_one_fail; printf("GEOMETRY ONE-SIDED FAIL block %zu a=%d b=%d\n", bi, ok_a, ok_b); }
        }
        else
        {
            // skin and physics_convex: the LLSD the constructors receive
            ++llsd_blocks;
            LLSD sa, sb;
            int ra = LLUZipHelper::unzip_llsd(sa, b.z.data(), (S32)b.z.size());
            int rb = ld_unzip_llsd(sb, b.z.data(), (S32)b.z.size(), st);
            if (ra == rb && (ra != ZR_OK || llsd_bytes(sa) == llsd_bytes(sb))) ++llsd_ident;
            else { ++llsd_differ; printf("LLSD DIFFERS block %zu (%d vs %d)\n", bi, ra, rb); }
        }
        if ((bi + 1) % 5000 == 0) { printf("  ... %zu blocks\n", bi + 1); fflush(stdout); }
    }
    printf("\n== verify (%zu blocks) ==\n", blocks.size());
    printf("inflate: both accept %llu (identical %llu, differ %llu), both refuse %llu, only zlib-ng %llu, only libdeflate %llu\n",
           both_ok, ident, differ, both_fail, only_z, only_ld);
    printf("compressed %.1f MB -> inflated %.1f MB; ratio median %.2f p99 %.2f max %.2f\n",
           in_total / 1e6, out_total / 1e6, pct(ratios, 0.5), pct(ratios, 0.99), pct(ratios, 1.0));
    printf("adler-32 one-bit trailer flip: refused by both %llu, accepted by either %llu\n", adler_rejected_both, adler_accepted);
    printf("geometry (LOD + physics_mesh) %llu: identical %llu, differ %llu, both fail %llu, one fails %llu; %llu faces %llu vertices %llu triangles\n",
           geo_blocks, geo_ident, geo_differ, geo_both_fail, geo_one_fail, faces, verts, tris);
    for (auto& kv : geo_reasons) printf("  differ reason %s: %llu\n", kv.first.c_str(), kv.second);
    printf("viewer path vs itself (determinism control): %llu checked, %llu differ\n", determinism_checked, determinism_bad);
    printf("LLSD (skin + physics_convex) %llu: identical %llu, differ %llu\n", llsd_blocks, llsd_ident, llsd_differ);
    printf("libdeflate scratch retries %llu over %llu calls\n", st.retries, st.calls);
    return (differ || only_z || only_ld || adler_accepted || geo_differ || geo_one_fail || llsd_differ) ? 3 : 0;
}

// pipeline: the whole LOD unpack (inflate + parse + unpack + cacheOptimize),
// viewer path vs libdeflate path, single thread.
static int cmd_pipeline(const std::string& pack, int reps)
{
    std::vector<Block> blocks;
    if (!read_pack(pack, blocks)) return 1;
    std::vector<const Block*> lods, llsds;
    for (const Block& b : blocks)
        (b.kind <= LOD3 ? lods : llsds).push_back(&b);
    LLUUID id;
    id.generate();
    LLVolumeParams vp = mesh_params(id);
    LdStats st;
    std::string who;
    for (int rep = 0; rep < reps; ++rep)
    {
        while (machine_busy(&who)) { printf("waiting: %s running\n", who.c_str()); fflush(stdout); Sleep(30000); }
        double t_inflate_z = 0, t_inflate_ld = 0, t_full_a = 0, t_full_b = 0, t_llsd_a = 0, t_llsd_b = 0;
        for (const Block* b : lods)
        {
            uint8_t* r = nullptr; size_t n = 0;
            auto t0 = clk::now();
            zng_viewer_inflate(b->z.data(), (int32_t)b->z.size(), r, n);
            t_inflate_z += ms_since(t0);
            free(r);
            const uint8_t* o = nullptr; size_t ln = 0;
            t0 = clk::now();
            ld_tls(b->z.data(), b->z.size(), o, ln, st);
            t_inflate_ld += ms_since(t0);
            ld_tls_release_if_big();
            {
                t0 = clk::now();
                LLPointer<LLVolume> v = new LLVolume(vp, lod_detail(b->kind));
                v->unpackVolumeFaces(const_cast<U8*>(b->z.data()), (S32)b->z.size());
                v = nullptr;
                t_full_a += ms_since(t0);
            }
            {
                t0 = clk::now();
                LLPointer<LLVolume> v = new LLVolume(vp, lod_detail(b->kind));
                LLSD mdl;
                if (ld_unzip_llsd(mdl, b->z.data(), (S32)b->z.size(), st) == ZR_OK)
                    ((*v).*get_ptr(UnpackTag{}))(mdl);
                v = nullptr;
                t_full_b += ms_since(t0);
            }
        }
        for (const Block* b : llsds)
        {
            LLSD a, c;
            auto t0 = clk::now();
            LLUZipHelper::unzip_llsd(a, b->z.data(), (S32)b->z.size());
            t_llsd_a += ms_since(t0);
            t0 = clk::now();
            ld_unzip_llsd(c, b->z.data(), (S32)b->z.size(), st);
            t_llsd_b += ms_since(t0);
        }
        printf("rep %d: LOD blocks %zu: inflate zlib-ng %.0f ms, libdeflate %.0f ms; full lodReceived unpack viewer %.0f ms, libdeflate %.0f ms\n",
               rep, lods.size(), t_inflate_z, t_inflate_ld, t_full_a, t_full_b);
        printf("rep %d: skin+physics blocks %zu: unzip_llsd viewer %.0f ms, libdeflate twin %.0f ms\n", rep, llsds.size(), t_llsd_a, t_llsd_b);
        if (machine_busy(&who)) printf("  DISTURBED: %s started during this rep\n", who.c_str());
        fflush(stdout);
    }
    return 0;
}
#endif  // BENCH_NO_VIEWER

// --------------------------------------------------------------------------
// speed: inflate only, all strategies, 1..N threads, per-block latency
// --------------------------------------------------------------------------
struct Strategy { const char* name; int id; double k; };
static const Strategy strategies[] = {
    { "zlib-ng viewer loop", 0, 0 },
    { "libdeflate exact size (oracle)", 1, 0 },
    { "libdeflate ratio x4, grow x2", 2, 4.0 },
    { "libdeflate ratio x8, grow x2", 2, 8.0 },
    { "libdeflate thread scratch", 3, 0 },
};

static double run_one(const Strategy& s, const Block& b, size_t ref_n, std::vector<uint8_t>& exact_buf, LdStats& st)
{
    auto t0 = clk::now();
    switch (s.id)
    {
    case 0: { uint8_t* r = nullptr; size_t n = 0; zng_viewer_inflate(b.z.data(), (int32_t)b.z.size(), r, n); free(r); break; }
    case 1: { size_t n = 0; ld_exact(b.z.data(), b.z.size(), exact_buf.data(), ref_n, n); break; }
    case 2: { size_t n = 0; ld_ratio(b.z.data(), b.z.size(), s.k, nullptr, n, st); break; }
    case 3: { const uint8_t* o = nullptr; size_t n = 0; ld_tls(b.z.data(), b.z.size(), o, n, st); ld_tls_release_if_big(); break; }
    }
    return std::chrono::duration<double, std::micro>(clk::now() - t0).count();
}

static int cmd_speed(const std::string& pack, int max_threads, int reps)
{
    std::vector<Block> blocks;
    if (!read_pack(pack, blocks)) return 1;
    std::vector<size_t> ref(blocks.size());
    size_t max_ref = 0;
    uint64_t in_bytes = 0, out_bytes = 0;
    for (size_t i = 0; i < blocks.size(); ++i)
    {
        ref[i] = ref_out_size(blocks[i]);
        max_ref = std::max(max_ref, ref[i]);
        in_bytes += blocks[i].z.size();
        out_bytes += ref[i];
    }
    printf("blocks %zu, %.1f MB in, %.1f MB out, largest output %.2f MB\n", blocks.size(), in_bytes / 1e6, out_bytes / 1e6, max_ref / 1e6);

    // per-block latency, single thread, best of reps per block (last byte in -> bytes out)
    std::string who;
    while (machine_busy(&who)) { printf("waiting: %s running\n", who.c_str()); fflush(stdout); Sleep(30000); }
    std::vector<size_t> order(blocks.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return ref[a] < ref[b]; });
    size_t median_i = order[order.size() / 2], p99_i = order[(size_t)(order.size() * 0.99)], max_i = order.back();
    printf("\n== latency per block, single thread, best of %d (microseconds) ==\n", reps);
    printf("block                         in KB   out KB");
    for (const Strategy& s : strategies) printf(" | %s", s.name);
    printf("\n");
    for (auto [label, idx] : { std::pair<const char*, size_t>{ "median output size", median_i }, { "p99 output size", p99_i }, { "largest output", max_i } })
    {
        printf("%-26s %8.1f %8.1f", label, blocks[idx].z.size() / 1024.0, ref[idx] / 1024.0);
        for (const Strategy& s : strategies)
        {
            std::vector<uint8_t> ex(ref[idx] + 64);
            double best = 1e30;
            for (int r = 0; r < std::max(reps, 5); ++r)
            {
                LdStats st;
                // cold thread scratch for the first run, warm afterwards
                best = std::min(best, run_one(s, blocks[idx], ref[idx], ex, st));
            }
            printf(" | %10.1f", best);
        }
        printf("%s\n", machine_busy(&who) ? "  DISTURBED" : "");
    }

    // whole-corpus distribution per strategy, single thread
    printf("\n== whole corpus, single thread: per-block us (median / p99 / max) and total ==\n");
    std::vector<uint8_t> exact_buf(max_ref + 64);
    for (const Strategy& s : strategies)
    {
        while (machine_busy(&who)) { printf("waiting: %s running\n", who.c_str()); fflush(stdout); Sleep(30000); }
        std::vector<double> best_total;
        std::vector<double> lat(blocks.size(), 1e30);
        LdStats st;
        for (int r = 0; r < reps; ++r)
        {
            auto t0 = clk::now();
            for (size_t i = 0; i < blocks.size(); ++i)
                lat[i] = std::min(lat[i], run_one(s, blocks[i], ref[i], exact_buf, st));
            best_total.push_back(ms_since(t0));
        }
        double tot = pct(best_total, 0.5);
        printf("%-32s median %7.1f  p99 %8.1f  max %9.1f us | total %8.0f ms (median of %d) | %7.0f MB/s out | retries %llu / %llu calls\n",
               s.name, pct(lat, 0.5), pct(lat, 0.99), pct(lat, 1.0), tot, reps, out_bytes / 1e3 / tot, st.retries, st.calls);
        if (machine_busy(&who)) printf("  DISTURBED: %s started during this run\n", who.c_str());
        fflush(stdout);
    }

    // threads: the mesh pool is 2 (llmeshrepository.cpp:984) plus the repo thread
    printf("\n== threads: wall time for the corpus, static interleave ==\n");
    for (int t = 1; t <= max_threads; ++t)
    {
        for (const Strategy& s : strategies)
        {
            if (s.id == 1 || (s.id == 2 && s.k == 8.0)) continue;
            while (machine_busy(&who)) { printf("waiting: %s running\n", who.c_str()); fflush(stdout); Sleep(30000); }
            std::vector<double> walls;
            for (int r = 0; r < reps; ++r)
            {
                std::atomic<int> ready{ 0 };
                std::atomic<bool> go{ false };
                std::vector<std::thread> th;
                for (int w = 0; w < t; ++w)
                {
                    th.emplace_back([&, w] {
                        std::vector<uint8_t> ex;
                        LdStats st;
                        ready++;
                        while (!go.load()) {}
                        for (size_t i = w; i < blocks.size(); i += t) run_one(s, blocks[i], ref[i], ex, st);
                    });
                }
                while (ready.load() < t) {}
                auto t0 = clk::now();
                go = true;
                for (auto& x : th) x.join();
                walls.push_back(ms_since(t0));
            }
            double w = pct(walls, 0.5);
            printf("threads %d  %-32s %8.0f ms  %7.0f MB/s out\n", t, s.name, w, out_bytes / 1e3 / w);
            if (machine_busy(&who)) printf("  DISTURBED: %s started during this run\n", who.c_str());
            fflush(stdout);
        }
    }

    // memory: per-call heap for zlib-ng (hooked zalloc) and libdeflate's decompressor
    {
        static std::atomic<size_t> cur{ 0 }, peak{ 0 };
        auto za = [](voidpf, uInt items, uInt size) -> voidpf {
            size_t n = (size_t)items * size;
            void* p = malloc(n + 16);
            *(size_t*)p = n;
            size_t c = cur += n;
            size_t pk = peak.load();
            while (c > pk && !peak.compare_exchange_weak(pk, c)) {}
            return (char*)p + 16;
        };
        auto zf = [](voidpf, voidpf p) { if (p) { cur -= *(size_t*)((char*)p - 16); free((char*)p - 16); } };
        z_stream strm{};
        strm.zalloc = za;
        strm.zfree = zf;
        std::vector<uint8_t> out(1 << 20);
        strm.next_in = blocks[median_i].z.data();
        strm.avail_in = (uInt)blocks[median_i].z.size();
        inflateInit(&strm);
        strm.next_out = out.data();
        strm.avail_out = (uInt)out.size();
        inflate(&strm, Z_NO_FLUSH);
        inflateEnd(&strm);
        printf("\nzlib-ng inflate state per call (zalloc peak): %zu bytes\n", peak.load());
        static std::atomic<size_t> ld_bytes{ 0 };
        libdeflate_options opt{};
        opt.sizeof_options = sizeof(opt);
        opt.malloc_func = [](size_t n) -> void* { ld_bytes += n; return malloc(n); };
        opt.free_func = [](void* p) { free(p); };
        libdeflate_decompressor* d = libdeflate_alloc_decompressor_ex(&opt);
        printf("libdeflate decompressor (allocated once per thread): %zu bytes\n", ld_bytes.load());
        libdeflate_free_decompressor(d);
    }
    PROCESS_MEMORY_COUNTERS pmc{};
    GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
    printf("process peak working set %.1f MB (includes the pack in memory)\n", pmc.PeakWorkingSetSize / 1e6);
    return 0;
}

// --------------------------------------------------------------------------
// synthetic: 4 / 8 / 16 MiB bodies, zlib (mesh-style) and gzip (HTTP-style)
// --------------------------------------------------------------------------
// llcorehttp's inflate_reply (_httplibcurl.cpp:132-213): 16 KiB input reads
// from the BufferArray, 16 KiB output appended to a new BufferArray (64 KiB
// blocks there; a vector of blocks here).
static size_t zng_http_reply(const std::vector<uint8_t>& body, std::vector<std::unique_ptr<uint8_t[]>>& blocks_out)
{
    z_stream z{};
    inflateInit2(&z, MAX_WBITS + 32);
    size_t pos = 0, total = 0, blk_used = 65536;
    uint8_t inbuf[16384], outbuf[16384];
    int ret = Z_OK;
    while (ret != Z_STREAM_END)
    {
        if (z.avail_in == 0)
        {
            size_t n = std::min<size_t>(sizeof(inbuf), body.size() - pos);
            if (!n) break;
            memcpy(inbuf, &body[pos], n);
            pos += n;
            z.next_in = inbuf;
            z.avail_in = (uInt)n;
        }
        z.next_out = outbuf;
        z.avail_out = sizeof(outbuf);
        ret = inflate(&z, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) break;
        size_t produced = sizeof(outbuf) - z.avail_out;
        size_t off = 0;
        while (off < produced)
        {
            if (blk_used == 65536) { blocks_out.emplace_back(new uint8_t[65536]); blk_used = 0; }
            size_t c = std::min(produced - off, 65536 - blk_used);
            memcpy(blocks_out.back().get() + blk_used, outbuf + off, c);
            blk_used += c; off += c;
        }
        total += produced;
    }
    inflateEnd(&z);
    return total;
}

static std::vector<uint8_t> zng_compress(const std::vector<uint8_t>& raw, int window_bits)
{
    z_stream z{};
    deflateInit2(&z, Z_DEFAULT_COMPRESSION, Z_DEFLATED, window_bits, 8, Z_DEFAULT_STRATEGY);
    std::vector<uint8_t> out(deflateBound(&z, (uLong)raw.size()));
    z.next_in = const_cast<uint8_t*>(raw.data());
    z.avail_in = (uInt)raw.size();
    z.next_out = out.data();
    z.avail_out = (uInt)out.size();
    deflate(&z, Z_FINISH);
    out.resize(z.total_out);
    deflateEnd(&z);
    return out;
}

static int cmd_synthetic(const std::string& pack, int reps)
{
    std::vector<Block> blocks;
    if (!read_pack(pack, blocks)) return 1;
    // raw material: real inflated mesh payloads, concatenated
    std::vector<uint8_t> raw;
    for (const Block& b : blocks)
    {
        uint8_t* r = nullptr; size_t n = 0;
        if (zng_viewer_inflate(b.z.data(), (int32_t)b.z.size(), r, n) == ZR_OK) raw.insert(raw.end(), r, r + n);
        free(r);
        if (raw.size() > 16u * 1024 * 1024) break;
    }
    std::string who;
    printf("== synthetic bodies from real mesh payload (best of %d, ms) ==\n", reps);
    printf("size  | zlib: viewer loop | ld thread scratch, cold | ld scratch, warm | ld exact || gzip HTTP: llcorehttp 16 KiB loop | ld flatten + ISIZE hint\n");
    for (size_t mb : { 4, 8, 16 })
    {
        std::vector<uint8_t> src(raw.begin(), raw.begin() + mb * 1024 * 1024);
        std::vector<uint8_t> zz = zng_compress(src, MAX_WBITS);
        std::vector<uint8_t> gz = zng_compress(src, MAX_WBITS + 16);
        Block zb{ LOD3, zz };
        double best[6] = { 1e30, 1e30, 1e30, 1e30, 1e30, 1e30 };
        std::vector<uint8_t> ex(src.size() + 64);
        for (int r = 0; r < reps; ++r)
        {
            while (machine_busy(&who)) { printf("waiting: %s running\n", who.c_str()); fflush(stdout); Sleep(30000); }
            LdStats st;
            best[0] = std::min(best[0], run_one(strategies[0], zb, src.size(), ex, st) / 1000);
            // cold: a fresh thread, so the scratch starts at 512 KiB and must grow
            double cold = 0;
            std::thread([&] { cold = run_one(strategies[4], zb, src.size(), ex, st) / 1000; }).join();
            best[1] = std::min(best[1], cold);
            {
                // warm: scratch already big enough (the 8 MiB release rule is skipped)
                const uint8_t* o = nullptr; size_t n = 0;
                ld_tls(zz.data(), zz.size(), o, n, st);
                auto t0 = clk::now();
                ld_tls(zz.data(), zz.size(), o, n, st);
                best[2] = std::min(best[2], ms_since(t0));
                ld_tls_release_if_big();
            }
            best[3] = std::min(best[3], run_one(strategies[1], zb, src.size(), ex, st) / 1000);
            {
                std::vector<std::unique_ptr<uint8_t[]>> outb;
                auto t0 = clk::now();
                size_t n = zng_http_reply(gz, outb);
                best[4] = std::min(best[4], ms_since(t0));
                if (n != src.size()) printf("http loop size mismatch\n");
            }
            {
                auto t0 = clk::now();
                std::vector<uint8_t> flat(gz.begin(), gz.end());  // BufferArray is chunked: one copy
                uint32_t isize;
                memcpy(&isize, &flat[flat.size() - 4], 4);
                std::vector<uint8_t> out(isize);
                size_t n = 0;
                libdeflate_gzip_decompress(tls_decompressor(), flat.data(), flat.size(), out.data(), out.size(), &n);
                best[5] = std::min(best[5], ms_since(t0));
                if (n != src.size()) printf("gzip size mismatch\n");
            }
        }
        printf("%2zu MiB | %8.2f | %8.2f | %8.2f | %8.2f || %8.2f | %8.2f   (zlib %.2f MB, gzip %.2f MB)\n",
               mb, best[0], best[1], best[2], best[3], best[4], best[5], zz.size() / 1e6, gz.size() / 1e6);
        if (machine_busy(&who)) printf("  DISTURBED: %s started during this run\n", who.c_str());
        fflush(stdout);
    }
    return 0;
}

// --------------------------------------------------------------------------
// gzfile: gunzip_file (llsys.cpp:1368) vs whole-file libdeflate, decode only
// --------------------------------------------------------------------------
static int cmd_gzfile(const std::vector<std::string>& files, int reps)
{
    std::string who;
    for (const std::string& path : files)
    {
        std::ifstream f(path, std::ios::binary);
        std::vector<uint8_t> gz((std::istreambuf_iterator<char>(f)), {});
        double best_a = 1e30, best_b = 1e30;
        std::vector<uint8_t> a_out, b_out;
        for (int r = 0; r < reps; ++r)
        {
            while (machine_busy(&who)) { printf("waiting: %s running\n", who.c_str()); fflush(stdout); Sleep(30000); }
            a_out.clear();
            auto t0 = clk::now();
            std::wstring wp(path.begin(), path.end());
            gzFile src = gzopen_w(wp.c_str(), "rb");
            uint8_t buf[32768];
            int n;
            while ((n = gzread(src, buf, sizeof(buf))) > 0) a_out.insert(a_out.end(), buf, buf + n);
            gzclose(src);
            best_a = std::min(best_a, ms_since(t0));

            t0 = clk::now();
            std::ifstream g(path, std::ios::binary);
            std::vector<uint8_t> in((std::istreambuf_iterator<char>(g)), {});
            uint32_t isize;
            memcpy(&isize, &in[in.size() - 4], 4);
            b_out.assign(isize, 0);
            size_t got = 0;
            libdeflate_result res = libdeflate_gzip_decompress(tls_decompressor(), in.data(), in.size(), b_out.data(), b_out.size(), &got);
            b_out.resize(res == LIBDEFLATE_SUCCESS ? got : 0);
            best_b = std::min(best_b, ms_since(t0));
        }
        printf("%s: %.2f MB gz -> %.2f MB | gzread 32 KiB loop %.1f ms | read whole + libdeflate %.1f ms | identical %s\n",
               fs::path(path).filename().string().substr(0, 8).c_str(), gz.size() / 1e6, a_out.size() / 1e6, best_a, best_b,
               a_out == b_out ? "yes" : "NO");
        if (machine_busy(&who)) printf("  DISTURBED: %s started during this run\n", who.c_str());
    }
    return 0;
}

// --------------------------------------------------------------------------
// fuzz: truncation, bit flips, header and trailer edits, trailing data
// --------------------------------------------------------------------------
static int cmd_fuzz(const std::string& pack, uint32_t seed, int per_block, size_t stride)
{
    std::vector<Block> blocks;
    if (!read_pack(pack, blocks)) return 1;
    std::mt19937_64 rng(seed);
    const char* cls_name[] = { "truncate random", "truncate 1-4 bytes", "1 bit flip", "8 bit flips", "trailer bit flip",
                               "header CINFO/FDICT edit", "trailing garbage", "byte run zeroed" };
    constexpr int NCLS = 8;
    uint64_t n[NCLS] = {}, both_rej[NCLS] = {}, both_acc_same[NCLS] = {}, both_acc_diff[NCLS] = {}, only_z[NCLS] = {}, only_ld[NCLS] = {};
    LdStats st;
    size_t tested = 0;
    for (size_t bi = 0; bi < blocks.size(); bi += stride)
    {
        const std::vector<uint8_t>& z = blocks[bi].z;
        if (z.size() < 8) continue;
        ++tested;
        for (int m = 0; m < per_block; ++m)
        {
            int c = m % NCLS;
            std::vector<uint8_t> v = z;
            switch (c)
            {
            case 0: v.resize(rng() % v.size()); break;
            case 1: v.resize(v.size() - 1 - rng() % 4); break;
            case 2: v[rng() % v.size()] ^= (uint8_t)(1u << (rng() % 8)); break;
            case 3: for (int k = 0; k < 8; ++k) v[rng() % v.size()] ^= (uint8_t)(1u << (rng() % 8)); break;
            case 4: v[v.size() - 1 - rng() % 4] ^= (uint8_t)(1u << (rng() % 8)); break;
            case 5:
            {
                // keep FCHECK valid so the edit reaches the window/dictionary checks
                uint8_t cinfo = (uint8_t)(rng() % 16), fdict = (uint8_t)(rng() % 2);
                v[0] = (uint8_t)((cinfo << 4) | 8);
                uint8_t flg = (uint8_t)((v[1] & 0xC0) | (fdict << 5));
                flg = (uint8_t)(flg + (31 - ((v[0] * 256u + flg) % 31)) % 31);
                v[1] = flg;
                break;
            }
            case 6: { size_t k = 1 + rng() % 64; for (size_t j = 0; j < k; ++j) v.push_back((uint8_t)rng()); break; }
            case 7: { size_t s = rng() % v.size(), k = std::min<size_t>(v.size() - s, 1 + rng() % 32); memset(&v[s], 0, k); break; }
            }
            ++n[c];
            uint8_t* zr = nullptr; size_t zn = 0;
            int zres = zng_viewer_inflate(v.data(), (int32_t)v.size(), zr, zn);
            const uint8_t* lo = nullptr; size_t ln = 0;
            int lres = v.empty() ? ZR_DATA_ERROR : ld_tls(v.data(), v.size(), lo, ln, st);
            if (zres != ZR_OK && lres != ZR_OK) ++both_rej[c];
            else if (zres == ZR_OK && lres == ZR_OK)
            {
                if (zn == ln && memcmp(zr, lo, zn) == 0) ++both_acc_same[c];
                else { ++both_acc_diff[c]; printf("ACCEPTED, DIFFERENT OUTPUT: block %zu class %s\n", bi, cls_name[c]); }
            }
            else if (zres == ZR_OK) { ++only_z[c]; if (only_z[c] <= 5) printf("only zlib-ng accepts: block %zu class %s (hdr %02x %02x)\n", bi, cls_name[c], v[0], v[1]); }
            else { ++only_ld[c]; if (only_ld[c] <= 5) printf("only libdeflate accepts: block %zu class %s (hdr %02x %02x)\n", bi, cls_name[c], v[0], v[1]); }
            free(zr);
            ld_tls_release_if_big();
        }
    }
    printf("\n== fuzz: %zu blocks, %d mutations each, seed %u ==\n", tested, per_block, seed);
    printf("%-26s %9s %11s %14s %14s %12s %14s\n", "class", "cases", "both refuse", "both accept =", "both accept !=", "only zlib-ng", "only libdeflate");
    for (int c = 0; c < NCLS; ++c)
        printf("%-26s %9llu %11llu %14llu %14llu %12llu %14llu\n", cls_name[c], n[c], both_rej[c], both_acc_same[c], both_acc_diff[c], only_z[c], only_ld[c]);
    return 0;
}

// flipscan: every single-bit flip of one block; lists each disagreement and
// whether the accepted output equals the unmodified block's output.
static int cmd_flipscan(const std::string& pack, size_t index)
{
    std::vector<Block> blocks;
    if (!read_pack(pack, blocks) || index >= blocks.size()) return 1;
    const std::vector<uint8_t>& z = blocks[index].z;
    uint8_t* orig = nullptr;
    size_t orig_n = 0;
    zng_viewer_inflate(z.data(), (int32_t)z.size(), orig, orig_n);
    LdStats st;
    uint64_t total = 0, agree = 0, only_ld = 0, only_z = 0, ld_same_as_orig = 0;
    for (size_t byte = 0; byte < z.size(); ++byte)
    {
        for (int bit = 0; bit < 8; ++bit)
        {
            std::vector<uint8_t> v = z;
            v[byte] ^= (uint8_t)(1u << bit);
            ++total;
            uint8_t* zr = nullptr; size_t zn = 0;
            int zres = zng_viewer_inflate(v.data(), (int32_t)v.size(), zr, zn);
            const uint8_t* lo = nullptr; size_t ln = 0;
            int lres = ld_tls(v.data(), v.size(), lo, ln, st);
            bool same = lres == ZR_OK && ln == orig_n && memcmp(lo, orig, orig_n) == 0;
            if ((zres == ZR_OK) == (lres == ZR_OK)) ++agree;
            else if (lres == ZR_OK)
            {
                ++only_ld;
                if (same) ++ld_same_as_orig;
                printf("only libdeflate accepts: byte %zu of %zu bit %d (zlib-ng code %d); output %s the original\n",
                       byte, z.size(), bit, zres, same ? "equals" : "DIFFERS FROM");
            }
            else { ++only_z; printf("only zlib-ng accepts: byte %zu bit %d\n", byte, bit); }
            free(zr);
            ld_tls_release_if_big();
        }
    }
    printf("block %zu (%s, %zu bytes): %llu flips, agree %llu, only libdeflate %llu (output equals original %llu), only zlib-ng %llu\n",
           index, kind_name[blocks[index].kind], z.size(), total, agree, only_ld, ld_same_as_orig, only_z);
    free(orig);
    return 0;
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        printf("usage: %s pack|verify|pipeline|speed|synthetic|gzfile|fuzz ...\n", argv[0]);
        return 2;
    }
    std::string cmd = argv[1];
#ifndef BENCH_NO_VIEWER
    if (cmd == "pack" && argc >= 4) return cmd_pack(argv[2], argv[3]);
    if (cmd == "verify" && argc >= 3) return cmd_verify(argv[2], argc >= 4 ? (size_t)atoll(argv[3]) : 0);
    if (cmd == "pipeline" && argc >= 3) return cmd_pipeline(argv[2], argc >= 4 ? atoi(argv[3]) : 3);
#endif
    if (cmd == "speed" && argc >= 3) return cmd_speed(argv[2], argc >= 4 ? atoi(argv[3]) : 3, argc >= 5 ? atoi(argv[4]) : 5);
    if (cmd == "synthetic" && argc >= 3) return cmd_synthetic(argv[2], argc >= 4 ? atoi(argv[3]) : 7);
    if (cmd == "gzfile" && argc >= 3)
    {
        std::vector<std::string> files(argv + 2, argv + argc);
        return cmd_gzfile(files, 5);
    }
    if (cmd == "flipscan" && argc >= 4) return cmd_flipscan(argv[2], (size_t)atoll(argv[3]));
    if (cmd == "fuzz" && argc >= 3)
        return cmd_fuzz(argv[2], argc >= 4 ? (uint32_t)atoi(argv[3]) : 1, argc >= 5 ? atoi(argv[4]) : 16, argc >= 6 ? (size_t)atoll(argv[5]) : 1);
    printf("bad arguments\n");
    return 2;
}
