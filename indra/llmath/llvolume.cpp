/**
 * @file llvolume.cpp
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "linden_common.h"
#include "llmemory.h"
#include "llmath.h"

#include <set>
#if !LL_WINDOWS
#include <stdint.h>
#endif
#include <cmath>
#include <cstddef>
#include <unordered_map>

#include "llerror.h"

#include "llvolumemgr.h"
#include "v2math.h"
#include "v3math.h"
#include "v4math.h"
#include "m4math.h"
#include "m3math.h"
#include "llmatrix3a.h"
#include "lloctree.h"
#include "llvolume.h"
#include "llstl.h"
#include "llsdserialize.h"
#include "llvector4a.h"
#include "llmatrix4a.h"
#include "llmeshoptimizer.h"
#include "lltimer.h"
#include "llvolumeoctree.h"
#include "vkstorm_ffi.h"

// The parametric mesher (profile and path generation, the mesh sweep, the
// face builders and tangents) is the Rust crate vkstorm-volume, reached
// through vkstorm_ffi. The classes here keep the data; the parameter records
// and the path point layout are shared with the crate.
static_assert(sizeof(LLPath::PathPt) == sizeof(VksPathPt), "LLPath::PathPt layout is shared with vkstorm-volume");
static_assert(sizeof(LLVector4a) == 4 * sizeof(float), "LLVector4a layout is shared with vkstorm-volume");
// The face result's vectors are read with aligned loads: they sit at 16-byte
// offsets and the local record is aligned where it is declared.
static_assert(offsetof(VksFaceResult, extents_min) == 0 && offsetof(VksFaceResult, extents_max) == 16 && offsetof(VksFaceResult, center) == 32,
              "VksFaceResult vectors must be at 16-byte offsets for load4a");

namespace
{
    VksProfileParams vks_profile_params(const LLProfileParams& params)
    {
        VksProfileParams p;
        p.curve = params.getCurveType();
        p.begin = params.getBegin();
        p.end = params.getEnd();
        p.hollow = params.getHollow();
        return p;
    }

    VksPathParams vks_path_params(const LLPathParams& params)
    {
        VksPathParams p;
        p.curve = params.getCurveType();
        p.begin = params.getBegin();
        p.end = params.getEnd();
        p.scale_x = params.getScaleX();
        p.scale_y = params.getScaleY();
        p.shear_x = params.getShearX();
        p.shear_y = params.getShearY();
        p.twist_begin = params.getTwistBegin();
        p.twist_end = params.getTwistEnd();
        p.radius_offset = params.getRadiusOffset();
        p.taper_x = params.getTaperX();
        p.taper_y = params.getTaperY();
        p.revolutions = params.getRevolutions();
        p.skew = params.getSkew();
        return p;
    }
}

#include "mikktspace/mikktspace.hh"

#include "meshoptimizer/meshoptimizer.h"

#define DEBUG_SILHOUETTE_BINORMALS 0
#define DEBUG_SILHOUETTE_NORMALS 0 // TomY: Use this to display normals using the silhouette
#define DEBUG_SILHOUETTE_EDGE_MAP 0 // DaveP: Use this to display edge map using the silhouette

constexpr F32 MIN_CUT_DELTA = 0.02f;

const F32 HOLLOW_MIN = 0.f;
// <AW: opensim-limits>
//constexpr F32 HOLLOW_MAX = 0.95f;
constexpr F32 HOLLOW_MAX = 0.99f;
// <AW: opensim-limits>
constexpr F32 HOLLOW_MAX_SQUARE = 0.7f;

constexpr F32 TWIST_MIN = -1.f;
constexpr F32 TWIST_MAX =  1.f;

constexpr F32 RATIO_MIN = 0.f;
constexpr F32 RATIO_MAX = 2.f; // Tom Y: Inverted sense here: 0 = top taper, 2 = bottom taper
// <AW: opensim-limits>
//constexpr F32 HOLE_X_MIN= 0.05f;
constexpr F32 HOLE_X_MIN= 0.01f;
// <AW: opensim-limits>
constexpr F32 HOLE_X_MAX= 1.0f;

// <AW: opensim-limits>
//constexpr F32 HOLE_Y_MIN= 0.05f;
constexpr F32 HOLE_Y_MIN= 0.01f;
// <AW: opensim-limits>
constexpr F32 HOLE_Y_MAX= 0.5f;

constexpr F32 SHEAR_MIN = -0.5f;
constexpr F32 SHEAR_MAX =  0.5f;

constexpr F32 REV_MIN = 1.f;
constexpr F32 REV_MAX = 4.f;

constexpr F32 TAPER_MIN = -1.f;
constexpr F32 TAPER_MAX =  1.f;

constexpr F32 SKEW_MIN  = -0.95f;
constexpr F32 SKEW_MAX  =  0.95f;

constexpr F32 SCULPT_MIN_AREA = 0.002f;
constexpr S32 SCULPT_MIN_AREA_DETAIL = 1;

bool gDebugGL = false; // See settings.xml "RenderDebugGL"

// <FS:ND> Cache for Triangles/LOD estimation
struct TrianglesPerLODCache
{
    LLProfileParams mProfileParams;
    LLPathParams mPathParams;
    S32 mTriangles[4];
};
// </FS:ND>

bool check_same_clock_dir( const LLVector3& pt1, const LLVector3& pt2, const LLVector3& pt3, const LLVector3& norm)
{
    LLVector3 test = (pt2-pt1)%(pt3-pt2);

    //answer
    if(test * norm < 0)
    {
        return false;
    }
    else
    {
        return true;
    }
}

bool LLLineSegmentBoxIntersect(const LLVector3& start, const LLVector3& end, const LLVector3& center, const LLVector3& size)
{
    return LLLineSegmentBoxIntersect(start.mV, end.mV, center.mV, size.mV);
}

bool LLLineSegmentBoxIntersect(const F32* start, const F32* end, const F32* center, const F32* size)
{
    F32 fAWdU[3]{};
    F32 dir[3]{};
    F32 diff[3]{};

    for (U32 i = 0; i < 3; i++)
    {
        dir[i] = 0.5f * (end[i] - start[i]);
        diff[i] = (0.5f * (end[i] + start[i])) - center[i];
        fAWdU[i] = fabsf(dir[i]);
        if(fabsf(diff[i])>size[i] + fAWdU[i]) return false;
    }

    float f;
    f = dir[1] * diff[2] - dir[2] * diff[1];    if(fabsf(f)>size[1]*fAWdU[2] + size[2]*fAWdU[1])  return false;
    f = dir[2] * diff[0] - dir[0] * diff[2];    if(fabsf(f)>size[0]*fAWdU[2] + size[2]*fAWdU[0])  return false;
    f = dir[0] * diff[1] - dir[1] * diff[0];    if(fabsf(f)>size[0]*fAWdU[1] + size[1]*fAWdU[0])  return false;

    return true;
}

// Finds tangent vec based on three vertices with texture coordinates.
// Fills in dummy values if the triangle has degenerate texture coordinates.
void calc_tangent_from_triangle(
    LLVector4a&         normal,
    LLVector4a&         tangent_out,
    const LLVector4a& v1,
    const LLVector2&  w1,
    const LLVector4a& v2,
    const LLVector2&  w2,
    const LLVector4a& v3,
    const LLVector2&  w3)
{
    const F32* v1ptr = v1.getF32ptr();
    const F32* v2ptr = v2.getF32ptr();
    const F32* v3ptr = v3.getF32ptr();

    float x1 = v2ptr[0] - v1ptr[0];
    float x2 = v3ptr[0] - v1ptr[0];
    float y1 = v2ptr[1] - v1ptr[1];
    float y2 = v3ptr[1] - v1ptr[1];
    float z1 = v2ptr[2] - v1ptr[2];
    float z2 = v3ptr[2] - v1ptr[2];

    float s1 = w2.mV[0] - w1.mV[0];
    float s2 = w3.mV[0] - w1.mV[0];
    float t1 = w2.mV[1] - w1.mV[1];
    float t2 = w3.mV[1] - w1.mV[1];

    F32 rd = s1*t2-s2*t1;

    float r = ((rd*rd) > FLT_EPSILON) ? (1.0f / rd)
                                                : ((rd > 0.0f) ? 1024.f : -1024.f); //some made up large ratio for division by zero

    llassert(llfinite(r));
    llassert(!llisnan(r));

    LLVector4a sdir(
        (t2 * x1 - t1 * x2) * r,
        (t2 * y1 - t1 * y2) * r,
        (t2 * z1 - t1 * z2) * r);

    LLVector4a tdir(
        (s1 * x2 - s2 * x1) * r,
        (s1 * y2 - s2 * y1) * r,
        (s1 * z2 - s2 * z1) * r);

    LLVector4a  n = normal;
    LLVector4a  t = sdir;

    LLVector4a ncrosst;
    ncrosst.setCross3(n,t);

    // Gram-Schmidt orthogonalize
    n.mul(n.dot3(t).getF32());

    LLVector4a tsubn;
    tsubn.setSub(t,n);

    if (tsubn.dot3(tsubn).getF32() > F_APPROXIMATELY_ZERO)
    {
        tsubn.normalize3fast_checked();

        // Calculate handedness
        F32 handedness = ncrosst.dot3(tdir).getF32() < 0.f ? -1.f : 1.f;

        tsubn.getF32ptr()[3] = handedness;

        tangent_out = tsubn;
    }
    else
    {
        // degenerate, make up a value
        //
        tangent_out.set(0,0,1,1);
    }

}


// intersect test between triangle vert0, vert1, vert2 and a ray from orig in direction dir.
// returns true if intersecting and returns barycentric coordinates in intersection_a, intersection_b,
// and returns the intersection point along dir in intersection_t.

// Moller-Trumbore algorithm
bool LLTriangleRayIntersect(const LLVector4a& vert0, const LLVector4a& vert1, const LLVector4a& vert2, const LLVector4a& orig, const LLVector4a& dir,
                            F32& intersection_a, F32& intersection_b, F32& intersection_t)
{

    /* find vectors for two edges sharing vert0 */
    LLVector4a edge1;
    edge1.setSub(vert1, vert0);

    LLVector4a edge2;
    edge2.setSub(vert2, vert0);

    /* begin calculating determinant - also used to calculate U parameter */
    LLVector4a pvec;
    pvec.setCross3(dir, edge2);

    /* if determinant is near zero, ray lies in plane of triangle */
    LLVector4a det;
    det.setAllDot3(edge1, pvec);

    if (det.greaterEqual(LLVector4a::getEpsilon()).getGatheredBits() & 0x7)
    {
        /* calculate distance from vert0 to ray origin */
        LLVector4a tvec;
        tvec.setSub(orig, vert0);

        /* calculate U parameter and test bounds */
        LLVector4a u;
        u.setAllDot3(tvec,pvec);

        if ((u.greaterEqual(LLVector4a::getZero()).getGatheredBits() & 0x7) &&
            (u.lessEqual(det).getGatheredBits() & 0x7))
        {
            /* prepare to test V parameter */
            LLVector4a qvec;
            qvec.setCross3(tvec, edge1);

            /* calculate V parameter and test bounds */
            LLVector4a v;
            v.setAllDot3(dir, qvec);


            //if (!(v < 0.f || u + v > det))

            LLVector4a sum_uv;
            sum_uv.setAdd(u, v);

            S32 v_gequal = v.greaterEqual(LLVector4a::getZero()).getGatheredBits() & 0x7;
            S32 sum_lequal = sum_uv.lessEqual(det).getGatheredBits() & 0x7;

            if (v_gequal  && sum_lequal)
            {
                /* calculate t, scale parameters, ray intersects triangle */
                LLVector4a t;
                t.setAllDot3(edge2,qvec);

                t.div(det);
                u.div(det);
                v.div(det);

                intersection_a = u[0];
                intersection_b = v[0];
                intersection_t = t[0];
                return true;
            }
        }
    }

    return false;
}

bool LLTriangleRayIntersectTwoSided(const LLVector4a& vert0, const LLVector4a& vert1, const LLVector4a& vert2, const LLVector4a& orig, const LLVector4a& dir,
                            F32& intersection_a, F32& intersection_b, F32& intersection_t)
{
    F32 u, v, t;

    /* find vectors for two edges sharing vert0 */
    LLVector4a edge1;
    edge1.setSub(vert1, vert0);


    LLVector4a edge2;
    edge2.setSub(vert2, vert0);

    /* begin calculating determinant - also used to calculate U parameter */
    LLVector4a pvec;
    pvec.setCross3(dir, edge2);

    /* if determinant is near zero, ray lies in plane of triangle */
    F32 det = edge1.dot3(pvec).getF32();


    if (det > -F_APPROXIMATELY_ZERO && det < F_APPROXIMATELY_ZERO)
    {
        return false;
    }

    F32 inv_det = 1.f / det;

    /* calculate distance from vert0 to ray origin */
    LLVector4a tvec;
    tvec.setSub(orig, vert0);

    /* calculate U parameter and test bounds */
    u = (tvec.dot3(pvec).getF32()) * inv_det;
    if (u < 0.f || u > 1.f)
    {
        return false;
    }

    /* prepare to test V parameter */
    tvec.sub(edge1);

    /* calculate V parameter and test bounds */
    v = (dir.dot3(tvec).getF32()) * inv_det;

    if (v < 0.f || u + v > 1.f)
    {
        return false;
    }

    /* calculate t, ray intersects triangle */
    t = (edge2.dot3(tvec).getF32()) * inv_det;

    intersection_a = u;
    intersection_b = v;
    intersection_t = t;


    return true;
}

//-------------------------------------------------------------------
// statics
//-------------------------------------------------------------------


//----------------------------------------------------

//static
S32 LLProfile::getNumPoints(const LLProfileParams& params, bool path_open,F32 detail, S32 split,
                         bool is_sculpted, S32 sculpt_size)
{ // the point count the generation below produces, for LOD triangle estimates
    VksProfileParams p = vks_profile_params(params);
    return vks_volume_profile_num_points(&p, path_open, detail, split, is_sculpted, sculpt_size);
}


bool LLProfile::generate(const LLProfileParams& params, bool path_open,F32 detail, S32 split,
                         bool is_sculpted, S32 sculpt_size)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VOLUME;

    if ((!mDirty) && (!is_sculpted))
    {
        return false;
    }
    mDirty = false;

    if (detail < MIN_LOD)
    {
        LL_INFOS() << "Generating profile with LOD < MIN_LOD.  CLAMPING" << LL_ENDL;
        detail = MIN_LOD;
    }

    mProfile.resize(0);
    mFaces.resize(0);

    VksProfileParams p = vks_profile_params(params);
    VksProfileOut out;
    S32 rc = vks_volume_profile_generate(&p, path_open, detail, split, is_sculpted, sculpt_size, &out);
    if (rc != 1)
    {
        if (rc == 0)
        {
            // Quick validation to eliminate some server crashes.
            LL_WARNS() << "LLProfile::generate() assertion failed (begin >= end)" << LL_ENDL;
        }
        else
        {
            LL_WARNS() << "LLProfile::generate() failed in the mesher" << LL_ENDL;
        }
        return false;
    }

    mProfile.resize(out.point_count);
    if (out.point_count)
    {
        LLVector4a::memcpyNonAliased16((F32*) mProfile.mArray, out.points, out.point_count * sizeof(LLVector4a));
    }
    mFaces.resize(out.face_count);
    for (U32 i = 0; i < out.face_count; ++i)
    {
        const VksProfileFace& s = out.faces[i];
        Face& f = mFaces[i];
        f.mIndex = s.index;
        f.mCount = s.count;
        f.mScaleU = s.scale_u;
        f.mCap = s.cap != 0;
        f.mFlat = s.flat != 0;
        f.mFaceID = s.face_id;
    }
    mTotal = out.total;
    mTotalOut = out.total_out;
    mOpen = out.open != 0;
    mConcave = out.concave != 0;

    return true;
}



bool LLProfileParams::importFile(LLFILE *fp)
{
    const S32 BUFSIZE = 16384;
    char buffer[BUFSIZE];   /* Flawfinder: ignore */
    // *NOTE: changing the size or type of these buffers will require
    // changing the sscanf below.
    char keyword[256];  /* Flawfinder: ignore */
    char valuestr[256]; /* Flawfinder: ignore */
    keyword[0] = 0;
    valuestr[0] = 0;
    F32 tempF32;
    U32 tempU32;

    while (!feof(fp))
    {
        if (fgets(buffer, BUFSIZE, fp) == NULL)
        {
            buffer[0] = '\0';
        }

        sscanf( /* Flawfinder: ignore */
            buffer,
            " %255s %255s",
            keyword, valuestr);
        if (!strcmp("{", keyword))
        {
            continue;
        }
        if (!strcmp("}",keyword))
        {
            break;
        }
        else if (!strcmp("curve", keyword))
        {
            sscanf(valuestr,"%d",&tempU32);
            setCurveType((U8) tempU32);
        }
        else if (!strcmp("begin",keyword))
        {
            sscanf(valuestr,"%g",&tempF32);
            setBegin(tempF32);
        }
        else if (!strcmp("end",keyword))
        {
            sscanf(valuestr,"%g",&tempF32);
            setEnd(tempF32);
        }
        else if (!strcmp("hollow",keyword))
        {
            sscanf(valuestr,"%g",&tempF32);
            setHollow(tempF32);
        }
        else
        {
            LL_WARNS() << "unknown keyword " << keyword << " in profile import" << LL_ENDL;
        }
    }

    return true;
}


bool LLProfileParams::exportFile(LLFILE *fp) const
{
    fprintf(fp,"\t\tprofile 0\n");
    fprintf(fp,"\t\t{\n");
    fprintf(fp,"\t\t\tcurve\t%d\n", getCurveType());
    fprintf(fp,"\t\t\tbegin\t%g\n", getBegin());
    fprintf(fp,"\t\t\tend\t%g\n", getEnd());
    fprintf(fp,"\t\t\thollow\t%g\n", getHollow());
    fprintf(fp, "\t\t}\n");
    return true;
}


bool LLProfileParams::importLegacyStream(std::istream& input_stream)
{
    const S32 BUFSIZE = 16384;
    char buffer[BUFSIZE];   /* Flawfinder: ignore */
    // *NOTE: changing the size or type of these buffers will require
    // changing the sscanf below.
    char keyword[256];  /* Flawfinder: ignore */
    char valuestr[256]; /* Flawfinder: ignore */
    keyword[0] = 0;
    valuestr[0] = 0;
    F32 tempF32;
    U32 tempU32;

    while (input_stream.good())
    {
        input_stream.getline(buffer, BUFSIZE);
        sscanf( /* Flawfinder: ignore */
            buffer,
            " %255s %255s",
            keyword,
            valuestr);
        if (!strcmp("{", keyword))
        {
            continue;
        }
        if (!strcmp("}",keyword))
        {
            break;
        }
        else if (!strcmp("curve", keyword))
        {
            sscanf(valuestr,"%d",&tempU32);
            setCurveType((U8) tempU32);
        }
        else if (!strcmp("begin",keyword))
        {
            sscanf(valuestr,"%g",&tempF32);
            setBegin(tempF32);
        }
        else if (!strcmp("end",keyword))
        {
            sscanf(valuestr,"%g",&tempF32);
            setEnd(tempF32);
        }
        else if (!strcmp("hollow",keyword))
        {
            sscanf(valuestr,"%g",&tempF32);
            setHollow(tempF32);
        }
        else
        {
        LL_WARNS() << "unknown keyword " << keyword << " in profile import" << LL_ENDL;
        }
    }

    return true;
}


bool LLProfileParams::exportLegacyStream(std::ostream& output_stream) const
{
    output_stream <<"\t\tprofile 0\n";
    output_stream <<"\t\t{\n";
    output_stream <<"\t\t\tcurve\t" << (S32) getCurveType() << "\n";
    output_stream <<"\t\t\tbegin\t" << getBegin() << "\n";
    output_stream <<"\t\t\tend\t" << getEnd() << "\n";
    output_stream <<"\t\t\thollow\t" << getHollow() << "\n";
    output_stream << "\t\t}\n";
    return true;
}

LLSD LLProfileParams::asLLSD() const
{
    LLSD sd;

    sd["curve"] = getCurveType();
    sd["begin"] = getBegin();
    sd["end"] = getEnd();
    sd["hollow"] = getHollow();
    return sd;
}

bool LLProfileParams::fromLLSD(LLSD& sd)
{
    setCurveType(sd["curve"].asInteger());
    setBegin((F32)sd["begin"].asReal());
    setEnd((F32)sd["end"].asReal());
    setHollow((F32)sd["hollow"].asReal());
    return true;
}

void LLProfileParams::copyParams(const LLProfileParams &params)
{
    setCurveType(params.getCurveType());
    setBegin(params.getBegin());
    setEnd(params.getEnd());
    setHollow(params.getHollow());
}


LLPath::~LLPath()
{
}

const LLVector2 LLPathParams::getBeginScale() const
{
    LLVector2 begin_scale(1.f, 1.f);
    if (getScaleX() > 1)
    {
        begin_scale.mV[0] = 2-getScaleX();
    }
    if (getScaleY() > 1)
    {
        begin_scale.mV[1] = 2-getScaleY();
    }
    return begin_scale;
}

