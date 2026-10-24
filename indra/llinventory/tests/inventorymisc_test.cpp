/**
 * @file inventory.cpp
 * @author Phoenix
 * @date 2005-11-15
 * @brief Functions for inventory test framework
 *
 * $LicenseInfo:firstyear=2005&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "linden_common.h"
#include "llsd.h"
#include "llsdserialize.h"

#include "../llcategory.h"
#include "../llinventory.h"
#include "../test/lltut.h"


#if LL_WINDOWS
// disable unreachable code warnings
#pragma warning(disable: 4702)
#endif

void set_random_inventory_metadata(LLInventoryObject* obj)
{
    S32 extra = rand() % 4;
    switch (extra)
    {
    case 0:
    {
        LLUUID thumbnail_id;
        thumbnail_id.generate();
        obj->setThumbnailUUID(thumbnail_id);
        break;
    }
    case 1:
        obj->setFavorite(true);
        break;
    case 2:
    {
        LLUUID thumbnail_id;
        thumbnail_id.generate();
        obj->setThumbnailUUID(thumbnail_id);
        obj->setFavorite(true);
        break;
    }
    default:
        break;
    }
}

LLPointer<LLInventoryItem> create_random_inventory_item()
{
    LLUUID item_id;
    item_id.generate();
    LLUUID parent_id;
    parent_id.generate();
    LLPermissions perm;
    LLUUID creator_id;
    creator_id.generate();
    LLUUID owner_id;
    owner_id.generate();
    LLUUID last_owner_id;
    last_owner_id.generate();
    LLUUID group_id;
    group_id.generate();
    perm.init(creator_id, owner_id, last_owner_id, group_id);
    perm.initMasks(PERM_ALL, PERM_ALL, PERM_COPY, PERM_COPY, PERM_MODIFY | PERM_COPY);
    LLUUID asset_id;
    asset_id.generate();
    S32 price = rand();
    LLSaleInfo sale_info(LLSaleInfo::FS_COPY, price);
    U32 flags = rand();
    S32 creation = (S32)time(NULL);

    LLPointer<LLInventoryItem> item = new LLInventoryItem(
        item_id,
        parent_id,
        perm,
        asset_id,
        LLAssetType::AT_OBJECT,
        LLInventoryType::IT_ATTACHMENT,
        std::string("Sample Object"),
        std::string("Used for Testing"),
        sale_info,
        flags,
        creation);
    set_random_inventory_metadata(item);
    return item;
}

// Fill the stack below the caller with a pattern, so that a reader which
// reads an uninitialised local sees the pattern rather than a lucky zero.
// Called through a volatile pointer so that it is never inlined.
void fill_stack(U8 pattern)
{
    volatile U8 area[64 * 1024];
    for (size_t i = 0; i < sizeof(area); ++i)
    {
        area[i] = pattern;
    }
}
void (*volatile poison_stack)(U8) = fill_stack;

LLPointer<LLInventoryCategory> create_random_inventory_cat()
{
    LLUUID item_id;
    item_id.generate();
    LLUUID parent_id;
    parent_id.generate();

    LLPointer<LLInventoryCategory> cat = new LLInventoryCategory(
        item_id,
        parent_id,
        LLFolderType::FT_NONE,
        std::string("Sample category"));
    set_random_inventory_metadata(cat);
    return cat;
}

namespace tut
{
    struct inventory_data
    {
    };
    typedef test_group<inventory_data> inventory_test;
    typedef inventory_test::object inventory_object;
    tut::inventory_test inv("LLInventory");

//***class LLInventoryType***//


    template<> template<>
    void inventory_object::test<1>()
    {
        LLInventoryType::EType retType =  LLInventoryType::lookup(std::string("sound"));
        ensure("1.LLInventoryType::lookup(char*) failed", retType == LLInventoryType::IT_SOUND);

        retType = LLInventoryType::lookup(std::string("snapshot"));
        ensure("2.LLInventoryType::lookup(char*) failed", retType == LLInventoryType::IT_SNAPSHOT);
    }

    template<> template<>
    void inventory_object::test<2>()
    {
        static std::string retType =  LLInventoryType::lookup(LLInventoryType::IT_CALLINGCARD);
        ensure("1.LLInventoryType::lookup(EType) failed", (retType == "callcard"));

        retType = LLInventoryType::lookup(LLInventoryType::IT_LANDMARK);
        ensure("2.LLInventoryType::lookup(EType) failed", (retType == "landmark"));

    }

    template<> template<>
    void inventory_object::test<3>()
    {
        static std::string retType =  LLInventoryType::lookupHumanReadable(LLInventoryType::IT_CALLINGCARD);
        ensure("1.LLInventoryType::lookupHumanReadable(EType) failed", (retType == "calling card"));

        retType =  LLInventoryType::lookupHumanReadable(LLInventoryType::IT_LANDMARK);
        ensure("2.LLInventoryType::lookupHumanReadable(EType) failed", (retType == "landmark"));
    }

    template<> template<>
    void inventory_object::test<4>()
    {
        static LLInventoryType::EType retType =  LLInventoryType::defaultForAssetType(LLAssetType::AT_TEXTURE);
        ensure("1.LLInventoryType::defaultForAssetType(LLAssetType EType) failed", retType == LLInventoryType::IT_TEXTURE);

        retType =  LLInventoryType::defaultForAssetType(LLAssetType::AT_LANDMARK);
        ensure("2.LLInventoryType::defaultForAssetType(LLAssetType EType) failed", retType == LLInventoryType::IT_LANDMARK);
    }

