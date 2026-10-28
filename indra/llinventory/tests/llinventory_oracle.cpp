/**
 * @file llinventory_oracle.cpp
 * @brief Frozen copies of the inventory data model (indra/llinventory and
 * llcommon/llassettype) as they were before vkstorm-rust rung seven, in
 * namespace llinventory_oracle, plus frozen copies of the viewer subclass
 * functions the recorders need (LLViewerInventoryItem::packMessage and the
 * LLViewerInventoryCategory cache LLSD keys) over both the frozen and the
 * live base classes. The behavioural oracle of INTEGRATION_TEST_llinventory_parity
 * and of llinventory_tool. Generated mechanically; check_r1_copies.py checks
 * every copied region against the live source. Not used by the viewer.
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#include "linden_common.h"
#include "llinventory_oracle.h"
#include <cstdarg>
#include <iostream>
#include <sstream>
#include <boost/tokenizer.hpp>
#include "lldbstrings.h"
#include "llerror.h"
#include "llfasttimer.h"
#include "llmemory.h"
#include "llsdserialize.h"
#include "llsdutil.h"
#include "llstreamtools.h"
#include "llstring.h"
#include "lltransactiontypes.h"
#include "llxorcipher.h"
#include "message.h"
#include "tea.h"

namespace llinventory_oracle
{

// ---- VKS-ORACLE-COPY BEGIN
// source: indra/llcommon/llassettype.cpp
// lines: 26-265
// blob: 9672a3262be33ff1955165e8307aac31a4d90006
// copied: the whole file after its licence block
// substitutions: #include lines and forward declarations of LLMessageSystem and LLUUID removed
// conditionals: 
// ----



///----------------------------------------------------------------------------
/// Class LLAssetType
///----------------------------------------------------------------------------
struct AssetEntry : public LLDictionaryEntry
{
    AssetEntry(const char *desc_name,
               const char *type_name,   // 8 character limit!
               const char *human_name,  // for decoding to human readable form; put any and as many printable characters you want in each one
               bool can_link,           // can you create a link to this type?
               bool can_fetch,          // can you fetch this asset by ID?
               bool can_know)           // can you see this asset's ID?
        :
        LLDictionaryEntry(desc_name),
        mTypeName(type_name),
        mHumanName(human_name),
        mCanLink(can_link),
        mCanFetch(can_fetch),
        mCanKnow(can_know)
    {
        llassert(strlen(mTypeName) <= 8);
    }

    const char *mTypeName;
    const char *mHumanName;
    bool mCanLink;
    bool mCanFetch;
    bool mCanKnow;
};

class LLAssetDictionary : public LLSingleton<LLAssetDictionary>,
                          public LLDictionary<LLAssetType::EType, AssetEntry>
{
    LLSINGLETON(LLAssetDictionary);
};

LLAssetDictionary::LLAssetDictionary()
{
    //                                                         DESCRIPTION          TYPE NAME   HUMAN NAME          CAN LINK?   CAN FETCH?  CAN KNOW?
    //                                                        |--------------------|-----------|-------------------|-----------|-----------|---------|
    addEntry(LLAssetType::AT_TEXTURE,           new AssetEntry("TEXTURE",           "texture",  "texture",          true,       false,      true));
    addEntry(LLAssetType::AT_SOUND,             new AssetEntry("SOUND",             "sound",    "sound",            true,       true,       true));
    addEntry(LLAssetType::AT_CALLINGCARD,       new AssetEntry("CALLINGCARD",       "callcard", "calling card",     true,       false,      false));
    addEntry(LLAssetType::AT_LANDMARK,          new AssetEntry("LANDMARK",          "landmark", "landmark",         true,       true,       true));
    addEntry(LLAssetType::AT_SCRIPT,            new AssetEntry("SCRIPT",            "script",   "legacy script",    true,       false,      false));
    addEntry(LLAssetType::AT_CLOTHING,          new AssetEntry("CLOTHING",          "clothing", "clothing",         true,       true,       true));
    addEntry(LLAssetType::AT_OBJECT,            new AssetEntry("OBJECT",            "object",   "object",           true,       false,      false));
    addEntry(LLAssetType::AT_NOTECARD,          new AssetEntry("NOTECARD",          "notecard", "note card",        true,       false,      true));
    addEntry(LLAssetType::AT_CATEGORY,          new AssetEntry("CATEGORY",          "category", "folder",           true,       false,      false));
    addEntry(LLAssetType::AT_LSL_TEXT,          new AssetEntry("LSL_TEXT",          "lsltext",  "lsl2 script",      true,       false,      false));
    addEntry(LLAssetType::AT_LSL_BYTECODE,      new AssetEntry("LSL_BYTECODE",      "lslbyte",  "lsl bytecode",     true,       false,      false));
    addEntry(LLAssetType::AT_TEXTURE_TGA,       new AssetEntry("TEXTURE_TGA",       "txtr_tga", "tga texture",      true,       false,      false));
    addEntry(LLAssetType::AT_BODYPART,          new AssetEntry("BODYPART",          "bodypart", "body part",        true,       true,       true));
    addEntry(LLAssetType::AT_SOUND_WAV,         new AssetEntry("SOUND_WAV",         "snd_wav",  "sound",            true,       false,      false));
    addEntry(LLAssetType::AT_IMAGE_TGA,         new AssetEntry("IMAGE_TGA",         "img_tga",  "targa image",      true,       false,      false));
    addEntry(LLAssetType::AT_IMAGE_JPEG,        new AssetEntry("IMAGE_JPEG",        "jpeg",     "jpeg image",       true,       false,      false));
    addEntry(LLAssetType::AT_ANIMATION,         new AssetEntry("ANIMATION",         "animatn",  "animation",        true,       true,       true));
    addEntry(LLAssetType::AT_GESTURE,           new AssetEntry("GESTURE",           "gesture",  "gesture",          true,       true,       true));
    addEntry(LLAssetType::AT_SIMSTATE,          new AssetEntry("SIMSTATE",          "simstate", "simstate",         false,      false,      false));

    addEntry(LLAssetType::AT_LINK,              new AssetEntry("LINK",              "link",     "sym link",         false,      false,      true));
    addEntry(LLAssetType::AT_LINK_FOLDER,       new AssetEntry("FOLDER_LINK",       "link_f",   "sym folder link",  false,      false,      true));
    addEntry(LLAssetType::AT_MESH,              new AssetEntry("MESH",              "mesh",     "mesh",             false,      false,      false));
    addEntry(LLAssetType::AT_WIDGET,            new AssetEntry("WIDGET",            "widget",   "widget",           false,      false,      false));
    addEntry(LLAssetType::AT_PERSON,            new AssetEntry("PERSON",            "person",   "person",           false,      false,      false));
    addEntry(LLAssetType::AT_SETTINGS,          new AssetEntry("SETTINGS",          "settings", "settings blob",    true,       true,       true));
    addEntry(LLAssetType::AT_MATERIAL,          new AssetEntry("MATERIAL",          "material", "render material",  true,       true,       true));
    addEntry(LLAssetType::AT_GLTF,              new AssetEntry("GLTF",              "gltf",     "GLTF",             true,       true,       true));
    addEntry(LLAssetType::AT_GLTF_BIN,          new AssetEntry("GLTF_BIN",          "glbin",    "GLTF binary",      true,       true,        true));
    addEntry(LLAssetType::AT_UNKNOWN,           new AssetEntry("UNKNOWN",           "invalid",  NULL,               false,      false,      false));
    addEntry(LLAssetType::AT_NONE,              new AssetEntry("NONE",              "-1",       NULL,               false,      false,      false));

};

const std::string LLAssetType::BADLOOKUP("llassettype_bad_lookup");

// static
LLAssetType::EType LLAssetType::getType(const std::string& desc_name)
{
    std::string s = desc_name;
    LLStringUtil::toUpper(s);
    return LLAssetDictionary::getInstance()->lookup(s);
}

// static
const std::string &LLAssetType::getDesc(LLAssetType::EType asset_type)
{
    const AssetEntry *entry = LLAssetDictionary::getInstance()->lookup(asset_type);
    if (entry)
    {
        return entry->mName;
    }
    else
    {
        return BADLOOKUP;
    }
}

// static
const char *LLAssetType::lookup(LLAssetType::EType asset_type)
{
    const LLAssetDictionary *dict = LLAssetDictionary::getInstance();
    const AssetEntry *entry = dict->lookup(asset_type);
    if (entry)
    {
        return entry->mTypeName;
    }
    else
    {
        return BADLOOKUP.c_str();
    }
}

// static
LLAssetType::EType LLAssetType::lookup(const char* name)
{
    return lookup(ll_safe_string(name));
}

// static
LLAssetType::EType LLAssetType::lookup(const std::string& type_name)
{
    const LLAssetDictionary *dict = LLAssetDictionary::getInstance();
    for (const LLAssetDictionary::value_type& pair : *dict)
    {
        const AssetEntry *entry = pair.second;
        if (type_name == entry->mTypeName)
        {
            return pair.first;
        }
    }
    return AT_UNKNOWN;
}

// static
const char *LLAssetType::lookupHumanReadable(LLAssetType::EType asset_type)
{
    const LLAssetDictionary *dict = LLAssetDictionary::getInstance();
    const AssetEntry *entry = dict->lookup(asset_type);
    if (entry)
    {
        return entry->mHumanName;
    }
    else
    {
        return BADLOOKUP.c_str();
    }
}

// static
LLAssetType::EType LLAssetType::lookupHumanReadable(const char* name)
{
    return lookupHumanReadable(ll_safe_string(name));
}

// static
LLAssetType::EType LLAssetType::lookupHumanReadable(const std::string& readable_name)
{
    const LLAssetDictionary *dict = LLAssetDictionary::getInstance();
    for (const LLAssetDictionary::value_type& pair : *dict)
    {
        const AssetEntry *entry = pair.second;
        if (entry->mHumanName && (readable_name == entry->mHumanName))
        {
            return pair.first;
        }
    }
    return AT_NONE;
}

// static
bool LLAssetType::lookupCanLink(EType asset_type)
{
    const LLAssetDictionary *dict = LLAssetDictionary::getInstance();
    const AssetEntry *entry = dict->lookup(asset_type);
    if (entry)
    {
        return entry->mCanLink;
    }
    return false;
}

// static
// Not adding this to dictionary since we probably will only have these two types
bool LLAssetType::lookupIsLinkType(EType asset_type)
{
    if (asset_type == AT_LINK || asset_type == AT_LINK_FOLDER)
    {
        return true;
    }
    return false;
}

// static
bool LLAssetType::lookupIsAssetFetchByIDAllowed(EType asset_type)
{
    const LLAssetDictionary *dict = LLAssetDictionary::getInstance();
    const AssetEntry *entry = dict->lookup(asset_type);
    if (entry)
    {
        return entry->mCanFetch;
    }
    return false;
}

// static
bool LLAssetType::lookupIsAssetIDKnowable(EType asset_type)
{
    const LLAssetDictionary *dict = LLAssetDictionary::getInstance();
    const AssetEntry *entry = dict->lookup(asset_type);
    if (entry)
    {
        return entry->mCanKnow;
    }
    return false;
}

LLSD LLAssetType::getTypeNames()
{
    LLSD type_names;
    const LLAssetDictionary *dict = LLAssetDictionary::getInstance();
    for (S32 type = AT_TEXTURE; type < AT_COUNT; ++type)
    {
        const AssetEntry *entry = dict->lookup((LLAssetType::EType) type);
        // skip llassettype_bad_lookup
        if (entry)
        {
            type_names.append(entry->mTypeName);
        }
    }
    return type_names;
}
// ---- VKS-ORACLE-COPY END

// ---- VKS-ORACLE-COPY BEGIN
// source: indra/llinventory/llinventorytype.cpp
// lines: 26-247
// blob: 8fa8983d3c418f17346ede78072061e4a8cf556f
// copied: the whole file after its licence block
// substitutions: #include lines and forward declarations of LLMessageSystem and LLUUID removed
// conditionals: 
// ----



static const std::string empty_string;

///----------------------------------------------------------------------------
/// Class LLInventoryType
///----------------------------------------------------------------------------
struct InventoryEntry : public LLDictionaryEntry
{
    InventoryEntry(const std::string &name, // unlike asset type names, not limited to 8 characters; need not match asset type names
                   const std::string &human_name, // for decoding to human readable form; put any and as many printable characters you want in each one.
                   int num_asset_types = 0, ...)
        :
        LLDictionaryEntry(name),
        mHumanName(human_name)
    {
        va_list argp;
        va_start(argp, num_asset_types);
        // Read in local textures
        for (U8 i=0; i < num_asset_types; i++)
        {
            LLAssetType::EType t = (LLAssetType::EType)va_arg(argp,int);
            mAssetTypes.push_back(t);
        }
        va_end(argp);
    }

    const std::string mHumanName;
    typedef std::vector<LLAssetType::EType> asset_vec_t;
    asset_vec_t mAssetTypes;
};

class LLInventoryDictionary : public LLSingleton<LLInventoryDictionary>,
                              public LLDictionary<LLInventoryType::EType, InventoryEntry>
{
    LLSINGLETON(LLInventoryDictionary);
};

LLInventoryDictionary::LLInventoryDictionary()
{
    addEntry(LLInventoryType::IT_TEXTURE,             new InventoryEntry("texture",   "texture",       1, LLAssetType::AT_TEXTURE));
    addEntry(LLInventoryType::IT_SOUND,               new InventoryEntry("sound",     "sound",         1, LLAssetType::AT_SOUND));
    addEntry(LLInventoryType::IT_CALLINGCARD,         new InventoryEntry("callcard",  "calling card",  1, LLAssetType::AT_CALLINGCARD));
    addEntry(LLInventoryType::IT_LANDMARK,            new InventoryEntry("landmark",  "landmark",      1, LLAssetType::AT_LANDMARK));
    addEntry(LLInventoryType::IT_OBJECT,              new InventoryEntry("object",    "object",        1, LLAssetType::AT_OBJECT));
    addEntry(LLInventoryType::IT_NOTECARD,            new InventoryEntry("notecard",  "note card",     1, LLAssetType::AT_NOTECARD));
    addEntry(LLInventoryType::IT_CATEGORY,            new InventoryEntry("category",  "folder"         ));
    addEntry(LLInventoryType::IT_ROOT_CATEGORY,       new InventoryEntry("root",      "root"           ));
    addEntry(LLInventoryType::IT_LSL,                 new InventoryEntry("script",    "script",        2, LLAssetType::AT_LSL_TEXT, LLAssetType::AT_LSL_BYTECODE));
    addEntry(LLInventoryType::IT_SNAPSHOT,            new InventoryEntry("snapshot",  "snapshot",      1, LLAssetType::AT_TEXTURE));
    addEntry(LLInventoryType::IT_ATTACHMENT,          new InventoryEntry("attach",    "attachment",    1, LLAssetType::AT_OBJECT));
    addEntry(LLInventoryType::IT_WEARABLE,            new InventoryEntry("wearable",  "wearable",      2, LLAssetType::AT_CLOTHING, LLAssetType::AT_BODYPART));
    addEntry(LLInventoryType::IT_ANIMATION,           new InventoryEntry("animation", "animation",     1, LLAssetType::AT_ANIMATION));
    addEntry(LLInventoryType::IT_GESTURE,             new InventoryEntry("gesture",   "gesture",       1, LLAssetType::AT_GESTURE));
    addEntry(LLInventoryType::IT_MESH,                new InventoryEntry("mesh",      "mesh",          1, LLAssetType::AT_MESH));
    addEntry(LLInventoryType::IT_GLTF,                new InventoryEntry("gltf",      "gltf",          1, LLAssetType::AT_GLTF));
    addEntry(LLInventoryType::IT_GLTF_BIN,            new InventoryEntry("glbin",     "glbin",         1, LLAssetType::AT_GLTF_BIN));
    addEntry(LLInventoryType::IT_WIDGET,              new InventoryEntry("widget",    "widget",        1, LLAssetType::AT_WIDGET));
    addEntry(LLInventoryType::IT_PERSON,              new InventoryEntry("person",    "person",        1, LLAssetType::AT_PERSON));
    addEntry(LLInventoryType::IT_SETTINGS,            new InventoryEntry("settings",  "settings",      1, LLAssetType::AT_SETTINGS));
    addEntry(LLInventoryType::IT_MATERIAL,            new InventoryEntry("material",  "render material", 1, LLAssetType::AT_MATERIAL));
}


// Maps asset types to the default inventory type for that kind of asset.
// Thus, "Lost and Found" is a "Category"
static const LLInventoryType::EType
DEFAULT_ASSET_FOR_INV_TYPE[LLAssetType::AT_COUNT] =
{
    LLInventoryType::IT_TEXTURE,        // 0    AT_TEXTURE
    LLInventoryType::IT_SOUND,          // 1    AT_SOUND
    LLInventoryType::IT_CALLINGCARD,    // 2    AT_CALLINGCARD
    LLInventoryType::IT_LANDMARK,       // 3    AT_LANDMARK
    LLInventoryType::IT_LSL,            // 4    AT_SCRIPT
    LLInventoryType::IT_WEARABLE,       // 5    AT_CLOTHING
    LLInventoryType::IT_OBJECT,         // 6    AT_OBJECT
    LLInventoryType::IT_NOTECARD,       // 7    AT_NOTECARD
    LLInventoryType::IT_CATEGORY,       // 8    AT_CATEGORY
    LLInventoryType::IT_NONE,           // 9    (null entry)
    LLInventoryType::IT_LSL,            // 10   AT_LSL_TEXT
    LLInventoryType::IT_LSL,            // 11   AT_LSL_BYTECODE
    LLInventoryType::IT_TEXTURE,        // 12   AT_TEXTURE_TGA
    LLInventoryType::IT_WEARABLE,       // 13   AT_BODYPART
    LLInventoryType::IT_CATEGORY,       // 14   AT_TRASH
    LLInventoryType::IT_CATEGORY,       // 15   AT_SNAPSHOT_CATEGORY
    LLInventoryType::IT_CATEGORY,       // 16   AT_LOST_AND_FOUND
    LLInventoryType::IT_SOUND,          // 17   AT_SOUND_WAV
    LLInventoryType::IT_NONE,           // 18   AT_IMAGE_TGA
    LLInventoryType::IT_NONE,           // 19   AT_IMAGE_JPEG
    LLInventoryType::IT_ANIMATION,      // 20   AT_ANIMATION
    LLInventoryType::IT_GESTURE,        // 21   AT_GESTURE
    LLInventoryType::IT_NONE,           // 22   AT_SIMSTATE

    LLInventoryType::IT_NONE,           // 23   AT_LINK
    LLInventoryType::IT_NONE,           // 24   AT_LINK_FOLDER

    LLInventoryType::IT_NONE,           // 25   AT_NONE
    LLInventoryType::IT_NONE,           // 26   AT_NONE
    LLInventoryType::IT_NONE,           // 27   AT_NONE
    LLInventoryType::IT_NONE,           // 28   AT_NONE
    LLInventoryType::IT_NONE,           // 29   AT_NONE
    LLInventoryType::IT_NONE,           // 30   AT_NONE
    LLInventoryType::IT_NONE,           // 31   AT_NONE
    LLInventoryType::IT_NONE,           // 32   AT_NONE
    LLInventoryType::IT_NONE,           // 33   AT_NONE
    LLInventoryType::IT_NONE,           // 34   AT_NONE
    LLInventoryType::IT_NONE,           // 35   AT_NONE
    LLInventoryType::IT_NONE,           // 36   AT_NONE
    LLInventoryType::IT_NONE,           // 37   AT_NONE
    LLInventoryType::IT_NONE,           // 38   AT_NONE
    LLInventoryType::IT_NONE,           // 39   AT_NONE
    LLInventoryType::IT_WIDGET,         // 40   AT_WIDGET
    LLInventoryType::IT_NONE,           // 41   AT_NONE
    LLInventoryType::IT_NONE,           // 42   AT_NONE
    LLInventoryType::IT_NONE,           // 43   AT_NONE
    LLInventoryType::IT_NONE,           // 44   AT_NONE
    LLInventoryType::IT_PERSON,         // 45   AT_PERSON
    LLInventoryType::IT_NONE,           // 46   AT_NONE
    LLInventoryType::IT_NONE,           // 47   AT_NONE
    LLInventoryType::IT_NONE,           // 48   AT_NONE
    LLInventoryType::IT_MESH,           // 49   AT_MESH

    LLInventoryType::IT_NONE,           // 50   AT_RESERVED_1
    LLInventoryType::IT_NONE,           // 51   AT_RESERVED_2
    LLInventoryType::IT_NONE,           // 52   AT_RESERVED_3
    LLInventoryType::IT_NONE,           // 53   AT_RESERVED_4
    LLInventoryType::IT_NONE,           // 54   AT_RESERVED_5
    LLInventoryType::IT_NONE,           // 55   AT_RESERVED_6

    LLInventoryType::IT_SETTINGS,       // 56   AT_SETTINGS
    LLInventoryType::IT_MATERIAL,       // 57   AT_MATERIAL
    LLInventoryType::IT_GLTF,           // 58   AT_GLTF
    LLInventoryType::IT_GLTF_BIN,       // 59   AT_GLTF_BIN
};

// static
const std::string &LLInventoryType::lookup(EType type)
{
    const InventoryEntry *entry = LLInventoryDictionary::getInstance()->lookup(type);
    if (!entry) return empty_string;
    return entry->mName;
}

// static
LLInventoryType::EType LLInventoryType::lookup(const std::string& name)
{
    return LLInventoryDictionary::getInstance()->lookup(name);
}

// XUI:translate
// translation from a type to a human readable form.
// static
const std::string &LLInventoryType::lookupHumanReadable(EType type)
{
    const InventoryEntry *entry = LLInventoryDictionary::getInstance()->lookup(type);
    if (!entry) return empty_string;
    return entry->mHumanName;
}

// return the default inventory for the given asset type.
// static
LLInventoryType::EType LLInventoryType::defaultForAssetType(LLAssetType::EType asset_type)
{
    if((asset_type >= 0) && (asset_type < LLAssetType::AT_COUNT))
    {
        return DEFAULT_ASSET_FOR_INV_TYPE[S32(asset_type)];
    }
    else
    {
        return IT_UNKNOWN;
    }
}


// add any types that we don't want the user to be able to change permissions on.
// static
bool LLInventoryType::cannotRestrictPermissions(LLInventoryType::EType type)
{
    switch(type)
    {
        case IT_CALLINGCARD:
        case IT_LANDMARK:
            return true;
        default:
            return false;
    }
}

// Should show permissions that apply only to objects rezed in world.
bool LLInventoryType::showInWorldPermissions(LLInventoryType::EType type)
{
    return (type != IT_SETTINGS);
}

bool inventory_and_asset_types_match(LLInventoryType::EType inventory_type,
                                     LLAssetType::EType asset_type)
{
    // Links can be of any inventory type.
    if (LLAssetType::lookupIsLinkType(asset_type))
        return true;

    const InventoryEntry *entry = LLInventoryDictionary::getInstance()->lookup(inventory_type);
    if (!entry) return false;

    for (InventoryEntry::asset_vec_t::const_iterator iter = entry->mAssetTypes.begin();
         iter != entry->mAssetTypes.end();
         iter++)
    {
        const LLAssetType::EType type = (*iter);
        if(type == asset_type)
        {
            return true;
        }
    }
    return false;
}
// ---- VKS-ORACLE-COPY END

// ---- VKS-ORACLE-COPY BEGIN
// source: indra/llinventory/llfoldertype.cpp
// lines: 26-243
// blob: 1be616f5fe1acd51de1b6d2cf7d68db9d5ed681b
// copied: the whole file after its licence block
// substitutions: #include lines and forward declarations of LLMessageSystem and LLUUID removed
// conditionals: 
// ----



///----------------------------------------------------------------------------
/// Class LLFolderType
///----------------------------------------------------------------------------
struct FolderEntry : public LLDictionaryEntry
{
    FolderEntry(const std::string &type_name, // 8 character limit!
                bool is_protected, // can the viewer change categories of this type?
                bool is_automatic, // always made before first login?
                bool is_singleton  // should exist as a unique copy under root
        )
        :
    LLDictionaryEntry(type_name),
    mIsProtected(is_protected),
    mIsAutomatic(is_automatic),
    mIsSingleton(is_singleton)
    {
        llassert(type_name.length() <= 8);
    }

    const bool mIsProtected;
    const bool mIsAutomatic;
    const bool mIsSingleton;
};

class LLFolderDictionary : public LLSingleton<LLFolderDictionary>,
                           public LLDictionary<LLFolderType::EType, FolderEntry>
{
    LLSINGLETON(LLFolderDictionary);
protected:
    virtual LLFolderType::EType notFound() const override
    {
        return LLFolderType::FT_NONE;
    }
};

// Folder types
//
// PROTECTED means that folders of this type can't be moved, deleted
// or otherwise modified by the viewer.
//
// SINGLETON means that there should always be exactly one folder of
// this type, and it should be the root or a child of the root. This
// is true for most types of folders.
//
// AUTOMATIC means that a copy of this folder should be created under
// the root before the user ever logs in, and should never be created
// from the viewer. A missing AUTOMATIC folder should be treated as a
// fatal error by the viewer, since it indicates either corrupted
// inventory or a failure in the inventory services.
//
LLFolderDictionary::LLFolderDictionary()
{
    //                                                              TYPE NAME, PROTECTED, AUTOMATIC, SINGLETON
    addEntry(LLFolderType::FT_TEXTURE,              new FolderEntry("texture",  true, true, true));
    addEntry(LLFolderType::FT_SOUND,                new FolderEntry("sound",    true, true, true));
    addEntry(LLFolderType::FT_CALLINGCARD,          new FolderEntry("callcard", true, true, false));
    addEntry(LLFolderType::FT_LANDMARK,             new FolderEntry("landmark", true, false, false));
    addEntry(LLFolderType::FT_CLOTHING,             new FolderEntry("clothing", true, true, true));
    addEntry(LLFolderType::FT_OBJECT,               new FolderEntry("object",   true, true, true));
    addEntry(LLFolderType::FT_NOTECARD,             new FolderEntry("notecard", true, true, true));
    addEntry(LLFolderType::FT_ROOT_INVENTORY,       new FolderEntry("root_inv", true, true, true));
    addEntry(LLFolderType::FT_LSL_TEXT,             new FolderEntry("lsltext",  true, true, true));
    addEntry(LLFolderType::FT_BODYPART,             new FolderEntry("bodypart", true, true, true));
    addEntry(LLFolderType::FT_TRASH,                new FolderEntry("trash",    true, false, true));
    addEntry(LLFolderType::FT_SNAPSHOT_CATEGORY,    new FolderEntry("snapshot", true, true, true));
    addEntry(LLFolderType::FT_LOST_AND_FOUND,       new FolderEntry("lstndfnd", true, true, true));
    addEntry(LLFolderType::FT_ANIMATION,            new FolderEntry("animatn",  true, true, true));
    addEntry(LLFolderType::FT_GESTURE,              new FolderEntry("gesture",  true, true, true));
    addEntry(LLFolderType::FT_FAVORITE,             new FolderEntry("favorite", true, false, true));

    for (S32 ensemble_num = S32(LLFolderType::FT_ENSEMBLE_START); ensemble_num <= S32(LLFolderType::FT_ENSEMBLE_END); ensemble_num++)
    {
        addEntry(LLFolderType::EType(ensemble_num), new FolderEntry("ensemble", false, false, false)); // Not used
    }

    addEntry(LLFolderType::FT_CURRENT_OUTFIT,       new FolderEntry("current",  true, false, true));
    addEntry(LLFolderType::FT_OUTFIT,               new FolderEntry("outfit",   false, false, false));
    addEntry(LLFolderType::FT_MY_OUTFITS,           new FolderEntry("my_otfts", true, false, true));

    addEntry(LLFolderType::FT_MESH,                 new FolderEntry("mesh",     true, false, false)); // Not used?

    addEntry(LLFolderType::FT_INBOX,                new FolderEntry("inbox",    true, false, true));
    addEntry(LLFolderType::FT_OUTBOX,               new FolderEntry("outbox",   false, false, false)); // <FS:Ansariel> Make obsolete Merchant Outbox folder deletable

    addEntry(LLFolderType::FT_BASIC_ROOT,           new FolderEntry("basic_rt", true, false, false));

    addEntry(LLFolderType::FT_MARKETPLACE_LISTINGS, new FolderEntry("merchant", false, false, false));
    addEntry(LLFolderType::FT_MARKETPLACE_STOCK,    new FolderEntry("stock",    false, false, false));
    addEntry(LLFolderType::FT_MARKETPLACE_VERSION,  new FolderEntry("version",  false, false, false));

    addEntry(LLFolderType::FT_SETTINGS,             new FolderEntry("settings", true, false, true));
    addEntry(LLFolderType::FT_MATERIAL,             new FolderEntry("material", true, false, true));

    addEntry(LLFolderType::FT_MY_SUITCASE,          new FolderEntry("suitcase", true, false, true)); // <FS:Ansariel> OpenSim HG-support

    addEntry(LLFolderType::FT_NONE,                 new FolderEntry("-1",       false, false, false));
};

// static
LLFolderType::EType LLFolderType::lookup(const std::string& name)
{
    return LLFolderDictionary::getInstance()->lookup(name);
}

// static
const std::string &LLFolderType::lookup(LLFolderType::EType folder_type)
{
    const FolderEntry *entry = LLFolderDictionary::getInstance()->lookup(folder_type);
    if (entry)
    {
        return entry->mName;
    }
    else
    {
        return badLookup();
    }
}

// static
// Only plain folders and a few other types aren't protected.  "Protected" means
// you can't move, deleted, or change certain properties such as their type.
bool LLFolderType::lookupIsProtectedType(EType folder_type)
{
    const LLFolderDictionary *dict = LLFolderDictionary::getInstance();
    const FolderEntry *entry = dict->lookup(folder_type);
    if (entry)
    {
        return entry->mIsProtected;
    }
    return true;
}

// static
// Is this folder type automatically created outside the viewer?
bool LLFolderType::lookupIsAutomaticType(EType folder_type)
{
    const LLFolderDictionary *dict = LLFolderDictionary::getInstance();
    const FolderEntry *entry = dict->lookup(folder_type);
    if (entry)
    {
        return entry->mIsAutomatic;
    }
    return true;
}

// static
// Should this folder always exist as a single copy under (or as) the root?
bool LLFolderType::lookupIsSingletonType(EType folder_type)
{
    const LLFolderDictionary *dict = LLFolderDictionary::getInstance();
    const FolderEntry *entry = dict->lookup(folder_type);
    if (entry)
    {
        return entry->mIsSingleton;
    }
    return true;
}

// static
bool LLFolderType::lookupIsEnsembleType(EType folder_type)
{
    return (folder_type >= FT_ENSEMBLE_START &&
            folder_type <= FT_ENSEMBLE_END);
}

// static
LLAssetType::EType LLFolderType::folderTypeToAssetType(LLFolderType::EType folder_type)
{
    if (LLAssetType::lookup(LLAssetType::EType(folder_type)) == LLAssetType::BADLOOKUP)
    {
        LL_WARNS() << "Converting to unknown asset type " << folder_type << LL_ENDL;
    }
    return (LLAssetType::EType)folder_type;
}

// static
LLFolderType::EType LLFolderType::assetTypeToFolderType(LLAssetType::EType asset_type)
{
    if (LLFolderType::lookup(LLFolderType::EType(asset_type)) == LLFolderType::badLookup())
    {
        LL_WARNS() << "Converting to unknown folder type " << asset_type << LL_ENDL;
    }
    return (LLFolderType::EType)asset_type;
}

// static
const std::string &LLFolderType::badLookup()
{
    static const std::string sBadLookup = "llfoldertype_bad_lookup";
    return sBadLookup;
}

LLSD LLFolderType::getTypeNames()
{
    LLSD type_names;
    for (S32 type = FT_TEXTURE; type < FT_COUNT; ++type)
    {
        if (lookupIsEnsembleType((LLFolderType::EType)type))
            continue;

        const FolderEntry* entry = LLFolderDictionary::getInstance()->lookup((LLFolderType::EType)type);
        // skip llfoldertype_bad_lookup
        if (entry)
        {
            type_names.append(entry->mName);
        }
    }
    return type_names;
}
// ---- VKS-ORACLE-COPY END

// ---- VKS-ORACLE-COPY BEGIN
// source: indra/llinventory/llinventorydefines.cpp
// lines: 26-31
// blob: e05e89f549e026af3cf115f1c581975f37fd7a0f
// copied: the whole file after its licence block
// substitutions: #include lines and forward declarations of LLMessageSystem and LLUUID removed
// conditionals: 
// ----


const U8 TASK_INVENTORY_ITEM_KEY = 0;
const U8 TASK_INVENTORY_ASSET_KEY = 1;
// ---- VKS-ORACLE-COPY END

// ---- VKS-ORACLE-COPY BEGIN
// source: indra/llinventory/llpermissions.cpp
// lines: 27-1122
// blob: d14c81a6bb271d8ed46a328edffa5aa55d80c327
// copied: the whole file after its licence block
// substitutions: #include lines and forward declarations of LLMessageSystem and LLUUID removed
// conditionals: 
// ----



// library includes

///----------------------------------------------------------------------------
/// Class LLPermissions
///----------------------------------------------------------------------------

const LLPermissions LLPermissions::DEFAULT;

bool LLPermissions::sIsInOpenSim = false; // <FS:TJ/> [FIRE-36028] Fix OpenSim object permissions

// No creator = created by system
LLPermissions::LLPermissions()
{
    init(LLUUID::null, LLUUID::null, LLUUID::null, LLUUID::null);
}


// Default to created by system
void LLPermissions::init(const LLUUID& creator, const LLUUID& owner, const LLUUID& last_owner, const LLUUID& group)
{
    mCreator    = creator;
    mOwner      = owner;
    mLastOwner  = last_owner;
    mGroup      = group;

    mMaskBase       = PERM_ALL;
    mMaskOwner      = PERM_ALL;
    mMaskEveryone   = PERM_ALL;
    mMaskGroup      = PERM_ALL;
    mMaskNextOwner = PERM_ALL;
    fixOwnership();
}


void LLPermissions::initMasks(PermissionMask base, PermissionMask owner,
                              PermissionMask everyone, PermissionMask group,
                              PermissionMask next)
{
    mMaskBase       = base;
    mMaskOwner      = owner;
    mMaskEveryone   = everyone;
    mMaskGroup      = group;
    mMaskNextOwner = next;
    fixFairUse();
    fix();
}

// ! BACKWARDS COMPATIBILITY ! Override masks for inventory types that
// no longer can have restricted permissions.  This takes care of previous
// version landmarks that could have had no copy/mod/transfer bits set.
void LLPermissions::initMasks(LLInventoryType::EType type)
{
    if (LLInventoryType::cannotRestrictPermissions(type))
    {
        initMasks(PERM_ALL, PERM_ALL, PERM_ALL, PERM_ALL, PERM_ALL);
    }
}

bool LLPermissions::getOwnership(LLUUID& owner_id, bool& is_group_owned) const
{
    if(mOwner.notNull())
    {
        owner_id = mOwner;
        is_group_owned = false;
        return true;
    }
    else if(mIsGroupOwned)
    {
        owner_id = mGroup;
        is_group_owned = true;
        return true;
    }
    return false;
}

LLUUID LLPermissions::getSafeOwner() const
{
    if(mOwner.notNull())
    {
        return mOwner;
    }
    else if(mIsGroupOwned)
    {
        return mGroup;
    }
    else
    {
        LL_WARNS() << "LLPermissions::getSafeOwner() called with no valid owner!" << LL_ENDL;
        LLUUID unused_uuid;
        unused_uuid.generate();

        return unused_uuid;
    }
}

U32 LLPermissions::getCRC32() const
{
    U32 rv = mCreator.getCRC32();
    rv += mOwner.getCRC32();
    rv += mLastOwner.getCRC32();
    rv += mGroup.getCRC32();
    rv += mMaskBase + mMaskOwner + mMaskEveryone + mMaskGroup;
    return rv;
}

void LLPermissions::set(const LLPermissions& from)
{
    mCreator    = from.mCreator;
    mOwner      = from.mOwner;
    mLastOwner  = from.mLastOwner;
    mGroup      = from.mGroup;

    mMaskBase       = from.mMaskBase;
    mMaskOwner      = from.mMaskOwner;
    mMaskEveryone   = from.mMaskEveryone;
    mMaskGroup      = from.mMaskGroup;
    mMaskNextOwner = from.mMaskNextOwner;
    mIsGroupOwned = from.mIsGroupOwned;
}

// Fix hierarchy of permissions.
void LLPermissions::fix()
{
    mMaskOwner &= mMaskBase;
    mMaskGroup &= mMaskOwner;
    // next owner uses base, since you may want to sell locked objects.
    mMaskNextOwner &= mMaskBase;
    mMaskEveryone &= mMaskOwner;
    mMaskEveryone &= ~PERM_MODIFY;
    if(!(mMaskBase & PERM_TRANSFER) && !mIsGroupOwned)
    {
        mMaskGroup &= ~PERM_COPY;
        mMaskEveryone &= ~PERM_COPY;
        // Do not set mask next owner to too restrictive because if we
        // rez an object, it may require an ownership transfer during
        // rez, which will note the overly restrictive perms, and then
        // fix them to allow fair use, which may be different than the
        // original intention.
    }
}

// Correct for fair use - you can never take away the right to move
// stuff you own, and you can never take away the right to transfer
// something you cannot otherwise copy.
void LLPermissions::fixFairUse()
{
    mMaskBase |= PERM_MOVE;
    if(!(mMaskBase & PERM_COPY))
    {
        mMaskBase |= PERM_TRANSFER;
    }
    // (mask next owner == PERM_NONE) iff mask base is no transfer
    if(mMaskNextOwner != PERM_NONE)
    {
        mMaskNextOwner |= PERM_MOVE;
    }
}

void LLPermissions::fixOwnership()
{
    if(mOwner.isNull() && mGroup.notNull())
    {
        mIsGroupOwned = true;
    }
    else
    {
        mIsGroupOwned = false;
    }
}

// Allow accumulation of permissions. Results in the tightest
// permissions possible. In the case of clashing UUIDs, it sets the ID
// to LLUUID::null.
void LLPermissions::accumulate(const LLPermissions& perm)
{
    if(perm.mCreator != mCreator)
    {
        mCreator = LLUUID::null;
    }
    if(perm.mOwner != mOwner)
    {
        mOwner = LLUUID::null;
    }
    if(perm.mLastOwner != mLastOwner)
    {
        mLastOwner = LLUUID::null;
    }
    if(perm.mGroup != mGroup)
    {
        mGroup = LLUUID::null;
    }

    mMaskBase &= perm.mMaskBase;
    mMaskOwner &= perm.mMaskOwner;
    mMaskGroup &= perm.mMaskGroup;
    mMaskEveryone &= perm.mMaskEveryone;
    mMaskNextOwner &= perm.mMaskNextOwner;
    fix();
}

// saves last owner, sets current owner, and sets the group.  note
// that this function has to more cleverly apply the fair use
// permissions.
bool LLPermissions::setOwnerAndGroup(
    const LLUUID& agent,
    const LLUUID& owner,
    const LLUUID& group,
    bool is_atomic)
{
    bool allowed = false;

    if( agent.isNull() || mOwner.isNull()
       || ((agent == mOwner) && ((owner == mOwner) || (mMaskOwner & PERM_TRANSFER)) ) )
    {
        // ...system can alway set owner
        // ...public objects can be claimed by anyone
        // ...otherwise, agent must own it and have transfer ability
        allowed = true;
    }

    if (allowed)
    {
        if(mLastOwner.isNull() || (!mOwner.isNull() && (owner != mLastOwner)))
        {
            mLastOwner = mOwner;
        }
        if((mOwner != owner)
           || (mOwner.isNull() && owner.isNull() && (mGroup != group)))
        {
            mMaskBase = mMaskNextOwner;
            mOwner = owner;
            // this is a selective use of fair use for atomic
            // permissions.
            if(is_atomic && !(mMaskBase & PERM_COPY))
            {
                mMaskBase |= PERM_TRANSFER;
            }
        }
        mGroup = group;
        fixOwnership();
        // if it's not atomic and we fix fair use, it blows away
        //objects as inventory items which have different permissions
        //than it's contents. :(
        // fixFairUse();
        mMaskBase |= PERM_MOVE;
        if(mMaskNextOwner != PERM_NONE) mMaskNextOwner |= PERM_MOVE;
        fix();
    }

    return allowed;
}

//Fix for DEV-33917, last owner isn't used much and has little impact on
//permissions so it's reasonably safe to do this, however, for now,
//limiting the functionality of this routine to objects which are
//group owned.
void LLPermissions::setLastOwner(const LLUUID& last_owner)
{
    if (isGroupOwned())
        mLastOwner = last_owner;
}


// only call this if you know what you're doing
// there are usually perm-bit consequences when the
// ownerhsip changes
void LLPermissions::yesReallySetOwner(const LLUUID& owner, bool group_owned)
{
    mOwner = owner;
    mIsGroupOwned = group_owned;
}

bool LLPermissions::deedToGroup(const LLUUID& agent, const LLUUID& group)
{
    if(group.notNull() && (agent.isNull() || ((group == mGroup)
                                              && (mMaskOwner & PERM_TRANSFER)
                                              && (mMaskGroup & PERM_MOVE))))
    {
        if(mOwner.notNull())
        {
            mLastOwner = mOwner;
            mOwner.setNull();
        }
        mMaskBase = mMaskNextOwner;
        mMaskGroup = PERM_NONE;
        mGroup = group;
        mIsGroupOwned = true;
        fixFairUse();
        fix();
        return true;
    }
    return false;
}

bool LLPermissions::setBaseBits(const LLUUID& agent, bool set, PermissionMask bits)
{
    bool ownership = false;
    if(agent.isNull())
    {
        // only the system is always allowed to change base bits
        ownership = true;
    }

    if (ownership)
    {
        if (set)
        {
            mMaskBase |= bits;      // turn on bits
        }
        else
        {
            mMaskBase &= ~bits;     // turn off bits
        }
        fix();
    }

    return ownership;
}


// Note: If you attempt to set bits that the base bits doesn't allow,
// the function will succeed, but those bits will not be set.
bool LLPermissions::setOwnerBits(const LLUUID& agent, bool set, PermissionMask bits)
{
    bool ownership = false;

    if(agent.isNull())
    {
        // ...system always allowed to change things
        ownership = true;
    }
    else if (agent == mOwner)
    {
        // ...owner bits can only be set by owner
        ownership = true;
    }

    // If we have correct ownership and
    if (ownership)
    {
        if (set)
        {
            mMaskOwner |= bits;         // turn on bits
        }
        else
        {
            mMaskOwner &= ~bits;        // turn off bits
        }
        fix();
    }

    return (ownership);
}

bool LLPermissions::setGroupBits(const LLUUID& agent, const LLUUID& group, bool set, PermissionMask bits)
{
    bool ownership = false;
    if((agent.isNull()) || (agent == mOwner)
       || ((group == mGroup) && (!mGroup.isNull())))
    {
        // The group bits can be set by the system, the owner, or a
        // group member.
        ownership = true;
    }

    if (ownership)
    {
        if (set)
        {
            mMaskGroup |= bits;
        }
        else
        {
            mMaskGroup &= ~bits;
        }
        fix();
    }
    return ownership;
}


// Note: If you attempt to set bits that the creator or owner doesn't allow,
// the function will succeed, but those bits will not be set.
bool LLPermissions::setEveryoneBits(const LLUUID& agent, const LLUUID& group, bool set, PermissionMask bits)
{
    bool ownership = false;
    if((agent.isNull()) || (agent == mOwner)
       || ((group == mGroup) && (!mGroup.isNull())))
    {
        // The everyone bits can be set by the system, the owner, or a
        // group member.
        ownership = true;
    }
    if (ownership)
    {
        if (set)
        {
            mMaskEveryone |= bits;
        }
        else
        {
            mMaskEveryone &= ~bits;
        }

        // Fix hierarchy of permissions
        fix();
    }
    return ownership;
}

// Note: If you attempt to set bits that the creator or owner doesn't allow,
// the function will succeed, but those bits will not be set.
bool LLPermissions::setNextOwnerBits(const LLUUID& agent, const LLUUID& group, bool set, PermissionMask bits)
{
    bool ownership = false;
    if((agent.isNull()) || (agent == mOwner)
       || ((group == mGroup) && (!mGroup.isNull())))
    {
        // The next owner bits can be set by the system, the owner, or
        // a group member.
        ownership = true;
    }
    if (ownership)
    {
        if (set)
        {
            mMaskNextOwner |= bits;
        }
        else
        {
            mMaskNextOwner &= ~bits;
        }

        // Fix-up permissions
        if(!(mMaskNextOwner & PERM_COPY))
        {
            mMaskNextOwner |= PERM_TRANSFER;
        }
        fix();
    }
    return ownership;
}

bool LLPermissions::allowOperationBy(PermissionBit op, const LLUUID& requester, const LLUUID& group) const
{
    if(requester.isNull())
    {
        // ...system making request
        // ...not owned
        return true;
    }
    else if (mIsGroupOwned && (mGroup == requester))
    {
        // group checking ownership permissions
        return (mMaskOwner & op);
    }
    else if (!mIsGroupOwned && (mOwner == requester))
    {
        // ...owner making request
        return (mMaskOwner & op);
    }
    else if(mGroup.notNull() && (mGroup == group))
    {
        // group member making request
        return ((mMaskGroup & op) || (mMaskEveryone & op));
    }
    return (mMaskEveryone & op);
}

//
// LLSD support for HTTP messages.
//
LLSD LLPermissions::packMessage() const
{
    LLSD result;
    result["creator-id"]    = mCreator;
    result["owner-id"]      = mOwner;
    result["group-id"]      = mGroup;

    result["base-mask"]     =   (S32)mMaskBase;
    result["owner-mask"]    =   (S32)mMaskOwner;
    result["group-mask"]    =   (S32)mMaskGroup;
    result["everyone-mask"] =   (S32)mMaskEveryone;
    result["next-owner-mask"]=  (S32)mMaskNextOwner;
    result["group-owned"]   = (bool)mIsGroupOwned;
    return result;
}

//
// Messaging support
//
void LLPermissions::packMessage(LLMessageSystem* msg) const
{
    msg->addUUIDFast(_PREHASH_CreatorID, mCreator);
    msg->addUUIDFast(_PREHASH_OwnerID, mOwner);
    msg->addUUIDFast(_PREHASH_GroupID, mGroup);

    msg->addU32Fast(_PREHASH_BaseMask,      mMaskBase );
    msg->addU32Fast(_PREHASH_OwnerMask, mMaskOwner );
    msg->addU32Fast(_PREHASH_GroupMask, mMaskGroup );
    msg->addU32Fast(_PREHASH_EveryoneMask,  mMaskEveryone );
    msg->addU32Fast(_PREHASH_NextOwnerMask, mMaskNextOwner );
    msg->addBOOLFast(_PREHASH_GroupOwned, mIsGroupOwned);
}

void LLPermissions::unpackMessage(LLSD perms)
{
    mCreator    =   perms["creator-id"];
    mOwner  =   perms["owner-id"];
    mGroup  =   perms["group-id"];

    mMaskBase   =   (U32)perms["base-mask"].asInteger();
    mMaskOwner  =   (U32)perms["owner-mask"].asInteger();
    mMaskGroup  =   (U32)perms["group-mask"].asInteger();
    mMaskEveryone   =   (U32)perms["everyone-mask"].asInteger();
    mMaskNextOwner  =   (U32)perms["next-owner-mask"].asInteger();
    mIsGroupOwned   =   perms["group-owned"].asBoolean();
}

void LLPermissions::unpackMessage(LLMessageSystem* msg, const char* block, S32 block_num)
{
    msg->getUUIDFast(block, _PREHASH_CreatorID, mCreator, block_num);
    msg->getUUIDFast(block, _PREHASH_OwnerID,   mOwner, block_num);
    msg->getUUIDFast(block, _PREHASH_GroupID,   mGroup, block_num);

    msg->getU32Fast(block, _PREHASH_BaseMask, mMaskBase, block_num );
    msg->getU32Fast(block, _PREHASH_OwnerMask, mMaskOwner, block_num );
    msg->getU32Fast(block, _PREHASH_GroupMask, mMaskGroup, block_num );
    msg->getU32Fast(block, _PREHASH_EveryoneMask, mMaskEveryone, block_num );
    msg->getU32Fast(block, _PREHASH_NextOwnerMask, mMaskNextOwner, block_num );
    bool tmp;
    msg->getBOOLFast(block, _PREHASH_GroupOwned, tmp, block_num);
    mIsGroupOwned = tmp;
}


// The mask keywords of the legacy text: a mask line that does not parse
// (no value, or a value that is not hexadecimal) reads as PERM_NONE.
static bool is_legacy_mask_keyword(const char* keyword)
{
    return !strcmp("creator_mask", keyword) || !strcmp("base_mask", keyword)
        || !strcmp("owner_mask", keyword) || !strcmp("group_mask", keyword)
        || !strcmp("everyone_mask", keyword) || !strcmp("next_owner_mask", keyword);
}

bool LLPermissions::importLegacyStream(std::istream& input_stream)
{
    init(LLUUID::null, LLUUID::null, LLUUID::null, LLUUID::null);
    const S32 BUFSIZE = 16384;

    // *NOTE: Changing the buffer size will require changing the scanf
    // calls below.
    char buffer[BUFSIZE];   /* Flawfinder: ignore */
    char keyword[256];  /* Flawfinder: ignore */
    char valuestr[256]; /* Flawfinder: ignore */
    char uuid_str[256]; /* Flawfinder: ignore */
    U32 mask = 0;

    keyword[0]  = '\0';
    valuestr[0] = '\0';
    uuid_str[0] = '\0';

    while (input_stream.good())
    {
        input_stream.getline(buffer, BUFSIZE);
        // Each line is parsed on its own; a line that does not parse is absent.
        keyword[0] = '\0';
        valuestr[0] = '\0';
        S32 fields = sscanf( /* Flawfinder: ignore */
            buffer,
            " %255s %255s",
            keyword, valuestr);
        if (fields < 1)
        {
            // a blank line counts as absent
            continue;
        }
        if (!strcmp("{", keyword))
        {
            continue;
        }
        if (!strcmp("}",keyword))
        {
            break;
        }
        else if (fields < 2 && !is_legacy_mask_keyword(keyword))
        {
            // a keyword without a value counts as absent
            continue;
        }
        else if (!strcmp("creator_mask", keyword))
        {
            // legacy support for "creator" masks
            if (1 == sscanf(valuestr, "%x", &mask))
            {
                mMaskBase = mask;
                fixFairUse();
            }
            else
            {
                // a mask that does not parse reads as no permissions, never wider
                mMaskBase = PERM_NONE;
            }
        }
        else if (!strcmp("base_mask", keyword))
        {
            if (1 == sscanf(valuestr, "%x", &mask))
            {
                mMaskBase = mask;
                //fixFairUse();
            }
            else
            {
                // a mask that does not parse reads as no permissions, never wider
                mMaskBase = PERM_NONE;
            }
        }
        else if (!strcmp("owner_mask", keyword))
        {
            if (1 == sscanf(valuestr, "%x", &mask))
            {
                mMaskOwner = mask;
            }
            else
            {
                // a mask that does not parse reads as no permissions, never wider
                mMaskOwner = PERM_NONE;
            }
        }
        else if (!strcmp("group_mask", keyword))
        {
            if (1 == sscanf(valuestr, "%x", &mask))
            {
                mMaskGroup = mask;
            }
            else
            {
                // a mask that does not parse reads as no permissions, never wider
                mMaskGroup = PERM_NONE;
            }
        }
        else if (!strcmp("everyone_mask", keyword))
        {
            if (1 == sscanf(valuestr, "%x", &mask))
            {
                mMaskEveryone = mask;
            }
            else
            {
                // a mask that does not parse reads as no permissions, never wider
                mMaskEveryone = PERM_NONE;
            }
        }
        else if (!strcmp("next_owner_mask", keyword))
        {
            if (1 == sscanf(valuestr, "%x", &mask))
            {
                mMaskNextOwner = mask;
            }
            else
            {
                // a mask that does not parse reads as no permissions, never wider
                mMaskNextOwner = PERM_NONE;
            }
        }
        else if (!strcmp("creator_id", keyword))
        {
            sscanf(valuestr, "%255s", uuid_str);    /* Flawfinder: ignore */
            mCreator.set(uuid_str);
        }
        else if (!strcmp("owner_id", keyword))
        {
            sscanf(valuestr, "%255s", uuid_str);    /* Flawfinder: ignore */
            mOwner.set(uuid_str);
        }
        else if (!strcmp("last_owner_id", keyword))
        {
            sscanf(valuestr, "%255s", uuid_str);    /* Flawfinder: ignore */
            mLastOwner.set(uuid_str);
        }
        else if (!strcmp("group_id", keyword))
        {
            sscanf(valuestr, "%255s", uuid_str);    /* Flawfinder: ignore */
            mGroup.set(uuid_str);
        }
        else if (!strcmp("group_owned", keyword))
        {
            if (1 == sscanf(valuestr, "%d", &mask))
            {
                if(mask) mIsGroupOwned = true;
                else mIsGroupOwned = false;
            }
        }
        else
        {
            LL_INFOS() << "unknown keyword " << keyword << " in permissions import" << LL_ENDL;
        }
    }
    fix();
    return true;
}


