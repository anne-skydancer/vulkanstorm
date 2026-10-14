/**
 * @file llwind.cpp
 * @brief LLWind class implementation
 *
 * $LicenseInfo:firstyear=2000&license=viewerlgpl$
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

// Wind is a lattice.  It is computed on the simulator, and transmitted to the viewer.
// It drives special effects like smoke blowing, trees bending, and grass wiggling.
//
// Currently wind lattice does not interpolate correctly to neighbors.  This will need
// work.

#include "llviewerprecompiledheaders.h"
#include "indra_constants.h"

#include "llwind.h"
#include "vswindfield.h"

// linden libraries
#include "llgl.h"
#include "patch_dct.h"
#include "patch_code.h"

// viewer
#include "noise.h"
#include "v4color.h"
#include "llworld.h"

// <FS:CR> Aurora Sim
#include "llviewerregion.h"
#include "llagent.h"
// </FS:CR> Aurora Sim


//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

LLWind::LLWind()
:   mSize(16)
{
    init();
}


LLWind::~LLWind()
{
    delete [] mVelX;
    delete [] mVelY;
}


//////////////////////////////////////////////////////////////////////
// Public Methods
//////////////////////////////////////////////////////////////////////


void LLWind::init()
{
    LL_DEBUGS("Wind") << "initializing wind size: "<< mSize << LL_ENDL;

    // Initialize vector data
    mVelX = new F32[mSize*mSize];
    mVelY = new F32[mSize*mSize];

    S32 i;
    for (i = 0; i < mSize*mSize; i++)
    {
        mVelX[i] = 0.5f;
        mVelY[i] = 0.5f;
    }
}


void LLWind::decompress(LLBitPack &bitpack, LLGroupHeader *group_headerp)
{
    LLPatchHeader  patch_header;
    S32 buffer[16*16];

    init_patch_decompressor(group_headerp->patch_size);

    // Don't use the packed group_header stride because the strides used on
    // simulator and viewer are not equal.
    group_headerp->stride = group_headerp->patch_size;
    set_group_of_patch_header(group_headerp);

    // X component
// <FS:CR> Aurora Sim
    //decode_patch_header(bitpack, &patch_header);
    decode_patch_header(bitpack, &patch_header, false);
// </FS:CR> Aurora Sim
    decode_patch(bitpack, buffer);
    decompress_patch(mVelX, buffer, &patch_header);

    // Y component
// <FS:CR> Aurora Sim
    //decode_patch_header(bitpack, &patch_header);
    decode_patch_header(bitpack, &patch_header, false);
// </FS:CR> Aurora Sim
    decode_patch(bitpack, buffer);
    decompress_patch(mVelY, buffer, &patch_header);

    S32 i, j, k;

    for (j=1; j<mSize-1; j++)
    {
        for (i=1; i<mSize-1; i++)
        {
            k = i + j * mSize;
            *(mVelX + k) = *(mVelX + k);
            *(mVelY + k) = *(mVelY + k);
        }
    }

    i = mSize - 1;
    for (j=1; j<mSize-1; j++)
    {
        k = i + j * mSize;
        *(mVelX + k) = *(mVelX + k);
        *(mVelY + k) = *(mVelY + k);
    }
    i = 0;
    for (j=1; j<mSize-1; j++)
    {
        k = i + j * mSize;
        *(mVelX + k) = *(mVelX + k);
        *(mVelY + k) = *(mVelY + k);
    }
    j = mSize - 1;
    for (i=1; i<mSize-1; i++)
    {
        k = i + j * mSize;
        *(mVelX + k) = *(mVelX + k);
        *(mVelY + k) = *(mVelY + k);
    }
    j = 0;
    for (i=1; i<mSize-1; i++)
    {
        k = i + j * mSize;
        *(mVelX + k) = *(mVelX + k);
        *(mVelY + k) = *(mVelY + k);
    }
}


LLVector3 LLWind::getAverage()
{
    //  Returns in average_wind the average wind velocity
    LLVector3 average(0.0f, 0.0f, 0.0f);
    S32 i, grid_count;
    grid_count = mSize * mSize;
    for (i = 0; i < grid_count; i++)
    {
        average.mV[VX] += mVelX[i];
        average.mV[VY] += mVelY[i];
    }

    average *= 1.f/((F32)(grid_count)) * WIND_SCALE_HACK;
    return average;
}


LLVector3 LLWind::getVelocityNoisy(const LLVector3 &pos_region, const F32 dim)
{
    //  Resolve a value, using fractal summing to perturb the returned value
    LLVector3 r_val(0,0,0);
    F32 norm = 1.0f;
    if (dim == 8)
    {
        norm = 1.875;
    }
    else if (dim == 4)
    {
        norm = 1.75;
    }
    else if (dim == 2)
    {
        norm = 1.5;
    }

    F32 temp_dim = dim;
    while (temp_dim >= 1.0)
    {
        LLVector3 pos_region_scaled(pos_region * temp_dim);
        r_val += getVelocity(pos_region_scaled) * (1.0f/temp_dim);
        temp_dim /= 2.0;
    }

    return r_val * (1.0f/norm) * WIND_SCALE_HACK;
}


LLVector3 LLWind::getVelocity(const LLVector3 &pos_region)
{
    if (!gAgent.getRegion())
        return LLVector3::zero;
    const F32 width = gAgent.getRegion()->getWidth();
    return vs_wind_velocity(std::span<const float, 256>(mVelX, 256), std::span<const float, 256>(mVelY, 256), pos_region, width, width);
}

void LLWind::setOriginGlobal(const LLVector3d &origin_global)
{
    mOriginGlobal = origin_global;
}