//*****class LLInventoryItem*****//

    template<> template<>
    void inventory_object::test<5>()
    {
        LLPointer<LLInventoryItem> src = create_random_inventory_item();
        LLSD sd = ll_create_sd_from_inventory_item(src);
        //LL_INFOS() << "sd: " << *sd << LL_ENDL;
        LLPointer<LLInventoryItem> dst = new LLInventoryItem;
        bool successful_parse = dst->fromLLSD(sd);
        ensure_equals("0.LLInventoryItem::fromLLSD()", successful_parse, true);
        ensure_equals("1.item id::getUUID() failed", dst->getUUID(), src->getUUID());
        ensure_equals("2.parent::getParentUUID() failed", dst->getParentUUID(), src->getParentUUID());
        ensure_equals("3.name::getName() failed", dst->getName(), src->getName());
        ensure_equals("4.type::getType() failed", dst->getType(), src->getType());

        ensure_equals("5.permissions::getPermissions() failed", dst->getPermissions(), src->getPermissions());
        ensure_equals("6.description::getDescription() failed", dst->getDescription(), src->getDescription());
        ensure_equals("7.sale type::getSaleType() failed", dst->getSaleInfo().getSaleType(), src->getSaleInfo().getSaleType());
        ensure_equals("8.sale price::getSalePrice() failed", dst->getSaleInfo().getSalePrice(), src->getSaleInfo().getSalePrice());
        ensure_equals("9.asset id::getAssetUUID() failed", dst->getAssetUUID(), src->getAssetUUID());
        ensure_equals("10.inventory type::getInventoryType() failed", dst->getInventoryType(), src->getInventoryType());
        ensure_equals("11.flags::getFlags() failed", dst->getFlags(), src->getFlags());
        ensure_equals("12.creation::getCreationDate() failed", dst->getCreationDate(), src->getCreationDate());

        LLUUID new_item_id, new_parent_id;
        new_item_id.generate();
        src->setUUID(new_item_id);

        new_parent_id.generate();
        src->setParent(new_parent_id);

        std::string new_name = "LindenLab";
        src->rename(new_name);

        src->setType(LLAssetType::AT_SOUND);

        LLUUID new_asset_id;
        new_asset_id.generate();

        src->setAssetUUID(new_asset_id);
        std::string new_desc = "SecondLife Testing";
        src->setDescription(new_desc);

        S32 new_price = rand();
        LLSaleInfo new_sale_info(LLSaleInfo::FS_COPY, new_price);
        src->setSaleInfo(new_sale_info);

        U32 new_flags = rand();
        S32 new_creation = (S32)time(NULL);

        LLPermissions new_perm;

        LLUUID new_creator_id;
        new_creator_id.generate();

        LLUUID new_owner_id;
        new_owner_id.generate();

        LLUUID last_owner_id;
        last_owner_id.generate();

        LLUUID new_group_id;
        new_group_id.generate();
        new_perm.init(new_creator_id, new_owner_id, last_owner_id, new_group_id);
        new_perm.initMasks(PERM_ALL, PERM_ALL, PERM_COPY, PERM_COPY, PERM_MODIFY | PERM_COPY);
        src->setPermissions(new_perm);

        src->setInventoryType(LLInventoryType::IT_SOUND);
        src->setFlags(new_flags);
        src->setCreationDate(new_creation);

        sd = ll_create_sd_from_inventory_item(src);
        //LL_INFOS() << "sd: " << *sd << LL_ENDL;
        successful_parse = dst->fromLLSD(sd);
        ensure_equals("13.item id::getUUID() failed", dst->getUUID(), src->getUUID());
        ensure_equals("14.parent::getParentUUID() failed", dst->getParentUUID(), src->getParentUUID());
        ensure_equals("15.name::getName() failed", dst->getName(), src->getName());
        ensure_equals("16.type::getType() failed", dst->getType(), src->getType());

        ensure_equals("17.permissions::getPermissions() failed", dst->getPermissions(), src->getPermissions());
        ensure_equals("18.description::getDescription() failed", dst->getDescription(), src->getDescription());
        ensure_equals("19.sale type::getSaleType() failed type", dst->getSaleInfo().getSaleType(), src->getSaleInfo().getSaleType());
        ensure_equals("20.sale price::getSalePrice() failed price", dst->getSaleInfo().getSalePrice(), src->getSaleInfo().getSalePrice());
        ensure_equals("21.asset id::getAssetUUID() failed id", dst->getAssetUUID(), src->getAssetUUID());
        ensure_equals("22.inventory type::getInventoryType() failed type", dst->getInventoryType(), src->getInventoryType());
        ensure_equals("23.flags::getFlags() failed", dst->getFlags(), src->getFlags());
        ensure_equals("24.creation::getCreationDate() failed", dst->getCreationDate(), src->getCreationDate());

    }

    template<> template<>
    void inventory_object::test<6>()
    {
        LLPointer<LLInventoryItem> src = create_random_inventory_item();

        LLUUID new_item_id, new_parent_id;
        new_item_id.generate();
        src->setUUID(new_item_id);

        new_parent_id.generate();
        src->setParent(new_parent_id);

        std::string new_name = "LindenLab";
        src->rename(new_name);

        src->setType(LLAssetType::AT_SOUND);

        LLUUID new_asset_id;
        new_asset_id.generate();

        src->setAssetUUID(new_asset_id);
        std::string new_desc = "SecondLife Testing";
        src->setDescription(new_desc);

        S32 new_price = rand();
        LLSaleInfo new_sale_info(LLSaleInfo::FS_COPY, new_price);
        src->setSaleInfo(new_sale_info);

        U32 new_flags = rand();
        S32 new_creation = (S32)time(NULL);

        LLPermissions new_perm;

        LLUUID new_creator_id;
        new_creator_id.generate();

        LLUUID new_owner_id;
        new_owner_id.generate();

        LLUUID last_owner_id;
        last_owner_id.generate();

        LLUUID new_group_id;
        new_group_id.generate();
        new_perm.init(new_creator_id, new_owner_id, last_owner_id, new_group_id);
        new_perm.initMasks(PERM_ALL, PERM_ALL, PERM_COPY, PERM_COPY, PERM_MODIFY | PERM_COPY);
        src->setPermissions(new_perm);

        src->setInventoryType(LLInventoryType::IT_SOUND);
        src->setFlags(new_flags);
        src->setCreationDate(new_creation);

        // test a save/load cycle to LLSD and back again
        // Note: ll_create_sd_from_inventory_item does not support metadata
        LLSD sd = ll_create_sd_from_inventory_item(src);
        LLPointer<LLInventoryItem> dst = new LLInventoryItem;
        bool successful_parse = dst->fromLLSD(sd);
        ensure_equals("0.LLInventoryItem::fromLLSD()", successful_parse, true);

        LLPointer<LLInventoryItem> src1 = create_random_inventory_item();
        src1->copyItem(src);

        ensure_equals("1.item id::getUUID() failed", dst->getUUID(), src1->getUUID());
        ensure_equals("2.parent::getParentUUID() failed", dst->getParentUUID(), src1->getParentUUID());
        ensure_equals("3.name::getName() failed", dst->getName(), src1->getName());
        ensure_equals("4.type::getType() failed", dst->getType(), src1->getType());

        ensure_equals("5.permissions::getPermissions() failed", dst->getPermissions(), src1->getPermissions());
        ensure_equals("6.description::getDescription() failed", dst->getDescription(), src1->getDescription());
        ensure_equals("7.sale type::getSaleType() failed type", dst->getSaleInfo().getSaleType(), src1->getSaleInfo().getSaleType());
        ensure_equals("8.sale price::getSalePrice() failed price", dst->getSaleInfo().getSalePrice(), src1->getSaleInfo().getSalePrice());
        ensure_equals("9.asset id::getAssetUUID() failed id", dst->getAssetUUID(), src1->getAssetUUID());
        ensure_equals("10.inventory type::getInventoryType() failed type", dst->getInventoryType(), src1->getInventoryType());
        ensure_equals("11.flags::getFlags() failed", dst->getFlags(), src1->getFlags());
        ensure_equals("12.creation::getCreationDate() failed", dst->getCreationDate(), src1->getCreationDate());

        // quick test to make sure generateUUID() really works
        src1->generateUUID();
        ensure_not_equals("13.item id::generateUUID() failed", src->getUUID(), src1->getUUID());
    }

    template<> template<>
    void inventory_object::test<7>()
    {
        std::string filename("linden_file.dat");
        llofstream fileXML(filename.c_str());
        if (!fileXML.is_open())
        {
            LL_ERRS() << "file could not be opened\n" << LL_ENDL;
            return;
        }

        LLPointer<LLInventoryItem> src1 = create_random_inventory_item();
        LLSD sd;
        src1->asLLSD(sd);
        fileXML << LLSDOStreamer<LLSDNotationFormatter>(sd) << std::endl;
        fileXML.close();


        LLPointer<LLInventoryItem> src2 = new LLInventoryItem();
        llifstream file(filename.c_str());
        if (!file.is_open())
        {
            LL_ERRS() << "file could not be opened\n" << LL_ENDL;
            return;
        }
        std::string line;
        LLPointer<LLSDParser> parser = new LLSDNotationParser();
        std::getline(file, line);
        LLSD s_item;
        std::istringstream iss(line);
        if (parser->parse(iss, s_item, line.length()) == LLSDParser::PARSE_FAILURE)
        {
            LL_ERRS()<< "Parsing cache failed" << LL_ENDL;
            return;
        }
        src2->fromLLSD(s_item);

        file.close();

        ensure_equals("1.item id::getUUID() failed", src1->getUUID(), src2->getUUID());
        ensure_equals("2.parent::getParentUUID() failed", src1->getParentUUID(), src2->getParentUUID());
        ensure_equals("3.permissions::getPermissions() failed", src1->getPermissions(), src2->getPermissions());
        ensure_equals("4.sale price::getSalePrice() failed price", src1->getSaleInfo().getSalePrice(), src2->getSaleInfo().getSalePrice());
        ensure_equals("5.asset id::getAssetUUID() failed id", src1->getAssetUUID(), src2->getAssetUUID());
        ensure_equals("6.type::getType() failed", src1->getType(), src2->getType());
        ensure_equals("7.inventory type::getInventoryType() failed type", src1->getInventoryType(), src2->getInventoryType());
        ensure_equals("8.name::getName() failed", src1->getName(), src2->getName());
        ensure_equals("9.description::getDescription() failed", src1->getDescription(), src2->getDescription());
        ensure_equals("10.creation::getCreationDate() failed", src1->getCreationDate(), src2->getCreationDate());
        ensure_equals("13.thumbnails::getThumbnailUUID() failed", src1->getThumbnailUUID(), src2->getThumbnailUUID());
        ensure_equals("14.favorites::getIsFavorite() failed", src1->getIsFavorite(), src2->getIsFavorite());
    }

    template<> template<>
    void inventory_object::test<8>()
    {
        LLPointer<LLInventoryItem> src1 = create_random_inventory_item();

        std::ostringstream ostream;
        src1->exportLegacyStream(ostream, true);

        std::istringstream istream(ostream.str());
        LLPointer<LLInventoryItem> src2 = new LLInventoryItem();
        src2->importLegacyStream(istream);

        ensure_equals("1.item id::getUUID() failed", src1->getUUID(), src2->getUUID());
        ensure_equals("2.parent::getParentUUID() failed", src1->getParentUUID(), src2->getParentUUID());
        ensure_equals("3.permissions::getPermissions() failed", src1->getPermissions(), src2->getPermissions());
        ensure_equals("4.sale price::getSalePrice() failed price", src1->getSaleInfo().getSalePrice(), src2->getSaleInfo().getSalePrice());
        ensure_equals("5.asset id::getAssetUUID() failed id", src1->getAssetUUID(), src2->getAssetUUID());
        ensure_equals("6.type::getType() failed", src1->getType(), src2->getType());
        ensure_equals("7.inventory type::getInventoryType() failed type", src1->getInventoryType(), src2->getInventoryType());
        ensure_equals("8.name::getName() failed", src1->getName(), src2->getName());
        ensure_equals("9.description::getDescription() failed", src1->getDescription(), src2->getDescription());
        ensure_equals("10.creation::getCreationDate() failed", src1->getCreationDate(), src2->getCreationDate());
        ensure_equals("11.thumbnails::getThumbnailUUID() failed", src1->getThumbnailUUID(), src2->getThumbnailUUID());
        ensure_equals("12.favorites::getIsFavorite() failed", false, src2->getIsFavorite()); // not supposed to carry over
    }

    template<> template<>
    void inventory_object::test<9>()
    {
        // Deleted LLInventoryItem::exportFileXML() and LLInventoryItem::importXML()
        // because I can't find any non-test code references to it. 2009-05-04 JC
    }

    template<> template<>
    void inventory_object::test<11>()
    {
        LLPointer<LLInventoryItem> src1 = create_random_inventory_item();
        LLSD retSd = src1->asLLSD();
        LLPointer<LLInventoryItem> src2 = new LLInventoryItem();
        src2->fromLLSD(retSd);

        ensure_equals("1.item id::getUUID() failed", src1->getUUID(), src2->getUUID());
        ensure_equals("2.parent::getParentUUID() failed", src1->getParentUUID(), src2->getParentUUID());
        ensure_equals("3.permissions::getPermissions() failed", src1->getPermissions(), src2->getPermissions());
        ensure_equals("4.asset id::getAssetUUID() failed id", src1->getAssetUUID(), src2->getAssetUUID());
        ensure_equals("5.type::getType() failed", src1->getType(), src2->getType());
        ensure_equals("6.inventory type::getInventoryType() failed type", src1->getInventoryType(), src2->getInventoryType());
        ensure_equals("7.flags::getFlags() failed", src1->getFlags(), src2->getFlags());
        ensure_equals("8.sale type::getSaleType() failed type", src1->getSaleInfo().getSaleType(), src2->getSaleInfo().getSaleType());
        ensure_equals("9.sale price::getSalePrice() failed price", src1->getSaleInfo().getSalePrice(), src2->getSaleInfo().getSalePrice());
        ensure_equals("10.name::getName() failed", src1->getName(), src2->getName());
        ensure_equals("11.description::getDescription() failed", src1->getDescription(), src2->getDescription());
        ensure_equals("12.creation::getCreationDate() failed", src1->getCreationDate(), src2->getCreationDate());
        ensure_equals("13.thumbnails::getThumbnailUUID() failed", src1->getThumbnailUUID(), src2->getThumbnailUUID());
        ensure_equals("14.favorites::getIsFavorite() failed", src1->getIsFavorite(), src2->getIsFavorite());
    }

