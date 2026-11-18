// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Tests for LLSimdKernels, the bit-exact AVX2 kernels of multi-core stage 3 (doc/perf/multicore-simd-study.md section 6.3).
// Each kernel is checked against the code it replaces, bit for bit:
//   K1  positions: LLFace::getGeometryVolume's former loop (LLMatrix4a::affineTransformSSE, the texture index in w) and
//       the padding after it (the last vertex's transform, w as computed)
//   K2  normals and tangents: its former loops (LLMatrix4a::rotate; tangents keep their w)
//   K5  LLCamera::AABBInFrustum8 / AABBInFrustumNoFarClip8 against AABBInFrustum / AABBInFrustumNoFarClip on real camera
//       frusta; aabbSphereIntersect8 against aabbSphereIntersectR2; and the octree cull's whole test (the frustum test, then
//       the sphere test unless the box is outside, LLOctreeCull::frustumCheck) against the one-box functions
// Inputs: random values, random bit patterns (every exponent, NaNs with payloads, denormals) and edge cases (+-0, +-inf,
// quiet and signalling NaNs, denormals, +-FLT_MAX, values whose squares overflow or underflow) in vertices, matrices,
// boxes, planes and radii; counts around the two-vertices-per-register boundary; sources and destinations on both
// 16-byte phases of a 32-byte line; flush-to-zero and denormals-are-zero on and off in all four rounding modes. Where two
// NaNs meet in one multiply or add, x86 returns the first operand's and either version's compiler may swap the operands
// (llsimdkernels.h), so there only "both NaN" is required; the tests count those floats and print the counts.
// test<5> is an opt-in micro-bench: set LL_SIMD_KERNELS_BENCH=1 to print ns per vertex and per box.

#include "linden_common.h"

#include "../llmath.h"
#include "../llsimdkernels.h"
#include "../llcamera.h"
#include "../llmatrix4a.h"
#include "../llplane.h"
#include "../v3math.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

#if defined(__x86_64__) || defined(_M_X64)
#include <xmmintrin.h>
#define TEST_HAS_MXCSR 1
#else
#define TEST_HAS_MXCSR 0
#endif

#include "../test/lltut.h"

// A build for AVX2 must compile the AVX2 kernels; otherwise these tests would compare the SSE code with itself.
#if defined(USE_AVX2_OPTIMIZATION) && (defined(__x86_64__) || defined(_M_X64)) && !defined(__AVX2__)
#error "USE_AVX2_OPTIMIZATION is set, but the compiler does not target AVX2"
#endif

#if defined(_MSC_VER)
#define TEST_NOINLINE __declspec(noinline)
#else
#define TEST_NOINLINE __attribute__((noinline))
#endif

namespace
{
    // --- Values ---------------------------------------------------------------------------------------------------------

    U32 bitsOf(F32 f)
    {
        U32 u;
        std::memcpy(&u, &f, sizeof(u));
        return u;
    }

    F32 fromBits(U32 u)
    {
        F32 f;
        std::memcpy(&f, &u, sizeof(f));
        return f;
    }

    // By the bits, so that no compiler setting can change the answer.
    bool isNaN(F32 f)
    {
        return (bitsOf(f) & 0x7fffffffu) > 0x7f800000u;
    }

    std::string hex(U32 u)
    {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "0x%08x", u);
        return buf;
    }

    // xorshift64*: deterministic, so every run and every platform tests the same data. C++ leaves the order in which a
    // call's arguments are evaluated open, so no argument list holds two draws: they go into locals first, last argument
    // first (the order MSVC and GCC both used when these lists held the draws, which keeps the data and the printed
    // counts as they were).
    struct Rng
    {
        uint64_t mState;
        explicit Rng(uint64_t seed) : mState(seed * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull) {}
        U32 bits()
        {
            mState ^= mState >> 12;
            mState ^= mState << 25;
            mState ^= mState >> 27;
            return U32((mState * 0x2545F4914F6CDD1Dull) >> 32);
        }
        U32 below(U32 n) { return bits() % n; }
        F32 uniform(F32 lo, F32 hi) { return lo + (hi - lo) * (F32(bits() >> 8) * (1.f / 16777216.f)); }
    };

    const U32 kSpecialBits[] =
    {
        0x00000000, 0x80000000, // +-0
        0x7f800000, 0xff800000, // +-inf
        0x7fc00000, 0xffc00000, // quiet NaNs; 0xffc00000 is the NaN x86 makes (inf - inf, 0 * inf)
        0x7fc12345, 0xffd00001, // quiet NaNs with payloads
        0x7f800001, 0xff812345, // signalling NaNs, quieted by any arithmetic
        0x00000001, 0x80000001, // smallest denormals
        0x007fffff, 0x807fffff, // largest denormals
        0x00800000, 0x80800000, // smallest normals
        0x7f7fffff, 0xff7fffff, // +-FLT_MAX
        0x3f800000, 0xbf800000, // +-1
        0x5f800000, 0xdf000000, // +2^64, -2^63: squares and products overflow
        0x1f800000, 0x9f800000, // +-2^-64: squares and products underflow to denormals or zero
    };

    F32 special(Rng& rng)
    {
        return fromBits(kSpecialBits[rng.below((U32)std::size(kSpecialBits))]);
    }

    // An edge-case value: a special value, any bit pattern, or an ordinary one.
    F32 edgeValue(Rng& rng, F32 range)
    {
        const U32 pick = rng.below(4);
        if (pick == 0)
        {
            return special(rng);
        }
        if (pick == 1)
        {
            return fromBits(rng.bits()); // every exponent, NaN payloads, denormals
        }
        return rng.uniform(-range, range);
    }

    // A vertex: mostly ordinary; every fourth one carries edge cases in any lane, w included.
    LLVector4a vertex(Rng& rng, F32 range)
    {
        LLVector4a v;
        if (rng.below(4) != 0)
        {
            const F32 w = rng.uniform(-1.f, 1.f); // last argument first (see Rng)
            const F32 z = rng.uniform(-range, range);
            const F32 y = rng.uniform(-range, range);
            const F32 x = rng.uniform(-range, range);
            v.set(x, y, z, w);
        }
        else
        {
            const F32 w = edgeValue(rng, 1.f);
            const F32 z = edgeValue(rng, range);
            const F32 y = edgeValue(rng, range);
            const F32 x = edgeValue(rng, range);
            v.set(x, y, z, w);
        }
        return v;
    }

    const S32 kMatrixKinds = 7;
    const char* const kMatrixNames[kMatrixKinds] =
    {
        "object transform", "axis-aligned (exact zeros)", "edge-case elements", "huge elements", "tiny elements",
        "identity", "full 4x4 (rigged normal matrix)"
    };

    LLMatrix4a matrix(Rng& rng, S32 kind)
    {
        F32 v[16];
        for (S32 i = 0; i < 16; ++i)
        {
            switch (kind)
            {
            case 0: // rotation and scale, a translation, w column (0, 0, 0, 1)
                v[i] = (i % 4 == 3) ? (i == 15 ? 1.f : 0.f) : (i >= 12 ? rng.uniform(-512.f, 512.f) : rng.uniform(-4.f, 4.f));
                break;
            case 1: // a scale on the diagonal and exact zeros elsewhere: 0 * inf meets the edge-case vertices
                v[i] = (i % 5 == 0) ? (i == 15 ? 1.f : rng.uniform(-8.f, 8.f)) : (i >= 12 && i < 15 ? rng.uniform(-512.f, 512.f) : 0.f);
                break;
            case 2:
                v[i] = edgeValue(rng, 8.f);
                break;
            case 3: // products and sums overflow to inf, and inf - inf makes NaN
                v[i] = rng.uniform(-1.f, 1.f) * 3.0e38f;
                break;
            case 4: // products underflow into denormals, flushed under flush-to-zero
                v[i] = rng.uniform(-1.f, 1.f) * 1.0e-36f;
                break;
            case 5:
                v[i] = (i % 5 == 0) ? 1.f : 0.f;
                break;
            default: // the inverse transpose of a bind shape matrix: a full 4x4
                v[i] = rng.uniform(-2.f, 2.f);
                break;
            }
        }
        LLMatrix4a m;
        m.loadu(v);
        return m;
    }

    // --- Floating-point environments --------------------------------------------------------------------------------

    struct FpCase
    {
        bool mFlushToZero;
        bool mDenormalsAreZero;
        U32  mRounding; // MXCSR RC: 0 nearest, 1 down, 2 up, 3 toward zero
        std::string name() const
        {
            static const char* const rounding[] = { "nearest", "down", "up", "toward zero" };
            return std::string("FTZ ") + (mFlushToZero ? "on" : "off") + ", DAZ " + (mDenormalsAreZero ? "on" : "off") +
                   ", rounding " + rounding[mRounding & 3];
        }
    };

