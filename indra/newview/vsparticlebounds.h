// Original LLVOPartGroup drawable extent scale. LGPL-2.1.
#pragma once
#include "llpartdata.h"
template<class Part>F32 vs_particle_scale(const Part& part)
{
        //remember the largest particle
        F32 max_scale=llmax(part.mScale.mV[0],part.mScale.mV[1]);

        if (part.mFlags & LLPartData::LL_PART_RIBBON_MASK)
        { //include ribbon segment length in scale
            const LLVector3* pos_agent = NULL;
            if (part.mParent)
            {
                pos_agent = &(part.mParent->mPosAgent);
            }
            else if (part.mPartSourcep.notNull())
            {
                pos_agent = &(part.mPartSourcep->mPosAgent);
            }

            if (pos_agent)
            {
                F32 dist = (*pos_agent-part.mPosAgent).length();

                max_scale = llmax(max_scale, dist);
            }
        }

    return max_scale;
}
