// llinventory_offer_oracle_guards.h: NOT COMPILED (never included).
// Recorder R4b: live code the frozen copies depend on, frozen as text so
// that check_r4b_copies.py fails when it changes. Same markers as the copies.
#error this file is text for check_r4b_copies.py and is not compiled

// R4B-COPY-BEGIN guard_process_improved_im
// source: indra/newview/llviewermessage.cpp
// function: process_improved_im (U8[MTUBYTES] bucket read, size, sender host)
// lines: 2499-2568
// blob: d7c6222cdb750cf1aadac78508a935a7d456293f
void process_improved_im(LLMessageSystem *msg, void **user_data)
{
    LL_PROFILE_ZONE_SCOPED;

    LLUUID from_id;
    bool from_group;
    LLUUID to_id;
    U8 offline;
    U8 d = 0;
    LLUUID session_id;
    U32 timestamp;
    std::string agentName;
    std::string message;
    U32 parent_estate_id = 0;
    LLUUID region_id;
    LLVector3 position;
    U8 binary_bucket[MTUBYTES];
    S32 binary_bucket_size;

    // *TODO: Translate - need to fix the full name to first/last (maybe)
    msg->getUUIDFast(_PREHASH_AgentData, _PREHASH_AgentID, from_id);
    msg->getBOOLFast(_PREHASH_MessageBlock, _PREHASH_FromGroup, from_group);
    msg->getUUIDFast(_PREHASH_MessageBlock, _PREHASH_ToAgentID, to_id);
    msg->getU8Fast(_PREHASH_MessageBlock, _PREHASH_Offline, offline);
    msg->getU8Fast(_PREHASH_MessageBlock, _PREHASH_Dialog, d);
    msg->getUUIDFast(_PREHASH_MessageBlock, _PREHASH_ID, session_id);
    msg->getU32Fast(_PREHASH_MessageBlock, _PREHASH_Timestamp, timestamp);
    //msg->getData("MessageBlock", "Count",     &count);
    msg->getStringFast(_PREHASH_MessageBlock, _PREHASH_FromAgentName, agentName);
    msg->getStringFast(_PREHASH_MessageBlock, _PREHASH_Message, message);
    msg->getU32Fast(_PREHASH_MessageBlock, _PREHASH_ParentEstateID, parent_estate_id);
    msg->getUUIDFast(_PREHASH_MessageBlock, _PREHASH_RegionID, region_id);
    msg->getVector3Fast(_PREHASH_MessageBlock, _PREHASH_Position, position);
    msg->getBinaryDataFast(_PREHASH_MessageBlock, _PREHASH_BinaryBucket, binary_bucket, 0, 0, MTUBYTES);
    binary_bucket_size = msg->getSizeFast(_PREHASH_MessageBlock, _PREHASH_BinaryBucket);
    EInstantMessage dialog = (EInstantMessage)d;
    LLHost sender = msg->getSender();

    LLSD metadata;
    if (msg->getNumberOfBlocksFast(_PREHASH_MetaData) > 0)
    {
        S32 metadata_size = msg->getSizeFast(_PREHASH_MetaData, 0, _PREHASH_Data);
        std::string metadata_buffer;
        metadata_buffer.resize(metadata_size, 0);

        msg->getBinaryDataFast(_PREHASH_MetaData, _PREHASH_Data, &metadata_buffer[0], metadata_size, 0, metadata_size );
        std::stringstream metadata_stream(metadata_buffer);
        if (LLSDSerialize::fromBinary(metadata, metadata_stream, metadata_size) == LLSDParser::PARSE_FAILURE)
        {
            metadata.clear();
        }
    }

    LLIMProcessing::processNewMessage(from_id,
        from_group,
        to_id,
        offline,
        dialog,
        session_id,
        timestamp,
        agentName,
        message,
        parent_estate_id,
        region_id,
        position,
        binary_bucket,
        binary_bucket_size,
        sender,
        metadata);
}
// R4B-COPY-END guard_process_improved_im