bool LLPermissions::exportLegacyStream(std::ostream& output_stream) const
{
    std::string uuid_str;

    output_stream <<  "\tpermissions 0\n";
    output_stream <<  "\t{\n";

    std::string buffer;
    buffer = llformat( "\t\tbase_mask\t%08x\n",     mMaskBase);
    output_stream << buffer;
    buffer = llformat( "\t\towner_mask\t%08x\n",        mMaskOwner);
    output_stream << buffer;
    buffer = llformat( "\t\tgroup_mask\t%08x\n",        mMaskGroup);
    output_stream << buffer;
    buffer = llformat( "\t\teveryone_mask\t%08x\n", mMaskEveryone);
    output_stream << buffer;
    buffer = llformat( "\t\tnext_owner_mask\t%08x\n",   mMaskNextOwner);
    output_stream << buffer;

    mCreator.toString(uuid_str);
    output_stream <<  "\t\tcreator_id\t" << uuid_str << "\n";

    mOwner.toString(uuid_str);
    output_stream <<  "\t\towner_id\t" << uuid_str << "\n";

    mLastOwner.toString(uuid_str);
    output_stream <<  "\t\tlast_owner_id\t" << uuid_str << "\n";

    mGroup.toString(uuid_str);
    output_stream <<  "\t\tgroup_id\t" << uuid_str << "\n";

    if(mIsGroupOwned)
    {
        output_stream <<  "\t\tgroup_owned\t1\n";
    }
    output_stream << "\t}\n";
    return true;
}

