// Bench only (llaisupdate_parity, VKS_AISUPDATE_BENCH): AISUpdate's C++ parse
// as it was before the inventory rung R3a moved it to Rust, frozen from
// llaisapi.cpp blob a0d52b1edebeebb208631d778c3bd89d6270dcf8 (viewer
// b4dcfea560), class renamed AISUpdateCppParse, checkTimeout() a no-op (the
// bench never yields). Included by llaisupdate_parity_stubs.cpp right after
// llaisapi.cpp, so it sees the same environment. Not an oracle: the
// recordings are; this only times the old parse against the Rust one.
class AISUpdateCppParse
{
public:
    AISUpdateCppParse(const LLSD& update, AISAPI::COMMAND_TYPE type, const LLSD& request_body) : mType(type)
    {
        mFetch = (type == AISAPI::FETCHITEM) || (type == AISAPI::FETCHCATEGORYCHILDREN) ||
                 (type == AISAPI::FETCHCATEGORYCATEGORIES) || (type == AISAPI::FETCHCATEGORYSUBSET) ||
                 (type == AISAPI::FETCHCOF) || (type == AISAPI::FETCHCATEGORYLINKS) || (type == AISAPI::FETCHORPHANS);
        mFetchDepth = MAX_FOLDER_DEPTH_REQUEST;
        if (mFetch && request_body.has("depth"))
        {
            mFetchDepth = request_body["depth"].asInteger();
        }
        parseUpdate(update);
    }
    size_t objectCount() const
    {
        return mItemsCreated.size() + mItemsUpdated.size() + mCategoriesCreated.size() + mCategoriesUpdated.size();
    }

    void parseUpdate(const LLSD& update);
    void parseMeta(const LLSD& update);
    void parseContent(const LLSD& update);
    void parseUUIDArray(const LLSD& content, const std::string& name, uuid_list_t& ids);
    void parseLink(const LLSD& link_map, S32 depth);
    void parseItem(const LLSD& link_map);
    void parseCategory(const LLSD& link_map, S32 depth);
    void parseDescendentCount(const LLUUID& category_id, LLFolderType::EType type, const LLSD& embedded);
    void parseEmbedded(const LLSD& embedded, S32 depth);
    void parseEmbeddedLinks(const LLSD& links, S32 depth);
    void parseEmbeddedItems(const LLSD& items);
    void parseEmbeddedCategories(const LLSD& categories, S32 depth);
    void parseEmbeddedItem(const LLSD& item);
    void parseEmbeddedCategory(const LLSD& category, S32 depth);

private:
    void checkTimeout() {}
    void clearParseResults()
    {
        mCatDescendentDeltas.clear();
        mCatDescendentsKnown.clear();
        mCatVersionsUpdated.clear();
        mItemsCreated.clear();
        mItemsLost.clear();
        mItemsUpdated.clear();
        mCategoriesCreated.clear();
        mCategoriesUpdated.clear();
        mObjectsDeletedIds.clear();
        mItemIds.clear();
        mCategoryIds.clear();
    }

    typedef std::map<LLUUID, size_t> uuid_int_map_t;
    uuid_int_map_t mCatDescendentDeltas;
    uuid_int_map_t mCatDescendentsKnown;
    uuid_int_map_t mCatVersionsUpdated;
    typedef std::map<LLUUID, LLPointer<LLViewerInventoryItem> > deferred_item_map_t;
    deferred_item_map_t mItemsCreated;
    deferred_item_map_t mItemsLost;
    deferred_item_map_t mItemsUpdated;
    typedef std::map<LLUUID, LLPointer<LLViewerInventoryCategory> > deferred_category_map_t;
    deferred_category_map_t mCategoriesCreated;
    deferred_category_map_t mCategoriesUpdated;
    uuid_list_t mObjectsDeletedIds;
    uuid_list_t mItemIds;
    uuid_list_t mCategoryIds;
    bool mFetch;
    S32 mFetchDepth;
    AISAPI::COMMAND_TYPE mType;
};

void AISUpdateCppParse::parseUpdate(const LLSD& update)
{
    clearParseResults();
    parseMeta(update);
    parseContent(update);
}

