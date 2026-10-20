/**
 * @file llvolume_test.cpp
 * @brief The parametric prim mesher against recorded fixtures (vkstorm-rust).
 *
 * The fixture corpus (tests/fixtures/llvolume_corpus.vksv) was recorded from
 * the original C++ mesher. Test 1 rebuilds every case through LLVolume and
 * compares profile, path, mesh, faces and tangents bit for bit. The same
 * program records the corpus (VKS_VOLUME_DUMP=<file>) and times it
 * (VKS_VOLUME_BENCH=<iterations>, VKS_VOLUME_BENCH_OUT=<csv>).
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#include "linden_common.h"
#include "lltut.h"

#include "llvolume.h"
#include "llquaternion.h"
#include "m3math.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#ifndef VKS_VOLUME_FIXTURE_DIR
#error VKS_VOLUME_FIXTURE_DIR must name the directory of the recorded corpora
#endif

// One corpus per platform toolchain, recorded from the C++ mesher built by
// that toolchain (libm and std::lerp differ between them, as they did for
// the C++): every block must match it exactly. A platform without a
// recording compares against the Windows corpus with a tolerance on the
// float arrays and exact integer data.
#if defined(_MSC_VER)
#define VKS_VOLUME_FIXTURE VKS_VOLUME_FIXTURE_DIR "/llvolume_corpus.vksv"
#define VKS_VOLUME_STRICT_PLATFORM true
#elif defined(LL_LINUX)
#define VKS_VOLUME_FIXTURE VKS_VOLUME_FIXTURE_DIR "/llvolume_corpus.linux.vksv"
#define VKS_VOLUME_STRICT_PLATFORM true
#else
#define VKS_VOLUME_FIXTURE VKS_VOLUME_FIXTURE_DIR "/llvolume_corpus.vksv"
#define VKS_VOLUME_STRICT_PLATFORM false
#endif

namespace
{
    constexpr U32 FIXTURE_MAGIC = 0x56534B56; // "VKSV"
    constexpr U32 FIXTURE_VERSION = 2;

    constexpr U32 FLAG_FULL = 1;      // array contents stored, not only hashes
    constexpr U32 FLAG_FLEX = 2;      // flexible path filled with the recorded points
    constexpr U32 FLAG_SCULPT = 4;    // sculpted with the recorded map
    constexpr U32 FLAG_TANGENTS = 8;  // tangents generated and recorded

    constexpr S32 RSQRT_SAMPLE_COUNT = 32;
    const F32 RSQRT_SAMPLES[RSQRT_SAMPLE_COUNT] = {
        1.f, 2.f, 3.f, 0.5f, 0.25f, 0.1f, 10.f, 100.f,
        1e-3f, 1e3f, 7.77f, 0.333f, 12345.678f, 1.5e-5f, 3.3e6f, 0.98765f,
        0.0625f, 0.75f, 1.0001f, 0.9999f, 42.f, 0.01234f, 5.5e-2f, 8.125f,
        1.7e-8f, 2.5e9f, 0.6f, 0.7f, 0.8f, 0.9f, 1.1f, 1.2f };

    const F32 LOD_DETAILS[4] = { 1.f, 1.5f, 2.5f, 4.f };

    //------------------------------------------------------------------
    // byte stream helpers
    //------------------------------------------------------------------
    struct Writer
    {
        std::vector<U8> b;
        void raw(const void* p, size_t n) { const U8* s = (const U8*)p; b.insert(b.end(), s, s + n); }
        void u8(U8 v) { b.push_back(v); }
        void u16(U16 v) { raw(&v, 2); }
        void u32(U32 v) { raw(&v, 4); }
        void i32(S32 v) { raw(&v, 4); }
        void u64(U64 v) { raw(&v, 8); }
        void f32(F32 v) { raw(&v, 4); }
        void str(const std::string& s) { u16((U16)s.size()); raw(s.data(), s.size()); }
    };

    struct Reader
    {
        const U8* p;
        const U8* end;
        bool ok;
        Reader(const std::vector<U8>& v) : p(v.data()), end(v.data() + v.size()), ok(true) {}
        bool take(void* dst, size_t n)
        {
            if ((size_t)(end - p) < n) { ok = false; memset(dst, 0, n); return false; }
            memcpy(dst, p, n); p += n; return true;
        }
        U8 u8() { U8 v; take(&v, 1); return v; }
        U16 u16() { U16 v; take(&v, 2); return v; }
        U32 u32() { U32 v; take(&v, 4); return v; }
        S32 i32() { S32 v; take(&v, 4); return v; }
        U64 u64() { U64 v; take(&v, 8); return v; }
        F32 f32() { F32 v; take(&v, 4); return v; }
        std::string str() { U16 n = u16(); std::string s(n, '\0'); take(s.data(), n); return s; }
        std::vector<U8> bytes(size_t n) { std::vector<U8> v(n); take(v.data(), n); return v; }
    };

    U64 fnv1a(const void* data, size_t len)
    {
        const U8* p = (const U8*)data;
        U64 h = 1469598103934665603ULL;
        for (size_t i = 0; i < len; ++i)
        {
            h ^= p[i];
            h *= 1099511628211ULL;
        }
        return h;
    }

    void rsqrtSignature(U32 out[RSQRT_SAMPLE_COUNT])
    {
        for (S32 i = 0; i < RSQRT_SAMPLE_COUNT; ++i)
        {
            LLVector4a v;
            v.splat(RSQRT_SAMPLES[i]);
            LLVector4a r;
            r = _mm_rsqrt_ps(v);
            memcpy(&out[i], r.getF32ptr(), 4);
        }
    }

    //------------------------------------------------------------------
    // the corpus
    //------------------------------------------------------------------
    struct CaseSpec
    {
        std::string name;
        LLVolumeParams params;
        F32 detail = 1.f;
        U32 flags = 0;
        U16 sculpt_w = 0;
        U16 sculpt_h = 0;
    };

    struct Shape
    {
        const char* name;
        U8 profile;
        U8 path;
    };

    const Shape SHAPES[] = {
        { "box",                LL_PCODE_PROFILE_SQUARE,      LL_PCODE_PATH_LINE },
        { "cylinder",           LL_PCODE_PROFILE_CIRCLE,      LL_PCODE_PATH_LINE },
        { "prism",              LL_PCODE_PROFILE_ISOTRI,      LL_PCODE_PATH_LINE },
        { "sphere",             LL_PCODE_PROFILE_CIRCLE_HALF, LL_PCODE_PATH_CIRCLE },
        { "torus",              LL_PCODE_PROFILE_CIRCLE,      LL_PCODE_PATH_CIRCLE },
        { "tube",               LL_PCODE_PROFILE_SQUARE,      LL_PCODE_PATH_CIRCLE },
        { "ring",               LL_PCODE_PROFILE_EQUALTRI,    LL_PCODE_PATH_CIRCLE },
        { "equaltri_line",      LL_PCODE_PROFILE_EQUALTRI,    LL_PCODE_PATH_LINE },
        { "righttri_line",      LL_PCODE_PROFILE_RIGHTTRI,    LL_PCODE_PATH_LINE },
        { "halfcircle_line",    LL_PCODE_PROFILE_CIRCLE_HALF, LL_PCODE_PATH_LINE },
        { "isotri_circle",      LL_PCODE_PROFILE_ISOTRI,      LL_PCODE_PATH_CIRCLE },
        { "righttri_circle",    LL_PCODE_PROFILE_RIGHTTRI,    LL_PCODE_PATH_CIRCLE },
        { "square_circle2",     LL_PCODE_PROFILE_SQUARE,      LL_PCODE_PATH_CIRCLE2 },
        { "circle_circle2",     LL_PCODE_PROFILE_CIRCLE,      LL_PCODE_PATH_CIRCLE2 },
        { "halfcircle_circle2", LL_PCODE_PROFILE_CIRCLE_HALF, LL_PCODE_PATH_CIRCLE2 },
        { "circle_test",        LL_PCODE_PROFILE_CIRCLE,      LL_PCODE_PATH_TEST },
    };
    constexpr S32 NUM_SHAPES = sizeof(SHAPES) / sizeof(SHAPES[0]);
    constexpr S32 NUM_UI_SHAPES = 7; // the first seven are the build-tool shapes

    bool isCirclePath(const LLVolumeParams& p)
    {
        return p.getPathParams().getCurveType() == LL_PCODE_PATH_CIRCLE &&
               (p.getProfileParams().getCurveType() & LL_PCODE_PROFILE_MASK) != LL_PCODE_PROFILE_CIRCLE_HALF;
    }

    LLVolumeParams makeParams(U8 profile, U8 hole, U8 path)
    {
        LLVolumeParams p;
        p.setType(profile | hole, path);
        p.setBeginAndEndS(0.f, 1.f);
        p.setBeginAndEndT(0.f, 1.f);
        p.setHollow(0.f);
        if (isCirclePath(p))
        {
            p.setRatio(1.f, 0.25f);
        }
        else
        {
            p.setRatio(1.f, 1.f);
        }
        p.setShear(0.f, 0.f);
        p.setTwistBegin(0.f);
        p.setTwistEnd(0.f);
        p.setTaper(0.f, 0.f);
        p.setRevolutions(1.f);
        p.setRadiusOffset(0.f);
        p.setSkew(0.f);
        return p;
    }

    void setHole(LLVolumeParams& p, U8 hole)
    {
        U8 profile = p.getProfileParams().getCurveType() & LL_PCODE_PROFILE_MASK;
        p.setType(profile | hole, p.getPathParams().getCurveType());
    }

    struct Variation
    {
        const char* name;
        std::function<void(LLVolumeParams&)> apply;
    };

    // Setter order matters (skew depends on revolutions and ratio, radius
    // offset on hole size and taper); every variation applies them in the
    // same order the build tool does.
    const Variation VARIATIONS[] = {
        { "hollow30",             [](LLVolumeParams& p) { p.setHollow(0.30f); } },
        { "hollow50_circlehole",  [](LLVolumeParams& p) { setHole(p, LL_PCODE_HOLE_CIRCLE); p.setHollow(0.50f); } },
        { "hollow50_squarehole",  [](LLVolumeParams& p) { setHole(p, LL_PCODE_HOLE_SQUARE); p.setHollow(0.50f); } },
        { "hollow50_trihole",     [](LLVolumeParams& p) { setHole(p, LL_PCODE_HOLE_TRIANGLE); p.setHollow(0.50f); } },
        { "hollow95",             [](LLVolumeParams& p) { p.setHollow(0.95f); } },
        { "hollow70_squarehole",  [](LLVolumeParams& p) { setHole(p, LL_PCODE_HOLE_SQUARE); p.setHollow(0.70f); } },
        { "cutS_25_75",           [](LLVolumeParams& p) { p.setBeginAndEndS(0.25f, 0.75f); } },
        { "cutS_0_50",            [](LLVolumeParams& p) { p.setBeginAndEndS(0.f, 0.5f); } },
        { "cutS_60_100",          [](LLVolumeParams& p) { p.setBeginAndEndS(0.6f, 1.f); } },
        { "cutS_125_875",         [](LLVolumeParams& p) { p.setBeginAndEndS(0.125f, 0.875f); } },
        { "cutS_30_35",           [](LLVolumeParams& p) { p.setBeginAndEndS(0.30f, 0.35f); } },
        { "cutT_20_80",           [](LLVolumeParams& p) { p.setBeginAndEndT(0.2f, 0.8f); } },
        { "cutT_0_50",            [](LLVolumeParams& p) { p.setBeginAndEndT(0.f, 0.5f); } },
        { "cutT_10_90",           [](LLVolumeParams& p) { p.setBeginAndEndT(0.1f, 0.9f); } },
        { "twist_0_50",           [](LLVolumeParams& p) { p.setTwistBegin(0.f); p.setTwistEnd(0.5f); } },
        { "twist_m50_50",         [](LLVolumeParams& p) { p.setTwistBegin(-0.5f); p.setTwistEnd(0.5f); } },
        { "twist_100_m100",       [](LLVolumeParams& p) { p.setTwistBegin(1.f); p.setTwistEnd(-1.f); } },
        { "twist_25_25",          [](LLVolumeParams& p) { p.setTwistBegin(0.25f); p.setTwistEnd(0.25f); } },
        { "taper_50_50",          [](LLVolumeParams& p) { p.setTaper(0.5f, 0.5f); } },
        { "taper_m50_25",         [](LLVolumeParams& p) { p.setTaper(-0.5f, 0.25f); } },
        { "taper_100_100",        [](LLVolumeParams& p) { p.setTaper(1.f, 1.f); } },
        { "taper_90_m90",         [](LLVolumeParams& p) { p.setTaper(0.9f, -0.9f); } },
        { "shear_50_50",          [](LLVolumeParams& p) { p.setShear(0.5f, 0.5f); } },
        { "shear_m25_50",         [](LLVolumeParams& p) { p.setShear(-0.25f, 0.5f); } },
        { "radius_50",            [](LLVolumeParams& p) { p.setRadiusOffset(0.5f); } },
        { "radius_m50",           [](LLVolumeParams& p) { p.setRadiusOffset(-0.5f); } },
        { "radius_90",            [](LLVolumeParams& p) { p.setRadiusOffset(0.9f); } },
        { "rev_150",              [](LLVolumeParams& p) { p.setRevolutions(1.5f); } },
        { "rev_225",              [](LLVolumeParams& p) { p.setRevolutions(2.25f); } },
        { "rev_400",              [](LLVolumeParams& p) { p.setRevolutions(4.f); } },
        { "skew_50",              [](LLVolumeParams& p) { p.setSkew(0.5f); } },
        { "skew_m95",             [](LLVolumeParams& p) { p.setSkew(-0.95f); } },
        { "skew_25",              [](LLVolumeParams& p) { p.setSkew(0.25f); } },
        { "ratio_50_50",          [](LLVolumeParams& p) { p.setRatio(0.5f, 0.5f); } },
        { "ratio_150_75",         [](LLVolumeParams& p) { p.setRatio(1.5f, 0.75f); } },
        { "ratio_200_200",        [](LLVolumeParams& p) { p.setRatio(2.f, 2.f); } },
        { "ratio_25_10",          [](LLVolumeParams& p) { p.setRatio(0.25f, 0.10f); } },
        { "ratio_0_0",            [](LLVolumeParams& p) { p.setRatio(0.f, 0.f); } },
        { "hollow_cutS",          [](LLVolumeParams& p) { p.setBeginAndEndS(0.25f, 0.75f); p.setHollow(0.5f); } },
        { "hollow_cutT",          [](LLVolumeParams& p) { p.setBeginAndEndT(0.25f, 0.75f); p.setHollow(0.5f); } },
        { "hollow_cut_twist",     [](LLVolumeParams& p) { p.setBeginAndEndS(0.2f, 0.9f); p.setHollow(0.4f); p.setTwistBegin(0.3f); p.setTwistEnd(-0.3f); } },
        { "taper_twist_shear",    [](LLVolumeParams& p) { p.setTaper(0.3f, -0.2f); p.setTwistBegin(0.f); p.setTwistEnd(1.f); p.setShear(0.2f, -0.1f); } },
        { "everything",           [](LLVolumeParams& p) {
            setHole(p, LL_PCODE_HOLE_SQUARE);
            p.setBeginAndEndS(0.1f, 0.8f);
            p.setBeginAndEndT(0.1f, 0.9f);
            p.setHollow(0.25f);
            p.setRatio(0.5f, 0.2f);
            p.setTaper(0.2f, 0.3f);
            p.setRevolutions(2.f);
            p.setRadiusOffset(0.3f);
            p.setSkew(0.2f);
            p.setTwistBegin(-0.25f);
            p.setTwistEnd(0.5f);
            p.setShear(0.1f, 0.1f); } },
    };
    constexpr S32 NUM_VARIATIONS = sizeof(VARIATIONS) / sizeof(VARIATIONS[0]);

    struct Rng
    {
        U64 s;
        explicit Rng(U64 seed) : s(seed) {}
        U64 next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
        F32 uniform() { return (F32)((next() >> 40) & 0xFFFFFF) / (F32)0x1000000; }
        F32 range(F32 lo, F32 hi) { return lo + (hi - lo) * uniform(); }
        F32 q(F32 v) { return floorf(v * 100.f + 0.5f) / 100.f; } // the build tool's 0.01 steps
        bool coin(F32 p) { return uniform() < p; }
        S32 pick(S32 n) { return (S32)(next() % (U64)n); }
    };

    std::string lodName(F32 detail)
    {
        std::ostringstream s;
        s << "lod" << detail;
        return s.str();
    }

    std::vector<CaseSpec> buildCorpus()
    {
        std::vector<CaseSpec> cases;
        auto add = [&](const std::string& name, const LLVolumeParams& p, F32 detail, U32 flags = 0, U16 sw = 0, U16 sh = 0)
        {
            CaseSpec c;
            c.name = name;
            c.params = p;
            c.detail = detail;
            c.flags = flags;
            c.sculpt_w = sw;
            c.sculpt_h = sh;
            cases.push_back(c);
        };

        // Every shape at every LOD the viewer requests.
        for (S32 s = 0; s < NUM_SHAPES; ++s)
        {
            for (S32 l = 0; l < 4; ++l)
            {
                add(std::string(SHAPES[s].name) + "/base/" + lodName(LOD_DETAILS[l]),
                    makeParams(SHAPES[s].profile, 0, SHAPES[s].path), LOD_DETAILS[l]);
            }
        }
        // Mesh assets run the mesher at detail 0 with box parameters.
        add("box/base/lod0", makeParams(LL_PCODE_PROFILE_SQUARE, 0, LL_PCODE_PATH_LINE), 0.f);

        // One parameter at a time on the build-tool shapes, lowest and highest LOD.
        for (S32 s = 0; s < NUM_UI_SHAPES; ++s)
        {
            for (S32 v = 0; v < NUM_VARIATIONS; ++v)
            {
                for (F32 detail : { 1.f, 4.f })
                {
                    LLVolumeParams p = makeParams(SHAPES[s].profile, 0, SHAPES[s].path);
                    VARIATIONS[v].apply(p);
                    add(std::string(SHAPES[s].name) + "/" + VARIATIONS[v].name + "/" + lodName(detail), p, detail);
                }
            }
        }
        // Everything at once on every shape at the middle LOD.
        for (S32 s = 0; s < NUM_SHAPES; ++s)
        {
            LLVolumeParams p = makeParams(SHAPES[s].profile, 0, SHAPES[s].path);
            VARIATIONS[NUM_VARIATIONS - 1].apply(p);
            add(std::string(SHAPES[s].name) + "/everything/lod2.5", p, 2.5f);
        }

        // Seeded random combinations.
        Rng rng(0x5EEDC0DEULL);
        for (S32 i = 0; i < 160; ++i)
        {
            const Shape& sh = SHAPES[rng.pick(NUM_SHAPES)];
            U8 hole = (U8)(rng.pick(4) << 4);
            LLVolumeParams p = makeParams(sh.profile, hole, sh.path);
            if (rng.coin(0.6f))
            {
                F32 b = rng.q(rng.range(0.f, 0.8f));
                F32 e = rng.q(rng.range(b + 0.05f, 1.f));
                p.setBeginAndEndS(b, e);
            }
            if (rng.coin(0.5f))
            {
                F32 b = rng.q(rng.range(0.f, 0.8f));
                F32 e = rng.q(rng.range(b + 0.05f, 1.f));
                p.setBeginAndEndT(b, e);
            }
            if (rng.coin(0.5f)) p.setHollow(rng.q(rng.range(0.05f, 0.95f)));
            if (rng.coin(0.5f))
            {
                if (isCirclePath(p)) p.setRatio(rng.q(rng.range(0.05f, 1.f)), rng.q(rng.range(0.05f, 0.5f)));
                else p.setRatio(rng.q(rng.range(0.f, 2.f)), rng.q(rng.range(0.f, 2.f)));
            }
            if (rng.coin(0.5f)) p.setTaper(rng.q(rng.range(-1.f, 1.f)), rng.q(rng.range(-1.f, 1.f)));
            if (rng.coin(0.4f)) p.setRevolutions(rng.q(rng.range(1.f, 4.f)));
            if (rng.coin(0.4f)) p.setRadiusOffset(rng.q(rng.range(-1.f, 1.f)));
            if (rng.coin(0.4f)) p.setSkew(rng.q(rng.range(-0.95f, 0.95f)));
            if (rng.coin(0.5f)) { p.setTwistBegin(rng.q(rng.range(-1.f, 1.f))); p.setTwistEnd(rng.q(rng.range(-1.f, 1.f))); }
            if (rng.coin(0.4f)) p.setShear(rng.q(rng.range(-0.5f, 0.5f)), rng.q(rng.range(-0.5f, 0.5f)));
            F32 detail = LOD_DETAILS[rng.pick(4)];
            std::ostringstream name;
            name << sh.name << "/random" << i << "/" << lodName(detail);
            add(name.str(), p, detail);
        }

        // Flexible paths: the path points come from the simulation, recorded here.
        for (F32 detail : LOD_DETAILS)
        {
            add("box_flex/base/" + lodName(detail), makeParams(LL_PCODE_PROFILE_SQUARE, 0, LL_PCODE_PATH_FLEXIBLE), detail, FLAG_FLEX);
            add("cylinder_flex/base/" + lodName(detail), makeParams(LL_PCODE_PROFILE_CIRCLE, 0, LL_PCODE_PATH_FLEXIBLE), detail, FLAG_FLEX);
        }
        for (F32 detail : { 1.f, 4.f })
        {
            LLVolumeParams p = makeParams(LL_PCODE_PROFILE_SQUARE, LL_PCODE_HOLE_CIRCLE, LL_PCODE_PATH_FLEXIBLE);
            p.setHollow(0.4f);
            p.setBeginAndEndS(0.2f, 0.8f);
            p.setTwistBegin(0.1f);
            p.setTwistEnd(-0.3f);
            p.setTaper(0.3f, -0.2f);
            add("box_flex/hollow_cut_twist_taper/" + lodName(detail), p, detail, FLAG_FLEX);
            LLVolumeParams q = makeParams(LL_PCODE_PROFILE_CIRCLE, 0, LL_PCODE_PATH_FLEXIBLE);
            q.setBeginAndEndS(0.f, 0.5f);
            add("cylinder_flex/cutS_0_50/" + lodName(detail), q, detail, FLAG_FLEX);
        }

        // Sculpted prims: the map-to-mesh step stays C++ this rung and is
        // recorded as input; the stitching in the face builder is tested.
        const U8 sculpt_types[4] = { LL_SCULPT_TYPE_SPHERE, LL_SCULPT_TYPE_TORUS, LL_SCULPT_TYPE_PLANE, LL_SCULPT_TYPE_CYLINDER };
        const char* sculpt_names[4] = { "sculpt_sphere", "sculpt_torus", "sculpt_plane", "sculpt_cylinder" };
        const U8 sculpt_flags[3] = { 0, LL_SCULPT_FLAG_INVERT, LL_SCULPT_FLAG_MIRROR };
        const char* flag_names[3] = { "plain", "invert", "mirror" };
        for (S32 t = 0; t < 4; ++t)
        {
            for (S32 f = 0; f < 3; ++f)
            {
                for (F32 detail : { 1.f, 4.f })
                {
                    LLVolumeParams p = makeParams(LL_PCODE_PROFILE_CIRCLE, 0, LL_PCODE_PATH_CIRCLE);
                    p.setSculptID(LLUUID("8f2c7e1a-0b3d-4c5e-9f60-1234567890ab"), sculpt_types[t] | sculpt_flags[f]);
                    add(std::string(sculpt_names[t]) + "/" + flag_names[f] + "/" + lodName(detail), p, detail, FLAG_SCULPT, 32, 32);
                }
            }
        }
        {
            LLVolumeParams p = makeParams(LL_PCODE_PROFILE_CIRCLE, 0, LL_PCODE_PATH_CIRCLE);
            p.setSculptID(LLUUID("8f2c7e1a-0b3d-4c5e-9f60-1234567890ab"), LL_SCULPT_TYPE_SPHERE | LL_SCULPT_FLAG_INVERT | LL_SCULPT_FLAG_MIRROR);
            add("sculpt_sphere/invert_mirror_64x16/lod4", p, 4.f, FLAG_SCULPT, 64, 16);
            LLVolumeParams q = makeParams(LL_PCODE_PROFILE_CIRCLE, 0, LL_PCODE_PATH_CIRCLE);
            q.setSculptID(LLUUID("8f2c7e1a-0b3d-4c5e-9f60-1234567890ab"), LL_SCULPT_TYPE_TORUS);
            add("sculpt_torus/plain_16x64/lod2.5", q, 2.5f, FLAG_SCULPT, 16, 64);
        }
        return cases;
    }

    //------------------------------------------------------------------
    // synthetic inputs for the flexible and sculpt cases
    //------------------------------------------------------------------
    constexpr S32 PATHPT_FLOATS = 21; // rot rows 0..2 (12), pos (4), scale (4), texT (1)

    // Deterministic flexible path of nine sections, as the simulation would fill it.
    std::vector<F32> makeFlexPath()
    {
        const S32 n = 9;
        std::vector<F32> out;
        for (S32 i = 0; i < n; ++i)
        {
            F32 t = (F32)i / (F32)(n - 1);
            LLQuaternion rot;
            rot.setQuat(0.2f * (F32)i, LLVector3(1.f, 0.3f, 0.f));
            LLMatrix3 m(rot);
            for (S32 r = 0; r < 3; ++r)
            {
                out.push_back(m.mMatrix[r][0]); out.push_back(m.mMatrix[r][1]); out.push_back(m.mMatrix[r][2]); out.push_back(0.f);
            }
            out.push_back(0.1f * sinf((F32)i)); out.push_back(0.05f * (F32)i); out.push_back(t - 0.5f); out.push_back(0.f);
            out.push_back(1.f - 0.05f * (F32)i); out.push_back(1.f - 0.03f * (F32)i); out.push_back(0.f); out.push_back(1.f);
            out.push_back(t);
        }
        return out;
    }

    void applyFlexPath(LLVolume* vol, const std::vector<F32>& pts)
    {
        S32 n = (S32)(pts.size() / PATHPT_FLOATS);
        vol->resizePath(n);
        LLPath& path = vol->getPath();
        for (S32 i = 0; i < n; ++i)
        {
            const F32* p = &pts[i * PATHPT_FLOATS];
            LLPath::PathPt& pt = path.mPath[i];
            LLMatrix3 m;
            for (S32 r = 0; r < 3; ++r)
            {
                m.mMatrix[r][0] = p[r * 4]; m.mMatrix[r][1] = p[r * 4 + 1]; m.mMatrix[r][2] = p[r * 4 + 2];
            }
            pt.mRot.loadu(m);
            pt.mPos.load3(p + 12);
            pt.mScale.set(p[16], p[17], p[18], p[19]);
            pt.mTexT = p[20];
        }
        vol->regen();
    }

    std::vector<U8> makeSculptMap(U16 w, U16 h)
    {
        std::vector<U8> map((size_t)w * h * 3);
        for (S32 y = 0; y < h; ++y)
        {
            for (S32 x = 0; x < w; ++x)
            {
                F32 fx = (F32)x / (F32)(w - 1);
                F32 fy = (F32)y / (F32)(h - 1);
                F32 r = 128.f + 100.f * sinf(F_TWO_PI * fx) * cosf(F_PI * fy);
                F32 g = 128.f + 100.f * cosf(F_TWO_PI * fx) * cosf(F_PI * fy);
                F32 b = 128.f + 100.f * sinf(F_PI * fy) * (0.5f + 0.5f * fx);
                U8* px = &map[((size_t)y * w + x) * 3];
                px[0] = (U8)llclamp((S32)r, 0, 255);
                px[1] = (U8)llclamp((S32)g, 0, 255);
                px[2] = (U8)llclamp((S32)b, 0, 255);
            }
        }
        return map;
    }

    // Build a case exactly as the viewer would (construction, then the
    // flexible path or the sculpt map), without tangents.
    LLPointer<LLVolume> buildVolume(const CaseSpec& c, const std::vector<F32>& flex, const std::vector<U8>& map)
    {
        LLPointer<LLVolume> vol = new LLVolume(c.params, c.detail);
        if (c.flags & FLAG_FLEX)
        {
            applyFlexPath(vol, flex);
        }
        if (c.flags & FLAG_SCULPT)
        {
            vol->sculpt(c.sculpt_w, c.sculpt_h, 3, map.data(), 0, false);
        }
        return vol;
    }

    //------------------------------------------------------------------
    // recording
    //------------------------------------------------------------------
    void writeBlock(Writer& w, const void* data, size_t len, bool full)
    {
        w.u64(fnv1a(data, len));
        w.u8(full ? 1 : 0);
        if (full)
        {
            w.u32((U32)len);
            w.raw(data, len);
        }
    }

    void pathToCanonical(const LLPath& path, std::vector<F32>& out)
    {
        out.clear();
        for (S32 i = 0; i < path.getPathLength(); ++i)
        {
            const LLPath::PathPt& pt = path.mPath[i];
            for (S32 r = 0; r < 3; ++r)
            {
                const F32* row = pt.mRot.mMatrix[r].getF32ptr();
                out.push_back(row[0]); out.push_back(row[1]); out.push_back(row[2]); out.push_back(row[3]);
            }
            const F32* pos = pt.mPos.getF32ptr();
            out.push_back(pos[0]); out.push_back(pos[1]); out.push_back(pos[2]); out.push_back(pos[3]);
            const F32* sc = pt.mScale.getF32ptr();
            out.push_back(sc[0]); out.push_back(sc[1]); out.push_back(sc[2]); out.push_back(sc[3]);
            out.push_back(pt.mTexT);
        }
    }

    void profileFacesToCanonical(const LLProfile& prof, std::vector<U8>& out)
    {
        Writer w;
        for (const LLProfile::Face& f : prof.mFaces)
        {
            w.i32(f.mIndex);
            w.i32(f.mCount);
            w.f32(f.mScaleU);
            w.u8(f.mCap ? 1 : 0);
            w.u8(f.mFlat ? 1 : 0);
            w.u16((U16)f.mFaceID);
        }
        out.swap(w.b);
    }

    size_t volumeBytes(const LLVolume* vol)
    {
        size_t n = (size_t)vol->getMesh().size() * 16 + (size_t)vol->getProfile().mProfile.size() * 16 + (size_t)vol->getPath().getPathLength() * 84;
        for (S32 i = 0; i < vol->getNumVolumeFaces(); ++i)
        {
            const LLVolumeFace& f = vol->getVolumeFace(i);
            n += (size_t)f.mNumVertices * (16 + 16 + 8 + 16 + 16) + (size_t)f.mNumIndices * 2;
        }
        return n;
    }

    void writeParams(Writer& w, const LLVolumeParams& p)
    {
        const LLProfileParams& pr = p.getProfileParams();
        const LLPathParams& pa = p.getPathParams();
        w.u8(pr.getCurveType()); w.f32(pr.getBegin()); w.f32(pr.getEnd()); w.f32(pr.getHollow());
        w.u8(pa.getCurveType()); w.f32(pa.getBegin()); w.f32(pa.getEnd());
        w.f32(pa.getScaleX()); w.f32(pa.getScaleY()); w.f32(pa.getShearX()); w.f32(pa.getShearY());
        w.f32(pa.getTwistBegin()); w.f32(pa.getTwistEnd()); w.f32(pa.getRadiusOffset());
        w.f32(pa.getTaperX()); w.f32(pa.getTaperY()); w.f32(pa.getRevolutions()); w.f32(pa.getSkew());
        w.u8(p.getSculptType());
    }

    LLVolumeParams readParams(Reader& r)
    {
        U8 pcurve = r.u8(); F32 pbegin = r.f32(); F32 pend = r.f32(); F32 hollow = r.f32();
        U8 curve = r.u8(); F32 begin = r.f32(); F32 end = r.f32();
        F32 scx = r.f32(); F32 scy = r.f32(); F32 shx = r.f32(); F32 shy = r.f32();
        F32 twist_begin = r.f32(); F32 twist_end = r.f32(); F32 radius_offset = r.f32();
        F32 tx = r.f32(); F32 ty = r.f32(); F32 revolutions = r.f32(); F32 skew = r.f32();
        U8 sculpt_type = r.u8();
        LLProfileParams profile(pcurve, pbegin, pend, hollow);
        LLPathParams path(curve, begin, end, scx, scy, shx, shy, twist_end, twist_begin, radius_offset, tx, ty, revolutions, skew);
        LLUUID id = sculpt_type ? LLUUID("8f2c7e1a-0b3d-4c5e-9f60-1234567890ab") : LLUUID::null;
        return LLVolumeParams(profile, path, id, sculpt_type);
    }

    void recordCase(Writer& w, U32 id, const CaseSpec& c, bool full)
    {
        std::vector<F32> flex;
        std::vector<U8> map;
        if (c.flags & FLAG_FLEX) flex = makeFlexPath();
        if (c.flags & FLAG_SCULPT) map = makeSculptMap(c.sculpt_w, c.sculpt_h);

        LLPointer<LLVolume> vol = buildVolume(c, flex, map);
        U32 flags = c.flags | FLAG_TANGENTS | (full ? FLAG_FULL : 0);

        w.u32(id);
        w.str(c.name);
        writeParams(w, c.params);
        w.f32(c.detail);
        w.u32(flags);
        w.u32(vol->mFaceMask);
        S32 counts[4] = { 0, 0, 0, 0 };
        if (!(c.flags & FLAG_SCULPT))
        {
            LLVolume::getLoDTriangleCounts(c.params, counts, vol);
        }
        for (S32 i = 0; i < 4; ++i) w.i32(counts[i]);

        if (c.flags & FLAG_FLEX)
        {
            w.u32((U32)(flex.size() / PATHPT_FLOATS));
            w.raw(flex.data(), flex.size() * 4);
        }
        if (c.flags & FLAG_SCULPT)
        {
            w.u16(c.sculpt_w);
            w.u16(c.sculpt_h);
            w.raw(map.data(), map.size());
        }

        const LLProfile& prof = vol->getProfile();
        w.u32(prof.mProfile.size());
        w.i32(prof.getTotal());
        w.i32(prof.getTotalOut());
        w.u8(prof.isOpen() ? 1 : 0);
        w.u8(prof.isConcave() ? 1 : 0);
        writeBlock(w, prof.mProfile.mArray, (size_t)prof.mProfile.size() * 16, full);
        std::vector<U8> faces;
        profileFacesToCanonical(prof, faces);
        w.u32((U32)prof.mFaces.size());
        writeBlock(w, faces.data(), faces.size(), true);

        const LLPath& path = vol->getPath();
        std::vector<F32> canon;
        pathToCanonical(path, canon);
        w.u32((U32)path.getPathLength());
        w.u8(path.isOpen() ? 1 : 0);
        w.f32(path.getStep());
        writeBlock(w, canon.data(), canon.size() * 4, full);

        // The sculpt mesh is an input for the Rust tests; always store it.
        w.u32(vol->getMesh().size());
        writeBlock(w, vol->getMesh().mArray, (size_t)vol->getMesh().size() * 16, full || (c.flags & FLAG_SCULPT));

        w.u32((U32)vol->getNumVolumeFaces());
        for (S32 i = 0; i < vol->getNumVolumeFaces(); ++i)
        {
            const LLVolumeFace& f = vol->getVolumeFace(i);
            w.i32(f.mID);
            w.u32(f.mTypeMask);
            w.i32(f.mBeginS); w.i32(f.mNumS); w.i32(f.mBeginT); w.i32(f.mNumT);
            w.u32((U32)f.mNumVertices);
            w.u32((U32)f.mNumIndices);
            w.raw(f.mExtents[0].getF32ptr(), 16);
            w.raw(f.mExtents[1].getF32ptr(), 16);
            w.raw(f.mCenter->getF32ptr(), 16);
            w.f32(f.mTexCoordExtents[0].mV[0]); w.f32(f.mTexCoordExtents[0].mV[1]);
            w.f32(f.mTexCoordExtents[1].mV[0]); w.f32(f.mTexCoordExtents[1].mV[1]);
            // Blocks that may be compared with a tolerance on another CPU
            // (values through _mm_rsqrt_ps: cap normals, tangents, normalised
            // normals) always carry their data, so such a comparison is never
            // skipped.
            const bool cap = (f.mTypeMask & LLVolumeFace::CAP_MASK) != 0;
            writeBlock(w, f.mPositions, (size_t)f.mNumVertices * 16, full);
            writeBlock(w, f.mNormals, (size_t)f.mNumVertices * 16, full || cap);
            writeBlock(w, f.mTexCoords, (size_t)f.mNumVertices * 8, full);
            writeBlock(w, f.mIndices, (size_t)f.mNumIndices * 2, full);
            vol->genTangents(i);
            writeBlock(w, f.mTangents, (size_t)f.mNumVertices * 16, true);
            writeBlock(w, f.mNormals, (size_t)f.mNumVertices * 16, true);
        }
    }

    void dumpCorpus(const std::string& out_path)
    {
        std::vector<CaseSpec> corpus = buildCorpus();
        const bool force_full = getenv("VKS_VOLUME_FULL") != nullptr;
        const size_t full_limit = 12 * 1024;
        S32 full_budget = 260;

        Writer w;
        w.u32(FIXTURE_MAGIC);
        w.u32(FIXTURE_VERSION);
        U32 sig[RSQRT_SAMPLE_COUNT];
        rsqrtSignature(sig);
        w.u32(RSQRT_SAMPLE_COUNT);
        for (S32 i = 0; i < RSQRT_SAMPLE_COUNT; ++i) w.u32(sig[i]);
        w.u32((U32)corpus.size());
        S32 full_count = 0;
        for (size_t i = 0; i < corpus.size(); ++i)
        {
            bool full = force_full;
            if (!full)
            {
                std::vector<F32> flex;
                std::vector<U8> map;
                if (corpus[i].flags & FLAG_FLEX) flex = makeFlexPath();
                if (corpus[i].flags & FLAG_SCULPT) map = makeSculptMap(corpus[i].sculpt_w, corpus[i].sculpt_h);
                LLPointer<LLVolume> probe = buildVolume(corpus[i], flex, map);
                full = volumeBytes(probe) <= full_limit && full_budget > 0;
            }
            if (full) { ++full_count; --full_budget; }
            recordCase(w, (U32)i, corpus[i], full);
        }
        std::ofstream out(out_path, std::ios::binary);
        out.write((const char*)w.b.data(), (std::streamsize)w.b.size());
        std::cout << "recorded " << corpus.size() << " cases (" << full_count << " with full data), "
                  << w.b.size() << " bytes to " << out_path << std::endl;
    }

    //------------------------------------------------------------------
    // comparison
    //------------------------------------------------------------------
    struct Block
    {
        U64 hash;
        bool has_data;
        std::vector<U8> data;
    };

    Block readBlock(Reader& r)
    {
        Block b;
        b.hash = r.u64();
        b.has_data = r.u8() != 0;
        if (b.has_data)
        {
            U32 len = r.u32();
            b.data = r.bytes(len);
        }
        return b;
    }

    struct Mismatches
    {
        S32 count = 0;
        S32 tolerance_only = 0;  // blocks compared with a tolerance
        S32 unverifiable = 0;    // hash-only blocks on a platform without a recording
        std::vector<std::string> details;
        void add(const std::string& what)
        {
            ++count;
            if (details.size() < 40) details.push_back(what);
        }
    };

    std::string firstDiff(const std::vector<U8>& expected, const void* actual, size_t len, size_t elem)
    {
        const U8* a = (const U8*)actual;
        if (expected.size() != len) return " (length " + std::to_string(len) + " vs " + std::to_string(expected.size()) + ")";
        for (size_t i = 0; i < len; i += elem)
        {
            if (memcmp(&expected[i], a + i, elem) != 0)
            {
                std::ostringstream s;
                s << " first difference at element " << i / elem << ": ";
                if (elem == 2)
                {
                    U16 e, g;
                    memcpy(&e, &expected[i], 2);
                    memcpy(&g, a + i, 2);
                    s << "expected " << e << " got " << g;
                    return s.str();
                }
                for (size_t k = 0; k + 4 <= elem; k += 4)
                {
                    F32 e, g;
                    memcpy(&e, &expected[i + k], 4);
                    memcpy(&g, a + i + k, 4);
                    s << (k ? ", " : "") << "expected " << e << " got " << g;
                }
                return s.str();
            }
        }
        return "";
    }

    // The tolerance comparison of one value: identical bits or two NaNs are
    // equal; otherwise any non-finite value is a difference; otherwise the
    // relative tolerance applies (against max(1, |expected|)).
    bool closeEnough(F32 expected, F32 actual, F32 rel)
    {
        U32 eb, ab;
        memcpy(&eb, &expected, 4);
        memcpy(&ab, &actual, 4);
        if (eb == ab) return true;
        if (llisnan(expected) && llisnan(actual)) return true;
        if (!llfinite(expected) || !llfinite(actual)) return false;
        return fabsf(expected - actual) <= rel * llmax(1.f, fabsf(expected));
    }

    // Loosely compare a block of floats: values that went through
    // _mm_rsqrt_ps on a CPU with a different approximation table (1e-3), or
    // any float array on a platform without its own recording (1e-4).
    bool withinTolerance(const std::vector<U8>& expected, const void* actual, size_t len, F32 rel)
    {
        if (expected.size() != len) return false;
        const F32* e = (const F32*)expected.data();
        const F32* a = (const F32*)actual;
        for (size_t i = 0; i < len / 4; ++i)
        {
            if (!closeEnough(e[i], a[i], rel)) return false;
        }
        return true;
    }

    void compareBlock(Mismatches& m, const std::string& what, const Block& b, const void* actual, size_t len, size_t elem, bool rsqrt_dependent, bool same_rsqrt)
    {
        U64 h = fnv1a(actual, len);
        if (h == b.hash && (!b.has_data || b.data.size() == len))
        {
            return;
        }
        const bool floats = elem != 2; // index blocks stay exact everywhere
        if ((rsqrt_dependent && !same_rsqrt) || (!VKS_VOLUME_STRICT_PLATFORM && floats))
        {
            if (!b.has_data)
            {
                if (rsqrt_dependent)
                {
                    m.add(what + ": no recorded data for the rsqrt tolerance comparison");
                }
                else
                {
                    ++m.unverifiable;
                }
                return;
            }
            ++m.tolerance_only;
            const F32 rel = (rsqrt_dependent && !same_rsqrt) ? 1e-3f : 1e-4f;
            if (!withinTolerance(b.data, actual, len, rel))
            {
                m.add(what + ": outside tolerance" + firstDiff(b.data, actual, len, elem));
            }
            return;
        }
        if (b.has_data)
        {
            m.add(what + firstDiff(b.data, actual, len, elem));
        }
        else
        {
            m.add(what + ": hash mismatch over " + std::to_string(len) + " bytes");
        }
    }

    // Profile face records (16 bytes: index, count, scale_u, cap, flat,
    // face id): the integer fields are compared exactly everywhere; only
    // scale_u takes the tolerance of a platform without a recording.
    void compareProfileFaces(Mismatches& m, const std::string& what, const Block& b, const std::vector<U8>& canon)
    {
        if (fnv1a(canon.data(), canon.size()) == b.hash && (!b.has_data || b.data.size() == canon.size()))
        {
            return;
        }
        if (!b.has_data)
        {
            m.add(what + ": hash mismatch over " + std::to_string(canon.size()) + " bytes");
            return;
        }
        if (b.data.size() != canon.size())
        {
            m.add(what + " (length " + std::to_string(canon.size()) + " vs " + std::to_string(b.data.size()) + ")");
            return;
        }
        for (size_t i = 0; i < canon.size(); i += 16)
        {
            const bool ints_equal = memcmp(&b.data[i], &canon[i], 8) == 0 && memcmp(&b.data[i + 12], &canon[i + 12], 4) == 0;
            F32 e, g;
            memcpy(&e, &b.data[i + 8], 4);
            memcpy(&g, &canon[i + 8], 4);
            const bool scale_equal = memcmp(&e, &g, 4) == 0 || (!VKS_VOLUME_STRICT_PLATFORM && closeEnough(e, g, 1e-4f));
            if (!ints_equal || !scale_equal)
            {
                S32 ei, ec, gi, gc;
                memcpy(&ei, &b.data[i], 4); memcpy(&ec, &b.data[i + 4], 4);
                memcpy(&gi, &canon[i], 4); memcpy(&gc, &canon[i + 4], 4);
                std::ostringstream s;
                s << what << " first difference at face " << i / 16 << ": expected (index " << ei << ", count " << ec << ", scaleU " << e
                  << ", cap " << (S32)b.data[i + 12] << ", flat " << (S32)b.data[i + 13] << ", id " << (S32)(b.data[i + 14] | (b.data[i + 15] << 8))
                  << ") got (index " << gi << ", count " << gc << ", scaleU " << g << ", cap " << (S32)canon[i + 12] << ", flat " << (S32)canon[i + 13]
                  << ", id " << (S32)(canon[i + 14] | (canon[i + 15] << 8)) << ")";
                m.add(s.str());
                return;
            }
            if (memcmp(&e, &g, 4) != 0) ++m.tolerance_only;
        }
    }

    template <class T>
    void compareScalar(Mismatches& m, const std::string& what, T expected, T actual)
    {
        if (!(expected == actual))
        {
            std::ostringstream s;
            s << what << ": expected " << expected << " got " << actual;
            m.add(s.str());
        }
    }

    void compareF32Bits(Mismatches& m, const std::string& what, const F32* expected, const F32* actual, S32 n)
    {
        if (memcmp(expected, actual, (size_t)n * 4) != 0)
        {
            if (!VKS_VOLUME_STRICT_PLATFORM)
            {
                bool close = true;
                for (S32 i = 0; i < n; ++i)
                {
                    if (!closeEnough(expected[i], actual[i], 1e-4f)) close = false;
                }
                if (close) { ++m.tolerance_only; return; }
            }
            std::ostringstream s;
            s << what << ": expected (";
            for (S32 i = 0; i < n; ++i) s << (i ? ", " : "") << expected[i];
            s << ") got (";
            for (S32 i = 0; i < n; ++i) s << (i ? ", " : "") << actual[i];
            s << ")";
            m.add(s.str());
        }
    }

    struct CorpusStats
    {
        S32 cases = 0;
        S32 faces = 0;
        S32 full_cases = 0;
        S32 blocks = 0;
    };

    void compareCase(Reader& r, Mismatches& m, CorpusStats& stats, bool same_rsqrt)
    {
        U32 id = r.u32();
        std::string name = r.str();
        LLVolumeParams params = readParams(r);
        F32 detail = r.f32();
        U32 flags = r.u32();
        U32 face_mask = r.u32();
        S32 counts[4];
        for (S32 i = 0; i < 4; ++i) counts[i] = r.i32();

        std::vector<F32> flex;
        std::vector<U8> map;
        CaseSpec spec;
        spec.name = name;
        spec.params = params;
        spec.detail = detail;
        spec.flags = flags;
        if (flags & FLAG_FLEX)
        {
            U32 n = r.u32();
            flex.resize((size_t)n * PATHPT_FLOATS);
            r.take(flex.data(), flex.size() * 4);
        }
        if (flags & FLAG_SCULPT)
        {
            spec.sculpt_w = r.u16();
            spec.sculpt_h = r.u16();
            map = r.bytes((size_t)spec.sculpt_w * spec.sculpt_h * 3);
        }
        if (!r.ok) { m.add(name + ": truncated fixture"); return; }

        const std::string tag = "[" + std::to_string(id) + "] " + name;
        ++stats.cases;
        if (flags & FLAG_FULL) ++stats.full_cases;

        LLPointer<LLVolume> vol = buildVolume(spec, flex, map);

        compareScalar(m, tag + " face mask", face_mask, vol->mFaceMask);
        if (!(flags & FLAG_SCULPT))
        {
            S32 got[4] = { 0, 0, 0, 0 };
            LLVolume::getLoDTriangleCounts(params, got, vol);
            for (S32 i = 0; i < 4; ++i) compareScalar(m, tag + " LOD triangle count " + std::to_string(i), counts[i], got[i]);
        }

        // profile
        {
            const LLProfile& prof = vol->getProfile();
            U32 count = r.u32(); S32 total = r.i32(); S32 total_out = r.i32(); U8 open = r.u8(); U8 concave = r.u8();
            Block pts = readBlock(r);
            U32 face_count = r.u32();
            Block faces = readBlock(r);
            compareScalar(m, tag + " profile point count", count, (U32)prof.mProfile.size());
            compareScalar(m, tag + " profile total", total, prof.getTotal());
            compareScalar(m, tag + " profile total_out", total_out, prof.getTotalOut());
            compareScalar(m, tag + " profile open", (S32)open, (S32)(prof.isOpen() ? 1 : 0));
            compareScalar(m, tag + " profile concave", (S32)concave, (S32)(prof.isConcave() ? 1 : 0));
            compareBlock(m, tag + " profile points", pts, prof.mProfile.mArray, (size_t)prof.mProfile.size() * 16, 16, false, same_rsqrt);
            compareScalar(m, tag + " profile face count", face_count, (U32)prof.mFaces.size());
            std::vector<U8> canon;
            profileFacesToCanonical(prof, canon);
            compareProfileFaces(m, tag + " profile faces", faces, canon);
            stats.blocks += 2;
        }
        // path
        {
            const LLPath& path = vol->getPath();
            U32 count = r.u32(); U8 open = r.u8(); F32 step = r.f32();
            Block pts = readBlock(r);
            compareScalar(m, tag + " path point count", count, (U32)path.getPathLength());
            compareScalar(m, tag + " path open", (S32)open, (S32)(path.isOpen() ? 1 : 0));
            F32 got_step = path.getStep();
            compareF32Bits(m, tag + " path step", &step, &got_step, 1);
            std::vector<F32> canon;
            pathToCanonical(path, canon);
            compareBlock(m, tag + " path points", pts, canon.data(), canon.size() * 4, PATHPT_FLOATS * 4, false, same_rsqrt);
            ++stats.blocks;
        }
        // mesh
        {
            U32 count = r.u32();
            Block mesh = readBlock(r);
            compareScalar(m, tag + " mesh point count", count, vol->getMesh().size());
            compareBlock(m, tag + " mesh", mesh, vol->getMesh().mArray, (size_t)vol->getMesh().size() * 16, 16, false, same_rsqrt);
            ++stats.blocks;
        }
        // faces
        U32 face_count = r.u32();
        compareScalar(m, tag + " volume face count", face_count, (U32)vol->getNumVolumeFaces());
        for (U32 i = 0; i < face_count; ++i)
        {
            S32 fid = r.i32(); U32 type_mask = r.u32();
            S32 begin_s = r.i32(); S32 num_s = r.i32(); S32 begin_t = r.i32(); S32 num_t = r.i32();
            U32 nv = r.u32(); U32 ni = r.u32();
            F32 extents[8]; r.take(extents, 32);
            F32 center[4]; r.take(center, 16);
            F32 tc_ext[4]; r.take(tc_ext, 16);
            Block pos = readBlock(r); Block norm = readBlock(r); Block tc = readBlock(r); Block idx = readBlock(r);
            Block tan = readBlock(r); Block norm_after = readBlock(r);
            if (!r.ok) { m.add(tag + ": truncated fixture"); return; }
            if ((S32)i >= vol->getNumVolumeFaces()) continue;
            ++stats.faces;
            stats.blocks += 6;
            const LLVolumeFace& f = vol->getVolumeFace((S32)i);
            const std::string ft = tag + " face " + std::to_string(i);
            compareScalar(m, ft + " id", fid, f.mID);
            compareScalar(m, ft + " type mask", type_mask, f.mTypeMask);
            compareScalar(m, ft + " beginS", begin_s, f.mBeginS);
            compareScalar(m, ft + " numS", num_s, f.mNumS);
            compareScalar(m, ft + " beginT", begin_t, f.mBeginT);
            compareScalar(m, ft + " numT", num_t, f.mNumT);
            compareScalar(m, ft + " vertex count", nv, (U32)f.mNumVertices);
            compareScalar(m, ft + " index count", ni, (U32)f.mNumIndices);
            if (nv != (U32)f.mNumVertices || ni != (U32)f.mNumIndices) continue;
            compareF32Bits(m, ft + " extents min", extents, f.mExtents[0].getF32ptr(), 4);
            compareF32Bits(m, ft + " extents max", extents + 4, f.mExtents[1].getF32ptr(), 4);
            compareF32Bits(m, ft + " center", center, f.mCenter->getF32ptr(), 4);
            F32 got_tc[4] = { f.mTexCoordExtents[0].mV[0], f.mTexCoordExtents[0].mV[1], f.mTexCoordExtents[1].mV[0], f.mTexCoordExtents[1].mV[1] };
            compareF32Bits(m, ft + " texcoord extents", tc_ext, got_tc, 4);
            const bool cap = (f.mTypeMask & LLVolumeFace::CAP_MASK) != 0;
            compareBlock(m, ft + " positions", pos, f.mPositions, (size_t)nv * 16, 16, false, same_rsqrt);
            compareBlock(m, ft + " normals", norm, f.mNormals, (size_t)nv * 16, 16, cap, same_rsqrt);
            compareBlock(m, ft + " texcoords", tc, f.mTexCoords, (size_t)nv * 8, 8, false, same_rsqrt);
            compareBlock(m, ft + " indices", idx, f.mIndices, (size_t)ni * 2, 2, false, same_rsqrt);
            vol->genTangents((S32)i);
            compareBlock(m, ft + " tangents", tan, f.mTangents, (size_t)nv * 16, 16, true, same_rsqrt);
            compareBlock(m, ft + " normalized normals", norm_after, f.mNormals, (size_t)nv * 16, 16, true, same_rsqrt);
        }
    }

    std::vector<U8> readFile(const std::string& path)
    {
        std::ifstream in(path, std::ios::binary);
        std::vector<U8> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        return data;
    }

    //------------------------------------------------------------------
    // timing
    //------------------------------------------------------------------
    void benchCorpus(S32 iterations, const std::string& out_path)
    {
        std::vector<CaseSpec> corpus = buildCorpus();
        std::ostream* out = &std::cout;
        std::ofstream file;
        if (!out_path.empty())
        {
            file.open(out_path);
            out = &file;
        }
        *out << "name,shape,detail,vertices,triangles,min_us,median_us\n";
        for (const CaseSpec& c : corpus)
        {
            std::vector<F32> flex;
            std::vector<U8> map;
            if (c.flags & FLAG_FLEX) flex = makeFlexPath();
            if (c.flags & FLAG_SCULPT) map = makeSculptMap(c.sculpt_w, c.sculpt_h);
            std::vector<double> samples;
            S32 verts = 0, tris = 0;
            for (S32 it = 0; it <= iterations; ++it)
            {
                auto t0 = std::chrono::steady_clock::now();
                LLPointer<LLVolume> vol = buildVolume(c, flex, map);
                for (S32 i = 0; i < vol->getNumVolumeFaces(); ++i) vol->genTangents(i);
                auto t1 = std::chrono::steady_clock::now();
                if (it > 0) samples.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
                tris = vol->getNumTriangles(&verts);
            }
            std::sort(samples.begin(), samples.end());
            std::string shape = c.name.substr(0, c.name.find('/'));
            *out << c.name << "," << shape << "," << c.detail << "," << verts << "," << tris << ","
                 << samples.front() << "," << samples[samples.size() / 2] << "\n";
        }
    }
}

namespace tut
{
    struct volume_data {};
    typedef test_group<volume_data> volume_group;
    typedef volume_group::object volume_object;
    tut::volume_group volume("LLVolume");

    // Every recorded case rebuilds bit for bit: profile, path, mesh, faces,
    // extents, centers, indices and tangents.
    template<> template<>
    void volume_object::test<1>()
    {
        const char* dump = getenv("VKS_VOLUME_DUMP");
        if (dump)
        {
            dumpCorpus(dump);
            return;
        }

        std::vector<U8> bytes = readFile(VKS_VOLUME_FIXTURE);
        ensure("fixture corpus present: " VKS_VOLUME_FIXTURE, !bytes.empty());
        Reader r(bytes);
        ensure_equals("fixture magic", r.u32(), FIXTURE_MAGIC);
        ensure_equals("fixture version", r.u32(), FIXTURE_VERSION);
        U32 sig_count = r.u32();
        ensure_equals("rsqrt signature size", sig_count, (U32)RSQRT_SAMPLE_COUNT);
        U32 recorded[RSQRT_SAMPLE_COUNT];
        for (S32 i = 0; i < RSQRT_SAMPLE_COUNT; ++i) recorded[i] = r.u32();
        U32 here[RSQRT_SAMPLE_COUNT];
        rsqrtSignature(here);
        const bool same_rsqrt = memcmp(recorded, here, sizeof(here)) == 0;
        std::cout << "LLVolume: corpus " << VKS_VOLUME_FIXTURE << std::endl;
        if (!same_rsqrt)
        {
            std::cout << "LLVolume: this CPU's _mm_rsqrt_ps differs from the recording machine; "
                         "normalized normals and tangents are compared with a tolerance" << std::endl;
        }
        if (!VKS_VOLUME_STRICT_PLATFORM)
        {
            std::cout << "LLVolume: no oracle recording for this platform; float arrays are compared with a tolerance" << std::endl;
        }
        U32 case_count = r.u32();
        ensure("fixture has cases", case_count > 0);

        Mismatches m;
        CorpusStats stats;
        for (U32 i = 0; i < case_count && r.ok; ++i)
        {
            compareCase(r, m, stats, same_rsqrt);
        }
        ensure("fixture read completely", r.ok);
        std::cout << "LLVolume: " << stats.cases << " cases, " << stats.faces << " faces, " << stats.blocks
                  << " array blocks compared (" << stats.full_cases << " cases with full data), "
                  << m.count << " mismatches, " << m.tolerance_only << " tolerance comparisons, "
                  << m.unverifiable << " unverifiable" << std::endl;
        for (const std::string& d : m.details)
        {
            std::cout << "  " << d << std::endl;
        }
        ensure_equals("mesher output matches the recorded corpus", m.count, 0);
        if (VKS_VOLUME_STRICT_PLATFORM)
        {
            ensure_equals("no unverifiable blocks on a recording platform", m.unverifiable, 0);
        }
        if (VKS_VOLUME_STRICT_PLATFORM && same_rsqrt)
        {
            ensure_equals("every block exact on the recording platform", m.tolerance_only, 0);
        }
    }

    // The comparison helpers themselves: the tolerance treats NaN and
    // infinities as differences unless both sides are NaN or identical, and
    // the index diagnostic decodes U16.
    template<> template<>
    void volume_object::test<3>()
    {
        const F32 nan = std::numeric_limits<F32>::quiet_NaN();
        const F32 inf = std::numeric_limits<F32>::infinity();
        ensure("equal values are close", closeEnough(0.5f, 0.5f, 1e-4f));
        ensure("two NaNs are close", closeEnough(nan, -nan, 1e-4f));
        ensure("NaN against a finite value is a difference", !closeEnough(0.5f, nan, 1e-4f) && !closeEnough(nan, 0.5f, 1e-4f));
        ensure("infinity against a finite value is a difference", !closeEnough(inf, 1e30f, 1e-4f) && !closeEnough(1.f, inf, 1e-4f));
        ensure("identical infinities are close", closeEnough(inf, inf, 1e-4f) && closeEnough(-inf, -inf, 1e-4f));
        ensure("opposite infinities are a difference", !closeEnough(inf, -inf, 1e-4f));
        ensure("within the relative tolerance", closeEnough(2.f, 2.f + 1e-4f, 1e-4f) && closeEnough(0.001f, 0.001f + 5e-5f, 1e-4f));
        ensure("outside the relative tolerance", !closeEnough(2.f, 2.f + 3e-4f, 1e-4f) && !closeEnough(0.f, 2e-4f, 1e-4f));

        std::vector<U8> expected_f(8);
        F32 ef[2] = { 1.f, 2.f };
        memcpy(expected_f.data(), ef, 8);
        F32 af[2] = { 1.f, nan };
        ensure("NaN against a finite value fails the block tolerance", !withinTolerance(expected_f, af, 8, 1e-3f));
        F32 bf[2] = { 1.f, 2.f + 1e-4f };
        ensure("a small difference passes the block tolerance", withinTolerance(expected_f, bf, 8, 1e-3f));

        std::vector<U8> expected_i(6);
        U16 ei[3] = { 7, 8, 9 };
        memcpy(expected_i.data(), ei, 6);
        U16 ai[3] = { 7, 8, 1234 };
        std::string d = firstDiff(expected_i, ai, 6, 2);
        ensure_equals("index diagnostic decodes U16", d, std::string(" first difference at element 2: expected 9 got 1234"));
        U16 ci[3] = { 7, 8, 9 };
        ensure_equals("no difference, no diagnostic", firstDiff(expected_i, ci, 6, 2), std::string(""));
    }

    // Timing of the corpus through LLVolume (construction and tangents),
    // only when asked for (VKS_VOLUME_BENCH=<iterations>).
    template<> template<>
    void volume_object::test<2>()
    {
        const char* bench = getenv("VKS_VOLUME_BENCH");
        if (!bench)
        {
            return;
        }
        S32 iterations = llmax(1, atoi(bench));
        const char* out = getenv("VKS_VOLUME_BENCH_OUT");
        benchCorpus(iterations, out ? out : "");
    }
}
