LLVector3d  LLWorld::clipToVisibleRegions(const LLVector3d &start_pos, const LLVector3d &end_pos)
{
    if (positionRegionValidGlobal(end_pos))
    {
        return end_pos;
    }

    LLViewerRegion* regionp = getRegionFromPosGlobal(start_pos);
    if (!regionp)
    {
        return start_pos;
    }

    LLVector3d delta_pos = end_pos - start_pos;
    LLVector3d delta_pos_abs;
    delta_pos_abs.setVec(delta_pos);
    delta_pos_abs.abs();

    LLVector3 region_coord = regionp->getPosRegionFromGlobal(end_pos);
    F64 clip_factor = 1.0;
    F32 region_width = regionp->getWidth();
    if (region_coord.mV[VX] < 0.f)
    {
        if (region_coord.mV[VY] < region_coord.mV[VX])
        {
            // clip along y -
            clip_factor = -(region_coord.mV[VY] / delta_pos_abs.mdV[VY]);
        }
        else
        {
            // clip along x -
            clip_factor = -(region_coord.mV[VX] / delta_pos_abs.mdV[VX]);
        }
    }
    else if (region_coord.mV[VX] > region_width)
    {
        if (region_coord.mV[VY] > region_coord.mV[VX])
        {
            // clip along y +
            clip_factor = (region_coord.mV[VY] - region_width) / delta_pos_abs.mdV[VY];
        }
        else
        {
            //clip along x +
            clip_factor = (region_coord.mV[VX] - region_width) / delta_pos_abs.mdV[VX];
        }
    }
    else if (region_coord.mV[VY] < 0.f)
    {
        // clip along y -
        clip_factor = -(region_coord.mV[VY] / delta_pos_abs.mdV[VY]);
    }
    else if (region_coord.mV[VY] > region_width)
    {
        // clip along y +
        clip_factor = (region_coord.mV[VY] - region_width) / delta_pos_abs.mdV[VY];
    }

    // clamp to within region dimensions
    LLVector3d final_region_pos = LLVector3d(region_coord) - (delta_pos * clip_factor);
    final_region_pos.mdV[VX] = llclamp(final_region_pos.mdV[VX], 0.0,
                                       (F64)(region_width - F_ALMOST_ZERO));
    final_region_pos.mdV[VY] = llclamp(final_region_pos.mdV[VY], 0.0,
                                       (F64)(region_width - F_ALMOST_ZERO));
    final_region_pos.mdV[VZ] = llclamp(final_region_pos.mdV[VZ], 0.0,
                                       (F64)(LLWorld::getInstance()->getRegionMaxHeight() - F_ALMOST_ZERO));
    return regionp->getPosGlobalFromRegion(LLVector3(final_region_pos));
}