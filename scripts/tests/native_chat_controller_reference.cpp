// Frozen OpenGL receive controller before renderer-peer extraction. LGPL-2.1.
void process_chat_from_simulator(LLMessageSystem *msg, void **user_data)
{
    if (gNonInteractive)
    {
        return;
    }
    LLChat  chat;
    std::string     mesg;
    std::string     from_name;
    U8          source_temp;
    U8          type_temp;
    U8          audible_temp;
    LLColor4    color(1.0f, 1.0f, 1.0f, 1.0f);
    LLUUID      from_id;
    LLUUID      owner_id;
    LLViewerObject* chatter;

    msg->getString("ChatData", "FromName", from_name);

    msg->getUUID("ChatData", "SourceID", from_id);
    chat.mFromID = from_id;

    // Object owner for objects
    msg->getUUID("ChatData", "OwnerID", owner_id);

    msg->getU8Fast(_PREHASH_ChatData, _PREHASH_SourceType, source_temp);
    chat.mSourceType = (EChatSourceType)source_temp;

    msg->getU8("ChatData", "ChatType", type_temp);
    chat.mChatType = (EChatType)type_temp;

    // NaCL - Antispam Registry
    static LLCachedControl<bool> useAntiSpam(gSavedSettings, "UseAntiSpam");
    static LLCachedControl<bool> useAntiSpamMine(gSavedSettings, "FSUseAntiSpamMine");
    bool deferred_spam_check = false;
    if (useAntiSpam && chat.mChatType != CHAT_TYPE_START && chat.mChatType != CHAT_TYPE_STOP)
    {
        if (chat.mSourceType == CHAT_SOURCE_OBJECT)
        {
            LLViewerObject* source = gObjectList.findObject(from_id);
            if (source)
            {
                if (source->permYouOwner() && useAntiSpamMine)
                {
                    // Only check if not a debug message
                    deferred_spam_check = chat.mChatType != CHAT_TYPE_DEBUG_MSG;
                }
                else if (!source->permYouOwner() && chat.mChatType != CHAT_TYPE_DEBUG_MSG && NACLAntiSpamRegistry::instance().checkQueue(ANTISPAM_QUEUE_CHAT, from_id, ANTISPAM_SOURCE_OBJECT))
                {
                    return;
                }
            }
        }
        // owner_id = from_id for agents
        else if (NACLAntiSpamRegistry::instance().checkQueue(ANTISPAM_QUEUE_CHAT, owner_id, ANTISPAM_SOURCE_AGENT))
        {
            return;
        }
    }
    // NaCl End

    msg->getU8Fast(_PREHASH_ChatData, _PREHASH_Audible, audible_temp);
    chat.mAudible = (EChatAudible)audible_temp;

    chat.mTime = LLFrameTimer::getElapsedSeconds();

    // IDEVO Correct for new-style "Resident" names
    if (chat.mSourceType == CHAT_SOURCE_AGENT)
    {
        // I don't know if it's OK to change this here, if
        // anything downstream does lookups by name, for instance

        LLAvatarName av_name;
        if (LLAvatarNameCache::get(from_id, &av_name))
        {
            chat.mFromName = av_name.getCompleteName();
        }
        else
        {
            chat.mFromName = LLCacheName::cleanFullName(from_name);
        }
    }
    else
    {
        // make sure that we don't have an empty or all-whitespace name
        // <FS:KC> Objects with no name get renamed to NO_NAME_OBJECT so the object profile is still accessable
        //LLStringUtil::trim(from_name);
        //if (from_name.empty())
        //{
        //  from_name = LLTrans::getString("Unnamed");
        //}
        //chat.mFromName = from_name;
        static const boost::regex whitespace_exp("^\\s*$");
        if (chat.mSourceType == CHAT_SOURCE_OBJECT && boost::regex_search(from_name, whitespace_exp))
        {
            //[FIRE-2434 Mark Unamed Objects based on setting
            static LLCachedControl<bool> FSMarkObjects(gSavedSettings, "FSMarkObjects");
            if (FSMarkObjects)
            {
                chat.mFromName = LLTrans::getString("no_name_object");
            }
            else
            {
                chat.mFromName = "";
            }
        }
        else
        {
            chat.mFromName = from_name;
        }
        // </FS:KC>
    }

    bool is_do_not_disturb = gAgent.isDoNotDisturb();

    bool is_muted = false;
    bool is_linden = false;
    is_muted = LLMuteList::getInstance()->isMuted(
        from_id,
        from_name,
        LLMute::flagTextChat)
        || LLMuteList::getInstance()->isMuted(owner_id, LLMute::flagTextChat);
    is_linden = chat.mSourceType != CHAT_SOURCE_OBJECT &&
        LLMuteList::isLinden(from_name);

    if (is_muted && (chat.mSourceType == CHAT_SOURCE_OBJECT))
    {
        return;
    }

    bool is_audible = (CHAT_AUDIBLE_FULLY == chat.mAudible);
    chatter = gObjectList.findObject(from_id);
    if (chatter)
    {
        chat.mPosAgent = chatter->getPositionAgent();
        static LLCachedControl<bool> EffectScriptChatParticles(gSavedSettings, "EffectScriptChatParticles"); // <FS:PP> gSavedSettings to LLCachedControl

        // Make swirly things only for talking objects. (not script debug messages, though)
//      if (chat.mSourceType == CHAT_SOURCE_OBJECT
//          && chat.mChatType != CHAT_TYPE_DEBUG_MSG
//          && gSavedSettings.getBOOL("EffectScriptChatParticles") )
// [RLVa:KB] - Checked: 2010-03-09 (RLVa-1.2.0b) | Modified: RLVa-1.0.0g
        if ( ((chat.mSourceType == CHAT_SOURCE_OBJECT) && (chat.mChatType != CHAT_TYPE_DEBUG_MSG)) &&
             // <FS:PP> gSavedSettings to LLCachedControl
             // (gSavedSettings.getBOOL("EffectScriptChatParticles")) &&
             (EffectScriptChatParticles) &&
             // </FS:PP>
             ((!rlv_handler_t::isEnabled()) || (CHAT_TYPE_OWNER != chat.mChatType)) )
// [/RLVa:KB]
        {
            LLPointer<LLViewerPartSourceChat> psc = new LLViewerPartSourceChat(chatter->getPositionAgent());
            psc->setSourceObject(chatter);
            psc->setColor(color);
            //We set the particles to be owned by the object's owner,
            //just in case they should be muted by the mute list
            psc->setOwnerUUID(owner_id);
            LLViewerPartSim::getInstance()->addPartSource(psc);
        }

        // record last audible utterance
        if (is_audible
            && (is_linden || (!is_muted && !is_do_not_disturb)))
        {
            if (chat.mChatType != CHAT_TYPE_START
                && chat.mChatType != CHAT_TYPE_STOP)
            {
                gAgent.heardChat(chat.mFromID);
            }
        }
    }

    if (is_audible)
    {
        //bool visible_in_chat_bubble = false;
        std::string verb;

        color.setVec(1.f,1.f,1.f,1.f);
        msg->getStringFast(_PREHASH_ChatData, _PREHASH_Message, mesg);

        // NaCl - Newline flood protection
        static LLCachedControl<bool> useAntiSpam(gSavedSettings, "UseAntiSpam");
        // <FS:TS> FIRE-23138: Add option to antispam user's own objects
        bool deferred_newline_spam_check = false;
        if (useAntiSpam)
        {
            bool doCheck = true;
            if (owner_id.isNull())
            {
                doCheck = false;
            }
            if (doCheck && !useAntiSpamMine)
            {
                if (gAgentID == owner_id)
                {
                    doCheck = false;
                    }
                if (doCheck && chatter && chatter->permYouOwner())
                {
                    doCheck = false;
                }
                if (doCheck && NACLAntiSpamRegistry::instance().checkNewlineFlood(ANTISPAM_QUEUE_CHAT, owner_id, mesg))
                {
                    return;
                }
            }
            if (doCheck && useAntiSpamMine)
            {
                // If it's the user's object, defer the check so RLV commands still work
                deferred_newline_spam_check = true;
            }
            // </FS:TS> FIRE-23138
        }
        // NaCl End

// [RLVa:KB] - Checked: 2010-04-23 (RLVa-1.2.0f) | Modified: RLVa-1.2.0f
        const VSReceivedChatContext receive_context{
            [chatter] { return VSReceivedChatContext::Source{chatter && chatter->isAttachment(), chatter && chatter->permYouOwner()}; },
            [&chat]() -> std::optional<std::string>
            {
                const auto* region = LLWorld::getInstance()->getRegionFromPosAgent(chat.mPosAgent);
                if (!region) return {};
                return LLSLURL(region->getName(), chat.mPosAgent).getLocationString();
            }};
        if (!vs_filter_received_chat(chat, mesg, owner_id, receive_context)) return;
// [/RLVa:KB]

        bool ircstyle = false;

        // Look for IRC-style emotes here so chatbubbles work
        // <FS:Ansariel> Consolidate IRC /me prefix checks
        //std::string prefix = mesg.substr(0, 4);
        //if (prefix == "/me " || prefix == "/me'")
        if (FSCommon::is_irc_me_prefix(mesg))
        // </FS:Ansariel>
        {
            ircstyle = true;
        }
        chat.mText = mesg;

        // Look for the start of typing so we can put "..." in the bubbles.
        if (CHAT_TYPE_START == chat.mChatType)
        {
            LLLocalSpeakerMgr::getInstance()->setSpeakerTyping(from_id, true);

            // Might not have the avatar constructed yet, eg on login.
            if (chatter && chatter->isAvatar())
            {
                ((LLVOAvatar*)chatter)->startTyping();
            }
            return;
        }
        else if (CHAT_TYPE_STOP == chat.mChatType)
        {
            LLLocalSpeakerMgr::getInstance()->setSpeakerTyping(from_id, false);

            // Might not have the avatar constructed yet, eg on login.
            if (chatter && chatter->isAvatar())
            {
                ((LLVOAvatar*)chatter)->stopTyping();
            }
            return;
        }

        // Look for IRC-style emotes
        if (ircstyle)
        {
            // set CHAT_STYLE_IRC to avoid adding Avatar Name as author of message. See EXT-656
            chat.mChatStyle = CHAT_STYLE_IRC;

            // Do nothing, ircstyle is fixed above for chat bubbles
        }
        else
        {
            chat.mText = "";
            switch(chat.mChatType)
            {
            case CHAT_TYPE_WHISPER:
                chat.mText = LLTrans::getString("whisper") + " ";
                break;
            case CHAT_TYPE_OWNER:
                // <FS:TT> Client LSL Bridge
                {
                    if (FSLSLBridge::instance().lslToViewer(mesg, from_id, owner_id))
                    {
                        return;
                    }
                }
                // </FS:TT>

                // <FS:KC> cmdline packager
                if (cmdline_packager(mesg, from_id, owner_id))
                {
                    return;
                }
                // </FS:KC>

// [RLVa:KB] - Checked: 2010-02-XX (RLVa-1.2.0a) | Modified: RLVa-1.1.0f
                // TODO-RLVa: [RLVa-1.2.0] consider rewriting this before a RLVa-1.2.0 release
                if (const auto result = vs_process_owner_chat(chat, mesg, [chatter]
                    {
                        return !chatter || !chatter->isAttachment() || !chatter->isTempAttachment() ||
                               RlvSettings::getEnableTemporaryAttachments();
                    }); result != VSOwnerChatResult::NotCommand)
                {
                    if (result == VSOwnerChatResult::Consumed) return;
                    break;
                }
// [/RLVa:KB]
// [RLVa:KB] - Checked: 2010-03-09 (RLVa-1.2.0b) | Modified: RLVa-1.0.0g
                // Copy/paste from above
                if  ( (rlv_handler_t::isEnabled()) && (chatter) && (chat.mSourceType == CHAT_SOURCE_OBJECT) &&
                      (gSavedSettings.getBOOL("EffectScriptChatParticles")) )
                {
                    LLPointer<LLViewerPartSourceChat> psc = new LLViewerPartSourceChat(chatter->getPositionAgent());
                    psc->setSourceObject(chatter);
                    psc->setColor(color);
                    //We set the particles to be owned by the object's owner,
                    //just in case they should be muted by the mute list
                    psc->setOwnerUUID(owner_id);
                    LLViewerPartSim::getInstance()->addPartSource(psc);
                }
// [/RLVa:KB]
            case CHAT_TYPE_DEBUG_MSG:
            case CHAT_TYPE_NORMAL:
            case CHAT_TYPE_DIRECT:
                break;
            case CHAT_TYPE_SHOUT:
                chat.mText = LLTrans::getString("shout") + " ";
                break;
            case CHAT_TYPE_START:
            case CHAT_TYPE_STOP:
                LL_WARNS("Messaging") << "Got chat type start/stop in main chat processing." << LL_ENDL;
                break;
            default:
                LL_WARNS("Messaging") << "Unknown type " << chat.mChatType << " in chat!" << LL_ENDL;
                break;
            }
            // <FS:TS> FIRE-23138: Enable spam checking for user's own objects
            if (deferred_spam_check && NACLAntiSpamRegistry::instance().checkQueue(ANTISPAM_QUEUE_CHAT, from_id, ANTISPAM_SOURCE_OBJECT))
            {
                return;
            }
            if (deferred_newline_spam_check && NACLAntiSpamRegistry::instance().checkNewlineFlood(ANTISPAM_QUEUE_CHAT, from_id, mesg))
            {
                return;
            }
            // </FS:TS> FIRE-23138

            chat.mText += mesg;
        }

        // <FS:Zi> Omnifilter support
        static LLCachedControl<bool> use_omnifilter(gSavedSettings, "OmnifilterEnabled", false);
        if (use_omnifilter)
        {
            OmnifilterEngine::Haystack haystack;
            haystack.mContent = chat.mText;
            haystack.mSenderName = from_name;   // we don't use chat.mFromName here because that will include display names etc.
            haystack.mOwnerID = chat.mFromID;

            switch (chat.mChatType)
            {
            case CHAT_TYPE_WHISPER:
            case CHAT_TYPE_NORMAL:
            case CHAT_TYPE_SHOUT:
            case CHAT_TYPE_DEBUG_MSG:
            case CHAT_TYPE_REGION:
            case CHAT_TYPE_OWNER:
            case CHAT_TYPE_DIRECT:
            {
                switch (chat.mSourceType)
                {
                case CHAT_SOURCE_AGENT:        // this is the type for regular chat
                {
                    haystack.mType = OmnifilterEngine::eType::NearbyChat;
                    break;
                }
                case CHAT_SOURCE_OBJECT:       // this is the type for object chat
                {
                    haystack.mType = OmnifilterEngine::eType::ObjectChat;
                    break;
                }
                default:
                {
                    LL_DEBUGS("Omnifilter") << "unhandled source type " << (U32)chat.mSourceType << LL_ENDL;
                    break;
                }
                }
                const OmnifilterEngine::Needle* needle = OmnifilterEngine::getInstance()->match(haystack);

                if (needle)
                {
                    // we need to make sure to put the typing stopped flag on the chatting avatar (if it is an avatar)
                    if (chatter && chatter->isAvatar())
                    {
                        LLLocalSpeakerMgr::getInstance()->setSpeakerTyping(from_id, false);
                        ((LLVOAvatar*)chatter)->stopTyping();
                    }

                    if (needle->mChatReplace.empty())       // no replacement defined, just suppress the message
                    {
                        return;
                    }

                    LLSD args;
                    args["REPLACEMENT"] = needle->mChatReplace;
                    chat.mText = LLTrans::getString("OmnifilterReplacement", args);
                }

                break;
            }
            default:
            {
                LL_DEBUGS("Omnifilter") << "unhandled chat type " << (U32)chat.mChatType << LL_ENDL;
                break;
            }
            }
        }
        // </FS:Zi>

        // We have a real utterance now, so can stop showing "..." and proceed.
        if (chatter && chatter->isAvatar())
        {
            LLLocalSpeakerMgr::getInstance()->setSpeakerTyping(from_id, false);
            ((LLVOAvatar*)chatter)->stopTyping();

            if (!is_muted && !is_do_not_disturb)
            {
                //visible_in_chat_bubble = gSavedSettings.getBOOL("UseChatBubbles");
                std::string formated_msg = "";
                LLViewerChat::formatChatMsg(chat, formated_msg);
                LLChat chat_bubble = chat;

                chat_bubble.mText = formated_msg;
                ((LLVOAvatar*)chatter)->addChat(chat_bubble);
            }
        }

        if (chatter)
        {
            chat.mPosAgent = chatter->getPositionAgent();
        }

        // truth table:
        // LINDEN   BUSY    MUTED   OWNED_BY_YOU    TASK        DISPLAY     STORE IN HISTORY
        // F        F       F       F               *           Yes         Yes
        // F        F       F       T               *           Yes         Yes
        // F        F       T       F               *           No          No
        // F        F       T       T               *           No          No
        // F        T       F       F               *           No          Yes
        // F        T       F       T               *           Yes         Yes
        // F        T       T       F               *           No          No
        // F        T       T       T               *           No          No
        // T        *       *       *               F           Yes         Yes

        chat.mMuted = is_muted && !is_linden;

        // pass owner_id to chat so that we can display the remote
        // object inspect for an object that is chatting with you
        LLSD args;
        chat.mOwnerID = owner_id;

        // <FS:PP> FIRE-10178: Keyword Alerts in group IM do not work unless the group is in the foreground (notification on receipt of local chat)
        if (FSKeywords::getInstance()->chatContainsKeyword(chat, true))
        {
            FSKeywords::notify(chat);
        }
        // </FS:PP>

        LLTranslate::instance().logCharsSeen(mesg.size());
        // <FS:PP> gSavedSettings to LLCachedControl
        // if (gSavedSettings.getBOOL("TranslateChat") && chat.mSourceType != CHAT_SOURCE_SYSTEM)
        static LLCachedControl<bool> TranslateChat(gSavedSettings, "TranslateChat");
        if (TranslateChat && chat.mSourceType != CHAT_SOURCE_SYSTEM)
        // </FS:PP>
        {
            if (chat.mChatStyle == CHAT_STYLE_IRC)
            {
                mesg = mesg.substr(4, std::string::npos);
            }
            const std::string from_lang = ""; // leave empty to trigger autodetect
            const std::string to_lang = LLTranslate::getTranslateLanguage();

            LLTranslate::instance().logCharsSent(mesg.size());
            LLTranslate::translateMessage(from_lang, to_lang, mesg,
                boost::bind(&translateSuccess, chat, args, mesg, from_lang, _1, _2),
                boost::bind(&translateFailure, chat, args, _1, _2));

        }
        else
        {
            LLNotificationsUI::LLNotificationManager::instance().onChat(chat, args);
        }

        // don't call notification for debug messages from not owned objects
        if (chat.mChatType == CHAT_TYPE_DEBUG_MSG)
        {
            // <FS:Ansariel> FIRE-15014: [OpenSim] osMessageObject(target, message) fails silently
            //if (gAgentID != chat.mOwnerID)
#ifdef OPENSIM
            if (LLGridManager::getInstance()->isInSecondLife() && gAgentID != chat.mOwnerID)
#else
            if (gAgentID != chat.mOwnerID)
#endif
            // </FS:Ansariel>
            {
                return;
            }
        }

        if (mesg != "")
        {

            // <FS:PP> FIRE-36367 Ability to play a sound when someone talks in Nearby Chat
            if (chat.mSourceType == CHAT_SOURCE_AGENT &&
                chat.mFromID.notNull() &&
                chat.mFromID != gAgentID &&
                !chat.mMuted &&
                (chat.mChatType == CHAT_TYPE_WHISPER ||
                 chat.mChatType == CHAT_TYPE_NORMAL ||
                 chat.mChatType == CHAT_TYPE_SHOUT ||
                 chat.mChatType == CHAT_TYPE_DIRECT))
            {
                make_ui_sound("UISndNearbyChat");
            }
            // </FS:PP>

            LLSD msg_notify = LLSD(LLSD::emptyMap());
            msg_notify["session_id"] = LLUUID();
            msg_notify["from_id"] = chat.mFromID;
            msg_notify["source_type"] = chat.mSourceType;
            // used to check if there is agent mention in the message
            msg_notify["message"] = mesg;
            on_new_message(msg_notify);
        }

    }
}