void AISUpdateCppParse::parseMeta(const LLSD& update)
{
    // parse _categories_removed -> mObjectsDeletedIds
    uuid_list_t cat_ids;
    parseUUIDArray(update,"_categories_removed",cat_ids);
    for (uuid_list_t::const_iterator it = cat_ids.begin();
         it != cat_ids.end(); ++it)
    {
        LLViewerInventoryCategory *cat = gInventory.getCategory(*it);
        if(cat)
        {
            mCatDescendentDeltas[cat->getParentUUID()]--;
            mObjectsDeletedIds.insert(*it);
        }
        else
        {
            LL_WARNS("Inventory") << "removed category not found " << *it << LL_ENDL;
        }
    }

    // parse _categories_items_removed -> mObjectsDeletedIds
    uuid_list_t item_ids;
    parseUUIDArray(update,"_category_items_removed",item_ids);
    parseUUIDArray(update,"_removed_items",item_ids);
    for (uuid_list_t::const_iterator it = item_ids.begin();
         it != item_ids.end(); ++it)
    {
        LLViewerInventoryItem *item = gInventory.getItem(*it);
        if(item)
        {
            mCatDescendentDeltas[item->getParentUUID()]--;
            mObjectsDeletedIds.insert(*it);
        }
        else
        {
            LL_WARNS("Inventory") << "removed item not found " << *it << LL_ENDL;
        }
    }

    // parse _broken_links_removed -> mObjectsDeletedIds
    uuid_list_t broken_link_ids;
    parseUUIDArray(update,"_broken_links_removed",broken_link_ids);
    for (uuid_list_t::const_iterator it = broken_link_ids.begin();
         it != broken_link_ids.end(); ++it)
    {
        LLViewerInventoryItem *item = gInventory.getItem(*it);
        if(item)
        {
            mCatDescendentDeltas[item->getParentUUID()]--;
            mObjectsDeletedIds.insert(*it);
        }
        else
        {
            LL_WARNS("Inventory") << "broken link not found " << *it << LL_ENDL;
        }
    }

    // parse _created_items
    parseUUIDArray(update,"_created_items",mItemIds);

    // parse _created_categories
    parseUUIDArray(update,"_created_categories",mCategoryIds);

    // Parse updated category versions.
    const std::string& ucv = "_updated_category_versions";
    if (update.has(ucv))
    {
        for(LLSD::map_const_iterator it = update[ucv].beginMap(),
                end = update[ucv].endMap();
            it != end; ++it)
        {
            const LLUUID id((*it).first);
            S32 version = (*it).second.asInteger();
            mCatVersionsUpdated[id] = version;
        }
    }
}

void AISUpdateCppParse::parseContent(const LLSD& update)
{
    // Errors from a fetch request might contain id without
    // full item or folder.
    // Todo: Depending on error we might want to do something,
    // like removing a 404 item or refetching parent folder
    if (update.has("linked_id") && update.has("parent_id"))
    {
        parseLink(update, mFetchDepth);
    }
    else if (update.has("item_id") && update.has("parent_id"))
    {
        parseItem(update);
    }

    if (mType == AISAPI::FETCHCATEGORYSUBSET)
    {
        // initial category is incomplete, don't process it,
        // go for content instead
        if (update.has("_embedded"))
        {
            parseEmbedded(update["_embedded"], mFetchDepth - 1);
        }
    }
    else if (update.has("category_id") && update.has("parent_id"))
    {
        parseCategory(update, mFetchDepth);
    }
    else
    {
        if (update.has("_embedded"))
        {
            parseEmbedded(update["_embedded"], mFetchDepth);
        }
    }
}

void AISUpdateCppParse::parseItem(const LLSD& item_map)
{
    LLUUID item_id = item_map["item_id"].asUUID();
    LLPointer<LLViewerInventoryItem> new_item(new LLViewerInventoryItem);
    LLViewerInventoryItem *curr_item = gInventory.getItem(item_id);
    if (curr_item)
    {
        // Default to current values where not provided.
        new_item->copyViewerItem(curr_item);
    }
    bool rv = new_item->unpackMessage(item_map);
    if (rv)
    {
        if (mFetch)
        {
            mItemsCreated[item_id] = new_item;
            new_item->setComplete(true);

            if (new_item->getParentUUID().isNull())
            {
                mItemsLost[item_id] = new_item;
            }
        }
        else if (curr_item)
        {
            mItemsUpdated[item_id] = new_item;
            // This statement is here to cause a new entry with 0
            // delta to be created if it does not already exist;
            // otherwise has no effect.
            mCatDescendentDeltas[new_item->getParentUUID()];
        }
        else
        {
            mItemsCreated[item_id] = new_item;
            // LLInventoryModel::addItem() refuses an item of no asset type, so
            // the model will not hold it: don't count it as a new descendant
            // (the entry is still created, with a 0 delta, as for an update).
            // getActualType(): the stored type, without the link-following
            // lookup of getType() (a link's own type is never AT_NONE).
            if (new_item->getActualType() > LLAssetType::AT_NONE)
            {
                mCatDescendentDeltas[new_item->getParentUUID()]++;
            }
            else
            {
                mCatDescendentDeltas[new_item->getParentUUID()];
            }
            new_item->setComplete(true);
        }
    }
    else
    {
        // *TODO: Wow, harsh.  Should we just complain and get out?
        LL_ERRS() << "unpack failed" << LL_ENDL;
    }
}