static const std::string PERM_CREATOR_ID_LABEL("creator_id");
static const std::string PERM_OWNER_ID_LABEL("owner_id");
static const std::string PERM_LAST_OWNER_ID_LABEL("last_owner_id");
static const std::string PERM_GROUP_ID_LABEL("group_id");
static const std::string PERM_IS_OWNER_GROUP_LABEL("is_owner_group");
static const std::string PERM_BASE_MASK_LABEL("base_mask");
static const std::string PERM_OWNER_MASK_LABEL("owner_mask");
static const std::string PERM_GROUP_MASK_LABEL("group_mask");
static const std::string PERM_EVERYONE_MASK_LABEL("everyone_mask");
static const std::string PERM_NEXT_OWNER_MASK_LABEL("next_owner_mask");

void LLPermissions::importLLSD(const LLSD& sd_perm)
{
    LLSD::map_const_iterator i, end;
    end = sd_perm.endMap();
    for (i = sd_perm.beginMap(); i != end; ++i)
    {
        const std::string& label = i->first;
        if (label == PERM_CREATOR_ID_LABEL)
        {
            mCreator = i->second.asUUID();
            continue;
        }
        if (label == PERM_OWNER_ID_LABEL)
        {
            mOwner = i->second.asUUID();
            continue;
        }
        if (label == PERM_LAST_OWNER_ID_LABEL)
        {
            mLastOwner = i->second.asUUID();
            continue;
        }
        if (label == PERM_GROUP_ID_LABEL)
        {
            mGroup = i->second.asUUID();
            continue;
        }
        if (label == PERM_BASE_MASK_LABEL)
        {
            PermissionMask mask = i->second.asInteger();
            mMaskBase = mask;
            continue;
        }
        if (label == PERM_OWNER_MASK_LABEL)
        {
            PermissionMask mask = i->second.asInteger();
            mMaskOwner = mask;
            continue;
        }
        if (label == PERM_EVERYONE_MASK_LABEL)
        {
            PermissionMask mask = i->second.asInteger();
            mMaskEveryone = mask;
            continue;
        }
        if (label == PERM_GROUP_MASK_LABEL)
        {
            PermissionMask mask = i->second.asInteger();
            mMaskGroup = mask;
            continue;
        }
        if (label == PERM_NEXT_OWNER_MASK_LABEL)
        {
            PermissionMask mask = i->second.asInteger();
            mMaskNextOwner = mask;
            continue;
        }
    }

    fixOwnership();
    fix();
}

bool LLPermissions::operator==(const LLPermissions &rhs) const
{
    return
        (mCreator == rhs.mCreator) &&
        (mOwner == rhs.mOwner) &&
        (mLastOwner == rhs.mLastOwner ) &&
        (mGroup == rhs.mGroup ) &&
        (mMaskBase == rhs.mMaskBase ) &&
        (mMaskOwner == rhs.mMaskOwner ) &&
        (mMaskGroup == rhs.mMaskGroup ) &&
        (mMaskEveryone == rhs.mMaskEveryone ) &&
        (mMaskNextOwner == rhs.mMaskNextOwner ) &&
        (mIsGroupOwned == rhs.mIsGroupOwned);
}


bool LLPermissions::operator!=(const LLPermissions &rhs) const
{
    return
        (mCreator != rhs.mCreator) ||
        (mOwner != rhs.mOwner) ||
        (mLastOwner != rhs.mLastOwner ) ||
        (mGroup != rhs.mGroup ) ||
        (mMaskBase != rhs.mMaskBase ) ||
        (mMaskOwner != rhs.mMaskOwner ) ||
        (mMaskGroup != rhs.mMaskGroup ) ||
        (mMaskEveryone != rhs.mMaskEveryone ) ||
        (mMaskNextOwner != rhs.mMaskNextOwner) ||
        (mIsGroupOwned != rhs.mIsGroupOwned);
}

std::ostream& operator<<(std::ostream &s, const LLPermissions &perm)
{
    s << "{Creator="        << perm.getCreator();
    s << ", Owner="         << perm.getOwner();
    s << ", Group="         << perm.getGroup();
    s << std::hex << ", BaseMask=0x" << perm.getMaskBase();
    s << ", OwnerMask=0x" << perm.getMaskOwner();
    s << ", EveryoneMask=0x" << perm.getMaskEveryone();
    s << ", GroupMask=0x" << perm.getMaskGroup();
    s << ", NextOwnerMask=0x" << perm.getMaskNextOwner() << std::dec;
    s << "}";
    return s;
}

///----------------------------------------------------------------------------
/// Class LLAggregatePermissions
///----------------------------------------------------------------------------

const LLAggregatePermissions LLAggregatePermissions::empty;


LLAggregatePermissions::LLAggregatePermissions()
{
    for(S32 i = 0; i < PI_COUNT; ++i)
    {
        mBits[i] = AP_EMPTY;
    }
}

LLAggregatePermissions::EValue LLAggregatePermissions::getValue(PermissionBit bit) const
{
    EPermIndex idx = perm2PermIndex(bit);
    EValue rv = AP_EMPTY;
    if(idx != PI_END)
    {
        rv = (LLAggregatePermissions::EValue)(mBits[idx]);
    }
    return rv;
}

// returns the bits compressed into a single byte: 00TTMMCC
// where TT = transfer, MM = modify, and CC = copy
// LSB is to the right
U8 LLAggregatePermissions::getU8() const
{
    U8 byte = mBits[PI_TRANSFER];
    byte <<= 2;
    byte |= mBits[PI_MODIFY];
    byte <<= 2;
    byte |= mBits[PI_COPY];
    return byte;
}

bool LLAggregatePermissions::isEmpty() const
{
    for(S32 i = 0; i < PI_END; ++i)
    {
        if(mBits[i] != AP_EMPTY)
        {
            return false;
        }
    }
    return true;
}

void LLAggregatePermissions::aggregate(PermissionMask mask)
{
    bool is_allowed = mask & PERM_COPY;
    aggregateBit(PI_COPY, is_allowed);
    is_allowed = mask & PERM_MODIFY;
    aggregateBit(PI_MODIFY, is_allowed);
    is_allowed = mask & PERM_TRANSFER;
    aggregateBit(PI_TRANSFER, is_allowed);
}

void LLAggregatePermissions::aggregate(const LLAggregatePermissions& ag)
{
    for(S32 idx = PI_COPY; idx != PI_END; ++idx)
    {
        aggregateIndex((EPermIndex)idx, ag.mBits[idx]);
    }
}

