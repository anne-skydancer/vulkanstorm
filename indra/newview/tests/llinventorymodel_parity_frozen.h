/**
 * @file llinventorymodel_parity_frozen.h
 * @brief The C++ cache codec of LLInventoryModel before sub-rung R2 (tests
 *        only; see llinventorymodel_parity_frozen.cpp).
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#ifndef LL_LLINVENTORYMODEL_PARITY_FROZEN_H
#define LL_LLINVENTORYMODEL_PARITY_FROZEN_H

#include "../llinventorymodel.h"

namespace vks_inv_frozen_cache
{
bool load_from_file(const std::string& filename,
                    LLInventoryModel::cat_array_t& categories,
                    LLInventoryModel::item_array_t& items,
                    LLInventoryModel::changed_items_t& cats_to_update,
                    bool& is_cache_obsolete);
bool save_to_file(const std::string& filename,
                  const LLInventoryModel::cat_array_t& categories,
                  const LLInventoryModel::item_array_t& items);
}

#endif