void AISUpdateCppParse::parseLink(const LLSD& link_map, S32 depth)
{
    LLUUID item_id = link_map["item_id"].asUUID();
    LLPointer<LLViewerInventoryItem> new_link(new LLViewerInventoryItem);
    LLViewerInventoryItem *curr_link = gInventory.getItem(item_id);
    if (curr_link)
    {
        // Default to current values where not provided.
        new_link->copyViewerItem(curr_link);
    }
    bool rv = new_link->unpackMessage(link_map);
    if (rv)
    {
        const LLUUID& parent_id = new_link->getParentUUID();
        if (mFetch)
        {
            LLPermissions default_perms;
            default_perms.init(gAgent.getID(), gAgent.getID(), LLUUID::null, LLUUID::null);
            default_perms.initMasks(PERM_NONE, PERM_NONE, PERM_NONE, PERM_NONE, PERM_NONE);
            new_link->setPermissions(default_perms);
            LLSaleInfo default_sale_info;
            new_link->setSaleInfo(default_sale_info);
            //LL_DEBUGS("Inventory") << "creating link from llsd: " << ll_pretty_print_sd(link_map) << LL_ENDL;
            mItemsCreated[item_id] = new_link;
            new_link->setComplete(true);

            if (new_link->getParentUUID().isNull())
            {
                mItemsLost[item_id] = new_link;
            }
        }
        else if (curr_link)
        {
            mItemsUpdated[item_id] = new_link;
            // This statement is here to cause a new entry with 0
            // delta to be created if it does not already exist;
            // otherwise has no effect.
            mCatDescendentDeltas[parent_id];
        }
        else
        {
            LLPermissions default_perms;
            default_perms.init(gAgent.getID(),gAgent.getID(),LLUUID::null,LLUUID::null);
            default_perms.initMasks(PERM_NONE,PERM_NONE,PERM_NONE,PERM_NONE,PERM_NONE);
            new_link->setPermissions(default_perms);
            LLSaleInfo default_sale_info;
            new_link->setSaleInfo(default_sale_info);
            //LL_DEBUGS("Inventory") << "creating link from llsd: " << ll_pretty_print_sd(link_map) << LL_ENDL;
            mItemsCreated[item_id] = new_link;
            mCatDescendentDeltas[parent_id]++;
            new_link->setComplete(true);
        }

        if (link_map.has("_embedded"))
        {
            parseEmbedded(link_map["_embedded"], depth);
        }
    }
    else
    {
        // *TODO: Wow, harsh.  Should we just complain and get out?
        LL_ERRS() << "unpack failed" << LL_ENDL;
    }
}