//******class LLInventoryCategory*******//

    template<> template<>
    void inventory_object::test<12>()
    {
        LLPointer<LLInventoryCategory> src = create_random_inventory_cat();
        LLSD sd = ll_create_sd_from_inventory_category(src);
        LLPointer<LLInventoryCategory> dst = ll_create_category_from_sd(sd);

        ensure_equals("1.item id::getUUID() failed", dst->getUUID(), src->getUUID());
        ensure_equals("2.parent::getParentUUID() failed", dst->getParentUUID(), src->getParentUUID());
        ensure_equals("3.name::getName() failed", dst->getName(), src->getName());
        ensure_equals("4.type::getType() failed", dst->getType(), src->getType());
        ensure_equals("5.preferred type::getPreferredType() failed", dst->getPreferredType(), src->getPreferredType());

        src->setPreferredType( LLFolderType::FT_TEXTURE);
        sd = ll_create_sd_from_inventory_category(src);
        dst = ll_create_category_from_sd(sd);
        ensure_equals("6.preferred type::getPreferredType() failed", dst->getPreferredType(), src->getPreferredType());


    }

    template<> template<>
    void inventory_object::test<13>()
    {
        std::string filename("linden_file.dat");
        llofstream fileXML(filename.c_str());
        if (!fileXML.is_open())
        {
            LL_ERRS() << "file could not be opened\n" << LL_ENDL;
            return;
        }

        LLPointer<LLInventoryCategory> src1 = create_random_inventory_cat();
        LLSD sd;
        src1->exportLLSD(sd);
        fileXML << LLSDOStreamer<LLSDNotationFormatter>(sd) << std::endl;
        fileXML.close();

        llifstream file(filename.c_str());
        if (!file.is_open())
        {
            LL_ERRS() << "file could not be opened\n" << LL_ENDL;
            return;
        }
        std::string line;
        LLPointer<LLSDParser> parser = new LLSDNotationParser();
        std::getline(file, line);
        LLSD s_item;
        std::istringstream iss(line);
        if (parser->parse(iss, s_item, line.length()) == LLSDParser::PARSE_FAILURE)
        {
            LL_ERRS()<< "Parsing cache failed" << LL_ENDL;
            return;
        }

        file.close();

        LLPointer<LLInventoryCategory> src2 = new LLInventoryCategory();
        src2->importLLSDMap(s_item);

        ensure_equals("1.item id::getUUID() failed", src1->getUUID(), src2->getUUID());
        ensure_equals("2.parent::getParentUUID() failed", src1->getParentUUID(), src2->getParentUUID());
        ensure_equals("3.type::getType() failed", src1->getType(), src2->getType());
        ensure_equals("4.preferred type::getPreferredType() failed", src1->getPreferredType(), src2->getPreferredType());
        ensure_equals("5.name::getName() failed", src1->getName(), src2->getName());
        ensure_equals("6.thumbnails::getThumbnailUUID() failed", src1->getThumbnailUUID(), src2->getThumbnailUUID());
        ensure_equals("7.favorites::getIsFavorite() failed", src1->getIsFavorite(), src2->getIsFavorite());
    }

    template<> template<>
    void inventory_object::test<14>()
    {
        LLPointer<LLInventoryCategory> src1 = create_random_inventory_cat();

        std::ostringstream ostream;
        src1->exportLegacyStream(ostream, true);

        std::istringstream istream(ostream.str());
        LLPointer<LLInventoryCategory> src2 = new LLInventoryCategory();
        src2->importLegacyStream(istream);

        ensure_equals("1.item id::getUUID() failed", src1->getUUID(), src2->getUUID());
        ensure_equals("2.parent::getParentUUID() failed", src1->getParentUUID(), src2->getParentUUID());
        ensure_equals("3.type::getType() failed", src1->getType(), src2->getType());
        ensure_equals("4.preferred type::getPreferredType() failed", src1->getPreferredType(), src2->getPreferredType());
        ensure_equals("5.name::getName() failed", src1->getName(), src2->getName());
        ensure_equals("13.thumbnails::getThumbnailUUID() failed", src1->getThumbnailUUID(), src2->getThumbnailUUID());
        ensure_equals("14.favorites::getIsFavorite() failed", false, src2->getIsFavorite()); // currently not supposed to carry over
    }

