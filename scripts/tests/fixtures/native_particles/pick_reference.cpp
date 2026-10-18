bool LLVOPartGroup::lineSegmentIntersect(const LLVector4a& start, const LLVector4a& end,
                                          S32 face,
                                          bool pick_transparent,
                                          bool pick_rigged,
                                          bool pick_unselectable,
                                          S32* face_hit,
                                          LLVector4a* intersection,
                                          LLVector2* tex_coord,
                                          LLVector4a* normal,
                                          LLVector4a* bi_normal)
{
    LLVector4a dir;
    dir.setSub(end, start);

    F32 closest_t = 2.f;
    bool ret = false;

    for (U32 idx = 0; idx < mViewerPartGroupp->mParticles.size(); ++idx)
    {
        const LLViewerPart &part = *((LLViewerPart*) (mViewerPartGroupp->mParticles[idx]));

        LLVector4a v[4];
        LLStrider<LLVector4a> verticesp;
        verticesp = v;

        getGeometry(part, verticesp);

        F32 a,b,t;
        if (LLTriangleRayIntersect(v[0], v[1], v[2], start, dir, a,b,t) ||
            LLTriangleRayIntersect(v[1], v[3], v[2], start, dir, a,b,t))
        {
            if (t >= 0.f &&
                t <= 1.f &&
                t < closest_t)
            {
                ret = true;
                closest_t = t;
                if (face_hit)
                {
                    *face_hit = idx;
                }

                if (intersection)
                {
                    LLVector4a intersect = dir;
                    intersect.mul(closest_t);
                    intersection->setAdd(intersect, start);
                }
            }
        }
    }

    return ret;
}