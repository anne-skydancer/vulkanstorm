void LLVOPartGroup::getGeometry(const LLViewerPart& part,
                                LLStrider<LLVector4a>& verticesp)
{
    if (part.mFlags & LLPartData::LL_PART_RIBBON_MASK)
    {
        LLVector4a axis, pos, paxis, ppos;
        F32 scale, pscale;

        pos.load3(part.mPosAgent.mV);
        axis.load3(part.mAxis.mV);
        scale = part.mScale.mV[0];

        if (part.mParent)
        {
            ppos.load3(part.mParent->mPosAgent.mV);
            paxis.load3(part.mParent->mAxis.mV);
            pscale = part.mParent->mScale.mV[0];
        }
        else
        { //use source object as position

            if (part.mPartSourcep->mSourceObjectp.notNull())
            {
                LLVector3 v = LLVector3(0,0,1);
                v *= part.mPartSourcep->mSourceObjectp->getRenderRotation();
                paxis.load3(v.mV);
                ppos.load3(part.mPartSourcep->mPosAgent.mV);
                pscale = part.mStartScale.mV[0];
            }
            else
            { //no source object, no parent, nothing to draw
                ppos = pos;
                pscale = scale;
                paxis = axis;
            }
        }

        LLVector4a p0, p1, p2, p3;

        scale *= 0.5f;
        pscale *= 0.5f;

        axis.mul(scale);
        paxis.mul(pscale);

        p0.setAdd(pos, axis);
        p1.setSub(pos,axis);
        p2.setAdd(ppos, paxis);
        p3.setSub(ppos, paxis);

        (*verticesp++) = p2;
        (*verticesp++) = p3;
        (*verticesp++) = p0;
        (*verticesp++) = p1;
    }
    else
    {
        LLVector4a part_pos_agent;
        part_pos_agent.load3(part.mPosAgent.mV);
        LLVector4a camera_agent;
        camera_agent.load3(getCameraPosition().mV);
        LLVector4a at;
        at.setSub(part_pos_agent, camera_agent);
        LLVector4a up(0, 0, 1);
        LLVector4a right;

        right.setCross3(at, up);
        right.normalize3fast();

        up.setCross3(right, at);
        up.normalize3fast();

        if (part.mFlags & LLPartData::LL_PART_FOLLOW_VELOCITY_MASK && !part.mVelocity.isExactlyZero())
        {
            LLVector4a normvel;
            normvel.load3(part.mVelocity.mV);
            normvel.normalize3fast();
            LLVector2 up_fracs;
            up_fracs.mV[0] = normvel.dot3(right).getF32();
            up_fracs.mV[1] = normvel.dot3(up).getF32();
            up_fracs.normalize();
            LLVector4a new_up;
            LLVector4a new_right;

            //new_up = up_fracs.mV[0] * right + up_fracs.mV[1]*up;
            LLVector4a t = right;
            t.mul(up_fracs.mV[0]);
            new_up = up;
            new_up.mul(up_fracs.mV[1]);
            new_up.add(t);

            //new_right = up_fracs.mV[1] * right - up_fracs.mV[0]*up;
            t = right;
            t.mul(up_fracs.mV[1]);
            new_right = up;
            new_right.mul(up_fracs.mV[0]);
            t.sub(new_right);

            up = new_up;
            right = t;
            up.normalize3fast();
            right.normalize3fast();
        }

        right.mul(0.5f*part.mScale.mV[0]);
        up.mul(0.5f*part.mScale.mV[1]);


        //HACK -- the verticesp->mV[3] = 0.f here are to set the texture index to 0 (particles don't use texture batching, maybe they should)
        // this works because there is actually a 4th float stored after the vertex position which is used as a texture index
        // also, somebody please VECTORIZE THIS

        LLVector4a ppapu;
        LLVector4a ppamu;

        ppapu.setAdd(part_pos_agent, up);
        ppamu.setSub(part_pos_agent, up);

        verticesp->setSub(ppapu, right);
        (*verticesp++).getF32ptr()[3] = 0.f;
        verticesp->setSub(ppamu, right);
        (*verticesp++).getF32ptr()[3] = 0.f;
        verticesp->setAdd(ppapu, right);
        (*verticesp++).getF32ptr()[3] = 0.f;
        verticesp->setAdd(ppamu, right);
        (*verticesp++).getF32ptr()[3] = 0.f;
    }
}