const LLVector2 LLPathParams::getEndScale() const
{
    LLVector2 end_scale(1.f, 1.f);
    if (getScaleX() < 1)
    {
        end_scale.mV[0] = getScaleX();
    }
    if (getScaleY() < 1)
    {
        end_scale.mV[1] = getScaleY();
    }
    return end_scale;
}

S32 LLPath::getNumPoints(const LLPathParams& params, F32 detail)
{ // the point count estimate for LOD triangle counts (for circles: the side count)
    VksPathParams p = vks_path_params(params);
    return vks_volume_path_num_points(&p, detail);
}

bool LLPath::generate(const LLPathParams& params, F32 detail, S32 split,
                      bool is_sculpted, S32 sculpt_size)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VOLUME;

    if ((!mDirty) && (!is_sculpted))
    {
        return false;
    }

    if (detail < MIN_LOD)
    {
        LL_INFOS() << "Generating path with LOD < MIN!  Clamping to 1" << LL_ENDL;
        detail = MIN_LOD;
    }

    mDirty = false;

    mPath.resize(0);
    mOpen = true;

    VksPathParams p = vks_path_params(params);
    VksPathOut out;
    if (vks_volume_path_generate(&p, detail, split, is_sculpted, sculpt_size, &out) != 1)
    {
        LL_WARNS() << "LLPath::generate() failed in the mesher" << LL_ENDL;
        return false;
    }

    mPath.resize(out.count);
    if (out.count)
    {
        // PathPt has a constructor, so GCC refuses a plain memcpy into it; the
        // layouts are shared (static_assert above) and the copy is intended.
        memcpy(static_cast<void*>(mPath.mArray), out.points, out.count * sizeof(PathPt));
    }
    mOpen = out.open != 0;
    mStep = out.step;
    mTotal = out.count;

    return true;
}

bool LLDynamicPath::generate(const LLPathParams& params, F32 detail, S32 split,
                             bool is_sculpted, S32 sculpt_size)
{
    mOpen = true; // Draw end caps
    if (getPathLength() == 0)
    {
        // Path hasn't been generated yet.
        // Some algorithms later assume at least TWO path points.
        resizePath(2);
        LLQuaternion quat;
        quat.setQuat(0,0,0);
        LLMatrix3 tmp(quat);

        for (U32 i = 0; i < 2; i++)
        {
            mPath[i].mPos.set(0, 0, 0);
            mPath[i].mRot.loadu(tmp);
            mPath[i].mScale.set(1, 1, 0, 1);
            mPath[i].mTexT = 0;
        }
    }

    return true;
}


bool LLPathParams::importFile(LLFILE *fp)
{
    const S32 BUFSIZE = 16384;
    char buffer[BUFSIZE];   /* Flawfinder: ignore */
    // *NOTE: changing the size or type of these buffers will require
    // changing the sscanf below.
    char keyword[256];  /* Flawfinder: ignore */
    char valuestr[256]; /* Flawfinder: ignore */
    keyword[0] = 0;
    valuestr[0] = 0;

    F32 tempF32;
    F32 x, y;
    U32 tempU32;

    while (!feof(fp))
    {
        if (fgets(buffer, BUFSIZE, fp) == NULL)
        {
            buffer[0] = '\0';
        }

        sscanf( /* Flawfinder: ignore */
            buffer,
            " %255s %255s",
            keyword, valuestr);
        if (!strcmp("{", keyword))
        {
            continue;
        }
        if (!strcmp("}",keyword))
        {
            break;
        }
        else if (!strcmp("curve", keyword))
        {
            sscanf(valuestr,"%d",&tempU32);
            setCurveType((U8) tempU32);
        }
        else if (!strcmp("begin",keyword))
        {
            sscanf(valuestr,"%g",&tempF32);
            setBegin(tempF32);
        }
        else if (!strcmp("end",keyword))
        {
            sscanf(valuestr,"%g",&tempF32);
            setEnd(tempF32);
        }
        else if (!strcmp("scale",keyword))
        {
            // Legacy for one dimensional scale per path
            sscanf(valuestr,"%g",&tempF32);
            setScale(tempF32, tempF32);
        }
        else if (!strcmp("scale_x", keyword))
        {
            sscanf(valuestr, "%g", &x);
            setScaleX(x);
        }
        else if (!strcmp("scale_y", keyword))
        {
            sscanf(valuestr, "%g", &y);
            setScaleY(y);
        }
        else if (!strcmp("shear_x", keyword))
        {
            sscanf(valuestr, "%g", &x);
            setShearX(x);
        }
        else if (!strcmp("shear_y", keyword))
        {
            sscanf(valuestr, "%g", &y);
            setShearY(y);
        }
        else if (!strcmp("twist",keyword))
        {
            sscanf(valuestr,"%g",&tempF32);
            setTwist(tempF32);
        }
        else if (!strcmp("twist_begin", keyword))
        {
            sscanf(valuestr, "%g", &y);
            setTwistBegin(y);
        }
        else if (!strcmp("radius_offset", keyword))
        {
            sscanf(valuestr, "%g", &y);
            setRadiusOffset(y);
        }
        else if (!strcmp("taper_x", keyword))
        {
            sscanf(valuestr, "%g", &y);
            setTaperX(y);
        }
        else if (!strcmp("taper_y", keyword))
        {
            sscanf(valuestr, "%g", &y);
            setTaperY(y);
        }
        else if (!strcmp("revolutions", keyword))
        {
            sscanf(valuestr, "%g", &y);
            setRevolutions(y);
        }
        else if (!strcmp("skew", keyword))
        {
            sscanf(valuestr, "%g", &y);
            setSkew(y);
        }
        else
        {
            LL_WARNS() << "unknown keyword " << " in path import" << LL_ENDL;
        }
    }
    return true;
}


bool LLPathParams::exportFile(LLFILE *fp) const
{
    fprintf(fp, "\t\tpath 0\n");
    fprintf(fp, "\t\t{\n");
    fprintf(fp, "\t\t\tcurve\t%d\n", getCurveType());
    fprintf(fp, "\t\t\tbegin\t%g\n", getBegin());
    fprintf(fp, "\t\t\tend\t%g\n", getEnd());
    fprintf(fp, "\t\t\tscale_x\t%g\n", getScaleX() );
    fprintf(fp, "\t\t\tscale_y\t%g\n", getScaleY() );
    fprintf(fp, "\t\t\tshear_x\t%g\n", getShearX() );
    fprintf(fp, "\t\t\tshear_y\t%g\n", getShearY() );
    fprintf(fp,"\t\t\ttwist\t%g\n", getTwist());

    fprintf(fp,"\t\t\ttwist_begin\t%g\n", getTwistBegin());
    fprintf(fp,"\t\t\tradius_offset\t%g\n", getRadiusOffset());
    fprintf(fp,"\t\t\ttaper_x\t%g\n", getTaperX());
    fprintf(fp,"\t\t\ttaper_y\t%g\n", getTaperY());
    fprintf(fp,"\t\t\trevolutions\t%g\n", getRevolutions());
    fprintf(fp,"\t\t\tskew\t%g\n", getSkew());

    fprintf(fp, "\t\t}\n");
    return true;
}


bool LLPathParams::importLegacyStream(std::istream& input_stream)
{
    const S32 BUFSIZE = 16384;
    char buffer[BUFSIZE];   /* Flawfinder: ignore */
    // *NOTE: changing the size or type of these buffers will require
    // changing the sscanf below.
    char keyword[256];  /* Flawfinder: ignore */
    char valuestr[256]; /* Flawfinder: ignore */
    keyword[0] = 0;
    valuestr[0] = 0;

    F32 tempF32;
    F32 x, y;
    U32 tempU32;

    while (input_stream.good())
    {
        input_stream.getline(buffer, BUFSIZE);
        sscanf( /* Flawfinder: ignore */
            buffer,
            " %255s %255s",
            keyword, valuestr);
        if (!strcmp("{", keyword))
        {
            continue;
        }
        if (!strcmp("}",keyword))
        {
            break;
        }
        else if (!strcmp("curve", keyword))
        {
            sscanf(valuestr,"%d",&tempU32);
            setCurveType((U8) tempU32);
        }
        else if (!strcmp("begin",keyword))
        {
            sscanf(valuestr,"%g",&tempF32);
            setBegin(tempF32);
        }
        else if (!strcmp("end",keyword))
        {
            sscanf(valuestr,"%g",&tempF32);
            setEnd(tempF32);
        }
        else if (!strcmp("scale",keyword))
        {
            // Legacy for one dimensional scale per path
            sscanf(valuestr,"%g",&tempF32);
            setScale(tempF32, tempF32);
        }
        else if (!strcmp("scale_x", keyword))
        {
            sscanf(valuestr, "%g", &x);
            setScaleX(x);
        }
        else if (!strcmp("scale_y", keyword))
        {
            sscanf(valuestr, "%g", &y);
            setScaleY(y);
        }
        else if (!strcmp("shear_x", keyword))
        {
            sscanf(valuestr, "%g", &x);
            setShearX(x);
        }
        else if (!strcmp("shear_y", keyword))
        {
            sscanf(valuestr, "%g", &y);
            setShearY(y);
        }
        else if (!strcmp("twist",keyword))
        {
            sscanf(valuestr,"%g",&tempF32);
            setTwist(tempF32);
        }
        else if (!strcmp("twist_begin", keyword))
        {
            sscanf(valuestr, "%g", &y);
            setTwistBegin(y);
        }
        else if (!strcmp("radius_offset", keyword))
        {
            sscanf(valuestr, "%g", &y);
            setRadiusOffset(y);
        }
        else if (!strcmp("taper_x", keyword))
        {
            sscanf(valuestr, "%g", &y);
            setTaperX(y);
        }
        else if (!strcmp("taper_y", keyword))
        {
            sscanf(valuestr, "%g", &y);
            setTaperY(y);
        }
        else if (!strcmp("revolutions", keyword))
        {
            sscanf(valuestr, "%g", &y);
            setRevolutions(y);
        }
        else if (!strcmp("skew", keyword))
        {
            sscanf(valuestr, "%g", &y);
            setSkew(y);
        }
        else
        {
            LL_WARNS() << "unknown keyword " << " in path import" << LL_ENDL;
        }
    }
    return true;
}


bool LLPathParams::exportLegacyStream(std::ostream& output_stream) const
{
    output_stream << "\t\tpath 0\n";
    output_stream << "\t\t{\n";
    output_stream << "\t\t\tcurve\t" << (S32) getCurveType() << "\n";
    output_stream << "\t\t\tbegin\t" << getBegin() << "\n";
    output_stream << "\t\t\tend\t" << getEnd() << "\n";
    output_stream << "\t\t\tscale_x\t" << getScaleX()  << "\n";
    output_stream << "\t\t\tscale_y\t" << getScaleY()  << "\n";
    output_stream << "\t\t\tshear_x\t" << getShearX()  << "\n";
    output_stream << "\t\t\tshear_y\t" << getShearY()  << "\n";
    output_stream <<"\t\t\ttwist\t" << getTwist() << "\n";

    output_stream <<"\t\t\ttwist_begin\t" << getTwistBegin() << "\n";
    output_stream <<"\t\t\tradius_offset\t" << getRadiusOffset() << "\n";
    output_stream <<"\t\t\ttaper_x\t" << getTaperX() << "\n";
    output_stream <<"\t\t\ttaper_y\t" << getTaperY() << "\n";
    output_stream <<"\t\t\trevolutions\t" << getRevolutions() << "\n";
    output_stream <<"\t\t\tskew\t" << getSkew() << "\n";

    output_stream << "\t\t}\n";
    return true;
}

LLSD LLPathParams::asLLSD() const
{
    LLSD sd = LLSD();
    sd["curve"] = getCurveType();
    sd["begin"] = getBegin();
    sd["end"] = getEnd();
    sd["scale_x"] = getScaleX();
    sd["scale_y"] = getScaleY();
    sd["shear_x"] = getShearX();
    sd["shear_y"] = getShearY();
    sd["twist"] = getTwist();
    sd["twist_begin"] = getTwistBegin();
    sd["radius_offset"] = getRadiusOffset();
    sd["taper_x"] = getTaperX();
    sd["taper_y"] = getTaperY();
    sd["revolutions"] = getRevolutions();
    sd["skew"] = getSkew();

    return sd;
}

bool LLPathParams::fromLLSD(LLSD& sd)
{
    setCurveType(sd["curve"].asInteger());
    setBegin((F32)sd["begin"].asReal());
    setEnd((F32)sd["end"].asReal());
    setScaleX((F32)sd["scale_x"].asReal());
    setScaleY((F32)sd["scale_y"].asReal());
    setShearX((F32)sd["shear_x"].asReal());
    setShearY((F32)sd["shear_y"].asReal());
    setTwist((F32)sd["twist"].asReal());
    setTwistBegin((F32)sd["twist_begin"].asReal());
    setRadiusOffset((F32)sd["radius_offset"].asReal());
    setTaperX((F32)sd["taper_x"].asReal());
    setTaperY((F32)sd["taper_y"].asReal());
    setRevolutions((F32)sd["revolutions"].asReal());
    setSkew((F32)sd["skew"].asReal());
    return true;
}

void LLPathParams::copyParams(const LLPathParams &params)
{
    setCurveType(params.getCurveType());
    setBegin(params.getBegin());
    setEnd(params.getEnd());
    setScale(params.getScaleX(), params.getScaleY() );
    setShear(params.getShearX(), params.getShearY() );
    setTwist(params.getTwist());
    setTwistBegin(params.getTwistBegin());
    setRadiusOffset(params.getRadiusOffset());
    setTaper( params.getTaperX(), params.getTaperY() );
    setRevolutions(params.getRevolutions());
    setSkew(params.getSkew());
}

LLProfile::~LLProfile()
{
}


S32 LLVolume::sNumMeshPoints = 0;

LLVolume::LLVolume(const LLVolumeParams &params, const F32 detail, const bool generate_single_face, const bool is_unique)
    : mParams(params)
    , mTrianglesCache(0) // <FS:ND> cache for trianges/LOD estimation
{
    mUnique = is_unique;
    mFaceMask = 0x0;
    mDetail = detail;
    mSculptLevel = -2;
    mSurfaceArea = 1.f; //only calculated for sculpts, defaults to 1 for all other prims
    mIsMeshAssetLoaded = false;
    mIsMeshAssetUnavaliable = false;
    mLODScaleBias.setVec(1,1,1);
    mHullPoints = nullptr;
    mHullIndices = nullptr;
    mNumHullPoints = 0;
    mNumHullIndices = 0;

    // set defaults
    if (mParams.getPathParams().getCurveType() == LL_PCODE_PATH_FLEXIBLE)
    {
        mPathp = new LLDynamicPath();
    }
    else
    {
        mPathp = new LLPath();
    }
    mProfilep = new LLProfile();

    mGenerateSingleFace = generate_single_face;

    generate();

    if ((mParams.getSculptID().isNull() && mParams.getSculptType() == LL_SCULPT_TYPE_NONE) || mParams.getSculptType() == LL_SCULPT_TYPE_MESH)
    {
        createVolumeFaces();
    }
}

void LLVolume::resizePath(S32 length)
{
    mPathp->resizePath(length);
    mVolumeFaces.clear();
    setDirty();
}

void LLVolume::regen()
{
    generate();
    createVolumeFaces();
}

void LLVolume::genTangents(S32 face)
{
    // generate legacy tangents for the specified face
    llassert(!isMeshAssetLoaded() || mVolumeFaces[face].mTangents != nullptr); // if this is a complete mesh asset, we should already have tangents
    mVolumeFaces[face].createTangents();
}

LLVolume::~LLVolume()
{
    sNumMeshPoints -= mMesh.size();
    delete mPathp;

    delete mProfilep;

    mPathp = NULL;
    mProfilep = NULL;
    mVolumeFaces.clear();

    ll_aligned_free_16(mHullPoints);
    mHullPoints = NULL;
    ll_aligned_free_16(mHullIndices);
    mHullIndices = NULL;

    delete mTrianglesCache; // <FS:ND>
}

bool LLVolume::generate()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VOLUME;

    LL_CHECK_MEMORY
    llassert_always(mProfilep);

    //Added 10.03.05 Dave Parks
    // Split is a parameter to LLProfile::generate that tesselates edges on the profile
    // to prevent lighting and texture interpolation errors on triangles that are
    // stretched due to twisting or scaling on the path.
    S32 split = (S32) ((mDetail)*0.66f);

    if (mParams.getPathParams().getCurveType() == LL_PCODE_PATH_LINE &&
        (mParams.getPathParams().getScale().mV[0] != 1.0f ||
         mParams.getPathParams().getScale().mV[1] != 1.0f) &&
        (mParams.getProfileParams().getCurveType() == LL_PCODE_PROFILE_SQUARE ||
         mParams.getProfileParams().getCurveType() == LL_PCODE_PROFILE_ISOTRI ||
         mParams.getProfileParams().getCurveType() == LL_PCODE_PROFILE_EQUALTRI ||
         mParams.getProfileParams().getCurveType() == LL_PCODE_PROFILE_RIGHTTRI))
    {
        split = 0;
    }

    mLODScaleBias.setVec(0.5f, 0.5f, 0.5f);

    F32 profile_detail = mDetail;
    F32 path_detail = mDetail;

    if ((mParams.getSculptType() & LL_SCULPT_TYPE_MASK) != LL_SCULPT_TYPE_MESH)
    {
        U8 path_type = mParams.getPathParams().getCurveType();
        U8 profile_type = mParams.getProfileParams().getCurveType();
        if (path_type == LL_PCODE_PATH_LINE && profile_type == LL_PCODE_PROFILE_CIRCLE)
        {
            //cylinders don't care about Z-Axis
            mLODScaleBias.setVec(0.6f, 0.6f, 0.0f);
        }
        else if (path_type == LL_PCODE_PATH_CIRCLE)
        {
            mLODScaleBias.setVec(0.6f, 0.6f, 0.6f);
        }
    }

    bool regenPath = mPathp->generate(mParams.getPathParams(), path_detail, split);
    bool regenProf = mProfilep->generate(mParams.getProfileParams(), mPathp->isOpen(),profile_detail, split);

    if (regenPath || regenProf )
    {
        S32 sizeS = mPathp->mPath.size();
        S32 sizeT = mProfilep->mProfile.size();

        sNumMeshPoints -= mMesh.size();
        mMesh.resize(sizeT * sizeS);
        sNumMeshPoints += mMesh.size();

        //generate vertex positions: the mesher writes straight into mMesh
        if (sizeS && sizeT)
        {
            vks_volume_sweep((const VksPathPt*) mPathp->mPath.mArray, sizeS,
                             (const F32*) mProfilep->mProfile.mArray, sizeT,
                             (F32*) mMesh.mArray);
        }

        for (std::vector<LLProfile::Face>::iterator iter = mProfilep->mFaces.begin();
             iter != mProfilep->mFaces.end(); ++iter)
        {
            LLFaceID id = iter->mFaceID;
            mFaceMask |= id;
        }
        LL_CHECK_MEMORY
        return true;
    }

    LL_CHECK_MEMORY
    return false;
}

void LLVolumeFace::VertexData::init()
{
    if (!mData)
    {
        mData = (LLVector4a*) ll_aligned_malloc_16(sizeof(LLVector4a)*2);
    }
}

LLVolumeFace::VertexData::VertexData()
{
    mData = NULL;
    init();
}

LLVolumeFace::VertexData::VertexData(const VertexData& rhs)
{
    mData = NULL;
    *this = rhs;
}

const LLVolumeFace::VertexData& LLVolumeFace::VertexData::operator=(const LLVolumeFace::VertexData& rhs)
{
    if (this != &rhs)
    {
        init();
        LLVector4a::memcpyNonAliased16((F32*) mData, (F32*) rhs.mData, 2*sizeof(LLVector4a));
        mTexCoord = rhs.mTexCoord;
    }
    return *this;
}

LLVolumeFace::VertexData::~VertexData()
{
    ll_aligned_free_16(mData);
    mData = NULL;
}

LLVector4a& LLVolumeFace::VertexData::getPosition()
{
    return mData[POSITION];
}

LLVector4a& LLVolumeFace::VertexData::getNormal()
{
    return mData[NORMAL];
}

const LLVector4a& LLVolumeFace::VertexData::getPosition() const
{
    return mData[POSITION];
}

const LLVector4a& LLVolumeFace::VertexData::getNormal() const
{
    return mData[NORMAL];
}


void LLVolumeFace::VertexData::setPosition(const LLVector4a& pos)
{
    mData[POSITION] = pos;
}

void LLVolumeFace::VertexData::setNormal(const LLVector4a& norm)
{
    mData[NORMAL] = norm;
}