// R4B-COPY-BEGIN guard_process_new_message_preamble
// source: indra/newview/llimprocessing.cpp
// function: LLIMProcessing::processNewMessage up to the dialog switch (antispam, name, mute, Omnifilter)
// lines: 703-938
// blob: 8fa885d340173cce97bd512894040370f970b57a
void LLIMProcessing::processNewMessage(LLUUID from_id,
    bool from_group,
    LLUUID to_id,
    U8 offline,
    EInstantMessage dialog, // U8
    LLUUID session_id,
    U32 timestamp,
    std::string agentName,
    std::string message,
    U32 parent_estate_id,
    LLUUID region_id,
    LLVector3 position,
    U8 *binary_bucket,
    S32 binary_bucket_size,
    LLHost &sender,
    LLSD metadata,
    LLUUID aux_id)
{
    LLChat chat;
    std::string buffer;
    std::string name = agentName;

    // NaCl - Antispam Registry
    if (dialog != IM_TYPING_START && dialog != IM_TYPING_STOP &&                                            // Typing notifications
        !(dialog == IM_NOTHING_SPECIAL && offline == IM_OFFLINE && from_id.notNull() && to_id.notNull()) && // Saved offline IMs
        !(dialog == IM_FROM_TASK && offline == IM_OFFLINE)                                                  // Saved offline IMs from objects
        )
    {
        if (NACLAntiSpamRegistry::instance().checkQueue(ANTISPAM_QUEUE_IM, from_id, ANTISPAM_SOURCE_AGENT))
        {
            return;
        }
    }
    // NaCl End

    // make sure that we don't have an empty or all-whitespace name
    LLStringUtil::trim(name);
    if (name.empty())
    {
        name = LLTrans::getString("Unnamed");
    }

    // Preserve the unaltered name for use in group notice mute checking.
    std::string original_name = name;

    // IDEVO convert new-style "Resident" names for display
    name = clean_name_from_im(name, dialog);

    bool is_do_not_disturb = gAgent.isDoNotDisturb();
    bool is_autorespond = gAgent.getAutorespond();
    bool is_autorespond_nonfriends = gAgent.getAutorespondNonFriends();
    // <FS:PP> FIRE-1245: Option to block/reject teleport offers
    bool is_rejecting_tp_offers = gAgent.getRejectTeleportOffers();
    static LLCachedControl<bool> FSDontRejectTeleportOffersFromFriends(gSavedPerAccountSettings, "FSDontRejectTeleportOffersFromFriends");
    // </FS:PP>
    bool is_rejecting_group_invites = gAgent.getRejectAllGroupInvites(); // <FS:PP> Option to block/reject all group invites
    bool is_rejecting_friendship_requests = gAgent.getRejectFriendshipRequests(); // <FS:PP> FIRE-15233: Automatic friendship request refusal
    bool is_autorespond_muted = gSavedPerAccountSettings.getBOOL("FSSendMutedAvatarResponse");
    bool is_muted = LLMuteList::getInstance()->isMuted(from_id, name, LLMute::flagTextChat)
        // object IMs contain sender object id in session_id (STORM-1209)
        || (dialog == IM_FROM_TASK && LLMuteList::getInstance()->isMuted(session_id));
    bool is_owned_by_me = false;
    bool is_friend = LLAvatarTracker::instance().getBuddyInfo(from_id) != NULL;
    static LLCachedControl<bool> accept_im_from_only_friend(gSavedPerAccountSettings, "VoiceCallsFriendsOnly");
    //bool is_linden = chat.mSourceType != CHAT_SOURCE_OBJECT &&
    //      LLMuteList::isLinden(name); <:FS:TM> Bear compile fix - is_linden not referenced

    // <FS:PP> FIRE-10500: Autoresponse for (Away)
    static LLCachedControl<bool> FSSendAwayAvatarResponse(gSavedPerAccountSettings, "FSSendAwayAvatarResponse");
    bool is_afk = gAgent.getAFK();
    // </FS:PP>

    /***
    * The simulator may have flagged this sender as a bot, if the viewer would like to display
    * the chat text in a different color or font, the below code is how the viewer can
    * tell if the sender is a bot.
    *-----------------------------------------------------
    bool is_bot = false;
    if (metadata.has("sender"))
    {   // The server has identified this sender as a bot.
        is_bot = metadata["sender"]["bot"].asBoolean();
    }
    *-----------------------------------------------------
    */

    // <FS:Zi> Omnifilter support
    static LLCachedControl<bool> use_omnifilter(gSavedSettings, "OmnifilterEnabled", false);
    if (use_omnifilter)
    {
        OmnifilterEngine::Haystack haystack;
        haystack.mContent = message;
        haystack.mSenderName = agentName;
        haystack.mOwnerID = from_id;

        switch (dialog)
        {
        case IM_NOTHING_SPECIAL:        // this is the type for regular IMs
        {
            haystack.mType = OmnifilterEngine::eType::InstantMessage;
            break;
        }
        case IM_SESSION_SEND:           // this is the type for regular group IMs
        case IM_SESSION_INVITE:         // this is the type for new group IM sessions - see LLIMMgr::addMessage()
        case IM_DO_NOT_DISTURB_AUTO_RESPONSE:
        {
            haystack.mType = OmnifilterEngine::eType::GroupChat;
            break;
        }
        case IM_FROM_TASK:
        {
            // haystack.mSenderName = agentName;    // Object name
            // haystack.mOwnerID = from_id;         // object owner UUID
            // haystack.mContent = message;         // IM content
            haystack.mType = OmnifilterEngine::eType::ObjectInstantMessage;
            break;
        }
        case IM_GROUP_INVITATION:
        {
            // haystack.mSenderName = agentName;    // Inviter "first.last"
            // haystack.mOwnerID = from_id;         // group ID
            haystack.mType = OmnifilterEngine::eType::GroupInvite;
            break;
        }
        case IM_INVENTORY_OFFERED:
        {
            // haystack.mSenderName = agentName;    // Sender "First Last"
            // haystack.mOwnerID = from_id;         // Sender UUID
            // haystack.mContent = message;         // item/folder name
            haystack.mType = OmnifilterEngine::eType::InventoryOffer;
            break;
        }
        case IM_FRIENDSHIP_OFFERED:
        {
            // haystack.mSenderName = agentName;    // Sender "First Last"
            // haystack.mOwnerID = from_id;         // Sender UUID
            // haystack.mContent = message;         // friendship message
            haystack.mType = OmnifilterEngine::eType::FriendshipOffer;
            break;
        }
        case IM_LURE_USER:
        {
            // haystack.mSenderName = agentName;    // Sender "First Last"
            // haystack.mOwnerID = from_id;         // Sender UUID
            // haystack.mContent = message;         // teleport message
            haystack.mType = OmnifilterEngine::eType::Lure;
            break;
        }
        case IM_TELEPORT_REQUEST:
        {
            // haystack.mSenderName = agentName;    // Sender "First Last"
            // haystack.mOwnerID = from_id;         // Sender UUID
            // haystack.mContent = message;         // teleport message
            haystack.mType = OmnifilterEngine::eType::TeleportRequest;
            break;
        }
        case IM_GROUP_NOTICE:
        {
            // haystack.mSenderName = agentName;    // Notice Sender "First Last"
            // haystack.mOwnerID = from_id;         // Notice Sender UUID
            // haystack.mContent = message;         // notice message
            haystack.mType = OmnifilterEngine::eType::GroupNotice;
            break;
        }
        default:
        {
            LL_DEBUGS("Omnifilter") << "unhandled IM type " << (U32)dialog << LL_ENDL;
            break;
        }
        }

        const OmnifilterEngine::Needle* needle = OmnifilterEngine::getInstance()->match(haystack);
        if (needle)
        {
            if (needle->mChatReplace.empty())       // no replacement defined, just suppress the message
            {
                return;
            }

            LLSD args;
            args["REPLACEMENT"] = needle->mChatReplace;
            message = LLTrans::getString("OmnifilterReplacement", args);
        }
    }
    // </FS:Zi>

    std::string notice_name;
    LLSD notice_args;
    if (metadata.has("notice"))
    {   // The server has injected a notice into the IM conversation.
        // These will be things like bot notifications, etc.
        notice_name = metadata["notice"]["id"].asString();
        notice_args = metadata["notice"]["data"];
    }

    chat.mMuted = is_muted;
    chat.mFromID = from_id;
    chat.mFromName = name;
    chat.mSourceType = (from_id.isNull() || (name == std::string(SYSTEM_FROM))) ? CHAT_SOURCE_SYSTEM : CHAT_SOURCE_AGENT;

    if (chat.mSourceType == CHAT_SOURCE_SYSTEM)
    { // Translate server message if required (MAINT-6109)
        translate_if_needed(message);
    }

    LLViewerObject *source = gObjectList.findObject(session_id); //Session ID is probably the wrong thing.
    if (source)
    {
        is_owned_by_me = source->permYouOwner();
    }

    // NaCl - Newline flood protection
    static LLCachedControl<bool> useAntiSpam(gSavedSettings, "UseAntiSpam");
    if (useAntiSpam && dialog != IM_GROUP_INVITATION)
    {
        bool doCheck = true;
        if (from_id.isNull() || gAgentID == from_id)
        {
            doCheck = false;
        }
        if (doCheck && is_owned_by_me)
        {
            doCheck = false;
        }
        if (doCheck && NACLAntiSpamRegistry::instance().checkNewlineFlood(ANTISPAM_QUEUE_IM, from_id, message))
        {
            return;
        }
    }
    // NaCl End

    std::string separator_string(": ");

    LLSD args;
    LLSD payload;
    LLNotification::Params params;

// R4B-COPY-END guard_process_new_message_preamble

// R4B-COPY-BEGIN guard_offline_coro
// source: indra/newview/llimprocessing.cpp
// function: LLIMProcessing::requestOfflineMessagesCoro (before the message loop)
// lines: 2438-2501
// blob: 8fa885d340173cce97bd512894040370f970b57a
void LLIMProcessing::requestOfflineMessagesCoro(std::string url)
{
    LLCore::HttpRequest::policy_t httpPolicy(LLCore::HttpRequest::DEFAULT_POLICY_ID);
    LLCoreHttpUtil::HttpCoroutineAdapter::ptr_t
        httpAdapter = std::make_shared<LLCoreHttpUtil::HttpCoroutineAdapter>("requestOfflineMessagesCoro", httpPolicy);
    LLCore::HttpRequest::ptr_t httpRequest = std::make_shared<LLCore::HttpRequest>();

    LLSD result = httpAdapter->getAndSuspend(httpRequest, url);

    LLSD httpResults = result[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS];
    LLCore::HttpStatus status = LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(httpResults);

    if (!status) // success = httpResults["success"].asBoolean();
    {
        LL_WARNS("Messaging") << "Error requesting offline messages via capability " << url << ", Status: " << status.toString() << "\nFalling back to legacy method." << LL_ENDL;

        requestOfflineMessagesLegacy();
        return;
    }

    LLSD contents = result[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS_CONTENT];

    if (!contents.size())
    {
        LL_WARNS("Messaging") << "No contents received for offline messages via capability " << url << LL_ENDL;
        return;
    }

    // Todo: once dirtsim-369 releases, remove one of the map/array options
    LLSD messages;
    if (contents.isArray())
    {
        messages = *contents.beginArray();
    }
    else if (contents.has("messages"))
    {
        messages = contents["messages"];
    }
    else
    {
        LL_WARNS("Messaging") << "Invalid offline message content received via capability " << url << LL_ENDL;
        return;
    }

    if (!messages.isArray())
    {
        LL_WARNS("Messaging") << "Invalid offline message content received via capability " << url << LL_ENDL;
        return;
    }

    if (messages.size() == 0)
    {
        // Nothing to process
        return;
    }

    if (!gAgent.getRegion())
    {
        LL_WARNS("Messaging") << "Region null while attempting to load messages." << LL_ENDL;
        return;
    }

    LL_INFOS("Messaging") << "Processing offline messages." << LL_ENDL;

// R4B-COPY-END guard_offline_coro

// R4B-COPY-BEGIN guard_mute_add
// source: indra/newview/llmutelist.cpp
// function: LLMuteList::add
// lines: 344-477
// blob: 3247ec020ecc1929c21e9372767759ad8bdc4d71
bool LLMuteList::add(const LLMute& mute, U32 flags)
{
    // Can't mute text from Lindens
    if ((mute.mType == LLMute::AGENT)
        && isLinden(mute.mName) && (flags & LLMute::flagTextChat || flags == 0))
    {
        LL_WARNS() << "Trying to mute a Linden; ignored" << LL_ENDL;
        LLNotifications::instance().add("MuteLinden", LLSD(), LLSD());
        return false;
    }

    // Can't mute self.
    if (mute.mType == LLMute::AGENT
        && mute.mID == gAgent.getID())
    {
        LL_WARNS() << "Trying to self; ignored" << LL_ENDL;
        return false;
    }

    static LLCachedControl<S32> mute_list_limit(gSavedSettings, "MuteListLimit", 1000);
    if (getMutes().size() >= mute_list_limit)
    {
        LL_WARNS() << "Mute limit is reached; ignored" << LL_ENDL;
        LLSD args;
        args["MUTE_LIMIT"] = mute_list_limit;
        LLNotifications::instance().add(LLNotification::Params("MuteLimitReached").substitutions(args));
        return false;
    }

    if (mute.mType == LLMute::BY_NAME)
    {
        // Can't mute empty string by name
        if (mute.mName.empty())
        {
            LL_WARNS() << "Trying to mute empty string by-name" << LL_ENDL;
            return false;
        }

        // Null mutes must have uuid null
        if (mute.mID.notNull())
        {
            LL_WARNS() << "Trying to add by-name mute with non-null id" << LL_ENDL;
            return false;
        }

        std::pair<string_set_t::iterator, bool> result = mLegacyMutes.insert(mute.mName);
        if (result.second)
        {
            LL_INFOS() << "Muting by name " << mute.mName << LL_ENDL;
            updateAdd(mute);
            notifyObservers();
            notifyObserversDetailed(mute);
            return true;
        }
        else
        {
            LL_INFOS() << "duplicate mute ignored" << LL_ENDL;
            // was duplicate
            return false;
        }
    }
    else
    {
        // <FS:Ansariel> FIRE-15746: Show block report in nearby chat
        bool show_message = false;

        // Need a local (non-const) copy to set up flags properly.
        LLMute localmute = mute;

        // If an entry for the same entity is already in the list, remove it, saving flags as necessary.
        mute_set_t::iterator it = mMutes.find(localmute);
        if (it != mMutes.end())
        {
            // This mute is already in the list.  Save the existing entry's flags if that's warranted.
            localmute.mFlags = it->mFlags;

            mMutes.erase(it);
            // Don't need to call notifyObservers() here, since it will happen after the entry has been re-added below.

            // <FS:Ansariel> FIRE-15746: Show block report in nearby chat
            show_message = false;
        }
        else
        {
            // There was no entry in the list previously.  Fake things up by making it look like the previous entry had all properties unmuted.
            localmute.mFlags = LLMute::flagAll;

            // <FS:Ansariel> FIRE-15746: Show block report in nearby chat
            show_message = true;
        }

        if(flags)
        {
            // The user passed some combination of flags.  Make sure those flag bits are turned off (i.e. those properties will be muted).
            localmute.mFlags &= (~flags);
        }
        else
        {
            // The user passed 0.  Make sure all flag bits are turned off (i.e. all properties will be muted).
            localmute.mFlags = 0;
        }

        // (re)add the mute entry.
        {
            std::pair<mute_set_t::iterator, bool> result = mMutes.insert(localmute);
            if (result.second)
            {
                LL_INFOS() << "Muting " << localmute.mName << " id " << localmute.mID << " flags " << localmute.mFlags << LL_ENDL;
                // <FS:Ansariel> FIRE-15746: Show block report in nearby chat
                //updateAdd(localmute);
                updateAdd(localmute, show_message);
                // </FS:Ansariel>
                notifyObservers();
                notifyObserversDetailed(localmute);

                //mute local lights that are attached to the avatar
                LLVOAvatar *avatarp = find_avatar(localmute.mID);
                if (avatarp)
                {
                    LLPipeline::removeMutedAVsLights(avatarp);
                }
                //remove agent's notifications as well
                if (localmute.mType == LLMute::AGENT)
                {
                    LLNotifications::instance().cancelByOwner(localmute.mID);
                }
                return true;
            }
        }
    }

    // If we were going to return success, we'd have done it by now.
    return false;
}
// R4B-COPY-END guard_mute_add

// R4B-COPY-BEGIN guard_mute_remove
// source: indra/newview/llmutelist.cpp
// function: LLMuteList::remove
// lines: 523-603
// blob: 3247ec020ecc1929c21e9372767759ad8bdc4d71
bool LLMuteList::remove(const LLMute& mute, U32 flags)
{
    bool found = false;

    // First, remove from main list.
    mute_set_t::iterator it = mMutes.find(mute);
    if (it != mMutes.end())
    {
        LLMute localmute = *it;
        bool remove = true;
        if(flags)
        {
            // If the user passed mute flags, we may only want to turn some flags on.
            localmute.mFlags |= flags;

            if(localmute.mFlags == LLMute::flagAll)
            {
                // Every currently available mute property has been masked out.
                // Remove the mute entry entirely.
            }
            else
            {
                // Only some of the properties are masked out.  Update the entry.
                remove = false;
            }
        }
        else
        {
            // The caller didn't pass any flags -- just remove the mute entry entirely.
            // set flags to notify observers with (flag being present means that something is allowed)
            localmute.mFlags = LLMute::flagAll;
        }

        // Always remove the entry from the set -- it will be re-added with new flags if necessary.
        mMutes.erase(it);

        if(remove)
        {
            // The entry was actually removed.  Notify the server.
            updateRemove(localmute);
            LL_INFOS() << "Unmuting " << localmute.mName << " id " << localmute.mID << " flags " << localmute.mFlags << LL_ENDL;
        }
        else
        {
            // Flags were updated, the mute entry needs to be retransmitted to the server and re-added to the list.
            mMutes.insert(localmute);
            // <FS:Ansariel> FIRE-15746: Show block report in nearby chat
            ////updateAdd(localmute);
            updateAdd(localmute, false);
            // </FS:Ansariel>
            LL_INFOS() << "Updating mute entry " << localmute.mName << " id " << localmute.mID << " flags " << localmute.mFlags << LL_ENDL;
        }

        // Must be after erase.
        notifyObservers();
        notifyObserversDetailed(localmute);

        // <FS:Ansariel> Return correct return value
        found = true;
    }
    else
    {
        // Clean up any legacy mutes
        string_set_t::iterator legacy_it = mLegacyMutes.find(mute.mName);
        if (legacy_it != mLegacyMutes.end())
        {
            // Database representation of legacy mute is UUID null.
            LLMute mute(LLUUID::null, *legacy_it, LLMute::BY_NAME);
            updateRemove(mute);
            mLegacyMutes.erase(legacy_it);
            // Must be after erase.
            notifyObservers();
            notifyObserversDetailed(mute);

            // <FS:Ansariel> Return correct return value
            found = true;
        }
    }

    return found;
}
// R4B-COPY-END guard_mute_remove

