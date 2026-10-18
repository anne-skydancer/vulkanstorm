// Frozen original viewer algorithm; LGPL-2.1, like the viewer.
LLVector2 LLFace::surfaceToTexture(LLVector2 surface_coord, const LLVector4a& position, const LLVector4a& normal)
{
    LLVector2 tc = surface_coord;

    const LLTextureEntry *tep = getTextureEntry();

    if (tep == NULL)
    {
        // can't do much without the texture entry
        return surface_coord;
    }

    //VECTORIZE THIS
    // see if we have a non-default mapping
    U8 texgen = tep->getTexGen();
    if (texgen != LLTextureEntry::TEX_GEN_DEFAULT)
    {
        LLVector4a& center = *(mDrawablep->getVOVolume()->getVolume()->getVolumeFace(mTEOffset).mCenter);

        LLVector4a volume_position;
        LLVector3 v_position(position.getF32ptr());

        volume_position.load3(mDrawablep->getVOVolume()->agentPositionToVolume(v_position).mV);

        if (!mDrawablep->getVOVolume()->isVolumeGlobal())
        {
            LLVector4a scale;
            scale.load3(mVObjp->getScale().mV);
            volume_position.mul(scale);
        }

        LLVector4a volume_normal;
        LLVector3 v_normal(normal.getF32ptr());
        volume_normal.load3(mDrawablep->getVOVolume()->agentDirectionToVolume(v_normal).mV);
        volume_normal.normalize3fast();

        if (texgen == LLTextureEntry::TEX_GEN_PLANAR)
        {
            planarProjection(tc, volume_normal, center, volume_position);
        }
    }

    if (mTextureMatrix) // if we have a texture matrix, use it
    {
        LLVector3 tc3(tc);
        tc3 = tc3 * *mTextureMatrix;
        tc = LLVector2(tc3);
    }

    else // otherwise use the texture entry parameters
    {
        xform(tc, cos(tep->getRotation()), sin(tep->getRotation()),
              tep->mOffsetS, tep->mOffsetT, tep->mScaleS, tep->mScaleT);
    }


    return tc;
}