#if TEST_HAS_MXCSR
    // Sets flush-to-zero (bit 15), denormals-are-zero (bit 6) and the rounding mode (bits 13-14) for its scope; the
    // exception masks stay as they are (all masked).
    struct ScopedFpCase
    {
        unsigned int mSaved;
        explicit ScopedFpCase(const FpCase& c) : mSaved(_mm_getcsr())
        {
            _mm_setcsr((mSaved & ~(0xE040u | 0x3Fu)) | (c.mFlushToZero ? 0x8000u : 0u) | (c.mDenormalsAreZero ? 0x0040u : 0u) |
                       ((c.mRounding & 3u) << 13));
        }
        ~ScopedFpCase() { _mm_setcsr(mSaved); }
    };

    std::vector<FpCase> fpCases()
    {
        std::vector<FpCase> cases;
        for (U32 rounding = 0; rounding < 4; ++rounding)
        {
            for (bool ftz : { true, false })
            {
                for (bool daz : { false, true })
                {
                    cases.push_back({ ftz, daz, rounding });
                }
            }
        }
        return cases;
    }
#else
    struct ScopedFpCase
    {
        explicit ScopedFpCase(const FpCase&) {}
    };

    std::vector<FpCase> fpCases()
    {
        return { { false, false, 0 } }; // the platform's default environment
    }
