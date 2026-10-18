void GoldAvatarActions::estateBanMultiple(const uuid_vec_t& idAgents)
{
    LLViewerRegion* region = gAgent.getRegion();
    if (!region)
    {
        return;
    }

    uuid_vec_t idEjectAgents(idAgents);
    if (!canEstateKickOrTeleportHomeMultiple(idEjectAgents, true))
        return;

    LLSD args, payload; std::string strMsgName, strResidents;
    for (uuid_vec_t::const_iterator itAgent = idEjectAgents.begin(); itAgent != idEjectAgents.end(); ++itAgent)
    {
        const LLUUID& idAgent = *itAgent;
        if (idEjectAgents.begin() != itAgent)
            strResidents += "\n";
        strResidents += LLSLURL("agent", idAgent, (!gRlvHandler.hasBehaviour(RLV_BHVR_SHOWNAMES)) ? "completename" : "rlvanonym").getSLURLString();
        payload["ids"].append(*itAgent);
    }

    std::string owner = LLSLURL("agent", region->getOwner(), "inspect").getSLURLString();
    if (gAgent.isGodlike())
    {
        LLStringUtil::format_map_t owner_args;
        owner_args["[OWNER]"] = owner;
        args["ALL_ESTATES"] = LLTrans::getString("RegionInfoAllEstatesOwnedBy", owner_args);
    }
    else if (region->getOwner() == gAgent.getID())
    {
        args["ALL_ESTATES"] = LLTrans::getString("RegionInfoAllEstatesYouOwn");
    }
    else if (region->isEstateManager())
    {
        LLStringUtil::format_map_t owner_args;
        owner_args["[OWNER]"] = owner.c_str();
        args["ALL_ESTATES"] = LLTrans::getString("RegionInfoAllEstatesYouManage", owner_args);
    }

    if (1 == payload["ids"].size())
    {
        args["EVIL_USER"] = strResidents;
        strMsgName = "EstateBanUser";
    }
    else
    {
        args["RESIDENTS"] = strResidents;
        strMsgName = "EstateBanUserMultiple";
    }

    LLNotificationsUtil::add(strMsgName, args, payload, &callbackEstateBan);
}
bool GoldAvatarActions::callbackEstateBan(const LLSD& notification, const LLSD& response)
{
    LLViewerRegion* region = gAgent.getRegion();
    if (!region) return false; // Region may have retired while confirmation was open.
    S32 idxOption = LLNotificationsUtil::getSelectedOption(notification, response);

    if (0 == idxOption || 1 == idxOption)
    {
        const LLSD& idAgents = notification["payload"]["ids"];
        for (LLSD::array_const_iterator itAgent = idAgents.beginArray(); itAgent != idAgents.endArray(); ++itAgent)
        {
            if (region->getOwner() == itAgent->asUUID())
            {
                // Can't ban the owner!
                continue;
            }

            U32 flags = ESTATE_ACCESS_BANNED_AGENT_ADD | ESTATE_ACCESS_ALLOWED_AGENT_REMOVE;

            if (itAgent + 1 != idAgents.endArray())
            {
                flags |= ESTATE_ACCESS_NO_REPLY;
            }

            if (idxOption == 1)
            {
                // All estates, either than I own or manage for this owner.
                // This will be verified on simulator. JC
                if (!region)
                {
                    break;
                }

                if (region->getOwner() == gAgent.getID()
                    || gAgent.isGodlike())
                {
                    flags |= ESTATE_ACCESS_APPLY_TO_ALL_ESTATES;
                }
                else if (region->isEstateManager())
                {
                    flags |= ESTATE_ACCESS_APPLY_TO_MANAGED_ESTATES;
                }
            }

            LLFloaterRegionInfo::nextInvoice();
            LLPanelEstateAccess::sendEstateAccessDelta(flags, itAgent->asUUID());
        }
    }
    return false;
}