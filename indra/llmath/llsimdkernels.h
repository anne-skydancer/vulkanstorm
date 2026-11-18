// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// LLSimdKernels: the bit-exact AVX2 kernels of the multi-core study's stage 3 (doc/perf/multicore-simd-study.md sections
// 4.2, 4.3 and 6.3).
//
//   K1  affineTransformPositions  LLMatrix4a::affineTransformSSE over a face's positions, w = the texture index
//                                 (LLFace::getGeometryVolume)
//   K2  rotateNormals             LLMatrix4a::rotate over a face's normals (LLFace::getGeometryVolume)
//       rotateTangents            LLMatrix4a::rotate over a face's tangents, w kept (LLFace::getGeometryVolume)
//   K5  aabbInFrustum8            LLCamera::AABBInFrustum / AABBInFrustumNoFarClip for eight boxes
//       aabbSphereIntersect8      AABBSphereIntersectR2 (the octree cull's sphere half) for eight boxes
//
// Bit-exact. K1 and K2 put two vertices in one 256-bit register, K5 one box in each of eight lanes. Every lane runs the
// operations of the SSE code it replaces, in the same order and with the same operand first: multiply and add stay
// separate (no FMA, study section 9), nothing is summed across lanes, and the selects move bits. The results are therefore
// the SSE code's bits for every input, NaN, +-inf, denormals and -0 included, under any MXCSR (flush-to-zero or not, any
// rounding mode). One case is IEEE's: where two NaNs meet in one multiply or add, x86 returns the first operand's NaN and
// the compiler may swap the operands of either version, so there "a NaN" is guaranteed, not which one. The tests
// (llmath/tests/llsimdkernels_test.cpp) check every other output bit for bit against the SSE code.
//
// AVX2 only, no runtime dispatch (owner decision 2026-11-10): a build for x86-64-v3 (__AVX2__: /arch:AVX2,
// -march=x86-64-v3, i.e. USE_AVX2_OPTIMIZATION) compiles the AVX2 loops; any other build (without that option, or ARM
// through sse2neon) compiles the SSE code the kernels replace, unchanged.
//
// Threads: pure functions of their arguments, no shared state; they may run on LL::FrameJobs helpers, which take over
// the owner's MXCSR.
#ifndef LL_LLSIMDKERNELS_H
#define LL_LLSIMDKERNELS_H

#include "llmath.h" // llsimdmath.h: LLVector4a, LLVector4Logical and LLSimdScalar, in their order
#include "llmatrix4a.h"
#include "llplane.h"
#include "v3math.h"

#if defined(__AVX2__)
#include <immintrin.h>
#endif

namespace LLSimdKernels
{
#if defined(__AVX2__)
    namespace detail
    {
        // The four floats of v in both 128-bit halves.
        inline __m256 bothHalves(const LLVector4a& v)
        {
            const __m128 q = v;
            return _mm256_insertf128_ps(_mm256_castps128_ps256(q), q, 1);
        }

        // LLMatrix4a::affineTransformSSE for the two vertices in v: (x * r0 + y * r1) + (z * r2 + r3), the SSE order.
        inline __m256 affine2(__m256 v, __m256 r0, __m256 r1, __m256 r2, __m256 r3)
        {
            __m256 x = _mm256_permute_ps(v, _MM_SHUFFLE(0, 0, 0, 0));
            __m256 y = _mm256_permute_ps(v, _MM_SHUFFLE(1, 1, 1, 1));
            __m256 z = _mm256_permute_ps(v, _MM_SHUFFLE(2, 2, 2, 2));
            x = _mm256_mul_ps(x, r0);
            y = _mm256_mul_ps(y, r1);
            z = _mm256_mul_ps(z, r2);
            x = _mm256_add_ps(x, y);
            z = _mm256_add_ps(z, r3);
            return _mm256_add_ps(x, z);
        }