bool LLVolumeFace::VertexData::operator<(const LLVolumeFace::VertexData& rhs)const
{
    const F32* lp = this->getPosition().getF32ptr();
    const F32* rp = rhs.getPosition().getF32ptr();

    if (lp[0] != rp[0])
    {
        return lp[0] < rp[0];
    }

    if (rp[1] != lp[1])
    {
        return lp[1] < rp[1];
    }

    if (rp[2] != lp[2])
    {
        return lp[2] < rp[2];
    }

    lp = getNormal().getF32ptr();
    rp = rhs.getNormal().getF32ptr();

    if (lp[0] != rp[0])
    {
        return lp[0] < rp[0];
    }

    if (rp[1] != lp[1])
    {
        return lp[1] < rp[1];
    }

    if (rp[2] != lp[2])
    {
        return lp[2] < rp[2];
    }

    if (mTexCoord.mV[0] != rhs.mTexCoord.mV[0])
    {
        return mTexCoord.mV[0] < rhs.mTexCoord.mV[0];
    }

    return mTexCoord.mV[1] < rhs.mTexCoord.mV[1];
}

bool LLVolumeFace::VertexData::operator==(const LLVolumeFace::VertexData& rhs)const
{
    return mData[POSITION].equals3(rhs.getPosition()) &&
            mData[NORMAL].equals3(rhs.getNormal()) &&
            mTexCoord == rhs.mTexCoord;
}

bool LLVolumeFace::VertexData::compareNormal(const LLVolumeFace::VertexData& rhs, F32 angle_cutoff) const
{
    bool retval = false;

    const F32 epsilon = 0.00001f;

    if (rhs.mData[POSITION].equals3(mData[POSITION], epsilon) &&
        fabs(rhs.mTexCoord[0]-mTexCoord[0]) < epsilon &&
        fabs(rhs.mTexCoord[1]-mTexCoord[1]) < epsilon)
    {
        if (angle_cutoff > 1.f)
        {
            retval = (mData[NORMAL].equals3(rhs.mData[NORMAL], epsilon));
        }
        else
        {
            F32 cur_angle = rhs.mData[NORMAL].dot3(mData[NORMAL]).getF32();
            retval = cur_angle > angle_cutoff;
        }
    }

    return retval;
}

bool LLVolume::unpackVolumeFaces(std::istream& is, S32 size)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VOLUME;

    //input stream is now pointing at a zlib compressed block of LLSD
    //decompress block
    LLSD mdl;
    U32 uzip_result = LLUZipHelper::unzip_llsd(mdl, is, size);
    if (uzip_result != LLUZipHelper::ZR_OK)
    {
        LL_DEBUGS("MeshStreaming") << "Failed to unzip LLSD blob for LoD with code " << uzip_result << " , will probably fetch from sim again." << LL_ENDL;
        return false;
    }
    return unpackVolumeFacesInternal(mdl);
}

bool LLVolume::unpackVolumeFaces(U8* in_data, S32 size, bool optimize)
{
    // The zlib-compressed LLSD block of one LOD is unpacked by vkstorm-assets
    // (vkstorm-rust rung five): inflate, LLSD parse, the dequantisation of
    // positions, normals, texture coordinates and joint weights, the sculpt
    // flags and the extents, bit for bit as unpackVolumeFacesInternal left
    // them. The faces are copied into LLVolumeFace buffers here; the cache
    // optimisation stays C++.
    int32_t err = 0;
    VksMeshFaces* faces = vks_mesh_unpack(in_data, (size_t)llmax(size, 0), (uint8_t)mParams.getSculptType(), &err);
    if (!faces)
    {
        if (err == 1)
        {
            LL_DEBUGS("MeshStreaming") << "Failed to unzip LLSD blob for LoD, will probably fetch from sim again." << LL_ENDL;
        }
        else if (err == 2)
        {
            LL_WARNS() << "found no faces!" << LL_ENDL;
        }
        else
        {
            LL_WARNS() << "Failed to unpack mesh LOD (code " << err << ")" << LL_ENDL;
        }
        return false;
    }

    const size_t face_count = vks_mesh_faces_count(faces);
    mVolumeFaces.resize(face_count);

    for (size_t i = 0; i < face_count; ++i)
    {
        LLVolumeFace& face = mVolumeFaces[i];
        VksMeshFace f;
        if (!vks_mesh_face(faces, i, &f))
        {
            vks_mesh_faces_free(faces);
            mVolumeFaces.clear();
            return false;
        }

        face.resizeIndices(f.num_indices);
        if (f.num_indices > 2 && !face.mIndices)
        {
            LL_WARNS() << "Failed to allocate " << f.num_indices << " indices for face index: " << i << " Total: " << face_count << LL_ENDL;
            continue;
        }
        face.resizeVertices(f.num_vertices);
        if (f.num_vertices > 0 && !face.mPositions)
        {
            LL_WARNS() << "Failed to allocate " << f.num_vertices << " vertices for face index: " << i << " Total: " << face_count << LL_ENDL;
            face.resizeIndices(0);
            continue;
        }
        if (f.incomplete)
        {
            // The C++ left such a face after allocating its buffers (an
            // empty index list); so does the port.
            LL_WARNS() << "Empty face present! Face index: " << i << " Total: " << face_count << LL_ENDL;
            continue;
        }

        if (f.num_indices > 0)
        {
            memcpy(face.mIndices, f.indices, sizeof(U16) * f.num_indices);
        }
        if (f.num_vertices > 0)
        {
            memcpy(face.mPositions->getF32ptr(), f.positions, sizeof(LLVector4a) * f.num_vertices);
            memcpy(face.mNormals->getF32ptr(), f.normals, sizeof(LLVector4a) * f.num_vertices);
            // The texture coordinates come in pairs (the SSE code wrote them
            // four floats at a time); the buffer is allocated for the even
            // count.
            memcpy(face.mTexCoords->mV, f.texcoords, sizeof(F32) * 2 * f.texcoord_count);
        }
        if (f.has_weights)
        {
            face.allocateWeights(f.num_vertices);
            if (!face.mWeights && f.num_vertices)
            {
                LL_WARNS() << "Failed to allocate " << f.num_vertices << " weights for face index: " << i << " Total: " << face_count << LL_ENDL;
                face.resizeIndices(0);
                face.resizeVertices(0);
                continue;
            }
            if (f.num_vertices > 0)
            {
                memcpy(face.mWeights->getF32ptr(), f.weights, sizeof(LLVector4a) * f.num_vertices);
            }
        }
        face.mNormalizedScale.set(f.normalized_scale[0], f.normalized_scale[1], f.normalized_scale[2]);
        face.mExtents[0].set(f.extents[0], f.extents[1], f.extents[2], f.extents[3]);
        face.mExtents[1].set(f.extents[4], f.extents[5], f.extents[6], f.extents[7]);
        face.mTexCoordExtents[0].set(f.tc_extents[0], f.tc_extents[1]);
        face.mTexCoordExtents[1].set(f.tc_extents[2], f.tc_extents[3]);
    }
    vks_mesh_faces_free(faces);

    if (optimize && !cacheOptimize(true))
    {
        // Out of memory?
        LL_WARNS() << "Failed to optimize!" << LL_ENDL;
        mVolumeFaces.clear();
        return false;
    }

    mSculptLevel = 0;  // success!

    return true;
}

bool LLVolume::unpackVolumeFacesInternal(const LLSD& mdl, bool optimize)
{
    {
        auto face_count = mdl.size();

        if (face_count == 0)
        { //no faces unpacked, treat as failed decode
            LL_WARNS() << "found no faces!" << LL_ENDL;
            return false;
        }

        mVolumeFaces.resize(face_count);

        for (size_t i = 0; i < face_count; ++i)
        {
            LLVolumeFace& face = mVolumeFaces[i];

            if (mdl[i].has("NoGeometry"))
            { //face has no geometry, continue
                face.resizeIndices(3);
                face.resizeVertices(1);
                face.mPositions->clear();
                face.mNormals->clear();
                face.mTexCoords->setZero();
                memset(face.mIndices, 0, sizeof(U16)*3);
                continue;
            }

            const LLSD::Binary& pos = mdl[i]["Position"].asBinary();
            const LLSD::Binary& norm = mdl[i]["Normal"].asBinary();
            // const LLSD::Binary& tangent = mdl[i]["Tangent"].asBinary(); // <FS:Beq/> more set but unused
            const LLSD::Binary& tc = mdl[i]["TexCoord0"].asBinary();
            const LLSD::Binary& idx = mdl[i]["TriangleList"].asBinary();

            //copy out indices
            auto num_indices = idx.size() / 2;
            const S32 indices_to_discard = num_indices % 3;
            if (indices_to_discard > 0)
            {
                // Invalid number of triangle indices
                LL_WARNS() << "Incomplete triangle discarded from face! Indices count " << num_indices << " was not divisible by 3. face index: " << i << " Total: " << face_count << LL_ENDL;
                num_indices -= indices_to_discard;
            }
            face.resizeIndices(static_cast<S32>(num_indices));

            if (num_indices > 2 && !face.mIndices)
            {
                LL_WARNS() << "Failed to allocate " << num_indices << " indices for face index: " << i << " Total: " << face_count << LL_ENDL;
                continue;
            }

            if (idx.empty() || face.mNumIndices < 3)
            { //why is there an empty index list?
                LL_WARNS() << "Empty face present! Face index: " << i << " Total: " << face_count << LL_ENDL;
                continue;
            }

            U16* indices = (U16*) &(idx[0]);
            for (U32 j = 0; j < num_indices; ++j)
            {
                face.mIndices[j] = indices[j];
            }

            //copy out vertices
            U32 num_verts = static_cast<U32>(pos.size())/(3*2);
            face.resizeVertices(num_verts);

            if (num_verts > 0 && !face.mPositions)
            {
                LL_WARNS() << "Failed to allocate " << num_verts << " vertices for face index: " << i << " Total: " << face_count << LL_ENDL;
                face.resizeIndices(0);
                continue;
            }

            LLVector3 minp;
            LLVector3 maxp;
            LLVector2 min_tc;
            LLVector2 max_tc;

            minp.setValue(mdl[i]["PositionDomain"]["Min"]);
            maxp.setValue(mdl[i]["PositionDomain"]["Max"]);
            LLVector4a min_pos, max_pos;
            min_pos.load3(minp.mV);
            max_pos.load3(maxp.mV);

            min_tc.setValue(mdl[i]["TexCoord0Domain"]["Min"]);
            max_tc.setValue(mdl[i]["TexCoord0Domain"]["Max"]);

            //unpack normalized scale/translation
            if (mdl[i].has("NormalizedScale"))
            {
                face.mNormalizedScale.setValue(mdl[i]["NormalizedScale"]);
            }
            else
            {
                face.mNormalizedScale.set(1, 1, 1);
            }

            LLVector4a pos_range;
            pos_range.setSub(max_pos, min_pos);
            LLVector2 tc_range2 = max_tc - min_tc;

            LLVector4a tc_range;
            tc_range.set(tc_range2[0], tc_range2[1], tc_range2[0], tc_range2[1]);
            LLVector4a min_tc4(min_tc[0], min_tc[1], min_tc[0], min_tc[1]);

            LLVector4a* pos_out = face.mPositions;
            LLVector4a* norm_out = face.mNormals;
            LLVector4a* tc_out = (LLVector4a*) face.mTexCoords;

            {
                U16* v = (U16*) &(pos[0]);
                for (U32 j = 0; j < num_verts; ++j)
                {
                    pos_out->set((F32) v[0], (F32) v[1], (F32) v[2]);
                    pos_out->div(65535.f);
                    pos_out->mul(pos_range);
                    pos_out->add(min_pos);
                    pos_out++;
                    v += 3;
                }

            }

            {
                if (!norm.empty())
                {
                    U16* n = (U16*) &(norm[0]);
                    for (U32 j = 0; j < num_verts; ++j)
                    {
                        norm_out->set((F32) n[0], (F32) n[1], (F32) n[2]);
                        norm_out->div(65535.f);
                        norm_out->mul(2.f);
                        norm_out->sub(1.f);
                        norm_out++;
                        n += 3;
                    }
                }
                else
                {
                    for (U32 j = 0; j < num_verts; ++j)
                    {
                        norm_out->clear();
                        norm_out++; // or just norm_out[j].clear();
                    }
                }
            }

#if 0 // keep this code for now in case we decide to add support for on-the-wire tangents
            {
                if (!tangent.empty())
                {
                    face.allocateTangents(face.mNumVertices);
                    U16* t = (U16*)&(tangent[0]);

                    // NOTE: tangents coming from the asset may not be mikkt space, but they should always be used by the GLTF shaders to
                    // maintain compliance with the GLTF spec
                    LLVector4a* t_out = face.mTangents;

                    for (U32 j = 0; j < num_verts; ++j)
                    {
                        t_out->set((F32)t[0], (F32)t[1], (F32)t[2], (F32) t[3]);
                        t_out->div(65535.f);
                        t_out->mul(2.f);
                        t_out->sub(1.f);

                        F32* tp = t_out->getF32ptr();
                        tp[3] = tp[3] < 0.f ? -1.f : 1.f;

                        t_out++;
                        t += 4;
                    }
                }
            }
#endif

            {
                if (!tc.empty())
                {
                    U16* t = (U16*) &(tc[0]);
                    for (U32 j = 0; j < num_verts; j+=2)
                    {
                        if (j < num_verts-1)
                        {
                            tc_out->set((F32) t[0], (F32) t[1], (F32) t[2], (F32) t[3]);
                        }
                        else
                        {
                            tc_out->set((F32) t[0], (F32) t[1], 0.f, 0.f);
                        }

                        t += 4;

                        tc_out->div(65535.f);
                        tc_out->mul(tc_range);
                        tc_out->add(min_tc4);

                        tc_out++;
                    }
                }
                else
                {
                    for (U32 j = 0; j < num_verts; j += 2)
                    {
                        tc_out->clear();
                        tc_out++;
                    }
                }
            }

            if (mdl[i].has("Weights"))
            {
                face.allocateWeights(num_verts);
                if (!face.mWeights && num_verts)
                {
                    LL_WARNS() << "Failed to allocate " << num_verts << " weights for face index: " << i << " Total: " << face_count << LL_ENDL;
                    face.resizeIndices(0);
                    face.resizeVertices(0);
                    continue;
                }

                const LLSD::Binary& weights = mdl[i]["Weights"].asBinary();

                U32 idx = 0;

                U32 cur_vertex = 0;
                while (idx < weights.size() && cur_vertex < num_verts)
                {
                    const U8 END_INFLUENCES = 0xFF;
                    U8 joint = weights[idx++];

                    U32 cur_influence = 0;
                    LLVector4 wght(0,0,0,0);
                    U32 joints[4] = {0,0,0,0};
                    LLVector4 joints_with_weights(0,0,0,0);

                    while (joint != END_INFLUENCES && idx < weights.size())
                    {
                        U16 influence = weights[idx++];
                        influence |= ((U16) weights[idx++] << 8);

                        F32 w = llclamp((F32) influence / 65535.f, 0.001f, 0.999f);
                        wght.mV[cur_influence] = w;
                        joints[cur_influence] = joint;
                        cur_influence++;

                        if (cur_influence >= 4)
                        {
                            joint = END_INFLUENCES;
                        }
                        else
                        {
                            joint = weights[idx++];
                        }
                    }
                    F32 wsum = wght.mV[VX] + wght.mV[VY] + wght.mV[VZ] + wght.mV[VW];
                    if (wsum <= 0.f)
                    {
                        wght = LLVector4(0.999f,0.f,0.f,0.f);
                    }
                    for (U32 k=0; k<4; k++)
                    {
                        F32 f_combined = (F32) joints[k] + wght[k];
                        joints_with_weights[k] = f_combined;
                        // Any weights we added above should wind up non-zero and applied to a specific bone.
                        // A failure here would indicate a floating point precision error in the math.
                        llassert((k >= cur_influence) || (f_combined - S32(f_combined) > 0.0f));
                    }
                    face.mWeights[cur_vertex].loadua(joints_with_weights.mV);

                    cur_vertex++;
                }

                if (cur_vertex != num_verts || idx != weights.size())
                {
                    LL_WARNS() << "Vertex weight count does not match vertex count!" << LL_ENDL;
                }

            }

            // modifier flags?
            bool do_mirror = (mParams.getSculptType() & LL_SCULPT_FLAG_MIRROR);
            bool do_invert = (mParams.getSculptType() &LL_SCULPT_FLAG_INVERT);


            // translate to actions:
            bool do_reflect_x = false;
            bool do_reverse_triangles = false;
            bool do_invert_normals = false;

            if (do_mirror)
            {
                do_reflect_x = true;
                do_reverse_triangles = !do_reverse_triangles;
            }

            if (do_invert)
            {
                do_invert_normals = true;
                do_reverse_triangles = !do_reverse_triangles;
            }

            // now do the work

            if (do_reflect_x)
            {
                LLVector4a* p = (LLVector4a*) face.mPositions;
                LLVector4a* n = (LLVector4a*) face.mNormals;

                for (S32 i = 0; i < face.mNumVertices; i++)
                {
                    p[i].mul(-1.0f);
                    n[i].mul(-1.0f);
                }
            }

            if (do_invert_normals)
            {
                LLVector4a* n = (LLVector4a*) face.mNormals;

                for (S32 i = 0; i < face.mNumVertices; i++)
                {
                    n[i].mul(-1.0f);
                }
            }

            if (do_reverse_triangles)
            {
                for (S32 j = 0; j < face.mNumIndices; j += 3)
                {
                    // swap the 2nd and 3rd index
                    S32 swap = face.mIndices[j+1];
                    face.mIndices[j+1] = face.mIndices[j+2];
                    face.mIndices[j+2] = swap;
                }
            }

            //calculate bounding box
            // VFExtents change
            LLVector4a& min = face.mExtents[0];
            LLVector4a& max = face.mExtents[1];

            if (face.mNumVertices < 3)
            { //empty face, use a dummy 1cm (at 1m scale) bounding box
                min.splat(-0.005f);
                max.splat(0.005f);
            }
            else
            {
                min = max = face.mPositions[0];

                for (S32 i = 1; i < face.mNumVertices; ++i)
                {
                    min.setMin(min, face.mPositions[i]);
                    max.setMax(max, face.mPositions[i]);
                }

                if (face.mTexCoords)
                {
                    LLVector2& min_tc = face.mTexCoordExtents[0];
                    LLVector2& max_tc = face.mTexCoordExtents[1];

                    min_tc = face.mTexCoords[0];
                    max_tc = face.mTexCoords[0];

                    for (S32 j = 1; j < face.mNumVertices; ++j)
                    {
                        update_min_max(min_tc, max_tc, face.mTexCoords[j]);
                    }
                }
                else
                {
                    face.mTexCoordExtents[0].set(0,0);
                    face.mTexCoordExtents[1].set(1,1);
                }
            }
        }
    }

    if (optimize && !cacheOptimize(true))
    {
        // Out of memory?
        LL_WARNS() << "Failed to optimize!" << LL_ENDL;
        mVolumeFaces.clear();
        return false;
    }

    mSculptLevel = 0;  // success!

    return true;
}


bool LLVolume::isMeshAssetLoaded() const
{
    return mIsMeshAssetLoaded;
}

void LLVolume::setMeshAssetLoaded(bool loaded)
{
    mIsMeshAssetLoaded = loaded;
    if (loaded)
    {
        mIsMeshAssetUnavaliable = false;
    }
}

void LLVolume::setMeshAssetUnavaliable(bool unavaliable)
{
    // Don't set it if at least one lod loaded
    if (!mIsMeshAssetLoaded)
    {
        mIsMeshAssetUnavaliable = unavaliable;
    }
}

bool LLVolume::isMeshAssetUnavaliable() const
{
    return mIsMeshAssetUnavaliable;
}

void LLVolume::copyFacesTo(std::vector<LLVolumeFace> &faces) const
{
    faces = mVolumeFaces;
}

void LLVolume::copyFacesFrom(const std::vector<LLVolumeFace> &faces)
{
    mVolumeFaces = faces;
    mSculptLevel = 0;
}

void LLVolume::copyVolumeFaces(const LLVolume* volume)
{
    mVolumeFaces = volume->mVolumeFaces;
    mSculptLevel = 0;
}

bool LLVolume::cacheOptimize(bool gen_tangents)
{
    for (S32 i = 0; i < mVolumeFaces.size(); ++i)
    {
        if (!mVolumeFaces[i].cacheOptimize(gen_tangents))
        {
            return false;
        }
    }
    return true;
}


S32 LLVolume::getNumFaces() const
{
    return mIsMeshAssetLoaded ? getNumVolumeFaces() : (S32)mProfilep->mFaces.size();
}


