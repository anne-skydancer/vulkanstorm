/**
 * @file llvotree.cpp
 * @brief LLVOTree class implementation
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

#include "llviewerprecompiledheaders.h"
#include "vsvegetation.h"
#include "noise.h"
#include <stdexcept>
namespace
{
constexpr S32 MAX_SLICES = 32, LEAF_VERTICES = 16, LEAF_INDICES = 24;
constexpr F32 LEAF_LEFT = .52f, LEAF_RIGHT = .98f, LEAF_TOP = 1, LEAF_BOTTOM = .52f, LEAF_WIDTH = 1;
constexpr F32 SRR3 = .577350269f, SRR2 = .707106781f;
} // namespace
VSTreeGeometry::VSTreeGeometry(const LLVOTree::TreeSpeciesData& species) : LLVOTree::TreeSpeciesData(species)
{
    if (mDepth > 8 || mTrunkDepth > 16 || !std::isfinite(mBranches) || mBranches < 1 || mBranches > 16)
        throw std::invalid_argument("Invalid tree species topology");
    reference();
}
void VSTreeGeometry::reference()
{
    U32 i, j;

    U32 slices = MAX_SLICES;

    S32 max_indices  = LEAF_INDICES;
    S32 max_vertices = LEAF_VERTICES;
    S32 lod;

    for (lod = 0; lod < sMAX_NUM_TREE_LOD_LEVELS; lod++)
    {
        slices                = sLODSlices[lod];
        sLODVertexOffset[lod] = max_vertices;
        sLODVertexCount[lod]  = slices * slices;
        sLODIndexOffset[lod]  = max_indices;
        sLODIndexCount[lod]   = (slices - 1) * (slices - 1) * 6;
        max_indices += sLODIndexCount[lod];
        max_vertices += sLODVertexCount[lod];
    }

    mReference.vertices.resize(max_vertices);
    mReference.normals.resize(max_vertices);
    mReference.uv.resize(max_vertices);
    mReference.colors.resize(max_vertices);
    mReference.indices.resize(max_indices);
    LLVector3* vertices     = mReference.vertices.data();
    LLVector3* normals      = mReference.normals.data();
    LLVector2* tex_coords   = mReference.uv.data();
    LLColor4U* colors       = mReference.colors.data();
    U16*       indicesp     = mReference.indices.data();
    S32        vertex_count = 0;
    S32        index_count  = 0;

    // First leaf
    *(normals++)    = LLVector3(-SRR2, -SRR2, 0.f);
    *(tex_coords++) = LLVector2(LEAF_LEFT, LEAF_BOTTOM);
    *(vertices++)   = LLVector3(-0.5f * LEAF_WIDTH, 0.f, 0.f);
    *(colors++)     = LLColor4U::white;
    vertex_count++;

    *(normals++)    = LLVector3(SRR3, -SRR3, SRR3);
    *(tex_coords++) = LLVector2(LEAF_RIGHT, LEAF_TOP);
    *(vertices++)   = LLVector3(0.5f * LEAF_WIDTH, 0.f, 1.f);
    *(colors++)     = LLColor4U::white;
    vertex_count++;

    *(normals++)    = LLVector3(-SRR3, -SRR3, SRR3);
    *(tex_coords++) = LLVector2(LEAF_LEFT, LEAF_TOP);
    *(vertices++)   = LLVector3(-0.5f * LEAF_WIDTH, 0.f, 1.f);
    *(colors++)     = LLColor4U::white;
    vertex_count++;

    *(normals++)    = LLVector3(SRR2, -SRR2, 0.f);
    *(tex_coords++) = LLVector2(LEAF_RIGHT, LEAF_BOTTOM);
    *(vertices++)   = LLVector3(0.5f * LEAF_WIDTH, 0.f, 0.f);
    *(colors++)     = LLColor4U::white;
    vertex_count++;

    *(indicesp++) = 0;
    index_count++;
    *(indicesp++) = 1;
    index_count++;
    *(indicesp++) = 2;
    index_count++;

    *(indicesp++) = 0;
    index_count++;
    *(indicesp++) = 3;
    index_count++;
    *(indicesp++) = 1;
    index_count++;

    // Same leaf, inverse winding/normals
    *(normals++)    = LLVector3(-SRR2, SRR2, 0.f);
    *(tex_coords++) = LLVector2(LEAF_LEFT, LEAF_BOTTOM);
    *(vertices++)   = LLVector3(-0.5f * LEAF_WIDTH, 0.f, 0.f);
    *(colors++)     = LLColor4U::white;
    vertex_count++;

    *(normals++)    = LLVector3(SRR3, SRR3, SRR3);
    *(tex_coords++) = LLVector2(LEAF_RIGHT, LEAF_TOP);
    *(vertices++)   = LLVector3(0.5f * LEAF_WIDTH, 0.f, 1.f);
    *(colors++)     = LLColor4U::white;
    vertex_count++;

    *(normals++)    = LLVector3(-SRR3, SRR3, SRR3);
    *(tex_coords++) = LLVector2(LEAF_LEFT, LEAF_TOP);
    *(vertices++)   = LLVector3(-0.5f * LEAF_WIDTH, 0.f, 1.f);
    *(colors++)     = LLColor4U::white;
    vertex_count++;

    *(normals++)    = LLVector3(SRR2, SRR2, 0.f);
    *(tex_coords++) = LLVector2(LEAF_RIGHT, LEAF_BOTTOM);
    *(vertices++)   = LLVector3(0.5f * LEAF_WIDTH, 0.f, 0.f);
    *(colors++)     = LLColor4U::white;
    vertex_count++;

    *(indicesp++) = 4;
    index_count++;
    *(indicesp++) = 6;
    index_count++;
    *(indicesp++) = 5;
    index_count++;

    *(indicesp++) = 4;
    index_count++;
    *(indicesp++) = 5;
    index_count++;
    *(indicesp++) = 7;
    index_count++;

    // next leaf
    *(normals++)    = LLVector3(SRR2, -SRR2, 0.f);
    *(tex_coords++) = LLVector2(LEAF_LEFT, LEAF_BOTTOM);
    *(vertices++)   = LLVector3(0.f, -0.5f * LEAF_WIDTH, 0.f);
    *(colors++)     = LLColor4U::white;
    vertex_count++;

    *(normals++)    = LLVector3(SRR3, SRR3, SRR3);
    *(tex_coords++) = LLVector2(LEAF_RIGHT, LEAF_TOP);
    *(vertices++)   = LLVector3(0.f, 0.5f * LEAF_WIDTH, 1.f);
    *(colors++)     = LLColor4U::white;
    vertex_count++;

    *(normals++)    = LLVector3(SRR3, -SRR3, SRR3);
    *(tex_coords++) = LLVector2(LEAF_LEFT, LEAF_TOP);
    *(vertices++)   = LLVector3(0.f, -0.5f * LEAF_WIDTH, 1.f);
    *(colors++)     = LLColor4U::white;
    vertex_count++;

    *(normals++)    = LLVector3(SRR2, SRR2, 0.f);
    *(tex_coords++) = LLVector2(LEAF_RIGHT, LEAF_BOTTOM);
    *(vertices++)   = LLVector3(0.f, 0.5f * LEAF_WIDTH, 0.f);
    *(colors++)     = LLColor4U::white;
    vertex_count++;

    *(indicesp++) = 8;
    index_count++;
    *(indicesp++) = 9;
    index_count++;
    *(indicesp++) = 10;
    index_count++;

    *(indicesp++) = 8;
    index_count++;
    *(indicesp++) = 11;
    index_count++;
    *(indicesp++) = 9;
    index_count++;

    // other side of same leaf
    *(normals++)    = LLVector3(-SRR2, -SRR2, 0.f);
    *(tex_coords++) = LLVector2(LEAF_LEFT, LEAF_BOTTOM);
    *(vertices++)   = LLVector3(0.f, -0.5f * LEAF_WIDTH, 0.f);
    *(colors++)     = LLColor4U::white;
    vertex_count++;

    *(normals++)    = LLVector3(-SRR3, SRR3, SRR3);
    *(tex_coords++) = LLVector2(LEAF_RIGHT, LEAF_TOP);
    *(vertices++)   = LLVector3(0.f, 0.5f * LEAF_WIDTH, 1.f);
    *(colors++)     = LLColor4U::white;
    vertex_count++;

    *(normals++)    = LLVector3(-SRR3, -SRR3, SRR3);
    *(tex_coords++) = LLVector2(LEAF_LEFT, LEAF_TOP);
    *(vertices++)   = LLVector3(0.f, -0.5f * LEAF_WIDTH, 1.f);
    *(colors++)     = LLColor4U::white;
    vertex_count++;

    *(normals++)    = LLVector3(-SRR2, SRR2, 0.f);
    *(tex_coords++) = LLVector2(LEAF_RIGHT, LEAF_BOTTOM);
    *(vertices++)   = LLVector3(0.f, 0.5f * LEAF_WIDTH, 0.f);
    *(colors++)     = LLColor4U::white;
    vertex_count++;

    *(indicesp++) = 12;
    index_count++;
    *(indicesp++) = 14;
    index_count++;
    *(indicesp++) = 13;
    index_count++;

    *(indicesp++) = 12;
    index_count++;
    *(indicesp++) = 13;
    index_count++;
    *(indicesp++) = 15;
    index_count++;

    // Generate geometry for the cylinders

    // Different LOD's

    // Generate the vertices
    // Generate the indices

    for (lod = 0; lod < sMAX_NUM_TREE_LOD_LEVELS; lod++)
    {
        slices          = sLODSlices[lod];
        F32 base_radius = 0.65f;
        F32 top_radius  = base_radius * mTaper;
        // LL_INFOS() << "Species " << ((U32) mSpecies) << ", taper = " << sSpeciesTable[mSpecies].mTaper << LL_ENDL;
        // LL_INFOS() << "Droop " << mDroop << ", branchlength: " << mBranchLength << LL_ENDL;
        F32 angle     = 0;
        F32 angle_inc = 360.f / (slices - 1);
        F32 z         = 0.f;
        F32 z_inc     = 1.f;
        if (slices > 3)
        {
            z_inc = 1.f / (slices - 3);
        }
        F32 radius = base_radius;

        F32       x1, y1;
        F32       noise_scale = mNoiseMag;
        LLVector3 nvec;

        const F32 cap_nudge = 0.1f; // Height to 'peak' the caps on top/bottom of branch

        const S32 fractal_depth = 5;
        F32       nvec_scale    = 1.f * mNoiseScale;
        F32       nvec_scalez   = 4.f * mNoiseScale;

        F32 tex_z_repeat = mRepeatTrunkZ;

        F32 start_radius;
        F32 nangle = 0;
        F32 height = 1.f;
        F32 r0;

        for (i = 0; i < slices; i++)
        {
            if (i == 0)
            {
                z  = -cap_nudge;
                r0 = 0.0;
            }
            else if (i == (slices - 1))
            {
                z  = 1.f + cap_nudge; //((i - 2) * z_inc) + cap_nudge;
                r0 = 0.0;
            }
            else
            {
                z  = (i - 1) * z_inc;
                r0 = base_radius + (top_radius - base_radius) * z;
            }

            for (j = 0; j < slices; j++)
            {
                if (slices - 1 == j)
                {
                    angle = 0.f;
                }
                else
                {
                    angle = j * angle_inc;
                }

                nangle = angle;

                x1 = cos(angle * DEG_TO_RAD);
                y1 = sin(angle * DEG_TO_RAD);
                LLVector2 tc;
                // This isn't totally accurate.  Should compute based on slope as well.
                start_radius = r0 * (1.f + 1.2f * fabs(z - 0.66f * height) / height);
                nvec.set(cos(nangle * DEG_TO_RAD) * start_radius * nvec_scale,
                         sin(nangle * DEG_TO_RAD) * start_radius * nvec_scale,
                         z * nvec_scalez);
                // First and last slice at 0 radius (to bring in top/bottom of structure)
                radius = start_radius + turbulence3((F32*)&nvec.mV, (F32)fractal_depth) * noise_scale;

                if (slices - 1 == j)
                {
                    // Not 0.5 for slight slop factor to avoid edges on leaves
                    tc = LLVector2(0.490f, (1.f - z / 2.f) * tex_z_repeat);
                }
                else
                {
                    tc = LLVector2((angle / 360.f) * 0.5f, (1.f - z / 2.f) * tex_z_repeat);
                }

                *(vertices++)   = LLVector3(x1 * radius, y1 * radius, z);
                *(normals++)    = LLVector3(x1, y1, 0.f);
                *(tex_coords++) = tc;
                *(colors++)     = LLColor4U::white;
                vertex_count++;
            }
        }

        for (i = 0; i < (slices - 1); i++)
        {
            for (j = 0; j < (slices - 1); j++)
            {
                S32 x1_offset = j + 1;
                if ((j + 1) == slices)
                {
                    x1_offset = 0;
                }
                // Generate the matching quads
                *(indicesp) = j + (i * slices) + sLODVertexOffset[lod];
                llassert(*(indicesp) < (U32)max_vertices);
                indicesp++;
                index_count++;
                *(indicesp) = x1_offset + ((i + 1) * slices) + sLODVertexOffset[lod];
                llassert(*(indicesp) < (U32)max_vertices);
                indicesp++;
                index_count++;
                *(indicesp) = j + ((i + 1) * slices) + sLODVertexOffset[lod];
                llassert(*(indicesp) < (U32)max_vertices);
                indicesp++;
                index_count++;

                *(indicesp) = j + (i * slices) + sLODVertexOffset[lod];
                llassert(*(indicesp) < (U32)max_vertices);
                indicesp++;
                index_count++;
                *(indicesp) = x1_offset + (i * slices) + sLODVertexOffset[lod];
                llassert(*(indicesp) < (U32)max_vertices);
                indicesp++;
                index_count++;
                *(indicesp) = x1_offset + ((i + 1) * slices) + sLODVertexOffset[lod];
                llassert(*(indicesp) < (U32)max_vertices);
                indicesp++;
                index_count++;
            }
        }
        slices /= 2;
    }
}
VSVegetationMesh VSTreeGeometry::mesh(const LLVector3& position, const LLQuaternion& rotation, const LLVector3& objectScale, S32 lod,
                                      const LLVector3& bend)
{
    if (lod < 0 || lod >= 4)
        return {};
    mTrunkLOD  = lod;
    mTrunkBend = bend;
    LLMatrix4 matrix;

    // Translate to tree base  HACK - adjustment in Z plants tree underground
    const LLVector3& pos_region = position;
    // gGL.translatef(pos_agent.mV[VX], pos_agent.mV[VY], pos_agent.mV[VZ] - 0.1f);
    LLMatrix4 trans_mat;
    trans_mat.setTranslation(pos_region.mV[VX], pos_region.mV[VY], pos_region.mV[VZ] - 0.1f);
    trans_mat *= matrix;

    // Rotate to tree position and bend for current trunk/wind
    // Note that trunk stiffness controls the amount of bend at the trunk as
    // opposed to the crown of the tree
    //
    const F32 TRUNK_STIFF = 22.f;

    LLQuaternion rot = LLQuaternion(mTrunkBend.magVec() * TRUNK_STIFF * DEG_TO_RAD, LLVector4(mTrunkBend.mV[VX], mTrunkBend.mV[VY], 0)) *
                       LLQuaternion(90.f * DEG_TO_RAD, LLVector4(0, 0, 1)) * rotation;

    LLMatrix4 rot_mat(rot);
    rot_mat *= trans_mat;

    F32       radius = objectScale.magVec() * 0.05f;
    LLMatrix4 scale_mat;
    scale_mat.mMatrix[0][0] = scale_mat.mMatrix[1][1] = scale_mat.mMatrix[2][2] = radius;

    scale_mat *= rot_mat;

    //  const F32 THRESH_ANGLE_FOR_BILLBOARD = 15.f;
    //  const F32 BLEND_RANGE_FOR_BILLBOARD = 3.f;

    F32 droop = mDroop + 25.f * (1.f - mTrunkBend.magVec());

    S32 stop_depth = 0;
    F32 alpha      = 1.0;

    U32 vert_count  = 0;
    U32 index_count = 0;

    calcNumVerts(vert_count, index_count, mTrunkLOD, stop_depth, mDepth, mTrunkDepth, mBranches);

    if (vert_count > 65535 || index_count > 393210)
        throw std::length_error("Tree geometry exceeds admission limit");
    VSVegetationMesh result;
    result.vertices.resize(vert_count);
    result.normals.resize(vert_count);
    result.uv.resize(vert_count);
    result.colors.resize(vert_count);
    result.indices.resize(index_count);
    LLStrider<LLVector3> vertices, normals;
    LLStrider<LLVector2> tex_coords;
    LLStrider<LLColor4U> colors;
    LLStrider<U16>       indices;
    vertices       = result.vertices.data();
    normals        = result.normals.data();
    tex_coords     = result.uv.data();
    colors         = result.colors.data();
    indices        = result.indices.data();
    U16 idx_offset = 0;
    genBranchPipeline(vertices, normals, tex_coords, colors, indices, idx_offset, scale_mat, lod, stop_depth, mDepth, mTrunkDepth, 1.f,
                      mTwist, droop, mBranches, alpha);
    return result;
}
void VSTreeGeometry::appendMesh(LLStrider<LLVector3>& vertices,
                                LLStrider<LLVector3>& normals,
                                LLStrider<LLVector2>& tex_coords,
                                LLStrider<LLColor4U>& colors,
                                LLStrider<U16>&       indices,
                                U16&                  cur_idx,
                                LLMatrix4&            matrix,
                                LLMatrix4&            norm_mat,
                                S32                   vert_start,
                                S32                   vert_count,
                                S32                   index_count,
                                S32                   index_offset)
{
    LLStrider<LLVector3> v;
    LLStrider<LLVector3> n;
    LLStrider<LLVector2> t;
    LLStrider<LLColor4U> c;
    LLStrider<U16>       idx;

    v   = mReference.vertices.data();
    n   = mReference.normals.data();
    t   = mReference.uv.data();
    c   = mReference.colors.data();
    idx = mReference.indices.data();

    // copy/transform vertices into mesh - check
    for (S32 i = 0; i < vert_count; i++)
    {
        U16 index      = vert_start + i;
        *vertices++    = v[index] * matrix;
        LLVector3 norm = n[index] * norm_mat;
        norm.normalize();
        *normals++    = norm;
        *tex_coords++ = t[index];
        *colors++     = c[index];
    }

    // copy offset indices into mesh - check
    for (S32 i = 0; i < index_count; i++)
    {
        U16 index  = index_offset + i;
        *indices++ = idx[index] - vert_start + cur_idx;
    }

    // increment index offset - check
    cur_idx += vert_count;
}

void VSTreeGeometry::genBranchPipeline(LLStrider<LLVector3>& vertices,
                                       LLStrider<LLVector3>& normals,
                                       LLStrider<LLVector2>& tex_coords,
                                       LLStrider<LLColor4U>& colors,
                                       LLStrider<U16>&       indices,
                                       U16&                  index_offset,
                                       LLMatrix4&            matrix,
                                       S32                   trunk_LOD,
                                       S32                   stop_level,
                                       U16                   depth,
                                       U16                   trunk_depth,
                                       F32                   scale,
                                       F32                   twist,
                                       F32                   droop,
                                       F32                   branches,
                                       F32                   alpha)
{
    //
    //  Generates a tree mesh by recursing, generating branches and then a 'leaf' texture.

    F32 constant_twist;
    F32 width = 0;

    F32 length = ((trunk_depth || (scale == 1.f)) ? mTrunkLength : mBranchLength);
    F32 aspect = ((trunk_depth || (scale == 1.f)) ? mTrunkAspect : mBranchAspect);

    constant_twist = 360.f / branches;

    if (stop_level >= 0)
    {
        if (depth > stop_level)
        {
            {
                llassert(sLODIndexCount[trunk_LOD] > 0);
                width = scale * length * aspect;
                LLMatrix4 scale_mat;
                scale_mat.mMatrix[0][0] = width;
                scale_mat.mMatrix[1][1] = width;
                scale_mat.mMatrix[2][2] = scale * length;
                scale_mat *= matrix;

                glm::mat4 norm(glm::make_mat4((F32*)scale_mat.mMatrix));
                LLMatrix4 norm_mat = LLMatrix4(glm::value_ptr(glm::transpose(glm::inverse(norm))));

                appendMesh(vertices, normals, tex_coords, colors, indices, index_offset, scale_mat, norm_mat, sLODVertexOffset[trunk_LOD],
                           sLODVertexCount[trunk_LOD], sLODIndexCount[trunk_LOD], sLODIndexOffset[trunk_LOD]);
            }

            // Recurse to create more branches
            for (S32 i = 0; i < (S32)branches; i++)
            {
                LLMatrix4 trans_mat;
                trans_mat.setTranslation(0, 0, scale * length);
                trans_mat *= matrix;

                LLQuaternion rot =
                    LLQuaternion(20.f * DEG_TO_RAD, LLVector4(0.f, 0.f, 1.f)) * LLQuaternion(droop * DEG_TO_RAD, LLVector4(0.f, 1.f, 0.f)) *
                    LLQuaternion(((constant_twist + ((i % 2 == 0) ? twist : -twist)) * i) * DEG_TO_RAD, LLVector4(0.f, 0.f, 1.f));

                LLMatrix4 rot_mat(rot);
                rot_mat *= trans_mat;

                genBranchPipeline(vertices, normals, tex_coords, colors, indices, index_offset, rot_mat, trunk_LOD, stop_level, depth - 1,
                                  0, scale * mScaleStep, twist, droop, branches, alpha);
            }
            //  Recurse to continue trunk
            if (trunk_depth)
            {
                LLMatrix4 trans_mat;
                trans_mat.setTranslation(0, 0, scale * length);
                trans_mat *= matrix;

                LLMatrix4 rot_mat(70.5f * DEG_TO_RAD, LLVector4(0, 0, 1));
                rot_mat *= trans_mat; // rotate a bit around Z when ascending
                genBranchPipeline(vertices, normals, tex_coords, colors, indices, index_offset, rot_mat, trunk_LOD, stop_level, depth,
                                  trunk_depth - 1, scale * mScaleStep, twist, droop, branches, alpha);
            }
        }
        else
        {
            //
            //  Append leaves as two 90 deg crossed quads with leaf textures
            //
            {
                LLMatrix4 scale_mat;
                scale_mat.mMatrix[0][0] = scale_mat.mMatrix[1][1] = scale_mat.mMatrix[2][2] = scale * mLeafScale;

                scale_mat *= matrix;

                glm::mat4 norm(glm::make_mat4((F32*)scale_mat.mMatrix));
                LLMatrix4 norm_mat = LLMatrix4(glm::value_ptr(glm::transpose(glm::inverse(norm))));

                appendMesh(vertices, normals, tex_coords, colors, indices, index_offset, scale_mat, norm_mat, 0, LEAF_VERTICES,
                           LEAF_INDICES, 0);
            }
        }
    }
}

void VSTreeGeometry::calcNumVerts(U32& vert_count, U32& index_count, S32 trunk_LOD, S32 stop_level, U16 depth, U16 trunk_depth,
                                  F32 branches)
{
    if (vert_count > 65535 || index_count > 393210)
        throw std::length_error("Tree geometry exceeds admission limit");
    if (stop_level >= 0)
    {
        if (depth > stop_level)
        {
            index_count += sLODIndexCount[trunk_LOD];
            vert_count += sLODVertexCount[trunk_LOD];

            // Recurse to create more branches
            for (S32 i = 0; i < (S32)branches; i++)
            {
                calcNumVerts(vert_count, index_count, trunk_LOD, stop_level, depth - 1, 0, branches);
            }

            //  Recurse to continue trunk
            if (trunk_depth)
            {
                calcNumVerts(vert_count, index_count, trunk_LOD, stop_level, depth, trunk_depth - 1, branches);
            }
        }
        else
        {
            index_count += LEAF_INDICES;
            vert_count += LEAF_VERTICES;
        }
    }
    else
    {
        index_count += LEAF_INDICES;
        vert_count += LEAF_VERTICES;
    }
}

VSVegetationMesh vs_grass_mesh(const LLVector3& center, const LLVector3& scale, F32 width, F32 height, const VSGrassBlade* blades,
                               unsigned count, const VSVegetationGround& ground)
{
    if (!blades || count > 32 || !ground || width <= 0 || height <= 0)
        return {};
    VSVegetationMesh mesh;
    mesh.vertices.reserve(count * 8);
    mesh.normals.reserve(count * 8);
    mesh.uv.reserve(count * 8);
    mesh.colors.reserve(count * 8);
    mesh.indices.reserve(count * 12);
    constexpr F32 base = .25f, bladeSize = .5f;
    constexpr U16 triangles[] = { 0, 2, 4, 2, 6, 4, 1, 5, 3, 3, 5, 7 };
    for (unsigned i = 0; i < count; ++i)
    {
        const auto& blade = blades[i];
        const F32   x = blade.x * scale.mV[VX], y = blade.y * scale.mV[VY];
        const F32   xf = blade.rx * base * width * blade.wind, yf = blade.ry * base * width * blade.wind;
        const F32   heightOffset = bladeSize * height * blade.wind;
        LLVector3   v1(center.mV[VX] + x + xf, center.mV[VY] + y + yf, 0);
        v1.mV[VZ]          = ground(v1);
        const LLVector3 v2 = v1 + LLVector3(blade.dx, blade.dy, heightOffset);
        // Retain the reference producer's asymmetric opposite-base placement.
        LLVector3 v3(center.mV[VX] + x - xf, center.mV[VY] + y - xf, 0);
        v3.mV[VZ]              = ground(v3);
        const LLVector3 v4     = v3 + LLVector3(blade.dx, blade.dy, heightOffset);
        LLVector3       normal = (v1 - v2) % (v2 - v3);
        normal.mV[VZ]          = .75f;
        normal.normalize();
        LLVector3 back   = -normal;
        back.mV[VZ]      = -back.mV[VZ];
        const U16 offset = U16(mesh.vertices.size());
        for (const auto& vertex : { v1, v2, v3, v4 })
        {
            mesh.vertices.push_back(vertex);
            mesh.vertices.push_back(vertex);
            mesh.normals.push_back(normal);
            mesh.normals.push_back(back);
        }
        for (const LLVector2 uv : { LLVector2(0, 0), LLVector2(0, .98f), LLVector2(1, 0), LLVector2(1, .98f) })
        {
            mesh.uv.push_back(uv);
            mesh.uv.push_back(uv);
            mesh.colors.push_back(LLColor4U::white);
            mesh.colors.push_back(LLColor4U::white);
        }
        for (const U16 index : triangles)
            mesh.indices.push_back(offset + index);
    }
    return mesh;
}