        // LLMatrix4a::rotate for the two vertices in v: (x * r0 + y * r1) + z * r2, the SSE order.
        inline __m256 rotate2(__m256 v, __m256 r0, __m256 r1, __m256 r2)
        {
            __m256 x = _mm256_permute_ps(v, _MM_SHUFFLE(0, 0, 0, 0));
            __m256 y = _mm256_permute_ps(v, _MM_SHUFFLE(1, 1, 1, 1));
            __m256 z = _mm256_permute_ps(v, _MM_SHUFFLE(2, 2, 2, 2));
            x = _mm256_mul_ps(x, r0);
            y = _mm256_mul_ps(y, r1);
            z = _mm256_mul_ps(z, r2);
            x = _mm256_add_ps(x, y);
            return _mm256_add_ps(x, z);
        }
    }
#endif

    // --- K1 and K2: a face's vertex streams ----------------------------------------------------------------------------
    // src holds count LLVector4a; dst receives count * 4 floats and must be 16-byte aligned (the LLVertexBuffer streams
    // are). Bits as the SSE loops of LLFace::getGeometryVolume they replace.

    // K1: dst[i] = m.affineTransformSSE(src[i]), ((x * r0 + y * r1) + (z * r2 + r3)), with w replaced by the w of
    // tex_index (its bits: LLFace keeps the texture index in position.w). 'last' receives the transform of src[count - 1]
    // with its computed w, which LLFace repeats into the face's padding; it is left as it was when count is 0.
    inline void affineTransformPositions(const LLMatrix4a& m, const LLVector4a* src, F32* dst, S32 count,
                                         const LLVector4a& tex_index, LLVector4a& last)
    {
        S32 i = 0;
#if defined(__AVX2__)
        if (count >= 2)
        {
            const __m256 r0 = detail::bothHalves(m.mMatrix[0]);
            const __m256 r1 = detail::bothHalves(m.mMatrix[1]);
            const __m256 r2 = detail::bothHalves(m.mMatrix[2]);
            const __m256 r3 = detail::bothHalves(m.mMatrix[3]);
            const __m256 tex = detail::bothHalves(tex_index);
            const F32* s = src->getF32ptr();
            const F32* const s_end4 = s + 4 * (count & ~3);
            F32* d = dst;
            __m256 res = _mm256_setzero_ps();
            for (; s < s_end4; s += 16, d += 16) // four vertices: two registers
            {
                const __m256 a = detail::affine2(_mm256_loadu_ps(s), r0, r1, r2, r3);
                res = detail::affine2(_mm256_loadu_ps(s + 8), r0, r1, r2, r3);
                _mm256_storeu_ps(d, _mm256_blend_ps(a, tex, 0x88)); // lanes 3 and 7 from tex
                _mm256_storeu_ps(d + 8, _mm256_blend_ps(res, tex, 0x88));
            }
            if (count & 2)
            {
                res = detail::affine2(_mm256_loadu_ps(s), r0, r1, r2, r3);
                _mm256_storeu_ps(d, _mm256_blend_ps(res, tex, 0x88));
            }
            i = count & ~1;
            if (i == count)
            {
                last = _mm256_extractf128_ps(res, 1);
            }
        }
#endif
        if (i < count)
        {
            LLVector4Logical mask;
            mask.clear();
            mask.setElement<3>();
            LLVector4a res0;
            LLVector4a tmp;
            for (; i < count; ++i)
            {
                m.affineTransformSSE(src[i], res0);
                tmp.setSelectWithMask(mask, tex_index, res0);
                tmp.store4a(dst + 4 * i);
            }
            last = res0;
        }
    }