void LLAggregatePermissions::aggregateBit(EPermIndex idx, bool allowed)
{
    //if(AP_SOME == mBits[idx]) return; // P4 branch prediction optimization
    switch(mBits[idx])
    {
    case AP_EMPTY:
        mBits[idx] = allowed ? AP_ALL : AP_NONE;
        break;
    case AP_NONE:
        mBits[idx] = allowed ? AP_SOME: AP_NONE;
        break;
    case AP_SOME:
        // no-op
        break;
    case AP_ALL:
        mBits[idx] = allowed ? AP_ALL : AP_SOME;
        break;
    default:
        LL_WARNS() << "Bad aggregateBit " << (S32)idx << " "
                << (allowed ? "true" : "false") << LL_ENDL;
        break;
    }
}

void LLAggregatePermissions::aggregateIndex(EPermIndex idx, U8 bits)
{
    switch(mBits[idx])
    {
    case AP_EMPTY:
        mBits[idx] = bits;
        break;
    case AP_NONE:
        switch(bits)
        {
        case AP_SOME:
        case AP_ALL:
            mBits[idx] = AP_SOME;
            break;
        case AP_EMPTY:
        case AP_NONE:
        default:
            // no-op
            break;
        }
        break;
    case AP_SOME:
        // no-op
        break;
    case AP_ALL:
        switch(bits)
        {
        case AP_NONE:
        case AP_SOME:
            mBits[idx] = AP_SOME;
            break;
        case AP_EMPTY:
        case AP_ALL:
        default:
            // no-op
            break;
        }
        break;
    default:
        LL_WARNS() << "Bad aggregate index " << (S32)idx << " "
                <<  (S32)bits << LL_ENDL;
        break;
    }
}

// static
LLAggregatePermissions::EPermIndex LLAggregatePermissions::perm2PermIndex(PermissionBit bit)
{
    EPermIndex idx = PI_END; // past any good value.
    switch(bit)
    {
    case PERM_COPY:
        idx = PI_COPY;
        break;
    case PERM_MODIFY:
        idx = PI_MODIFY;
        break;
    case PERM_TRANSFER:
        idx = PI_TRANSFER;
        break;
    default:
        break;
    }
    return idx;
}


void LLAggregatePermissions::packMessage(LLMessageSystem* msg, const char* field) const
{
    msg->addU8Fast(field, getU8());
}

void LLAggregatePermissions::unpackMessage(LLMessageSystem* msg, const char* block, const char* field, S32 block_num)
{
    const U8 TWO_BITS = 0x3; // binary 00000011
    U8 bits = 0;
    msg->getU8Fast(block, field, bits, block_num);
    mBits[PI_COPY] = bits & TWO_BITS;
    bits >>= 2;
    mBits[PI_MODIFY] = bits & TWO_BITS;
    bits >>= 2;
    mBits[PI_TRANSFER] = bits & TWO_BITS;
}

const std::string AGGREGATE_VALUES[4] =
    {
        std::string( "Empty" ),
        std::string( "None" ),
        std::string( "Some" ),
        std::string( "All" )
    };

std::ostream& operator<<(std::ostream &s, const LLAggregatePermissions &perm)
{
    s << "{PI_COPY=" << AGGREGATE_VALUES[perm.mBits[LLAggregatePermissions::PI_COPY]];
    s << ", PI_MODIFY=" << AGGREGATE_VALUES[perm.mBits[LLAggregatePermissions::PI_MODIFY]];
    s << ", PI_TRANSFER=" << AGGREGATE_VALUES[perm.mBits[LLAggregatePermissions::PI_TRANSFER]];
    s << "}";
    return s;
}

// This converts a permissions mask into a string for debugging use.
void mask_to_string(U32 mask, char* str, bool isOpenSim /*=false*/) // <FS:Beq/> remove misleading X for export when not in OpenSim
{
    if (mask & PERM_MOVE)
    {
        *str = 'V';
    }
    else
    {
        *str = ' ';
    }
    str++;

    if (mask & PERM_MODIFY)
    {
        *str = 'M';
    }
    else
    {
        *str = ' ';
    }
    str++;

    if (mask & PERM_COPY)
    {
        *str = 'C';
    }
    else
    {
        *str = ' ';
    }
    str++;

    if (mask & PERM_TRANSFER)
    {
        *str = 'T';
    }
    else
    {
        *str = ' ';
    }
    str++;

// <FS:CR> OpenSim export permission
#ifdef OPENSIM
    // The export permission is only available in OpenSim.
    if (isOpenSim)
    {
        if (mask & PERM_EXPORT)
        {
            *str = 'X';
        }
        else
        {
            *str = ' ';
        }
        str++;
    }
#endif
// </FS:CR>
    *str = '\0';
}

std::string mask_to_string(U32 mask, bool isOpenSim /*=false*/) // <FS:Beq/> remove misleading X for export when not in OpenSim
{
    char str[16];
    mask_to_string(mask, str, isOpenSim); // <FS:Beq/> remove misleading X for export when not in OpenSim
    return std::string(str);
}

///----------------------------------------------------------------------------
/// exported functions
///----------------------------------------------------------------------------

LLSD ll_create_sd_from_permissions(const LLPermissions& perm)
{
    LLSD rv;
    ll_fill_sd_from_permissions(rv, perm);
    return rv;
}
void ll_fill_sd_from_permissions(LLSD& rv, const LLPermissions& perm)
{
    rv[PERM_CREATOR_ID_LABEL] = perm.getCreator();
    rv[PERM_OWNER_ID_LABEL] = perm.getOwner();
    rv[PERM_LAST_OWNER_ID_LABEL] = perm.getLastOwner();
    rv[PERM_GROUP_ID_LABEL] = perm.getGroup();
    rv[PERM_IS_OWNER_GROUP_LABEL] = perm.isGroupOwned();
    rv[PERM_BASE_MASK_LABEL] = (LLSD::Integer)perm.getMaskBase();
    rv[PERM_OWNER_MASK_LABEL] = (LLSD::Integer)perm.getMaskOwner();
    rv[PERM_GROUP_MASK_LABEL] = (LLSD::Integer)perm.getMaskGroup();
    rv[PERM_EVERYONE_MASK_LABEL] = (LLSD::Integer)perm.getMaskEveryone();
    rv[PERM_NEXT_OWNER_MASK_LABEL] = (LLSD::Integer)perm.getMaskNextOwner();
}

LLPermissions ll_permissions_from_sd(const LLSD& sd_perm)
{
    LLPermissions rv;
    rv.importLLSD(sd_perm);
    return rv;
}
// ---- VKS-ORACLE-COPY END

// ---- VKS-ORACLE-COPY BEGIN
// source: indra/llinventory/llsaleinfo.cpp
// lines: 26-332
// blob: 9a3b2f59648c5c7a3f1dbbf1299d48604a887ebb
// copied: the whole file after its licence block
// substitutions: #include lines and forward declarations of LLMessageSystem and LLUUID removed
// conditionals: 
// ----




// use this to avoid temporary object creation
const LLSaleInfo LLSaleInfo::DEFAULT;

///----------------------------------------------------------------------------
/// Local function declarations, constants, enums, and typedefs
///----------------------------------------------------------------------------

const char* FOR_SALE_NAMES[] =
{
    "not",
    "orig",
    "copy",
    "cntn"
};

///----------------------------------------------------------------------------
/// Class llsaleinfo
///----------------------------------------------------------------------------

// Default constructor
LLSaleInfo::LLSaleInfo() :
    mSaleType(LLSaleInfo::FS_NOT),
    mSalePrice(DEFAULT_PRICE)
{
}

LLSaleInfo::LLSaleInfo(EForSale sale_type, S32 sale_price) :
    mSaleType(sale_type),
    mSalePrice(sale_price)
{
    mSalePrice = llclamp(mSalePrice, 0, S32_MAX);
}

bool LLSaleInfo::isForSale() const
{
    return (FS_NOT != mSaleType);
}

U32 LLSaleInfo::getCRC32() const
{
    U32 rv = (U32)mSalePrice;
    rv += (mSaleType * 0x07073096);
    return rv;
}

bool LLSaleInfo::exportLegacyStream(std::ostream& output_stream) const
{
    const char* type = lookup(mSaleType);
    if (!type)
    {
        LL_WARNS_ONCE() << "Unknown sale type: " << mSaleType << LL_ENDL;
        type = lookup(LLSaleInfo::FS_NOT);
    }
    output_stream << "\tsale_info\t0\n\t{\n";
    output_stream << "\t\tsale_type\t" << type << "\n";
    output_stream << "\t\tsale_price\t" << mSalePrice << "\n";
    output_stream <<"\t}\n";
    return true;
}

LLSD LLSaleInfo::asLLSD() const
{
    LLSD sd;
    asLLSD(sd);
    return sd;
}

void LLSaleInfo::asLLSD(LLSD& sd) const
{
    const char* type = lookup(mSaleType);
    if (!type)
    {
        LL_WARNS_ONCE() << "Unknown sale type: " << mSaleType << LL_ENDL;
        type = lookup(LLSaleInfo::FS_NOT);
    }
    sd["sale_type"] = std::string(type);
    sd["sale_price"] = mSalePrice;
}

bool LLSaleInfo::fromLLSD(const LLSD& sd, bool& has_perm_mask, U32& perm_mask)
{
    const char *w;

    if (sd["sale_type"].isString())
    {
        mSaleType = lookup(sd["sale_type"].asString().c_str());
    }
    else if(sd["sale_type"].isInteger())
    {
        S8 type = (U8)sd["sale_type"].asInteger();
        mSaleType = static_cast<LLSaleInfo::EForSale>(type);
    }

    mSalePrice = llclamp(sd["sale_price"].asInteger(), 0, S32_MAX);
    w = "perm_mask";
    if (sd.has(w))
    {
        has_perm_mask = true;
        perm_mask = ll_U32_from_sd(sd[w]);
    }
    return true;
}

bool LLSaleInfo::importLegacyStream(std::istream& input_stream, bool& has_perm_mask, U32& perm_mask)
{
    has_perm_mask = false;

    // *NOTE: Changing the buffer size will require changing the scanf
    // calls below.
    char buffer[MAX_STRING];    /* Flawfinder: ignore */
    char keyword[MAX_STRING];   /* Flawfinder: ignore */
    char valuestr[MAX_STRING];  /* Flawfinder: ignore */
    bool success = true;

    keyword[0] = '\0';
    valuestr[0] = '\0';
    while(success && input_stream.good())
    {
        input_stream.getline(buffer, MAX_STRING);
        // Each line is parsed on its own; a line that does not parse is absent.
        keyword[0] = '\0';
        valuestr[0] = '\0';
        S32 fields = sscanf( /* Flawfinder: ignore */
            buffer,
            " %254s %254s",
            keyword, valuestr);
        if(!keyword[0])
        {
            continue;
        }
        if(0 == strcmp("{",keyword))
        {
            continue;
        }
        if(0 == strcmp("}", keyword))
        {
            break;
        }
        else if (fields < 2)
        {
            // a keyword without a value counts as absent
            continue;
        }
        else if(0 == strcmp("sale_type", keyword))
        {
            mSaleType = lookup(valuestr);
        }
        else if(0 == strcmp("sale_price", keyword))
        {
            if (1 == sscanf(valuestr, "%d", &mSalePrice))
            {
                mSalePrice = llclamp(mSalePrice, 0, S32_MAX);
            }
        }
        else if (!strcmp("perm_mask", keyword))
        {
            //LL_INFOS() << "found deprecated keyword perm_mask" << LL_ENDL;
            if (1 == sscanf(valuestr, "%x", &perm_mask))
            {
                has_perm_mask = true;
            }
        }
        else
        {
            LL_WARNS() << "unknown keyword '" << keyword
                    << "' in sale info import" << LL_ENDL;
        }
    }
    return success;
}

void LLSaleInfo::setSalePrice(S32 price)
{
    mSalePrice = price;
    mSalePrice = llclamp(mSalePrice, 0, S32_MAX);
}

LLSD LLSaleInfo::packMessage() const
{
    LLSD result;

    U8 sale_type = static_cast<U8>(mSaleType);
    result["sale-type"]     = (U8)sale_type;
    result["sale-price"]    = (S32)mSalePrice;
    //result[_PREHASH_NextOwnerMask] = mNextOwnerPermMask;
    return result;
}

void LLSaleInfo::packMessage(LLMessageSystem* msg) const
{
    U8 sale_type = static_cast<U8>(mSaleType);
    msg->addU8Fast(_PREHASH_SaleType, sale_type);
    msg->addS32Fast(_PREHASH_SalePrice, mSalePrice);
    //msg->addU32Fast(_PREHASH_NextOwnerMask, mNextOwnerPermMask);
}

void LLSaleInfo::unpackMessage(LLSD sales)
{
    U8 sale_type = (U8)sales["sale-type"].asInteger();
    mSaleType = static_cast<EForSale>(sale_type);

    mSalePrice = (S32)sales["sale-price"].asInteger();
    mSalePrice = llclamp(mSalePrice, 0, S32_MAX);
    //msg->getU32Fast(block, _PREHASH_NextOwnerMask, mNextOwnerPermMask);
}

void LLSaleInfo::unpackMessage(LLMessageSystem* msg, const char* block)
{
    U8 sale_type;
    msg->getU8Fast(block, _PREHASH_SaleType, sale_type);
    mSaleType = static_cast<EForSale>(sale_type);
    msg->getS32Fast(block, _PREHASH_SalePrice, mSalePrice);
    mSalePrice = llclamp(mSalePrice, 0, S32_MAX);
    //msg->getU32Fast(block, _PREHASH_NextOwnerMask, mNextOwnerPermMask);
}

void LLSaleInfo::unpackMultiMessage(LLMessageSystem* msg, const char* block,
                                    S32 block_num)
{
    U8 sale_type;
    msg->getU8Fast(block, _PREHASH_SaleType, sale_type, block_num);
    mSaleType = static_cast<EForSale>(sale_type);
    msg->getS32Fast(block, _PREHASH_SalePrice, mSalePrice, block_num);
    mSalePrice = llclamp(mSalePrice, 0, S32_MAX);
    //msg->getU32Fast(block, _PREHASH_NextOwnerMask, mNextOwnerPermMask, block_num);
}

LLSaleInfo::EForSale LLSaleInfo::lookup(const char* name)
{
    for(S32 i = 0; i < FS_COUNT; i++)
    {
        if(0 == strcmp(name, FOR_SALE_NAMES[i]))
        {
            // match
            return (EForSale)i;
        }
    }
    return FS_NOT;
}

const char* LLSaleInfo::lookup(EForSale type)
{
    if((type >= 0) && (type < FS_COUNT))
    {
        return FOR_SALE_NAMES[S32(type)];
    }
    else
    {
        return NULL;
    }
}

// Allow accumulation of sale info. The price of each is added,
// conflict in sale type results in FS_NOT, and the permissions are
// tightened.
void LLSaleInfo::accumulate(const LLSaleInfo& sale_info)
{
    if(mSaleType != sale_info.mSaleType)
    {
        mSaleType = FS_NOT;
    }
    mSalePrice += sale_info.mSalePrice;
    //mNextOwnerPermMask &= sale_info.mNextOwnerPermMask;
}

bool LLSaleInfo::operator==(const LLSaleInfo &rhs) const
{
    return (
        (mSaleType == rhs.mSaleType) &&
        (mSalePrice == rhs.mSalePrice)
        );
}

bool LLSaleInfo::operator!=(const LLSaleInfo &rhs) const
{
    return (
        (mSaleType != rhs.mSaleType) ||
        (mSalePrice != rhs.mSalePrice)
        );
}


///----------------------------------------------------------------------------
/// Local function definitions
///----------------------------------------------------------------------------

///----------------------------------------------------------------------------
/// exported functions
///----------------------------------------------------------------------------
static const std::string ST_TYPE_LABEL("sale_type");
static const std::string ST_PRICE_LABEL("sale_price");

LLSD ll_create_sd_from_sale_info(const LLSaleInfo& sale)
{
    LLSD rv;
    const char* type = LLSaleInfo::lookup(sale.getSaleType());
    if(!type) type = LLSaleInfo::lookup(LLSaleInfo::FS_NOT);
    rv[ST_TYPE_LABEL] = type;
    rv[ST_PRICE_LABEL] = sale.getSalePrice();
    return rv;
}

LLSaleInfo ll_sale_info_from_sd(const LLSD& sd)
{
    LLSaleInfo rv;
    rv.setSaleType(LLSaleInfo::lookup(sd[ST_TYPE_LABEL].asString().c_str()));
    rv.setSalePrice(llclamp((S32)sd[ST_PRICE_LABEL], 0, S32_MAX));
    return rv;
}
// ---- VKS-ORACLE-COPY END

// ---- VKS-ORACLE-COPY BEGIN
// source: indra/llinventory/llinventory.cpp
// lines: 26-1734
// blob: 434ad7c81ee8699b675b86b3bfa2def35737ea69
// copied: the whole file after its licence block
// substitutions: #include lines and forward declarations of LLMessageSystem and LLUUID removed
// conditionals: 
// ----




///----------------------------------------------------------------------------
/// Exported functions
///----------------------------------------------------------------------------
// FIXME D567 - what's the point of these, especially if we don't even use them consistently?
static const std::string INV_ITEM_ID_LABEL("item_id");
static const std::string INV_FOLDER_ID_LABEL("cat_id");
static const std::string INV_PARENT_ID_LABEL("parent_id");
static const std::string INV_THUMBNAIL_LABEL("thumbnail");
static const std::string INV_FAVORITE_LABEL("favorite");
static const std::string INV_THUMBNAIL_ID_LABEL("thumbnail_id");
static const std::string INV_ASSET_TYPE_LABEL("type");
static const std::string INV_PREFERRED_TYPE_LABEL("preferred_type");
static const std::string INV_INVENTORY_TYPE_LABEL("inv_type");
static const std::string INV_NAME_LABEL("name");
static const std::string INV_DESC_LABEL("desc");
static const std::string INV_PERMISSIONS_LABEL("permissions");
static const std::string INV_SHADOW_ID_LABEL("shadow_id");
static const std::string INV_ASSET_ID_LABEL("asset_id");
static const std::string INV_LINKED_ID_LABEL("linked_id");
static const std::string INV_SALE_INFO_LABEL("sale_info");
static const std::string INV_FLAGS_LABEL("flags");
static const std::string INV_CREATION_DATE_LABEL("created_at");
static const std::string INV_TOGGLED_LABEL("toggled");

// key used by agent-inventory-service
static const std::string INV_ASSET_TYPE_LABEL_WS("type_default");
static const std::string INV_FOLDER_ID_LABEL_WS("category_id");

///----------------------------------------------------------------------------
/// Local function declarations, constants, enums, and typedefs
///----------------------------------------------------------------------------

const LLUUID MAGIC_ID("3c115e51-04f4-523c-9fa6-98aff1034730");

///----------------------------------------------------------------------------
/// Class LLInventoryObject
///----------------------------------------------------------------------------

LLInventoryObject::LLInventoryObject(const LLUUID& uuid,
                                     const LLUUID& parent_uuid,
                                     LLAssetType::EType type,
                                     const std::string& name)
:   mUUID(uuid),
    mParentUUID(parent_uuid),
    mType(type),
    mName(name),
    mCreationDate(0),
    mFavorite(false)
{
    correctInventoryName(mName);
}

LLInventoryObject::LLInventoryObject()
:   mType(LLAssetType::AT_NONE),
    mCreationDate(0),
    mFavorite(false)
{
}

LLInventoryObject::~LLInventoryObject()
{
}

void LLInventoryObject::copyObject(const LLInventoryObject* other)
{
    mUUID = other->mUUID;
    mParentUUID = other->mParentUUID;
    mType = other->mType;
    mName = other->mName;
    mThumbnailUUID = other->mThumbnailUUID;
    mFavorite = other->mFavorite;
    mRuntime = other->mRuntime;
}

const LLUUID& LLInventoryObject::getUUID() const
{
    return mUUID;
}

const LLUUID& LLInventoryObject::getParentUUID() const
{
    return mParentUUID;
}

const LLUUID& LLInventoryObject::getThumbnailUUID() const
{
    return mThumbnailUUID;
}

bool LLInventoryObject::getIsFavorite() const
{
    return mFavorite;
}

const std::string& LLInventoryObject::getRuntime() const
{
    return mRuntime;
}

void LLInventoryObject::setRuntime(std::string_view runtime)
{
    mRuntime = runtime;
}

const std::string& LLInventoryObject::getName() const
{
    return mName;
}