#endif

    // --- Comparison -----------------------------------------------------------------------------------------------------

    bool bothNaN(F32 a, F32 b)
    {
        return isNaN(a) && isNaN(b);
    }

    // Whether two NaNs meet in one operation of lane k of m.affineTransformSSE(v): ((x*r0 + y*r1) + (z*r2 + r3)). Scalar
    // operations in the same order give each step's NaN-ness under the same MXCSR (inf - inf depends on overflow, which
    // depends on the rounding mode).
    TEST_NOINLINE bool affineNaNsMeet(const LLVector4a& v, const LLMatrix4a& m, S32 k)
    {
        const F32 x = v[0], y = v[1], z = v[2];
        const F32 m0 = m.mMatrix[0][k], m1 = m.mMatrix[1][k], m2 = m.mMatrix[2][k], m3 = m.mMatrix[3][k];
        const F32 a = x * m0, b = y * m1, c = z * m2;
        const F32 s1 = a + b, s2 = c + m3;
        return bothNaN(x, m0) || bothNaN(y, m1) || bothNaN(z, m2) || bothNaN(a, b) || bothNaN(c, m3) || bothNaN(s1, s2);
    }

    // The same for m.rotate(v): ((x*r0 + y*r1) + z*r2).
    TEST_NOINLINE bool rotateNaNsMeet(const LLVector4a& v, const LLMatrix4a& m, S32 k)
    {
        const F32 x = v[0], y = v[1], z = v[2];
        const F32 m0 = m.mMatrix[0][k], m1 = m.mMatrix[1][k], m2 = m.mMatrix[2][k];
        const F32 a = x * m0, b = y * m1, c = z * m2;
        const F32 s = a + b;
        return bothNaN(x, m0) || bothNaN(y, m1) || bothNaN(z, m2) || bothNaN(a, b) || bothNaN(s, c);
    }

    struct Tally
    {
        size_t mFloats = 0;        // floats compared
        size_t mNaNsMet = 0;       // of which two NaNs met in one operation: only "both NaN" is required
        size_t mPayloadsDiffer = 0; // of those, the NaN bits differ (allowed, counted)
        std::string mFailure;      // the first difference, empty when none

        // Identical bits; or, where two NaNs met in one operation, both NaN.
        void check(F32 expected, F32 actual, bool nans_meet, const std::string& where)
        {
            ++mFloats;
            const bool same = bitsOf(expected) == bitsOf(actual);
            if (nans_meet)
            {
                ++mNaNsMet;
                if (!same)
                {
                    ++mPayloadsDiffer;
                }
                if (same || bothNaN(expected, actual))
                {
                    return;
                }
            }
            else if (same)
            {
                return;
            }
            if (mFailure.empty())
            {
                mFailure = where + ": expected " + hex(bitsOf(expected)) + ", got " + hex(bitsOf(actual));
            }
        }

        void exact(U32 expected, U32 actual, const std::string& where)
        {
            ++mFloats;
            if (expected != actual && mFailure.empty())
            {
                mFailure = where + ": expected " + hex(expected) + ", got " + hex(actual);
            }
        }

        std::string summary(const char* what) const
        {
            return std::string(what) + ": " + std::to_string(mFloats) + " floats bit for bit, " + std::to_string(mNaNsMet) +
                   " where two NaNs met (" + std::to_string(mPayloadsDiffer) + " with another NaN payload)";
        }
    };

    // --- Buffers ----------------------------------------------------------------------------------------------------------

    // Floats on a 64-byte boundary plus a phase (in floats), with guard floats before and after that must stay untouched.
    struct GuardedFloats
    {
        static const size_t kGuard = 16;
        static const U32 kCanary = 0x7fbadbadu; // a NaN pattern no kernel produces from these inputs
        F32* mBase;
        F32* mData;
        size_t mCount;
        GuardedFloats(size_t count, size_t phase) : mCount(count)
        {
            mBase = (F32*)ll_aligned_malloc_64((count + 2 * kGuard + 16) * sizeof(F32));
            mData = mBase + kGuard + phase;
            for (size_t i = 0; i < count + 2 * kGuard + 16; ++i)
            {
                mBase[i] = fromBits(kCanary);
            }
        }
        ~GuardedFloats() { ll_aligned_free_64(mBase); }
        GuardedFloats(const GuardedFloats&) = delete;
        GuardedFloats& operator=(const GuardedFloats&) = delete;
        bool guardsIntact() const
        {
            for (F32* p = mBase; p < mData; ++p)
            {
                if (bitsOf(*p) != kCanary)
                {
                    return false;
                }
            }
            for (size_t i = mCount; i < mCount + kGuard; ++i)
            {
                if (bitsOf(mData[i]) != kCanary)
                {
                    return false;
                }
            }
            return true;
        }
    };

    // LLVector4a on a 64-byte boundary plus a phase (0 or 1 vectors: both 16-byte phases of a 32-byte line).
    struct Vertices
    {
        LLVector4a* mBase;
        LLVector4a* mData;
        Vertices(size_t count, size_t phase)
        {
            mBase = (LLVector4a*)ll_aligned_malloc_64((count + 2) * sizeof(LLVector4a));
            mData = mBase + phase;
        }
        ~Vertices() { ll_aligned_free_64(mBase); }
        Vertices(const Vertices&) = delete;
        Vertices& operator=(const Vertices&) = delete;
    };

    // --- The former loops of LLFace::getGeometryVolume, verbatim, and the same through the kernels -------------------

    TEST_NOINLINE void referencePositions(const LLMatrix4a& mat_vert, const LLVector4a* src, S32 num_vertices, F32* dst,
                                          S32 geom_count, const LLVector4a& texIdx, LLVector4a& res0)
    {
        const LLVector4a* end = src + num_vertices;
        F32* end_f32 = dst + geom_count * 4;
        LLVector4Logical mask;
        mask.clear();
        mask.setElement<3>();
        LLVector4a tmp;
        while (src < end)
        {
            mat_vert.affineTransform(*src++, res0);
            tmp.setSelectWithMask(mask, texIdx, res0);
            tmp.store4a((F32*) dst);
            dst += 4;
        }
        while (dst < end_f32)
        {
            res0.store4a((F32*) dst);
            dst += 4;
        }
    }

    TEST_NOINLINE void kernelPositions(const LLMatrix4a& mat_vert, const LLVector4a* src, S32 num_vertices, F32* dst,
                                       S32 geom_count, const LLVector4a& texIdx, LLVector4a& res0)
    {
        F32* end_f32 = dst + geom_count * 4;
        LLSimdKernels::affineTransformPositions(mat_vert, src, dst, num_vertices, texIdx, res0);
        dst += 4 * num_vertices;
        while (dst < end_f32)
        {
            res0.store4a((F32*) dst);
            dst += 4;
        }
    }

    TEST_NOINLINE void referenceNormals(const LLMatrix4a& mat_normal, const LLVector4a* src, S32 num_vertices, F32* normals)
    {
        const LLVector4a* end = src + num_vertices;
        while (src < end)
        {
            LLVector4a normal;
            mat_normal.rotate(*src++, normal);
            normal.store4a(normals);
            normals += 4;
        }
    }

    TEST_NOINLINE void kernelNormals(const LLMatrix4a& mat_normal, const LLVector4a* src, S32 num_vertices, F32* normals)
    {
        LLSimdKernels::rotateNormals(mat_normal, src, normals, num_vertices);
    }

    TEST_NOINLINE void referenceTangents(const LLMatrix4a& mat_normal, const LLVector4a* src, S32 num_vertices, F32* tangents)
    {
        LLVector4Logical mask;
        mask.clear();
        mask.setElement<3>();
        const LLVector4a* end = src + num_vertices;
        while (src < end)
        {
            LLVector4a tangent_out;
            mat_normal.rotate(*src, tangent_out);
            tangent_out.setSelectWithMask(mask, *src, tangent_out);
            tangent_out.store4a(tangents);
            src++;
            tangents += 4;
        }
    }

    TEST_NOINLINE void kernelTangents(const LLMatrix4a& mat_normal, const LLVector4a* src, S32 num_vertices, F32* tangents)
    {
        LLSimdKernels::rotateTangents(mat_normal, src, tangents, num_vertices);
    }

    // LLFace keeps the texture index's bits in position.w.
    LLVector4a textureIndex(S32 index)
    {
        LLVector4a tex_idx;
        tex_idx.set(0, 0, 0, fromBits((U32)index));
        return tex_idx;
    }

    // --- Cameras ------------------------------------------------------------------------------------------------------

    LLVector3 unit(const LLVector3& v)
    {
        LLVector3 u(v);
        u.normVec();
        return u;
    }

    // A perspective camera built as LLViewerCamera::updateFrustumPlanes builds one: the eight corners of the frustum (near
    // then far; bottom left, bottom right, top right, top left) into LLCamera::calcAgentFrustumPlanes. 'at' and 'up' are
    // orthogonal unit vectors.
    void setPerspective(LLCamera& cam, const LLVector3& origin, const LLVector3& at, const LLVector3& up, F32 fov, F32 aspect,
                        F32 near_d, F32 far_d)
    {
        const LLVector3 left = up % at;
        cam.setOrigin(origin);
        cam.setAxes(at, left, up);
        const F32 t = tanf(0.5f * fov);
        LLVector3 frust[8];
        const F32 dist[2] = { near_d, far_d };
        for (S32 f = 0; f < 2; ++f)
        {
            const F32 h = t * dist[f];
            const F32 w = h * aspect;
            const LLVector3 c = origin + at * dist[f];
            frust[4 * f + 0] = c + left * w - up * h; // bottom left
            frust[4 * f + 1] = c - left * w - up * h; // bottom right
            frust[4 * f + 2] = c - left * w + up * h; // top right
            frust[4 * f + 3] = c + left * w + up * h; // top left
        }
        cam.calcAgentFrustumPlanes(frust);
    }

    struct TestCamera
    {
        std::string mName;
        LLCamera mCamera;
        LLVector3 mAt;
        F32 mFov = 1.f;
        F32 mAspect = 1.f;
        F32 mFar = 256.f;
    };

    std::vector<TestCamera> testCameras()
    {
        std::vector<TestCamera> cams;
        const LLVector3 origin(128.3f, 97.1f, 24.6f);
        const LLVector3 at = unit(LLVector3(0.8f, 0.55f, -0.24f));
        const LLVector3 up = unit(LLVector3(0.f, 0.f, 1.f) - at * (at * LLVector3(0.f, 0.f, 1.f)));

        auto add = [&](const std::string& name, const LLVector3& o, const LLVector3& a, const LLVector3& u, F32 fov, F32 aspect,
                       F32 near_d, F32 far_d) -> LLCamera&
        {
            cams.emplace_back();
            TestCamera& c = cams.back();
            c.mName = name;
            c.mAt = a;
            c.mFov = fov;
            c.mAspect = aspect;
            c.mFar = far_d;
            setPerspective(c.mCamera, o, a, u, fov, aspect, near_d, far_d);
            return c.mCamera;
        };

        add("perspective", origin, at, up, 1.047f, 1.87f, 0.1f, 256.f);
        add("axis-aligned (exact zeros in the normals)", origin, LLVector3(1.f, 0.f, 0.f), LLVector3(0.f, 0.f, 1.f), 1.047f,
            1.5f, 0.25f, 128.f);
        add("wide (160 degrees)", origin, at, up, 2.79f, 2.4f, 0.1f, 512.f);
        add("narrow (5 degrees)", origin, at, up, 0.0873f, 1.f, 1.f, 1024.f);
        {
            LLCamera& cam = add("user clip plane (water)", origin, at, up, 1.047f, 1.87f, 0.1f, 256.f);
            LLPlane water(LLVector3(0.f, 0.f, 20.f), LLVector3(0.f, 0.f, -1.f));
            cam.setUserClipPlane(water);
        }
        {
            LLCamera& cam = add("near plane ignored", origin, at, up, 1.047f, 1.87f, 0.1f, 256.f);
            cam.ignoreAgentFrustumPlane(LLCamera::AGENT_PLANE_NEAR);
        }
        {
            // A box-shaped (orthographic) frustum, as the sun shadow cascades use: an oriented box's corners.
            cams.emplace_back();
            TestCamera& c = cams.back();
            c.mName = "orthographic box (shadow cascade)";
            c.mAt = unit(LLVector3(0.3f, -0.4f, -0.87f));
            c.mFar = 300.f;
            const LLVector3 a = c.mAt;
            const LLVector3 u = unit(LLVector3(0.f, 1.f, 0.f) - a * (a * LLVector3(0.f, 1.f, 0.f)));
            const LLVector3 l = u % a;
            const LLVector3 o = origin - a * 150.f;
            c.mCamera.setOrigin(o);
            c.mCamera.setAxes(a, l, u);
            LLVector3 frust[8];
            for (S32 f = 0; f < 2; ++f)
            {
                const LLVector3 cc = o + a * (f ? 300.f : 1.f);
                frust[4 * f + 0] = cc + l * 80.f - u * 60.f;
                frust[4 * f + 1] = cc - l * 80.f - u * 60.f;
                frust[4 * f + 2] = cc - l * 80.f + u * 60.f;
                frust[4 * f + 3] = cc + l * 80.f + u * 60.f;
            }
            c.mCamera.calcAgentFrustumPlanes(frust);
        }
        {
            // Every corner in one point: zero normals (normVec of a zero vector), planes (0, 0, 0, -0), masks 7.
            cams.emplace_back();
            TestCamera& c = cams.back();
            c.mName = "degenerate (zero normals)";
            c.mAt = at;
            c.mCamera.setOrigin(origin);
            LLVector3 frust[8];
            for (LLVector3& p : frust)
            {
                p = origin;
            }
            c.mCamera.calcAgentFrustumPlanes(frust);
        }
        return cams;
    }

    // Boxes around a camera: most near or in its frustum (the three answers all occur), some anywhere, and every fifth one
    // with edge cases in its centre or radius (NaN, +-inf, denormals, -0, FLT_MAX, negative radii).
    void cameraBoxes(Rng& rng, const TestCamera& cam, std::vector<LLVector4a>& centers, std::vector<LLVector4a>& radii, size_t count)
    {
        const LLCamera& c = cam.mCamera;
        const LLVector3 o = c.getOrigin();
        const LLVector3 a = c.getAtAxis();
        const LLVector3 l = c.getLeftAxis();
        const LLVector3 u = c.getUpAxis();
        const F32 t = tanf(0.5f * llclamp(cam.mFov, 0.01f, 3.0f));
        centers.resize(count);
        radii.resize(count);
        for (size_t i = 0; i < count; ++i)
        {
            LLVector3 p;
            F32 size;
            if (rng.below(4) != 0)
            {
                const F32 d = rng.uniform(-0.1f * cam.mFar, 1.2f * cam.mFar);
                const F32 spread = 1.4f * t * llmax(d, 1.f);
                const F32 up = rng.uniform(-spread, spread); // last operand first (see Rng)
                const F32 left = rng.uniform(-spread * cam.mAspect, spread * cam.mAspect);
                p = o + a * d + l * left + u * up;
                size = rng.uniform(0.01f, 0.25f * spread + 2.f);
            }
            else
            {
                const F32 z = rng.uniform(-200.f, 600.f);
                const F32 y = rng.uniform(-1000.f, 1000.f);
                const F32 x = rng.uniform(-1000.f, 1000.f);
                p.setVec(x, y, z);
                size = rng.uniform(0.f, 120.f);
            }
            centers[i].set(p.mV[0], p.mV[1], p.mV[2], 0.f);
            const F32 rz = rng.uniform(0.2f, 1.f);
            const F32 ry = rng.uniform(0.2f, 1.f);
            const F32 rx = rng.uniform(0.2f, 1.f);
            radii[i].set(size * rx, size * ry, size * rz, 0.f);
            if (rng.below(5) == 0)
            {
                const S32 edges = 1 + rng.below(2);
                for (S32 e = 0; e < edges; ++e)
                {
                    LLVector4a& target = rng.below(2) ? centers[i] : radii[i];
                    F32* f = target.getF32ptr();
                    f[rng.below(3)] = rng.below(2) ? special(rng) : edgeValue(rng, 1000.f);
                }
            }
        }
    }

    void setBlock(LLSimdKernels::CenterRadius8& block, const LLVector4a* centers, const LLVector4a* radii)
    {
        for (S32 k = 0; k < 8; ++k)
        {
            block.set(k, centers[k], radii[k]);
        }
    }

    // The float next to f toward +inf (up) or -inf (down); NaN and the infinity in that direction stay as they are.
    F32 nextFloat(F32 f, bool up)
    {
        return std::nextafter(f, up ? std::numeric_limits<F32>::infinity() : -std::numeric_limits<F32>::infinity());
    }

    // The values a box is compared with, computed as the one-box code computes them (LLVector4a operations, same order):
    // for a plane, the dot of its nearest and farthest box corners; for the sphere test, the corners' squared distances
    // and the per-axis distance sums. Boundary probes put a plane distance or a squared radius exactly on these values
    // and on the floats next to them, so that any change in a kernel's arithmetic (operation order, fusion) flips a
    // decision somewhere, which random data almost never does.
    void planeDots(const LLPlane& plane, U8 mask, const LLVector4a& center, const LLVector4a& radius, F32& dmin, F32& dmax)
    {
        const LLVector4a scaler((mask & 1) ? 1.f : -1.f, (mask & 2) ? 1.f : -1.f, (mask & 4) ? 1.f : -1.f);
        LLVector4a rscale, minp, maxp;
        rscale.setMul(radius, scaler);
        minp.setSub(center, rscale);
        maxp.setAdd(center, rscale);
        dmin = plane.dot3(minp).getF32();
        dmax = plane.dot3(maxp).getF32();
    }

    void sphereSums(const LLVector4a& min, const LLVector4a& max, const LLVector3& origin, F32 sums[5])
    {
        LLVector4a origina;
        origina.load3(origin.mV);
        LLVector4a v;
        v.setSub(min, origina);
        sums[0] = v.dot3(v).getF32();
        v.setSub(max, origina);
        sums[1] = v.dot3(v).getF32();
        F32 d = 0.f;
        for (U32 i = 0; i < 3; i++)
        {
            if (origin.mV[i] < min[i])
            {
                const F32 t = min[i] - origin.mV[i];
                d += t * t;
            }
            else if (origin.mV[i] > max[i])
            {
                const F32 t = origin.mV[i] - max[i];
                d += t * t;
            }
            sums[2 + i] = d;
        }
    }

    // --- Micro-bench helpers ----------------------------------------------------------------------------------------

    typedef std::chrono::steady_clock BenchClock;

    // The best ns per item of 'trials' trials, each repeating fn until it has run at least 'min_ns'.
    template <typename F>
    double bestNsPerItem(F&& fn, double items, int trials = 7, double min_ns = 2.0e7)
    {
        double best = 1e300;
        for (int t = 0; t < trials; ++t)
        {
            size_t reps = 0;
            const BenchClock::time_point start = BenchClock::now();
            double elapsed = 0.0;
            do
            {
                fn();
                ++reps;
                elapsed = std::chrono::duration<double, std::nano>(BenchClock::now() - start).count();
            } while (elapsed < min_ns);
            best = std::min(best, elapsed / (double(reps) * items));
        }
        return best;
    }

    volatile U32 sBenchSink = 0;

    void sinkFloats(const F32* p, size_t n)
    {
        U32 x = 0;
        for (size_t i = 0; i < n; i += 61)
        {
            x ^= bitsOf(p[i]);
        }
        sBenchSink = sBenchSink + x;
    }
} // namespace