void LLVolume::createVolumeFaces()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VOLUME;

    if (mGenerateSingleFace)
    {
        // do nothing
    }
    else
    {
        S32 num_faces = getNumFaces();
        bool partial_build = true;
        if (num_faces != mVolumeFaces.size())
        {
            partial_build = false;
            mVolumeFaces.resize(num_faces);
        }
        // Initialize volume faces with parameter data
        for (S32 i = 0; i < (S32)mVolumeFaces.size(); i++)
        {
            LLVolumeFace& vf = mVolumeFaces[i];
            LLProfile::Face& face = mProfilep->mFaces[i];
            vf.mBeginS = face.mIndex;
            vf.mNumS = face.mCount;
            if (vf.mNumS < 0)
            {
                LL_ERRS() << "Volume face corruption detected." << LL_ENDL;
            }

            vf.mBeginT = 0;
            vf.mNumT= getPath().mPath.size();
            vf.mID = i;

            // Set the type mask bits correctly
            if (mParams.getProfileParams().getHollow() > 0)
            {
                vf.mTypeMask |= LLVolumeFace::HOLLOW_MASK;
            }
            if (mProfilep->isOpen())
            {
                vf.mTypeMask |= LLVolumeFace::OPEN_MASK;
            }
            if (face.mCap)
            {
                vf.mTypeMask |= LLVolumeFace::CAP_MASK;
                if (face.mFaceID == LL_FACE_PATH_BEGIN)
                {
                    vf.mTypeMask |= LLVolumeFace::TOP_MASK;
                }
                else
                {
                    llassert(face.mFaceID == LL_FACE_PATH_END);
                    vf.mTypeMask |= LLVolumeFace::BOTTOM_MASK;
                }
            }
            else if (face.mFaceID & (LL_FACE_PROFILE_BEGIN | LL_FACE_PROFILE_END))
            {
                vf.mTypeMask |= LLVolumeFace::FLAT_MASK | LLVolumeFace::END_MASK;
            }
            else
            {
                vf.mTypeMask |= LLVolumeFace::SIDE_MASK;
                if (face.mFlat)
                {
                    vf.mTypeMask |= LLVolumeFace::FLAT_MASK;
                }
                if (face.mFaceID & LL_FACE_INNER_SIDE)
                {
                    vf.mTypeMask |= LLVolumeFace::INNER_MASK;
                    if (face.mFlat && vf.mNumS > 2)
                    { //flat inner faces have to copy vert normals
                        vf.mNumS = vf.mNumS*2;
                        if (vf.mNumS < 0)
                        {
                            LL_ERRS() << "Volume face corruption detected." << LL_ENDL;
                        }
                    }
                }
                else
                {
                    vf.mTypeMask |= LLVolumeFace::OUTER_MASK;
                }
            }
        }

        for (face_list_t::iterator iter = mVolumeFaces.begin();
             iter != mVolumeFaces.end(); ++iter)
        {
            (*iter).create(this, partial_build);
        }
    }
}


inline LLVector4a sculpt_rgb_to_vector(U8 r, U8 g, U8 b)
{
    // maps RGB values to vector values [0..255] -> [-0.5..0.5]
    LLVector4a value;
    LLVector4a sub(0.5f, 0.5f, 0.5f);

    value.set(r,g,b);
    value.mul(1.f/255.f);
    value.sub(sub);

    return value;
}

inline U32 sculpt_xy_to_index(U32 x, U32 y, U16 sculpt_width, U16 sculpt_height, S8 sculpt_components)
{
    U32 index = (x + y * sculpt_width) * sculpt_components;
    return index;
}


inline U32 sculpt_st_to_index(S32 s, S32 t, S32 size_s, S32 size_t, U16 sculpt_width, U16 sculpt_height, S8 sculpt_components)
{
    U32 x = (U32) ((F32)s/(size_s) * (F32) sculpt_width);
    U32 y = (U32) ((F32)t/(size_t) * (F32) sculpt_height);

    return sculpt_xy_to_index(x, y, sculpt_width, sculpt_height, sculpt_components);
}


inline LLVector4a sculpt_index_to_vector(U32 index, const U8* sculpt_data)
{
    LLVector4a v = sculpt_rgb_to_vector(sculpt_data[index], sculpt_data[index+1], sculpt_data[index+2]);

    return v;
}

inline LLVector4a sculpt_st_to_vector(S32 s, S32 t, S32 size_s, S32 size_t, U16 sculpt_width, U16 sculpt_height, S8 sculpt_components, const U8* sculpt_data)
{
    U32 index = sculpt_st_to_index(s, t, size_s, size_t, sculpt_width, sculpt_height, sculpt_components);

    return sculpt_index_to_vector(index, sculpt_data);
}

inline LLVector4a sculpt_xy_to_vector(U32 x, U32 y, U16 sculpt_width, U16 sculpt_height, S8 sculpt_components, const U8* sculpt_data)
{
    U32 index = sculpt_xy_to_index(x, y, sculpt_width, sculpt_height, sculpt_components);

    return sculpt_index_to_vector(index, sculpt_data);
}

// NaCl - Graphics crasher protection
void LLVolume::calcSurfaceArea()
{
  mSurfaceArea = sculptGetSurfaceArea();
}
// NaCl End

F32 LLVolume::sculptGetSurfaceArea()
{
    // test to see if image has enough variation to create non-degenerate geometry

    F32 area = 0;

    S32 sizeS = mPathp->mPath.size();
    S32 sizeT = mProfilep->mProfile.size();

    for (S32 s = 0; s < sizeS-1; s++)
    {
        for (S32 t = 0; t < sizeT-1; t++)
        {
            // get four corners of quad
            LLVector4a& p1 = mMesh[(s  )*sizeT + (t  )];
            LLVector4a& p2 = mMesh[(s+1)*sizeT + (t  )];
            LLVector4a& p3 = mMesh[(s  )*sizeT + (t+1)];
            LLVector4a& p4 = mMesh[(s+1)*sizeT + (t+1)];

            // compute the area of the quad by taking the length of the cross product of the two triangles
            LLVector4a v0,v1,v2,v3;
            v0.setSub(p1,p2);
            v1.setSub(p1,p3);
            v2.setSub(p4,p2);
            v3.setSub(p4,p3);

            LLVector4a cross1, cross2;
            cross1.setCross3(v0,v1);
            cross2.setCross3(v2,v3);

            //LLVector3 cross1 = (p1 - p2) % (p1 - p3);
            //LLVector3 cross2 = (p4 - p2) % (p4 - p3);

            area += (cross1.getLength3() + cross2.getLength3()).getF32() / 2.f;
        }
    }

    return area;
}

// create empty placeholder shape
void LLVolume::sculptGenerateEmptyPlaceholder()
{
    S32 sizeS = mPathp->mPath.size();
    S32 sizeT = mProfilep->mProfile.size();

    S32 line = 0;

    for (S32 s = 0; s < sizeS; s++)
    {
        for (S32 t = 0; t < sizeT; t++)
        {
            S32 i = t + line;
            LLVector4a& pt = mMesh[i];

            F32* p = pt.getF32ptr();

            p[0] = 0;
            p[1] = 0;
            p[2] = 0;

            llassert(pt.isFinite3());
        }
        line += sizeT;
    }
}

// create sphere placeholder shape
void LLVolume::sculptGenerateSpherePlaceholder()
{
    S32 sizeS = mPathp->mPath.size();
    S32 sizeT = mProfilep->mProfile.size();

    S32 line = 0;

    for (S32 s = 0; s < sizeS; s++)
    {
        for (S32 t = 0; t < sizeT; t++)
        {
            S32 i = t + line;
            LLVector4a& pt = mMesh[i];


            F32 u = (F32)s / (sizeS - 1);
            F32 v = (F32)t / (sizeT - 1);

            const F32 RADIUS = (F32) 0.3;

            F32* p = pt.getF32ptr();

            p[0] = (F32)(sin(F_PI * v) * cos(2.0 * F_PI * u) * RADIUS);
            p[1] = (F32)(sin(F_PI * v) * sin(2.0 * F_PI * u) * RADIUS);
            p[2] = (F32)(cos(F_PI * v) * RADIUS);

            llassert(pt.isFinite3());
        }
        line += sizeT;
    }
}

// create the vertices from the map
void LLVolume::sculptGenerateMapVertices(U16 sculpt_width, U16 sculpt_height, S8 sculpt_components, const U8* sculpt_data, U8 sculpt_type)
{
    U8 sculpt_stitching = sculpt_type & LL_SCULPT_TYPE_MASK;
    bool sculpt_invert = sculpt_type & LL_SCULPT_FLAG_INVERT;
    bool sculpt_mirror = sculpt_type & LL_SCULPT_FLAG_MIRROR;
    bool reverse_horizontal = (sculpt_invert ? !sculpt_mirror : sculpt_mirror);  // XOR

    S32 sizeS = mPathp->mPath.size();
    S32 sizeT = mProfilep->mProfile.size();

    S32 line = 0;
    for (S32 s = 0; s < sizeS; s++)
    {
        // Run along the profile.
        for (S32 t = 0; t < sizeT; t++)
        {
            S32 i = t + line;
            LLVector4a& pt = mMesh[i];

            S32 reversed_t = t;

            if (reverse_horizontal)
            {
                reversed_t = sizeT - t - 1;
            }

            U32 x = (U32) ((F32)reversed_t/(sizeT-1) * (F32) sculpt_width);
            U32 y = (U32) ((F32)s/(sizeS-1) * (F32) sculpt_height);


            if (y == 0)  // top row stitching
            {
                // pinch?
                if (sculpt_stitching == LL_SCULPT_TYPE_SPHERE)
                {
                    x = sculpt_width / 2;
                }
            }

            if (y == sculpt_height)  // bottom row stitching
            {
                // wrap?
                if (sculpt_stitching == LL_SCULPT_TYPE_TORUS)
                {
                    y = 0;
                }
                else
                {
                    y = sculpt_height - 1;
                }

                // pinch?
                if (sculpt_stitching == LL_SCULPT_TYPE_SPHERE)
                {
                    x = sculpt_width / 2;
                }
            }

            if (x == sculpt_width)   // side stitching
            {
                // wrap?
                if ((sculpt_stitching == LL_SCULPT_TYPE_SPHERE) ||
                    (sculpt_stitching == LL_SCULPT_TYPE_TORUS) ||
                    (sculpt_stitching == LL_SCULPT_TYPE_CYLINDER))
                {
                    x = 0;
                }

                else
                {
                    x = sculpt_width - 1;
                }
            }

            pt = sculpt_xy_to_vector(x, y, sculpt_width, sculpt_height, sculpt_components, sculpt_data);

            if (sculpt_mirror)
            {
                LLVector4a scale(-1.f,1,1,1);
                pt.mul(scale);
            }

            llassert(pt.isFinite3());
        }

        line += sizeT;
    }
}


constexpr S32 SCULPT_REZ_1 = 6;  // changed from 4 to 6 - 6 looks round whereas 4 looks square
constexpr S32 SCULPT_REZ_2 = 8;
constexpr S32 SCULPT_REZ_3 = 16;
constexpr S32 SCULPT_REZ_4 = 32;

S32 sculpt_sides(F32 detail)
{
    // detail is usually one of: 1, 1.5, 2.5, 4.0.

    if (detail <= 1.0)
    {
        return SCULPT_REZ_1;
    }
    if (detail <= 2.0)
    {
        return SCULPT_REZ_2;
    }
    if (detail <= 3.0)
    {
        return SCULPT_REZ_3;
    }
    else
    {
        return SCULPT_REZ_4;
    }
}



// determine the number of vertices in both s and t direction for this sculpt
void sculpt_calc_mesh_resolution(U16 width, U16 height, U8 type, F32 detail, S32& s, S32& t)
{
    // this code has the following properties:
    // 1) the aspect ratio of the mesh is as close as possible to the ratio of the map
    //    while still using all available verts
    // 2) the mesh cannot have more verts than is allowed by LOD
    // 3) the mesh cannot have more verts than is allowed by the map

    S32 max_vertices_lod = (S32)pow((double)sculpt_sides(detail), 2.0);
    S32 max_vertices_map = width * height / 4;

    S32 vertices;
    if (max_vertices_map > 0)
        vertices = llmin(max_vertices_lod, max_vertices_map);
    else
        vertices = max_vertices_lod;


    F32 ratio;
    if ((width == 0) || (height == 0))
        ratio = 1.f;
    else
        ratio = (F32) width / (F32) height;


    s = (S32)(F32) sqrt(((F32)vertices / ratio));

    s = llmax(s, 4);              // no degenerate sizes, please
    t = vertices / s;

    t = llmax(t, 4);              // no degenerate sizes, please
    s = vertices / t;
}

// sculpt replaces generate() for sculpted surfaces
void LLVolume::sculpt(U16 sculpt_width, U16 sculpt_height, S8 sculpt_components, const U8* sculpt_data, S32 sculpt_level, bool visible_placeholder)
{
    U8 sculpt_type = mParams.getSculptType();

    bool data_is_empty = false;

    if (sculpt_width == 0 || sculpt_height == 0 || sculpt_components < 3 || sculpt_data == NULL)
    {
        sculpt_level = -1;
        data_is_empty = true;
    }

    S32 requested_sizeS = 0;
    S32 requested_sizeT = 0;

    sculpt_calc_mesh_resolution(sculpt_width, sculpt_height, sculpt_type, mDetail, requested_sizeS, requested_sizeT);

    mPathp->generate(mParams.getPathParams(), mDetail, 0, true, requested_sizeS);
    mProfilep->generate(mParams.getProfileParams(), mPathp->isOpen(), mDetail, 0, true, requested_sizeT);

    S32 sizeS = mPathp->mPath.size();         // we requested a specific size, now see what we really got
    S32 sizeT = mProfilep->mProfile.size();   // we requested a specific size, now see what we really got

    // weird crash bug - DEV-11158 - trying to collect more data:
    if ((sizeS == 0) || (sizeT == 0))
    {
        LL_WARNS() << "sculpt bad mesh size " << sizeS << " " << sizeT << LL_ENDL;
    }

    sNumMeshPoints -= mMesh.size();
    mMesh.resize(sizeS * sizeT);
    sNumMeshPoints += mMesh.size();

    //generate vertex positions
    if (!data_is_empty)
    {
        sculptGenerateMapVertices(sculpt_width, sculpt_height, sculpt_components, sculpt_data, sculpt_type);

        // don't test lowest LOD to support legacy content DEV-33670
        if (mDetail > SCULPT_MIN_AREA_DETAIL)
        {
            F32 area = sculptGetSurfaceArea();

            mSurfaceArea = area;

            const F32 SCULPT_MAX_AREA = 384.f;

            if (area < SCULPT_MIN_AREA || area > SCULPT_MAX_AREA)
            {
                data_is_empty = true;
                visible_placeholder = true;
            }
        }
    }

    if (data_is_empty)
    {
        if (visible_placeholder)
        {
            // Object should be visible since there will be nothing else to display
            sculptGenerateSpherePlaceholder();
        }
        else
        {
            sculptGenerateEmptyPlaceholder();
        }
    }

    for (S32 i = 0; i < (S32)mProfilep->mFaces.size(); i++)
    {
        mFaceMask |= mProfilep->mFaces[i].mFaceID;
    }

    mSculptLevel = sculpt_level;

    // Delete any existing faces so that they get regenerated
    mVolumeFaces.clear();

    createVolumeFaces();
}




bool LLVolume::isCap(S32 face)
{
    return mProfilep->mFaces[face].mCap;
}

bool LLVolume::isFlat(S32 face)
{
    return mProfilep->mFaces[face].mFlat;
}


bool LLVolumeParams::isSculpt() const
{
    return (mSculptType & LL_SCULPT_TYPE_MASK) != LL_SCULPT_TYPE_NONE;
}

bool LLVolumeParams::isMeshSculpt() const
{
    return (mSculptType & LL_SCULPT_TYPE_MASK) == LL_SCULPT_TYPE_MESH;
}

bool LLVolumeParams::operator==(const LLVolumeParams &params) const
{
    return ( (getPathParams() == params.getPathParams()) &&
             (getProfileParams() == params.getProfileParams()) &&
             (mSculptID == params.mSculptID) &&
             (mSculptType == params.mSculptType) );
}

bool LLVolumeParams::operator!=(const LLVolumeParams &params) const
{
    return ( (getPathParams() != params.getPathParams()) ||
             (getProfileParams() != params.getProfileParams()) ||
             (mSculptID != params.mSculptID) ||
             (mSculptType != params.mSculptType) );
}

bool LLVolumeParams::operator<(const LLVolumeParams &params) const
{
    if( getPathParams() != params.getPathParams() )
    {
        return getPathParams() < params.getPathParams();
    }

    if (getProfileParams() != params.getProfileParams())
    {
        return getProfileParams() < params.getProfileParams();
    }

    if (mSculptID != params.mSculptID)
    {
        return mSculptID < params.mSculptID;
    }

    return mSculptType < params.mSculptType;


}

void LLVolumeParams::copyParams(const LLVolumeParams &params)
{
    mProfileParams.copyParams(params.mProfileParams);
    mPathParams.copyParams(params.mPathParams);
    mSculptID = params.getSculptID();
    mSculptType = params.getSculptType();
}

// Less restricitve approx 0 for volumes
constexpr F32 APPROXIMATELY_ZERO = 0.001f;
bool approx_zero( F32 f, F32 tolerance = APPROXIMATELY_ZERO)
{
    return (f >= -tolerance) && (f <= tolerance);
}

// return true if in range (or nearly so)
static bool limit_range(F32& v, F32 min, F32 max, F32 tolerance = APPROXIMATELY_ZERO)
{
    F32 min_delta = v - min;
    if (min_delta < 0.f)
    {
        v = min;
        if (!approx_zero(min_delta, tolerance))
            return false;
    }
    F32 max_delta = max - v;
    if (max_delta < 0.f)
    {
        v = max;
        if (!approx_zero(max_delta, tolerance))
            return false;
    }
    return true;
}

bool LLVolumeParams::setBeginAndEndS(const F32 b, const F32 e)
{
    bool valid = true;

    // First, clamp to valid ranges.
    F32 begin = b;
    valid &= limit_range(begin, 0.f, 1.f - MIN_CUT_DELTA);

    F32 end = e;
    if (end >= .0149f && end < MIN_CUT_DELTA) end = MIN_CUT_DELTA; // eliminate warning for common rounding error
    valid &= limit_range(end, MIN_CUT_DELTA, 1.f);

    valid &= limit_range(begin, 0.f, end - MIN_CUT_DELTA, .01f);

    // Now set them.
    mProfileParams.setBegin(begin);
    mProfileParams.setEnd(end);

    return valid;
}

bool LLVolumeParams::setBeginAndEndT(const F32 b, const F32 e)
{
    bool valid = true;

    // First, clamp to valid ranges.
    F32 begin = b;
    valid &= limit_range(begin, 0.f, 1.f - MIN_CUT_DELTA);

    F32 end = e;
    valid &= limit_range(end, MIN_CUT_DELTA, 1.f);

    valid &= limit_range(begin, 0.f, end - MIN_CUT_DELTA, .01f);

    // Now set them.
    mPathParams.setBegin(begin);
    mPathParams.setEnd(end);

    return valid;
}

bool LLVolumeParams::setHollow(const F32 h)
{
    // Validate the hollow based on path and profile.
    U8 profile  = mProfileParams.getCurveType() & LL_PCODE_PROFILE_MASK;
    U8 hole_type    = mProfileParams.getCurveType() & LL_PCODE_HOLE_MASK;

    F32 max_hollow = HOLLOW_MAX;

    // Only square holes have trouble.
    if (LL_PCODE_HOLE_SQUARE == hole_type)
    {
        switch(profile)
        {
        case LL_PCODE_PROFILE_CIRCLE:
        case LL_PCODE_PROFILE_CIRCLE_HALF:
        case LL_PCODE_PROFILE_EQUALTRI:
            max_hollow = HOLLOW_MAX_SQUARE;
        }
    }

    F32 hollow = h;
    bool valid = limit_range(hollow, HOLLOW_MIN, max_hollow);
    mProfileParams.setHollow(hollow);

    return valid;
}

bool LLVolumeParams::setTwistBegin(const F32 b)
{
    F32 twist_begin = b;
    bool valid = limit_range(twist_begin, TWIST_MIN, TWIST_MAX);
    mPathParams.setTwistBegin(twist_begin);
    return valid;
}

bool LLVolumeParams::setTwistEnd(const F32 e)
{
    F32 twist_end = e;
    bool valid = limit_range(twist_end, TWIST_MIN, TWIST_MAX);
    mPathParams.setTwistEnd(twist_end);
    return valid;
}

bool LLVolumeParams::setRatio(const F32 x, const F32 y)
{
    F32 min_x = RATIO_MIN;
    F32 max_x = RATIO_MAX;
    F32 min_y = RATIO_MIN;
    F32 max_y = RATIO_MAX;
    // If this is a circular path (and not a sphere) then 'ratio' is actually hole size.
    U8 path_type    = mPathParams.getCurveType();
    U8 profile_type = mProfileParams.getCurveType() & LL_PCODE_PROFILE_MASK;
    if ( LL_PCODE_PATH_CIRCLE == path_type &&
         LL_PCODE_PROFILE_CIRCLE_HALF != profile_type)
    {
        // Holes are more restricted...
        min_x = HOLE_X_MIN;
        max_x = HOLE_X_MAX;
        min_y = HOLE_Y_MIN;
        max_y = HOLE_Y_MAX;
    }

    F32 ratio_x = x;
    bool valid = limit_range(ratio_x, min_x, max_x);
    F32 ratio_y = y;
    valid &= limit_range(ratio_y, min_y, max_y);

    mPathParams.setScale(ratio_x, ratio_y);

    return valid;
}

bool LLVolumeParams::setShear(const F32 x, const F32 y)
{
    F32 shear_x = x;
    bool valid = limit_range(shear_x, SHEAR_MIN, SHEAR_MAX);
    F32 shear_y = y;
    valid &= limit_range(shear_y, SHEAR_MIN, SHEAR_MAX);
    mPathParams.setShear(shear_x, shear_y);
    return valid;
}

