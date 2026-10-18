// Frozen original process_chat_from_simulator receive policy. LGPL-2.1.
// Linden Research and Firestorm authors; original getter and branch order.
bool reference(LLChat&chat,std::string&mesg,LLViewerObject*chatter,const LLUUID& owner_id)
{
    const auto from_id=chat.mFromID;
        if ( (rlv_handler_t::isEnabled()) && (CHAT_TYPE_START != chat.mChatType) && (CHAT_TYPE_STOP != chat.mChatType) )
        {
            // NOTE: chatter can be NULL (may not have rezzed yet, or could be another avie's HUD attachment)
            bool is_attachment = (chatter) ? chatter->isAttachment() : false;
            bool is_owned_by_me = (chatter) ? chatter->permYouOwner() : false;

            // Filtering "rules":
            //   avatar  => filter all avie text (unless it's this avie or they're an exemption)
            //   objects => filter everything except attachments this avie owns (never filter llOwnerSay or llRegionSayTo chat)
            if ( ( (CHAT_SOURCE_AGENT == chat.mSourceType) && (from_id != gAgent.getID()) ) ||
                 ( (CHAT_SOURCE_OBJECT == chat.mSourceType) && ((!is_owned_by_me) || (!is_attachment)) &&
                   (CHAT_TYPE_OWNER != chat.mChatType) && (CHAT_TYPE_DIRECT != chat.mChatType) ) )
            {
                bool fIsEmote = RlvUtil::isEmote(mesg);
                static LLCachedControl<bool> RestrainedLoveShowEllipsis(gSavedSettings, "RestrainedLoveShowEllipsis"); // <FS:PP> gSavedSettings to LLCachedControl
                if ((!fIsEmote) &&
                    (((gRlvHandler.hasBehaviour(RLV_BHVR_RECVCHAT)) && (!gRlvHandler.isException(RLV_BHVR_RECVCHAT, from_id))) ||
                     ((gRlvHandler.hasBehaviour(RLV_BHVR_RECVCHATFROM)) && (gRlvHandler.isException(RLV_BHVR_RECVCHATFROM, from_id))) ))
                {
                    // <FS:PP> gSavedSettings to LLCachedControl
                    // if ( (gRlvHandler.filterChat(mesg, false)) && (!gSavedSettings.getBOOL("RestrainedLoveShowEllipsis")) )
                    if ( (gRlvHandler.filterChat(mesg, false)) && (!RestrainedLoveShowEllipsis) )
                    // </FS:PP>
                        return false;
                }
                else if ((fIsEmote) &&
                         (((gRlvHandler.hasBehaviour(RLV_BHVR_RECVEMOTE)) && (!gRlvHandler.isException(RLV_BHVR_RECVEMOTE, from_id))) ||
                          ((gRlvHandler.hasBehaviour(RLV_BHVR_RECVEMOTEFROM)) && (gRlvHandler.isException(RLV_BHVR_RECVEMOTEFROM, from_id))) ))
                {
                    // <FS:PP> gSavedSettings to LLCachedControl
                    // if (!gSavedSettings.getBOOL("RestrainedLoveShowEllipsis"))
                    if (!RestrainedLoveShowEllipsis)
                    // </FS:PP>
                        return false;
                    mesg = "/me ...";
                }
            }

            // Filtering "rules":
            //   avatar => filter only their name (unless it's this avie)
            //   other  => filter everything
            if (!RlvActions::canShowName(RlvActions::SNC_DEFAULT))
            {
                if (CHAT_SOURCE_AGENT != chat.mSourceType)
                {
                    if (chat.mChatType != CHAT_TYPE_RADAR) // Radar messages are already correct anonymized
                    {
                        RlvUtil::filterNames(chat.mFromName);
                    }
                }
                else if (!RlvActions::canShowName(RlvActions::SNC_DEFAULT, chat.mFromID))
                {
                    LLAvatarName av_name;
                    if (LLAvatarNameCache::get(chat.mFromID, &av_name))
                    {
                        chat.mFromName = RlvStrings::getAnonym(av_name);
                    }
                    else
                    {
                        chat.mFromName = RlvStrings::getAnonym(chat.mFromName);
                    }
                    chat.mRlvNamesFiltered = true;
                }
            }

            // Create an "objectim" URL for objects if we're either @shownames or @showloc restricted
            // (we need to do this now because we won't be have enough information to do it later on)
            if ( (CHAT_SOURCE_OBJECT == chat.mSourceType) &&
                 ( (!RlvActions::canShowName(RlvActions::SNC_DEFAULT)) || (!RlvActions::canShowLocation()) ) )
            {
                LLSD sdQuery;
                sdQuery["name"] = chat.mFromName;
                sdQuery["owner"] = owner_id;

                if ( (!RlvActions::canShowName(RlvActions::SNC_DEFAULT, owner_id)) && (!is_owned_by_me) )
                    sdQuery["rlv_shownames"] = true;

                const LLViewerRegion* pRegion = LLWorld::getInstance()->getRegionFromPosAgent(chat.mPosAgent);
                if (pRegion)
                    sdQuery["slurl"] = LLSLURL(pRegion->getName(), chat.mPosAgent).getLocationString();

                chat.mURL = LLSLURL("objectim", from_id, LLURI::mapToQueryString(sdQuery)).getSLURLString();
            }
        }
    return true;
}