namespace tut
{
    struct llsimdkernels_data
    {
    };
    typedef test_group<llsimdkernels_data> llsimdkernels_group;
    typedef llsimdkernels_group::object object;
    llsimdkernels_group llsimdkernelsgrp("llsimdkernels");

    template<> template<>
    void object::test<1>()
    {
        set_test_name("K1 positions: bit for bit against the former loop, padding and the last transform included");
        const S32 counts[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 15, 16, 17, 31, 32, 33, 255, 256, 257, 1000, 1001, 4099 };
        const S32 pads[] = { 0, 1, 3 };
        const std::vector<FpCase> cases = fpCases();
        Rng rng(1);
        Tally tally;
        size_t vertices = 0;
        for (S32 kind = 0; kind < kMatrixKinds; ++kind)
        {
            const LLMatrix4a m = matrix(rng, kind);
            for (S32 count : counts)
            {
                const size_t src_phase = rng.below(2);
                const size_t dst_phase = 4 * rng.below(2); // floats: 0 or 16 bytes into a 32-byte line
                const S32 pad = pads[rng.below((U32)std::size(pads))];
                const S32 geom_count = count + pad;
                const S32 index = (S32)rng.below(16);
                const LLVector4a tex_idx = textureIndex(index);
                Vertices src(count, src_phase);
                for (S32 i = 0; i < count; ++i)
                {
                    src.mData[i] = vertex(rng, 256.f);
                }
                for (const FpCase& fp : cases)
                {
                    GuardedFloats ref(4 * geom_count, dst_phase);
                    GuardedFloats out(4 * geom_count, dst_phase);
                    LLVector4a ref_last;
                    LLVector4a out_last;
                    ref_last.set(fromBits(0x12345678), fromBits(0x9abcdef0), fromBits(0x0badf00d), fromBits(0x7f812345));
                    out_last = ref_last; // an untouched 'last' must stay what it was
                    {
                        ScopedFpCase env(fp);
                        referencePositions(m, src.mData, count, ref.mData, geom_count, tex_idx, ref_last);
                        kernelPositions(m, src.mData, count, out.mData, geom_count, tex_idx, out_last);
                        // The NaN meetings are counted under the same environment.
                        const std::string where = std::string(kMatrixNames[kind]) + ", " + std::to_string(count) + " vertices, " + fp.name();
                        for (S32 i = 0; i < geom_count; ++i)
                        {
                            const bool padding = i >= count;
                            for (S32 k = 0; k < 4; ++k)
                            {
                                const F32 e = ref.mData[4 * i + k];
                                const F32 a = out.mData[4 * i + k];
                                const std::string at = where + ", vertex " + std::to_string(i) + " lane " + std::to_string(k);
                                if (!padding && k == 3)
                                {
                                    tally.exact(bitsOf(tex_idx[3]), bitsOf(a), at + " (texture index)");
                                    tally.exact(bitsOf(e), bitsOf(a), at + " (texture index, reference)");
                                }
                                else if (padding && count == 0)
                                {
                                    tally.exact(bitsOf(e), bitsOf(a), at + " (padding from an untouched last)");
                                }
                                else
                                {
                                    const LLVector4a& v = src.mData[padding ? count - 1 : i];
                                    tally.check(e, a, affineNaNsMeet(v, m, k), at);
                                }
                            }
                        }
                        for (S32 k = 0; k < 4; ++k)
                        {
                            const std::string at = where + ", last lane " + std::to_string(k);
                            if (count == 0)
                            {
                                tally.exact(bitsOf(ref_last[k]), bitsOf(out_last[k]), at + " (untouched)");
                            }
                            else
                            {
                                tally.check(ref_last[k], out_last[k], affineNaNsMeet(src.mData[count - 1], m, k), at);
                            }
                        }
                    }
                    ensure("no write outside the reference's buffer", ref.guardsIntact());
                    ensure("no write outside the kernel's buffer: " + std::to_string(count) + " vertices", out.guardsIntact());
                    ensure(tally.mFailure, tally.mFailure.empty());
                    vertices += geom_count;
                }
            }
        }
        std::cout << tally.summary("K1 positions") << ", " << vertices << " vertices" << std::endl;
        ensure("the edge cases include NaNs that meet", tally.mNaNsMet > 0);
    }