bool LLVolumeParams::setTaperX(const F32 v)
{
    F32 taper = v;
    bool valid = limit_range(taper, TAPER_MIN, TAPER_MAX);
    mPathParams.setTaperX(taper);
    return valid;
}

bool LLVolumeParams::setTaperY(const F32 v)
{
    F32 taper = v;
    bool valid = limit_range(taper, TAPER_MIN, TAPER_MAX);
    mPathParams.setTaperY(taper);
    return valid;
}

bool LLVolumeParams::setRevolutions(const F32 r)
{
    F32 revolutions = r;
    bool valid = limit_range(revolutions, REV_MIN, REV_MAX);
    mPathParams.setRevolutions(revolutions);
    return valid;
}

bool LLVolumeParams::setRadiusOffset(const F32 offset)
{
    bool valid = true;

    // If this is a sphere, just set it to 0 and get out.
    U8 path_type    = mPathParams.getCurveType();
    U8 profile_type = mProfileParams.getCurveType() & LL_PCODE_PROFILE_MASK;
    if ( LL_PCODE_PROFILE_CIRCLE_HALF == profile_type ||
        LL_PCODE_PATH_CIRCLE != path_type )
    {
        mPathParams.setRadiusOffset(0.f);
        return true;
    }

    // Limit radius offset, based on taper and hole size y.
    F32 radius_offset   = offset;
    F32 taper_y         = getTaperY();
    F32 radius_mag      = fabs(radius_offset);
    F32 hole_y_mag      = fabs(getRatioY());
    F32 taper_y_mag     = fabs(taper_y);
    // Check to see if the taper effects us.
    if ( (radius_offset > 0.f && taper_y < 0.f) ||
            (radius_offset < 0.f && taper_y > 0.f) )
    {
        // The taper does not help increase the radius offset range.
        taper_y_mag = 0.f;
    }
    F32 max_radius_mag = 1.f - hole_y_mag * (1.f - taper_y_mag) / (1.f - hole_y_mag);

    // Enforce the maximum magnitude.
    F32 delta = max_radius_mag - radius_mag;
    if (delta < 0.f)
    {
        // Check radius offset sign.
        if (radius_offset < 0.f)
        {
            radius_offset = -max_radius_mag;
        }
        else
        {
            radius_offset = max_radius_mag;
        }
        valid = approx_zero(delta, .1f);
    }

    mPathParams.setRadiusOffset(radius_offset);
    return valid;
}

bool LLVolumeParams::setSkew(const F32 skew_value)
{
    bool valid = true;

    // Check the skew value against the revolutions.
    F32 skew        = llclamp(skew_value, SKEW_MIN, SKEW_MAX);
    F32 skew_mag    = fabs(skew);
    F32 revolutions = getRevolutions();
    F32 scale_x     = getRatioX();
    F32 min_skew_mag = 1.0f - 1.0f / (revolutions * scale_x + 1.0f);
    // Discontinuity; A revolution of 1 allows skews below 0.5.
    if ( fabs(revolutions - 1.0f) < 0.001)
        min_skew_mag = 0.0f;

    // Clip skew.
    F32 delta = skew_mag - min_skew_mag;
    if (delta < 0.f)
    {
        // Check skew sign.
        if (skew < 0.0f)
        {
            skew = -min_skew_mag;
        }
        else
        {
            skew = min_skew_mag;
        }
        valid = approx_zero(delta, .01f);
    }

    mPathParams.setSkew(skew);
    return valid;
}

bool LLVolumeParams::setSculptID(const LLUUID& sculpt_id, U8 sculpt_type)
{
    mSculptID = sculpt_id;
    mSculptType = sculpt_type;
    return true;
}

bool LLVolumeParams::setType(U8 profile, U8 path)
{
    bool result = true;
    // First, check profile and path for validity.
    U8 profile_type = profile & LL_PCODE_PROFILE_MASK;
    U8 hole_type    = (profile & LL_PCODE_HOLE_MASK) >> 4;
    U8 path_type    = path >> 4;

    if (profile_type > LL_PCODE_PROFILE_MAX)
    {
        // Bad profile.  Make it square.
        profile = LL_PCODE_PROFILE_SQUARE;
        result = false;
        LL_WARNS() << "LLVolumeParams::setType changing bad profile type (" << profile_type
                << ") to be LL_PCODE_PROFILE_SQUARE" << LL_ENDL;
    }
    else if (hole_type > LL_PCODE_HOLE_MAX)
    {
        // Bad hole.  Make it the same.
        profile = profile_type;
        result = false;
        LL_WARNS() << "LLVolumeParams::setType changing bad hole type (" << hole_type
                << ") to be LL_PCODE_HOLE_SAME" << LL_ENDL;
    }

    if (path_type < LL_PCODE_PATH_MIN ||
        path_type > LL_PCODE_PATH_MAX)
    {
        // Bad path.  Make it linear.
        result = false;
        LL_WARNS() << "LLVolumeParams::setType changing bad path (" << path
                << ") to be LL_PCODE_PATH_LINE" << LL_ENDL;
        path = LL_PCODE_PATH_LINE;
    }

    mProfileParams.setCurveType(profile);
    mPathParams.setCurveType(path);
    return result;
}

// static
bool LLVolumeParams::validate(U8 prof_curve, F32 prof_begin, F32 prof_end, F32 hollow,
        U8 path_curve, F32 path_begin, F32 path_end,
        F32 scx, F32 scy, F32 shx, F32 shy,
        F32 twistend, F32 twistbegin, F32 radiusoffset,
        F32 tx, F32 ty, F32 revolutions, F32 skew)
{
    LLVolumeParams test_params;
    if (!test_params.setType        (prof_curve, path_curve))
    {
            return false;
    }
    if (!test_params.setBeginAndEndS    (prof_begin, prof_end))
    {
            return false;
    }
    if (!test_params.setBeginAndEndT    (path_begin, path_end))
    {
            return false;
    }
    if (!test_params.setHollow      (hollow))
    {
            return false;
    }
    if (!test_params.setTwistBegin      (twistbegin))
    {
            return false;
    }
    if (!test_params.setTwistEnd        (twistend))
    {
            return false;
    }
    if (!test_params.setRatio       (scx, scy))
    {
            return false;
    }
    if (!test_params.setShear       (shx, shy))
    {
            return false;
    }
    if (!test_params.setTaper       (tx, ty))
    {
            return false;
    }
    if (!test_params.setRevolutions     (revolutions))
    {
            return false;
    }
    if (!test_params.setRadiusOffset    (radiusoffset))
    {
            return false;
    }
    if (!test_params.setSkew        (skew))
    {
            return false;
    }
    return true;
}

// <FS:ND> Cache LOD Triangle counts, it is expensive to calculate them each time.
//  static void getLoDTriangleCounts(const LLVolumeParams& params, S32* counts);
//void LLVolume::getLoDTriangleCounts(const LLVolumeParams& params, S32* counts)
void LLVolume::getLoDTriangleCounts(const LLVolumeParams& params, S32* counts, LLVolume *aVolume)
// </FS:ND>
{ //attempt to approximate the number of triangles that will result from generating a volume LoD set for the
    //supplied LLVolumeParams -- inaccurate, but a close enough approximation for determining streaming cost
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VOLUME;

    // <FS:ND> check cache first
    if( aVolume->mTrianglesCache && aVolume->mTrianglesCache->mPathParams == params.getPathParams() && aVolume->mTrianglesCache->mProfileParams == params.getProfileParams() )
    {
        counts[ 0 ] = aVolume->mTrianglesCache->mTriangles[0];
        counts[ 1 ] = aVolume->mTrianglesCache->mTriangles[1];
        counts[ 2 ] = aVolume->mTrianglesCache->mTriangles[2];
        counts[ 3 ] = aVolume->mTrianglesCache->mTriangles[3];
        return;
    }

    if( !aVolume->mTrianglesCache )
        aVolume->mTrianglesCache = new TrianglesPerLODCache();

    aVolume->mTrianglesCache->mPathParams = params.getPathParams();
    aVolume->mTrianglesCache->mProfileParams = params.getProfileParams();
    // </FS:ND>

    F32 detail[] = {1.f, 1.5f, 2.5f, 4.f};
    for (S32 i = 0; i < 4; i++)
    {
        S32 count = 0;
        S32 path_points = LLPath::getNumPoints(params.getPathParams(), detail[i]);
        S32 profile_points = LLProfile::getNumPoints(params.getProfileParams(), false, detail[i]);

        count = (profile_points-1)*2*(path_points-1);
        count += profile_points*2;

        counts[i] = count;
        aVolume->mTrianglesCache->mTriangles[i] = count; // </FS:ND>
    }
}


S32 LLVolume::getNumTriangles(S32* vcount) const
{
    U32 triangle_count = 0;
    U32 vertex_count = 0;

    for (S32 i = 0; i < getNumVolumeFaces(); ++i)
    {
        const LLVolumeFace& face = getVolumeFace(i);
        triangle_count += face.mNumIndices/3;

        vertex_count += face.mNumVertices;
    }


    if (vcount)
    {
        *vcount = vertex_count;
    }

    return triangle_count;
}

void LLVolumeFace::generateSilhouetteEdge(const LLVolume* volume, std::vector<S32>& edge) const
{
    llassert(edge.empty()); // edge is supposed to be a scratch array

    if (volume->isMeshAssetLoaded()) { return; }

    if (mTypeMask & CAP_MASK)
    {
        // Logic copied from LLVolumeFace::createCap - indicates a face created via
        // createUnCutCubeCap.
        if (!(mTypeMask & HOLLOW_MASK) &&
            !(mTypeMask & OPEN_MASK) &&
            ((volume->getParams().getPathParams().getBegin()==0.0f)&&
            (volume->getParams().getPathParams().getEnd()==1.0f))&&
            (volume->getParams().getProfileParams().getCurveType()==LL_PCODE_PROFILE_SQUARE &&
             volume->getParams().getPathParams().getCurveType()==LL_PCODE_PATH_LINE)
            )
        {
            LL_PROFILE_ZONE_NAMED_CATEGORY_VOLUME("llvfgse - CAP_MASK");

            const LLAlignedArray<LLVector4a,64>& profile = volume->getProfile().mProfile;
            S32 grid_size = (profile.size()-1)/4;
            edge.resize(mNumIndices);
            llassert(edge.size() == 6*grid_size*grid_size);

            S32 cur_edge = 0;
            for(S32 gx = 0;gx<grid_size;gx++)
            {
                for(S32 gy = 0;gy<grid_size;gy++)
                {
                    if (mTypeMask & TOP_MASK)
                    {

                        S32 edge_value = grid_size * 2 * gy + gx * 2;

                        if (gx > 0)
                        {
                            edge[cur_edge++] = edge_value;
                        }
                        else
                        {
                            edge[cur_edge++] = -1; // Mark face to higlight it
                        }

                        if (gy < grid_size - 1)
                        {
                            edge[cur_edge++] = edge_value;
                        }
                        else
                        {
                            edge[cur_edge++] = -1;
                        }

                        edge[cur_edge++] = edge_value;

                        if (gx < grid_size - 1)
                        {
                            edge[cur_edge++] = edge_value;
                        }
                        else
                        {
                            edge[cur_edge++] = -1;
                        }

                        if (gy > 0)
                        {
                            edge[cur_edge++] = edge_value;
                        }
                        else
                        {
                            edge[cur_edge++] = -1;
                        }

                        edge[cur_edge++] = edge_value;
                    }
                    else
                    {
                        S32 edge_value = grid_size * 2 * gy + gx * 2;

                        if (gy > 0)
                        {
                            edge[cur_edge++] = edge_value;
                        }
                        else
                        {
                            edge[cur_edge++] = -1;
                        }

                        if (gx < grid_size - 1)
                        {
                            edge[cur_edge++] = edge_value;
                        }
                        else
                        {
                            edge[cur_edge++] = -1;
                        }

                        edge[cur_edge++] = edge_value;

                        if (gy < grid_size - 1)
                        {
                            edge[cur_edge++] = edge_value;
                        }
                        else
                        {
                            edge[cur_edge++] = -1;
                        }

                        if (gx > 0)
                        {
                            edge[cur_edge++] = edge_value;
                        }
                        else
                        {
                            edge[cur_edge++] = -1;
                        }

                        edge[cur_edge++] = edge_value;
                    }
                }
            }
        }
    }
    else if ((mTypeMask & END_MASK) || (mTypeMask & SIDE_MASK))
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_VOLUME("llvfgse - END_MASK or SIDE_MASK");

        edge.resize(mNumIndices);
        llassert(edge.size() == 6*(mNumS-1)*(mNumT-1));

        S32 cur_edge = 0;
        const bool flat_face = mTypeMask & FLAT_MASK;
        for (S32 t = 0; t < (mNumT-1); t++)
        {
            for (S32 s = 0; s < (mNumS-1); s++)
            {
                // bottom left/top right neighbor face
                edge[cur_edge++] = (mNumS-1)*2*t+s*2+1;

                if (t < mNumT-2)
                {   // top right/top left neighbor face
                    edge[cur_edge++] = (mNumS-1)*2*(t+1)+s*2+1;
                }
                else if (mNumT <= 3 || volume->getPath().isOpen())
                {   // no neighbor
                    edge[cur_edge++] = -1;
                }
                else
                {   // wrap on T
                    edge[cur_edge++] = s*2+1;
                }

                if (s > 0)
                {   // top left/bottom left neighbor face
                    edge[cur_edge++] = (mNumS-1)*2*t+s*2-1;
                }
                else if (flat_face || volume->getProfile().isOpen())
                {   // no neighbor
                    edge[cur_edge++] = -1;
                }
                else
                {   // wrap on S
                    edge[cur_edge++] = (mNumS-1)*2*t+(mNumS-2)*2+1;
                }

                if (t > 0)
                {   // bottom left/bottom right neighbor face
                    edge[cur_edge++] = (mNumS-1)*2*(t-1)+s*2;
                }
                else if (mNumT <= 3 || volume->getPath().isOpen())
                {   // no neighbor
                    edge[cur_edge++] = -1;
                }
                else
                {   // wrap on T
                    edge[cur_edge++] = (mNumS-1)*2*(mNumT-2)+s*2;
                }

                if (s < mNumS-2)
                {   // bottom right/top right neighbor face
                    edge[cur_edge++] = (mNumS-1)*2*t+(s+1)*2;
                }
                else if (flat_face || volume->getProfile().isOpen())
                {   // no neighbor
                    edge[cur_edge++] = -1;
                }
                else
                {   // wrap on S
                    edge[cur_edge++] = (mNumS-1)*2*t;
                }

                // top right/bottom left neighbor face
                edge[cur_edge++] = (mNumS-1)*2*t+s*2;
            }
        }
    }
    else
    {
        LL_ERRS() << "Unknown/uninitialized face type!" << LL_ENDL;
    }
}

//-----------------------------------------------------------------------------
// generateSilhouetteVertices()
//-----------------------------------------------------------------------------
void LLVolume::generateSilhouetteVertices(std::vector<LLVector3> &vertices,
                                          std::vector<LLVector3> &normals,
                                          const LLVector3& obj_cam_vec_in,
                                          const LLMatrix4& mat_in,
                                          const LLMatrix3& norm_mat_in,
                                          S32 face_mask)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VOLUME;

    LLMatrix4a mat;
    mat.loadu(mat_in);

    LLMatrix4a norm_mat;
    norm_mat.loadu(norm_mat_in);

    LLVector4a obj_cam_vec;
    obj_cam_vec.load3(obj_cam_vec_in.mV);

    vertices.clear();
    normals.clear();

    if ((mParams.getSculptType() & LL_SCULPT_TYPE_MASK) == LL_SCULPT_TYPE_MESH)
    {
        return;
    }

    S32 cur_index = 0;
    // Scratch array for per-face silhouette edge information. This also has a
    // lot of dev-only debug information that we might not care about anymore.
    // (see DEBUG_SILHOUETTE_EDGE_MAP)
    // *TODO: Consider removing the debug associated with
    // DEBUG_SILHOUETTE_EDGE_MAP, and remove its associated computational
    // overhead in generateSilhouetteEdge.
    std::vector<S32> edge;
    //for each face
    for (face_list_t::iterator iter = mVolumeFaces.begin();
         iter != mVolumeFaces.end(); ++iter)
    {
        LLVolumeFace& face = *iter;

        if (!(face_mask & (0x1 << cur_index++)) ||
             face.mNumIndices == 0)
        {
            continue;
        }
        // Attempt to generate "edge" info for this silhouette, which is used
        // for some prims. If the edge array remains empty, then this
        // silhouette generation method is not supported for this face.
        edge.clear();
        face.generateSilhouetteEdge(this, edge);
        if (edge.empty())
        {
            continue;
        }

        if (face.mTypeMask & (LLVolumeFace::CAP_MASK))
        {
            LLVector4a* v = (LLVector4a*)face.mPositions;
            LLVector4a* n = (LLVector4a*)face.mNormals;

            for (S32 j = 0; j < face.mNumIndices / 3; j++)
            {
                for (S32 k = 0; k < 3; k++)
                {
                    S32 index = edge[j * 3 + k];

                    if (index == -1)
                    {
                        // silhouette edge, currently only cubes, so no other conditions

                        S32 v1 = face.mIndices[j * 3 + k];
                        S32 v2 = face.mIndices[j * 3 + ((k + 1) % 3)];

                        LLVector4a t;
                        mat.affineTransform(v[v1], t);
                        vertices.push_back(LLVector3(t[0], t[1], t[2]));

                        norm_mat.rotate(n[v1], t);

                        t.normalize3fast();
                        normals.push_back(LLVector3(t[0], t[1], t[2]));

                        mat.affineTransform(v[v2], t);
                        vertices.push_back(LLVector3(t[0], t[1], t[2]));

                        norm_mat.rotate(n[v2], t);
                        t.normalize3fast();
                        normals.push_back(LLVector3(t[0], t[1], t[2]));
                    }
                }
            }

        }
        else
        {
            //==============================================
            //DEBUG draw edge map instead of silhouette edge
            //==============================================

#if DEBUG_SILHOUETTE_EDGE_MAP

            //for each triangle
            U32 tri_count = face.mNumIndices / 3;
            for (U32 j = 0; j < tri_count; j++) {
                //get vertices
                S32 v1 = face.mIndices[j*3+0];
                S32 v2 = face.mIndices[j*3+1];
                S32 v3 = face.mIndices[j*3+2];

                //get current face center
                LLVector3 cCenter = (face.mVertices[v1].getPosition() +
                                    face.mVertices[v2].getPosition() +
                                    face.mVertices[v3].getPosition()) / 3.0f;

                //for each edge
                for (S32 k = 0; k < 3; k++) {
                    S32 nIndex = edge[j*3+k];
                    if (nIndex <= -1) {
                        continue;
                    }

                    if (nIndex >= (S32)tri_count) {
                        continue;
                    }
                    //get neighbor vertices
                    v1 = face.mIndices[nIndex*3+0];
                    v2 = face.mIndices[nIndex*3+1];
                    v3 = face.mIndices[nIndex*3+2];

                    //get neighbor face center
                    LLVector3 nCenter = (face.mVertices[v1].getPosition() +
                                    face.mVertices[v2].getPosition() +
                                    face.mVertices[v3].getPosition()) / 3.0f;

                    //draw line
                    vertices.push_back(cCenter);
                    vertices.push_back(nCenter);
                    normals.push_back(LLVector3(1,1,1));
                    normals.push_back(LLVector3(1,1,1));
                    segments.push_back(vertices.size());
                }
            }

            continue;

            //==============================================
            //DEBUG
            //==============================================

            //==============================================
            //DEBUG draw normals instead of silhouette edge
            //==============================================
#elif DEBUG_SILHOUETTE_NORMALS

            //for each vertex
            for (U32 j = 0; j < face.mNumVertices; j++) {
                vertices.push_back(face.mVertices[j].getPosition());
                vertices.push_back(face.mVertices[j].getPosition() + face.mVertices[j].getNormal()*0.1f);
                normals.push_back(LLVector3(0,0,1));
                normals.push_back(LLVector3(0,0,1));
                segments.push_back(vertices.size());
#if DEBUG_SILHOUETTE_BINORMALS
                vertices.push_back(face.mVertices[j].getPosition());
                vertices.push_back(face.mVertices[j].getPosition() + face.mVertices[j].mTangent*0.1f);
                normals.push_back(LLVector3(0,0,1));
                normals.push_back(LLVector3(0,0,1));
                segments.push_back(vertices.size());
#endif
            }

            continue;
#else
            //==============================================
            //DEBUG
            //==============================================

            constexpr U8 AWAY = 0x01,
                         TOWARDS = 0x02;

            //for each triangle
            std::vector<U8> fFacing;
            vector_append(fFacing, face.mNumIndices/3);

            LLVector4a* v = (LLVector4a*) face.mPositions;
            LLVector4a* n = (LLVector4a*) face.mNormals;

            for (S32 j = 0; j < face.mNumIndices/3; j++)
            {
                //approximate normal
                S32 v1 = face.mIndices[j*3+0];
                S32 v2 = face.mIndices[j*3+1];
                S32 v3 = face.mIndices[j*3+2];

                LLVector4a c1,c2;
                c1.setSub(v[v1], v[v2]);
                c2.setSub(v[v2], v[v3]);

                LLVector4a norm;

                norm.setCross3(c1, c2);

                if (norm.dot3(norm) < 0.00000001f)
                {
                    fFacing[j] = AWAY | TOWARDS;
                }
                else
                {
                    //get view vector
                    LLVector4a view;
                    view.setSub(obj_cam_vec, v[v1]);
                    bool away = view.dot3(norm) > 0.0f;
                    if (away)
                    {
                        fFacing[j] = AWAY;
                    }
                    else
                    {
                        fFacing[j] = TOWARDS;
                    }
                }
            }

            //for each triangle
            for (S32 j = 0; j < face.mNumIndices/3; j++)
            {
                if (fFacing[j] == (AWAY | TOWARDS))
                { //this is a degenerate triangle
                    //take neighbor facing (degenerate faces get facing of one of their neighbors)
                    // *FIX IF NEEDED:  this does not deal with neighboring degenerate faces
                    for (S32 k = 0; k < 3; k++)
                    {
                        S32 index = edge[j*3+k];
                        if (index != -1)
                        {
                            fFacing[j] = fFacing[index];
                            break;
                        }
                    }
                    continue; //skip degenerate face
                }

                //for each edge
                for (S32 k = 0; k < 3; k++) {
                    S32 index = edge[j*3+k];
                    if (index != -1 && fFacing[index] == (AWAY | TOWARDS)) {
                        //our neighbor is degenerate, make him face our direction
                        fFacing[edge[j*3+k]] = fFacing[j];
                        continue;
                    }

                    if (index == -1 ||      //edge has no neighbor, MUST be a silhouette edge
                        (fFacing[index] & fFacing[j]) == 0) {   //we found a silhouette edge

                        S32 v1 = face.mIndices[j*3+k];
                        S32 v2 = face.mIndices[j*3+((k+1)%3)];

                        LLVector4a t;
                        mat.affineTransform(v[v1], t);
                        vertices.push_back(LLVector3(t[0], t[1], t[2]));

                        norm_mat.rotate(n[v1], t);

                        t.normalize3fast();
                        normals.push_back(LLVector3(t[0], t[1], t[2]));

                        mat.affineTransform(v[v2], t);
                        vertices.push_back(LLVector3(t[0], t[1], t[2]));

                        norm_mat.rotate(n[v2], t);
                        t.normalize3fast();
                        normals.push_back(LLVector3(t[0], t[1], t[2]));
                    }
                }
            }
#endif
        }
    }
}

