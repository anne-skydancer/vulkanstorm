/** Tests for independent SLua source-language and runtime metadata, and for
    the boundary of the Rust data model: legacy text from files, LLSD. */
#include "linden_common.h"
#include "llinventory.h"
#include "lldate.h"
#include "lluri.h"
#include "../test/lltut.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
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

// The legacy readers take the rest of a stream as one window and leave the
// stream where the C++ readers left it (llinventory_vks.h). A task inventory
// file is read in text mode, where the delivered bytes differ from the file's
// (CRLF) and tellg/seekg do not round-trip on Windows (LF): read every item of
// a file larger than a stream buffer, as LLViewerObject::loadTaskInvFile does,
// and compare with the same loop over the delivered bytes in memory.
struct legacy_file_data
{
    static std::string fileText()
    {
        std::ostringstream out;
        LLPointer<LLInventoryObject> contents = new LLInventoryObject(LLUUID::generateNewID(), LLUUID::null,
                                                                      LLAssetType::AT_CATEGORY, "Contents");
        contents->exportLegacyStream(out);
        for (S32 i = 0; i < 300; ++i)
        {
            LLPermissions perm;
            perm.init(LLUUID::generateNewID(), LLUUID::generateNewID(), LLUUID::null, LLUUID::null);
            perm.initMasks(PERM_ALL, PERM_ALL, i % 3 ? PERM_NONE : PERM_COPY, PERM_NONE, PERM_MOVE | PERM_TRANSFER);
            const std::string name = "Item " + std::to_string(i) + std::string(i % 57, 'n');
            const std::string desc = std::string(i % 131, 'd');
            LLPointer<LLInventoryItem> item = new LLInventoryItem(
                LLUUID::generateNewID(), LLUUID::generateNewID(), perm, LLUUID::generateNewID(),
                i % 2 ? LLAssetType::AT_NOTECARD : LLAssetType::AT_LSL_TEXT,
                i % 2 ? LLInventoryType::IT_NOTECARD : LLInventoryType::IT_LSL, name, desc,
                LLSaleInfo(LLSaleInfo::FS_NOT, i), 0, 1100000000 + i);
            item->exportLegacyStream(out, true);
        }
        return out.str();
    }

    // The loop of LLViewerObject::loadTaskInvFile: every object read, as its
    // legacy text, then the stream state.
    static std::string readAll(std::istream& in)
    {
        std::string out;
        char buffer[MAX_STRING];
        char keyword[MAX_STRING];
        while (in.good())
        {
            in.getline(buffer, MAX_STRING);
            if (sscanf(buffer, " %254s", keyword) == EOF)
            {
                break;
            }
            std::ostringstream text;
            if (0 == strcmp("inv_item", keyword))
            {
                LLPointer<LLInventoryItem> item = new LLInventoryItem;
                item->importLegacyStream(in);
                item->exportLegacyStream(text, true);
            }
            else if (0 == strcmp("inv_object", keyword))
            {
                LLPointer<LLInventoryObject> object = new LLInventoryObject;
                object->importLegacyStream(in);
                object->exportLegacyStream(text);
            }
            else
            {
                text << "unknown " << keyword << "\n";
            }
            out += text.str();
        }
        out += "state " + std::to_string((int)in.rdstate()) + "\n";
        return out;
    }

    static void check(const std::string& label, const std::string& bytes, std::ios::openmode mode)
    {
        const std::filesystem::path path = std::filesystem::temp_directory_path() /
            ("llinventory_legacy_" + std::to_string((unsigned long long)LLUUID::generateNewID().getCRC32()) + ".txt");
        {
            std::ofstream file(path, std::ios::binary);
            file.write(bytes.data(), (std::streamsize)bytes.size());
        }
        std::string delivered;
        {
            std::ifstream file(path, mode);
            std::ostringstream all;
            all << file.rdbuf();
            delivered = all.str();
        }
        std::string from_file;
        {
            std::ifstream file(path, mode);
            from_file = readAll(file);
        }
        std::filesystem::remove(path);
        std::istringstream memory(delivered);
        const std::string expected = readAll(memory);
        ensure(label + ": items read", expected.find("inv_item") != std::string::npos);
        ensure_equals(label, from_file, expected);
    }

    static std::string crlf(const std::string& text)
    {
        std::string out;
        for (char c : text)
        {
            if (c == '\n')
            {
                out += '\r';
            }
            out += c;
        }
        return out;
    }
};
using legacy_file_group = test_group<legacy_file_data>;
using legacy_file_test = legacy_file_group::object;
legacy_file_group legacy_file_tests("Legacy inventory text from files");

template<> template<> void legacy_file_test::test<1>()
{
    const std::string text = fileText();
    ensure("larger than a stream buffer", text.size() > 64 * 1024);
    check("LF, text mode", text, std::ios::in);
    check("CRLF, text mode", crlf(text), std::ios::in);
    check("LF, binary mode", text, std::ios::in | std::ios::binary);
    check("CRLF, binary mode", crlf(text), std::ios::in | std::ios::binary);
}

template<> template<> void legacy_file_test::test<2>()
{
    // A file that ends inside an item, and one whose last item has no "}".
    const std::string text = fileText();
    const std::string cut = text.substr(0, text.size() / 2 + 17);
    check("cut, LF, text mode", cut, std::ios::in);
    check("cut, CRLF, text mode", crlf(cut), std::ios::in);
    const std::string open = text.substr(0, text.rfind('}'));
    check("no last brace, LF, text mode", open, std::ios::in);
    check("no last brace, CRLF, text mode", crlf(open), std::ios::in);
}

