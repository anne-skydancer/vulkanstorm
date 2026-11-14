/**
 * @file llinventorymodel_parity_frozen.h
 * @brief The C++ cache codec of LLInventoryModel before sub-rung R2 (tests
 *        only; see llinventorymodel_parity_frozen.cpp).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, version 3 of the License or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See LICENSE and
 * NOTICE.md in the source tree.
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