S32 LLVolume::lineSegmentIntersect(const LLVector4a& start, const LLVector4a& end,
                                   S32 face,
                                   LLVector4a* intersection,LLVector2* tex_coord, LLVector4a* normal, LLVector4a* tangent_out)
{
    S32 hit_face = -1;

    S32 start_face;
    S32 end_face;

    if (face == -1) // ALL_SIDES
    {
        start_face = 0;
        end_face = getNumVolumeFaces() - 1;
    }
    else
    {
        start_face = face;
        end_face = face;
    }

    LLVector4a dir;
    dir.setSub(end, start);

    F32 closest_t = 2.f; // must be larger than 1

    end_face = llmin(end_face, getNumVolumeFaces()-1);

    for (S32 i = start_face; i <= end_face; i++)
    {
        LLVolumeFace &face = mVolumeFaces[i];

        LLVector4a box_center;
        box_center.setAdd(face.mExtents[0], face.mExtents[1]);
        box_center.mul(0.5f);

        LLVector4a box_size;
        box_size.setSub(face.mExtents[1], face.mExtents[0]);

        if (LLLineSegmentBoxIntersect(start, end, box_center, box_size))
        {
            if (tangent_out != NULL) // if the caller wants tangents, we may need to generate them
            {
                genTangents(i);
            }

            if (isUnique())
            { //don't bother with an octree for flexi volumes
                U32 tri_count = face.mNumIndices/3;

                for (U32 j = 0; j < tri_count; ++j)
                {
                    U16 idx0 = face.mIndices[j*3+0];
                    U16 idx1 = face.mIndices[j*3+1];
                    U16 idx2 = face.mIndices[j*3+2];

                    const LLVector4a& v0 = face.mPositions[idx0];
                    const LLVector4a& v1 = face.mPositions[idx1];
                    const LLVector4a& v2 = face.mPositions[idx2];

                    F32 a,b,t;

                    if (LLTriangleRayIntersect(v0, v1, v2,
                            start, dir, a, b, t))
                    {
                        if ((t >= 0.f) &&      // if hit is after start
                            (t <= 1.f) &&      // and before end
                            (t < closest_t))   // and this hit is closer
                        {
                            closest_t = t;
                            hit_face = i;

                            if (intersection != NULL)
                            {
                                LLVector4a intersect = dir;
                                intersect.mul(closest_t);
                                intersect.add(start);
                                *intersection = intersect;
                            }


                            if (tex_coord != NULL)
                            {
                                LLVector2* tc = (LLVector2*) face.mTexCoords;
                                *tex_coord = ((1.f - a - b)  * tc[idx0] +
                                    a              * tc[idx1] +
                                    b              * tc[idx2]);

                            }

                            if (normal!= NULL)
                            {
                                LLVector4a* norm = face.mNormals;

                                LLVector4a n1,n2,n3;
                                n1 = norm[idx0];
                                n1.mul(1.f-a-b);

                                n2 = norm[idx1];
                                n2.mul(a);

                                n3 = norm[idx2];
                                n3.mul(b);

                                n1.add(n2);
                                n1.add(n3);

                                *normal     = n1;
                            }

                            if (tangent_out != NULL)
                            {
                                LLVector4a* tangents = face.mTangents;

                                LLVector4a t1,t2,t3;
                                t1 = tangents[idx0];
                                t1.mul(1.f-a-b);

                                t2 = tangents[idx1];
                                t2.mul(a);

                                t3 = tangents[idx2];
                                t3.mul(b);

                                t1.add(t2);
                                t1.add(t3);

                                *tangent_out = t1;
                            }
                        }
                    }
                }
            }
            else
            {
                if (!face.getOctree())
                {
                    face.createOctree();
                }

                LLOctreeTriangleRayIntersect intersect(start, dir, &face, &closest_t, intersection, tex_coord, normal, tangent_out);
                intersect.traverse(face.getOctree());
                if (intersect.mHitFace)
                {
                    hit_face = i;
                }
            }
        }
    }


    return hit_face;
}

class LLVertexIndexPair
{
public:
    LLVertexIndexPair(const LLVector3 &vertex, const S32 index);

    LLVector3 mVertex;
    S32 mIndex;
};

LLVertexIndexPair::LLVertexIndexPair(const LLVector3 &vertex, const S32 index)
{
    mVertex = vertex;
    mIndex = index;
}

constexpr F32 VERTEX_SLOP = 0.00001f;

struct lessVertex
{
    bool operator()(const LLVertexIndexPair *a, const LLVertexIndexPair *b)
    {
        const F32 slop = VERTEX_SLOP;

        if (a->mVertex.mV[0] + slop < b->mVertex.mV[0])
        {
            return true;
        }
        else if (a->mVertex.mV[0] - slop > b->mVertex.mV[0])
        {
            return false;
        }

        if (a->mVertex.mV[1] + slop < b->mVertex.mV[1])
        {
            return true;
        }
        else if (a->mVertex.mV[1] - slop > b->mVertex.mV[1])
        {
            return false;
        }

        if (a->mVertex.mV[2] + slop < b->mVertex.mV[2])
        {
            return true;
        }
        else if (a->mVertex.mV[2] - slop > b->mVertex.mV[2])
        {
            return false;
        }

        return false;
    }
};

struct lessTriangle
{
    bool operator()(const S32 *a, const S32 *b)
    {
        if (*a < *b)
        {
            return true;
        }
        else if (*a > *b)
        {
            return false;
        }

        if (*(a+1) < *(b+1))
        {
            return true;
        }
        else if (*(a+1) > *(b+1))
        {
            return false;
        }

        if (*(a+2) < *(b+2))
        {
            return true;
        }
        else if (*(a+2) > *(b+2))
        {
            return false;
        }

        return false;
    }
};

bool equalTriangle(const S32 *a, const S32 *b)
{
    if ((*a == *b) && (*(a+1) == *(b+1)) && (*(a+2) == *(b+2)))
    {
        return true;
    }
    return false;
}

bool LLVolumeParams::importFile(LLFILE *fp)
{
    //LL_INFOS() << "importing volume" << LL_ENDL;
    const S32 BUFSIZE = 16384;
    char buffer[BUFSIZE];   /* Flawfinder: ignore */
    // *NOTE: changing the size or type of this buffer will require
    // changing the sscanf below.
    char keyword[256];  /* Flawfinder: ignore */
    keyword[0] = 0;

    while (!feof(fp))
    {
        if (fgets(buffer, BUFSIZE, fp) == NULL)
        {
            buffer[0] = '\0';
        }

        sscanf(buffer, " %255s", keyword);  /* Flawfinder: ignore */
        if (!strcmp("{", keyword))
        {
            continue;
        }
        if (!strcmp("}",keyword))
        {
            break;
        }
        else if (!strcmp("profile", keyword))
        {
            mProfileParams.importFile(fp);
        }
        else if (!strcmp("path",keyword))
        {
            mPathParams.importFile(fp);
        }
        else
        {
            LL_WARNS() << "unknown keyword " << keyword << " in volume import" << LL_ENDL;
        }
    }

    return true;
}

bool LLVolumeParams::exportFile(LLFILE *fp) const
{
    fprintf(fp,"\tshape 0\n");
    fprintf(fp,"\t{\n");
    mPathParams.exportFile(fp);
    mProfileParams.exportFile(fp);
    fprintf(fp, "\t}\n");
    return true;
}


bool LLVolumeParams::importLegacyStream(std::istream& input_stream)
{
    //LL_INFOS() << "importing volume" << LL_ENDL;
    const S32 BUFSIZE = 16384;
    // *NOTE: changing the size or type of this buffer will require
    // changing the sscanf below.
    char buffer[BUFSIZE];       /* Flawfinder: ignore */
    char keyword[256];      /* Flawfinder: ignore */
    keyword[0] = 0;

    while (input_stream.good())
    {
        input_stream.getline(buffer, BUFSIZE);
        sscanf(buffer, " %255s", keyword);
        if (!strcmp("{", keyword))
        {
            continue;
        }
        if (!strcmp("}",keyword))
        {
            break;
        }
        else if (!strcmp("profile", keyword))
        {
            mProfileParams.importLegacyStream(input_stream);
        }
        else if (!strcmp("path",keyword))
        {
            mPathParams.importLegacyStream(input_stream);
        }
        else
        {
            LL_WARNS() << "unknown keyword " << keyword << " in volume import" << LL_ENDL;
        }
    }

    return true;
}

bool LLVolumeParams::exportLegacyStream(std::ostream& output_stream) const
{
    output_stream <<"\tshape 0\n";
    output_stream <<"\t{\n";
    mPathParams.exportLegacyStream(output_stream);
    mProfileParams.exportLegacyStream(output_stream);
    output_stream << "\t}\n";
    return true;
}

LLSD LLVolumeParams::sculptAsLLSD() const
{
    LLSD sd = LLSD();
    sd["id"] = getSculptID();
    sd["type"] = getSculptType();

    return sd;
}

bool LLVolumeParams::sculptFromLLSD(LLSD& sd)
{
    setSculptID(sd["id"].asUUID(), (U8)sd["type"].asInteger());
    return true;
}

LLSD LLVolumeParams::asLLSD() const
{
    LLSD sd = LLSD();
    sd["path"] = mPathParams;
    sd["profile"] = mProfileParams;
    sd["sculpt"] = sculptAsLLSD();

    return sd;
}

bool LLVolumeParams::fromLLSD(LLSD& sd)
{
    mPathParams.fromLLSD(sd["path"]);
    mProfileParams.fromLLSD(sd["profile"]);
    sculptFromLLSD(sd["sculpt"]);

    return true;
}

void LLVolumeParams::reduceS(F32 begin, F32 end)
{
    begin = llclampf(begin);
    end = llclampf(end);
    if (begin > end)
    {
        F32 temp = begin;
        begin = end;
        end = temp;
    }
    F32 a = mProfileParams.getBegin();
    F32 b = mProfileParams.getEnd();
    mProfileParams.setBegin(a + begin * (b - a));
    mProfileParams.setEnd(a + end * (b - a));
}

void LLVolumeParams::reduceT(F32 begin, F32 end)
{
    begin = llclampf(begin);
    end = llclampf(end);
    if (begin > end)
    {
        F32 temp = begin;
        begin = end;
        end = temp;
    }
    F32 a = mPathParams.getBegin();
    F32 b = mPathParams.getEnd();
    mPathParams.setBegin(a + begin * (b - a));
    mPathParams.setEnd(a + end * (b - a));
}

const F32 MIN_CONCAVE_PROFILE_WEDGE = 0.125f;   // 1/8 unity
const F32 MIN_CONCAVE_PATH_WEDGE = 0.111111f;   // 1/9 unity

// returns true if the shape can be approximated with a convex shape
// for collison purposes
bool LLVolumeParams::isConvex() const
{
    if (!getSculptID().isNull())
    {
        // can't determine, be safe and say no:
        return false;
    }

    F32 path_length = mPathParams.getEnd() - mPathParams.getBegin();
    F32 hollow = mProfileParams.getHollow();

    U8 path_type = mPathParams.getCurveType();
    if ( path_length > MIN_CONCAVE_PATH_WEDGE
        && ( mPathParams.getTwist() != mPathParams.getTwistBegin()
             || (hollow > 0.f
                 && LL_PCODE_PATH_LINE != path_type) ) )
    {
        // twist along a "not too short" path is concave
        return false;
    }

    F32 profile_length = mProfileParams.getEnd() - mProfileParams.getBegin();
    bool same_hole = hollow == 0.f
                     || (mProfileParams.getCurveType() & LL_PCODE_HOLE_MASK) == LL_PCODE_HOLE_SAME;

    F32 min_profile_wedge = MIN_CONCAVE_PROFILE_WEDGE;
    U8 profile_type = mProfileParams.getCurveType() & LL_PCODE_PROFILE_MASK;
    if ( LL_PCODE_PROFILE_CIRCLE_HALF == profile_type )
    {
        // it is a sphere and spheres get twice the minimum profile wedge
        min_profile_wedge = 2.f * MIN_CONCAVE_PROFILE_WEDGE;
    }

    bool convex_profile = ( ( profile_length == 1.f
                             || profile_length <= 0.5f )
                           && hollow == 0.f )                       // trivially convex
                          || ( profile_length <= min_profile_wedge
                              && same_hole );                       // effectvely convex (even when hollow)

    if (!convex_profile)
    {
        // profile is concave
        return false;
    }

    if ( LL_PCODE_PATH_LINE == path_type )
    {
        // straight paths with convex profile
        return true;
    }

    bool concave_path = (path_length < 1.0f) && (path_length > 0.5f);
    if (concave_path)
    {
        return false;
    }

    // we're left with spheres, toroids and tubes
    if ( LL_PCODE_PROFILE_CIRCLE_HALF == profile_type )
    {
        // at this stage all spheres must be convex
        return true;
    }

    // it's a toroid or tube
    if ( path_length <= MIN_CONCAVE_PATH_WEDGE )
    {
        // effectively convex
        return true;
    }

    return false;
}

// debug
void LLVolumeParams::setCube()
{
    mProfileParams.setCurveType(LL_PCODE_PROFILE_SQUARE);
    mProfileParams.setBegin(0.f);
    mProfileParams.setEnd(1.f);
    mProfileParams.setHollow(0.f);

    mPathParams.setBegin(0.f);
    mPathParams.setEnd(1.f);
    mPathParams.setScale(1.f, 1.f);
    mPathParams.setShear(0.f, 0.f);
    mPathParams.setCurveType(LL_PCODE_PATH_LINE);
    mPathParams.setTwistBegin(0.f);
    mPathParams.setTwistEnd(0.f);
    mPathParams.setRadiusOffset(0.f);
    mPathParams.setTaper(0.f, 0.f);
    mPathParams.setRevolutions(0.f);
    mPathParams.setSkew(0.f);
}

LLFaceID LLVolume::generateFaceMask()
{
    LLFaceID new_mask = 0x0000;

    switch(mParams.getProfileParams().getCurveType() & LL_PCODE_PROFILE_MASK)
    {
    case LL_PCODE_PROFILE_CIRCLE:
    case LL_PCODE_PROFILE_CIRCLE_HALF:
        new_mask |= LL_FACE_OUTER_SIDE_0;
        break;
    case LL_PCODE_PROFILE_SQUARE:
        {
            for(S32 side = (S32)(mParams.getProfileParams().getBegin() * 4.f); side < llceil(mParams.getProfileParams().getEnd() * 4.f); side++)
            {
                new_mask |= LL_FACE_OUTER_SIDE_0 << side;
            }
        }
        break;
    case LL_PCODE_PROFILE_ISOTRI:
    case LL_PCODE_PROFILE_EQUALTRI:
    case LL_PCODE_PROFILE_RIGHTTRI:
        {
            for(S32 side = (S32)(mParams.getProfileParams().getBegin() * 3.f); side < llceil(mParams.getProfileParams().getEnd() * 3.f); side++)
            {
                new_mask |= LL_FACE_OUTER_SIDE_0 << side;
            }
        }
        break;
    default:
        LL_ERRS() << "Unknown profile!" << LL_ENDL;
        break;
    }

    // handle hollow objects
    if (mParams.getProfileParams().getHollow() > 0)
    {
        new_mask |= LL_FACE_INNER_SIDE;
    }

    // handle open profile curves
    if (mProfilep->isOpen())
    {
        new_mask |= LL_FACE_PROFILE_BEGIN | LL_FACE_PROFILE_END;
    }

    // handle open path curves
    if (mPathp->isOpen())
    {
        new_mask |= LL_FACE_PATH_BEGIN | LL_FACE_PATH_END;
    }

    return new_mask;
}

bool LLVolume::isFaceMaskValid(LLFaceID face_mask)
{
    LLFaceID test_mask = 0;
    for(S32 i = 0; i < getNumFaces(); i++)
    {
        test_mask |= mProfilep->mFaces[i].mFaceID;
    }

    return test_mask == face_mask;
}

bool LLVolume::isConvex() const
{
    // mParams.isConvex() may return false even though the final
    // geometry is actually convex due to LOD approximations.
    // TODO -- provide LLPath and LLProfile with isConvex() methods
    // that correctly determine convexity. -- Leviathan
    return mParams.isConvex();
}


std::ostream& operator<<(std::ostream &s, const LLProfileParams &profile_params)
{
    s << "{type=" << (U32) profile_params.mCurveType;
    s << ", begin=" << profile_params.mBegin;
    s << ", end=" << profile_params.mEnd;
    s << ", hollow=" << profile_params.mHollow;
    s << "}";
    return s;
}


std::ostream& operator<<(std::ostream &s, const LLPathParams &path_params)
{
    s << "{type=" << (U32) path_params.mCurveType;
    s << ", begin=" << path_params.mBegin;
    s << ", end=" << path_params.mEnd;
    s << ", twist=" << path_params.mTwistEnd;
    s << ", scale=" << path_params.mScale;
    s << ", shear=" << path_params.mShear;
    s << ", twist_begin=" << path_params.mTwistBegin;
    s << ", radius_offset=" << path_params.mRadiusOffset;
    s << ", taper=" << path_params.mTaper;
    s << ", revolutions=" << path_params.mRevolutions;
    s << ", skew=" << path_params.mSkew;
    s << "}";
    return s;
}


std::ostream& operator<<(std::ostream &s, const LLVolumeParams &volume_params)
{
    s << "{profileparams = " << volume_params.mProfileParams;
    s << ", pathparams = " << volume_params.mPathParams;
    s << "}";
    return s;
}


std::ostream& operator<<(std::ostream &s, const LLProfile &profile)
{
    s << " {open=" << (U32) profile.mOpen;
    s << ", dirty=" << profile.mDirty;
    s << ", totalout=" << profile.mTotalOut;
    s << ", total=" << profile.mTotal;
    s << "}";
    return s;
}


std::ostream& operator<<(std::ostream &s, const LLPath &path)
{
    s << "{open=" << (U32) path.mOpen;
    s << ", dirty=" << path.mDirty;
    s << ", step=" << path.mStep;
    s << ", total=" << path.mTotal;
    s << "}";
    return s;
}

std::ostream& operator<<(std::ostream &s, const LLVolume &volume)
{
    s << "{params = " << volume.getParams();
    s << ", path = " << *volume.mPathp;
    s << ", profile = " << *volume.mProfilep;
    s << "}";
    return s;
}


std::ostream& operator<<(std::ostream &s, const LLVolume *volumep)
{
    s << "{params = " << volumep->getParams();
    s << ", path = " << *(volumep->mPathp);
    s << ", profile = " << *(volumep->mProfilep);
    s << "}";
    return s;
}

