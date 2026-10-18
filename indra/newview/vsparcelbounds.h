// Native peer of LLViewerParcelOverlay::encroachesOnNearbyParcel. LGPL-2.1.
#pragma once
#include "llbbox.h"
#include "llparcel.h"
#include <vector>

template<class Cell>
bool vs_parcel_bounds_cross(const std::vector<LLBBox>& boxes, F32 width, F32 height, Cell cell)
{
    const S32 columns = S32(width / PARCEL_GRID_STEP_METERS);
    const S32 rows = S32(height / PARCEL_GRID_STEP_METERS);
    for (const auto& box : boxes)
    {
        const LLVector3 min = box.getMinAgent();
        const LLVector3 max = box.getMaxAgent();
        if (min.mV[VX] < 0 || min.mV[VY] < 0 || max.mV[VX] > width || max.mV[VY] > height)
            return true;
        const S32 left = S32(llclamp(min.mV[VX] / PARCEL_GRID_STEP_METERS, 0.f, F32(columns - 1)));
        const S32 right = S32(llclamp(max.mV[VX] / PARCEL_GRID_STEP_METERS, 0.f, F32(columns - 1)));
        const S32 bottom = S32(llclamp(min.mV[VY] / PARCEL_GRID_STEP_METERS, 0.f, F32(rows - 1)));
        const S32 top = S32(llclamp(max.mV[VY] / PARCEL_GRID_STEP_METERS, 0.f, F32(rows - 1)));
        for (S32 row = bottom; row <= top; ++row)
        {
            for (S32 col = left; col <= right; ++col)
            {
                if (col < columns - 1 && (cell(row, col + 1) & PARCEL_WEST_LINE) && col < right)
                    return true;
                if (row < rows - 1 && (cell(row + 1, col) & PARCEL_SOUTH_LINE) && row < top)
                    return true;
            }
        }
    }
    return false;
}
