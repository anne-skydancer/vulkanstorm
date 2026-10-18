template<class Part>float referenceScale(const Part& value)
{const Part*part=&value;float max_scale=0;
        //remember the largest particle
        max_scale = llmax(max_scale, part->mScale.mV[0], part->mScale.mV[1]);

        if (part->mFlags & LLPartData::LL_PART_RIBBON_MASK)
        { //include ribbon segment length in scale
            const LLVector3* pos_agent = NULL;
            if (part->mParent)
            {
                pos_agent = &(part->mParent->mPosAgent);
            }
            else if (part->mPartSourcep.notNull())
            {
                pos_agent = &(part->mPartSourcep->mPosAgent);
            }

            if (pos_agent)
            {
                F32 dist = (*pos_agent-part->mPosAgent).length();

                max_scale = llmax(max_scale, dist);
            }
        }

return max_scale;}