LLVolumeFace::LLVolumeFace() :
    mID(0),
    mTypeMask(0),
    mBeginS(0),
    mBeginT(0),
    mNumS(0),
    mNumT(0),
    mNumVertices(0),
    mNumAllocatedVertices(0),
    mNumIndices(0),
    mPositions(NULL),
    mNormals(NULL),
    mTangents(NULL),
    mTexCoords(NULL),
    mIndices(NULL),
    mWeights(NULL),
#if USE_SEPARATE_JOINT_INDICES_AND_WEIGHTS
    mJustWeights(NULL),
    mJointIndices(NULL),
#endif
    mWeightsScrubbed(false),
    mOctree(NULL),
    mOctreeTriangles(NULL),
    mOptimized(false)
{
    mExtents = (LLVector4a*) ll_aligned_malloc_16(sizeof(LLVector4a)*3);
    mExtents[0].splat(-0.5f);
    mExtents[1].splat(0.5f);
    mCenter = mExtents+2;
}

LLVolumeFace::LLVolumeFace(const LLVolumeFace& src)
:   mID(0),
    mTypeMask(0),
    mBeginS(0),
    mBeginT(0),
    mNumS(0),
    mNumT(0),
    mNumVertices(0),
    mNumAllocatedVertices(0),
    mNumIndices(0),
    mPositions(NULL),
    mNormals(NULL),
    mTangents(NULL),
    mTexCoords(NULL),
    mIndices(NULL),
    mWeights(NULL),
#if USE_SEPARATE_JOINT_INDICES_AND_WEIGHTS
    mJustWeights(NULL),
    mJointIndices(NULL),
#endif
    mWeightsScrubbed(false),
    mOctree(NULL),
    mOctreeTriangles(NULL)
{
    try
    {
        mExtents = (LLVector4a*)ll_aligned_malloc_16(sizeof(LLVector4a) * 3);
        mCenter = mExtents + 2;
        *this = src;
    }
    catch (std::bad_alloc&)
    {
        LLError::LLUserWarningMsg::showOutOfMemory();
        LL_ERRS("LLVolume") << "Bad memory allocation in LLVolumeFace" << LL_ENDL;
    }
}

LLVolumeFace& LLVolumeFace::operator=(const LLVolumeFace& src)
{
    if (&src == this)
    { //self assignment, do nothing
        return *this;
    }

    mID = src.mID;
    mTypeMask = src.mTypeMask;
    mBeginS = src.mBeginS;
    mBeginT = src.mBeginT;
    mNumS = src.mNumS;
    mNumT = src.mNumT;

    mExtents[0] = src.mExtents[0];
    mExtents[1] = src.mExtents[1];
    *mCenter = *src.mCenter;

    mNumVertices = 0;
    mNumIndices = 0;

    freeData();

    resizeVertices(src.mNumVertices);
    resizeIndices(src.mNumIndices);

    if (mNumVertices)
    {
        S32 vert_size = mNumVertices*sizeof(LLVector4a);
        S32 tc_size = (mNumVertices*sizeof(LLVector2)+0xF) & ~0xF;

        LLVector4a::memcpyNonAliased16((F32*) mPositions, (F32*) src.mPositions, vert_size);

        if (src.mNormals)
        {
        LLVector4a::memcpyNonAliased16((F32*) mNormals, (F32*) src.mNormals, vert_size);
        }

        if(src.mTexCoords)
        {
            LLVector4a::memcpyNonAliased16((F32*) mTexCoords, (F32*) src.mTexCoords, tc_size);
        }

        if (src.mTangents)
        {
            allocateTangents(src.mNumVertices);
            LLVector4a::memcpyNonAliased16((F32*) mTangents, (F32*) src.mTangents, vert_size);
        }
        else
        {
            ll_aligned_free_16(mTangents);
            mTangents = NULL;
        }

        if (src.mWeights)
        {
            llassert(!mWeights); // don't orphan an old alloc here accidentally
            allocateWeights(src.mNumVertices);
            LLVector4a::memcpyNonAliased16((F32*) mWeights, (F32*) src.mWeights, vert_size);
            mWeightsScrubbed = src.mWeightsScrubbed;
        }
        else
        {
            ll_aligned_free_16(mWeights);
            mWeights = NULL;
            mWeightsScrubbed = false;
        }

    #if USE_SEPARATE_JOINT_INDICES_AND_WEIGHTS
        if (src.mJointIndices)
        {
            llassert(!mJointIndices); // don't orphan an old alloc here accidentally
            allocateJointIndices(src.mNumVertices);
            LLVector4a::memcpyNonAliased16((F32*) mJointIndices, (F32*) src.mJointIndices, src.mNumVertices * sizeof(U8) * 4);
        }
        else*/
        {
            ll_aligned_free_16(mJointIndices);
            mJointIndices = NULL;
        }
    #endif

    }

    if (mNumIndices)
    {
        S32 idx_size = (mNumIndices*sizeof(U16)+0xF) & ~0xF;

        LLVector4a::memcpyNonAliased16((F32*) mIndices, (F32*) src.mIndices, idx_size);
    }
    else
    {
        ll_aligned_free_16(mIndices);
        mIndices = NULL;
    }

    mOptimized = src.mOptimized;
    mNormalizedScale = src.mNormalizedScale;

    //delete
    return *this;
}

LLVolumeFace::~LLVolumeFace()
{
    ll_aligned_free_16(mExtents);
    mExtents = NULL;
    mCenter = NULL;

    freeData();
}

void LLVolumeFace::freeData()
{
    ll_aligned_free<64>(mPositions);
    mPositions = NULL;

    //normals and texture coordinates are part of the same buffer as mPositions, do not free them separately
    mNormals = NULL;
    mTexCoords = NULL;

    ll_aligned_free_16(mIndices);
    mIndices = NULL;
    ll_aligned_free_16(mTangents);
    mTangents = NULL;
    ll_aligned_free_16(mWeights);
    mWeights = NULL;

#if USE_SEPARATE_JOINT_INDICES_AND_WEIGHTS
    ll_aligned_free_16(mJointIndices);
    mJointIndices = NULL;
    ll_aligned_free_16(mJustWeights);
    mJustWeights = NULL;
#endif

    destroyOctree();
}

bool LLVolumeFace::create(LLVolume* volume, bool partial_build)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VOLUME;

    //tree for this face is no longer valid
    destroyOctree();

    LL_CHECK_MEMORY
    if (!(mTypeMask & (CAP_MASK | END_MASK | SIDE_MASK)))
    {
        LL_ERRS() << "Unknown/uninitialized face type!" << LL_ENDL;
        return false;
    }

    const LLVolumeParams& vp = volume->getParams();
    const LLProfile& profile = volume->getProfile();
    const LLPath& path = volume->getPath();

    VksFaceParams p;
    p.mesh = (const F32*) volume->getMesh().mArray;
    p.mesh_count = volume->getMesh().size();
    p.profile = (const F32*) profile.mProfile.mArray;
    p.profile_count = profile.mProfile.size();
    p.profile_total = profile.getTotal();
    p.profile_open = profile.isOpen();
    p.path = (const VksPathPt*) path.mPath.mArray;
    p.path_count = path.mPath.size();
    p.path_open = path.isOpen();
    p.id = mID;
    p.type_mask = mTypeMask;
    p.begin_s = mBeginS;
    p.num_s = mNumS;
    p.begin_t = mBeginT;
    p.num_t = mNumT;
    p.profile_curve = vp.getProfileParams().getCurveType();
    p.path_curve = vp.getPathParams().getCurveType();
    p.sculpt_type = vp.getSculptType();
    p.path_begin = vp.getPathParams().getBegin();
    p.path_end = vp.getPathParams().getEnd();

    S32 num_vertices = 0;
    S32 num_indices = 0;
    bool fresh_vertices = false;
    bool fresh_indices = false;
    switch (vks_volume_face_sizes(&p, &num_vertices, &num_indices))
    {
    case VKS_FACE_SIDE:
        partial_build = (num_vertices > mNumVertices || num_indices > mNumIndices) ? false : partial_build;
        if (!partial_build)
        {
            resizeVertices(num_vertices);
            resizeIndices(num_indices);
            fresh_vertices = fresh_indices = true;
        }
        break;
    case VKS_FACE_CAP:
        resizeVertices(num_vertices);
        resizeIndices(num_indices);
        fresh_vertices = fresh_indices = true;
        partial_build = false;
        break;
    case VKS_FACE_UNCUT_CUBE_CAP:
        resizeVertices(num_vertices);
        fresh_vertices = true;
        if (!partial_build)
        {
            resizeIndices(num_indices);
            fresh_indices = true;
        }
        break;
    default:
        LL_WARNS() << "LLVolumeFace::create: the mesher rejected the face" << LL_ENDL;
        return false;
    }

    // The mesher forms slices over these buffers before it writes them, so
    // freshly allocated storage is zeroed first (positions, normals and the
    // padded texture coordinates are one block); a partial rebuild keeps the
    // data it has.
    if (fresh_vertices && mPositions)
    {
        const size_t tc_size = ((mNumVertices * sizeof(LLVector2)) + 0xF) & ~0xF;
        memset(mPositions, 0, sizeof(LLVector4a) * 2 * mNumVertices + tc_size);
    }
    if (fresh_indices && mIndices)
    {
        memset(mIndices, 0, ((mNumIndices * sizeof(U16)) + 0xF) & ~0xF);
    }

    LL_CHECK_MEMORY

    VksFaceBuffers b;
    b.positions = (F32*) mPositions;
    b.normals = (F32*) mNormals;
    b.texcoords = (F32*) mTexCoords;
    b.indices = mIndices;
    b.num_vertices = mNumVertices;
    b.num_indices = mNumIndices;
    b.write_indices = !partial_build;

    alignas(16) VksFaceResult r;
    if (vks_volume_face_build(&p, &b, &r) != 1)
    {
        LL_WARNS() << "LLVolumeFace::create: the mesher could not build the face" << LL_ENDL;
        return false;
    }

    mExtents[0].load4a(r.extents_min);
    mExtents[1].load4a(r.extents_max);
    mCenter->load4a(r.center);
    if (r.tc_extents_set)
    {
        mTexCoordExtents[0].set(r.tc_min[0], r.tc_min[1]);
        mTexCoordExtents[1].set(r.tc_max[0], r.tc_max[1]);
    }

    LL_CHECK_MEMORY
    return true;
}

void LLVolumeFace::getVertexData(U16 index, LLVolumeFace::VertexData& cv)
{
    cv.setPosition(mPositions[index]);
    if (mNormals)
    {
        cv.setNormal(mNormals[index]);
    }
    else
    {
        cv.getNormal().clear();
    }

    if (mTexCoords)
    {
        cv.mTexCoord = mTexCoords[index];
    }
    else
    {
        cv.mTexCoord.clear();
    }
}

bool LLVolumeFace::VertexMapData::operator==(const LLVolumeFace::VertexData& rhs) const
{
    return getPosition().equals3(rhs.getPosition()) &&
        mTexCoord == rhs.mTexCoord &&
        getNormal().equals3(rhs.getNormal());
}

bool LLVolumeFace::VertexMapData::ComparePosition::operator()(const LLVector3& a, const LLVector3& b) const
{
    if (a.mV[0] != b.mV[0])
    {
        return a.mV[0] < b.mV[0];
    }

    if (a.mV[1] != b.mV[1])
    {
        return a.mV[1] < b.mV[1];
    }

    return a.mV[2] < b.mV[2];
}

void LLVolumeFace::remap()
{
    // Generate a remap buffer
    // Documentation for meshopt_generateVertexRemapMulti claims that remap should use vertice count
    // but all examples use indice count. There are out of bounds crashes when using vertice count.
    // To be on the safe side use bigger of the two.
    std::vector<unsigned int> remap(llmax(mNumIndices, mNumVertices), 0);
    S32 remap_vertices_count = static_cast<S32>(LLMeshOptimizer::generateRemapMultiU16(&remap[0],
        mIndices,
        mNumIndices,
        mPositions,
        mNormals,
        mTexCoords,
        mNumVertices));

    // Allocate new buffers
    S32 size = ((mNumIndices * sizeof(U16)) + 0xF) & ~0xF;
    U16* remap_indices = (U16*)ll_aligned_malloc_16(size);

    S32 tc_bytes_size = ((remap_vertices_count * sizeof(LLVector2)) + 0xF) & ~0xF;
    LLVector4a* remap_positions = (LLVector4a*)ll_aligned_malloc<64>(sizeof(LLVector4a) * 2 * remap_vertices_count + tc_bytes_size);
    LLVector4a* remap_normals = remap_positions + remap_vertices_count;
    LLVector2* remap_tex_coords = (LLVector2*)(remap_normals + remap_vertices_count);

    // Fill the buffers
    LLMeshOptimizer::remapIndexBufferU16(remap_indices, mIndices, mNumIndices, &remap[0]);
    LLMeshOptimizer::remapPositionsBuffer(remap_positions, mPositions, mNumVertices, &remap[0]);
    LLMeshOptimizer::remapNormalsBuffer(remap_normals, mNormals, mNumVertices, &remap[0]);
    LLMeshOptimizer::remapUVBuffer(remap_tex_coords, mTexCoords, mNumVertices, &remap[0]);

    // Free unused buffers
    ll_aligned_free_16(mIndices);
    ll_aligned_free<64>(mPositions);

    // Tangets are now invalid
    ll_aligned_free_16(mTangents);
    mTangents = NULL;

    // Assign new values
    mIndices = remap_indices;
    mPositions = remap_positions;
    mNormals = remap_normals;
    mTexCoords = remap_tex_coords;
    mNumVertices = remap_vertices_count;
    mNumAllocatedVertices = remap_vertices_count;
}

void LLVolumeFace::optimize(F32 angle_cutoff)
{
    LLVolumeFace new_face;

    //map of points to vector of vertices at that point
    std::map<U64, std::vector<VertexMapData> > point_map;

    LLVector4a range;
    range.setSub(mExtents[1],mExtents[0]);

    //remove redundant vertices
    for (S32 i = 0; i < mNumIndices; ++i)
    {
        U16 index = mIndices[i];

        if (index >= mNumVertices)
        {
            // invalid index
            // replace with a valid index to avoid crashes
            index = mNumVertices - 1;
            mIndices[i] = index;

            // Needs better logging
            LL_DEBUGS_ONCE("LLVOLUME") << "Invalid index, substituting" << LL_ENDL;
        }

        LLVolumeFace::VertexData cv;
        getVertexData(index, cv);

        bool found = false;

        LLVector4a pos;
        pos.setSub(mPositions[index], mExtents[0]);
        pos.div(range);

        U64 pos64 = 0;

        pos64 = (U16) (pos[0]*65535);
        pos64 = pos64 | (((U64) (pos[1]*65535)) << 16);
        pos64 = pos64 | (((U64) (pos[2]*65535)) << 32);

        std::map<U64, std::vector<VertexMapData> >::iterator point_iter = point_map.find(pos64);

        if (point_iter != point_map.end())
        { //duplicate point might exist
            for (U32 j = 0; j < point_iter->second.size(); ++j)
            {
                LLVolumeFace::VertexData& tv = (point_iter->second)[j];
                if (tv.compareNormal(cv, angle_cutoff))
                {
                    found = true;
                    new_face.pushIndex((point_iter->second)[j].mIndex);
                    break;
                }
            }
        }

        if (!found)
        {
            new_face.pushVertex(cv);
            U16 index = (U16) new_face.mNumVertices-1;
            new_face.pushIndex(index);

            VertexMapData d;
            d.setPosition(cv.getPosition());
            d.mTexCoord = cv.mTexCoord;
            d.setNormal(cv.getNormal());
            d.mIndex = index;
            if (point_iter != point_map.end())
            {
                point_iter->second.push_back(d);
            }
            else
            {
                point_map[pos64].push_back(d);
            }
        }
    }


    if (angle_cutoff > 1.f && !mNormals)
    {
        // Now alloc'd with positions
        //ll_aligned_free_16(new_face.mNormals);
        new_face.mNormals = NULL;
    }

    if (!mTexCoords)
    {
        // Now alloc'd with positions
        //ll_aligned_free_16(new_face.mTexCoords);
        new_face.mTexCoords = NULL;
    }

    // Only swap data if we've actually optimized the mesh
    //
    if (new_face.mNumVertices <= mNumVertices)
    {
        llassert(new_face.mNumIndices == mNumIndices);
        swapData(new_face);
    }

}

class LLVCacheTriangleData;

class LLVCacheVertexData
{
public:
    S32 mIdx;
    S32 mCacheTag;
    F64 mScore;
    U32 mActiveTriangles;
    std::vector<LLVCacheTriangleData*> mTriangles;

    LLVCacheVertexData()
    {
        mCacheTag = -1;
        mScore = 0.0;
        mActiveTriangles = 0;
        mIdx = -1;
    }
};

class LLVCacheTriangleData
{
public:
    bool mActive;
    F64 mScore;
    LLVCacheVertexData* mVertex[3];

    LLVCacheTriangleData()
    {
        mActive = true;
        mScore = 0.0;
        mVertex[0] = mVertex[1] = mVertex[2] = NULL;
    }

    void complete()
    {
        mActive = false;
        for (S32 i = 0; i < 3; ++i)
        {
            if (mVertex[i])
            {
                llassert(mVertex[i]->mActiveTriangles > 0);
                mVertex[i]->mActiveTriangles--;
            }
        }
    }

    bool operator<(const LLVCacheTriangleData& rhs) const
    { //highest score first
        return rhs.mScore < mScore;
    }
};

constexpr F64 FindVertexScore_CacheDecayPower = 1.5;
constexpr F64 FindVertexScore_LastTriScore = 0.75;
constexpr F64 FindVertexScore_ValenceBoostScale = 2.0;
constexpr F64 FindVertexScore_ValenceBoostPower = 0.5;
constexpr U32 MaxSizeVertexCache = 32;
constexpr F64 FindVertexScore_Scaler = 1.0/(MaxSizeVertexCache-3);

F64 find_vertex_score(LLVCacheVertexData& data)
{
    F64 score = -1.0;

    score = 0.0;

    S32 cache_idx = data.mCacheTag;

    if (cache_idx < 0)
    {
        //not in cache
    }
    else
    {
        if (cache_idx < 3)
        { //vertex was in the last triangle
            score = FindVertexScore_LastTriScore;
        }
        else
        { //more points for being higher in the cache
                score = 1.0-((cache_idx-3)*FindVertexScore_Scaler);
                score = pow(score, FindVertexScore_CacheDecayPower);
        }
    }

    //bonus points for having low valence
    F64 valence_boost = pow((F64)data.mActiveTriangles, -FindVertexScore_ValenceBoostPower);
    score += FindVertexScore_ValenceBoostScale * valence_boost;

    return score;
}

class LLVCacheFIFO
{
public:
    LLVCacheVertexData* mCache[MaxSizeVertexCache];
    U32 mMisses;

    LLVCacheFIFO()
    {
        mMisses = 0;
        for (U32 i = 0; i < MaxSizeVertexCache; ++i)
        {
            mCache[i] = NULL;
        }
    }

    void addVertex(LLVCacheVertexData* data)
    {
        if (data->mCacheTag == -1)
        {
            mMisses++;

            S32 end = MaxSizeVertexCache-1;

            if (mCache[end])
            {
                mCache[end]->mCacheTag = -1;
            }

            for (S32 i = end; i > 0; --i)
            {
                mCache[i] = mCache[i-1];
                if (mCache[i])
                {
                    mCache[i]->mCacheTag = i;
                }
            }

            mCache[0] = data;
            data->mCacheTag = 0;
        }
    }
};

class LLVCacheLRU
{
public:
    LLVCacheVertexData* mCache[MaxSizeVertexCache+3];

    LLVCacheTriangleData* mBestTriangle;

    U32 mMisses;

    LLVCacheLRU()
    {
        for (U32 i = 0; i < MaxSizeVertexCache+3; ++i)
        {
            mCache[i] = NULL;
        }

        mBestTriangle = NULL;
        mMisses = 0;
    }

    void addVertex(LLVCacheVertexData* data)
    {
        S32 end = MaxSizeVertexCache+2;
        if (data->mCacheTag != -1)
        { //just moving a vertex to the front of the cache
            end = data->mCacheTag;
        }
        else
        {
            mMisses++;
            if (mCache[end])
            { //adding a new vertex, vertex at end of cache falls off
                mCache[end]->mCacheTag = -1;
            }
        }

        for (S32 i = end; i > 0; --i)
        { //adjust cache pointers and tags
            mCache[i] = mCache[i-1];

            if (mCache[i])
            {
                mCache[i]->mCacheTag = i;
            }
        }

        mCache[0] = data;
        mCache[0]->mCacheTag = 0;
    }

    void addTriangle(LLVCacheTriangleData* data)
    {
        addVertex(data->mVertex[0]);
        addVertex(data->mVertex[1]);
        addVertex(data->mVertex[2]);
    }

