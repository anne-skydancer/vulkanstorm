void LLViewerPartSourceChat::updatePart(LLViewerPart &part, const F32 dt)
{
    F32 frac = part.mLastUpdateTime/part.mMaxAge;

    LLVector3 center_pos;
    LLViewerPartSource *ps = (LLViewerPartSource*)part.mPartSourcep;
    LLViewerPartSourceChat *pss = (LLViewerPartSourceChat *)ps;
    if (pss->sourcePosition(part.mPosAgent))
    {
    }
    else
    {
        part.mPosAgent = pss->mPosAgent;
    }
    F32 x = sin(F_TWO_PI*frac + part.mParameter);
    F32 y = cos(F_TWO_PI*frac + part.mParameter);

    part.mPosAgent.mV[VX] += x;
    part.mPosAgent.mV[VY] += y;
    part.mPosAgent.mV[VZ] += -0.5f + frac;
}
void LLViewerPartSourceBeam::updatePart(LLViewerPart &part, const F32 dt)
{
    F32 frac = part.mLastUpdateTime/part.mMaxAge;

    LLViewerPartSource *ps = (LLViewerPartSource*)part.mPartSourcep;
    LLViewerPartSourceBeam *psb = (LLViewerPartSourceBeam *)ps;
    if (psb->mSourceObjectp.isNull()
#if VS_NATIVE_VULKAN
        && !psb->mNativePosition
#endif
       )
    {
        part.mFlags = LLPartData::LL_PART_DEAD_MASK;
        return;
    }

    LLVector3 source_pos_agent;
    LLVector3 target_pos_agent;
    bool hasTarget=false;
#if VS_NATIVE_VULKAN
    if(psb->mNativePosition)
    {
        if(!psb->sourcePosition(source_pos_agent)){part.mFlags=LLPartData::LL_PART_DEAD_MASK;return;}
        const auto target=psb->mNativeTarget?psb->mNativeTarget():std::nullopt;
        hasTarget=bool(target);if(target)target_pos_agent=*target;
    }
    else
#endif
    {
    if (!psb->mSourceObjectp.isNull() && !psb->mSourceObjectp->mDrawable.isNull())
    {
        if (psb->mSourceObjectp->isAvatar())
        {
            LLViewerObject *objp = psb->mSourceObjectp;
            LLVOAvatar *avp = (LLVOAvatar *)objp;
            source_pos_agent = avp->mWristLeftp->getWorldPosition();
        }
        else
        {
            source_pos_agent = psb->mSourceObjectp->getRenderPosition();
        }
    }
    if (!psb->mTargetObjectp.isNull() && !psb->mTargetObjectp->mDrawable.isNull())
    {
        target_pos_agent = psb->mTargetObjectp->getRenderPosition();
    }

        hasTarget=psb->mTargetObjectp.notNull();
    }
    part.mPosAgent = (1.f - frac) * source_pos_agent;
    if (!hasTarget)
    {
        part.mPosAgent += frac * (gAgent.getPosAgentFromGlobal(psb->mLKGTargetPosGlobal));
    }
    else
    {
        part.mPosAgent += frac * target_pos_agent;
    }
}