// LLSD crosses to Rust flat and comes back as a document (llsdflat.h): the
// "into" forms return the value they were given with their own keys added,
// so every other value must come back exactly, bit for bit.
struct llsd_flat_data
{
    static LLSD everyKind()
    {
        LLSD sd = LLSD::emptyMap();
        sd["undefined"] = LLSD();
        sd["true"] = true;
        sd["false"] = false;
        sd["int min"] = (LLSD::Integer)S32_MIN;
        sd["int max"] = (LLSD::Integer)S32_MAX;
        sd["int -1"] = -1;
        sd["real -0"] = -0.0;
        sd["real inf"] = std::numeric_limits<F64>::infinity();
        sd["real denormal"] = std::numeric_limits<F64>::denorm_min();
        U64 nan_bits = 0x7ff4000000000abcULL; // a NaN with a payload
        F64 nan;
        memcpy(&nan, &nan_bits, sizeof(nan));
        sd["real nan"] = nan;
        sd["string"] = std::string("a\0b\xc3\xa9\xff|", 7);
        sd["string empty"] = std::string();
        sd["uuid"] = LLUUID("0123abcd-4567-89ef-0123-456789abcdef");
        sd["date"] = LLDate(1234567890.125);
        sd["uri"] = LLURI("http://example.com/a%20b?c=d");
        sd["binary"] = LLSD::Binary{ 0, 1, 2, 0xff, 0 };
        sd["binary empty"] = LLSD::Binary();
        sd["map empty"] = LLSD::emptyMap();
        sd["array empty"] = LLSD::emptyArray();
        LLSD nested = LLSD::emptyArray();
        nested.append(LLSD());
        nested.append(LLSD().with("k", LLSD::emptyArray().with(0, 3.5)));
        nested.append(LLSD::emptyMap());
        sd["array"] = nested;
        sd[std::string("key\0with nul", 12)] = 7;
        sd["\xff key"] = "high key byte";
        return sd;
    }

    static void same(const std::string& at, const LLSD& a, const LLSD& b)
    {
        ensure_equals(at + " type", (int)b.type(), (int)a.type());
        switch (a.type())
        {
        case LLSD::TypeBoolean:
            ensure_equals(at, b.asBoolean(), a.asBoolean());
            break;
        case LLSD::TypeInteger:
            ensure_equals(at, b.asInteger(), a.asInteger());
            break;
        case LLSD::TypeReal:
        {
            const F64 x = a.asReal();
            const F64 y = b.asReal();
            ensure(at + " bits", memcmp(&x, &y, sizeof(x)) == 0);
            break;
        }
        case LLSD::TypeDate:
        {
            const F64 x = a.asDate().secondsSinceEpoch();
            const F64 y = b.asDate().secondsSinceEpoch();
            ensure(at + " bits", memcmp(&x, &y, sizeof(x)) == 0);
            break;
        }
        case LLSD::TypeString:
        case LLSD::TypeURI:
            ensure_equals(at, b.asString(), a.asString());
            break;
        case LLSD::TypeUUID:
            ensure_equals(at, b.asUUID(), a.asUUID());
            break;
        case LLSD::TypeBinary:
            ensure(at, b.asBinary() == a.asBinary());
            break;
        case LLSD::TypeMap:
        {
            ensure_equals(at + " size", b.size(), a.size());
            auto i = a.beginMap();
            auto j = b.beginMap();
            for (; i != a.endMap(); ++i, ++j)
            {
                ensure_equals(at + " key", j->first, i->first);
                same(at + "/" + i->first, i->second, j->second);
            }
            break;
        }
        case LLSD::TypeArray:
            ensure_equals(at + " size", b.size(), a.size());
            for (size_t k = 0; k < a.size(); ++k)
            {
                same(at + "[" + std::to_string(k) + "]", a[k], b[k]);
            }
            break;
        default:
            break;
        }
    }
};
using llsd_flat_group = test_group<llsd_flat_data>;
using llsd_flat_test = llsd_flat_group::object;
llsd_flat_group llsd_flat_tests("LLSD across the inventory calls");

template<> template<> void llsd_flat_test::test<1>()
{
    const LLSD given = everyKind();
    LLSD sd = given;
    LLSaleInfo(LLSaleInfo::FS_COPY, 25).asLLSD(sd);
    ensure_equals("sale type added", sd["sale_type"].asString(), std::string("copy"));
    ensure_equals("sale price added", sd["sale_price"].asInteger(), 25);
    sd.erase("sale_type");
    sd.erase("sale_price");
    same("sd", given, sd);
}

template<> template<> void llsd_flat_test::test<2>()
{
    // An undefined value goes as the empty buffer and comes back a map; a
    // value nested in the given map comes back unchanged.
    LLSD sd;
    ll_fill_sd_from_permissions(sd, LLPermissions::DEFAULT);
    ensure("undefined becomes a map", sd.isMap() && sd.has("base_mask"));
    const LLSD array = everyKind()["array"];
    LLSD inner = LLSD::emptyMap();
    inner["nested"] = array;
    ll_fill_sd_from_permissions(inner, LLPermissions::DEFAULT);
    same("nested", array, inner["nested"]);
}
}