    void updateScores()
    {
        LLVCacheVertexData** data_iter = mCache+MaxSizeVertexCache;
        LLVCacheVertexData** end_data = mCache+MaxSizeVertexCache+3;

        while(data_iter != end_data)
        {
            LLVCacheVertexData* data = *data_iter++;
            //trailing 3 vertices aren't actually in the cache for scoring purposes
            if (data)
            {
                data->mCacheTag = -1;
            }
        }

        data_iter = mCache;
        end_data = mCache+MaxSizeVertexCache;

        while (data_iter != end_data)
        { //update scores of vertices in cache
            LLVCacheVertexData* data = *data_iter++;
            if (data)
            {
                data->mScore = find_vertex_score(*data);
            }
        }

        mBestTriangle = NULL;
        //update triangle scores
        data_iter = mCache;
        end_data = mCache+MaxSizeVertexCache+3;

        while (data_iter != end_data)
        {
            LLVCacheVertexData* data = *data_iter++;
            if (data)
            {
                for (std::vector<LLVCacheTriangleData*>::iterator iter = data->mTriangles.begin(), end_iter = data->mTriangles.end(); iter != end_iter; ++iter)
                {
                    LLVCacheTriangleData* tri = *iter;
                    if (tri->mActive)
                    {
                        tri->mScore = tri->mVertex[0] ? tri->mVertex[0]->mScore : 0;
                        tri->mScore += tri->mVertex[1] ? tri->mVertex[1]->mScore : 0;
                        tri->mScore += tri->mVertex[2] ? tri->mVertex[2]->mScore : 0;

                        if (!mBestTriangle || mBestTriangle->mScore < tri->mScore)
                        {
                            mBestTriangle = tri;
                        }
                    }
                }
            }
        }

        //knock trailing 3 vertices off the cache
        data_iter = mCache+MaxSizeVertexCache;
        end_data = mCache+MaxSizeVertexCache+3;
        while (data_iter != end_data)
        {
            LLVCacheVertexData* data = *data_iter;
            if (data)
            {
                llassert(data->mCacheTag == -1);
                *data_iter = NULL;
            }
            ++data_iter;
        }
    }
};

// data structures for tangent generation

struct MikktData
{
    LLVolumeFace* face;
    std::vector<LLVector3> p;
    std::vector<LLVector3> n;
    std::vector<LLVector2> tc;
    std::vector<LLVector4> w;
    std::vector<LLVector4> t;

    MikktData(LLVolumeFace* f)
        : face(f)
    {
        U32 count = face->mNumIndices;

        try
        {
            p.resize(count);
            n.resize(count);
            tc.resize(count);
            t.resize(count);

            if (face->mWeights)
            {
                w.resize(count);
            }
        }
        catch (std::bad_alloc&)
        {
            LLError::LLUserWarningMsg::showOutOfMemory();
            LL_ERRS("LLCoros") << "Bad memory allocation in MikktData, elements count: " << count << LL_ENDL;
        }


        LLVector3 inv_scale(1.f / face->mNormalizedScale.mV[0], 1.f / face->mNormalizedScale.mV[1], 1.f / face->mNormalizedScale.mV[2]);


        for (S32 i = 0; i < face->mNumIndices; ++i)
        {
            U32 idx = face->mIndices[i];

            p[i].set(face->mPositions[idx].getF32ptr());
            p[i].scaleVec(face->mNormalizedScale); //put mesh in original coordinate frame when reconstructing tangents
            n[i].set(face->mNormals[idx].getF32ptr());
            n[i].scaleVec(inv_scale);
            n[i].normalize();
            tc[i].set(face->mTexCoords[idx]);

            if (face->mWeights)
            {
                w[i].set(face->mWeights[idx].getF32ptr());
            }
        }
    }

    uint32_t GetNumFaces()
    {
        return uint32_t(face->mNumIndices / 3);
    }

    uint32_t GetNumVerticesOfFace(const uint32_t face_num)
    {
        return 3;
    }

    mikk::float3 GetPosition(const uint32_t face_num, const uint32_t vert_num)
    {
        F32* v = p[face_num * 3 + vert_num].mV;
        return mikk::float3(v);
    }

    mikk::float3 GetTexCoord(const uint32_t face_num, const uint32_t vert_num)
    {
        F32* uv = tc[face_num * 3 + vert_num].mV;
        return mikk::float3(uv[0], uv[1], 1.0f);
    }

    mikk::float3 GetNormal(const uint32_t face_num, const uint32_t vert_num)
    {
        F32* normal = n[face_num * 3 + vert_num].mV;
        return mikk::float3(normal);
    }

    void SetTangentSpace(const uint32_t face_num, const uint32_t vert_num, mikk::float3 T, bool orientation)
    {
        S32 i = face_num * 3 + vert_num;
        t[i].set(T.x, T.y, T.z, orientation ? 1.0f : -1.0f);
    }
};

bool LLVolumeFace::cacheOptimize(bool gen_tangents)
{ //optimize for vertex cache according to Forsyth method:
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VOLUME;
    llassert(!mOptimized);
    mOptimized = true;

    if (gen_tangents && mNormals && mTexCoords)
    { // generate mikkt space tangents before cache optimizing since the index buffer may change
        // a bit of a hack to do this here, but this function gets called exactly once for the lifetime of a mesh
        // and is executed on a background thread
        MikktData data(this);
        mikk::Mikktspace ctx(data);
        try
        {
            ctx.genTangSpace();
        }
        catch (std::bad_alloc&)
        {
            LLError::LLUserWarningMsg::showOutOfMemory();
            LL_ERRS("LLVolume") << "Bad memory allocation in MikktData::genTangSpace" << LL_ENDL;
        }
        catch (...)
        {
            LL_WARNS_ONCE("LLVolume") << "Mikktspace::genTangSpace() failed" << LL_ENDL;
            return false;
        }


        //re-weld
        meshopt_Stream mos[] =
        {
            { &data.p[0], sizeof(LLVector3), sizeof(LLVector3) },
            { &data.n[0], sizeof(LLVector3), sizeof(LLVector3) },
            { &data.t[0], sizeof(LLVector4), sizeof(LLVector4) },
            { &data.tc[0], sizeof(LLVector2), sizeof(LLVector2) },
            { data.w.empty() ? nullptr : &data.w[0], sizeof(LLVector4), sizeof(LLVector4) }
        };

        std::vector<U32> remap;
        try
        {
            remap.resize(data.p.size());
        }
        catch (std::bad_alloc&)
        {
            LLError::LLUserWarningMsg::showOutOfMemory();
            LL_ERRS("LLVOLUME") << "Failed to allocate memory for remap: " << (S32)data.p.size() << LL_ENDL;
        }

        U32 stream_count = data.w.empty() ? 4 : 5;

        S32 vert_count = 0;
        if (!data.p.empty())
        {
            try
            {
                // providing mIndices should help avoid unused vertices
                // but those should have been filtered out on upload
                vert_count = static_cast<S32>(meshopt_generateVertexRemapMulti(&remap[0], nullptr, data.p.size(), data.p.size(), mos, stream_count));
            }
            catch (std::bad_alloc&)
            {
                LLError::LLUserWarningMsg::showOutOfMemory();
                LL_ERRS("LLVolume") << "Failed to allocate memory for VertexRemap: " << (S32)data.p.size() << LL_ENDL;
            }
        }

        // Probably should be using meshopt_remapVertexBuffer instead of remaping manually
        if (vert_count < 65535 && vert_count > 0)
        {
            //copy results back into volume
            resizeVertices(vert_count);
            if (mNumVertices == 0)
            {
                LLError::LLUserWarningMsg::showOutOfMemory();
                LL_ERRS("LLVolume") << "Failed to allocate memory for resizeVertices(" << vert_count << ")" << LL_ENDL;
            }

            if (!data.w.empty())
            {
                allocateWeights(vert_count);
            }

            allocateTangents(mNumVertices);

            for (S32 i = 0; i < mNumIndices; ++i)
            {
                U32 src_idx = i;
                U32 dst_idx = remap[i];
                if (dst_idx == U32_MAX)
                {
                    // Unused indices? Probably need to resize mIndices
                    dst_idx = mNumVertices - 1;
                    llassert(false);
                    LL_DEBUGS_ONCE("LLVOLUME") << "U32_MAX destination index, substituting" << LL_ENDL;
                }
                else if (dst_idx >= (U32)mNumVertices)
                {
                    dst_idx = mNumVertices - 1;
                    // Shouldn't happen, figure out what gets returned in remap and why.
                    llassert(false);
                    LL_DEBUGS_ONCE("LLVOLUME") << "Invalid destination index, substituting" << LL_ENDL;
                }
                if (src_idx >= (U32)data.p.size())
                {
                    // data.p.size() is supposed to be equal to mNumIndices
                    src_idx = (U32)(data.p.size() - 1);
                    llassert(false);
                    LL_DEBUGS_ONCE("LLVOLUME") << "Invalid source index, substituting" << LL_ENDL;
                }
                mIndices[i] = dst_idx;

                mPositions[dst_idx].load3(data.p[src_idx].mV);
                mNormals[dst_idx].load3(data.n[src_idx].mV);
                mTexCoords[dst_idx] = data.tc[src_idx];

                mTangents[dst_idx].loadua(data.t[src_idx].mV);

                if (mWeights)
                {
                    mWeights[dst_idx].loadua(data.w[src_idx].mV);
                }
            }

            // put back in normalized coordinate frame
            LLVector4a inv_scale(1.f/mNormalizedScale.mV[0], 1.f / mNormalizedScale.mV[1], 1.f / mNormalizedScale.mV[2]);
            LLVector4a scale;
            scale.load3(mNormalizedScale.mV);
            scale.getF32ptr()[3] = 1.f;

            for (S32 i = 0; i < mNumVertices; ++i)
            {
                mPositions[i].mul(inv_scale);
                mNormals[i].mul(scale);
                mNormals[i].normalize3();
                F32 w = mTangents[i].getF32ptr()[3];
                mTangents[i].mul(scale);
                mTangents[i].normalize3();
                mTangents[i].getF32ptr()[3] = w;
            }
        }
        else
        {
            if (vert_count <= 0)
            {
                LL_WARNS_ONCE("LLVOLUME") << "meshopt_generateVertexRemapMulti failed to process a model or model was invalid" << LL_ENDL;
            }
            // blew past the max vertex size limit, use legacy tangent generation which never adds verts
            createTangents();
        }
    }

    // cache optimize index buffer

    // meshopt needs scratch space, do some pointer shuffling to avoid an extra index buffer copy
    U16* src_indices = mIndices;
    mIndices = nullptr;
    resizeIndices(mNumIndices);

    meshopt_optimizeVertexCache<U16>(mIndices, src_indices, mNumIndices, mNumVertices);

    ll_aligned_free_16(src_indices);

    return true;
}

void LLVolumeFace::createOctree(F32 scaler, const LLVector4a& center, const LLVector4a& size)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VOLUME;

    if (getOctree())
    {
        return;
    }

    ND_OCTREE_LOG << "Creating octree with scale " << scaler << " mNumIndices " << mNumIndices << ND_OCTREE_LOG_END;
    llassert(mNumIndices % 3 == 0);

    mOctree = new LLVolumeOctree(center, size);
    const U32 num_triangles = mNumIndices / 3;
    // Initialize all the triangles we need
    mOctreeTriangles = new LLVolumeTriangle[num_triangles];

    for (U32 triangle_index = 0; triangle_index < num_triangles; ++triangle_index)
    { //for each triangle
        const U32 index = triangle_index * 3;
        LLVolumeTriangle* tri = &mOctreeTriangles[triangle_index];

        const LLVector4a& v0 = mPositions[mIndices[index]];
        const LLVector4a& v1 = mPositions[mIndices[index + 1]];
        const LLVector4a& v2 = mPositions[mIndices[index + 2]];

        //store pointers to vertex data
        tri->mV[0] = &v0;
        tri->mV[1] = &v1;
        tri->mV[2] = &v2;

        //store indices
        tri->mIndex[0] = mIndices[index];
        tri->mIndex[1] = mIndices[index + 1];
        tri->mIndex[2] = mIndices[index + 2];

        //get minimum point
        LLVector4a min = v0;
        min.setMin(min, v1);
        min.setMin(min, v2);

        //get maximum point
        LLVector4a max = v0;
        max.setMax(max, v1);
        max.setMax(max, v2);

        //compute center
        LLVector4a center;
        center.setAdd(min, max);
        center.mul(0.5f);

        tri->mPositionGroup = center;

        //compute "radius"
        LLVector4a size;
        size.setSub(max,min);

        tri->mRadius = size.getLength3().getF32() * scaler;

        ND_OCTREE_LOG << "insertion " << index
                << " tri.mV " << *tri->mV[0] << "/" << *tri->mV[1] << "/" << *tri->mV[2]
                << " tri.mIndex " << tri->mIndex[0] << "/" << tri->mIndex[1] << "/" << tri->mIndex[2]
                << ND_OCTREE_LOG_END;

        //insert
        mOctree->insert(tri);
        ND_OCTREE_LOG << "insertion done" << std::endl << ND_OCTREE_LOG_END;
    }

    ND_OCTREE_LOG << "octree created" << std::endl << std::endl << ND_OCTREE_LOG_END;

    //remove unneeded octree layers
    while (!mOctree->balance()) { }

    //calculate AABB for each node
    LLVolumeOctreeRebound rebound;
    rebound.traverse(mOctree);

    if (gDebugGL)
    {
        LLVolumeOctreeValidate validate;
        validate.traverse(mOctree);
    }
}

void LLVolumeFace::destroyOctree()
{
    delete mOctree;
    mOctree = nullptr;
    delete[] mOctreeTriangles;
    mOctreeTriangles = nullptr;
}

const LLVolumeOctree* LLVolumeFace::getOctree() const
{
    return mOctree;
}


void LLVolumeFace::swapData(LLVolumeFace& rhs)
{
    llswap(rhs.mPositions, mPositions);
    llswap(rhs.mNormals, mNormals);
    llswap(rhs.mTangents, mTangents);
    llswap(rhs.mTexCoords, mTexCoords);
    llswap(rhs.mIndices,mIndices);
    llswap(rhs.mNumVertices, mNumVertices);
    llswap(rhs.mNumIndices, mNumIndices);
}

void LLVolumeFace::createTangents()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VOLUME;

    if (!mTangents)
    {
        allocateTangents(mNumVertices);
        if (mTangents)
        {
            // the mesher forms a slice over the new buffer before writing it
            memset(mTangents, 0, sizeof(LLVector4a) * mNumVertices);
        }

        // generate tangents, then normalize the normals: bump map/planar
        // projection code requires normals to be normalized
        vks_volume_tangents(mNumVertices, (const F32*) mPositions, (F32*) mNormals, (const F32*) mTexCoords,
                            mNumIndices, mIndices, (F32*) mTangents, 1);
    }
}

void LLVolumeFace::resizeVertices(S32 num_verts)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VOLUME;

    ll_aligned_free<64>(mPositions);
    //DO NOT free mNormals and mTexCoords as they are part of mPositions buffer
    ll_aligned_free_16(mTangents);

    mTangents = NULL;

    if (num_verts)
    {
        //pad texture coordinate block end to allow for QWORD reads
        S32 tc_size = ((num_verts*sizeof(LLVector2)) + 0xF) & ~0xF;

        mPositions = (LLVector4a*) ll_aligned_malloc<64>(sizeof(LLVector4a)*2*num_verts+tc_size);
        mNormals = mPositions+num_verts;
        mTexCoords = (LLVector2*) (mNormals+num_verts);

        ll_assert_aligned(mPositions, 64);
    }
    else
    {
        mPositions = NULL;
        mNormals = NULL;
        mTexCoords = NULL;
    }


    if (mPositions)
    {
        mNumVertices = num_verts;
        mNumAllocatedVertices = num_verts;
    }
    else
    {
        // Either num_verts is zero or allocation failure
        mNumVertices = 0;
        mNumAllocatedVertices = 0;
    }

    // Force update
    mJointRiggingInfoTab.clear();
}

void LLVolumeFace::pushVertex(const LLVolumeFace::VertexData& cv)
{
    pushVertex(cv.getPosition(), cv.getNormal(), cv.mTexCoord);
}

void LLVolumeFace::pushVertex(const LLVector4a& pos, const LLVector4a& norm, const LLVector2& tc)
{
    S32 new_verts = mNumVertices+1;

    if (new_verts > mNumAllocatedVertices)
    {
        // double buffer size on expansion
        new_verts *= 2;

        S32 new_tc_size = ((new_verts*8)+0xF) & ~0xF;
        S32 old_tc_size = ((mNumVertices*8)+0xF) & ~0xF;

        S32 old_vsize = mNumVertices*16;

        S32 new_size = new_verts*16*2+new_tc_size;

        LLVector4a* old_buf = mPositions;

        mPositions = (LLVector4a*) ll_aligned_malloc<64>(new_size);
        mNormals = mPositions+new_verts;
        mTexCoords = (LLVector2*) (mNormals+new_verts);

        if (old_buf != NULL)
        {
            // copy old positions into new buffer
            LLVector4a::memcpyNonAliased16((F32*)mPositions, (F32*)old_buf, old_vsize);

            // normals
            LLVector4a::memcpyNonAliased16((F32*)mNormals, (F32*)(old_buf + mNumVertices), old_vsize);

            // tex coords
            LLVector4a::memcpyNonAliased16((F32*)mTexCoords, (F32*)(old_buf + mNumVertices * 2), old_tc_size);
        }

        // just clear tangents
        ll_aligned_free_16(mTangents);
        mTangents = NULL;
        ll_aligned_free<64>(old_buf);

        mNumAllocatedVertices = new_verts;

    }

    mPositions[mNumVertices] = pos;
    mNormals[mNumVertices] = norm;
    mTexCoords[mNumVertices] = tc;

    mNumVertices++;
}

void LLVolumeFace::allocateTangents(S32 num_verts)
{
    ll_aligned_free_16(mTangents);
    mTangents = (LLVector4a*) ll_aligned_malloc_16(sizeof(LLVector4a)*num_verts);
}

void LLVolumeFace::allocateWeights(S32 num_verts)
{
    ll_aligned_free_16(mWeights);
    mWeights = (LLVector4a*)ll_aligned_malloc_16(sizeof(LLVector4a)*num_verts);

}

void LLVolumeFace::allocateJointIndices(S32 num_verts)
{
#if USE_SEPARATE_JOINT_INDICES_AND_WEIGHTS
    ll_aligned_free_16(mJointIndices);
    ll_aligned_free_16(mJustWeights);

    mJointIndices = (U8*)ll_aligned_malloc_16(sizeof(U8) * 4 * num_verts);
    mJustWeights = (LLVector4a*)ll_aligned_malloc_16(sizeof(LLVector4a) * num_verts);
#endif
}

void LLVolumeFace::resizeIndices(S32 num_indices)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VOLUME;

    ll_aligned_free_16(mIndices);
    llassert(num_indices % 3 == 0);

    if (num_indices)
    {
        //pad index block end to allow for QWORD reads
        S32 size = ((num_indices*sizeof(U16)) + 0xF) & ~0xF;

        mIndices = (U16*) ll_aligned_malloc_16(size);
    }
    else
    {
        mIndices = NULL;
    }

    if (mIndices)
    {
        mNumIndices = num_indices;
    }
    else
    {
        // Either num_indices is zero or allocation failure
        mNumIndices = 0;
    }
}

void LLVolumeFace::pushIndex(const U16& idx)
{
    S32 new_count = mNumIndices + 1;
    S32 new_size = ((new_count*2)+0xF) & ~0xF;

    S32 old_size = ((mNumIndices*2)+0xF) & ~0xF;
    if (new_size != old_size)
    {
        mIndices = (U16*) ll_aligned_realloc_16(mIndices, new_size, old_size);
        ll_assert_aligned(mIndices,16);
    }

    mIndices[mNumIndices++] = idx;
}

void LLVolumeFace::fillFromLegacyData(std::vector<LLVolumeFace::VertexData>& v, std::vector<U16>& idx)
{
    resizeVertices(static_cast<S32>(v.size()));
    resizeIndices(static_cast<S32>(idx.size()));

    for (U32 i = 0; i < v.size(); ++i)
    {
        mPositions[i] = v[i].getPosition();
        mNormals[i] = v[i].getNormal();
        mTexCoords[i] = v[i].mTexCoord;
    }

    for (U32 i = 0; i < idx.size(); ++i)
    {
        mIndices[i] = idx[i];
    }
}

//adapted from Lengyel, Eric. "Computing Tangent Space Basis Vectors for an Arbitrary Mesh". Terathon Software 3D Graphics Library, 2001. http://www.terathon.com/code/tangent.html
void LLCalculateTangentArray(U32 vertexCount, const LLVector4a *vertex, const LLVector4a *normal,
        const LLVector2 *texcoord, U32 triangleCount, const U16* index_array, LLVector4a *tangent)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_VOLUME;

    // The mesher forms a slice over the output before writing all of it.
    if (tangent && vertexCount)
    {
        memset(tangent, 0, sizeof(LLVector4a) * vertexCount);
    }
    // The normals are only read (normalize_normals = 0).
    vks_volume_tangents((S32) vertexCount, (const F32*) vertex, (F32*) const_cast<LLVector4a*>(normal), (const F32*) texcoord,
                        (S32) (triangleCount * 3), index_array, (F32*) tangent, 0);
}