// To bypass linked items, since llviewerinventory's getType
// will return the linked-to item's type instead of this object's type.
LLAssetType::EType LLInventoryObject::getActualType() const
{
    return mType;
}

bool LLInventoryObject::getIsLinkType() const
{
    return LLAssetType::lookupIsLinkType(mType);
}

// See LLInventoryItem override.
// virtual
const LLUUID& LLInventoryObject::getLinkedUUID() const
{
    return mUUID;
}

LLAssetType::EType LLInventoryObject::getType() const
{
    return mType;
}

void LLInventoryObject::setUUID(const LLUUID& new_uuid)
{
    mUUID = new_uuid;
}

void LLInventoryObject::rename(const std::string& n)
{
    std::string new_name(n);
    correctInventoryName(new_name);
    if( !new_name.empty() && new_name != mName )
    {
        mName = new_name;
    }
}

void LLInventoryObject::setParent(const LLUUID& new_parent)
{
    mParentUUID = new_parent;
}

void LLInventoryObject::setThumbnailUUID(const LLUUID& thumbnail_uuid)
{
    mThumbnailUUID = thumbnail_uuid;
}

void LLInventoryObject::setFavorite(bool favorite)
{
    mFavorite = favorite;
}

void LLInventoryObject::setType(LLAssetType::EType type)
{
    mType = type;
    if (type != LLAssetType::AT_LSL_TEXT) mRuntime.clear();
}


// virtual
bool LLInventoryObject::importLegacyStream(std::istream& input_stream)
{
    // *NOTE: Changing the buffer size will require changing the scanf
    // calls below.
    char buffer[MAX_STRING];    /* Flawfinder: ignore */
    char keyword[MAX_STRING];   /* Flawfinder: ignore */
    char valuestr[MAX_STRING];  /* Flawfinder: ignore */

    keyword[0] = '\0';
    valuestr[0] = '\0';
    while(input_stream.good())
    {
        input_stream.getline(buffer, MAX_STRING);
        // Each line is parsed on its own; a line that does not parse is absent.
        keyword[0] = '\0';
        valuestr[0] = '\0';
        S32 fields = sscanf(buffer, " %254s %254s", keyword, valuestr);  /* Flawfinder: ignore */
        if (fields < 1)
        {
            // a blank line counts as absent
            continue;
        }
        if(0 == strcmp("{",keyword))
        {
            continue;
        }
        if(0 == strcmp("}", keyword))
        {
            break;
        }
        else if (fields < 2 && 0 != strcmp("metadata", keyword))
        {
            // a keyword without a value counts as absent
            // (metadata keeps its own handling)
            continue;
        }
        else if(0 == strcmp("obj_id", keyword))
        {
            mUUID.set(valuestr);
        }
        else if(0 == strcmp("parent_id", keyword))
        {
            mParentUUID.set(valuestr);
        }
        else if(0 == strcmp("type", keyword))
        {
            mType = LLAssetType::lookup(valuestr);
        }
        else if (0 == strcmp("metadata", keyword))
        {
            LLSD metadata;
            if (strncmp("<llsd>", valuestr, 6) == 0)
            {
                std::istringstream stream(valuestr);
                LLSDSerialize::fromXML(metadata, stream);
            }
            else
            {
                // next line likely contains metadata, but at the moment is not supported
                // can do something like:
                // LLSDSerialize::fromNotation(metadata, input_stream, -1);
            }

            setRuntime(metadata["script"]["runtime"].asString());
            if (metadata.has("thumbnail"))
            {
                const LLSD& thumbnail = metadata["thumbnail"];
                if (thumbnail.has("asset_id"))
                {
                    setThumbnailUUID(thumbnail["asset_id"].asUUID());
                }
                else
                {
                    setThumbnailUUID(LLUUID::null);
                }
            }
            else
            {
                setThumbnailUUID(LLUUID::null);
            }

            if (metadata.has("favorite"))
            {
                const LLSD& favorite = metadata["favorite"];
                if (favorite.has("toggled"))
                {
                    setFavorite(favorite["toggled"].asBoolean());
                }
                else
                {
                    setFavorite(false);
                }
            }
            else
            {
                setFavorite(false);
            }
        }
        else if(0 == strcmp("name", keyword))
        {
            //strcpy(valuestr, buffer + strlen(keyword) + 3);
            // *NOTE: Not ANSI C, but widely supported.
            sscanf( /* Flawfinder: ignore */
                buffer,
                " %254s %254[^|]",
                keyword, valuestr);
            mName.assign(valuestr);
            correctInventoryName(mName);
        }
        else
        {
            LL_WARNS() << "unknown keyword '" << keyword
                    << "' in LLInventoryObject::importLegacyStream() for object " << mUUID << LL_ENDL;
        }
    }
    return true;
}

bool LLInventoryObject::exportLegacyStream(std::ostream& output_stream, bool) const
{
    std::string uuid_str;
    output_stream <<  "\tinv_object\t0\n\t{\n";
    mUUID.toString(uuid_str);
    output_stream << "\t\tobj_id\t" << uuid_str << "\n";
    mParentUUID.toString(uuid_str);
    output_stream << "\t\tparent_id\t" << uuid_str << "\n";
    output_stream << "\t\ttype\t" << LLAssetType::lookup(mType) << "\n";
    output_stream << "\t\tname\t" << mName.c_str() << "|\n";
    output_stream << "\t}\n";
    return true;
}

void LLInventoryObject::updateParentOnServer(bool) const
{
    // don't do nothin'
    LL_WARNS() << "LLInventoryObject::updateParentOnServer() called.  Doesn't do anything." << LL_ENDL;
}

void LLInventoryObject::updateServer(bool) const
{
    // don't do nothin'
    LL_WARNS() << "LLInventoryObject::updateServer() called.  Doesn't do anything." << LL_ENDL;
}

// static
void LLInventoryObject::correctInventoryName(std::string& name)
{
    LLStringUtil::replaceNonstandardASCII(name, ' ');
    LLStringUtil::replaceChar(name, '|', ' ');
    LLStringUtil::trim(name);
    LLStringUtil::truncate(name, DB_INV_ITEM_NAME_STR_LEN);
}

time_t LLInventoryObject::getCreationDate() const
{
    return mCreationDate;
}

void LLInventoryObject::setCreationDate(time_t creation_date_utc)
{
    mCreationDate = creation_date_utc;
}


const std::string& LLInventoryItem::getDescription() const
{
    return mDescription;
}

const std::string& LLInventoryItem::getActualDescription() const
{
    return mDescription;
}

///----------------------------------------------------------------------------
/// Class LLInventoryItem
///----------------------------------------------------------------------------

LLInventoryItem::LLInventoryItem(const LLUUID& uuid,
                                 const LLUUID& parent_uuid,
                                 const LLPermissions& permissions,
                                 const LLUUID& asset_uuid,
                                 LLAssetType::EType type,
                                 LLInventoryType::EType inv_type,
                                 const std::string& name,
                                 const std::string& desc,
                                 const LLSaleInfo& sale_info,
                                 U32 flags,
                                 S32 creation_date_utc) :
    LLInventoryObject(uuid, parent_uuid, type, name),
    mPermissions(permissions),
    mAssetUUID(asset_uuid),
    mDescription(desc),
    mSaleInfo(sale_info),
    mInventoryType(inv_type),
    mFlags(flags)
{
    mCreationDate = creation_date_utc;

    LLStringUtil::replaceNonstandardASCII(mDescription, ' ');
    LLStringUtil::replaceChar(mDescription, '|', ' ');

    mPermissions.initMasks(inv_type);
}

LLInventoryItem::LLInventoryItem() :
    LLInventoryObject(),
    mPermissions(),
    mAssetUUID(),
    mDescription(),
    mSaleInfo(),
    mInventoryType(LLInventoryType::IT_NONE),
    mFlags(0)
{
    mCreationDate = 0;
}

LLInventoryItem::LLInventoryItem(const LLInventoryItem* other) :
    LLInventoryObject()
{
    copyItem(other);
}

LLInventoryItem::~LLInventoryItem()
{
}

// virtual
void LLInventoryItem::copyItem(const LLInventoryItem* other)
{
    copyObject(other);
    mPermissions = other->mPermissions;
    mAssetUUID = other->mAssetUUID;
    mThumbnailUUID = other->mThumbnailUUID;
    mDescription = other->mDescription;
    mSaleInfo = other->mSaleInfo;
    mInventoryType = other->mInventoryType;
    mFlags = other->mFlags;
    mCreationDate = other->mCreationDate;
    mRuntime = other->mRuntime;
}

// If this is a linked item, then the UUID of the base object is
// this item's assetID.
// virtual
const LLUUID& LLInventoryItem::getLinkedUUID() const
{
    if (LLAssetType::lookupIsLinkType(getActualType()))
    {
        return mAssetUUID;
    }

    return LLInventoryObject::getLinkedUUID();
}

const LLPermissions& LLInventoryItem::getPermissions() const
{
    return mPermissions;
}

const LLUUID& LLInventoryItem::getCreatorUUID() const
{
    return mPermissions.getCreator();
}

const LLUUID& LLInventoryItem::getAssetUUID() const
{
    return mAssetUUID;
}

void LLInventoryItem::setAssetUUID(const LLUUID& asset_id)
{
    mAssetUUID = asset_id;
}


U32 LLInventoryItem::getCRC32() const
{
    // *FIX: Not a real crc - more of a checksum.
    // *NOTE: We currently do not validate the name or description,
    // but if they change in transit, it's no big deal.
    U32 crc = mUUID.getCRC32();
    //LL_DEBUGS() << "1 crc: " << std::hex << crc << std::dec << LL_ENDL;
    crc += mParentUUID.getCRC32();
    //LL_DEBUGS() << "2 crc: " << std::hex << crc << std::dec << LL_ENDL;
    crc += mPermissions.getCRC32();
    //LL_DEBUGS() << "3 crc: " << std::hex << crc << std::dec << LL_ENDL;
    crc += mAssetUUID.getCRC32();
    //LL_DEBUGS() << "4 crc: " << std::hex << crc << std::dec << LL_ENDL;
    crc += mType;
    //LL_DEBUGS() << "5 crc: " << std::hex << crc << std::dec << LL_ENDL;
    crc += mInventoryType;
    //LL_DEBUGS() << "6 crc: " << std::hex << crc << std::dec << LL_ENDL;
    crc += mFlags;
    //LL_DEBUGS() << "7 crc: " << std::hex << crc << std::dec << LL_ENDL;
    crc += mSaleInfo.getCRC32();
    //LL_DEBUGS() << "8 crc: " << std::hex << crc << std::dec << LL_ENDL;
    crc += (U32)mCreationDate;
    //LL_DEBUGS() << "9 crc: " << std::hex << crc << std::dec << LL_ENDL;
    crc += mThumbnailUUID.getCRC32();
    return crc;
}

// static
void LLInventoryItem::correctInventoryDescription(std::string& desc)
{
    LLStringUtil::replaceNonstandardASCII(desc, ' ');
    LLStringUtil::replaceChar(desc, '|', ' ');
}

void LLInventoryItem::setDescription(const std::string& d)
{
    std::string new_desc(d);
    LLInventoryItem::correctInventoryDescription(new_desc);
    if( new_desc != mDescription )
    {
        mDescription = new_desc;
    }
}

void LLInventoryItem::setPermissions(const LLPermissions& perm)
{
    mPermissions = perm;

    // Override permissions to unrestricted if this is a landmark
    mPermissions.initMasks(mInventoryType);
}

void LLInventoryItem::setInventoryType(LLInventoryType::EType inv_type)
{
    mInventoryType = inv_type;
}

void LLInventoryItem::setFlags(U32 flags)
{
    mFlags = flags;
}

// Currently only used in the Viewer to handle calling cards
// where the creator is actually used to store the target.
void LLInventoryItem::setCreator(const LLUUID& creator)
{
    mPermissions.setCreator(creator);
}

void LLInventoryItem::accumulatePermissionSlamBits(const LLInventoryItem& old_item)
{
    // Remove any pre-existing II_FLAGS_PERM_OVERWRITE_MASK flags
    // because we now detect when they should be set.
    setFlags( old_item.getFlags() | (getFlags() & ~(LLInventoryItemFlags::II_FLAGS_PERM_OVERWRITE_MASK)) );

    // Enforce the PERM_OVERWRITE flags for any masks that are different
    // but only for AT_OBJECT's since that is the only asset type that can
    // exist in-world (instead of only in-inventory or in-object-contents).
    if (LLAssetType::AT_OBJECT == getType())
    {
        LLPermissions old_permissions = old_item.getPermissions();
        U32 flags_to_be_set = 0;
        if(old_permissions.getMaskNextOwner() != getPermissions().getMaskNextOwner())
        {
            flags_to_be_set |= LLInventoryItemFlags::II_FLAGS_OBJECT_SLAM_PERM;
        }
        if(old_permissions.getMaskEveryone() != getPermissions().getMaskEveryone())
        {
            flags_to_be_set |= LLInventoryItemFlags::II_FLAGS_OBJECT_PERM_OVERWRITE_EVERYONE;
        }
        if(old_permissions.getMaskGroup() != getPermissions().getMaskGroup())
        {
            flags_to_be_set |= LLInventoryItemFlags::II_FLAGS_OBJECT_PERM_OVERWRITE_GROUP;
        }
        LLSaleInfo old_sale_info = old_item.getSaleInfo();
        if(old_sale_info != getSaleInfo())
        {
            flags_to_be_set |= LLInventoryItemFlags::II_FLAGS_OBJECT_SLAM_SALE;
        }
        setFlags(getFlags() | flags_to_be_set);
    }
}

const LLSaleInfo& LLInventoryItem::getSaleInfo() const
{
    return mSaleInfo;
}

void LLInventoryItem::setSaleInfo(const LLSaleInfo& sale_info)
{
    mSaleInfo = sale_info;
}

LLInventoryType::EType LLInventoryItem::getInventoryType() const
{
    return mInventoryType;
}

U32 LLInventoryItem::getFlags() const
{
    return mFlags;
}

time_t LLInventoryItem::getCreationDate() const
{
    return mCreationDate;
}


// virtual
void LLInventoryItem::packMessage(LLMessageSystem* msg) const
{
    msg->addUUIDFast(_PREHASH_ItemID, mUUID);
    msg->addUUIDFast(_PREHASH_FolderID, mParentUUID);
    mPermissions.packMessage(msg);
    msg->addUUIDFast(_PREHASH_AssetID, mAssetUUID);
    S8 type = static_cast<S8>(mType);
    msg->addS8Fast(_PREHASH_Type, type);
    type = static_cast<S8>(mInventoryType);
    msg->addS8Fast(_PREHASH_InvType, type);
    msg->addU32Fast(_PREHASH_Flags, mFlags);
    mSaleInfo.packMessage(msg);
    msg->addStringFast(_PREHASH_Name, mName);
    msg->addStringFast(_PREHASH_Description, mDescription);
    msg->addS32Fast(_PREHASH_CreationDate, (S32)mCreationDate);
    U32 crc = getCRC32();
    msg->addU32Fast(_PREHASH_CRC, crc);
}

// virtual
bool LLInventoryItem::unpackMessage(LLMessageSystem* msg, const char* block, S32 block_num)
{
    msg->getUUIDFast(block, _PREHASH_ItemID, mUUID, block_num);
    msg->getUUIDFast(block, _PREHASH_FolderID, mParentUUID, block_num);
    mPermissions.unpackMessage(msg, block, block_num);
    msg->getUUIDFast(block, _PREHASH_AssetID, mAssetUUID, block_num);

    S8 type;
    msg->getS8Fast(block, _PREHASH_Type, type, block_num);
    mType = static_cast<LLAssetType::EType>(type);
    msg->getS8(block, "InvType", type, block_num);
    mInventoryType = static_cast<LLInventoryType::EType>(type);
    mPermissions.initMasks(mInventoryType);

    msg->getU32Fast(block, _PREHASH_Flags, mFlags, block_num);

    mSaleInfo.unpackMultiMessage(msg, block, block_num);

    msg->getStringFast(block, _PREHASH_Name, mName, block_num);
    LLStringUtil::replaceNonstandardASCII(mName, ' ');

    msg->getStringFast(block, _PREHASH_Description, mDescription, block_num);
    LLStringUtil::replaceNonstandardASCII(mDescription, ' ');

    S32 date;
    msg->getS32(block, "CreationDate", date, block_num);
    mCreationDate = date;

    U32 local_crc = getCRC32();
    U32 remote_crc = 0;
    msg->getU32(block, "CRC", remote_crc, block_num);
//#define CRC_CHECK
#ifdef CRC_CHECK
    if(local_crc == remote_crc)
    {
        LL_DEBUGS() << "crc matches" << LL_ENDL;
        return true;
    }
    else
    {
        LL_WARNS() << "inventory crc mismatch: local=" << std::hex << local_crc
                << " remote=" << remote_crc << std::dec << LL_ENDL;
        return false;
    }
#else
    return (local_crc == remote_crc);
#endif
}

// virtual
bool LLInventoryItem::importLegacyStream(std::istream& input_stream)
{
    // *NOTE: Changing the buffer size will require changing the scanf
    // calls below.
    char buffer[MAX_STRING];    /* Flawfinder: ignore */
    char keyword[MAX_STRING];   /* Flawfinder: ignore */
    char valuestr[MAX_STRING];  /* Flawfinder: ignore */
    char junk[MAX_STRING];  /* Flawfinder: ignore */
    bool success = true;

    keyword[0] = '\0';
    valuestr[0] = '\0';

    mInventoryType = LLInventoryType::IT_NONE;
    mAssetUUID.setNull();
    while(success && input_stream.good())
    {
        input_stream.getline(buffer, MAX_STRING);
        // Each line is parsed on its own; a line that does not parse is absent.
        keyword[0] = '\0';
        valuestr[0] = '\0';
        S32 fields = sscanf( /* Flawfinder: ignore */
            buffer,
            " %254s %254s",
            keyword, valuestr);
        if (fields < 1)
        {
            // a blank line counts as absent
            continue;
        }
        if(0 == strcmp("{",keyword))
        {
            continue;
        }
        if(0 == strcmp("}", keyword))
        {
            break;
        }
        else if (fields < 2 && 0 != strcmp("permissions", keyword) && 0 != strcmp("sale_info", keyword) && 0 != strcmp("metadata", keyword))
        {
            // a keyword without a value counts as absent
            // (block headers and metadata keep their own handling)
            continue;
        }
        else if(0 == strcmp("item_id", keyword))
        {
            mUUID.set(valuestr);
        }
        else if(0 == strcmp("parent_id", keyword))
        {
            mParentUUID.set(valuestr);
        }
        else if(0 == strcmp("permissions", keyword))
        {
            success = mPermissions.importLegacyStream(input_stream);
        }
        else if(0 == strcmp("sale_info", keyword))
        {
            // Sale info used to contain next owner perm. It is now in
            // the permissions. Thus, we read that out, and fix legacy
            // objects. It's possible this op would fail, but it
            // should pick up the vast majority of the tasks.
            bool has_perm_mask = false;
            U32 perm_mask = 0;
            success = mSaleInfo.importLegacyStream(input_stream, has_perm_mask, perm_mask);
            if(has_perm_mask)
            {
                if(perm_mask == PERM_NONE)
                {
                    perm_mask = mPermissions.getMaskOwner();
                }
                // fair use fix.
                if(!(perm_mask & PERM_COPY))
                {
                    perm_mask |= PERM_TRANSFER;
                }
                mPermissions.setMaskNext(perm_mask);
            }
        }
        else if(0 == strcmp("shadow_id", keyword))
        {
            mAssetUUID.set(valuestr);
            LLXORCipher cipher(MAGIC_ID.mData, UUID_BYTES);
            cipher.decrypt(mAssetUUID.mData, UUID_BYTES);
        }
        else if(0 == strcmp("asset_id", keyword))
        {
            mAssetUUID.set(valuestr);
        }
        else if(0 == strcmp("type", keyword))
        {
            mType = LLAssetType::lookup(valuestr);
        }
        else if (0 == strcmp("metadata", keyword))
        {
            LLSD metadata;
            if (strncmp("<llsd>", valuestr, 6) == 0)
            {
                std::istringstream stream(valuestr);
                LLSDSerialize::fromXML(metadata, stream);
            }
            else
            {
                // next line likely contains metadata, but at the moment is not supported
                // can do something like:
                // LLSDSerialize::fromNotation(metadata, input_stream, -1);
            }

            setRuntime(metadata["script"]["runtime"].asString());
            if (metadata.has("thumbnail"))
            {
                const LLSD& thumbnail = metadata["thumbnail"];
                if (thumbnail.has("asset_id"))
                {
                    setThumbnailUUID(thumbnail["asset_id"].asUUID());
                }
                else
                {
                    setThumbnailUUID(LLUUID::null);
                }
            }
            else
            {
                setThumbnailUUID(LLUUID::null);
            }

            if (metadata.has("favorite"))
            {
                const LLSD& favorite = metadata["favorite"];
                if (favorite.has("toggled"))
                {
                    setFavorite(favorite["toggled"].asBoolean());
                }
                else
                {
                    setFavorite(false);
                }
            }
            else
            {
                setFavorite(false);
            }
        }
        else if(0 == strcmp("inv_type", keyword))
        {
            mInventoryType = LLInventoryType::lookup(std::string(valuestr));
        }
        else if(0 == strcmp("flags", keyword))
        {
            sscanf(valuestr, "%x", &mFlags);
        }
        else if(0 == strcmp("name", keyword))
        {
            //strcpy(valuestr, buffer + strlen(keyword) + 3);
            // *NOTE: Not ANSI C, but widely supported.
            sscanf( /* Flawfinder: ignore */
                buffer,
                " %254s%254[\t]%254[^|]",
                keyword, junk, valuestr);

            // IW: sscanf chokes and puts | in valuestr if there's no name
            if (valuestr[0] == '|')
            {
                valuestr[0] = '\000';
            }

            mName.assign(valuestr);
            LLStringUtil::replaceNonstandardASCII(mName, ' ');
            LLStringUtil::replaceChar(mName, '|', ' ');
        }
        else if(0 == strcmp("desc", keyword))
        {
            //strcpy(valuestr, buffer + strlen(keyword) + 3);
            // *NOTE: Not ANSI C, but widely supported.
            sscanf( /* Flawfinder: ignore */
                buffer,
                " %254s%254[\t]%254[^|]",
                keyword, junk, valuestr);

            if (valuestr[0] == '|')
            {
                valuestr[0] = '\000';
            }

            mDescription.assign(valuestr);
            LLStringUtil::replaceNonstandardASCII(mDescription, ' ');
            /* TODO -- ask Ian about this code
            const char *donkey = mDescription.c_str();
            if (donkey[0] == '|')
            {
                LL_ERRS() << "Donkey" << LL_ENDL;
            }
            */
        }
        else if(0 == strcmp("creation_date", keyword))
        {
            S32 date = 0;
            if (1 == sscanf(valuestr, "%d", &date))
            {
                mCreationDate = date;
            }
        }
        else
        {
            LL_WARNS() << "unknown keyword '" << keyword
                    << "' in inventory import of item " << mUUID << LL_ENDL;
        }
    }

    // Need to convert 1.0 simstate files to a useful inventory type
    // and potentially deal with bad inventory tyes eg, a landmark
    // marked as a texture.
    if((LLInventoryType::IT_NONE == mInventoryType)
       || !inventory_and_asset_types_match(mInventoryType, mType))
    {
        LL_DEBUGS() << "Resetting inventory type for " << mUUID << LL_ENDL;
        mInventoryType = LLInventoryType::defaultForAssetType(mType);
    }

    mPermissions.initMasks(mInventoryType);

    return success;
}