    namespace detail
    {
        // m.rotate(src[i]), ((x * r0 + y * r1) + z * r2); with KEEP_W the w of src[i] replaces the computed one.
        template <bool KEEP_W>
        inline void rotate(const LLMatrix4a& m, const LLVector4a* src, F32* dst, S32 count)
        {
            S32 i = 0;
#if defined(__AVX2__)
            if (count >= 2)
            {
                const __m256 r0 = bothHalves(m.mMatrix[0]);
                const __m256 r1 = bothHalves(m.mMatrix[1]);
                const __m256 r2 = bothHalves(m.mMatrix[2]);
                const F32* s = src->getF32ptr();
                F32* d = dst;
                // Two vertices; with KEEP_W lanes 3 and 7 come from the source.
                auto two = [&](const F32* from, F32* to)
                {
                    const __m256 v = _mm256_loadu_ps(from);
                    __m256 x = rotate2(v, r0, r1, r2);
                    if constexpr (KEEP_W)
                    {
                        x = _mm256_blend_ps(x, v, 0x88);
                    }
                    _mm256_storeu_ps(to, x);
                };
                const F32* const s_end4 = s + 4 * (count & ~3);
                for (; s < s_end4; s += 16, d += 16) // four vertices: two registers
                {
                    two(s, d);
                    two(s + 8, d + 8);
                }
                if (count & 2)
                {
                    two(s, d);
                }
                i = count & ~1;
            }
#endif
            if (i < count)
            {
                LLVector4Logical mask;
                mask.clear();
                mask.setElement<3>();
                for (; i < count; ++i)
                {
                    LLVector4a out;
                    m.rotate(src[i], out);
                    if constexpr (KEEP_W)
                    {
                        out.setSelectWithMask(mask, src[i], out);
                    }
                    out.store4a(dst + 4 * i);
                }
            }
        }
    }

    // K2: dst[i] = m.rotate(src[i]), the normal stream.
    inline void rotateNormals(const LLMatrix4a& m, const LLVector4a* src, F32* dst, S32 count)
    {
        detail::rotate<false>(m, src, dst, count);
    }

    // K2: dst[i] = m.rotate(src[i]) with the w of src[i] (the bitangent sign), the tangent stream.
    inline void rotateTangents(const LLMatrix4a& m, const LLVector4a* src, F32* dst, S32 count)
    {
        detail::rotate<true>(m, src, dst, count);
    }

    // --- K5: eight boxes against a frustum and a sphere ------------------------------------------------------------------
    // One box per lane, structure of arrays. Lanes without a box may hold any value; their results are not meaningful.

    // Boxes as centre and half size per axis (LLCamera's 'center' and 'radius').
    struct alignas(32) CenterRadius8
    {
        F32 mCenter[3][8];
        F32 mRadius[3][8];

        void set(S32 lane, const LLVector4a& center, const LLVector4a& radius)
        {
            for (S32 a = 0; a < 3; ++a)
            {
                mCenter[a][lane] = center[a];
                mRadius[a][lane] = radius[a];
            }
        }
    };

    // Boxes as min and max corners (the octree groups' extents).
    struct alignas(32) MinMax8
    {
        F32 mMin[3][8];
        F32 mMax[3][8];

        void set(S32 lane, const LLVector4a& min, const LLVector4a& max)
        {
            for (S32 a = 0; a < 3; ++a)
            {
                mMin[a][lane] = min[a];
                mMax[a][lane] = max[a];
            }
        }
    };

#if defined(__AVX2__)
    namespace detail
    {
        // LLCamera's sFrustumScaler: per plane octant mask, the sign of the box corner along each axis.
        alignas(16) inline constexpr F32 kFrustumScaler[8][4] =
        {
            { -1.f, -1.f, -1.f, 0.f }, { 1.f, -1.f, -1.f, 0.f }, { -1.f, 1.f, -1.f, 0.f }, { 1.f, 1.f, -1.f, 0.f },
            { -1.f, -1.f,  1.f, 0.f }, { 1.f, -1.f,  1.f, 0.f }, { -1.f, 1.f,  1.f, 0.f }, { 1.f, 1.f,  1.f, 0.f },
        };
    }

