// Frozen reference controller policy; source-only witness. LGPL-2.1.
void LLFloaterInspect::onClickCreatorProfile()
{
        const LLSelectNode* node = getSelectedNode();
        if(node)
        {
            // Only anonymize the creator if they're also the owner or if they're a nearby avie
            const LLUUID& idCreator = node->mPermissions->getCreator();
            if ( (!RlvActions::canShowName(RlvActions::SNC_DEFAULT, idCreator)) && ((node->mPermissions->getOwner() == idCreator) || (RlvUtil::isNearbyAgent(idCreator))) )
            {
                return;
            }
            LLAvatarActions::showProfile(idCreator);
        }
}
void LLFloaterInspect::onClickOwnerProfile()
{
        const LLSelectNode* node = getSelectedNode();
        if(node)
        {
            if(node->mPermissions->isGroupOwned())
            {
                const LLUUID& idGroup = node->mPermissions->getGroup();
                LLGroupActions::show(idGroup);
            }
            else
            {
                const LLUUID& owner_id = node->mPermissions->getOwner();
                if (!RlvActions::canShowName(RlvActions::SNC_DEFAULT, owner_id))
                    return;
                LLAvatarActions::showProfile(owner_id);
            }

        }
}
void LLFloaterInspect::onSelectObject()
{
    if(LLFloaterInspect::getSelectedUUID() != LLUUID::null)
    {
        if (!RlvActions::isRlvEnabled())
        {
            getChildView("button owner")->setEnabled(true);
            getChildView("button creator")->setEnabled(true);
        }
        else
        {
            const LLSelectNode* node = getSelectedNode();
            const LLUUID& idOwner = (node) ? node->mPermissions->getOwner() : LLUUID::null;
            const LLUUID& idCreator = (node) ? node->mPermissions->getCreator() : LLUUID::null;

            // See LLFloaterInspect::onClickCreatorProfile()
            getChildView("button owner")->setEnabled( (RlvActions::canShowName(RlvActions::SNC_DEFAULT, idOwner)) || ((node) && (node->mPermissions->isGroupOwned())) );
            // See LLFloaterInspect::onClickOwnerProfile()
            getChildView("button creator")->setEnabled( ((idOwner != idCreator) && (!RlvUtil::isNearbyAgent(idCreator))) || (RlvActions::canShowName(RlvActions::SNC_DEFAULT, idCreator)) );
        }
    }
}
void LLFloaterInspect::refresh()
{
    LLUUID creator_id;
    std::string creator_name;
    S32 pos = mObjectList->getScrollPos();
    // PoundLife - Improved Object Inspect
    LLLocale locale("");
    LLResMgr& res_mgr = LLResMgr::instance();
    LLSelectMgr& sel_mgr = LLSelectMgr::instance();
    S32 fcount = 0;
    S32 fcount_visible = 0;
    S32 tcount = 0;
    S32 vcount = 0;
    S32 objcount = 0;
    S32 primcount = 0;
    U32 complexity = 0;
    mTextureList.clear();
    mTextureMemory = 0;
    mTextureVRAMMemory = 0;
    std::string format_res_string;
    static LLCachedControl<F32> max_complexity_setting(gSavedSettings, "MaxAttachmentComplexity");
    F32 max_attachment_complexity = max_complexity_setting;
    max_attachment_complexity = llmax(max_attachment_complexity, 1.0e6f);
    // PoundLife - End
    getChildView("button owner")->setEnabled(false);
    getChildView("button creator")->setEnabled(false);
    LLUUID selected_uuid;
    S32 selected_index = mObjectList->getFirstSelectedIndex();
    if(selected_index > -1)
    {
        LLScrollListItem* first_selected =
            mObjectList->getFirstSelected();
        if (first_selected)
        {
            selected_uuid = first_selected->getUUID();
        }
    }
    mObjectList->operateOnAll(LLScrollListCtrl::OP_DELETE);
    //List all transient objects, then all linked objects

    for (LLObjectSelection::valid_iterator iter = mObjectSelection->valid_begin();
         iter != mObjectSelection->valid_end(); iter++)
    {
        LLSelectNode* obj = *iter;
        LLSD row;
        std::string owner_name, creator_name;
        auto vobj{obj->getObject()};

        if (obj->mCreationDate == 0)
        {   // Don't have valid information from the server, so skip this one
            continue;
        }

        time_t timestamp = (time_t) (obj->mCreationDate/1000000);
        static bool use_24h = gSavedSettings.getBOOL("Use24HourClock");
        std::string timeStr = use_24h ? getString("timeStamp") : getString("timeStampAMPM");
        LLSD substitution;
        substitution["datetime"] = (S32) timestamp;
        LLStringUtil::format (timeStr, substitution);

        const LLUUID& idOwner = obj->mPermissions->getOwner();
        const LLUUID& idCreator = obj->mPermissions->getCreator();
        LLAvatarName av_name;

        if(obj->mPermissions->isGroupOwned())
        {
            std::string group_name;
            const LLUUID& idGroup = obj->mPermissions->getGroup();
            if(gCacheName->getGroupName(idGroup, group_name))
            {
                // <FS:Ansariel> Make text localizable
                //owner_name = "[" + group_name + "] (group)";
                owner_name = "[" + group_name + "] " + getString("Group");
            }
            else
            {
                owner_name = LLTrans::getString("RetrievingData");
                if (mOwnerNameCacheConnection.connected())
                {
                    mOwnerNameCacheConnection.disconnect();
                }
                mOwnerNameCacheConnection = gCacheName->getGroup(idGroup, boost::bind(&LLFloaterInspect::onGetOwnerNameCallback, this));
            }
        }
        else
        {
            // Only work with the names if we actually get a result
            // from the name cache. If not, defer setting the
            // actual name and set a placeholder.
            if (LLAvatarNameCache::get(idOwner, &av_name))
            {
// [RLVa:KB] - Checked: RLVa-2.0.1
                bool fRlvCanShowName = (RlvActions::canShowName(RlvActions::SNC_DEFAULT, idOwner)) || (obj->mPermissions->isGroupOwned());
                owner_name = (fRlvCanShowName) ? av_name.getCompleteName() : RlvStrings::getAnonym(av_name);
// [/RLVa:KB]
//              owner_name = av_name.getCompleteName();
            }
            else
            {
                owner_name = LLTrans::getString("RetrievingData");
                if (mOwnerNameCacheConnection.connected())
                {
                    mOwnerNameCacheConnection.disconnect();
                }
                mOwnerNameCacheConnection = LLAvatarNameCache::get(idOwner, boost::bind(&LLFloaterInspect::onGetOwnerNameCallback, this));
            }
        }

        if (LLAvatarNameCache::get(idCreator, &av_name))
        {
// [RLVa:KB] - Checked: RLVa-2.0.1
            const LLUUID& idCreator = obj->mPermissions->getCreator();
            bool fRlvCanShowName = (RlvActions::canShowName(RlvActions::SNC_DEFAULT, idCreator)) || ( (obj->mPermissions->getOwner() != idCreator) && (!RlvUtil::isNearbyAgent(idCreator)) );
            creator_name = (fRlvCanShowName) ? av_name.getCompleteName() : RlvStrings::getAnonym(av_name);
// [/RLVa:KB]
//          creator_name = av_name.getCompleteName();
        }
        else
        {
            creator_name = LLTrans::getString("RetrievingData");
            if (mCreatorNameCacheConnection.connected())
            {
                mCreatorNameCacheConnection.disconnect();
            }
            mCreatorNameCacheConnection = LLAvatarNameCache::get(idCreator, boost::bind(&LLFloaterInspect::onGetCreatorNameCallback, this));
        }

        row["id"] = vobj->getID();
        row["columns"][0]["column"] = "object_name";
        row["columns"][0]["type"] = "text";
        // make sure we're either at the top of the link chain
        // or top of the editable chain, for attachments
        if(!(vobj->isRoot() || vobj->isRootEdit()))
        {
            row["columns"][0]["value"] = std::string("   ") + obj->mName;
        }
        else
        {
            row["columns"][0]["value"] = obj->mName;
        }
        row["columns"][1]["column"] = "owner_name";
        row["columns"][1]["type"] = "text";
        row["columns"][1]["value"] = owner_name;
        row["columns"][2]["column"] = "creator_name";
        row["columns"][2]["type"] = "text";
        row["columns"][2]["value"] = creator_name;
        row["columns"][3]["column"] = "creation_date";
        row["columns"][3]["type"] = "text";
        row["columns"][3]["value"] = timeStr;
        // <FS:PP> FIRE-12854: Include a Description column in the Inspect Objects floater
        row["columns"][4]["column"] = "description";
        row["columns"][4]["type"] = "text";
        row["columns"][4]["value"] = obj->mDescription;
        // </FS:PP>
        // <FS:Ansariel> Correct creation date sorting
        row["columns"][5]["column"] = "creation_date_sort";
        row["columns"][5]["type"] = "text";
        row["columns"][5]["value"] = llformat("%d", timestamp);
        // </FS:Ansariel>
        // PoundLife - Improved Object Inspect
        res_mgr.getIntegerString(format_res_string, vobj->getNumFaces());
        row["columns"][6]["column"] = "facecount";
        row["columns"][6]["type"] = "text";
        row["columns"][6]["value"] = format_res_string;

        res_mgr.getIntegerString(format_res_string, vobj->getNumVertices());
        row["columns"][7]["column"] = "vertexcount";
        row["columns"][7]["type"] = "text";
        row["columns"][7]["value"] = format_res_string;

        res_mgr.getIntegerString(format_res_string, vobj->getNumIndices() / 3);
        row["columns"][8]["column"] = "trianglecount";
        row["columns"][8]["type"] = "text";
        row["columns"][8]["value"] = format_res_string;

        // Poundlife - Get VRAM
        U32 texture_memory = 0;
        U32 vram_memory = 0;
        getObjectTextureMemory(vobj, texture_memory, vram_memory);
        res_mgr.getIntegerString(format_res_string, texture_memory / 1024);
        row["columns"][9]["column"] = "tramcount";
        row["columns"][9]["type"] = "text";
        row["columns"][9]["value"] = format_res_string;

        res_mgr.getIntegerString(format_res_string, vram_memory / 1024);
        row["columns"][10]["column"] = "vramcount";
        row["columns"][10]["type"] = "text";
        row["columns"][10]["value"] = format_res_string;

        row["columns"][11]["column"] = "facecount_sort";
        row["columns"][11]["type"] = "text";
        row["columns"][11]["value"] = LLSD::Integer(vobj->getNumFaces());

        row["columns"][12]["column"] = "vertexcount_sort";
        row["columns"][12]["type"] = "text";
        row["columns"][12]["value"] = LLSD::Integer(vobj->getNumVertices());

        row["columns"][13]["column"] = "trianglecount_sort";
        row["columns"][13]["type"] = "text";
        row["columns"][13]["value"] = LLSD::Integer(vobj->getNumIndices() / 3);

        row["columns"][14]["column"] = "tramcount_sort";
        row["columns"][14]["type"] = "text";
        row["columns"][14]["value"] = LLSD::Integer(texture_memory / 1024);

        row["columns"][15]["column"] = "vramcount_sort";
        row["columns"][15]["type"] = "text";
        row["columns"][15]["value"] = LLSD::Integer(vram_memory / 1024);

        primcount = sel_mgr.getSelection()->getObjectCount();
        objcount = sel_mgr.getSelection()->getRootObjectCount();
        fcount += vobj->getNumFaces();
        fcount_visible += vobj->getNumVisibleFaces();
        tcount += vobj->getNumIndices() / 3;
        vcount += vobj->getNumVertices();

        // PoundLife - END
        mObjectList->addElement(row, ADD_TOP);
    }

    // <FS:Ansariel> Show complexity in inspect window
    for (LLObjectSelection::valid_root_iterator root_it = mObjectSelection->valid_root_begin(); root_it != mObjectSelection->valid_root_end(); ++root_it)
    {
        LLSelectNode* obj = *root_it;
        LLVOVolume* volume = dynamic_cast<LLVOVolume*>(obj->getObject());
        if (volume)
        {
            LLVOVolume::texture_cost_t textures;
            F32 attachment_total_cost = 0;
            F32 attachment_volume_cost = 0;
            F32 attachment_texture_cost = 0;
            F32 attachment_children_cost = 0;

            attachment_volume_cost += volume->getRenderCost(textures);

            for (const auto& child_obj : volume->getChildren())
            {
                LLVOVolume *child = dynamic_cast<LLVOVolume*>(child_obj.get());
                if (child)
                {
                    attachment_children_cost += child->getRenderCost(textures);
                }
            }

            for (auto texture : textures)
            {
                // add the cost of each individual texture in the linkset
                attachment_texture_cost += LLVOVolume::getTextureCost(texture);
            }

            attachment_total_cost = attachment_volume_cost + attachment_texture_cost + attachment_children_cost;

            // Limit attachment complexity to avoid signed integer flipping of the wearer's ACI
            complexity += (U32)llclamp(attachment_total_cost, 0.f, max_attachment_complexity);
        }
    }
    // </FS:Ansariel>

    if(selected_index > -1 && mObjectList->getItemIndex(selected_uuid) == selected_index)
    {
        mObjectList->selectNthItem(selected_index);
    }
    else
    {
        mObjectList->selectNthItem(0);
    }
    onSelectObject();
    mObjectList->setScrollPos(pos);

    // PoundLife - Total linkset stats.
    LLStringUtil::format_map_t args;
    res_mgr.getIntegerString(format_res_string, objcount);
    args["NUM_OBJECTS"] = format_res_string;
    res_mgr.getIntegerString(format_res_string, primcount);
    args["NUM_PRIMS"] = format_res_string;
    res_mgr.getIntegerString(format_res_string, fcount_visible);
    args["NUM_VISIBLE_FACES"] = format_res_string;
    res_mgr.getIntegerString(format_res_string, fcount);
    args["NUM_FACES"] = format_res_string;
    res_mgr.getIntegerString(format_res_string, vcount);
    args["NUM_VERTICES"] = format_res_string;
    res_mgr.getIntegerString(format_res_string, tcount);
    args["NUM_TRIANGLES"] = format_res_string;
    res_mgr.getIntegerString(format_res_string, static_cast<S32>(mTextureList.size()));
    args["NUM_TEXTURES"] = format_res_string;
    res_mgr.getIntegerString(format_res_string, mTextureMemory / 1024);
    args["TEXTURE_MEMORY"] = format_res_string;
    res_mgr.getIntegerString(format_res_string, mTextureVRAMMemory / 1024);
    args["VRAM_USAGE"] = format_res_string;
    res_mgr.getIntegerString(format_res_string, complexity);
    args["COMPLEXITY"] = format_res_string;
    getChild<LLTextBase>("linksetstats_text")->setText(getString("stats_list", args));
    // PoundLife - End
}