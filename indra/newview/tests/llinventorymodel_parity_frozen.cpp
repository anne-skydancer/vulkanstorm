/**
 * @file llinventorymodel_parity_frozen.cpp
 * @brief The C++ cache codec of LLInventoryModel before sub-rung R2 (tests
 *        only): the bench baseline.
 *
 * LLInventoryModel::loadFromFile and saveToFile as they were before the
 * cache file moved to Rust (vkstorm-rust 3846816a16, llinventorymodel.cpp
 * blob ebae977f703a1c5d8e8e4d6ea2b607177be30179, lines 3710-3925), as free
 * functions: `LLInventoryModel::` dropped from the names and the array
 * types, sCurrentInvCacheVersion read as CACHE_VERSION. Only the bench of
 * INTEGRATION_TEST_llinventorymodel_parity (VKS_INVMODEL_BENCH) calls them,
 * to time the C++ against the Rust-backed live functions in one binary; the
 * fixtures, recorded from this C++, are the parity oracle. They are not
 * kept in step with the live source (check_r2_copies.py does not read
 * them): the live functions now call Rust.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2014, Linden Research, Inc.
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#include "../llviewerprecompiledheaders.h"

#include "../llinventorymodel.h"
#include "../llviewerinventory.h"
#include "llsdserialize.h"
#include "llpermissions.h"

#include "llinventorymodel_parity_frozen.h"

namespace vks_inv_frozen_cache
{
typedef LLInventoryModel::cat_array_t cat_array_t;
typedef LLInventoryModel::item_array_t item_array_t;
typedef LLInventoryModel::changed_items_t changed_items_t;
static const S32 CACHE_VERSION = 5;
static const char* const LOG_INV("Inventory");

bool load_from_file(const std::string& filename,
                                    cat_array_t& categories,
                                    item_array_t& items,
                                    changed_items_t& cats_to_update,
                                    bool &is_cache_obsolete)
{
    LL_PROFILE_ZONE_NAMED("inventory load from file");

    if(filename.empty())
    {
        LL_ERRS(LOG_INV) << "filename is Null!" << LL_ENDL;
        return false;
    }
    LL_INFOS(LOG_INV) << "loading inventory from: (" << filename << ")" << LL_ENDL;

    llifstream file(filename.c_str(), std::ifstream::in | std::ifstream::binary);

    if (!file.is_open())
    {
        LL_INFOS(LOG_INV) << "unable to load inventory from: " << filename << LL_ENDL;
        return false;
    }

    is_cache_obsolete = true; // Obsolete until proven current
    U32 value_nbo = 0;
    file.read((char*)&value_nbo, sizeof(U32));
    if (file.fail())
    {
        LL_WARNS(LOG_INV) << "Failed to read cache version. Unable to load inventory from: " << filename << LL_ENDL;
    }
    else
    {
        S32 version = (S32)ntohl(value_nbo);
        if (version == CACHE_VERSION)
        {
            // Cache is up to date
            is_cache_obsolete = false;
        }
        else
        {
            LL_WARNS(LOG_INV) << "Inventory cache is out of date" << LL_ENDL;
        }
    }

    LLSD inventory;
    if (!is_cache_obsolete)
    {
        LL_PROFILE_ZONE_NAMED("inventory load from file - llsd parse");
        LLPointer<LLSDParser> parser = new LLSDBinaryParser();

        if (parser->parse(file, inventory, LLSDSerialize::SIZE_UNLIMITED) == LLSDParser::PARSE_FAILURE)
        {
            is_cache_obsolete = true;
            LL_WARNS(LOG_INV) << "Parsing inventory cache failed" << LL_ENDL;
        }
    }

    if (!is_cache_obsolete)
    {
        {
            LL_PROFILE_ZONE_NAMED("inventory load from file - categories");
            const LLSD& llsd_cats = inventory["categories"];
            if (llsd_cats.isArray())
            {
                LLSD::array_const_iterator iter = llsd_cats.beginArray();
                LLSD::array_const_iterator end  = llsd_cats.endArray();
                for (; iter != end; ++iter)
                {
                    LLPointer<LLViewerInventoryCategory> inv_cat = new LLViewerInventoryCategory(LLUUID::null);
                    if (inv_cat->importLLSDMap(*iter))
                    {
                        categories.push_back(inv_cat);
                    }
                }
            }
        }

        {
            LL_PROFILE_ZONE_NAMED("inventory load from file - items");
            const LLSD& llsd_items = inventory["items"];
            if (llsd_items.isArray())
            {
                LLSD::array_const_iterator iter = llsd_items.beginArray();
                LLSD::array_const_iterator end  = llsd_items.endArray();
                for (; iter != end; ++iter)
                {
                    LLPointer<LLViewerInventoryItem> inv_item = new LLViewerInventoryItem;
                    if (inv_item->fromLLSD(*iter))
                    {
                        if (inv_item->getUUID().isNull())
                        {
                            LL_DEBUGS(LOG_INV) << "Ignoring inventory with null item id: " << inv_item->getName() << LL_ENDL;
                        }
                        else
                        {
                            if (inv_item->getType() == LLAssetType::AT_UNKNOWN)
                            {
                                cats_to_update.insert(inv_item->getParentUUID());
                            }
                            else
                            {
                                items.push_back(inv_item);
                            }
                        }
                    }

                    //      TODO(brad) - figure out how to reenable this without breaking everything else
                    //      static constexpr U64 BATCH_SIZE = 512U;
                    //      if ((++lines_count % BATCH_SIZE) == 0)
                    //      {
                    //          // SL-19968 - make sure message system code gets a chance to run every so often
                    //          pump_idle_startup_network();
                    //      }
                }
            }
        }
    }

    file.close();

    return !is_cache_obsolete;
}

bool save_to_file(const std::string& filename,
    const cat_array_t& categories,
    const item_array_t& items)
{
    if (filename.empty())
    {
        LL_ERRS(LOG_INV) << "Filename is Null!" << LL_ENDL;
        return false;
    }

    LL_INFOS(LOG_INV) << "saving inventory to: (" << filename << ")" << LL_ENDL;

    try
    {
        llofstream fileSD(filename.c_str(), std::ios_base::out | std::ios_base::binary);
        if (!fileSD.is_open())
        {
            LL_WARNS(LOG_INV) << "Failed to open file. Unable to save inventory to: " << filename << LL_ENDL;
            return false;
        }
        U32 value_nbo = htonl(CACHE_VERSION);
        fileSD.write((const char*)(&value_nbo), sizeof(U32));
        if (fileSD.fail())
        {
            LL_WARNS(LOG_INV) << "Failed to write cache. Unable to save inventory to: " << filename << LL_ENDL;
            return false;
        }

        LLSD inventory;
        inventory["categories"] = LLSD::emptyArray();
        LLSD& cat_array = inventory["categories"];

        S32 cat_count = 0;
        for (auto& cat : categories)
        {
            if (cat.isNull())
            {
                LL_WARNS(LOG_INV) << "Skipping null category during inventory save" << LL_ENDL;
                continue;
            }
            if (cat->getVersion() != LLViewerInventoryCategory::VERSION_UNKNOWN)
            {
                LLSD sd;
                cat->exportLLSD(sd);
                cat_array.append(sd);
                cat_count++;
            }
        }

        inventory["items"] = LLSD::emptyArray();
        LLSD& item_array = inventory["items"];
        auto it_count = items.size();
        for (auto& item : items)
        {
            if (item.isNull())
            {
                LL_WARNS(LOG_INV) << "Skipping null item during inventory save" << LL_ENDL;
                continue;
            }
            LLSD sd;
            item->asLLSD(sd);
            item_array.append(sd);
        }
        fileSD << LLSDOStreamer<LLSDBinaryFormatter>(inventory) << std::endl;
        if (fileSD.fail())
        {
            LL_WARNS(LOG_INV) << "Failed to write cache. Unable to save inventory to: " << filename << LL_ENDL;
            return false;
        }
        fileSD.flush();

        fileSD.close();

        LL_INFOS(LOG_INV) << "Inventory saved: " << (S32)cat_count << " categories, " << (S32)it_count << " items." << LL_ENDL;
    }
    catch(std::bad_alloc&)
    {
        // We are quiting, so just log an error and move on.
        LL_WARNS(LOG_INV) << "Failed to save inventory to cache due to memory allocation failure." << LL_ENDL;
        return false;
    }
    catch (...)
    {
        LOG_UNHANDLED_EXCEPTION("");
        LL_INFOS(LOG_INV) << "Failed to save inventory to: (" << filename << ")" << LL_ENDL;
        return false;
    }

    return true;
}
} // namespace vks_inv_frozen_cache