    // K5: out[k] = what LLCamera::AABBInFrustum returns for box k (0 outside, 1 partly in, 2 fully in), tested against
    // planes [0, plane_count) whose octant mask is below 8, except skip_plane (LLCamera::AGENT_PLANE_FAR for
    // AABBInFrustumNoFarClip; an index of plane_count or more for none). Callers use LLCamera::AABBInFrustum8 and
    // AABBInFrustumNoFarClip8, which pass the camera's planes and fall back to the one-box functions without AVX2. The SSE
    // loop leaves at the first plane the box lies outside of; here every plane is tested and the decisions are ORed, which
    // gives the same answer: 0 when any plane has the box outside, else 1 when any plane cuts it, else 2.
    inline void aabbInFrustum8(const LLPlane* planes, const U8* masks, U32 plane_count, U32 skip_plane,
                               const CenterRadius8& boxes, S32* out)
    {
        const __m256 cx = _mm256_load_ps(boxes.mCenter[0]);
        const __m256 cy = _mm256_load_ps(boxes.mCenter[1]);
        const __m256 cz = _mm256_load_ps(boxes.mCenter[2]);
        const __m256 rx = _mm256_load_ps(boxes.mRadius[0]);
        const __m256 ry = _mm256_load_ps(boxes.mRadius[1]);
        const __m256 rz = _mm256_load_ps(boxes.mRadius[2]);
        __m256 outside = _mm256_setzero_ps();
        __m256 partial = _mm256_setzero_ps();
        for (U32 i = 0; i < plane_count; i++)
        {
            const U8 mask = masks[i];
            if (i == skip_plane || mask >= 8)
            {
                continue;
            }
            const LLPlane& p(planes[i]);
            const __m256 px = _mm256_set1_ps(p[0]);
            const __m256 py = _mm256_set1_ps(p[1]);
            const __m256 pz = _mm256_set1_ps(p[2]);
            const __m256 d = _mm256_set1_ps(-p[3]);
            // rscale = radius * scaler; minp = center - rscale; maxp = center + rscale
            const F32* scaler = detail::kFrustumScaler[mask];
            const __m256 rsx = _mm256_mul_ps(rx, _mm256_broadcast_ss(scaler + 0));
            const __m256 rsy = _mm256_mul_ps(ry, _mm256_broadcast_ss(scaler + 1));
            const __m256 rsz = _mm256_mul_ps(rz, _mm256_broadcast_ss(scaler + 2));
            // dot3: (p.x * v.x + p.y * v.y) + p.z * v.z (LLVector4a::dot3, plane first)
            const __m256 dmin = _mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(px, _mm256_sub_ps(cx, rsx)),
                                                            _mm256_mul_ps(py, _mm256_sub_ps(cy, rsy))),
                                              _mm256_mul_ps(pz, _mm256_sub_ps(cz, rsz)));
            outside = _mm256_or_ps(outside, _mm256_cmp_ps(dmin, d, _CMP_GT_OQ));
            const __m256 dmax = _mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(px, _mm256_add_ps(cx, rsx)),
                                                            _mm256_mul_ps(py, _mm256_add_ps(cy, rsy))),
                                              _mm256_mul_ps(pz, _mm256_add_ps(cz, rsz)));
            partial = _mm256_or_ps(partial, _mm256_cmp_ps(dmax, d, _CMP_GT_OQ));
        }
        // 2, minus 1 where a plane cuts the box (a true compare is -1), and 0 where a plane has it outside
        const __m256i answer = _mm256_andnot_si256(_mm256_castps_si256(outside),
                                                   _mm256_add_epi32(_mm256_set1_epi32(2), _mm256_castps_si256(partial)));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(out), answer);
    }
