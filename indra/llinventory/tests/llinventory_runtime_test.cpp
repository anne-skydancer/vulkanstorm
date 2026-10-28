/** Tests for independent SLua source-language and runtime metadata. */
#include "linden_common.h"
#include "llinventory.h"
#include "../test/lltut.h"
#include <sstream>

namespace tut
{
struct script_inventory_data
{
    LLPointer<LLInventoryItem> item = new LLInventoryItem;

    LLSD metadata(U8 subtype, const std::string& runtime)
    {
        LLSD sd;
        sd["type"] = "lsltext";
        sd["inv_type"] = "script";
        sd["flags"] = subtype;
        sd["script"]["runtime"] = runtime;
        return sd;
    }
};
using script_inventory_group = test_group<script_inventory_data>;
using script_inventory_test = script_inventory_group::object;
script_inventory_group script_inventory_tests("Script inventory metadata");

template<> template<> void script_inventory_test::test<1>()
{
    ensure("SLua metadata accepted", item->fromLLSD(metadata(SST_LUA, "luau")));
    ensure_equals("SLua source subtype", item->getInventorySubType(), U8(SST_LUA));
    ensure_equals("SLua runtime", item->getRuntime(), std::string("luau"));
    LLPointer<LLInventoryItem> copy = new LLInventoryItem;
    ensure("serialized metadata accepted", copy->fromLLSD(item->asLLSD()));
    ensure_equals("runtime survives inventory cache", copy->getRuntime(), std::string("luau"));
    ensure_equals("subtype survives inventory cache", copy->getInventorySubType(), U8(SST_LUA));
}

template<> template<> void script_inventory_test::test<2>()
{
    item->fromLLSD(metadata(SST_LSL, "luau"));
    ensure_equals("LSL source stays LSL on Luau VM", item->getInventorySubType(), U8(SST_LSL));
    ensure_equals("LSL Luau runtime retained", item->getRuntime(), std::string("luau"));
}

template<> template<> void script_inventory_test::test<3>()
{
    LLSD wrapped = metadata(SST_LUA, "luau");
    wrapped["metadata"]["script"] = wrapped["script"];
    wrapped.erase("script");
    ensure("non-AIS metadata accepted", item->fromLLSD(wrapped));
    ensure_equals("wrapped runtime decoded", item->getRuntime(), std::string("luau"));
    wrapped.erase("metadata");
    item->fromLLSD(wrapped);
    ensure("absent metadata clears previous runtime", item->getRuntime().empty());
}

template<> template<> void script_inventory_test::test<4>()
{
    item->fromLLSD(metadata(SST_LUA, "luau"));
    LLPointer<LLInventoryItem> copy = new LLInventoryItem;
    copy->copyItem(item);
    ensure_equals("copy retains runtime", copy->getRuntime(), std::string("luau"));
    copy->setType(LLAssetType::AT_NOTECARD);
    ensure("changing asset type clears runtime", copy->getRuntime().empty());
}

template<> template<> void script_inventory_test::test<5>()
{
    item->setFlags(0x02000000);
    item->setInventorySubType(SST_LUA);
    ensure_equals("subtype retains other inventory flags", item->getFlags(), U32(0x02000001));
    item->setInventorySubType(SST_LSL);
    ensure_equals("clearing subtype retains other flags", item->getFlags(), U32(0x02000000));
}

template<> template<> void script_inventory_test::test<6>()
{
    item->fromLLSD(metadata(SST_LUA, "luau"));
    std::stringstream stream;
    ensure("legacy export succeeds", item->exportLegacyStream(stream, true));
    LLPointer<LLInventoryItem> copy = new LLInventoryItem;
    ensure("legacy import succeeds", copy->importLegacyStream(stream));
    ensure_equals("legacy task inventory retains runtime", copy->getRuntime(), std::string("luau"));
    ensure_equals("legacy task inventory retains source language", copy->getInventorySubType(), U8(SST_LUA));
}

template<> template<> void script_inventory_test::test<7>()
{
    item->fromLLSD(metadata(SST_LSL, "luau"));
    LLSD update;
    update["name"] = "Renamed script";
    ensure("partial name accepted", item->fromLLSD(update, false));
    ensure_equals("partial name keeps runtime", item->getRuntime(), std::string("luau"));
    update["metadata"]["unrelated"] = true;
    item->fromLLSD(update, false);
    ensure_equals("unrelated metadata keeps runtime", item->getRuntime(), std::string("luau"));
    update["metadata"]["script"]["runtime"] = "mono";
    item->fromLLSD(update, false);
    ensure_equals("explicit partial runtime changes VM", item->getRuntime(), std::string("mono"));
    update.erase("metadata");
    item->fromLLSD(update, true);
    ensure("full replacement clears absent runtime", item->getRuntime().empty());
}
}