//******class LLSaleInfo*******//

    template<> template<>
    void inventory_object::test<15>()
    {
        set_test_name("legacy export of a sale type outside 0..3 writes 'not'");
        // An unknown sale type (bad server data) used to stream lookup()'s
        // NULL: an access violation on Windows, a bad stream elsewhere.
        for (S32 type : { (S32)LLSaleInfo::FS_COUNT, 7 })
        {
            LLSaleInfo sale(static_cast<LLSaleInfo::EForSale>(type), 10);
            std::ostringstream out;
            ensure("sale info export returns true", sale.exportLegacyStream(out));
            ensure("sale info export leaves the stream good", out.good());
            ensure_equals("sale info export",
                          out.str(),
                          std::string("\tsale_info\t0\n\t{\n\t\tsale_type\tnot\n\t\tsale_price\t10\n\t}\n"));

            LLPointer<LLInventoryItem> src = create_random_inventory_item();
            src->setSaleInfo(sale);
            std::ostringstream item_out;
            src->exportLegacyStream(item_out, true);
            ensure("item export leaves the stream good", item_out.good());
            std::istringstream item_in(item_out.str());
            LLPointer<LLInventoryItem> dst = new LLInventoryItem();
            dst->importLegacyStream(item_in);
            ensure_equals("item reimport: sale type", dst->getSaleInfo().getSaleType(), LLSaleInfo::FS_NOT);
            ensure_equals("item reimport: sale price", dst->getSaleInfo().getSalePrice(), 10);
            ensure_equals("item reimport: name", dst->getName(), src->getName());
        }
    }