#endif

    // The octree cull's sphere half for one box: 2 when both corners lie inside the sphere (squared distance below
    // radius_squared), 0 when the box lies outside it, else 1. This is AABBSphereIntersectR2 for LLVector4a boxes, moved
    // here unchanged from llvieweroctree.cpp, which now calls it, so that the eight-box kernel is tested against it.
    inline S32 aabbSphereIntersectR2(const LLVector4a& min, const LLVector4a& max, const LLVector3 &origin, const F32 &r)
    {
        F32 d = 0.f;
        F32 t;

        LLVector4a origina;
        origina.load3(origin.mV);

        LLVector4a v;
        v.setSub(min, origina);

        if (v.dot3(v) < r)
        {
            v.setSub(max, origina);
            if (v.dot3(v) < r)
            {
                return 2;
            }
        }


        for (U32 i = 0; i < 3; i++)
        {
            if (origin.mV[i] < min[i])
            {
                t = min[i] - origin.mV[i];
                d += t*t;
            }
            else if (origin.mV[i] > max[i])
            {
                t = origin.mV[i] - max[i];
                d += t*t;
            }

            if (d > r)
            {
                return 0;
            }
        }

        return 1;
    }

    // K5: out[k] = aabbSphereIntersectR2(min_k, max_k, origin, radius_squared). In a lane where an axis adds nothing the
    // sum gets + 0, which leaves it unchanged: it starts at +0 and only ever grows by a square, never by NaN (a comparison
    // with NaN is false, so no NaN difference is squared).
    inline void aabbSphereIntersect8(const MinMax8& boxes, const LLVector3& origin, F32 radius_squared, S32* out)
    {
#if defined(__AVX2__)
        const __m256 r = _mm256_set1_ps(radius_squared);
        const __m256 zero = _mm256_setzero_ps();
        const __m256 mn[3] = { _mm256_load_ps(boxes.mMin[0]), _mm256_load_ps(boxes.mMin[1]), _mm256_load_ps(boxes.mMin[2]) };
        const __m256 mx[3] = { _mm256_load_ps(boxes.mMax[0]), _mm256_load_ps(boxes.mMax[1]), _mm256_load_ps(boxes.mMax[2]) };
        // origina.load3(origin) is (x, y, z, 0); v = corner - origina; dot3(v, v) = (vx * vx + vy * vy) + vz * vz
        const __m256 o[3] = { _mm256_set1_ps(origin.mV[0]), _mm256_set1_ps(origin.mV[1]), _mm256_set1_ps(origin.mV[2]) };
        __m256 v[3];
        for (S32 a = 0; a < 3; ++a)
        {
            v[a] = _mm256_sub_ps(mn[a], o[a]);
        }
        const __m256 dmin = _mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(v[0], v[0]), _mm256_mul_ps(v[1], v[1])),
                                          _mm256_mul_ps(v[2], v[2]));
        for (S32 a = 0; a < 3; ++a)
        {
            v[a] = _mm256_sub_ps(mx[a], o[a]);
        }
        const __m256 dmax = _mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(v[0], v[0]), _mm256_mul_ps(v[1], v[1])),
                                          _mm256_mul_ps(v[2], v[2]));
        const __m256 inside = _mm256_and_ps(_mm256_cmp_ps(dmin, r, _CMP_LT_OQ), _mm256_cmp_ps(dmax, r, _CMP_LT_OQ));

        __m256 d = zero;
        __m256 outside = zero;
        for (S32 a = 0; a < 3; ++a)
        {
            const __m256 below = _mm256_cmp_ps(o[a], mn[a], _CMP_LT_OQ); // origin < min: t = min - origin
            const __m256 above = _mm256_cmp_ps(o[a], mx[a], _CMP_GT_OQ); // else origin > max: t = origin - max
            __m256 t = _mm256_blendv_ps(zero, _mm256_sub_ps(o[a], mx[a]), above);
            t = _mm256_blendv_ps(t, _mm256_sub_ps(mn[a], o[a]), below);
            d = _mm256_add_ps(d, _mm256_mul_ps(t, t));
            outside = _mm256_or_ps(outside, _mm256_cmp_ps(d, r, _CMP_GT_OQ));
        }
        // 1, plus 1 where both corners are inside (a true compare is -1), and 0 where the box is outside but not inside
        const __m256i inside_i = _mm256_castps_si256(inside);
        const __m256i answer = _mm256_andnot_si256(_mm256_andnot_si256(inside_i, _mm256_castps_si256(outside)),
                                                   _mm256_sub_epi32(_mm256_set1_epi32(1), inside_i));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(out), answer);
#else
        for (S32 k = 0; k < 8; ++k)
        {
            const LLVector4a min(boxes.mMin[0][k], boxes.mMin[1][k], boxes.mMin[2][k]);
            const LLVector4a max(boxes.mMax[0][k], boxes.mMax[1][k], boxes.mMax[2][k]);
            out[k] = aabbSphereIntersectR2(min, max, origin, radius_squared);
        }
#endif
    }
}

#endif // LL_LLSIMDKERNELS_H
