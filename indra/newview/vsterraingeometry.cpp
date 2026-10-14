/**
 * @file llvosurfacepatch.cpp
 * @brief Viewer-object derived "surface patch", which is a piece of terrain
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
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
#include "vsterraingeometry.h"
#include <stdexcept>
#include <utility>
VSTerrainGeometry::VSTerrainGeometry(U32 size, U32 stride, U32 north, U32 east, Evaluate evaluate) :
    mPatchSize(size),
    mLastStride(stride),
    mLastNorthStride(north),
    mLastEastStride(east),
    mEval(std::move(evaluate))
{
    auto valid = [&](U32 value)
    {
        return value && !(value & (value - 1)) && value <= size;
    };
    if ((size != 16 && size != 32) || !valid(stride) || !valid(north) || !valid(east))
        throw std::invalid_argument("Invalid terrain geometry stride");
}
VSTerrainMesh VSTerrainGeometry::mesh()
{
    if (!mEval)
        return {};
    S32 vertices = 0, indices = 0;
    getGeomSizesMain(mLastStride, vertices, indices);
    getGeomSizesNorth(mLastStride, mLastNorthStride, vertices, indices);
    getGeomSizesEast(mLastStride, mLastEastStride, vertices, indices);
    VSTerrainMesh result;
    result.vertices.resize(vertices);
    result.normals.resize(vertices);
    result.uv.resize(vertices);
    result.detail.resize(vertices);
    result.indices.resize(indices);
    LLStrider<LLVector3> v, n;
    LLStrider<LLVector2> u, d;
    LLStrider<U16>       i;
    v          = result.vertices.data();
    n          = result.normals.data();
    u          = result.uv.data();
    d          = result.detail.data();
    i          = result.indices.data();
    U32 offset = 0;
    updateMainGeometry(v, n, u, d, i, offset);
    updateNorthGeometry(v, n, u, d, i, offset);
    updateEastGeometry(v, n, u, d, i, offset);
    return result;
}
void VSTerrainGeometry::updateMainGeometry(LLStrider<LLVector3>& verticesp,
                                           LLStrider<LLVector3>& normalsp,
                                           LLStrider<LLVector2>& texCoords0p,
                                           LLStrider<LLVector2>& texCoords1p,
                                           LLStrider<U16>&       indicesp,
                                           U32&                  index_offset)
{
    S32 i, j, x, y;

    U32 patch_size, render_stride;
    S32 num_vertices, num_indices;
    U32 index;

    llassert(mLastStride > 0);

    render_stride = mLastStride;
    patch_size    = mPatchSize;
    S32 vert_size = patch_size / render_stride;

    ///////////////////////////
    //
    // Render the main patch
    //
    //

    num_vertices = 0;
    num_indices  = 0;
    // First, figure out how many vertices we need...
    getGeomSizesMain(render_stride, num_vertices, num_indices);

    if (num_vertices > 0)
    {
        // Generate patch points first
        for (j = 0; j < vert_size; j++)
        {
            for (i = 0; i < vert_size; i++)
            {
                x = i * render_stride;
                y = j * render_stride;
                mEval(x, y, render_stride, verticesp.get(), normalsp.get(), texCoords0p.get(), texCoords1p.get());
                verticesp++;
                normalsp++;
                texCoords0p++;
                texCoords1p++;
            }
        }

        for (j = 0; j < (vert_size - 1); j++)
        {
            if (j % 2)
            {
                for (i = (vert_size - 1); i > 0; i--)
                {
                    index         = (i - 1) + j * vert_size;
                    *(indicesp++) = index_offset + index;

                    index         = i + (j + 1) * vert_size;
                    *(indicesp++) = index_offset + index;

                    index         = (i - 1) + (j + 1) * vert_size;
                    *(indicesp++) = index_offset + index;

                    index         = (i - 1) + j * vert_size;
                    *(indicesp++) = index_offset + index;

                    index         = i + j * vert_size;
                    *(indicesp++) = index_offset + index;

                    index         = i + (j + 1) * vert_size;
                    *(indicesp++) = index_offset + index;
                }
            }
            else
            {
                for (i = 0; i < (vert_size - 1); i++)
                {
                    index         = i + j * vert_size;
                    *(indicesp++) = index_offset + index;

                    index         = (i + 1) + (j + 1) * vert_size;
                    *(indicesp++) = index_offset + index;

                    index         = i + (j + 1) * vert_size;
                    *(indicesp++) = index_offset + index;

                    index         = i + j * vert_size;
                    *(indicesp++) = index_offset + index;

                    index         = (i + 1) + j * vert_size;
                    *(indicesp++) = index_offset + index;

                    index         = (i + 1) + (j + 1) * vert_size;
                    *(indicesp++) = index_offset + index;
                }
            }
        }
    }
    index_offset += num_vertices;
}

void VSTerrainGeometry::updateNorthGeometry(LLStrider<LLVector3>& verticesp,
                                            LLStrider<LLVector3>& normalsp,
                                            LLStrider<LLVector2>& texCoords0p,
                                            LLStrider<LLVector2>& texCoords1p,
                                            LLStrider<U16>&       indicesp,
                                            U32&                  index_offset)
{
    S32 i, x, y;

    S32 num_vertices;

    U32 render_stride = mLastStride;
    S32 patch_size    = mPatchSize;
    S32 length        = patch_size / render_stride;
    S32 half_length   = length / 2;
    U32 north_stride  = mLastNorthStride;

    ///////////////////////////
    //
    // Render the north strip
    //
    //

    // Stride lengths are the same
    if (north_stride == render_stride)
    {
        num_vertices = 2 * length + 1;

        // Main patch
        for (i = 0; i < length; i++)
        {
            x = i * render_stride;
            y = mPatchSize - render_stride;

            mEval(x, y, render_stride, verticesp.get(), normalsp.get(), texCoords0p.get(), texCoords1p.get());
            verticesp++;
            normalsp++;
            texCoords0p++;
            texCoords1p++;
        }

        // North patch
        for (i = 0; i <= length; i++)
        {
            x = i * render_stride;
            y = mPatchSize;
            mEval(x, y, render_stride, verticesp.get(), normalsp.get(), texCoords0p.get(), texCoords1p.get());
            verticesp++;
            normalsp++;
            texCoords0p++;
            texCoords1p++;
        }

        for (i = 0; i < length; i++)
        {
            // Generate indices
            *(indicesp++) = index_offset + i;
            *(indicesp++) = index_offset + length + i + 1;
            *(indicesp++) = index_offset + length + i;

            if (i != length - 1)
            {
                *(indicesp++) = index_offset + i;
                *(indicesp++) = index_offset + i + 1;
                *(indicesp++) = index_offset + length + i + 1;
            }
        }
    }
    else if (north_stride > render_stride)
    {
        // North stride is longer (has less vertices)
        num_vertices = length + length / 2 + 1;

        // Iterate through this patch's points
        for (i = 0; i < length; i++)
        {
            x = i * render_stride;
            y = mPatchSize - render_stride;

            mEval(x, y, render_stride, verticesp.get(), normalsp.get(), texCoords0p.get(), texCoords1p.get());
            verticesp++;
            normalsp++;
            texCoords0p++;
            texCoords1p++;
        }

        // Iterate through the north patch's points
        for (i = 0; i <= length; i += 2)
        {
            x = i * render_stride;
            y = mPatchSize;

            mEval(x, y, render_stride, verticesp.get(), normalsp.get(), texCoords0p.get(), texCoords1p.get());
            verticesp++;
            normalsp++;
            texCoords0p++;
            texCoords1p++;
        }

        for (i = 0; i < length; i++)
        {
            if (!(i % 2))
            {
                *(indicesp++) = index_offset + i;
                *(indicesp++) = index_offset + i + 1;
                *(indicesp++) = index_offset + length + (i / 2);

                *(indicesp++) = index_offset + i + 1;
                *(indicesp++) = index_offset + length + (i / 2) + 1;
                *(indicesp++) = index_offset + length + (i / 2);
            }
            else if (i < (length - 1))
            {
                *(indicesp++) = index_offset + i;
                *(indicesp++) = index_offset + i + 1;
                *(indicesp++) = index_offset + length + (i / 2) + 1;
            }
        }
    }
    else
    {
        // North stride is shorter (more vertices)
        length       = patch_size / north_stride;
        half_length  = length / 2;
        num_vertices = length + half_length + 1;

        // Iterate through this patch's points
        for (i = 0; i < length; i += 2)
        {
            x = i * north_stride;
            y = mPatchSize - render_stride;

            mEval(x, y, render_stride, verticesp.get(), normalsp.get(), texCoords0p.get(), texCoords1p.get());
            verticesp++;
            normalsp++;
            texCoords0p++;
            texCoords1p++;
        }

        // Iterate through the north patch's points
        for (i = 0; i <= length; i++)
        {
            x = i * north_stride;
            y = mPatchSize;

            mEval(x, y, render_stride, verticesp.get(), normalsp.get(), texCoords0p.get(), texCoords1p.get());
            verticesp++;
            normalsp++;
            texCoords0p++;
            texCoords1p++;
        }

        for (i = 0; i < length; i++)
        {
            if (!(i % 2))
            {
                *(indicesp++) = index_offset + half_length + i;
                *(indicesp++) = index_offset + i / 2;
                *(indicesp++) = index_offset + half_length + i + 1;
            }
            else if (i < (length - 2))
            {
                *(indicesp++) = index_offset + half_length + i;
                *(indicesp++) = index_offset + i / 2;
                *(indicesp++) = index_offset + i / 2 + 1;

                *(indicesp++) = index_offset + half_length + i;
                *(indicesp++) = index_offset + i / 2 + 1;
                *(indicesp++) = index_offset + half_length + i + 1;
            }
            else
            {
                *(indicesp++) = index_offset + half_length + i;
                *(indicesp++) = index_offset + i / 2;
                *(indicesp++) = index_offset + half_length + i + 1;
            }
        }
    }
    index_offset += num_vertices;
}

void VSTerrainGeometry::updateEastGeometry(LLStrider<LLVector3>& verticesp,
                                           LLStrider<LLVector3>& normalsp,
                                           LLStrider<LLVector2>& texCoords0p,
                                           LLStrider<LLVector2>& texCoords1p,
                                           LLStrider<U16>&       indicesp,
                                           U32&                  index_offset)
{
    S32 i, x, y;

    S32 num_vertices;

    U32 render_stride = mLastStride;
    S32 patch_size    = mPatchSize;
    S32 length        = patch_size / render_stride;
    S32 half_length   = length / 2;

    U32 east_stride = mLastEastStride;

    // Stride lengths are the same
    if (east_stride == render_stride)
    {
        num_vertices = 2 * length + 1;

        // Main patch
        for (i = 0; i < length; i++)
        {
            x = mPatchSize - render_stride;
            y = i * render_stride;

            mEval(x, y, render_stride, verticesp.get(), normalsp.get(), texCoords0p.get(), texCoords1p.get());
            verticesp++;
            normalsp++;
            texCoords0p++;
            texCoords1p++;
        }

        // East patch
        for (i = 0; i <= length; i++)
        {
            x = mPatchSize;
            y = i * render_stride;
            mEval(x, y, render_stride, verticesp.get(), normalsp.get(), texCoords0p.get(), texCoords1p.get());
            verticesp++;
            normalsp++;
            texCoords0p++;
            texCoords1p++;
        }

        for (i = 0; i < length; i++)
        {
            // Generate indices
            *(indicesp++) = index_offset + i;
            *(indicesp++) = index_offset + length + i;
            *(indicesp++) = index_offset + length + i + 1;

            if (i != length - 1)
            {
                *(indicesp++) = index_offset + i;
                *(indicesp++) = index_offset + length + i + 1;
                *(indicesp++) = index_offset + i + 1;
            }
        }
    }
    else if (east_stride > render_stride)
    {
        // East stride is longer (has less vertices)
        num_vertices = length + half_length + 1;

        // Iterate through this patch's points
        for (i = 0; i < length; i++)
        {
            x = mPatchSize - render_stride;
            y = i * render_stride;

            mEval(x, y, render_stride, verticesp.get(), normalsp.get(), texCoords0p.get(), texCoords1p.get());
            verticesp++;
            normalsp++;
            texCoords0p++;
            texCoords1p++;
        }
        // Iterate through the east patch's points
        for (i = 0; i <= length; i += 2)
        {
            x = mPatchSize;
            y = i * render_stride;

            mEval(x, y, render_stride, verticesp.get(), normalsp.get(), texCoords0p.get(), texCoords1p.get());
            verticesp++;
            normalsp++;
            texCoords0p++;
            texCoords1p++;
        }

        for (i = 0; i < length; i++)
        {
            if (!(i % 2))
            {
                *(indicesp++) = index_offset + i;
                *(indicesp++) = index_offset + length + (i / 2);
                *(indicesp++) = index_offset + i + 1;

                *(indicesp++) = index_offset + i + 1;
                *(indicesp++) = index_offset + length + (i / 2);
                *(indicesp++) = index_offset + length + (i / 2) + 1;
            }
            else if (i < (length - 1))
            {
                *(indicesp++) = index_offset + i;
                *(indicesp++) = index_offset + length + (i / 2) + 1;
                *(indicesp++) = index_offset + i + 1;
            }
        }
    }
    else
    {
        // East stride is shorter (more vertices)
        length       = patch_size / east_stride;
        half_length  = length / 2;
        num_vertices = length + length / 2 + 1;

        // Iterate through this patch's points
        for (i = 0; i < length; i += 2)
        {
            x = mPatchSize - render_stride;
            y = i * east_stride;

            mEval(x, y, render_stride, verticesp.get(), normalsp.get(), texCoords0p.get(), texCoords1p.get());
            verticesp++;
            normalsp++;
            texCoords0p++;
            texCoords1p++;
        }
        // Iterate through the east patch's points
        for (i = 0; i <= length; i++)
        {
            x = mPatchSize;
            y = i * east_stride;

            mEval(x, y, render_stride, verticesp.get(), normalsp.get(), texCoords0p.get(), texCoords1p.get());
            verticesp++;
            normalsp++;
            texCoords0p++;
            texCoords1p++;
        }

        for (i = 0; i < length; i++)
        {
            if (!(i % 2))
            {
                *(indicesp++) = index_offset + half_length + i;
                *(indicesp++) = index_offset + half_length + i + 1;
                *(indicesp++) = index_offset + i / 2;
            }
            else if (i < (length - 2))
            {
                *(indicesp++) = index_offset + half_length + i;
                *(indicesp++) = index_offset + i / 2 + 1;
                *(indicesp++) = index_offset + i / 2;

                *(indicesp++) = index_offset + half_length + i;
                *(indicesp++) = index_offset + half_length + i + 1;
                *(indicesp++) = index_offset + i / 2 + 1;
            }
            else
            {
                *(indicesp++) = index_offset + half_length + i;
                *(indicesp++) = index_offset + half_length + i + 1;
                *(indicesp++) = index_offset + i / 2;
            }
        }
    }
    index_offset += num_vertices;
}

void VSTerrainGeometry::getGeomSizesMain(const S32 stride, S32& num_vertices, S32& num_indices)
{
    S32 patch_size = mPatchSize;

    // First, figure out how many vertices we need...
    S32 vert_size = patch_size / stride;
    if (vert_size >= 2)
    {
        num_vertices += vert_size * vert_size;
        num_indices += 6 * (vert_size - 1) * (vert_size - 1);
    }
}

void VSTerrainGeometry::getGeomSizesNorth(const S32 stride, const S32 north_stride, S32& num_vertices, S32& num_indices)
{
    S32 patch_size = mPatchSize;
    S32 length     = patch_size / stride;
    // Stride lengths are the same
    if (north_stride == stride)
    {
        num_vertices += 2 * length + 1;
        num_indices += length * 6 - 3;
    }
    else if (north_stride > stride)
    {
        // North stride is longer (has less vertices)
        num_vertices += length + (length / 2) + 1;
        num_indices += (length / 2) * 9 - 3;
    }
    else
    {
        // North stride is shorter (more vertices)
        length = patch_size / north_stride;
        num_vertices += length + (length / 2) + 1;
        num_indices += 9 * (length / 2) - 3;
    }
}

void VSTerrainGeometry::getGeomSizesEast(const S32 stride, const S32 east_stride, S32& num_vertices, S32& num_indices)
{
    S32 patch_size = mPatchSize;
    S32 length     = patch_size / stride;
    // Stride lengths are the same
    if (east_stride == stride)
    {
        num_vertices += 2 * length + 1;
        num_indices += length * 6 - 3;
    }
    else if (east_stride > stride)
    {
        // East stride is longer (has less vertices)
        num_vertices += length + (length / 2) + 1;
        num_indices += (length / 2) * 9 - 3;
    }
    else
    {
        // East stride is shorter (more vertices)
        length = patch_size / east_stride;
        num_vertices += length + (length / 2) + 1;
        num_indices += 9 * (length / 2) - 3;
    }
}
