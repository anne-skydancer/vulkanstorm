// Frozen actual original Area Search policy/protocol witness before native source transposition. LGPL-2.1.
bool FSAreaSearch::isSearchableObject(LLViewerObject* objectp, LLViewerRegion* our_region) const
{
    // need to be connected to region object is in.
    if (!objectp->getRegion())
    {
        return false;
    }

    // Land doesn't have object properties
    if (objectp->getPCode() == LLViewerObject::LL_VO_SURFACE_PATCH)
    {
        return false;
    }

    // Object needs to be selectable to get properties from the server
    if (!objectp->mbCanSelect)
    {
        return false;
    }

    // Avatars are spechiel objects that don't have normal properties
    if (objectp->isAvatar())
    {
        return false;
    }

    //-----------------------------------------------------------------------
    // Excludes
    //-----------------------------------------------------------------------

    if (mExcludeChildPrims && !(objectp->isRoot() || (objectp->isAttachment() && objectp->isRootEdit())))
    {
        return false;
    }

    if (mExcludeNeighborRegions && !(objectp->getRegion() == our_region))
    {
        return false;
    }

    if (mExcludeAttachment && objectp->isAttachment())
    {
        return false;
    }

    if (mExcludePhysics && objectp->flagUsePhysics())
    {
        return false;
    }

    if (mExcludeTemporary && objectp->flagTemporaryOnRez())
    {
        return false;
    }

    if (mExcludeReflectionProbe && objectp->mReflectionProbe.notNull())
    {
        return false;
    }

    return true;
}
void FSAreaSearch::requestObjectProperties(const std::vector<U32>& request_list, bool select, LLViewerRegion* regionp)
{
    bool start_new_message = true;
    S32 select_count = 0;
    LLMessageSystem* msg = gMessageSystem;

    for (const auto& id : request_list)
    {
        if (start_new_message)
        {
            if (select)
            {
                msg->newMessageFast(_PREHASH_ObjectSelect);
            }
            else
            {
                msg->newMessageFast(_PREHASH_ObjectDeselect);
            }
            msg->nextBlockFast(_PREHASH_AgentData);
            msg->addUUIDFast(_PREHASH_AgentID, gAgentID);
            msg->addUUIDFast(_PREHASH_SessionID, gAgentSessionID);
            select_count++;
            start_new_message = false;
        }

        msg->nextBlockFast(_PREHASH_ObjectData);
        msg->addU32Fast(_PREHASH_ObjectLocalID, id);
        select_count++;

        if (msg->isSendFull(NULL) || select_count >= MAX_OBJECTS_PER_PACKET)
        {
            LL_DEBUGS("FSAreaSearch") << "Sent one full " << (select ? "ObjectSelect" : "ObjectDeselect") << " message with " << select_count << " object data blocks." << LL_ENDL;
            msg->sendReliable(regionp->getHost());
            select_count = 0;
            start_new_message = true;
        }
    }

    if (!start_new_message)
    {
        LL_DEBUGS("FSAreaSearch") << "Sent one partcial " << (select ? "ObjectSelect" : "ObjectDeselect") << " message with " << select_count << " object data blocks." << LL_ENDL;
        msg->sendReliable(regionp->getHost());
    }
}