void AISUpdateCppParse::parseCategory(const LLSD& category_map, S32 depth)
{
    LLUUID category_id = category_map["category_id"].asUUID();
    S32 version = LLViewerInventoryCategory::VERSION_UNKNOWN;

    if (category_map.has("version"))
    {
        version = category_map["version"].asInteger();
    }

    LLViewerInventoryCategory *curr_cat = gInventory.getCategory(category_id);

    if (curr_cat
        && curr_cat->getVersion() > LLViewerInventoryCategory::VERSION_UNKNOWN
        && curr_cat->getDescendentCount() != LLViewerInventoryCategory::DESCENDENT_COUNT_UNKNOWN
        && version > LLViewerInventoryCategory::VERSION_UNKNOWN
        && version < curr_cat->getVersion())
    {
        LL_WARNS() << "Got stale folder, known: " << curr_cat->getVersion()
            << ", received: " << version << LL_ENDL;
        // <FS:Beq> FIRE-33337 workaround for rename issue until proper fix is in place and tested
        // Also servers a general handler for version de-sync bugs in the future.
        if( version < curr_cat->getVersion() )
        {
            // AIS version is considered canonical, so we need to refetch
            curr_cat->setVersion(LLViewerInventoryCategory::VERSION_UNKNOWN);
            curr_cat->fetch();
        }
        // </FS:Beq>
        return;
    }

    LLPointer<LLViewerInventoryCategory> new_cat;
    if (curr_cat)
    {
        // Default to current values where not provided.
        new_cat = new LLViewerInventoryCategory(curr_cat);
    }
    else
    {
        if (category_map.has("agent_id"))
        {
            new_cat = new LLViewerInventoryCategory(category_map["agent_id"].asUUID());
        }
        else
        {
            LL_DEBUGS() << "No owner provided, folder might be assigned wrong owner" << LL_ENDL;
            new_cat = new LLViewerInventoryCategory(LLUUID::null);
        }
    }
    bool rv = new_cat->unpackMessage(category_map);
    // *NOTE: unpackMessage does not unpack version or descendent count.
    if (rv)
    {
        // Check descendent count first, as it may be needed
        // to populate newly created categories
        if (category_map.has("_embedded"))
        {
            parseDescendentCount(category_id, new_cat->getPreferredType(), category_map["_embedded"]);
        }

        if (mFetch)
        {
            uuid_int_map_t::const_iterator lookup_it = mCatDescendentsKnown.find(category_id);
            if (mCatDescendentsKnown.end() != lookup_it)
            {
                S32 descendent_count = static_cast<S32>(lookup_it->second);
                LL_DEBUGS("Inventory") << "Setting descendents count to " << descendent_count
                    << " for category " << category_id << LL_ENDL;
                new_cat->setDescendentCount(descendent_count);

                // set version only if we are sure this update has full data and embeded items
                // since viewer uses version to decide if folder and content still need fetching
                if (version > LLViewerInventoryCategory::VERSION_UNKNOWN
                    && depth >= 0)
                {
                    if (curr_cat && curr_cat->getVersion() > version)
                    {
                        LL_WARNS("Inventory") << "Version was " << curr_cat->getVersion()
                            << ", but fetch returned version " << version
                            << " for category " << category_id << LL_ENDL;
                    }
                    else
                    {
                        LL_DEBUGS("Inventory") << "Setting version to " << version
                            << " for category " << category_id << LL_ENDL;
                    }

                    new_cat->setVersion(version);
                }
            }
            else if (curr_cat
                     && curr_cat->getVersion() > LLViewerInventoryCategory::VERSION_UNKNOWN
                     && version > curr_cat->getVersion())
            {
                LL_DEBUGS("Inventory") << "Category " << category_id
                    << " is stale. Known version: " << curr_cat->getVersion()
                    << " server version: " << version << LL_ENDL;
            }
            mCategoriesCreated[category_id] = new_cat;
        }
        else if (curr_cat)
        {
            mCategoriesUpdated[category_id] = new_cat;
            // This statement is here to cause a new entry with 0
            // delta to be created if it does not already exist;
            // otherwise has no effect.
            mCatDescendentDeltas[new_cat->getParentUUID()];
            // Capture update for the category itself as well.
            mCatDescendentDeltas[category_id];
        }
        else
        {
            // Set version/descendents for newly created categories.
            uuid_int_map_t::const_iterator lookup_it = mCatDescendentsKnown.find(category_id);
            if (mCatDescendentsKnown.end() != lookup_it)
            {
                S32 descendent_count = static_cast<S32>(lookup_it->second);
                LL_DEBUGS("Inventory") << "Setting descendents count to " << descendent_count
                    << " for new category " << category_id << LL_ENDL;
                new_cat->setDescendentCount(descendent_count);

                // Don't set version unles correct children count is present
                if (category_map.has("version"))
                {
                    S32 version = category_map["version"].asInteger();
                    LL_DEBUGS("Inventory") << "Setting version to " << version
                        << " for new category " << category_id << LL_ENDL;
                    new_cat->setVersion(version);
                }
            }
            mCategoriesCreated[category_id] = new_cat;
            mCatDescendentDeltas[new_cat->getParentUUID()]++;
        }
    }
    else
    {
        // *TODO: Wow, harsh.  Should we just complain and get out?
        LL_ERRS() << "unpack failed" << LL_ENDL;
    }

    // Check for more embedded content.
    if (category_map.has("_embedded"))
    {
        parseEmbedded(category_map["_embedded"], depth - 1);
    }
}