bool LLInventoryItem::exportLegacyStream(std::ostream& output_stream, bool include_asset_key) const
{
    std::string uuid_str;
    output_stream << "\tinv_item\t0\n\t{\n";
    mUUID.toString(uuid_str);
    output_stream << "\t\titem_id\t" << uuid_str << "\n";
    mParentUUID.toString(uuid_str);
    output_stream << "\t\tparent_id\t" << uuid_str << "\n";
    mPermissions.exportLegacyStream(output_stream);

    if (mThumbnailUUID.notNull() || !mRuntime.empty())
    {
        // Max length is 255 chars, will have to export differently if it gets more data
        // Ex: use newline and toNotation (uses {}) for unlimited size
        LLSD metadata;
        if (mThumbnailUUID.notNull())
        {
            metadata["thumbnail"] = LLSD().with("asset_id", mThumbnailUUID);
        }
        if (!mRuntime.empty())
        {
            metadata["script"] = LLSD().with("runtime", mRuntime);
        }

        output_stream << "\t\tmetadata\t";
        LLSDSerialize::toXML(metadata, output_stream);
        output_stream << "|\n";
    }

    // Check for permissions to see the asset id, and if so write it
    // out as an asset id. Otherwise, apply our cheesy encryption.
    if(include_asset_key)
    {
        U32 mask = mPermissions.getMaskBase();
        if(((mask & PERM_ITEM_UNRESTRICTED) == PERM_ITEM_UNRESTRICTED)
           || (mAssetUUID.isNull()))
        {
            mAssetUUID.toString(uuid_str);
            output_stream << "\t\tasset_id\t" << uuid_str << "\n";
        }
        else
        {
            LLUUID shadow_id(mAssetUUID);
            LLXORCipher cipher(MAGIC_ID.mData, UUID_BYTES);
            cipher.encrypt(shadow_id.mData, UUID_BYTES);
            shadow_id.toString(uuid_str);
            output_stream << "\t\tshadow_id\t" << uuid_str << "\n";
        }
    }
    else
    {
        LLUUID::null.toString(uuid_str);
        output_stream << "\t\tasset_id\t" << uuid_str << "\n";
    }
    output_stream << "\t\ttype\t" << LLAssetType::lookup(mType) << "\n";
    const std::string inv_type_str = LLInventoryType::lookup(mInventoryType);
    if(!inv_type_str.empty())
        output_stream << "\t\tinv_type\t" << inv_type_str << "\n";
    std::string buffer;
    buffer = llformat( "\t\tflags\t%08x\n", mFlags);
    output_stream << buffer;
    mSaleInfo.exportLegacyStream(output_stream);
    output_stream << "\t\tname\t" << mName.c_str() << "|\n";
    output_stream << "\t\tdesc\t" << mDescription.c_str() << "|\n";
    output_stream << "\t\tcreation_date\t" << mCreationDate << "\n";
    output_stream << "\t}\n";
    return true;
}

LLSD LLInventoryItem::asLLSD() const
{
    LLSD sd;
    asLLSD(sd);
    return sd;
}

void LLInventoryItem::asLLSD( LLSD& sd ) const
{
    sd[INV_ITEM_ID_LABEL] = mUUID;
    sd[INV_PARENT_ID_LABEL] = mParentUUID;
    ll_fill_sd_from_permissions(sd[INV_PERMISSIONS_LABEL], mPermissions);

    if (mThumbnailUUID.notNull())
    {
        sd[INV_THUMBNAIL_LABEL] = LLSD().with(INV_ASSET_ID_LABEL, mThumbnailUUID);
    }

    if (mFavorite)
    {
        sd[INV_FAVORITE_LABEL] = LLSD().with(INV_TOGGLED_LABEL, mFavorite);
    }

    if (!mRuntime.empty())
    {
        sd["script"] = LLSD().with("runtime", mRuntime);
    }

    U32 mask = mPermissions.getMaskBase();
    if(((mask & PERM_ITEM_UNRESTRICTED) == PERM_ITEM_UNRESTRICTED)
        || (mAssetUUID.isNull()))
    {
        sd[INV_ASSET_ID_LABEL] = mAssetUUID;
    }
    else
    {
        // *TODO: get rid of this. Phoenix 2008-01-30
        LLUUID shadow_id(mAssetUUID);
        LLXORCipher cipher(MAGIC_ID.mData, UUID_BYTES);
        cipher.encrypt(shadow_id.mData, UUID_BYTES);
        sd[INV_SHADOW_ID_LABEL] = shadow_id;
    }
    sd[INV_ASSET_TYPE_LABEL] = std::string(LLAssetType::lookup(mType));
    const std::string inv_type_str = LLInventoryType::lookup(mInventoryType);
    if(!inv_type_str.empty())
    {
        sd[INV_INVENTORY_TYPE_LABEL] = inv_type_str;
    }
    else
    {
        sd[INV_INVENTORY_TYPE_LABEL] = (LLSD::Integer)mInventoryType;
    }
    //sd[INV_FLAGS_LABEL] = (S32)mFlags;
    sd[INV_FLAGS_LABEL] = ll_sd_from_U32(mFlags);
    mSaleInfo.asLLSD(sd[INV_SALE_INFO_LABEL]);
    sd[INV_NAME_LABEL] = mName;
    sd[INV_DESC_LABEL] = mDescription;
    sd[INV_CREATION_DATE_LABEL] = (LLSD::Integer)mCreationDate;
}

bool LLInventoryItem::fromLLSD(const LLSD& sd, bool is_new)
{
    LL_PROFILE_ZONE_SCOPED;
    if (is_new)
    {
        // If we're adding LLSD to an existing object, need avoid
        // clobbering these fields.
        mInventoryType = LLInventoryType::IT_NONE;
        mAssetUUID.setNull();
    }

    // TODO - figure out if this should be moved into the noclobber fields above
    mThumbnailUUID.setNull();
    mFavorite = false;
    // <FS:TJ> [FIRE-36028] Fix OpenSim object permissions
    //mPermissions.init(LLUUID::null, LLUUID::null, LLUUID::null, LLUUID::null);
    if (!LLPermissions::getIsInOpenSim())
    {
        mPermissions.init(LLUUID::null, LLUUID::null, LLUUID::null, LLUUID::null);
    }
    // </FS:TJ>

    if (is_new || sd.has("script") || (sd.has("metadata") && sd["metadata"].has("script")))
    {
        mRuntime = sd.has("script") ? sd["script"]["runtime"].asString()
                                   : sd["metadata"]["script"]["runtime"].asString();
    }

    // iterate as map to avoid making unnecessary temp copies of everything
    LLSD::map_const_iterator i, end;
    end = sd.endMap();
    for (i = sd.beginMap(); i != end; ++i)
    {
        if (i->first == INV_ITEM_ID_LABEL)
        {
            mUUID = i->second;
            continue;
        }

        if (i->first == INV_PARENT_ID_LABEL)
        {
            mParentUUID = i->second;
            continue;
        }

        if (i->first == INV_THUMBNAIL_LABEL)
        {
            const LLSD &thumbnail_map = i->second;
            if (thumbnail_map.has(INV_ASSET_ID_LABEL))
            {
                mThumbnailUUID = thumbnail_map[INV_ASSET_ID_LABEL];
            }
            /* Example:
                <key> asset_id </key>
                <uuid> acc0ec86 - 17f2 - 4b92 - ab41 - 6718b1f755f7 </uuid>
                <key> perms </key>
                <integer> 8 </integer>
                <key>service</key>
                <integer> 3 </integer>
                <key>version</key>
                <integer> 1 </key>
            */
          continue;
        }

        if (i->first == INV_THUMBNAIL_ID_LABEL)
        {
            mThumbnailUUID = i->second.asUUID();
            continue;
        }

        if (i->first == INV_FAVORITE_LABEL)
        {
            const LLSD& favorite_map = i->second;
            if (favorite_map.has(INV_TOGGLED_LABEL))
            {
                mFavorite = favorite_map[INV_TOGGLED_LABEL].asBoolean();
            }
            continue;
        }

        if (i->first == INV_PERMISSIONS_LABEL)
        {
            mPermissions.importLLSD(i->second);
            continue;
        }

        if (i->first == INV_SALE_INFO_LABEL)
        {
            // Sale info used to contain next owner perm. It is now in
            // the permissions. Thus, we read that out, and fix legacy
            // objects. It's possible this op would fail, but it
            // should pick up the vast majority of the tasks.
            bool has_perm_mask = false;
            U32  perm_mask     = 0;
            if (!mSaleInfo.fromLLSD(i->second, has_perm_mask, perm_mask))
            {
                return false;
            }
            if (has_perm_mask)
            {
                if (perm_mask == PERM_NONE)
                {
                    perm_mask = mPermissions.getMaskOwner();
                }
                // fair use fix.
                if (!(perm_mask & PERM_COPY))
                {
                    perm_mask |= PERM_TRANSFER;
                }
                mPermissions.setMaskNext(perm_mask);
            }
            continue;
        }

        if (i->first == INV_SHADOW_ID_LABEL)
        {
            mAssetUUID = i->second;
            LLXORCipher cipher(MAGIC_ID.mData, UUID_BYTES);
            cipher.decrypt(mAssetUUID.mData, UUID_BYTES);
            continue;
        }

        if (i->first == INV_ASSET_ID_LABEL)
        {
            mAssetUUID = i->second;
            continue;
        }

        if (i->first == INV_LINKED_ID_LABEL)
        {
            mAssetUUID = i->second;
            continue;
        }

        if (i->first == INV_ASSET_TYPE_LABEL)
        {
            LLSD const &label = i->second;
            if (label.isString())
            {
                mType = LLAssetType::lookup(label.asStringRef().c_str());
            }
            else if (label.isInteger())
            {
                S8 type = (U8) label.asInteger();
                mType   = static_cast<LLAssetType::EType>(type);
            }
            continue;
        }

        if (i->first == INV_INVENTORY_TYPE_LABEL)
        {
            LLSD const &label = i->second;
            if (label.isString())
            {
                mInventoryType = LLInventoryType::lookup(label.asStringRef().c_str());
            }
            else if (label.isInteger())
            {
                S8 type        = (U8) label.asInteger();
                mInventoryType = static_cast<LLInventoryType::EType>(type);
            }
            continue;
        }

        if (i->first == INV_FLAGS_LABEL)
        {
            LLSD const &label = i->second;
            if (label.isBinary())
            {
                mFlags = ll_U32_from_sd(label);
            }
            else if (label.isInteger())
            {
                mFlags = label.asInteger();
            }
            continue;
        }

        if (i->first == INV_NAME_LABEL)
        {
            mName = i->second.asString();
            LLStringUtil::replaceNonstandardASCII(mName, ' ');
            LLStringUtil::replaceChar(mName, '|', ' ');
            continue;
        }

        if (i->first == INV_DESC_LABEL)
        {
            mDescription = i->second.asString();
            LLStringUtil::replaceNonstandardASCII(mDescription, ' ');
            continue;
        }

        if (i->first == INV_CREATION_DATE_LABEL)
        {
            mCreationDate = i->second.asInteger();
            continue;
        }
    }

    // Need to convert 1.0 simstate files to a useful inventory type
    // and potentially deal with bad inventory tyes eg, a landmark
    // marked as a texture.
    if((LLInventoryType::IT_NONE == mInventoryType)
       || !inventory_and_asset_types_match(mInventoryType, mType))
    {
        LL_DEBUGS() << "Resetting inventory type for " << mUUID << LL_ENDL;
        mInventoryType = LLInventoryType::defaultForAssetType(mType);
    }

    mPermissions.initMasks(mInventoryType);

    return true;
}

///----------------------------------------------------------------------------
/// Class LLInventoryCategory
///----------------------------------------------------------------------------

LLInventoryCategory::LLInventoryCategory(const LLUUID& uuid,
                                         const LLUUID& parent_uuid,
                                         LLFolderType::EType preferred_type,
                                         const std::string& name) :
    LLInventoryObject(uuid, parent_uuid, LLAssetType::AT_CATEGORY, name),
    mPreferredType(preferred_type)
{
}

LLInventoryCategory::LLInventoryCategory() :
    mPreferredType(LLFolderType::FT_NONE)
{
    mType = LLAssetType::AT_CATEGORY;
}

LLInventoryCategory::LLInventoryCategory(const LLInventoryCategory* other) :
    LLInventoryObject()
{
    copyCategory(other);
}

LLInventoryCategory::~LLInventoryCategory()
{
}

// virtual
void LLInventoryCategory::copyCategory(const LLInventoryCategory* other)
{
    copyObject(other);
    mPreferredType = other->mPreferredType;
}

LLFolderType::EType LLInventoryCategory::getPreferredType() const
{
    return mPreferredType;
}

void LLInventoryCategory::setPreferredType(LLFolderType::EType type)
{
    mPreferredType = type;
}

LLSD LLInventoryCategory::asLLSD() const
{
    LLSD sd = LLSD();
    sd[INV_ITEM_ID_LABEL]   = mUUID;
    sd[INV_PARENT_ID_LABEL] = mParentUUID;
    S8 type = static_cast<S8>(mPreferredType);
    sd[INV_ASSET_TYPE_LABEL] = type;
    sd[INV_NAME_LABEL] = mName;

    if (mThumbnailUUID.notNull())
    {
        sd[INV_THUMBNAIL_LABEL] = LLSD().with(INV_ASSET_ID_LABEL, mThumbnailUUID);
    }

    if (mFavorite)
    {
        sd[INV_FAVORITE_LABEL] = LLSD().with(INV_TOGGLED_LABEL, mFavorite);
    }

    return sd;
}

LLSD LLInventoryCategory::asAISCreateCatLLSD() const
{
    LLSD sd                 = LLSD();
    sd[INV_FOLDER_ID_LABEL_WS]  = mUUID;
    sd[INV_PARENT_ID_LABEL] = mParentUUID;
    S8 type                 = static_cast<S8>(mPreferredType);
    sd[INV_ASSET_TYPE_LABEL_WS] = type;
    sd[INV_NAME_LABEL] = mName;

    if (mThumbnailUUID.notNull())
    {
        sd[INV_THUMBNAIL_LABEL] = LLSD().with(INV_ASSET_ID_LABEL, mThumbnailUUID);
    }

    if (mFavorite)
    {
        sd[INV_FAVORITE_LABEL] = LLSD().with(INV_TOGGLED_LABEL, mFavorite);
    }

    return sd;
}


// virtual
void LLInventoryCategory::packMessage(LLMessageSystem* msg) const
{
    msg->addUUIDFast(_PREHASH_FolderID, mUUID);
    msg->addUUIDFast(_PREHASH_ParentID, mParentUUID);
    S8 type = static_cast<S8>(mPreferredType);
    msg->addS8Fast(_PREHASH_Type, type);
    msg->addStringFast(_PREHASH_Name, mName);
}

bool LLInventoryCategory::fromLLSD(const LLSD& sd)
{
    std::string_view w;

    w = INV_FOLDER_ID_LABEL_WS;
    if (sd.has(w))
    {
        mUUID = sd[w];
    }
    w = INV_PARENT_ID_LABEL;
    if (sd.has(w))
    {
        mParentUUID = sd[w];
    }
    mThumbnailUUID.setNull();
    w = INV_THUMBNAIL_LABEL;
    if (sd.has(w))
    {
        const LLSD &thumbnail_map = sd[w];
        w = INV_ASSET_ID_LABEL;
        if (thumbnail_map.has(w))
        {
            mThumbnailUUID = thumbnail_map[w];
        }
    }
    else
    {
        w = INV_THUMBNAIL_ID_LABEL;
        if (sd.has(w))
        {
            mThumbnailUUID = sd[w];
        }
    }
    mFavorite = false;
    w = INV_FAVORITE_LABEL;
    if (sd.has(w))
    {
        const LLSD& favorite_map = sd[w];
        w = INV_TOGGLED_LABEL;
        if (favorite_map.has(w))
        {
            mFavorite = favorite_map[w].asBoolean();
        }
    }
    w = INV_ASSET_TYPE_LABEL;
    if (sd.has(w))
    {
        S8 type = (U8)sd[w].asInteger();
        mPreferredType = static_cast<LLFolderType::EType>(type);
    }
    w = INV_ASSET_TYPE_LABEL_WS;
    if (sd.has(w))
    {
        S8 type = (U8)sd[w].asInteger();
        mPreferredType = static_cast<LLFolderType::EType>(type);
    }

    w = INV_NAME_LABEL;
    if (sd.has(w))
    {
        mName = sd[w].asString();
        LLStringUtil::replaceNonstandardASCII(mName, ' ');
        LLStringUtil::replaceChar(mName, '|', ' ');
    }
    return true;
}

// virtual
void LLInventoryCategory::unpackMessage(LLMessageSystem* msg,
                                        const char* block,
                                        S32 block_num)
{
    msg->getUUIDFast(block, _PREHASH_FolderID, mUUID, block_num);
    msg->getUUIDFast(block, _PREHASH_ParentID, mParentUUID, block_num);
    S8 type;
    msg->getS8Fast(block, _PREHASH_Type, type, block_num);
    mPreferredType = static_cast<LLFolderType::EType>(type);
    msg->getStringFast(block, _PREHASH_Name, mName, block_num);
    LLStringUtil::replaceNonstandardASCII(mName, ' ');
}

