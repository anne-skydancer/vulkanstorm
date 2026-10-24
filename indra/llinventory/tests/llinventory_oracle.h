/**
 * @file llinventory_oracle.h
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

#ifndef VKS_LLINVENTORY_ORACLE_H
#define VKS_LLINVENTORY_ORACLE_H

// Dependencies of the copied headers, included outside the namespace so that
// the copies use the live LLUUID, LLSD, LLStringUtil, singletons and message system.
#include <cstdarg>
#include <iosfwd>
#include <list>
#include <string>
#include <string_view>
#include <vector>
#include "llpointer.h"
#include "llrefcount.h"
#include "llsd.h"
#include "lluuid.h"
#include "lltrace.h"
#include "llxmlnode.h"
#include "llsingleton.h"
#include "lldictionary.h"
#include "llinvtranslationbrdg.h"

class LLMessageSystem;

namespace llinventory_oracle
{

// ---- VKS-ORACLE-COPY BEGIN
// source: indra/llinventory/llpermissionsflags.h
// lines: 28-96
// blob: 6b785880faefb0f938aed81539b6e0dea6336f79
// copied: the whole header between its include guard
// substitutions: #include lines and forward declarations of LLMessageSystem and LLUUID removed
// conditionals: #ifndef LL_LLPERMISSIONSFLAGS_H
// ----

// Flags for various permissions bits.
// Shared between viewer and simulator.

// permission bits
typedef U32 PermissionMask;
typedef U32 PermissionBit;


// Do you have permission to transfer ownership of the object or
// item. Fair use rules dictate that if you cannot copy, you can
// always transfer.
constexpr PermissionBit PERM_TRANSFER           = (1 << 13); // 0x00002000

// objects, scale or change textures
// parcels, allow building on it
constexpr PermissionBit PERM_MODIFY             = (1 << 14); // 0x00004000

// objects, allow copy
constexpr PermissionBit PERM_COPY               = (1 << 15); // 0x00008000

// <FS:CR> OpenSim export permission
const PermissionBit PERM_EXPORT             = (1 << 16); // 0x00010000
// </FS:CR>

// parcels, allow entry, deprecated
//constexpr PermissionBit PERM_ENTER            = (1 << 16); // 0x00010000

// parcels, allow terraform, deprecated
//constexpr PermissionBit PERM_TERRAFORM        = (1 << 17); // 0x00020000

// NOTA BENE: This flag is NO LONGER USED!!! However, it is possible that some
// objects in the universe have it set so DON"T USE IT going forward.
//constexpr PermissionBit PERM_OWNER_DEBIT      = (1 << 18); // 0x00040000

// objects, can grab/translate/rotate
constexpr PermissionBit PERM_MOVE               = (1 << 19); // 0x00080000

// parcels, avatars take damage, deprecated
//const PermissionBit   PERM_DAMAGE         = (1 << 20); // 0x00100000

// don't use bit 31 -- printf/scanf with "%x" assume signed numbers
constexpr PermissionBit PERM_RESERVED           = ((U32)1) << 31;

constexpr PermissionMask PERM_NONE              = 0x00000000;
constexpr PermissionMask PERM_ALL               = 0x7FFFFFFF;
//constexpr PermissionMask PERM_ALL_PARCEL      = PERM_MODIFY | PERM_ENTER | PERM_TERRAFORM | PERM_DAMAGE;
constexpr PermissionMask PERM_ITEM_UNRESTRICTED =  PERM_MODIFY | PERM_COPY | PERM_TRANSFER;


// Useful stuff for transmission.
// Which permissions field are we trying to change?
constexpr U8 PERM_BASE      = 0x01;
// TODO: Add another PERM_OWNER operation type for allowOperationBy  DK 04/03/06
constexpr U8 PERM_OWNER     = 0x02;
constexpr U8 PERM_GROUP     = 0x04;
constexpr U8 PERM_EVERYONE  = 0x08;
constexpr U8 PERM_NEXT_OWNER = 0x10;

// This is just a quickie debugging key
// no modify: PERM_ALL & ~PERM_MODIFY                  = 0x7fffbfff
// no copy:   PERM_ALL & ~PERM_COPY                    = 0x7fff7fff
// no modify or copy:                                  = 0x7fff3fff
// no transfer: PERM_ALL & ~PERM_TRANSFER              = 0x7fffdfff
// no modify, no transfer                              = 0x7fff9fff
// no copy, no transfer (INVALID!)                     = 0x7fff5fff
// no modify, no copy, no transfer (INVALID!)          = 0x7fff1fff


// ---- VKS-ORACLE-COPY END

// ---- VKS-ORACLE-COPY BEGIN
// source: indra/llcommon/llassettype.h
// lines: 29-176
// blob: 17177d81c3cc86bce739f689754e40e3f953f89f
// copied: the whole header between its include guard
// substitutions: #include lines and forward declarations of LLMessageSystem and LLUUID removed
// conditionals: #ifndef LL_LLASSETTYPE_H
// ----


class LL_COMMON_API LLAssetType
{
public:
    enum EType
    {
        AT_TEXTURE = 0,
            // Used for painting the faces of geometry.
            // Stored in typical j2c stream format.

        AT_SOUND = 1,
            // Used to fill the aural spectrum.

        AT_CALLINGCARD = 2,
            // Links instant message access to the user on the card.
            // : E.G. A card for yourself, for linden support, for
            // : the guy you were talking to in the coliseum.

        AT_LANDMARK = 3,
            // Links to places in the world with location and a screen shot or image saved.
            // : E.G. Home, linden headquarters, the coliseum, destinations where
            // : we want to increase traffic.

        AT_SCRIPT = 4,
            // Valid scripts that can be attached to an object.
            // : E.G. Open a door, jump into the air.

        AT_CLOTHING = 5,
            // A collection of textures and parameters that can be worn by an avatar.

        AT_OBJECT = 6,
            // Any combination of textures, sounds, and scripts that are
            // associated with a fixed piece of geometry.
            // : E.G. A hot tub, a house with working door.

        AT_NOTECARD = 7,
            // Just text.

        AT_CATEGORY = 8,
            // Holds a collection of inventory items.
            // It's treated as an item in the inventory and therefore needs a type.

        AT_LSL_TEXT = 10,
        AT_LSL_BYTECODE = 11,
            // The LSL is the scripting language.
            // We've split it into a text and bytecode representation.

        AT_TEXTURE_TGA = 12,
            // Uncompressed TGA texture.

        AT_BODYPART = 13,
            // A collection of textures and parameters that can be worn by an avatar.

        AT_SOUND_WAV = 17,
            // Uncompressed sound.

        AT_IMAGE_TGA = 18,
            // Uncompressed image, non-square.
            // Not appropriate for use as a texture.

        AT_IMAGE_JPEG = 19,
            // Compressed image, non-square.
            // Not appropriate for use as a texture.

        AT_ANIMATION = 20,
            // Animation.

        AT_GESTURE = 21,
            // Gesture, sequence of animations, sounds, chat, wait steps.

        AT_SIMSTATE = 22,
            // Simstate file.

        AT_LINK = 24,
            // Inventory symbolic link

        AT_LINK_FOLDER = 25,
            // Inventory folder link

        AT_MARKETPLACE_FOLDER = 26,
            // Marketplace folder. Same as an AT_CATEGORY but different display methods.

        AT_WIDGET = 40,
            // UI Widget: this is *not* an inventory asset type, only a viewer side asset (e.g. button, other ui items...)

        AT_PERSON = 45,
            // A user uuid  which is not an inventory asset type, used in viewer only for adding a person to a chat via drag and drop.

        AT_MESH = 49,
        // Mesh data in our proprietary SLM format

        AT_RESERVED_1 = 50,
        AT_RESERVED_2 = 51,
        AT_RESERVED_3 = 52,
        AT_RESERVED_4 = 53,
        AT_RESERVED_5 = 54,
        AT_RESERVED_6 = 55,

        AT_SETTINGS = 56,   // Collection of settings
        AT_MATERIAL = 57,   // Render Material
        AT_GLTF = 58,   // gltf json document
        AT_GLTF_BIN = 59, // gltf binary data

        AT_COUNT = 60,

            // +*********************************************************+
            // |  TO ADD AN ELEMENT TO THIS ENUM:                        |
            // +*********************************************************+
            // | 1. INSERT BEFORE AT_COUNT                               |
            // | 2. INCREMENT AT_COUNT BY 1                              |
            // | 3. ADD TO LLAssetType.cpp                               |
            // | 4. ADD TO LLViewerAssetType.cpp                         |
            // | 5. ADD TO DEFAULT_ASSET_FOR_INV in LLInventoryType.cpp  |
            // +*********************************************************+
        AT_UNKNOWN = 255,
        AT_NONE = -1
    };

    // machine transation between type and strings
    static EType                lookup(const char* name); // safe conversion to std::string, *TODO: deprecate
    static EType                lookup(const std::string& type_name);
    static const char*          lookup(EType asset_type);

    // translation from a type to a human readable form.
    static EType                lookupHumanReadable(const char* desc_name); // safe conversion to std::string, *TODO: deprecate
    static EType                lookupHumanReadable(const std::string& readable_name);
    static const char*          lookupHumanReadable(EType asset_type);

    static EType                getType(const std::string& desc_name);
    static const std::string&   getDesc(EType asset_type);

    static bool                 lookupCanLink(EType asset_type);
    static bool                 lookupIsLinkType(EType asset_type);

    static bool                 lookupIsAssetFetchByIDAllowed(EType asset_type); // the asset allows direct download
    static bool                 lookupIsAssetIDKnowable(EType asset_type); // asset data can be known by the viewer

    static LLSD getTypeNames();

    static const std::string    BADLOOKUP;

protected:
    LLAssetType() {}
    ~LLAssetType() {}
};

// ---- VKS-ORACLE-COPY END

// ---- VKS-ORACLE-COPY BEGIN
// source: indra/llinventory/llinventorytype.h
// lines: 29-162
// blob: e110c1dbe71d1298538bf78f6b468b7288730dd0
// copied: the whole header between its include guard
// substitutions: #include lines and forward declarations of LLMessageSystem and LLUUID removed
// conditionals: #ifndef LLINVENTORYTYPE_H
// ----


//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Class LLInventoryType
//
// Class used to encapsulate operations around inventory type.
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

class LLInventoryType
{
public:
    enum EType
    {
        IT_TEXTURE = 0,
        IT_SOUND = 1,
        IT_CALLINGCARD = 2,
        IT_LANDMARK = 3,
        //IT_SCRIPT = 4,
        //IT_CLOTHING = 5,
        IT_OBJECT = 6,
        IT_NOTECARD = 7,
        IT_CATEGORY = 8,
        IT_ROOT_CATEGORY = 9,
        IT_LSL = 10,
        //IT_LSL_BYTECODE = 11,
        //IT_TEXTURE_TGA = 12,
        //IT_BODYPART = 13,
        //IT_TRASH = 14,
        IT_SNAPSHOT = 15,
        //IT_LOST_AND_FOUND = 16,
        IT_ATTACHMENT = 17,
        IT_WEARABLE = 18,
        IT_ANIMATION = 19,
        IT_GESTURE = 20,
        IT_MESH = 22,
        IT_WIDGET = 23,
        IT_PERSON = 24,
        IT_SETTINGS = 25,
        IT_MATERIAL = 26,
        IT_GLTF = 27,
        IT_GLTF_BIN = 28,
        IT_COUNT = 29,

        IT_UNKNOWN = 255,
        IT_NONE = -1
    };

    enum EIconName
    {
        ICONNAME_TEXTURE,
        ICONNAME_SOUND,
        ICONNAME_CALLINGCARD_ONLINE,
        ICONNAME_CALLINGCARD_OFFLINE,
        ICONNAME_LANDMARK,
        ICONNAME_LANDMARK_VISITED,
        ICONNAME_SCRIPT,
        ICONNAME_CLOTHING,
        ICONNAME_OBJECT,
        ICONNAME_OBJECT_MULTI,
        ICONNAME_NOTECARD,
        ICONNAME_BODYPART,
        ICONNAME_SNAPSHOT,

        ICONNAME_BODYPART_SHAPE,
        ICONNAME_BODYPART_SKIN,
        ICONNAME_BODYPART_HAIR,
        ICONNAME_BODYPART_EYES,
        ICONNAME_CLOTHING_SHIRT,
        ICONNAME_CLOTHING_PANTS,
        ICONNAME_CLOTHING_SHOES,
        ICONNAME_CLOTHING_SOCKS,
        ICONNAME_CLOTHING_JACKET,
        ICONNAME_CLOTHING_GLOVES,
        ICONNAME_CLOTHING_UNDERSHIRT,
        ICONNAME_CLOTHING_UNDERPANTS,
        ICONNAME_CLOTHING_SKIRT,
        ICONNAME_CLOTHING_ALPHA,
        ICONNAME_CLOTHING_TATTOO,
        ICONNAME_CLOTHING_UNIVERSAL,

        ICONNAME_ANIMATION,
        ICONNAME_GESTURE,

        ICONNAME_CLOTHING_PHYSICS,

        ICONNAME_LINKITEM,
        ICONNAME_LINKFOLDER,
        ICONNAME_MESH,

        ICONNAME_SETTINGS,
        ICONNAME_SETTINGS_SKY,
        ICONNAME_SETTINGS_WATER,
        ICONNAME_SETTINGS_DAY,

        ICONNAME_MATERIAL,

        ICONNAME_SCRIPT_LUAU,

        ICONNAME_INVALID,
        ICONNAME_UNKNOWN,
        ICONNAME_COUNT,
        ICONNAME_NONE = -1
    };


    // machine transation between type and strings
    static EType lookup(const std::string& name);
    static const std::string &lookup(EType type);
    // translation from a type to a human readable form.
    static const std::string &lookupHumanReadable(EType type);

    // return the default inventory for the given asset type.
    static EType defaultForAssetType(LLAssetType::EType asset_type);

    // true if this type cannot have restricted permissions.
    static bool cannotRestrictPermissions(EType type);

    static bool showInWorldPermissions(EType type);

private:
    // don't instantiate or derive one of these objects
    LLInventoryType( void );
    ~LLInventoryType( void );
};

enum ScriptSubtype_t { SST_LSL = 0, SST_LUA = 1 };

// helper function that returns true if inventory type and asset type
// are potentially compatible. For example, an attachment must be an
// object, but a wearable can be a bodypart or clothing asset.
bool inventory_and_asset_types_match(LLInventoryType::EType inventory_type,
                                     LLAssetType::EType asset_type);

// ---- VKS-ORACLE-COPY END

// ---- VKS-ORACLE-COPY BEGIN
// source: indra/llinventory/llfoldertype.h
// lines: 29-132
// blob: e55c41eee9f85a04e55b5d226ccc6749fead872f
// copied: the whole header between its include guard
// substitutions: #include lines and forward declarations of LLMessageSystem and LLUUID removed
// conditionals: #ifndef LL_LLFOLDERTYPE_H
// ----


// This class handles folder types (similar to assettype, except for folders)
// and operations on those.
class LL_COMMON_API LLFolderType
{
public:
    // ! BACKWARDS COMPATIBILITY ! Folder type enums must match asset type enums.
    enum EType
    {
        FT_TEXTURE = 0,

        FT_SOUND = 1,

        FT_CALLINGCARD = 2,

        FT_LANDMARK = 3,

        FT_CLOTHING = 5,

        FT_OBJECT = 6,

        FT_NOTECARD = 7,

        FT_ROOT_INVENTORY = 8,
            // We'd really like to change this to 9 since AT_CATEGORY is 8,
            // but "My Inventory" has been type 8 for a long time.

        FT_LSL_TEXT = 10,

        FT_BODYPART = 13,

        FT_TRASH = 14,

        FT_SNAPSHOT_CATEGORY = 15,

        FT_LOST_AND_FOUND = 16,

        FT_ANIMATION = 20,

        FT_GESTURE = 21,

        FT_FAVORITE = 23,

        FT_ENSEMBLE_START = 26,
        FT_ENSEMBLE_END = 45,
            // This range is reserved for special clothing folder types.

        FT_CURRENT_OUTFIT = 46,
        FT_OUTFIT = 47,
        FT_MY_OUTFITS = 48,

        FT_MESH = 49,

        FT_INBOX = 50,
        FT_OUTBOX = 51,

        FT_BASIC_ROOT = 52,

        FT_MARKETPLACE_LISTINGS = 53,
        FT_MARKETPLACE_STOCK = 54,
        FT_MARKETPLACE_VERSION = 55,    // Note: We actually *never* create folders with that type. This is used for icon override only.

        FT_SETTINGS = 56,

        FT_MATERIAL = 57,

        // <FS:Ansariel> Folder types for our own virtual system folders
        FT_FIRESTORM = 58,
        FT_PHOENIX = 59,
        FT_RLV = 60,
        // </FS:Ansariel> Folder types for our own virtual system folders

        FT_MY_SUITCASE = 100, // <FS:Ansariel> OpenSim HG-support

        FT_COUNT,

        FT_NONE = -1

        // When adding, see note at bottom of LLAssetType::Etype
    };

    static EType                lookup(const std::string& type_name);
    static const std::string&   lookup(EType folder_type);

    static bool                 lookupIsProtectedType(EType folder_type);
    static bool                 lookupIsAutomaticType(EType folder_type);
    static bool                 lookupIsSingletonType(EType folder_type);
    static bool                 lookupIsEnsembleType(EType folder_type);

    static LLAssetType::EType   folderTypeToAssetType(LLFolderType::EType folder_type);
    static LLFolderType::EType  assetTypeToFolderType(LLAssetType::EType asset_type);

    static const std::string&   badLookup(); // error string when a lookup fails

    static LLSD getTypeNames();

protected:
    LLFolderType() {}
    ~LLFolderType() {}
};

// ---- VKS-ORACLE-COPY END

// ---- VKS-ORACLE-COPY BEGIN
// source: indra/llinventory/llinventorydefines.h
// lines: 29-100
// blob: 8e7bd87ac4332cefac3736990cd9e173145534dc
// copied: the whole header between its include guard
// substitutions: #include lines and forward declarations of LLMessageSystem and LLUUID removed
// conditionals: #ifndef LL_LLINVENTORYDEFINES_H
// ----

// Consts for "key" field in the task inventory update message
extern const U8 TASK_INVENTORY_ITEM_KEY;
extern const U8 TASK_INVENTORY_ASSET_KEY;

// Max inventory buffer size (for use in packBinaryBucket)
enum
{
    MAX_INVENTORY_BUFFER_SIZE = 1024
};

//--------------------------------------------------------------------
// Inventory item flags enums
//   The shared flags at the top are shared among all inventory
//   types. After that section, all values of flags are type
//   dependent.  The shared flags will start at 2^30 and work
//   down while item type specific flags will start at 2^0 and work up.
//--------------------------------------------------------------------
class LLInventoryItemFlags
{
public:
    enum EType
    {
        II_FLAGS_NONE                               = 0,

        II_FLAGS_SHARED_SINGLE_REFERENCE            = 0x40000000,
            // The asset has only one reference in the system. If the
            // inventory item is deleted, or the assetid updated, then we
            // can remove the old reference.

        II_FLAGS_LANDMARK_VISITED                   = 1,

        II_FLAGS_OBJECT_SLAM_PERM                   = 0x100,
            // Object permissions should have next owner perm be more
            // restrictive on rez. We bump this into the second byte of the
            // flags since the low byte is used to track attachment points.

        II_FLAGS_OBJECT_SLAM_SALE                   = 0x1000,
            // The object sale information has been changed.

        II_FLAGS_OBJECT_PERM_OVERWRITE_BASE         = 0x010000,
        II_FLAGS_OBJECT_PERM_OVERWRITE_OWNER        = 0x020000,
        II_FLAGS_OBJECT_PERM_OVERWRITE_GROUP        = 0x040000,
        II_FLAGS_OBJECT_PERM_OVERWRITE_EVERYONE     = 0x080000,
        II_FLAGS_OBJECT_PERM_OVERWRITE_NEXT_OWNER   = 0x100000,
            // Specify which permissions masks to overwrite
            // upon rez.  Normally, if no permissions slam (above) or
            // overwrite flags are set, the asset's permissions are
            // used and the inventory's permissions are ignored.  If
            // any of these flags are set, the inventory's permissions
            // take precedence.

        II_FLAGS_OBJECT_HAS_MULTIPLE_ITEMS          = 0x200000,
            // Whether a returned object is composed of multiple items.

        II_FLAGS_SUBTYPE_MASK                       = 0x0000ff,
            // Some items like Wearables and settings use the low order byte
            // of flags to store the sub type of the inventory item.
            // see LLWearableType::EType enumeration found in newview/llwearable.h

        II_FLAGS_PERM_OVERWRITE_MASK =              (II_FLAGS_OBJECT_SLAM_PERM |
                                                     II_FLAGS_OBJECT_SLAM_SALE |
                                                     II_FLAGS_OBJECT_PERM_OVERWRITE_BASE |
                                                     II_FLAGS_OBJECT_PERM_OVERWRITE_OWNER |
                                                     II_FLAGS_OBJECT_PERM_OVERWRITE_GROUP |
                                                     II_FLAGS_OBJECT_PERM_OVERWRITE_EVERYONE |
                                                     II_FLAGS_OBJECT_PERM_OVERWRITE_NEXT_OWNER),
            // These bits need to be cleared whenever the asset_id is updated
            // on a pre-existing inventory item (DEV-28098 and DEV-30997)
    };
};

// ---- VKS-ORACLE-COPY END

// ---- VKS-ORACLE-COPY BEGIN
// source: indra/llinventory/llpermissions.h
// lines: 29-461
// blob: 70b29cd312d1b684d196f2dee7d65fdcbd99c6d2
// copied: the whole header between its include guard
// substitutions: #include lines and forward declarations of LLMessageSystem and LLUUID removed
// conditionals: #ifndef LL_LLPERMISSIONS_H
// ----


// prototypes
extern void mask_to_string(U32 mask, char* str, bool isOpenSim=false);
extern std::string mask_to_string(U32 mask, bool isOpenSim=false);
template<class T> class LLMetaClassT;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Class LLPermissions
//
// Class which encapsulates object and inventory permissions/ownership/etc.
//
// Permissions where originally a static state creator/owner and set
// of cap bits. Since then, it has grown to include group information,
// last owner, masks for different people. The implementation has been
// chosen such that a uuid is stored for each current/past owner, and
// a bitmask is stored for the base permissions, owner permissions,
// group permissions, and everyone else permissions.
//
// The base permissions represent the most permissive state that the
// permissions can possibly be in. Thus, if the base permissions do
// not allow copying, no one can ever copy the object. The permissions
// also maintain a tree-like hierarchy of permissions, thus, if we
// (for sake of discussions) denote more permissive as '>', then this
// is invariant:
//
// base mask >= owner mask >= group mask
//                         >= everyone mask
//                         >= next owner mask
// NOTE: the group mask does not effect everyone or next, everyone
// does not effect group or next, etc.
//
// It is considered a fair use right to move or delete any object you
// own.  Another fair use right is the ability to give away anything
// which you cannot copy. One way to look at that is that if you have
// a unique item, you can always give that one copy you have to
// someone else.
//
// Most of the bitmask is easy to understand, PERM_COPY means you can
// copy !PERM_TRANSFER means you cannot transfer, etc. Given that we
// now track the concept of 'next owner' inside of the permissions
// object, we can describe some new meta-meaning to the PERM_MODIFY
// flag. PERM_MODIFY is usually meant to note if you can change an
// item, but since we record next owner permissions, we can interpret
// a no-modify object as 'you cannot modify this object and you cannot
// make derivative works.' When evaluating functionality, and
// comparisons against permissions, keep this concept in mind for
// logical consistency.
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

class LLPermissions
{
private:
    LLUUID          mCreator;               // null if object created by system
    LLUUID          mOwner;                 // null if object "unowned" (owned by system)
    LLUUID          mLastOwner;             // object's last owner
    LLUUID          mGroup;                 // The group association

    PermissionMask  mMaskBase;              // initially permissive, progressively AND restricted by each owner

    PermissionMask  mMaskOwner;             // set by owner, applies to owner only, restricts lower permissions
    PermissionMask  mMaskEveryone;          // set by owner, applies to everyone else

    PermissionMask  mMaskGroup;             // set by owner, applies to group that is associated with permissions

    PermissionMask mMaskNextOwner;          // set by owner, applied to base on transfer.

    // Usually set in the fixOwnership() method based on current uuid
    // values.
    bool mIsGroupOwned;

    static bool sIsInOpenSim; // <FS:TJ/> [FIRE-36028] Fix OpenSim object permissions

    // Correct for fair use - you can never take away the right to
    // move stuff you own, and you can never take away the right to
    // transfer something you cannot otherwise copy.
    void fixFairUse();

    // Fix internal consistency for group/agent ownership
    void fixOwnership();

public:
    static const LLPermissions DEFAULT;

    LLPermissions();                        // defaults to created by system
    //~LLPermissions();

    // base initialization code
    void init(const LLUUID& creator, const LLUUID& owner,
              const LLUUID& last_owner, const LLUUID& group);
    void initMasks(PermissionMask base, PermissionMask owner,
                   PermissionMask everyone, PermissionMask group,
                   PermissionMask next);
    // adjust permissions based on inventory type.
    void initMasks(LLInventoryType::EType type);

    //
    // ACCESSORS
    //

    // return the agent_id of the agent that created the item
    const LLUUID&   getCreator()        const   { return mCreator; }

    // return the agent_id of the owner. returns LLUUID::null if group
    // owned or public (a really big group).
    const LLUUID&   getOwner()          const   { return mOwner; }

    // return the group_id of the group associated with the
    // object.
    const LLUUID&   getGroup()          const   { return mGroup; }

    // return the agent_id of the last agent owner. Only returns
    // LLUUID::null if there has never been a previous owner (*note: this is apparently not true, say for textures in inventory, it may return LLUUID::null even if there was a previous owner).
    const LLUUID&   getLastOwner()      const   { return mLastOwner; }

    U32             getMaskBase()       const   { return mMaskBase; }
    U32             getMaskOwner()      const   { return mMaskOwner; }
    U32             getMaskGroup()      const   { return mMaskGroup; }
    U32             getMaskEveryone()   const   { return mMaskEveryone; }
    U32 getMaskNextOwner() const { return mMaskNextOwner; }

    // return true if the object has any owner
    bool isOwned() const { return (mOwner.notNull() || mIsGroupOwned); }

    // return true if group_id is owner.
    bool isGroupOwned() const { return mIsGroupOwned; }

    // This API returns true if the object is owned at all, and false
    // otherwise. If it is owned at all, owner id is filled with
    // either the owner id or the group id, and the is_group_owned
    // parameter is appropriately filled. The values of owner_id and
    // is_group_owned are not changed if the object is not owned.
    bool getOwnership(LLUUID& owner_id, bool& is_group_owned) const;

    // Gets the 'safe' owner.  This should never return LLUUID::null.
    // If no group owned, return the agent owner id normally.
    // If group owned, return the group id.
    // If not owned, return a random uuid which should have no power.
    LLUUID getSafeOwner() const;

    // return a cheap crc
    U32 getCRC32() const;


    //
    // MANIPULATORS
    //

    // Fix hierarchy of permissions, applies appropriate permissions
    // at each level to ensure that base permissions are respected,
    // and also ensures that if base cannot transfer, then group and
    // other cannot copy.
    void fix();

    // All of these methods just do exactly what they say. There is no
    // permissions checking to see if the operation is allowed, and do
    // not fix the permissions hierarchy. So please only use these
    // methods when you are know what you're doing and coding on
    // behalf of the system - ie, acting as god.
    void set(const LLPermissions& permissions);
    void setMaskBase(U32 mask)     { mMaskBase = mask; }
    void setMaskOwner(U32 mask)    { mMaskOwner = mask; }
    void setMaskEveryone(U32 mask) { mMaskEveryone = mask;}
    void setMaskGroup(U32 mask)    { mMaskGroup = mask;}
    void setMaskNext(U32 mask) { mMaskNextOwner = mask; }

    // Allow accumulation of permissions. Results in the tightest
    // permissions possible. In the case of clashing UUIDs, it sets
    // the ID to LLUUID::null.
    void accumulate(const LLPermissions& perm);

    //
    // CHECKED MANIPULATORS
    //

    // These functions return true on success.  They return false if
    // the given agent isn't allowed to make the change.  You can pass
    // LLUUID::null as the agent id if the change is being made by the
    // simulator itself, not on behalf of any agent - this will always
    // succeed. Passing in group id of LLUUID:null means no group, and
    // does not offer special permission to do anything.

    // saves last owner, sets current owner, and sets the group.
    // set is_atomic = true means that this permission represents
    // an atomic permission and not a collection of permissions.
    // Currently, the only way to have a collection is when an object
    // has inventory and is then itself rolled up into an inventory
    // item.
    bool setOwnerAndGroup(const LLUUID& agent, const LLUUID& owner, const LLUUID& group, bool is_atomic);

    // only call this if you know what you're doing
    // there are usually perm-bit consequences when the
    // ownerhsip changes
    void yesReallySetOwner(const LLUUID& owner, bool group_owned);

    // Last owner doesn't have much in the way of permissions so it's
    //not too dangerous to do this.
    void setLastOwner(const LLUUID& last_owner);

    // saves last owner, sets owner to uuid null, sets group
    // owned. group_id must be the group of the object (that's who it
    // is being deeded to) and the object must be group
    // modify. Technically, the agent id and group id are not
    // necessary, but I wanted this function to look like the other
    // checked manipulators (since that is how it is used.) If the
    // agent is the system or (group == mGroup and group modify and
    // owner transfer) then this function will deed the permissions,
    // set the next owner mask, and return true. Otherwise, no change
    // is effected, and the function returns false.
    bool deedToGroup(const LLUUID& agent, const LLUUID& group);
    // Attempt to set or clear the given bitmask.  Returns true if you
    // are allowed to modify the permissions.  If you attempt to turn
    // on bits not allowed by the base bits, the function will return
    // true, but those bits will not be set.
    bool setBaseBits( const LLUUID& agent, bool set, PermissionMask bits);
    bool setOwnerBits( const LLUUID& agent, bool set, PermissionMask bits);
    bool setGroupBits( const LLUUID& agent, const LLUUID& group, bool set, PermissionMask bits);
    bool setEveryoneBits(const LLUUID& agent, const LLUUID& group, bool set, PermissionMask bits);
    bool setNextOwnerBits(const LLUUID& agent, const LLUUID& group, bool set, PermissionMask bits);

    // This is currently only used in the Viewer to handle calling cards
    // where the creator is actually used to store the target. Use with care.
    void setCreator(const LLUUID& creator) { mCreator = creator; }

    //
    // METHODS
    //

    // All the allow* functions return true if the given agent or
    // group can perform the function. Prefer using this set of
    // operations to check permissions on an object.  These return
    // true if the given agent or group can perform the function.
    // They also return true if the object isn't owned, or the
    // requesting agent is a system agent.  See llpermissionsflags.h
    // for bits.
    bool allowOperationBy(PermissionBit op, const LLUUID& agent, const LLUUID& group = LLUUID::null) const;

    inline bool allowModifyBy(const LLUUID &agent_id) const;
    inline bool allowCopyBy(const LLUUID& agent_id) const;
    inline bool allowMoveBy(const LLUUID& agent_id) const;

    inline bool allowModifyBy(const LLUUID &agent_id, const LLUUID& group) const;
    inline bool allowCopyBy(const LLUUID& agent_id, const LLUUID& group) const;
    inline bool allowMoveBy(const LLUUID &agent_id, const LLUUID &group) const;

    // This somewhat specialized function is meant for testing if the
    // current owner is allowed to transfer to the specified agent id.
    inline bool allowTransferTo(const LLUUID &agent_id) const;

    // Returns true if the object can exported by the given agent
    // (e.g. saved as a local .gltf file)
    // The current test should return true if the agent is the owner
    // AND the creator of the object.
    inline bool allowExportBy(const LLUUID& agent_id) const;
#ifdef OPENSIM
    inline bool allowOpenSimExportBy(const LLUUID& agent_id) const;    // <FS:CR> OpenSim export permission
#endif

    // <FS:TJ> [FIRE-36028] Fix OpenSim object permissions
    static bool getIsInOpenSim() { return sIsInOpenSim; }
    static void setupIsInOpenSim(bool is_in_open_sim) { sIsInOpenSim = is_in_open_sim; }
    // </FS:TJ>

    //
    // MISC METHODS and OPERATORS
    //

    LLSD    packMessage() const;
    void    unpackMessage(LLSD perms);

    // For messaging system support
    void    packMessage(LLMessageSystem* msg) const;
    void    unpackMessage(LLMessageSystem* msg, const char* block, S32 block_num = 0);

    bool    importLegacyStream(std::istream& input_stream);
    bool    exportLegacyStream(std::ostream& output_stream) const;

    void importLLSD(const LLSD& sd_perm);

    bool operator==(const LLPermissions &rhs) const;
    bool operator!=(const LLPermissions &rhs) const;

    friend std::ostream& operator<<(std::ostream &s, const LLPermissions &perm);

};

// Inlines
bool LLPermissions::allowModifyBy(const LLUUID& agent, const LLUUID& group) const
{
    return allowOperationBy(PERM_MODIFY, agent, group);
}

bool LLPermissions::allowCopyBy(const LLUUID& agent, const LLUUID& group) const
{
    return allowOperationBy(PERM_COPY, agent, group);
}


bool LLPermissions::allowMoveBy(const LLUUID& agent, const LLUUID& group) const
{
    return allowOperationBy(PERM_MOVE, agent, group);
}

bool LLPermissions::allowModifyBy(const LLUUID& agent) const
{
    return allowOperationBy(PERM_MODIFY, agent, LLUUID::null);
}

bool LLPermissions::allowCopyBy(const LLUUID& agent) const
{
    return allowOperationBy(PERM_COPY, agent, LLUUID::null);
}

bool LLPermissions::allowMoveBy(const LLUUID& agent) const
{
    return allowOperationBy(PERM_MOVE, agent, LLUUID::null);
}

bool LLPermissions::allowExportBy(const LLUUID& agent) const
{
    return agent == mOwner && agent == mCreator;
}

// <FS:CR> Opensim Export Permissions
#ifdef OPENSIM
bool LLPermissions::allowOpenSimExportBy(const LLUUID& agent) const
{
    return ((mCreator == agent) ? true : (allowOperationBy(PERM_EXPORT, agent, LLUUID::null)));
}
#endif
// </FS:CR>

bool LLPermissions::allowTransferTo(const LLUUID &agent_id) const
{
    if (mIsGroupOwned)
    {
        return allowOperationBy(PERM_TRANSFER, mGroup, mGroup);
    }
    else
    {
        return ((mOwner == agent_id) ? true : allowOperationBy(PERM_TRANSFER, mOwner));
    }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Class LLAggregatePermissions
//
// Class which encapsulates object and inventory permissions,
// ownership, etc. Currently, it only aggregates PERM_COPY,
// PERM_MODIFY, and PERM_TRANSFER.
//
// Usually you will construct an instance and hand the object several
// permissions masks to aggregate the copy, modify, and
// transferability into a nice trinary value.
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

class LLAggregatePermissions
{
public:
    enum EValue
    {
        AP_EMPTY = 0x00,
        AP_NONE = 0x01,
        AP_SOME = 0x02,
        AP_ALL = 0x03
    };

    // construct an empty aggregate permissions
    LLAggregatePermissions();

    // pass in a PERM_COPY, PERM_TRANSFER, etc, and get out a EValue
    // enumeration describing the current aggregate permissions.
    EValue getValue(PermissionBit bit) const;

    // returns the permissions packed into the 6 LSB of a U8:
    // 00TTMMCC
    // where TT = transfer, MM = modify, and CC = copy
    // LSB is to the right
    U8 getU8() const;

    // return true is the aggregate permissions are empty, otherwise false.
    bool isEmpty() const ;

    // pass in a PERM_COPY, PERM_TRANSFER, etc, and an EValue
    // enumeration to specifically set that value. Not implemented
    // because I'm not sure it's a useful api.
    //void setValue(PermissionBit bit, EValue);

    // Given a mask, aggregate the useful permissions.
    void aggregate(PermissionMask mask);

    // Aggregate aggregates
    void aggregate(const LLAggregatePermissions& ag);

    // message handling
    void packMessage(LLMessageSystem* msg, const char* field) const;
    void unpackMessage(LLMessageSystem* msg, const char* block, const char *field, S32 block_num = 0);

    static const LLAggregatePermissions empty;

    friend std::ostream& operator<<(std::ostream &s, const LLAggregatePermissions &perm);

protected:
    enum EPermIndex
    {
        PI_COPY = 0,
        PI_MODIFY = 1,
        PI_TRANSFER = 2,
        PI_END = 3,
        PI_COUNT = 3
    };
    void aggregateBit(EPermIndex idx, bool allowed);
    void aggregateIndex(EPermIndex idx, U8 bits);
    static EPermIndex perm2PermIndex(PermissionBit bit);

    // structure used to store the aggregate so far.
    U8 mBits[PI_COUNT];
};

// These functions convert between structured data and permissions as
// appropriate for serialization. The permissions are a map of things
// like 'creator_id', 'owner_id', etc, with the value copied from the
// permission object.
LLSD ll_create_sd_from_permissions(const LLPermissions& perm);
void ll_fill_sd_from_permissions(LLSD& rv, const LLPermissions& perm);
LLPermissions ll_permissions_from_sd(const LLSD& sd_perm);

// ---- VKS-ORACLE-COPY END

// ---- VKS-ORACLE-COPY BEGIN
// source: indra/llinventory/llsaleinfo.h
// lines: 29-120
// blob: 7186e8ab498b11a54cf1b3a7b70a7ef3bc083d35
// copied: the whole header between its include guard
// substitutions: #include lines and forward declarations of LLMessageSystem and LLUUID removed
// conditionals: #ifndef LL_LLSALEINFO_H
// ----


//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Class LLSaleInfo
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

// L$ default price for objects
const S32 DEFAULT_PRICE = 10;


class LLSaleInfo
{
public:
    // use this to avoid temporary object creation
    static const LLSaleInfo DEFAULT;

    enum EForSale
    {
        // item is not to be considered for transactions
        FS_NOT = 0,

        // the origional is on sale
        FS_ORIGINAL = 1,

        // A copy is for sale
        FS_COPY = 2,

        // Valid only for tasks, the inventory is for sale
        // at the price in this structure.
        FS_CONTENTS = 3,

        FS_COUNT
    };

protected:
    EForSale mSaleType;
    S32 mSalePrice;

public:
    // default constructor is fine usually
    LLSaleInfo();
    LLSaleInfo(EForSale sale_type, S32 sale_price);

    // accessors
    bool isForSale() const;
    EForSale getSaleType() const { return mSaleType; }
    S32 getSalePrice() const { return mSalePrice; }
    U32 getCRC32() const;

    // mutators
    void setSaleType(EForSale type)     { mSaleType = type; }
    void setSalePrice(S32 price);
    //void setNextOwnerPermMask(U32 mask)   { mNextOwnerPermMask = mask; }

    bool exportLegacyStream(std::ostream& output_stream) const;
    LLSD asLLSD() const;
    void asLLSD(LLSD &sd) const;
    operator LLSD() const { return asLLSD(); }
    bool fromLLSD(const LLSD& sd, bool& has_perm_mask, U32& perm_mask);
    bool importLegacyStream(std::istream& input_stream, bool& has_perm_mask, U32& perm_mask);

    LLSD packMessage() const;
    void unpackMessage(LLSD sales);

    // message serialization
    void packMessage(LLMessageSystem* msg) const;
    void unpackMessage(LLMessageSystem* msg, const char* block);
    void unpackMultiMessage(LLMessageSystem* msg, const char* block,
                            S32 block_num);

    // static functionality for determine for sale status.
    static EForSale lookup(const char* name);
    static const char* lookup(EForSale type);

    // Allow accumulation of sale info. The price of each is added,
    // conflict in sale type results in FS_NOT, and the permissions
    // are tightened.
    void accumulate(const LLSaleInfo& sale_info);

    bool operator==(const LLSaleInfo &rhs) const;
    bool operator!=(const LLSaleInfo &rhs) const;
};

// These functions convert between structured data and sale info as
// appropriate for serialization.
LLSD ll_create_sd_from_sale_info(const LLSaleInfo& sale);
LLSaleInfo ll_sale_info_from_sd(const LLSD& sd);

// ---- VKS-ORACLE-COPY END

// ---- VKS-ORACLE-COPY BEGIN
// source: indra/llinventory/llinventory.h
// lines: 29-304
// blob: 76b3364a802608736f3c5d6a97298fcc5bce82b2
// copied: the whole header between its include guard
// substitutions: #include lines and forward declarations of LLMessageSystem and LLUUID removed
// conditionals: #ifndef LL_LLINVENTORY_H
// ----



//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Class LLInventoryObject
//
//   Base class for anything in the user's inventory.   Handles the common code
//   between items and categories.
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
class LLInventoryObject : public LLRefCount
{
public:
    typedef std::list<LLPointer<LLInventoryObject> > object_list_t;
    typedef std::list<LLConstPointer<LLInventoryObject> > const_object_list_t;

    //--------------------------------------------------------------------
    // Initialization
    //--------------------------------------------------------------------
public:
    LLInventoryObject();
    LLInventoryObject(const LLUUID& uuid,
                      const LLUUID& parent_uuid,
                      LLAssetType::EType type,
                      const std::string& name);
    void copyObject(const LLInventoryObject* other); // LLRefCount requires custom copy
protected:
    virtual ~LLInventoryObject();

    //--------------------------------------------------------------------
    // Accessors
    //--------------------------------------------------------------------
public:
    virtual const LLUUID& getUUID() const; // inventoryID that this item points to
    virtual const LLUUID& getLinkedUUID() const; // inventoryID that this item points to, else this item's inventoryID
    const LLUUID& getParentUUID() const;
    virtual const LLUUID& getThumbnailUUID() const;
    virtual bool getIsFavorite() const;
    virtual const std::string& getRuntime() const;
    virtual const std::string& getName() const;
    virtual LLAssetType::EType getType() const;
    LLAssetType::EType getActualType() const; // bypasses indirection for linked items
    bool getIsLinkType() const;
    virtual time_t getCreationDate() const;

    //--------------------------------------------------------------------
    // Mutators
    //   Will not call updateServer
    //--------------------------------------------------------------------
public:
    void setUUID(const LLUUID& new_uuid);
    virtual void rename(const std::string& new_name);
    void setParent(const LLUUID& new_parent);
    virtual void setThumbnailUUID(const LLUUID& thumbnail_uuid);
    virtual void setFavorite(bool favorite);
    virtual void setRuntime(std::string_view runtime);
    void setType(LLAssetType::EType type);
    virtual void setCreationDate(time_t creation_date_utc); // only stored for items

    // in place correction for inventory name string
    static void correctInventoryName(std::string& name);

    //--------------------------------------------------------------------
    // File Support
    //   Implemented here so that a minimal information set can be transmitted
    //   between simulator and viewer.
    //--------------------------------------------------------------------

    virtual bool importLegacyStream(std::istream& input_stream);
    virtual bool exportLegacyStream(std::ostream& output_stream, bool include_asset_key = true) const;

    virtual void updateParentOnServer(bool) const;
    virtual void updateServer(bool) const;

    //--------------------------------------------------------------------
    // Member Variables
    //--------------------------------------------------------------------
protected:
    LLUUID mUUID;
    LLUUID mParentUUID; // Parent category.  Root categories have LLUUID::NULL.
    LLUUID mThumbnailUUID;
    bool mFavorite;
    std::string mRuntime;
    LLAssetType::EType mType;
    std::string mName;
    time_t mCreationDate; // seconds from 1/1/1970, UTC
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Class LLInventoryItem
//
//   An item in the current user's inventory.
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
class LLInventoryItem : public LLInventoryObject
{
public:
    typedef std::vector<LLPointer<LLInventoryItem> > item_array_t;

    //--------------------------------------------------------------------
    // Initialization
    //--------------------------------------------------------------------
public:
    LLInventoryItem(const LLUUID& uuid,
                    const LLUUID& parent_uuid,
                    const LLPermissions& permissions,
                    const LLUUID& asset_uuid,
                    LLAssetType::EType type,
                    LLInventoryType::EType inv_type,
                    const std::string& name,
                    const std::string& desc,
                    const LLSaleInfo& sale_info,
                    U32 flags,
                    S32 creation_date_utc);
    LLInventoryItem();
    // Create a copy of an inventory item from a pointer to another item
    // Note: Because InventoryItems are ref counted, reference copy (a = b)
    // is prohibited
    LLInventoryItem(const LLInventoryItem* other);
    virtual void copyItem(const LLInventoryItem* other); // LLRefCount requires custom copy
    void generateUUID() { mUUID.generate(); }
protected:
    ~LLInventoryItem(); // ref counted

    //--------------------------------------------------------------------
    // Accessors
    //--------------------------------------------------------------------
public:
    virtual const LLUUID& getLinkedUUID() const;
    virtual const LLPermissions& getPermissions() const;
    virtual const LLUUID& getCreatorUUID() const;
    virtual const LLUUID& getAssetUUID() const;
    virtual const std::string& getDescription() const;
    virtual const std::string& getActualDescription() const; // Does not follow links
    virtual const LLSaleInfo& getSaleInfo() const;
    virtual LLInventoryType::EType getInventoryType() const;
    virtual U8 getInventorySubType() const { return getFlags() & LLInventoryItemFlags::II_FLAGS_SUBTYPE_MASK; }
    virtual U32 getFlags() const;
    virtual time_t getCreationDate() const;
    virtual U32 getCRC32() const; // really more of a checksum.

    //--------------------------------------------------------------------
    // Mutators
    //   Will not call updateServer and will never fail
    //   (though it may correct to sane values)
    //--------------------------------------------------------------------
public:
    void setAssetUUID(const LLUUID& asset_id);
    static void correctInventoryDescription(std::string& name);
    void setDescription(const std::string& new_desc);
    void setSaleInfo(const LLSaleInfo& sale_info);
    void setPermissions(const LLPermissions& perm);
    void setInventoryType(LLInventoryType::EType inv_type);
    void setInventorySubType(U8 sub_type) { setFlags((getFlags() & ~LLInventoryItemFlags::II_FLAGS_SUBTYPE_MASK) | (sub_type & LLInventoryItemFlags::II_FLAGS_SUBTYPE_MASK)); }
    void setFlags(U32 flags);
    void setCreator(const LLUUID& creator); // only used for calling cards

    // Check for changes in permissions masks and sale info
    // and set the corresponding bits in mFlags.
    void accumulatePermissionSlamBits(const LLInventoryItem& old_item);

    // Put this inventory item onto the current outgoing mesage.
    // Assumes you have already called nextBlock().
    virtual void packMessage(LLMessageSystem* msg) const;

    // Returns true if the inventory item came through the network correctly.
    // Uses a simple crc check which is defeatable, but we want to detect
    // network mangling somehow.
    virtual bool unpackMessage(LLMessageSystem* msg, const char* block, S32 block_num = 0);

    //--------------------------------------------------------------------
    // File Support
    //--------------------------------------------------------------------
public:
    virtual bool importLegacyStream(std::istream& input_stream);
    virtual bool exportLegacyStream(std::ostream& output_stream, bool include_asset_key = true) const;

    //--------------------------------------------------------------------
    // Helper Functions
    //--------------------------------------------------------------------
public:
    LLSD asLLSD() const;
    void asLLSD( LLSD& sd ) const;
    bool fromLLSD(const LLSD& sd, bool is_new = true);

    //--------------------------------------------------------------------
    // Member Variables
    //--------------------------------------------------------------------
protected:
    LLPermissions mPermissions;
    LLUUID mAssetUUID;
    std::string mDescription;
    LLSaleInfo mSaleInfo;
    LLInventoryType::EType mInventoryType;
    U32 mFlags;
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Class LLInventoryCategory
//
//   A category/folder of inventory items. Users come with a set of default
//   categories, and can create new ones as needed.
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
class LLInventoryCategory : public LLInventoryObject
{
public:
    typedef std::vector<LLPointer<LLInventoryCategory> > cat_array_t;

    //--------------------------------------------------------------------
    // Initialization
    //--------------------------------------------------------------------
public:
    LLInventoryCategory(const LLUUID& uuid, const LLUUID& parent_uuid,
                        LLFolderType::EType preferred_type,
                        const std::string& name);
    LLInventoryCategory();
    LLInventoryCategory(const LLInventoryCategory* other);
    void copyCategory(const LLInventoryCategory* other); // LLRefCount requires custom copy
protected:
    virtual ~LLInventoryCategory();

    //--------------------------------------------------------------------
    // Accessors And Mutators
    //--------------------------------------------------------------------
public:
    LLFolderType::EType getPreferredType() const;
    void setPreferredType(LLFolderType::EType type);
    LLSD asLLSD() const;
    LLSD asAISCreateCatLLSD() const;
    bool fromLLSD(const LLSD& sd);

    //--------------------------------------------------------------------
    // Messaging
    //--------------------------------------------------------------------
public:
    virtual void packMessage(LLMessageSystem* msg) const;
    virtual void unpackMessage(LLMessageSystem* msg, const char* block, S32 block_num = 0);

    //--------------------------------------------------------------------
    // File Support
    //--------------------------------------------------------------------
public:
    virtual bool importLegacyStream(std::istream& input_stream);
    virtual bool exportLegacyStream(std::ostream& output_stream, bool include_asset_key = true) const;

    virtual void exportLLSD(LLSD& sd) const;
    bool importLLSDMap(const LLSD& cat_data);
    virtual bool importLLSD(const std::string& label, const LLSD& value);
    //--------------------------------------------------------------------
    // Member Variables
    //--------------------------------------------------------------------
protected:
    LLFolderType::EType mPreferredType; // Type that this category was "meant" to hold (although it may hold any type).
};


//-----------------------------------------------------------------------------
// Convertors
//
//   These functions convert between structured data and an inventory
//   item, appropriate for serialization.
//   Not up to date (no favorites, nor thumbnails), for testing purposes
//-----------------------------------------------------------------------------
LLSD ll_create_sd_from_inventory_item(LLPointer<LLInventoryItem> item);
LLSD ll_create_sd_from_inventory_category(LLPointer<LLInventoryCategory> cat);
LLPointer<LLInventoryCategory> ll_create_category_from_sd(const LLSD& sd_cat);

// ---- VKS-ORACLE-COPY END

// ---- VKS-ORACLE-COPY BEGIN
// source: indra/llinventory/llcategory.h
// lines: 29-97
// blob: a68ebaf721fb76671181c00d2a6bab0832e72856
// copied: the whole header between its include guard
// substitutions: #include lines and forward declarations of LLMessageSystem and LLUUID removed
// conditionals: #ifndef LL_LLCATEGORY_H
// ----

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Class LLCategory
//
// An instance of the LLCategory class represents a particular
// category in a hierarchical classification system. For now, it is 4
// levels deep with 255 (minus 1) possible values at each level. If a
// non zero value is found at level 4, that is the leaf category,
// otherwise, it is the first level that has a 0 in the next depth
// level.
//
// To output the names of all top level categories, you could do the
// following:
//
//  S32 count = LLCategory::none.getSubCategoryCount();
//  for(S32 i = 0; i < count; i++)
//  {
//      LL_INFOS() << none.getSubCategory(i).lookupNmae() << LL_ENDL;
//  }
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~


class LLCategory
{
public:
    // Nice default static const.
    static const LLCategory none;

    // construction. Since this is really a POD type, destruction,
    // copy, and assignment are handled by the compiler.
    LLCategory();
    explicit LLCategory(U32 value) { init(value); }

    // methods
    void init(U32 value);
    U32 getU32() const;
    S32 getSubCategoryCount() const;

    // This method will return a category that is the nth
    // subcategory. If you're already at the bottom of the hierarchy,
    // then the method will return a copy of this.
    LLCategory getSubCategory(U8 n) const;

    // This method will return the name of the leaf category type
    const char* lookupName() const;

    // This method will return the full hierarchy name in an easily
    // interpreted (TOP)|(SUB1)|(SUB2) format. *NOTE: not implemented
    // because we don't have anything but top level categories at the
    // moment.
    //const char* lookupFullName() const;

    // message serialization
    void packMessage(LLMessageSystem* msg) const;
    void unpackMessage(LLMessageSystem* msg, const char* block);
    void unpackMultiMessage(LLMessageSystem* msg, const char* block,
                            S32 block_num);
protected:
    enum
    {
        CATEGORY_TOP = 0,
        CATEGORY_DEPTH = 4,
    };

    U8 mData[CATEGORY_DEPTH];
};


// ---- VKS-ORACLE-COPY END

// ---- VKS-ORACLE-COPY BEGIN
// source: indra/llinventory/llinventorysettings.h
// lines: 30-62
// blob: 6b6685d088d3d43b72e2a3c0c40d3e295125bb68
// copied: the whole header between its include guard
// substitutions: #include lines and forward declarations of LLMessageSystem and LLUUID removed
// conditionals: #ifndef LL_INVENTORY_SETTINGS_H
// ----


class LLSettingsType : public LLParamSingleton<LLSettingsType>
{
    LLSINGLETON(LLSettingsType, LLTranslationBridge::ptr_t &trans);
    ~LLSettingsType();

    friend struct SettingsEntry;

public:
    enum type_e
    {
        ST_SKY = 0,
        ST_WATER = 1,
        ST_DAYCYCLE = 2,

        ST_INVALID = 255,
        ST_NONE = -1
    };

    static type_e fromInventoryFlags(U32 flags);
    static LLInventoryType::EIconName getIconName(type_e type);
    static std::string getDefaultName(type_e type);

protected:

    LLTranslationBridge::ptr_t mTranslator;
};


// ---- VKS-ORACLE-COPY END

// ---- VKS-ORACLE-COPY BEGIN
// source: indra/llinventory/lltransactionflags.h
// lines: 28-64
// blob: 3c61b785a3ec8e75bd93c7f9de1e4cc66201ec7c
// copied: the whole header between its include guard
// substitutions: #include lines and forward declarations of LLMessageSystem and LLUUID removed
// conditionals: #ifndef LL_LLTRANSACTIONFLAGS_H
// ----


typedef U8 TransactionFlags;

// defined in common/llinventory/lltransactionflags.cpp
extern const TransactionFlags TRANSACTION_FLAGS_NONE;
extern const TransactionFlags TRANSACTION_FLAG_SOURCE_GROUP;
extern const TransactionFlags TRANSACTION_FLAG_DEST_GROUP;
extern const TransactionFlags TRANSACTION_FLAG_OWNER_GROUP;
extern const TransactionFlags TRANSACTION_FLAG_SIMULTANEOUS_CONTRIBUTION;
extern const TransactionFlags TRANSACTION_FLAG_SIMULTANEOUS_CONTRIBUTION_REMOVAL;

// very simple helper functions
TransactionFlags pack_transaction_flags(bool is_source_group, bool is_dest_group);
bool is_tf_source_group(TransactionFlags flags);
bool is_tf_dest_group(TransactionFlags flags);
bool is_tf_owner_group(TransactionFlags flags);

// stupid helper functions which should be replaced with some kind of
// internationalizeable message.
std::string build_transfer_message_to_source(
    S32 amount,
    const LLUUID& source_id,
    const LLUUID& dest_id,
    const std::string& dest_name,
    S32 transaction_type,
    const std::string& description);

std::string build_transfer_message_to_destination(
    S32 amount,
    const LLUUID& dest_id,
    const LLUUID& source_id,
    const std::string& source_name,
    S32 transaction_type,
    const std::string& description);

// ---- VKS-ORACLE-COPY END

// ---- VKS-ORACLE-COPY BEGIN
// source: indra/llinventory/llnotecard.h
// lines: 29-68
// blob: 764157692d4357377831e07d2367fd0a9c2ddd75
// copied: the whole header between its include guard
// substitutions: #include lines and forward declarations of LLMessageSystem and LLUUID removed
// conditionals: #ifndef LL_NOTECARD_H
// ----


class LLNotecard
{
public:
    /**
     * @brief anonymous enumeration to set max size.
     */
    enum
    {
        MAX_SIZE = 65536
    };

    LLNotecard(S32 max_text = LLNotecard::MAX_SIZE);
    virtual ~LLNotecard();

    bool importStream(std::istream& str);
    bool exportStream(std::ostream& str);

    const std::vector<LLPointer<LLInventoryItem> >& getItems() const { return mItems; }
    const std::string& getText() const { return mText; }
    std::string& getText() { return mText; }

    void setItems(const std::vector<LLPointer<LLInventoryItem> >& items);
    void setText(const std::string& text);
    S32 getVersion() { return mVersion; }
    S32 getEmbeddedVersion() { return mEmbeddedVersion; }

