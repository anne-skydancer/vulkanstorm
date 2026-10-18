void goldSendEstateOwnerMessage(const LLViewerRegion* pRegion, const std::string& request, const LLUUID& invoice, const strings_t& strings)
{
    if (pRegion)
    {
        LL_INFOS() << "Sending estate request '" << request << "'" << LL_ENDL;
        gMessageSystem->newMessage("EstateOwnerMessage");
        gMessageSystem->nextBlockFast(_PREHASH_AgentData);
        gMessageSystem->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
        gMessageSystem->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
        gMessageSystem->addUUIDFast(_PREHASH_TransactionID, LLUUID::null); //not used
        gMessageSystem->nextBlock("MethodData");
        gMessageSystem->addString("Method", request);
        gMessageSystem->addUUID("Invoice", invoice);
        if(strings.empty())
        {
            gMessageSystem->nextBlock("ParamList");
            gMessageSystem->addString("Parameter", NULL);
        }
        else
        {
            strings_t::const_iterator it = strings.begin();
            strings_t::const_iterator end = strings.end();
            for(; it != end; ++it)
            {
                gMessageSystem->nextBlock("ParamList");
                gMessageSystem->addString("Parameter", *it);
            }
        }
        gMessageSystem->sendReliable(pRegion->getHost());
    }
}
bool GoldAvatarActions::callbackLandEject(const LLSD& notification, const LLSD& response)
{
    S32 idxOption = LLNotificationsUtil::getSelectedOption(notification, response);
    if (2 == idxOption)                         // Cancel button.
        return false;

    bool fBanEnabled = notification["payload"]["ban_enabled"].asBoolean();
    if ( (0 == idxOption) || (fBanEnabled) )    // Eject button (or Eject + Ban)
    {
        const LLSD& idAgents = notification["payload"]["ids"];
        for (LLSD::array_const_iterator itAgent = idAgents.beginArray(); itAgent != idAgents.endArray(); ++itAgent)
        {
            const LLUUID idAgent = itAgent->asUUID(); const LLViewerRegion* pAgentRegion = NULL;
            if (getRegionFromAgentID(idAgent, &pAgentRegion))
            {
                // This is tricky. It is similar to say if it is not an 'Eject' button, and it is also not an 'Cancel' button,
                // and ban_enabled==true, it should be the 'Eject and Ban' button.
                U32 flags = ( (0 != idxOption) && (fBanEnabled) ) ? 0x1 : 0x0;

                gMessageSystem->newMessage("EjectUser");
                gMessageSystem->nextBlock("AgentData");
                gMessageSystem->addUUID("AgentID", gAgent.getID());
                gMessageSystem->addUUID("SessionID", gAgent.getSessionID());
                gMessageSystem->nextBlock("Data");
                gMessageSystem->addUUID("TargetID", idAgent);
                gMessageSystem->addU32("Flags", flags);
                gMessageSystem->sendReliable(pAgentRegion->getHost());
            }
        }
    }
    return false;
}
bool GoldAvatarActions::callbackLandFreeze(const LLSD& notification, const LLSD& response)
{
    S32 idxOption = LLNotificationsUtil::getSelectedOption(notification, response);
    if ( (0 == idxOption) || (1 == idxOption) )
    {
        U32 flags = (0 == idxOption) ? 0x0 : 0x1;

        const LLSD& idAgents = notification["payload"]["ids"];
        for (LLSD::array_const_iterator itAgent = idAgents.beginArray(); itAgent != idAgents.endArray(); ++itAgent)
        {
            const LLUUID idAgent = itAgent->asUUID(); const LLViewerRegion* pAgentRegion = NULL;
            if (getRegionFromAgentID(idAgent, &pAgentRegion))
            {
                gMessageSystem->newMessage("FreezeUser");
                gMessageSystem->nextBlock("AgentData");
                gMessageSystem->addUUID("AgentID", gAgent.getID());
                gMessageSystem->addUUID("SessionID", gAgent.getSessionID());
                gMessageSystem->nextBlock("Data");
                gMessageSystem->addUUID("TargetID", idAgent);
                gMessageSystem->addU32("Flags", flags);
                gMessageSystem->sendReliable(pAgentRegion->getHost());
            }
        }
    }
    return false;
}
bool GoldAvatarActions::callbackEstateKick(const LLSD& notification, const LLSD& response)
{
    S32 idxOption = LLNotificationsUtil::getSelectedOption(notification, response);
    if (0 == idxOption)
    {
        const LLSD& idAgents = notification["payload"]["ids"];
        for (LLSD::array_const_iterator itAgent = idAgents.beginArray(); itAgent != idAgents.endArray(); ++itAgent)
        {
            const LLViewerRegion* pRegion = NULL;
            if (getRegionFromAgentID(itAgent->asUUID(), &pRegion))
            {
                strings_t strings;
                strings.push_back(itAgent->asString());

                goldSendEstateOwnerMessage(pRegion, "kickestate", LLUUID::generateNewID(), strings);
            }
        }
    }
    return false;
}
bool GoldAvatarActions::callbackEstateTeleportHome(const LLSD& notification, const LLSD& response)
{
    S32 idxOption = LLNotificationsUtil::getSelectedOption(notification, response);
    if (0 == idxOption)
    {
        const LLSD& idAgents = notification["payload"]["ids"];
        for (LLSD::array_const_iterator itAgent = idAgents.beginArray(); itAgent != idAgents.endArray(); ++itAgent)
        {
            const LLViewerRegion* pRegion = NULL;
            if (getRegionFromAgentID(itAgent->asUUID(), &pRegion))
            {
                strings_t strings;
                strings.push_back(gAgent.getID().asString());
                strings.push_back(itAgent->asString());

                goldSendEstateOwnerMessage(pRegion, "teleporthomeuser", LLUUID::generateNewID(), strings);
            }
        }
    }
    return false;
}