//******class LLCategory*******//

    template<> template<>
    void inventory_object::test<16>()
    {
        set_test_name("LLCategory with an unknown top byte stays inside its tables");
        // The defined top-level categories are unchanged.
        ensure_equals("none: count", LLCategory::none.getSubCategoryCount(), 5);
        ensure_equals("none: name", std::string(LLCategory::none.lookupName()), std::string("(none)"));
        ensure_equals("object: count", LLCategory(0x01000000).getSubCategoryCount(), 1);
        ensure_equals("object: name", std::string(LLCategory(0x01000000).lookupName()), std::string("Object"));
        ensure_equals("landmark: count", LLCategory(0x05000000).getSubCategoryCount(), 0);
        ensure_equals("6: count", LLCategory(0x06000000).getSubCategoryCount(), 0);
        ensure_equals("6: name", std::string(LLCategory(0x06000000).lookupName()), std::string("Component"));
        ensure("7 with a subcategory: name", LLCategory(0x07010000).lookupName() == NULL);

        // A top byte above 6 used to index past CATEGORY_INDEX (and, above 7,
        // past CATEGORY_NAME).
        for (U32 top = 7; top <= 255; ++top)
        {
            std::string id = "top byte " + std::to_string(top);
            for (U32 rest : { 0x000000u, 0x010000u, 0x010101u })
            {
                LLCategory cat((top << 24) | rest);
                ensure_equals(id + ": count", cat.getSubCategoryCount(), 0);
                ensure(id + ": name", cat.lookupName() == NULL);
            }
        }
    }