    template<> template<>
    void object::test<2>()
    {
        set_test_name("K2 normals and tangents: bit for bit against the former loops");
        const S32 counts[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 15, 16, 17, 31, 32, 33, 255, 256, 257, 1000, 1001, 4099 };
        const std::vector<FpCase> cases = fpCases();
        Rng rng(2);
        Tally normals_tally;
        Tally tangents_tally;
        for (S32 kind = 0; kind < kMatrixKinds; ++kind)
        {
            const LLMatrix4a m = matrix(rng, kind);
            for (S32 count : counts)
            {
                const size_t src_phase = rng.below(2);
                const size_t dst_phase = 4 * rng.below(2);
                Vertices src(count, src_phase);
                for (S32 i = 0; i < count; ++i)
                {
                    src.mData[i] = vertex(rng, 1.5f);
                }
                for (const FpCase& fp : cases)
                {
                    GuardedFloats ref_n(4 * count, dst_phase);
                    GuardedFloats out_n(4 * count, dst_phase);
                    GuardedFloats ref_t(4 * count, dst_phase);
                    GuardedFloats out_t(4 * count, dst_phase);
                    {
                        ScopedFpCase env(fp);
                        referenceNormals(m, src.mData, count, ref_n.mData);
                        kernelNormals(m, src.mData, count, out_n.mData);
                        referenceTangents(m, src.mData, count, ref_t.mData);
                        kernelTangents(m, src.mData, count, out_t.mData);
                        const std::string where = std::string(kMatrixNames[kind]) + ", " + std::to_string(count) + " vertices, " + fp.name();
                        for (S32 i = 0; i < count; ++i)
                        {
                            for (S32 k = 0; k < 4; ++k)
                            {
                                const std::string at = where + ", vertex " + std::to_string(i) + " lane " + std::to_string(k);
                                const bool meet = rotateNaNsMeet(src.mData[i], m, k);
                                normals_tally.check(ref_n.mData[4 * i + k], out_n.mData[4 * i + k], meet, "normal: " + at);
                                if (k == 3)
                                {
                                    tangents_tally.exact(bitsOf(src.mData[i][3]), bitsOf(out_t.mData[4 * i + 3]), "tangent w: " + at);
                                    tangents_tally.exact(bitsOf(ref_t.mData[4 * i + 3]), bitsOf(out_t.mData[4 * i + 3]), "tangent w (reference): " + at);
                                }
                                else
                                {
                                    tangents_tally.check(ref_t.mData[4 * i + k], out_t.mData[4 * i + k], meet, "tangent: " + at);
                                }
                            }
                        }
                    }
                    ensure("no write outside the normal buffers", ref_n.guardsIntact() && out_n.guardsIntact());
                    ensure("no write outside the tangent buffers", ref_t.guardsIntact() && out_t.guardsIntact());
                    ensure(normals_tally.mFailure, normals_tally.mFailure.empty());
                    ensure(tangents_tally.mFailure, tangents_tally.mFailure.empty());
                }
            }
        }
        std::cout << normals_tally.summary("K2 normals") << std::endl;
        std::cout << tangents_tally.summary("K2 tangents") << std::endl;
        ensure("the edge cases include NaNs that meet", normals_tally.mNaNsMet > 0);
    }