void AISUpdateCppParse::parseDescendentCount(const LLUUID& category_id, LLFolderType::EType type, const LLSD& embedded)
{
    // We can only determine true descendent count if this contains all descendent types.
    if (embedded.has("categories") &&
        embedded.has("links") &&
        embedded.has("items"))
    {
        mCatDescendentsKnown[category_id] = embedded["categories"].size();
        mCatDescendentsKnown[category_id] += embedded["links"].size();
        mCatDescendentsKnown[category_id] += embedded["items"].size();
    }
    else if (mFetch && embedded.has("links") && (type == LLFolderType::FT_CURRENT_OUTFIT || type == LLFolderType::FT_OUTFIT))
    {
        // COF and outfits contain links only
        mCatDescendentsKnown[category_id] = embedded["links"].size();
    }
}

void AISUpdateCppParse::parseEmbedded(const LLSD& embedded, S32 depth)
{
    checkTimeout();

    if (embedded.has("links")) // _embedded in a category
    {
        parseEmbeddedLinks(embedded["links"], depth);
    }
    if (embedded.has("items")) // _embedded in a category
    {
        parseEmbeddedItems(embedded["items"]);
    }
    if (embedded.has("item")) // _embedded in a link
    {
        parseEmbeddedItem(embedded["item"]);
    }
    if (embedded.has("categories")) // _embedded in a category
    {
        parseEmbeddedCategories(embedded["categories"], depth);
    }
    if (embedded.has("category")) // _embedded in a link
    {
        parseEmbeddedCategory(embedded["category"], depth);
    }
}

void AISUpdateCppParse::parseUUIDArray(const LLSD& content, const std::string& name, uuid_list_t& ids)
{
    if (content.has(name))
    {
        for(LLSD::array_const_iterator it = content[name].beginArray(),
                end = content[name].endArray();
                it != end; ++it)
        {
            ids.insert((*it).asUUID());
        }
    }
}

void AISUpdateCppParse::parseEmbeddedLinks(const LLSD& links, S32 depth)
{
    for(LLSD::map_const_iterator linkit = links.beginMap(),
            linkend = links.endMap();
        linkit != linkend; ++linkit)
    {
        const LLUUID link_id((*linkit).first);
        const LLSD& link_map = (*linkit).second;
        if (!mFetch && mItemIds.end() == mItemIds.find(link_id))
        {
            LL_DEBUGS("Inventory") << "Ignoring link not in items list " << link_id << LL_ENDL;
        }
        else
        {
            parseLink(link_map, depth);
        }
    }
}

void AISUpdateCppParse::parseEmbeddedItem(const LLSD& item)
{
    // a single item (_embedded in a link)
    if (item.has("item_id"))
    {
        if (mFetch || mItemIds.end() != mItemIds.find(item["item_id"].asUUID()))
        {
            parseItem(item);
        }
    }
}

void AISUpdateCppParse::parseEmbeddedItems(const LLSD& items)
{
    // a map of items (_embedded in a category)
    for(LLSD::map_const_iterator itemit = items.beginMap(),
            itemend = items.endMap();
        itemit != itemend; ++itemit)
    {
        const LLUUID item_id((*itemit).first);
        const LLSD& item_map = (*itemit).second;
        if (!mFetch && mItemIds.end() == mItemIds.find(item_id))
        {
            LL_DEBUGS("Inventory") << "Ignoring item not in items list " << item_id << LL_ENDL;
        }
        else
        {
            parseItem(item_map);
        }
    }
}

void AISUpdateCppParse::parseEmbeddedCategory(const LLSD& category, S32 depth)
{
    // a single category (_embedded in a link)
    if (category.has("category_id"))
    {
        if (mFetch || mCategoryIds.end() != mCategoryIds.find(category["category_id"].asUUID()))
        {
            parseCategory(category, depth);
        }
    }
}

void AISUpdateCppParse::parseEmbeddedCategories(const LLSD& categories, S32 depth)
{
    // a map of categories (_embedded in a category)
    for(LLSD::map_const_iterator categoryit = categories.beginMap(),
            categoryend = categories.endMap();
        categoryit != categoryend; ++categoryit)
    {
        const LLUUID category_id((*categoryit).first);
        const LLSD& category_map = (*categoryit).second;
        if (!mFetch && mCategoryIds.end() == mCategoryIds.find(category_id))
        {
            LL_DEBUGS("Inventory") << "Ignoring category not in categories list " << category_id << LL_ENDL;
        }
        else
        {
            parseCategory(category_map, depth);
        }
    }
}

