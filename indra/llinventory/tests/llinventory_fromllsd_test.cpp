/**
 * @file llinventory_fromllsd_test.cpp
 * @brief LLInventoryItem::fromLLSD() against the original key-by-key loop.
 *
 * #5881 replaced the if-chain in fromLLSD() with a switch on key length.
 * reference_from_llsd() below is the original if-chain, kept as the oracle;
 * every key (runtime included) must decode the same way in both.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "linden_common.h"
#include "llinventory.h"
#include "llinventorytype.h"
#include "llassettype.h"
#include "llsdutil.h"
#include "llxorcipher.h"
#include "../test/lltut.h"

namespace
{
const LLUUID k_magic("3c115e51-04f4-523c-9fa6-98aff1034730");

struct Decoded
{
    LLUUID id, parent, thumb, asset;
    bool favorite = false;
    LLPermissions perms;
    LLSaleInfo sale;
    LLAssetType::EType type = LLAssetType::AT_NONE;
    LLInventoryType::EType inv_type = LLInventoryType::IT_NONE;
    U32 flags = 0;
    std::string name, desc, runtime;
    time_t created = 0;
    bool ok = true;
};

// The if-chain exactly as it was before #5881.
Decoded reference_from_llsd(const LLSD& sd)
{
    Decoded d;
    d.perms.init(LLUUID::null, LLUUID::null, LLUUID::null, LLUUID::null);
    if (sd.has("script") || (sd.has("metadata") && sd["metadata"].has("script")))
    {
        d.runtime = sd.has("script") ? sd["script"]["runtime"].asString()
                                     : sd["metadata"]["script"]["runtime"].asString();
    }
    for (LLSD::map_const_iterator i = sd.beginMap(); i != sd.endMap(); ++i)
    {
        if (i->first == "item_id") { d.id = i->second; continue; }
        if (i->first == "parent_id") { d.parent = i->second; continue; }
        if (i->first == "thumbnail")
        {
            const LLSD& m = i->second;
            if (m.has("asset_id")) { d.thumb = m["asset_id"]; }
            continue;
        }
        if (i->first == "thumbnail_id") { d.thumb = i->second.asUUID(); continue; }
        if (i->first == "favorite")
        {
            const LLSD& m = i->second;
            if (m.has("toggled")) { d.favorite = m["toggled"].asBoolean(); }
            continue;
        }
        if (i->first == "permissions") { d.perms.importLLSD(i->second); continue; }
        if (i->first == "sale_info")
        {
            bool has_mask = false;
            U32 mask = 0;
            if (!d.sale.fromLLSD(i->second, has_mask, mask)) { d.ok = false; return d; }
            if (has_mask)
            {
                if (mask == PERM_NONE) { mask = d.perms.getMaskOwner(); }
                if (!(mask & PERM_COPY)) { mask |= PERM_TRANSFER; }
                d.perms.setMaskNext(mask);
            }
            continue;
        }
        if (i->first == "shadow_id")
        {
            d.asset = i->second;
            LLXORCipher cipher(k_magic.mData, UUID_BYTES);
            cipher.decrypt(d.asset.mData, UUID_BYTES);
            continue;
        }
        if (i->first == "asset_id") { d.asset = i->second; continue; }
        if (i->first == "linked_id") { d.asset = i->second; continue; }
        if (i->first == "type")
        {
            const LLSD& l = i->second;
            if (l.isString()) { d.type = LLAssetType::lookup(l.asStringRef().c_str()); }
            else if (l.isInteger()) { S8 t = (U8)l.asInteger(); d.type = static_cast<LLAssetType::EType>(t); }
            continue;
        }
        if (i->first == "inv_type")
        {
            const LLSD& l = i->second;
            if (l.isString()) { d.inv_type = LLInventoryType::lookup(l.asStringRef().c_str()); }
            else if (l.isInteger()) { S8 t = (U8)l.asInteger(); d.inv_type = static_cast<LLInventoryType::EType>(t); }
            continue;
        }
        if (i->first == "flags")
        {
            const LLSD& l = i->second;
            if (l.isBinary()) { d.flags = ll_U32_from_sd(l); }
            else if (l.isInteger()) { d.flags = l.asInteger(); }
            continue;
        }
        if (i->first == "name")
        {
            d.name = i->second.asString();
            LLStringUtil::replaceNonstandardASCII(d.name, ' ');
            LLStringUtil::replaceChar(d.name, '|', ' ');
            continue;
        }
        if (i->first == "desc")
        {
            d.desc = i->second.asString();
            LLStringUtil::replaceNonstandardASCII(d.desc, ' ');
            continue;
        }
        if (i->first == "created_at") { d.created = i->second.asInteger(); continue; }
    }
    if (d.inv_type == LLInventoryType::IT_NONE || !inventory_and_asset_types_match(d.inv_type, d.type))
    {
        d.inv_type = LLInventoryType::defaultForAssetType(d.type);
    }
    d.perms.initMasks(d.inv_type);
    return d;
}
}

namespace tut
{
struct fromllsd_data
{
    // Compare a fresh item decoded by fromLLSD() with the reference decoding.
    void check(const std::string& label, const LLSD& sd)
    {
        LLPointer<LLInventoryItem> item = new LLInventoryItem;
        bool ok = item->fromLLSD(sd);
        Decoded d = reference_from_llsd(sd);
        ensure_equals(label + ": result", ok, d.ok);
        if (!d.ok)
        {
            return;
        }
        ensure_equals(label + ": item_id", item->getUUID(), d.id);
        ensure_equals(label + ": parent_id", item->getParentUUID(), d.parent);
        ensure_equals(label + ": thumbnail", item->getThumbnailUUID(), d.thumb);
        ensure_equals(label + ": favorite", item->getIsFavorite(), d.favorite);
        ensure(label + ": permissions", item->getPermissions() == d.perms);
        ensure(label + ": sale_info", item->getSaleInfo() == d.sale);
        ensure_equals(label + ": asset", item->getAssetUUID(), d.asset);
        ensure_equals(label + ": type", (S32)item->getType(), (S32)d.type);
        ensure_equals(label + ": inv_type", (S32)item->getInventoryType(), (S32)d.inv_type);
        ensure_equals(label + ": flags", item->getFlags(), d.flags);
        ensure_equals(label + ": name", item->getName(), d.name);
        ensure_equals(label + ": desc", item->getDescription(), d.desc);
        ensure_equals(label + ": created_at", (S64)item->getCreationDate(), (S64)d.created);
        ensure_equals(label + ": runtime", item->getRuntime(), d.runtime);
    }

    static LLSD full_item()
    {
        LLSD sd;
        sd["item_id"] = LLUUID("11111111-1111-1111-1111-111111111111");
        sd["parent_id"] = LLUUID("22222222-2222-2222-2222-222222222222");
        sd["thumbnail"]["asset_id"] = LLUUID("33333333-3333-3333-3333-333333333333");
        sd["favorite"]["toggled"] = true;
        sd["permissions"]["creator_id"] = LLUUID("44444444-4444-4444-4444-444444444444");
        sd["permissions"]["owner_id"] = LLUUID("55555555-5555-5555-5555-555555555555");
        sd["permissions"]["group_id"] = LLUUID::null;
        sd["permissions"]["last_owner_id"] = LLUUID::null;
        sd["permissions"]["base_mask"] = (S32)0x7fffffff;
        sd["permissions"]["owner_mask"] = (S32)0x7fffffff;
        sd["permissions"]["group_mask"] = (S32)0;
        sd["permissions"]["everyone_mask"] = (S32)0;
        sd["permissions"]["next_owner_mask"] = (S32)0x00082000;
        sd["sale_info"]["sale_type"] = "not";
        sd["sale_info"]["sale_price"] = (S32)10;
        sd["asset_id"] = LLUUID("66666666-6666-6666-6666-666666666666");
        sd["type"] = "lsltext";
        sd["inv_type"] = "script";
        sd["flags"] = (S32)1;
        sd["name"] = "A|name here";
        sd["desc"] = "desc|kept";
        sd["created_at"] = (S32)1700000000;
        sd["script"]["runtime"] = "luau";
        return sd;
    }
};
typedef test_group<fromllsd_data> fromllsd_group;
typedef fromllsd_group::object fromllsd_test;
fromllsd_group fromllsd_tests("LLInventoryItem fromLLSD vs reference loop");

template<> template<> void fromllsd_test::test<1>()
{
    LLSD sd = full_item();
    check("all keys", sd);

    // Absolute values, so the test cannot pass with both sides decoding nothing.
    LLPointer<LLInventoryItem> item = new LLInventoryItem;
    ensure("decode ok", item->fromLLSD(sd));
    ensure_equals("abs name", item->getName(), std::string("A name here"));
    ensure_equals("abs asset", item->getAssetUUID(), LLUUID("66666666-6666-6666-6666-666666666666"));
    ensure_equals("abs flags", item->getFlags(), (U32)1);
    ensure("abs favorite", item->getIsFavorite());
    ensure_equals("abs runtime", item->getRuntime(), std::string("luau"));
}

template<> template<> void fromllsd_test::test<2>()
{
    // One key at a time: every key, alone, decodes the same way.
    LLSD all = full_item();
    for (LLSD::map_const_iterator i = all.beginMap(); i != all.endMap(); ++i)
    {
        LLSD one;
        one[i->first] = i->second;
        check("only " + i->first, one);
    }
}

template<> template<> void fromllsd_test::test<3>()
{
    // Alternative encodings: integer types, binary flags, shadow_id, linked_id,
    // thumbnail_id, runtime under metadata, and an item with no inv_type.
    LLSD sd = full_item();
    sd["type"] = (S32)LLAssetType::AT_NOTECARD;
    sd["inv_type"] = (S32)LLInventoryType::IT_NOTECARD;
    sd["flags"] = ll_sd_from_U32(0x02000001);
    check("integer types, binary flags", sd);

    sd.erase("asset_id");
    sd["shadow_id"] = LLUUID("77777777-7777-7777-7777-777777777777");
    check("shadow_id", sd);

    sd.erase("shadow_id");
    sd["linked_id"] = LLUUID("88888888-8888-8888-8888-888888888888");
    check("linked_id", sd);

    sd.erase("thumbnail");
    sd["thumbnail_id"] = LLUUID("99999999-9999-9999-9999-999999999999");
    check("thumbnail_id", sd);

    sd.erase("inv_type");
    check("no inv_type", sd);

    LLSD wrapped = full_item();
    wrapped["metadata"]["script"] = wrapped["script"];
    wrapped.erase("script");
    check("runtime under metadata", wrapped);
}

template<> template<> void fromllsd_test::test<4>()
{
    // Unknown keys of the same lengths as real keys must be ignored, and
    // near misses ("script", "cat_id", "preferred_type") must not leak in.
    LLSD sd = full_item();
    sd["namx"] = "wrong";
    sd["flagz"] = (S32)99;
    sd["item_ix"] = LLUUID("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa");
    sd["asset_ix"] = LLUUID("bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb");
    sd["parent_ix"] = LLUUID("cccccccc-cccc-cccc-cccc-cccccccccccc");
    sd["created_ax"] = (S32)5;
    sd["permissionz"] = LLSD();
    sd["thumbnail_ix"] = LLUUID("dddddddd-dddd-dddd-dddd-dddddddddddd");
    sd["cat_id"] = LLUUID("eeeeeeee-eeee-eeee-eeee-eeeeeeeeeeee");
    sd["preferred_type"] = "none";
    sd["a_very_long_unknown_key_name"] = (S32)1;
    check("unknown keys", sd);
}

template<> template<> void fromllsd_test::test<5>()
{
    // An unknown sale_type string does not fail the decode (LLSaleInfo::fromLLSD
    // always succeeds) and decodes to FS_NOT, in both.
    LLSD sd = full_item();
    sd["sale_info"]["sale_type"] = "bogus";
    check("unknown sale_type", sd);

    LLPointer<LLInventoryItem> item = new LLInventoryItem;
    ensure("decode ok", item->fromLLSD(sd));
    ensure_equals("sale type", (S32)item->getSaleInfo().getSaleType(), (S32)LLSaleInfo::FS_NOT);
    ensure_equals("sale price", item->getSaleInfo().getSalePrice(), (S32)10);
}
}
