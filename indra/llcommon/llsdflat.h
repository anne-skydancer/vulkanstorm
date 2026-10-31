/**
 * @file llsdflat.h
 * @brief LLSD values in and out of the vkstorm-ffi calls that take or
 *        return them (VksLlsdFlat, VksLlsdDoc).
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * $/LicenseInfo$
 */

#ifndef LL_LLSDFLAT_H
#define LL_LLSDFLAT_H

#include "llsd.h"
#include "vkstorm_ffi.h"

#include <vector>

// An LLSD value as a VksLlsdFlat: every value as it is, nothing coerced, so
// the Rust side applies the coercions itself. The inverse of
// ll_sd_from_vks_doc. The buffer belongs to this object; keep it alive for
// the call. An undefined value is an empty buffer.
class LL_COMMON_API LLSDFlat
{
public:
    explicit LLSDFlat(const LLSD& sd);
    LLSDFlat(const LLSDFlat&) = delete;
    LLSDFlat& operator=(const LLSDFlat&) = delete;

    const VksLlsdFlat* get() const { return &mFlat; }

private:
    void add(const LLSD& sd);
    void addNode(uint8_t kind, uint32_t len, uint64_t payload);
    void addBytes(uint8_t kind, const void* data, size_t len);

    std::vector<VksLlsdNode> mNodes;
    std::vector<uint8_t> mBytes;
    VksLlsdFlat mFlat;
};

// The value of a document a vks_* call returned, built as a parse result is
// (map keys are unique there), and the document freed. Undefined for null.
LL_COMMON_API LLSD ll_sd_from_vks_doc(VksLlsdDoc* doc);

#endif // LL_LLSDFLAT_H