// virtual
bool LLInventoryCategory::importLegacyStream(std::istream& input_stream)
{
    // *NOTE: Changing the buffer size will require changing the scanf
    // calls below.
    char buffer[MAX_STRING];    /* Flawfinder: ignore */
    char keyword[MAX_STRING];   /* Flawfinder: ignore */
    char valuestr[MAX_STRING];  /* Flawfinder: ignore */

    keyword[0] = '\0';
    valuestr[0] = '\0';
    while(input_stream.good())
    {
        input_stream.getline(buffer, MAX_STRING);
        // Each line is parsed on its own; a line that does not parse is absent.
        keyword[0] = '\0';
        valuestr[0] = '\0';
        S32 fields = sscanf( /* Flawfinder: ignore */
            buffer,
            " %254s %254s",
            keyword, valuestr);
        if (fields < 1)
        {
            // a blank line counts as absent
            continue;
        }
        if(0 == strcmp("{",keyword))
        {
            continue;
        }
        if(0 == strcmp("}", keyword))
        {
            break;
        }
        else if (fields < 2 && 0 != strcmp("metadata", keyword))
        {
            // a keyword without a value counts as absent
            // (metadata keeps its own handling)
            continue;
        }
        else if(0 == strcmp("cat_id", keyword))
        {
            mUUID.set(valuestr);
        }
        else if(0 == strcmp("parent_id", keyword))
        {
            mParentUUID.set(valuestr);
        }
        else if(0 == strcmp("type", keyword))
        {
            mType = LLAssetType::lookup(valuestr);
        }
        else if(0 == strcmp("pref_type", keyword))
        {
            mPreferredType = LLFolderType::lookup(valuestr);
        }
        else if(0 == strcmp("name", keyword))
        {
            //strcpy(valuestr, buffer + strlen(keyword) + 3);
            // *NOTE: Not ANSI C, but widely supported.
            sscanf( /* Flawfinder: ignore */
                buffer,
                " %254s %254[^|]",
                keyword, valuestr);
            mName.assign(valuestr);
            LLStringUtil::replaceNonstandardASCII(mName, ' ');
            LLStringUtil::replaceChar(mName, '|', ' ');
        }
        else if (0 == strcmp("metadata", keyword))
        {
            LLSD metadata;
            if (strncmp("<llsd>", valuestr, 6) == 0)
            {
                std::istringstream stream(valuestr);
                LLSDSerialize::fromXML(metadata, stream);
            }
            else
            {
                // next line likely contains metadata, but at the moment is not supported
                // can do something like:
                // LLSDSerialize::fromNotation(metadata, input_stream, -1);
            }

            setRuntime(metadata["script"]["runtime"].asString());
            if (metadata.has("thumbnail"))
            {
                const LLSD& thumbnail = metadata["thumbnail"];
                if (thumbnail.has("asset_id"))
                {
                    setThumbnailUUID(thumbnail["asset_id"].asUUID());
                }
                else
                {
                    setThumbnailUUID(LLUUID::null);
                }
            }
            else
            {
                setThumbnailUUID(LLUUID::null);
            }

            if (metadata.has("favorite"))
            {
                const LLSD& favorite = metadata["favorite"];
                if (favorite.has("toggled"))
                {
                    setFavorite(favorite["toggled"].asBoolean());
                }
                else
                {
                    setFavorite(false);
                }
            }
            else
            {
                setFavorite(false);
            }
        }
        else
        {
            LL_WARNS() << "unknown keyword '" << keyword
                    << "' in inventory import category "  << mUUID << LL_ENDL;
        }
    }
    return true;
}

bool LLInventoryCategory::exportLegacyStream(std::ostream& output_stream, bool) const
{
    std::string uuid_str;
    output_stream << "\tinv_category\t0\n\t{\n";
    mUUID.toString(uuid_str);
    output_stream << "\t\tcat_id\t" << uuid_str << "\n";
    mParentUUID.toString(uuid_str);
    output_stream << "\t\tparent_id\t" << uuid_str << "\n";
    output_stream << "\t\ttype\t" << LLAssetType::lookup(mType) << "\n";
    output_stream << "\t\tpref_type\t" << LLFolderType::lookup(mPreferredType) << "\n";
    output_stream << "\t\tname\t" << mName.c_str() << "|\n";
    if (mThumbnailUUID.notNull())
    {
        // Only up to 255 chars
        LLSD metadata;
        metadata["thumbnail"] = LLSD().with("asset_id", mThumbnailUUID);
        output_stream << "\t\tmetadata\t";
        LLSDSerialize::toXML(metadata, output_stream);
        output_stream << "|\n";
    }
    output_stream << "\t}\n";
    return true;
}

void LLInventoryCategory::exportLLSD(LLSD& cat_data) const
{
    cat_data[INV_FOLDER_ID_LABEL] = mUUID;
    cat_data[INV_PARENT_ID_LABEL] = mParentUUID;
    cat_data[INV_ASSET_TYPE_LABEL] = std::string(LLAssetType::lookup(mType));
    cat_data[INV_PREFERRED_TYPE_LABEL] = LLFolderType::lookup(mPreferredType);
    cat_data[INV_NAME_LABEL] = mName;

    if (mThumbnailUUID.notNull())
    {
        cat_data[INV_THUMBNAIL_LABEL] = LLSD().with(INV_ASSET_ID_LABEL, mThumbnailUUID);
    }
    if (mFavorite)
    {
        cat_data[INV_FAVORITE_LABEL] = LLSD().with(INV_TOGGLED_LABEL, mFavorite);
    }
}

bool LLInventoryCategory::importLLSDMap(const LLSD& cat_data)
{
    LLSD::map_const_iterator i, end;
    end = cat_data.endMap();
    for ( i = cat_data.beginMap(); i != end; ++i)
    {
        importLLSD(i->first, i->second);
    }
    return true;
}

bool LLInventoryCategory::importLLSD(const std::string& label, const LLSD& value)
{
    if (label == INV_FOLDER_ID_LABEL)
    {
        setUUID(value.asUUID());
        return true;
    }
    else if (label == INV_PARENT_ID_LABEL)
    {
        setParent(value.asUUID());
        return true;
    }
    else if (label == INV_ASSET_TYPE_LABEL)
    {
        setType(LLAssetType::lookup(value.asString()));
        return true;
    }
    else if (label == INV_PREFERRED_TYPE_LABEL)
    {
        setPreferredType(LLFolderType::lookup(value.asString()));
        return true;
    }
    else if (label == INV_THUMBNAIL_LABEL)
    {
        LLUUID thumbnail_uuid;
        if (value.has(INV_ASSET_ID_LABEL))
        {
            thumbnail_uuid = value[INV_ASSET_ID_LABEL].asUUID();
        }
        setThumbnailUUID(thumbnail_uuid);
        return true;
    }
    if (label == INV_FAVORITE_LABEL)
    {
        bool favorite = false;
        if (value.has(INV_TOGGLED_LABEL))
        {
            favorite = value[INV_TOGGLED_LABEL].asBoolean();
        }
        setFavorite(favorite);
    }
    else if (label == INV_NAME_LABEL)
    {
        mName = value.asString();
        LLStringUtil::replaceNonstandardASCII(mName, ' ');
        LLStringUtil::replaceChar(mName, '|', ' ');
        return true;
    }
    return false;
}

///----------------------------------------------------------------------------
/// Local function definitions for testing purposes
///----------------------------------------------------------------------------

LLSD ll_create_sd_from_inventory_item(LLPointer<LLInventoryItem> item)
{
    LLSD rv;
    if(item.isNull()) return rv;
    if (item->getType() == LLAssetType::AT_NONE)
    {
        LL_WARNS() << "ll_create_sd_from_inventory_item() for item with AT_NONE"
            << LL_ENDL;
        return rv;
    }
    rv[INV_ITEM_ID_LABEL] =  item->getUUID();
    rv[INV_PARENT_ID_LABEL] = item->getParentUUID();
    rv[INV_NAME_LABEL] = item->getName();
    rv[INV_ASSET_TYPE_LABEL] = LLAssetType::lookup(item->getType());
    rv[INV_ASSET_ID_LABEL] = item->getAssetUUID();
    rv[INV_DESC_LABEL] = item->getDescription();
    rv[INV_SALE_INFO_LABEL] = ll_create_sd_from_sale_info(item->getSaleInfo());
    rv[INV_PERMISSIONS_LABEL] =
        ll_create_sd_from_permissions(item->getPermissions());
    rv[INV_INVENTORY_TYPE_LABEL] =
        LLInventoryType::lookup(item->getInventoryType());
    rv[INV_FLAGS_LABEL] = (S32)item->getFlags();
    rv[INV_CREATION_DATE_LABEL] = (S32)item->getCreationDate();
    return rv;
}

LLSD ll_create_sd_from_inventory_category(LLPointer<LLInventoryCategory> cat)
{
    LLSD rv;
    if(cat.isNull()) return rv;
    if (cat->getType() == LLAssetType::AT_NONE)
    {
        LL_WARNS() << "ll_create_sd_from_inventory_category() for cat with AT_NONE"
            << LL_ENDL;
        return rv;
    }
    rv[INV_FOLDER_ID_LABEL] = cat->getUUID();
    rv[INV_PARENT_ID_LABEL] = cat->getParentUUID();
    rv[INV_NAME_LABEL] = cat->getName();
    rv[INV_ASSET_TYPE_LABEL] = LLAssetType::lookup(cat->getType());
    if(LLFolderType::lookupIsProtectedType(cat->getPreferredType()))
    {
        rv[INV_PREFERRED_TYPE_LABEL] =
            LLFolderType::lookup(cat->getPreferredType()).c_str();
    }
    return rv;
}

LLPointer<LLInventoryCategory> ll_create_category_from_sd(const LLSD& sd_cat)
{
    LLPointer<LLInventoryCategory> rv = new LLInventoryCategory;
    rv->setUUID(sd_cat[INV_FOLDER_ID_LABEL].asUUID());
    rv->setParent(sd_cat[INV_PARENT_ID_LABEL].asUUID());
    rv->rename(sd_cat[INV_NAME_LABEL].asString());
    rv->setType(
        LLAssetType::lookup(sd_cat[INV_ASSET_TYPE_LABEL].asString()));
    rv->setPreferredType(
            LLFolderType::lookup(
                sd_cat[INV_PREFERRED_TYPE_LABEL].asString()));
    return rv;
}
// ---- VKS-ORACLE-COPY END

// ---- VKS-ORACLE-COPY BEGIN
// source: indra/llinventory/llcategory.cpp
// lines: 25-190
// blob: a945cdf7cc02c0ca710b6b086f996272877107d0
// copied: the whole file after its licence block
// substitutions: #include lines and forward declarations of LLMessageSystem and LLUUID removed
// conditionals: 
// ----




const LLCategory LLCategory::none;

///----------------------------------------------------------------------------
/// Local function declarations, constants, enums, and typedefs
///----------------------------------------------------------------------------

// This is the storage of the category names. It's loosely based on a
// heap-like structure with indices into it for faster searching and
// so that we don't have to maintain a balanced heap. It's *VITALLY*
// important that the CATEGORY_INDEX and CATEGORY_NAME tables are kept
// in synch.

// CATEGORY_INDEX indexes into CATEGORY_NAME at the first occurance of
// a child. Thus, the first child of root is "Object" which is located
// in CATEGORY_NAME[1].
const S32 CATEGORY_INDEX[] =
{
    1,  // ROOT
    6,  // object
    7,  // clothing
    7,  // texture
    7,  // sound
    7,  // landmark
    7,  // object|component
    7,  // off the end (required for child count calculations)
};

// The heap of names
const char* CATEGORY_NAME[] =
{
    "(none)",
    "Object",       // (none)
    "Clothing",
    "Texture",
    "Sound",
    "Landmark",
    "Component",    // object
    NULL
};

///----------------------------------------------------------------------------
/// Class llcategory
///----------------------------------------------------------------------------

LLCategory::LLCategory()
{
    // this is used as a simple compile time assertion. If this code
    // fails to compile, the depth has been changed, and we need to
    // clean up some of the code that relies on the depth, such as the
    // default constructor. If CATEGORY_DEPTH != 4, this code will
    // attempt to construct a zero length array - which the compiler
    // should balk at.
//  static const char CATEGORY_DEPTH_CHECK[(CATEGORY_DEPTH == 4)?1:0] = {' '};  // unused

    // actually initialize the object.
    mData[0] = 0;
    mData[1] = 0;
    mData[2] = 0;
    mData[3] = 0;
}

void LLCategory::init(U32 value)
{
    U8 v;
    for(S32 i = 0; i < CATEGORY_DEPTH; i++)
    {
        v = (U8)((0x000000ff) & value);
        mData[CATEGORY_DEPTH - 1 - i] = v;
        value >>= 8;
    }
}

U32 LLCategory::getU32() const
{
    U32 rv = 0;
    rv |= mData[0];
    rv <<= 8;
    rv |= mData[1];
    rv <<= 8;
    rv |= mData[2];
    rv <<= 8;
    rv |= mData[3];
    return rv;
}

S32 LLCategory::getSubCategoryCount() const
{
    if (mData[0] + 1 >= (S32)LL_ARRAY_SIZE(CATEGORY_INDEX))
    {
        // unknown top level category
        return 0;
    }
    S32 rv = CATEGORY_INDEX[mData[0] + 1] - CATEGORY_INDEX[mData[0]];
    return rv;
}

// This method will return a category that is the nth subcategory. If
// you're already at the bottom of the hierarchy, then the method will
// return a copy of this.
LLCategory LLCategory::getSubCategory(U8 n) const
{
    LLCategory rv(*this);
    for(S32 i = 0; i < (CATEGORY_DEPTH - 1); i++)
    {
        if(rv.mData[i] == 0)
        {
            rv.mData[i] = n + 1;
            break;
        }
    }
    return rv;
}

// This method will return the name of the leaf category type
const char* LLCategory::lookupName() const
{
    S32 i = 0;
    S32 index = mData[i++];
    if (index >= (S32)LL_ARRAY_SIZE(CATEGORY_NAME))
    {
        // unknown top level category: the table's terminating NULL, which
        // the first unknown value (7) already gets
        return CATEGORY_NAME[LL_ARRAY_SIZE(CATEGORY_NAME) - 1];
    }
    while((i < CATEGORY_DEPTH) && (mData[i] != 0))
    {
        index = CATEGORY_INDEX[index];
        ++i;
    }
    return CATEGORY_NAME[index];
}

// message serialization
void LLCategory::packMessage(LLMessageSystem* msg) const
{
    U32 data = getU32();
    msg->addU32Fast(_PREHASH_Category, data);
}

// message serialization
void LLCategory::unpackMessage(LLMessageSystem* msg, const char* block)
{
    U32 data;
    msg->getU32Fast(block, _PREHASH_Category, data);
    init(data);
}

// message serialization
void LLCategory::unpackMultiMessage(LLMessageSystem* msg, const char* block,
                                    S32 block_num)
{
    U32 data;
    msg->getU32Fast(block, _PREHASH_Category, data, block_num);
    init(data);
}

///----------------------------------------------------------------------------
/// Local function definitions
///----------------------------------------------------------------------------
// ---- VKS-ORACLE-COPY END

// ---- VKS-ORACLE-COPY BEGIN
// source: indra/llinventory/llinventorysettings.cpp
// lines: 27-120
// blob: 2562614480d354638bca6c3050d1e07c9924d7ca
// copied: the whole file after its licence block
// substitutions: #include lines and forward declarations of LLMessageSystem and LLUUID removed
// conditionals: 
// ----



//=========================================================================
struct SettingsEntry : public LLDictionaryEntry
{
    SettingsEntry(const std::string &name,
        const std::string& default_new_name,
        LLInventoryType::EIconName iconName) :
        LLDictionaryEntry(name),
        mDefaultNewName(default_new_name),
        mLabel(name),
        mIconName(iconName)
    {
        std::string transdname = LLSettingsType::getInstance()->mTranslator->getString(mLabel);
        if (!transdname.empty())
        {
            mLabel = transdname;
        }

        // <FS:Ansariel> Name of newly created setting is not translated
        transdname = LLSettingsType::getInstance()->mTranslator->getString(mDefaultNewName);
        if (!transdname.empty())
        {
            mDefaultNewName = transdname;
        }
        // </FS:Ansariel>
}

    std::string mLabel;
    std::string mDefaultNewName; //keep mLabel for backward compatibility
    LLInventoryType::EIconName mIconName;
};

class LLSettingsDictionary : public LLSingleton<LLSettingsDictionary>,
    public LLDictionary<LLSettingsType::type_e, SettingsEntry>
{
    LLSINGLETON(LLSettingsDictionary);

    void initSingleton() override;
};

LLSettingsDictionary::LLSettingsDictionary() 
{
}

void LLSettingsDictionary::initSingleton()
{
    addEntry(LLSettingsType::ST_SKY,        new SettingsEntry("sky",        "New Sky",      LLInventoryType::ICONNAME_SETTINGS_SKY));
    addEntry(LLSettingsType::ST_WATER,      new SettingsEntry("water",      "New Water",    LLInventoryType::ICONNAME_SETTINGS_WATER));
    addEntry(LLSettingsType::ST_DAYCYCLE,   new SettingsEntry("day",        "New Day",      LLInventoryType::ICONNAME_SETTINGS_DAY));
    addEntry(LLSettingsType::ST_NONE,       new SettingsEntry("none",       "New Settings", LLInventoryType::ICONNAME_SETTINGS));
    addEntry(LLSettingsType::ST_INVALID,    new SettingsEntry("invalid",    "New Settings", LLInventoryType::ICONNAME_SETTINGS));
}

//=========================================================================

LLSettingsType::LLSettingsType(LLTranslationBridge::ptr_t &trans)
{
    mTranslator = trans;
}

LLSettingsType::~LLSettingsType()
{
    mTranslator.reset();
}

LLSettingsType::type_e LLSettingsType::fromInventoryFlags(U32 flags)
{
    return  (LLSettingsType::type_e)(flags & LLInventoryItemFlags::II_FLAGS_SUBTYPE_MASK);
}

LLInventoryType::EIconName LLSettingsType::getIconName(LLSettingsType::type_e type)
{
    const SettingsEntry *entry = LLSettingsDictionary::instance().lookup(type);
    if (!entry) 
        return getIconName(ST_INVALID);
    return entry->mIconName;
}

std::string LLSettingsType::getDefaultName(LLSettingsType::type_e type)
{
    const SettingsEntry *entry = LLSettingsDictionary::instance().lookup(type);
    if (!entry)
        return getDefaultName(ST_INVALID);
    return entry->mDefaultNewName;
}
// ---- VKS-ORACLE-COPY END

// ---- VKS-ORACLE-COPY BEGIN
// source: indra/llinventory/lltransactionflags.cpp
// lines: 27-179
// blob: 0fe7785f1a04a3275144efe6125b75dfaf29d297
// copied: the whole file after its licence block
// substitutions: #include lines and forward declarations of LLMessageSystem and LLUUID removed
// conditionals: 
// ----



const U8 TRANSACTION_FLAGS_NONE = 0;
const U8 TRANSACTION_FLAG_SOURCE_GROUP = 1;
const U8 TRANSACTION_FLAG_DEST_GROUP = 2;
const U8 TRANSACTION_FLAG_OWNER_GROUP = 4;
const U8 TRANSACTION_FLAG_SIMULTANEOUS_CONTRIBUTION = 8;
const U8 TRANSACTION_FLAG_SIMULTANEOUS_CONTRIBUTION_REMOVAL = 16;

U8 pack_transaction_flags(bool is_source_group, bool is_dest_group)
{
    U8 rv = 0;
    if(is_source_group) rv |= TRANSACTION_FLAG_SOURCE_GROUP;
    if(is_dest_group) rv |= TRANSACTION_FLAG_DEST_GROUP;
    return rv;
}

bool is_tf_source_group(TransactionFlags flags)
{
    return ((flags & TRANSACTION_FLAG_SOURCE_GROUP) == TRANSACTION_FLAG_SOURCE_GROUP);
}

bool is_tf_dest_group(TransactionFlags flags)
{
    return ((flags & TRANSACTION_FLAG_DEST_GROUP) == TRANSACTION_FLAG_DEST_GROUP);
}

bool is_tf_owner_group(TransactionFlags flags)
{
    return ((flags & TRANSACTION_FLAG_OWNER_GROUP) == TRANSACTION_FLAG_OWNER_GROUP);
}