//******legacy readers*******//

    template<> template<>
    void inventory_object::test<17>()
    {
        set_test_name("legacy readers give defined values for malformed lines");
        // Each value below used to come from an uninitialised local. A line
        // that does not parse is now absent, so the field keeps its value,
        // except a permission mask, which reads as no permissions.
        {
            std::istringstream in("\t{\n\t\tcreation_date\tabc\n\t}\n");
            LLPointer<LLInventoryItem> item = new LLInventoryItem();
            poison_stack(0xa5);
            item->importLegacyStream(in);
            ensure_equals("item: unparsable creation_date", (S64)item->getCreationDate(), (S64)0);
        }
        {
            std::istringstream in("\t{\n\t\tbase_mask\tzz\n\t}\n");
            LLPermissions perm;
            poison_stack(0xa5);
            perm.importLegacyStream(in);
            ensure_equals("permissions: unparsable first mask", perm.getMaskBase(), (U32)PERM_NONE);
            ensure_equals("permissions: owner mask follows the base", perm.getMaskOwner(), (U32)PERM_NONE);
        }
        {
            std::istringstream in("\t{\n\t\tgroup_owned\tx\n\t}\n");
            LLPermissions perm;
            poison_stack(0xa5);
            perm.importLegacyStream(in);
            ensure("permissions: unparsable group_owned", !perm.isGroupOwned());
        }
        {
            // The old garbage rarely formed a valid UUID, so this case
            // states the defined value rather than catching the old code.
            std::istringstream in("\t{\n\t\tcreator_id\n\t}\n");
            LLPermissions perm;
            poison_stack(0xa5);
            perm.importLegacyStream(in);
            ensure("permissions: value-less creator_id", perm.getCreator().isNull());
        }
    }

    // The legacy text of an item, as exportLegacyStream() writes it.
    std::string legacy_text(const LLInventoryItem* item)
    {
        std::ostringstream out;
        item->exportLegacyStream(out, true);
        return out.str();
    }

    // Insert text after the first occurrence of what (which must exist).
    std::string insert_after(std::string text, const std::string& what, const std::string& extra)
    {
        size_t pos = text.find(what);
        ensure("fixture text contains " + what, pos != std::string::npos);
        text.insert(pos + what.size(), extra);
        return text;
    }

    // Import text into a new item and check it against src and the rest of
    // the stream: the item must end at its own closing brace.
    void ensure_reads_as(const std::string& desc, const std::string& text, const LLInventoryItem* src)
    {
        std::istringstream in(text + "NEXT\tline\n");
        LLPointer<LLInventoryItem> dst = new LLInventoryItem();
        dst->importLegacyStream(in);
        ensure_equals(desc + ": item id", dst->getUUID(), src->getUUID());
        ensure_equals(desc + ": permissions", dst->getPermissions(), src->getPermissions());
        ensure_equals(desc + ": asset id", dst->getAssetUUID(), src->getAssetUUID());
        ensure_equals(desc + ": type", dst->getType(), src->getType());
        ensure_equals(desc + ": flags", dst->getFlags(), src->getFlags());
        ensure_equals(desc + ": sale type", dst->getSaleInfo().getSaleType(), src->getSaleInfo().getSaleType());
        ensure_equals(desc + ": sale price", dst->getSaleInfo().getSalePrice(), src->getSaleInfo().getSalePrice());
        ensure_equals(desc + ": name", dst->getName(), src->getName());
        ensure_equals(desc + ": description", dst->getDescription(), src->getDescription());
        ensure_equals(desc + ": creation date", dst->getCreationDate(), src->getCreationDate());
        std::string rest;
        std::getline(in, rest);
        ensure_equals(desc + ": the item ends at its closing brace", rest, std::string("NEXT\tline"));
    }

    template<> template<>
    void inventory_object::test<18>()
    {
        set_test_name("a blank line after the permissions block keeps the item's closing brace");
        // The blank line used to re-run the permissions keyword: the block was
        // read again from the defaults up to the item's own '}'.
        LLPointer<LLInventoryItem> src = create_random_inventory_item();
        std::string text = legacy_text(src);
        text = insert_after(text, "\tpermissions 0\n", "");
        text.insert(text.find("\t}\n", text.find("\tpermissions 0\n")) + 3, "\n");
        ensure_reads_as("blank line after permissions", text, src);
        ensure_reads_as("whitespace line after permissions",
                        insert_after(legacy_text(src), "\t\tgroup_id\t" + src->getPermissions().getGroup().asString() + "\n\t}\n", " \t\r\n"),
                        src);
    }

    template<> template<>
    void inventory_object::test<19>()
    {
        set_test_name("a keyword-only line leaves its field unchanged");
        // The keyword used to run again with the previous line's value.
        LLPointer<LLInventoryItem> src = create_random_inventory_item();
        std::string text = legacy_text(src);
        text = insert_after(text, "\t\tdesc\t" + src->getDescription() + "|\n",
                            "\t\ttype\n\t\tname\n\t\tdesc\t\n\t\tflags\n\t\tcreation_date\n");
        ensure_reads_as("keyword-only lines after the values", text, src);

        LLPointer<LLInventoryCategory> cat = create_random_inventory_cat();
        std::ostringstream out;
        cat->exportLegacyStream(out, true);
        std::string cat_text = insert_after(out.str(), "\t\tname\t" + cat->getName() + "|\n", "\t\ttype\n\t\tpref_type\n");
        std::istringstream in(cat_text);
        LLPointer<LLInventoryCategory> dst = new LLInventoryCategory();
        dst->importLegacyStream(in);
        ensure_equals("category: type", dst->getType(), cat->getType());
        ensure_equals("category: preferred type", dst->getPreferredType(), cat->getPreferredType());
        ensure_equals("category: name", dst->getName(), cat->getName());
    }

    template<> template<>
    void inventory_object::test<20>()
    {
        set_test_name("blank lines in and after the sale block");
        // A blank line after the block used to re-enter the sale reader, which
        // then read up to the item's own '}'.
        LLPointer<LLInventoryItem> src = create_random_inventory_item();
        std::string text = legacy_text(src);
        text = insert_after(text, "\t\tsale_type\t", "");
        text.insert(text.find('\n', text.find("\t\tsale_type\t")) + 1, "\n");
        text.insert(text.find("\t}\n", text.find("\tsale_info\t0\n")) + 3, "\n");
        ensure_reads_as("blank lines in and after the sale block", text, src);

        std::istringstream in("\t{\n\n\t\tsale_type\tcopy\n\n\t\tsale_price\t10\n\n\t}\n");
        LLSaleInfo sale;
        bool has_perm_mask = true;
        U32 perm_mask = 0;
        sale.importLegacyStream(in, has_perm_mask, perm_mask);
        ensure_equals("sale block: type", sale.getSaleType(), LLSaleInfo::FS_COPY);
        ensure_equals("sale block: price", sale.getSalePrice(), 10);
        ensure("sale block: no perm_mask", !has_perm_mask);
    }

    template<> template<>
    void inventory_object::test<21>()
    {
        set_test_name("a well-formed item with an empty name and description reads as before");
        // The writer emits "\t\tname\t|" for an empty name; it must still read
        // as an empty name, and the rest of the item as written.
        LLPointer<LLInventoryItem> src = create_random_inventory_item();
        std::string text = legacy_text(src);
        const std::string name_line = "\t\tname\t" + src->getName() + "|\n";
        const std::string desc_line = "\t\tdesc\t" + src->getDescription() + "|\n";
        text.replace(text.find(name_line), name_line.size(), "\t\tname\t|\n");
        text.replace(text.find(desc_line), desc_line.size(), "\t\tdesc\t|\n");
        std::istringstream in(text + "NEXT\tline\n");
        LLPointer<LLInventoryItem> dst = create_random_inventory_item();
        dst->importLegacyStream(in);
        ensure_equals("empty name", dst->getName(), std::string());
        ensure_equals("empty description", dst->getDescription(), std::string());
        ensure_equals("item id", dst->getUUID(), src->getUUID());
        ensure_equals("permissions", dst->getPermissions(), src->getPermissions());
        ensure_equals("creation date", dst->getCreationDate(), src->getCreationDate());
        std::string rest;
        std::getline(in, rest);
        ensure_equals("the item ends at its closing brace", rest, std::string("NEXT\tline"));
    }

    template<> template<>
    void inventory_object::test<22>()
    {
        set_test_name("a garbage group_owned line after the masks does not make the item group owned");
        // The unparsable value used to reuse the last parsed mask, so any
        // non-zero mask before it turned group ownership on.
        std::istringstream in("\t{\n\t\tbase_mask\t00082000\n\t\towner_mask\t00082000\n\t\tgroup_owned\tjunk\n\t}\n");
        LLPermissions perm;
        perm.importLegacyStream(in);
        ensure("permissions: not group owned", !perm.isGroupOwned());

        LLPointer<LLInventoryItem> src = create_random_inventory_item();
        std::string text = insert_after(legacy_text(src),
                                        "\t\tgroup_id\t" + src->getPermissions().getGroup().asString() + "\n",
                                        "\t\tgroup_owned\tjunk\n");
        ensure_reads_as("item with a garbage group_owned line", text, src);
        ensure("item: not group owned", !src->getPermissions().isGroupOwned());
    }

    template<> template<>
    void inventory_object::test<23>()
    {
        set_test_name("a permission mask line that does not parse reads as no permissions");
        // Absent would leave init()'s PERM_ALL, wider than the data allows.
        struct MaskCase
        {
            const char* keyword;
            U32 (LLPermissions::*get)() const;
        };
        const MaskCase cases[] = {
            { "creator_mask", &LLPermissions::getMaskBase },
            { "base_mask", &LLPermissions::getMaskBase },
            { "owner_mask", &LLPermissions::getMaskOwner },
            { "group_mask", &LLPermissions::getMaskGroup },
            { "everyone_mask", &LLPermissions::getMaskEveryone },
            { "next_owner_mask", &LLPermissions::getMaskNextOwner },
        };
        for (const MaskCase& c : cases)
        {
            for (const char* value : { "\tzz", "\t-", "" })
            {
                const std::string line = std::string(c.keyword) + value;
                std::istringstream in("\t{\n\t\t" + line + "\n\t}\n");
                LLPermissions perm;
                perm.importLegacyStream(in);
                // creator_mask's fixFairUse() would add PERM_MOVE: it must not run.
                ensure_equals("'" + line + "' reads as no permissions", (perm.*c.get)(), (U32)PERM_NONE);
            }
        }

        // In an item, the mask's own field is 0 and the item reads on as written.
        LLPointer<LLInventoryItem> src = create_random_inventory_item();
        std::string text = legacy_text(src);
        const std::string line = "\t\teveryone_mask\t";
        size_t pos = text.find(line);
        ensure("item text has an everyone mask", pos != std::string::npos);
        text.replace(pos + line.size(), text.find('\n', pos) - pos - line.size(), "zz");
        std::istringstream in(text);
        LLPointer<LLInventoryItem> dst = new LLInventoryItem();
        dst->importLegacyStream(in);
        ensure_equals("item: everyone mask", dst->getPermissions().getMaskEveryone(), (U32)PERM_NONE);
        ensure_equals("item: base mask", dst->getPermissions().getMaskBase(), src->getPermissions().getMaskBase());
        ensure_equals("item: name", dst->getName(), src->getName());
    }
}