private:
    bool importEmbeddedItemsStream(std::istream& str);
    bool exportEmbeddedItemsStream(std::ostream& str);
    std::vector<LLPointer<LLInventoryItem> > mItems;
    std::string mText;
    S32 mMaxText;
    S32 mVersion;
    S32 mEmbeddedVersion;
};

// ---- VKS-ORACLE-COPY END

// Harness declarations (not copies): the viewer subclasses reduced to the
// members the copied functions below use.
class LLViewerInventoryItem : public LLInventoryItem
{
public:
    LLTransactionID mTransactionID;
    void packMessage(LLMessageSystem* msg) const override;
    U32 getCRC32() const override;
protected:
    ~LLViewerInventoryItem() {}
};
class LLViewerInventoryCategory : public LLInventoryCategory
{
public:
    LLUUID mOwnerID;
    S32 mVersion = -1;
    void setVersion(S32 version);
    void packMessage(LLMessageSystem* msg) const override;
    void exportLLSD(LLSD& cat_data) const override;
    bool importLLSD(const std::string& label, const LLSD& value) override;
protected:
    ~LLViewerInventoryCategory() {}
};

} // namespace llinventory_oracle

#include "llinventory.h"

// The same viewer subclass functions over the live base classes, so that the
// live side of the parity test packs and caches through the live llinventory.
namespace llinventory_live_shim
{
class LLViewerInventoryItem : public ::LLInventoryItem
{
public:
    LLTransactionID mTransactionID;
    void packMessage(LLMessageSystem* msg) const override;
    U32 getCRC32() const override;
protected:
    ~LLViewerInventoryItem() {}
};
class LLViewerInventoryCategory : public ::LLInventoryCategory
{
public:
    LLUUID mOwnerID;
    S32 mVersion = -1;
    void setVersion(S32 version);
    void packMessage(LLMessageSystem* msg) const override;
    void exportLLSD(LLSD& cat_data) const override;
    bool importLLSD(const std::string& label, const LLSD& value) override;
protected:
    ~LLViewerInventoryCategory() {}
};
} // namespace llinventory_live_shim

#endif // VKS_LLINVENTORY_ORACLE_H