void append_reason(std::ostream& ostr, S32 transaction_type, const std::string& description);   // <FS:CR>
void append_reason(
    std::ostream& ostr,
    S32 transaction_type,
    const std::string& description)
{
    switch( transaction_type )
    {
    case TRANS_OBJECT_SALE:
      ostr << " for " << (description.length() > 0 ? description : std::string("<unknown>"));
        break;
    case TRANS_LAND_SALE:
        ostr << " for a parcel of land";
        break;
    case TRANS_LAND_PASS_SALE:
        ostr << " for a land access pass";
        break;
    case TRANS_GROUP_LAND_DEED:
        ostr << " for deeding land";
        break;
    default:
        break;
    }
}

std::string build_transfer_message_to_source(
    S32 amount,
    const LLUUID& source_id,
    const LLUUID& dest_id,
    const std::string& dest_name,
    S32 transaction_type,
    const std::string& description)
{
    LL_DEBUGS() << "build_transfer_message_to_source: " << amount << " "
        << source_id << " " << dest_id << " " << dest_name << " "
        << transaction_type << " "
        << (description.empty() ? "(no desc)" : description)
        << LL_ENDL;
    if(source_id.isNull())
    {
        return description;
    }
    if((0 == amount) && description.empty())
    {
        return description;
    }
    std::ostringstream ostr;
    if(dest_id.isNull())
    {
        // *NOTE: Do not change these strings!  The viewer matches
        // them in llviewermessage.cpp to perform localization.
        // If you need to make changes, add a new, localizable message. JC
// <FS:AW opensim currency support>
//      ostr << "You paid L$" << amount;
        ostr << Tea::wrapCurrency("You paid L$") << amount;
// </FS:AW opensim currency support>
        switch(transaction_type)
        {
        case TRANS_GROUP_CREATE:
            ostr << " to create a group";
            break;
        case TRANS_GROUP_JOIN:
            ostr << " to join a group";
            break;
        case TRANS_UPLOAD_CHARGE:
            ostr << " to upload";
            break;
        default:
            break;
        }
    }
    else
    {
// <FS:AW opensim currency support>
//      ostr << "You paid " << dest_name << " L$" << amount;
        ostr << "You paid " << dest_name << Tea::wrapCurrency(" L$") << amount;
// </FS:AW opensim currency support>
        append_reason(ostr, transaction_type, description);
    }
    ostr << ".";
    return ostr.str();
}

std::string build_transfer_message_to_destination(
    S32 amount,
    const LLUUID& dest_id,
    const LLUUID& source_id,
    const std::string& source_name,
    S32 transaction_type,
    const std::string& description)
{
    LL_DEBUGS() << "build_transfer_message_to_dest: " << amount << " "
        << dest_id << " " << source_id << " " << source_name << " "
        << transaction_type << " " << (description.empty() ? "(no desc)" : description)
        << LL_ENDL;
    if(0 == amount)
    {
        return std::string();
    }
    if(dest_id.isNull())
    {
        return description;
    }
    std::ostringstream ostr;
    // *NOTE: Do not change these strings!  The viewer matches
    // them in llviewermessage.cpp to perform localization.
    // If you need to make changes, add a new, localizable message. JC
// <FS:AW opensim currency support>
//  ostr << source_name << " paid you L$" << amount;
    ostr << source_name << Tea::wrapCurrency(" paid you L$") << amount;
// </FS:AW opensim currency support>
    append_reason(ostr, transaction_type, description);
    ostr << ".";
    return ostr.str();
}
// ---- VKS-ORACLE-COPY END

// ---- VKS-ORACLE-COPY BEGIN
// source: indra/llinventory/llnotecard.cpp
// lines: 26-289
// blob: d73e9c8966f6c802495f7cbcb3f23e5aadbbcae9
// copied: the whole file after its licence block
// substitutions: #include lines and forward declarations of LLMessageSystem and LLUUID removed
// conditionals: 
// ----


LLNotecard::LLNotecard(S32 max_text)
    : mMaxText(max_text),
      mVersion(0),
      mEmbeddedVersion(0)
{
}

LLNotecard::~LLNotecard()
{
}

bool LLNotecard::importEmbeddedItemsStream(std::istream& str)
{
    // Version 1 format:
    //      LLEmbeddedItems version 1
    //      {
    //          count <number of entries being used and not deleted>
    //          {
    //              ext char index <index>
    //              <InventoryItem chunk>
    //          }
    //      }

    S32 i;
    S32 count = 0;

    str >> std::ws >> "LLEmbeddedItems version" >> mEmbeddedVersion >> "\n";
    if (str.fail())
    {
        LL_WARNS() << "Invalid Linden text file header" << LL_ENDL;
        goto import_file_failed;
    }

    if( 1 != mEmbeddedVersion )
    {
        LL_WARNS() << "Invalid LLEmbeddedItems version: " << mEmbeddedVersion << LL_ENDL;
        goto import_file_failed;
    }

    str >> std::ws >> "{\n";
    if(str.fail())
    {
        LL_WARNS() << "Invalid Linden text file format: missing {" << LL_ENDL;
        goto import_file_failed;
    }

    str >> std::ws >> "count " >> count >> "\n";
    if(str.fail())
    {
        LL_WARNS() << "Invalid LLEmbeddedItems count" << LL_ENDL;
        goto import_file_failed;
    }

    if((count < 0))
    {
        LL_WARNS() << "Invalid LLEmbeddedItems count value: " << count << LL_ENDL;
        goto import_file_failed;
    }

    for(i = 0; i < count; i++)
    {
        str >> std::ws >> "{\n";
        if(str.fail())
        {
            LL_WARNS() << "Invalid LLEmbeddedItems file format: missing {" << LL_ENDL;
            goto import_file_failed;
        }

        U32 index = 0;
        str >> std::ws >> "ext char index " >> index >> "\n";
        if(str.fail())
        {
            LL_WARNS() << "Invalid LLEmbeddedItems file format: missing ext char index" << LL_ENDL;
            goto import_file_failed;
        }

        str >> std::ws >> "inv_item\t0\n";
        if(str.fail())
        {
            LL_WARNS() << "Invalid LLEmbeddedItems file format: missing inv_item" << LL_ENDL;
            goto import_file_failed;
        }

        LLPointer<LLInventoryItem> item = new LLInventoryItem;
        if (!item->importLegacyStream(str))
        {
            LL_INFOS() << "notecard import failed" << LL_ENDL;
            goto import_file_failed;
        }
        mItems.push_back(item);

        str >> std::ws >> "}\n";
        if(str.fail())
        {
            LL_WARNS() << "Invalid LLEmbeddedItems file format: missing }" << LL_ENDL;
            goto import_file_failed;
        }
    }

    str >> std::ws >> "}\n";
    if(str.fail())
    {
        LL_WARNS() << "Invalid LLEmbeddedItems file format: missing }" << LL_ENDL;
        goto import_file_failed;
    }

    return true;

    import_file_failed:
    return false;
}

bool LLNotecard::importStream(std::istream& str)
{
    // Version 1 format:
    //      Linden text version 1
    //      {
    //          <EmbeddedItemList chunk>
    //          Text length
    //          <ASCII text; 0x80 | index = embedded item>
    //      }

    // Version 2 format: (NOTE: Imports identically to version 1)
    //      Linden text version 2
    //      {
    //          <EmbeddedItemList chunk>
    //          Text length
    //          <UTF8 text; FIRST_EMBEDDED_CHAR + index = embedded item>
    //      }

    str >> std::ws >> "Linden text version " >> mVersion >> "\n";
    if(str.fail())
    {
        LL_WARNS() << "Invalid Linden text file header " << LL_ENDL;
        return false;
    }

    if( 1 != mVersion && 2 != mVersion)
    {
        LL_WARNS() << "Invalid Linden text file version: " << mVersion << LL_ENDL;
        return false;
    }

    str >> std::ws >> "{\n";
    if(str.fail())
    {
        LL_WARNS() << "Invalid Linden text file format" << LL_ENDL;
        return false;
    }

    if(!importEmbeddedItemsStream(str))
    {
        return false;
    }

    char line_buf[STD_STRING_BUF_SIZE]; /* Flawfinder: ignore */
    str.getline(line_buf, STD_STRING_BUF_SIZE);
    if(str.fail())
    {
        LL_WARNS() << "Invalid Linden text length field" << LL_ENDL;
        return false;
    }
    line_buf[STD_STRING_STR_LEN] = '\0';

    S32 text_len = 0;
    if( 1 != sscanf(line_buf, "Text length %d", &text_len) )
    {
        LL_WARNS() << "Invalid Linden text length field" << LL_ENDL;
        return false;
    }

    if(text_len > mMaxText || text_len < 0)
    {
        LL_WARNS() << "Invalid Linden text length: " << text_len << LL_ENDL;
        return false;
    }

    bool success = true;

    char* text = new char[text_len + 1];
    fullread(str, text, text_len);
    if(str.fail())
    {
        LL_WARNS() << "Invalid Linden text: text shorter than text length: " << text_len << LL_ENDL;
        success = false;
    }
    text[text_len] = '\0';

    if(success)
    {
        // Actually set the text
        mText = std::string(text);
    }

    delete[] text;

    return success;
}

////////////////////////////////////////////////////////////////////////////

bool LLNotecard::exportEmbeddedItemsStream( std::ostream& out_stream )
{
    out_stream << "LLEmbeddedItems version 1\n";
    out_stream << "{\n";

    out_stream << llformat("count %d\n", mItems.size() );

    S32 idx = 0;
    for (std::vector<LLPointer<LLInventoryItem> >::iterator iter = mItems.begin();
         iter != mItems.end(); ++iter)
    {
        LLInventoryItem* item = *iter;
        if (item)
        {
            out_stream << "{\n";
            out_stream << llformat("ext char index %d\n", idx  );
            if( !item->exportLegacyStream( out_stream ) )
            {
                return false;
            }
            out_stream << "}\n";
        }
        ++idx;
    }

    out_stream << "}\n";

    return true;
}

bool LLNotecard::exportStream( std::ostream& out_stream )
{
    out_stream << "Linden text version 2\n";
    out_stream << "{\n";

    if( !exportEmbeddedItemsStream( out_stream ) )
    {
        return false;
    }

    out_stream << llformat("Text length %d\n", mText.length() );
    out_stream << mText;
    out_stream << "}\n";

    return true;
}

////////////////////////////////////////////////////////////////////////////

void LLNotecard::setItems(const std::vector<LLPointer<LLInventoryItem> >& items)
{
    mItems = items;
}

void LLNotecard::setText(const std::string& text)
{
    mText = text;
}
// ---- VKS-ORACLE-COPY END

// ---- VKS-ORACLE-COPY BEGIN
// source: indra/newview/llviewerinventory.cpp
// lines: 91-92
// blob: 5c3cd2e97772cb2f6bc2c9bcfdc6d53021131335
// copied: the cache labels INV_OWNER_ID and INV_VERSION (over the frozen base classes)
// substitutions: #include lines and forward declarations of LLMessageSystem and LLUUID removed
// conditionals: 
// ----
static const std::string INV_OWNER_ID("owner_id");
static const std::string INV_VERSION("version");
// ---- VKS-ORACLE-COPY END

// ---- VKS-ORACLE-COPY BEGIN
// source: indra/newview/llviewerinventory.cpp
// lines: 576-593
// blob: 5c3cd2e97772cb2f6bc2c9bcfdc6d53021131335
// copied: LLViewerInventoryItem::packMessage (over the frozen base classes)
// substitutions: #include lines and forward declarations of LLMessageSystem and LLUUID removed
// conditionals: 
// ----
void LLViewerInventoryItem::packMessage(LLMessageSystem* msg) const
{
    msg->addUUIDFast(_PREHASH_ItemID, mUUID);
    msg->addUUIDFast(_PREHASH_FolderID, mParentUUID);
    mPermissions.packMessage(msg);
    msg->addUUIDFast(_PREHASH_TransactionID, mTransactionID);
    S8 type = static_cast<S8>(mType);
    msg->addS8Fast(_PREHASH_Type, type);
    type = static_cast<S8>(mInventoryType);
    msg->addS8Fast(_PREHASH_InvType, type);
    msg->addU32Fast(_PREHASH_Flags, mFlags);
    mSaleInfo.packMessage(msg);
    msg->addStringFast(_PREHASH_Name, mName);
    msg->addStringFast(_PREHASH_Description, mDescription);
    msg->addS32Fast(_PREHASH_CreationDate, (S32)mCreationDate);
    U32 crc = getCRC32();
    msg->addU32Fast(_PREHASH_CRC, crc);
}
// ---- VKS-ORACLE-COPY END

// ---- VKS-ORACLE-COPY BEGIN
// source: indra/newview/llviewerinventory.cpp
// lines: 2647-2650
// blob: 5c3cd2e97772cb2f6bc2c9bcfdc6d53021131335
// copied: LLViewerInventoryItem::getCRC32 (over the frozen base classes)
// substitutions: #include lines and forward declarations of LLMessageSystem and LLUUID removed
// conditionals: 
// ----
U32 LLViewerInventoryItem::getCRC32() const
{
    return LLInventoryItem::getCRC32();
}
// ---- VKS-ORACLE-COPY END

// ---- VKS-ORACLE-COPY BEGIN
// source: indra/newview/llviewerinventory.cpp
// lines: 675-682
// blob: 5c3cd2e97772cb2f6bc2c9bcfdc6d53021131335
// copied: LLViewerInventoryCategory::packMessage (over the frozen base classes)
// substitutions: #include lines and forward declarations of LLMessageSystem and LLUUID removed
// conditionals: 
// ----
void LLViewerInventoryCategory::packMessage(LLMessageSystem* msg) const
{
    msg->addUUIDFast(_PREHASH_FolderID, mUUID);
    msg->addUUIDFast(_PREHASH_ParentID, mParentUUID);
    S8 type = static_cast<S8>(mPreferredType);
    msg->addS8Fast(_PREHASH_Type, type);
    msg->addStringFast(_PREHASH_Name, mName);
}
// ---- VKS-ORACLE-COPY END

// ---- VKS-ORACLE-COPY BEGIN
// source: indra/newview/llviewerinventory.cpp
// lines: 740-743
// blob: 5c3cd2e97772cb2f6bc2c9bcfdc6d53021131335
// copied: LLViewerInventoryCategory::setVersion (over the frozen base classes)
// substitutions: #include lines and forward declarations of LLMessageSystem and LLUUID removed
// conditionals: 
// ----
void LLViewerInventoryCategory::setVersion(S32 version)
{
    mVersion = version;
}
// ---- VKS-ORACLE-COPY END

// ---- VKS-ORACLE-COPY BEGIN
// source: indra/newview/llviewerinventory.cpp
// lines: 853-877
// blob: 5c3cd2e97772cb2f6bc2c9bcfdc6d53021131335
// copied: LLViewerInventoryCategory::exportLLSD and importLLSD (over the frozen base classes)
// substitutions: #include lines and forward declarations of LLMessageSystem and LLUUID removed
// conditionals: 
// ----
void LLViewerInventoryCategory::exportLLSD(LLSD & cat_data) const
{
    LLInventoryCategory::exportLLSD(cat_data);
    cat_data[INV_OWNER_ID] = mOwnerID;
    cat_data[INV_VERSION] = mVersion;
}

bool LLViewerInventoryCategory::importLLSD(const std::string& label, const LLSD& value)
{
    if (LLInventoryCategory::importLLSD(label, value))
    {
        return true;
    }
    else if (label == INV_OWNER_ID)
    {
        mOwnerID = value.asUUID();
        return true;
    }
    else if (label == INV_VERSION)
    {
        setVersion(value.asInteger());
        return true;
    }
    return false;
}
// ---- VKS-ORACLE-COPY END

} // namespace llinventory_oracle

namespace llinventory_live_shim
{

// ---- VKS-ORACLE-COPY BEGIN
// source: indra/newview/llviewerinventory.cpp
// lines: 91-92
// blob: 5c3cd2e97772cb2f6bc2c9bcfdc6d53021131335
// copied: the cache labels INV_OWNER_ID and INV_VERSION (over the live base classes)
// substitutions: #include lines and forward declarations of LLMessageSystem and LLUUID removed
// conditionals: 
// ----
static const std::string INV_OWNER_ID("owner_id");
static const std::string INV_VERSION("version");
// ---- VKS-ORACLE-COPY END

// ---- VKS-ORACLE-COPY BEGIN
// source: indra/newview/llviewerinventory.cpp
// lines: 576-593
// blob: 5c3cd2e97772cb2f6bc2c9bcfdc6d53021131335
// copied: LLViewerInventoryItem::packMessage (over the live base classes)
// substitutions: #include lines and forward declarations of LLMessageSystem and LLUUID removed
// conditionals: 
// ----
void LLViewerInventoryItem::packMessage(LLMessageSystem* msg) const
{
    msg->addUUIDFast(_PREHASH_ItemID, mUUID);
    msg->addUUIDFast(_PREHASH_FolderID, mParentUUID);
    mPermissions.packMessage(msg);
    msg->addUUIDFast(_PREHASH_TransactionID, mTransactionID);
    S8 type = static_cast<S8>(mType);
    msg->addS8Fast(_PREHASH_Type, type);
    type = static_cast<S8>(mInventoryType);
    msg->addS8Fast(_PREHASH_InvType, type);
    msg->addU32Fast(_PREHASH_Flags, mFlags);
    mSaleInfo.packMessage(msg);
    msg->addStringFast(_PREHASH_Name, mName);
    msg->addStringFast(_PREHASH_Description, mDescription);
    msg->addS32Fast(_PREHASH_CreationDate, (S32)mCreationDate);
    U32 crc = getCRC32();
    msg->addU32Fast(_PREHASH_CRC, crc);
}
// ---- VKS-ORACLE-COPY END

// ---- VKS-ORACLE-COPY BEGIN
// source: indra/newview/llviewerinventory.cpp
// lines: 2647-2650
// blob: 5c3cd2e97772cb2f6bc2c9bcfdc6d53021131335
// copied: LLViewerInventoryItem::getCRC32 (over the live base classes)
// substitutions: #include lines and forward declarations of LLMessageSystem and LLUUID removed
// conditionals: 
// ----
U32 LLViewerInventoryItem::getCRC32() const
{
    return LLInventoryItem::getCRC32();
}
// ---- VKS-ORACLE-COPY END

// ---- VKS-ORACLE-COPY BEGIN
// source: indra/newview/llviewerinventory.cpp
// lines: 675-682
// blob: 5c3cd2e97772cb2f6bc2c9bcfdc6d53021131335
// copied: LLViewerInventoryCategory::packMessage (over the live base classes)
// substitutions: #include lines and forward declarations of LLMessageSystem and LLUUID removed
// conditionals: 
// ----
void LLViewerInventoryCategory::packMessage(LLMessageSystem* msg) const
{
    msg->addUUIDFast(_PREHASH_FolderID, mUUID);
    msg->addUUIDFast(_PREHASH_ParentID, mParentUUID);
    S8 type = static_cast<S8>(mPreferredType);
    msg->addS8Fast(_PREHASH_Type, type);
    msg->addStringFast(_PREHASH_Name, mName);
}
// ---- VKS-ORACLE-COPY END

// ---- VKS-ORACLE-COPY BEGIN
// source: indra/newview/llviewerinventory.cpp
// lines: 740-743
// blob: 5c3cd2e97772cb2f6bc2c9bcfdc6d53021131335
// copied: LLViewerInventoryCategory::setVersion (over the live base classes)
// substitutions: #include lines and forward declarations of LLMessageSystem and LLUUID removed
// conditionals: 
// ----
void LLViewerInventoryCategory::setVersion(S32 version)
{
    mVersion = version;
}
// ---- VKS-ORACLE-COPY END

// ---- VKS-ORACLE-COPY BEGIN
// source: indra/newview/llviewerinventory.cpp
// lines: 853-877
// blob: 5c3cd2e97772cb2f6bc2c9bcfdc6d53021131335
// copied: LLViewerInventoryCategory::exportLLSD and importLLSD (over the live base classes)
// substitutions: #include lines and forward declarations of LLMessageSystem and LLUUID removed
// conditionals: 
// ----
void LLViewerInventoryCategory::exportLLSD(LLSD & cat_data) const
{
    LLInventoryCategory::exportLLSD(cat_data);
    cat_data[INV_OWNER_ID] = mOwnerID;
    cat_data[INV_VERSION] = mVersion;
}

bool LLViewerInventoryCategory::importLLSD(const std::string& label, const LLSD& value)
{
    if (LLInventoryCategory::importLLSD(label, value))
    {
        return true;
    }
    else if (label == INV_OWNER_ID)
    {
        mOwnerID = value.asUUID();
        return true;
    }
    else if (label == INV_VERSION)
    {
        setVersion(value.asInteger());
        return true;
    }
    return false;
}
// ---- VKS-ORACLE-COPY END

} // namespace llinventory_live_shim
