/** Tests for independent SLua source-language and runtime metadata, and for
    the boundary of the Rust data model: legacy text from files. */
#include "linden_common.h"
#include "llinventory.h"
#include "llnotecard.h"
#include <stdexcept>
#include "../test/lltut.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
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

// A stream buffer that hands out `chunk` bytes per refill and can throw from a
// refill once `throw_at` bytes are delivered; it counts the bulk reads.
struct small_buffer : public std::streambuf
{
    small_buffer(const std::string& text, size_t chunk, size_t throw_at = std::string::npos)
    :   mText(text), mChunk(chunk), mThrowAt(throw_at)
    {
    }

    int_type underflow() override
    {
        if (gptr() < egptr())
        {
            return traits_type::to_int_type(*gptr());
        }
        if (mPos >= mText.size())
        {
            return traits_type::eof();
        }
        if (mPos >= mThrowAt)
        {
            throw std::runtime_error("read error");
        }
        const size_t n = std::min(mChunk, mText.size() - mPos);
        mBuffer.assign(mText, mPos, n);
        mPos += n;
        setg(&mBuffer[0], &mBuffer[0], &mBuffer[0] + n);
        return traits_type::to_int_type(*gptr());
    }

    std::streamsize xsgetn(char* s, std::streamsize n) override
    {
        ++mReads;
        const std::streamsize got = std::streambuf::xsgetn(s, n);
        if (mThrowOnPutBack)
        {
            // Nothing read stays available to put back: sungetc goes to
            // pbackfail.
            setg(gptr(), gptr(), egptr());
        }
        return got;
    }

    int_type pbackfail(int_type c) override
    {
        if (mThrowOnPutBack)
        {
            throw std::runtime_error("put back error");
        }
        return std::streambuf::pbackfail(c);
    }

    std::string mText;
    std::string mBuffer;
    size_t mChunk;
    size_t mThrowAt;
    size_t mPos = 0;
    size_t mReads = 0;
    bool mThrowOnPutBack = false;
};

template<> template<> void legacy_file_test::test<3>()
{
    // A record much longer than a refill: the parts the reader takes stay one
    // refill each, however many refills it takes (its part size is capped, so
    // it never falls to one-byte reads).
    LLNotecard source;
    std::string text;
    for (S32 i = 0; text.size() < 16 * 1024; ++i)
    {
        text += "Line " + std::to_string(i) + " of a long notecard\n";
    }
    source.setText(text);
    std::ostringstream out;
    ensure("exported", source.exportStream(out));
    const std::string bytes = out.str();

    small_buffer sb(bytes, 16);
    std::istream in(&sb);
    LLNotecard notecard;
    ensure("imported", notecard.importStream(in));
    ensure_equals("text", notecard.getText(), text);
    ensure("one read per refill, not one per byte (" + std::to_string(sb.mReads) + " reads)",
           sb.mReads <= bytes.size() / 16 + 8);
}

template<> template<> void legacy_file_test::test<4>()
{
    // A read error under the reader is an import failure with badbit set, as
    // when istream's own reads catch it; the exception does not escape.
    const std::string text = fileText();
    const size_t item = text.find("\tinv_item");
    ensure("an item", item != std::string::npos);
    const size_t body = text.find('\n', item) + 1;
    small_buffer sb(text.substr(body), 16, 100);
    std::istream in(&sb);
    LLPointer<LLInventoryItem> imported = new LLInventoryItem;
    bool ok = true;
    try
    {
        ok = imported->importLegacyStream(in);
    }
    catch (...)
    {
        fail("an exception escaped the legacy reader");
    }
    ensure("import failed", !ok);
    ensure("badbit set", in.bad());
}

template<> template<> void legacy_file_test::test<5>()
{
    // Putting back the bytes the reader read past the item fails: the import
    // fails too (the stream is not where the reader left it), with badbit.
    const std::string text = fileText();
    const size_t item = text.find("\tinv_item");
    ensure("an item", item != std::string::npos);
    const size_t body = text.find('\n', item) + 1;
    small_buffer sb(text.substr(body), 16);
    sb.mThrowOnPutBack = true;
    std::istream in(&sb);
    LLPointer<LLInventoryItem> imported = new LLInventoryItem;
    bool ok = true;
    try
    {
        ok = imported->importLegacyStream(in);
    }
    catch (...)
    {
        fail("an exception escaped the legacy reader");
    }
    ensure("import failed", !ok);
    ensure("badbit set", in.bad());
}
}