    template<> template<>
    void object::test<3>()
    {
        set_test_name("K5 AABBInFrustum8 and AABBInFrustumNoFarClip8 against the one-box tests on real frusta");
        const size_t kBoxes = 4096;
        const std::vector<FpCase> cases = fpCases();
        std::vector<TestCamera> cams = testCameras();
        Rng rng(3);
        size_t compared = 0;
        size_t answers[3] = { 0, 0, 0 };
        for (TestCamera& tc : cams)
        {
            LLCamera& cam = tc.mCamera;
            std::vector<LLVector4a> centers, radii;
            cameraBoxes(rng, tc, centers, radii, kBoxes);

            // A second plane set with the camera's normals and shifted distances, as LLCamera::calcRegionFrustumPlanes makes
            // for a neighbouring region (passed as 'planes'; the masks stay the camera's).
            LLPlane shifted[LLCamera::AGENT_PLANE_USER_CLIP_NUM];
            const LLVector3 shift(256.f, -256.f, 0.f);
            for (U32 i = 0; i < LLCamera::AGENT_PLANE_USER_CLIP_NUM; ++i)
            {
                const LLPlane& p = cam.getAgentPlane(i);
                const LLVector3 n(p[0], p[1], p[2]);
                shifted[i].setVec(n, p[3] + n * shift);
            }

            size_t camera_answers[3] = { 0, 0, 0 };
            for (const FpCase& fp : cases)
            {
                ScopedFpCase env(fp);
                for (size_t b = 0; b < kBoxes; b += 8)
                {
                    LLSimdKernels::CenterRadius8 block;
                    setBlock(block, &centers[b], &radii[b]);
                    for (S32 variant = 0; variant < 4; ++variant)
                    {
                        const bool far_clip = (variant & 1) == 0;
                        const LLPlane* planes = (variant & 2) ? shifted : nullptr;
                        S32 out[8];
                        if (far_clip)
                        {
                            cam.AABBInFrustum8(block, out, planes);
                        }
                        else
                        {
                            cam.AABBInFrustumNoFarClip8(block, out, planes);
                        }
                        for (S32 k = 0; k < 8; ++k)
                        {
                            const S32 expected = far_clip ? cam.AABBInFrustum(centers[b + k], radii[b + k], planes)
                                                          : cam.AABBInFrustumNoFarClip(centers[b + k], radii[b + k], planes);
                            if (out[k] != expected)
                            {
                                const LLVector4a& c = centers[b + k];
                                const LLVector4a& r = radii[b + k];
                                fail(tc.mName + ", " + fp.name() + (far_clip ? ", AABBInFrustum" : ", AABBInFrustumNoFarClip") +
                                     (planes ? " (shifted planes)" : "") + ", box " + std::to_string(b + k) + ": expected " +
                                     std::to_string(expected) + ", got " + std::to_string(out[k]) + "; centre " + hex(bitsOf(c[0])) +
                                     " " + hex(bitsOf(c[1])) + " " + hex(bitsOf(c[2])) + ", radius " + hex(bitsOf(r[0])) + " " +
                                     hex(bitsOf(r[1])) + " " + hex(bitsOf(r[2])));
                            }
                            ++compared;
                            ++answers[llclamp(expected, 0, 2)];
                            ++camera_answers[llclamp(expected, 0, 2)];
                        }
                    }
                }
            }
            if (tc.mName == "perspective" || tc.mName == "user clip plane (water)" || tc.mName == "orthographic box (shadow cascade)")
            {
                ensure(tc.mName + ": boxes outside, cut and inside all occur",
                       camera_answers[0] > 0 && camera_answers[1] > 0 && camera_answers[2] > 0);
            }
        }
        // Boundary probes on the perspective camera: for box k of a block and plane i, a plane array whose plane i keeps the
        // camera's normal and gets the distance that puts the box's nearest or farthest corner exactly on it, or one float
        // to either side (the masks stay the camera's, which the normals decide).
        size_t probes = 0;
        {
            LLCamera& cam = cams[0].mCamera;
            std::vector<LLVector4a> centers, radii;
            Rng probe_rng(33);
            cameraBoxes(probe_rng, cams[0], centers, radii, 512);
            const U32 kPlanes = LLCamera::AGENT_PLANE_USER_CLIP_NUM;
            LLPlane planes[kPlanes];
            U8 masks[kPlanes];
            for (U32 i = 0; i < kPlanes; ++i)
            {
                planes[i] = cam.getAgentPlane(i);
                masks[i] = planes[i].calcPlaneMask();
            }
            for (const FpCase& fp : cases)
            {
                ScopedFpCase env(fp);
                for (size_t b = 0; b < centers.size(); b += 8)
                {
                    LLSimdKernels::CenterRadius8 block;
                    setBlock(block, &centers[b], &radii[b]);
                    for (S32 k = 0; k < 8; ++k)
                    {
                        for (U32 i = 0; i < LLCamera::AGENT_PLANE_NO_USER_CLIP_NUM; ++i)
                        {
                            F32 dots[2];
                            planeDots(planes[i], masks[i], centers[b + k], radii[b + k], dots[0], dots[1]);
                            for (F32 dot : dots)
                            {
                                for (S32 side = -1; side <= 1; ++side)
                                {
                                    LLPlane probe[kPlanes];
                                    std::copy(planes, planes + kPlanes, probe);
                                    const F32 d = side == 0 ? dot : nextFloat(dot, side > 0);
                                    probe[i].setVec(LLVector3(planes[i][0], planes[i][1], planes[i][2]), -d); // tested: dot > -w
                                    for (bool far_clip : { true, false })
                                    {
                                        S32 out[8];
                                        if (far_clip)
                                        {
                                            cam.AABBInFrustum8(block, out, probe);
                                        }
                                        else
                                        {
                                            cam.AABBInFrustumNoFarClip8(block, out, probe);
                                        }
                                        for (S32 j = 0; j < 8; ++j)
                                        {
                                            const S32 expected = far_clip ? cam.AABBInFrustum(centers[b + j], radii[b + j], probe)
                                                                          : cam.AABBInFrustumNoFarClip(centers[b + j], radii[b + j], probe);
                                            if (out[j] != expected)
                                            {
                                                fail("boundary probe, " + fp.name() + ", plane " + std::to_string(i) + " at " +
                                                     hex(bitsOf(d)) + ", box " + std::to_string(b + j) + ": expected " +
                                                     std::to_string(expected) + ", got " + std::to_string(out[j]));
                                            }
                                        }
                                        ++probes;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        std::cout << "K5 frustum: " << compared << " box tests identical (" << answers[0] << " outside, " << answers[1]
                  << " partly in, " << answers[2] << " inside), " << cams.size() << " cameras; " << probes
                  << " boundary probes of 8 boxes identical" << std::endl;
    }

    template<> template<>
    void object::test<4>()
    {
        set_test_name("K5 aabbSphereIntersect8 against aabbSphereIntersectR2, and the octree cull's whole test");
        const std::vector<FpCase> cases = fpCases();
        Rng rng(4);

        // The sphere half alone: boxes from realistic to inverted (min above max) and edge cases, origins near them,
        // squared radii from ordinary to 0, denormal, FLT_MAX, inf and NaN.
        size_t compared = 0;
        size_t answers[3] = { 0, 0, 0 };
        const size_t kBoxes = 4096;
        std::vector<LLVector4a> mins(kBoxes), maxs(kBoxes);
        for (size_t i = 0; i < kBoxes; ++i)
        {
            const F32 cz = rng.uniform(-50.f, 300.f); // last argument first (see Rng)
            const F32 cy = rng.uniform(-300.f, 300.f);
            const F32 cx = rng.uniform(-300.f, 300.f);
            const LLVector4a c(cx, cy, cz);
            const F32 s = rng.uniform(0.f, 60.f);
            const F32 rz = rng.uniform(0.1f, 1.f);
            const F32 ry = rng.uniform(0.1f, 1.f);
            const F32 rx = rng.uniform(0.1f, 1.f);
            LLVector4a r(s * rx, s * ry, s * rz);
            if (rng.below(10) == 0)
            {
                r.mul(-1.f); // inverted: min above max
            }
            mins[i].setSub(c, r);
            maxs[i].setAdd(c, r);
            if (rng.below(5) == 0)
            {
                LLVector4a& target = rng.below(2) ? mins[i] : maxs[i];
                target.getF32ptr()[rng.below(3)] = rng.below(2) ? special(rng) : edgeValue(rng, 400.f);
            }
        }
        const F32 radii[] = { 0.f, fromBits(0x00000001), 1.f, 100.f, 250.f, 400.f, 1.0e19f, fromBits(0x7f7fffff),
                              fromBits(0x7f800000), fromBits(0x7fc00000) };
        for (const FpCase& fp : cases)
        {
            ScopedFpCase env(fp);
            for (S32 o = 0; o < 6; ++o)
            {
                const F32 oz = rng.uniform(-50.f, 300.f); // last argument first (see Rng)
                const F32 oy = rng.uniform(-300.f, 300.f);
                const F32 ox = rng.uniform(-300.f, 300.f);
                LLVector3 origin(ox, oy, oz);
                if (o == 5)
                {
                    origin.mV[rng.below(3)] = special(rng);
                }
                for (F32 rad : radii)
                {
                    const F32 r2 = rad * rad; // as AABBSphereIntersect squares its radius
                    for (size_t b = 0; b < kBoxes; b += 8)
                    {
                        LLSimdKernels::MinMax8 block;
                        for (S32 k = 0; k < 8; ++k)
                        {
                            block.set(k, mins[b + k], maxs[b + k]);
                        }
                        S32 out[8];
                        LLSimdKernels::aabbSphereIntersect8(block, origin, r2, out);
                        for (S32 k = 0; k < 8; ++k)
                        {
                            const S32 expected = LLSimdKernels::aabbSphereIntersectR2(mins[b + k], maxs[b + k], origin, r2);
                            if (out[k] != expected)
                            {
                                fail("sphere, " + fp.name() + ", radius " + hex(bitsOf(rad)) + ", origin " + hex(bitsOf(origin.mV[0])) +
                                     " " + hex(bitsOf(origin.mV[1])) + " " + hex(bitsOf(origin.mV[2])) + ", box " + std::to_string(b + k) +
                                     ": expected " + std::to_string(expected) + ", got " + std::to_string(out[k]));
                            }
                            ++compared;
                            ++answers[llclamp(expected, 0, 2)];
                        }
                    }
                }
            }
        }
        ensure("sphere: outside, cut and inside all occur", answers[0] > 0 && answers[1] > 0 && answers[2] > 0);

        // Boundary probes: squared radii exactly on a box's corner distances and per-axis sums, and on the floats next to
        // them; the origin near the box, so that some axes lie inside its slab and add nothing.
        size_t probes = 0;
        for (const FpCase& fp : cases)
        {
            ScopedFpCase env(fp);
            Rng probe_rng(44);
            for (size_t b = 0; b < 1024; b += 8)
            {
                LLSimdKernels::MinMax8 block;
                for (S32 k = 0; k < 8; ++k)
                {
                    block.set(k, mins[b + k], maxs[b + k]);
                }
                LLVector4a mid;
                mid.setAdd(mins[b], maxs[b]);
                mid.mul(0.5f);
                const F32 dz = probe_rng.uniform(-40.f, 40.f); // last argument first (see Rng)
                const F32 dy = probe_rng.uniform(-40.f, 40.f);
                const F32 dx = probe_rng.uniform(-40.f, 40.f);
                const LLVector3 origin(mid[0] + dx, mid[1] + dy, mid[2] + dz);
                for (S32 k = 0; k < 8; ++k)
                {
                    F32 sums[5];
                    sphereSums(mins[b + k], maxs[b + k], origin, sums);
                    for (F32 s : sums)
                    {
                        for (S32 side = -1; side <= 1; ++side)
                        {
                            const F32 r2 = side == 0 ? s : nextFloat(s, side > 0);
                            S32 out[8];
                            LLSimdKernels::aabbSphereIntersect8(block, origin, r2, out);
                            for (S32 j = 0; j < 8; ++j)
                            {
                                const S32 expected = LLSimdKernels::aabbSphereIntersectR2(mins[b + j], maxs[b + j], origin, r2);
                                if (out[j] != expected)
                                {
                                    fail("sphere boundary probe, " + fp.name() + ", squared radius " + hex(bitsOf(r2)) + ", box " +
                                         std::to_string(b + j) + ": expected " + std::to_string(expected) + ", got " +
                                         std::to_string(out[j]));
                                }
                            }
                            ++probes;
                        }
                    }
                }
            }
        }

        // The octree cull's whole test, LLOctreeCull::frustumCheck: AABBInFrustumNoFarClip on the bounds, then, unless the
        // box is outside, the lower of that and the sphere test on the extents (the camera's origin and corner distance).
        size_t whole = 0;
        std::vector<TestCamera> cams = testCameras();
        for (TestCamera& tc : cams)
        {
            LLCamera& cam = tc.mCamera;
            std::vector<LLVector4a> centers, half;
            cameraBoxes(rng, tc, centers, half, 2048);
            const LLVector3 origin = cam.getOrigin();
            const F32 r2 = cam.mFrustumCornerDist * cam.mFrustumCornerDist;
            for (const FpCase& fp : cases)
            {
                ScopedFpCase env(fp);
                for (size_t b = 0; b < centers.size(); b += 8)
                {
                    LLSimdKernels::CenterRadius8 bounds;
                    LLSimdKernels::MinMax8 extents;
                    LLVector4a lo[8], hi[8];
                    for (S32 k = 0; k < 8; ++k)
                    {
                        bounds.set(k, centers[b + k], half[b + k]);
                        lo[k].setSub(centers[b + k], half[b + k]);
                        hi[k].setAdd(centers[b + k], half[b + k]);
                        extents.set(k, lo[k], hi[k]);
                    }
                    S32 frustum[8];
                    S32 sphere[8];
                    cam.AABBInFrustumNoFarClip8(bounds, frustum);
                    LLSimdKernels::aabbSphereIntersect8(extents, origin, r2, sphere);
                    for (S32 k = 0; k < 8; ++k)
                    {
                        S32 expected = cam.AABBInFrustumNoFarClip(centers[b + k], half[b + k]);
                        if (expected != 0)
                        {
                            expected = llmin(expected, LLSimdKernels::aabbSphereIntersectR2(lo[k], hi[k], origin, r2));
                        }
                        const S32 got = frustum[k] != 0 ? llmin(frustum[k], sphere[k]) : 0;
                        if (got != expected)
                        {
                            fail("whole cull test, " + tc.mName + ", " + fp.name() + ", box " + std::to_string(b + k) + ": expected " +
                                 std::to_string(expected) + ", got " + std::to_string(got));
                        }
                        ++whole;
                    }
                }
            }
        }
        std::cout << "K5 sphere: " << compared << " box tests identical (" << answers[0] << " outside, " << answers[1]
                  << " cut, " << answers[2] << " inside), " << probes << " boundary probes of 8 boxes identical; whole cull test: "
                  << whole << " identical" << std::endl;
    }

    template<> template<>
    void object::test<5>()
    {
        set_test_name("micro-bench (opt-in: LL_SIMD_KERNELS_BENCH=1)");
        const char* bench = getenv("LL_SIMD_KERNELS_BENCH");
        if (!bench || !*bench || *bench == '0')
        {
            return;
        }
#if TEST_HAS_MXCSR
        const FpCase viewer = { true, false, 0 }; // the main thread's state: flush-to-zero, round to nearest
        ScopedFpCase env(viewer);
#endif
#if defined(__AVX2__)
        std::cout << "\nLLSimdKernels micro-bench, AVX2 kernels against the SSE code they replace (best of 7 trials)\n";
#else
        std::cout << "\nLLSimdKernels micro-bench: this build does not target AVX2, so both columns run the SSE code\n";
#endif
        Rng rng(5);
        const LLMatrix4a m = matrix(rng, 0);
        const LLVector4a tex_idx = textureIndex(3);
        const S32 sizes[] = { 24, 384, 1500, 10000, 65536 };
        std::cout << "kernel      vertices  dst phase  SSE ns/vertex  AVX2 ns/vertex  speed-up  SSE us/call  AVX2 us/call\n";
        for (S32 n : sizes)
        {
            Vertices src(n, 0);
            for (S32 i = 0; i < n; ++i)
            {
                const F32 w = rng.uniform(-1.f, 1.f); // last argument first (see Rng)
                const F32 z = rng.uniform(-64.f, 64.f);
                const F32 y = rng.uniform(-64.f, 64.f);
                const F32 x = rng.uniform(-64.f, 64.f);
                src.mData[i].set(x, y, z, w);
            }
            for (size_t phase : { size_t(0), size_t(4) })
            {
                GuardedFloats a(4 * n, phase);
                GuardedFloats b(4 * n, phase);
                LLVector4a last_a, last_b;
                const double pos_ref = bestNsPerItem([&] { referencePositions(m, src.mData, n, a.mData, n, tex_idx, last_a); }, n);
                const double pos_new = bestNsPerItem([&] { kernelPositions(m, src.mData, n, b.mData, n, tex_idx, last_b); }, n);
                const double nrm_ref = bestNsPerItem([&] { referenceNormals(m, src.mData, n, a.mData); }, n);
                const double nrm_new = bestNsPerItem([&] { kernelNormals(m, src.mData, n, b.mData); }, n);
                const double tan_ref = bestNsPerItem([&] { referenceTangents(m, src.mData, n, a.mData); }, n);
                const double tan_new = bestNsPerItem([&] { kernelTangents(m, src.mData, n, b.mData); }, n);
                sinkFloats(a.mData, 4 * n);
                sinkFloats(b.mData, 4 * n);
                const struct { const char* name; double ref, now; } rows[] =
                {
                    { "K1 position", pos_ref, pos_new }, { "K2 normal  ", nrm_ref, nrm_new }, { "K2 tangent ", tan_ref, tan_new },
                };
                for (const auto& row : rows)
                {
                    char line[200];
                    std::snprintf(line, sizeof(line), "%s %9d  %9d  %13.3f  %14.3f  %7.2fx  %11.3f  %12.3f\n", row.name, n,
                                  int(phase * 4), row.ref, row.now, row.ref / row.now, row.ref * n / 1000.0, row.now * n / 1000.0);
                    std::cout << line;
                }
            }
        }

        // K5: the camera's real planes, boxes near its frustum (all three answers), one box at a time against eight.
        std::vector<TestCamera> cams = testCameras();
        LLCamera& cam = cams[0].mCamera;
        const size_t kBoxes = 8192;
        std::vector<LLVector4a> centers, radii;
        Rng box_rng(6);
        cameraBoxes(box_rng, cams[0], centers, radii, kBoxes);
        for (size_t i = 0; i < kBoxes; ++i)
        {
            // ordinary boxes only: the bench measures the common case
            for (S32 a = 0; a < 3; ++a)
            {
                if (!std::isfinite(centers[i][a]) || !std::isfinite(radii[i][a]) || radii[i][a] < 0.f)
                {
                    centers[i].getF32ptr()[a] = 128.f;
                    radii[i].getF32ptr()[a] = 1.f;
                }
            }
        }
        std::vector<LLSimdKernels::CenterRadius8> blocks(kBoxes / 8);
        std::vector<LLSimdKernels::MinMax8> extents(kBoxes / 8);
        std::vector<LLVector4a> mins(kBoxes), maxs(kBoxes);
        for (size_t b = 0; b < kBoxes; b += 8)
        {
            setBlock(blocks[b / 8], &centers[b], &radii[b]);
            for (S32 k = 0; k < 8; ++k)
            {
                mins[b + k].setSub(centers[b + k], radii[b + k]);
                maxs[b + k].setAdd(centers[b + k], radii[b + k]);
                extents[b / 8].set(k, mins[b + k], maxs[b + k]);
            }
        }
        std::vector<S32> results(kBoxes);
        size_t answers[3] = { 0, 0, 0 };
        for (size_t i = 0; i < kBoxes; ++i)
        {
            ++answers[cam.AABBInFrustumNoFarClip(centers[i], radii[i])];
        }
        const double one = bestNsPerItem([&]
        {
            for (size_t i = 0; i < kBoxes; ++i)
            {
                results[i] = cam.AABBInFrustumNoFarClip(centers[i], radii[i]);
            }
        }, double(kBoxes));
        const double eight = bestNsPerItem([&]
        {
            for (size_t b = 0; b < kBoxes; b += 8)
            {
                cam.AABBInFrustumNoFarClip8(blocks[b / 8], &results[b]);
            }
        }, double(kBoxes));
        const double gather = bestNsPerItem([&]
        {
            LLSimdKernels::CenterRadius8 block;
            for (size_t b = 0; b < kBoxes; b += 8)
            {
                setBlock(block, &centers[b], &radii[b]);
                cam.AABBInFrustumNoFarClip8(block, &results[b]);
            }
        }, double(kBoxes));
        const LLVector3 origin = cam.getOrigin();
        const F32 r2 = cam.mFrustumCornerDist * cam.mFrustumCornerDist;
        const double sphere_one = bestNsPerItem([&]
        {
            for (size_t i = 0; i < kBoxes; ++i)
            {
                results[i] = LLSimdKernels::aabbSphereIntersectR2(mins[i], maxs[i], origin, r2);
            }
        }, double(kBoxes));
        const double sphere_eight = bestNsPerItem([&]
        {
            for (size_t b = 0; b < kBoxes; b += 8)
            {
                LLSimdKernels::aabbSphereIntersect8(extents[b / 8], origin, r2, &results[b]);
            }
        }, double(kBoxes));
        U32 x = 0;
        for (S32 r : results)
        {
            x += (U32)r;
        }
        sBenchSink = sBenchSink + x;
        char line[400];
        std::snprintf(line, sizeof(line),
                      "K5 AABBInFrustumNoFarClip, %zu boxes (%zu outside, %zu partly in, %zu inside): one at a time %.2f ns/box, "
                      "eight per call %.2f ns/box (%.2fx), eight with the gather from LLVector4a pairs %.2f ns/box (%.2fx)\n"
                      "K5 sphere half: one at a time %.2f ns/box, eight per call %.2f ns/box (%.2fx)\n",
                      kBoxes, answers[0], answers[1], answers[2], one, eight, one / eight, gather, one / gather, sphere_one,
                      sphere_eight, sphere_one / sphere_eight);
        std::cout << line << std::endl;
    }
}
