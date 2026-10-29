# 03 — Content and data model

This chapter surveys the viewer's content and data model: first and deepest
the inventory (the `indra/llinventory` record library, the viewer's
`LLInventoryModel` and its cache, fetch, AIS, offers, giving and object
contents), then primitives, characters and appearance, and the viewer object
store that follows inventory in the port order. Inventory is the current rung
of the Rust port: its oracle phase is complete (six recorders, R1–R4b, with
frozen C++ copies, about 55 MB of fixtures and six replay tests in CI), and
the crate `vkstorm-inventory` exists only as a skeleton of module doc
comments. The main structural finding is that the inventory records' field
storage cannot leave C++ in this rung (the viewer subclasses touch the base
classes' protected members directly), so the port is "Rust decides, C++
holds": flat records and op lists across a C ABI. The lost "survey 1–4"
documents that the fixture READMEs cite are partly reconstructed in §5.6.

Conventions. Paths are repository-relative to the viewer checkout
(`C:\Users\annes\source\repos\vulkanstorm`, branch `codex/rust-inventory-model`)
unless prefixed `core-rust:` (the `vkstorm-core-rust` checkout, branch `main`
at `1b183c7`). Line numbers are those of the working tree when this chapter
was written. "Inference:" marks a conclusion that is not stated in code or
documentation. Upstream Linden Lab code is "LL"; Firestorm additions carry
`// <FS:…>` markers, RLVa additions `// [RLVa:KB]`, Catznip `// [SL:KB]`;
Vulkanstorm additions are the `vks*`/`VKS_*` names, the `tests/` oracles and
the bug fixes listed in §5.5.

---

## Scope and size

### Per directory

Command (Git Bash, from `indra/`):

```sh
for d in llinventory llprimitive llcharacter llappearance llinventory/tests newview/tests; do
  n=$(find $d -maxdepth 1 -type f \( -name '*.cpp' -o -name '*.h' \) | wc -l)
  l=$(find $d -maxdepth 1 -type f \( -name '*.cpp' -o -name '*.h' \) -print0 | xargs -0 cat | wc -l)
  echo "$d $n $l"; done
```

| directory | C++ files | lines | notes |
|---|---:|---:|---|
| `indra/llinventory` | 40 | 15,589 | of which 5,843 lines are environment settings (`llsettings*`), 2,829 parcel/economy/user relations; see below |
| `indra/llinventory/tests` | 7 | 12,984 | R1 oracle and tool (plus `check_r1_copies.py`, 375 lines Python) |
| `indra/llprimitive` | 36 | 18,151 | |
| `indra/llcharacter` | 40 | 16,083 | |
| `indra/llappearance` | 34 | 13,893 | |
| `indra/newview/tests` | 80 | 41,583 | all viewer tests; the inventory oracles are 35,216 lines of it (C++, Python, CMake, fence lists) |

### Per inventory file group

Command: `cat <files> | wc -l` over each group (Git Bash, `indra/`).

| group | files | lines |
|---|---|---:|
| Inventory record core | `llinventory/llinventory.*`, `llpermissions.*`, `llpermissionsflags.h`, `llsaleinfo.*`, `llcategory.*`, `llfoldertype.*`, `llinventorytype.*`, `llinventorydefines.*`, `llinventorysettings.*`, `lltransactionflags.*`, `lltransactiontypes.h`, `llinvtranslationbrdg.h`, `llcommon/llassettype.*` (23 files) | 6,571 |
| Embedded inventory formats | `llnotecard.*`, `lllandmark.*` | 788 |
| Environment settings (not inventory records) | `llsettingsbase/sky/water/daycycle.*` (8 files) | 5,843 |
| Parcel, economy, user relations (not inventory) | `llparcel*`, `lleconomy.*`, `lluserrelations.*` | 2,829 |
| Viewer model | `newview/llinventorymodel.*`, `llviewerinventory.*`, `llinventoryobserver.*`, `llviewerfoldertype.*` | 11,592 |
| Fetch and AIS | `llinventorymodelbackgroundfetch.*`, `llaisapi.*` | 4,031 |
| Give/offer carriers | `llgiveinventory.*`, `llimprocessing.cpp`, `llviewermessage.cpp` (whole files; the offer code is a fraction) | 12,308 |
| Inventory UI (other chapter) | `llinventorybridge.*`, `llinventorypanel.*`, `llinventoryfunctions.*`, `llinventoryfilter.*`, `llfolderviewmodelinventory.*`, `llinventorygallery*`, `llinventoryitemslist.*`, `llinventoryicon.*`, `llpanelmaininventory.*`, `llfloaterinventory*`, `llsidepanelinventory*`, `fsfloaterpartialinventory.*` (30 files) | 34,988 |
| RLVa inventory | `rlvinventory.*`, `rlvlocks.*` | 3,235 |
| Task inventory UI | `llpanelobjectinventory.*`, `llfloateropenobject.*`, `llvoinventorylistener.*` | 2,757 |

Largest single inventory files (`wc -l`): `llinventorybridge.cpp` 9,636,
`llinventorymodel.cpp` 5,810, `llinventoryfunctions.cpp` 4,404,
`llviewerinventory.cpp` 2,837, `llinventory.cpp` 1,814,
`llaisapi.cpp` 1,864, `llinventorymodelbackgroundfetch.cpp` 1,849,
`llpermissions.cpp` 1,188.

### Fixture and crate size

| item | size | command |
|---|---|---|
| R1 fixtures `indra/llinventory/tests/fixtures` | 8.9 MB, 19 files | `du -sh`, `find -type f \| wc -l` |
| R2–R4b fixtures `indra/newview/tests/fixtures/inventory` | 46 MB, 1,337 files | same |
| crate fixtures `core-rust:crates/vkstorm-inventory/tests/fixtures/{model,ais,cache,give,msgs,offer,task}` | byte-identical to the viewer's (`diff -rq`), except `ais/README.md`, which differs in line endings only | `diff -rq` |
| crate source `core-rust:crates/vkstorm-inventory/src` | 31 files, 120 lines, doc comments only, no code, no tests | `wc -l` |

---

## Inventory: library layer (llinventory)

### Components

| component | file:line | role | origin |
|---|---|---|---|
| `LLAssetType` | `indra/llcommon/llassettype.h:37-146`, table `llassettype.cpp:74-104` | asset type enum (0..59, `AT_UNKNOWN`=255, `AT_NONE`=-1), legacy names, human names, three flags per entry (can link, fetch-by-id allowed, id knowable) | LL |
| `LLInventoryType` | `indra/llinventory/llinventorytype.h:43-74` | inventory type enum (0..28, gaps, `IT_UNKNOWN`=255, `IT_NONE`=-1), `defaultForAssetType`, `inventory_and_asset_types_match` | LL |
| `LLFolderType` | `indra/llinventory/llfoldertype.h:41-108` | folder (preferred) types; `FT_FIRESTORM`=58, `FT_PHOENIX`=59, `FT_RLV`=60 (`<FS:Ansariel>` virtual system folders, `llfoldertype.h:98-102`), `FT_MY_SUITCASE`=100 (`<FS:Ansariel>` OpenSim HG, `:104`); `FT_OUTBOX` made deletable (`llfoldertype.cpp:117`, FS) | LL + FS |
| `LLSettingsType` | `indra/llinventory/llinventorysettings.h:45-50` | `ST_SKY/WATER/DAYCYCLE`, `fromInventoryFlags`, icon and default names via `LLInvTranslationBrdg` (`llinvtranslationbrdg.h`) | LL |
| Item flags | `indra/llinventory/llinventorydefines.h:52-95` | `II_FLAGS_*`: subtype mask `0xff`, landmark visited, object slam/overwrite bits, `II_FLAGS_SHARED_SINGLE_REFERENCE` | LL |
| `LLPermissions` | `indra/llinventory/llpermissions.h`, `.cpp:45-1188` | creator/owner/last owner/group, five masks, group-owned flag; fix, fair-use, accumulate, ownership changes, LLSD/legacy/message forms | LL + FS (OpenSim export, `sIsInOpenSim`) |
| `LLAggregatePermissions` | `llpermissions.cpp:902-1080` | 2-bit tri-state per right (copy/modify/transfer), packed `U8` | LL |
| `LLSaleInfo` | `indra/llinventory/llsaleinfo.cpp:56-344` | sale type (`FS_NOT/ORIGINAL/COPY/CONTENTS`) and price, legacy/LLSD/message forms, `accumulate` | LL |
| `LLCategory` | `indra/llinventory/llcategory.cpp` | object-for-sale categories (packed U32), unrelated to inventory folders | LL |
| Transaction flags | `indra/llinventory/lltransactionflags.cpp` | flags and `build_transfer_message_*` | LL (5 `<FS` markers) |
| `LLInventoryObject` | `indra/llinventory/llinventory.h:49-124`, `.cpp:79-374` | id, parent, thumbnail, favorite, runtime, type, name, creation date | LL |
| `LLInventoryItem` | `llinventory.h:132-230`, `.cpp:376-1336` | + permissions, asset id, description, sale info, inventory type, flags | LL |
| `LLInventoryCategory` | `llinventory.h:241-292`, `.cpp:1341-1770` | + preferred type | LL |
| `LLNotecard` | `indra/llinventory/llnotecard.cpp` | notecard asset: embedded items through the item legacy reader (`:115`) and writer (`:248`), `MAX_SIZE` 65536 | LL |
| `LLLandmark` | `indra/llinventory/lllandmark.cpp` | landmark asset parse | LL |

The settings classes (`llsettingsbase.cpp` and friends) are environment
objects (sky, water, day cycle) that only share the asset type
`AT_SETTINGS`; they are out of scope for the inventory rung and are not in
the crate's module list. `llparcel.*`, `lleconomy.*` and `lluserrelations.*`
live in this directory for historical reasons and are not inventory.

### Record layout

`LLInventoryObject` members (`llinventory.h:115-123`, all `protected`):
`mUUID`, `mParentUUID`, `mThumbnailUUID`, `mFavorite`, `mRuntime`
(SLua script runtime, LL), `mType` (`LLAssetType::EType`), `mName`,
`mCreationDate` (`time_t`). `LLInventoryItem` adds (`:226-232`):
`mPermissions`, `mAssetUUID`, `mDescription`, `mSaleInfo`,
`mInventoryType`, `mFlags`. `LLInventoryCategory` adds `mPreferredType`
(`:289-290`). Every accessor is `virtual` (`llinventory.h:72-111`,
`:166-213`) because `LLViewerInventoryItem` overrides the getters to
follow links (`llviewerinventory.cpp:2446-2650`): for a link (`AT_LINK`,
`AT_LINK_FOLDER`) `mAssetUUID` holds the target id, and the viewer's
`getName`, `getPermissions`, `getDescription`, `getSaleInfo`,
`getInventoryType`, `getFlags`, `getCreationDate`, `getThumbnailUUID`
resolve through `gInventory`.

All objects are `LLRefCount` with `LLPointer` ownership; the copy
operations are explicit (`copyObject`, `copyItem`, `copyCategory`,
`llinventory.cpp:104, 440, 1367`) because `LLRefCount` forbids the copy
constructor's normal use.

### Invariants

**Permission hierarchy, `LLPermissions::fix()`** (`llpermissions.cpp:155-176`):

```
owner    &= base
group    &= owner
next     &= base            // next owner uses base, not owner
everyone &= owner
everyone &= ~PERM_MODIFY
if (!(base & PERM_TRANSFER) && !group_owned) { group &= ~PERM_COPY; everyone &= ~PERM_COPY; }
```

`fixFairUse()` (`:178-190`) adds `PERM_MOVE` to base, `PERM_TRANSFER` to a
base without `PERM_COPY`, and `PERM_MOVE` to a non-zero next-owner mask.
`initMasks(base, owner, everyone, group, next)` (`:68`) is `fixFairUse()`
then `fix()`; `initMasks(EType)` (`:84`) applies the per-inventory-type
restrictions. `accumulate` (`:207-235`) nulls clashing ids, ANDs all five
masks and calls `fix()`. `setOwnerAndGroup` (`:237-288`) changes owner and
group and applies next-owner masks with the `is_atomic` rule (R1 case
`c3.soag.8.A.B.G.0`/`.1`). Mask bits are in `llpermissionsflags.h:40-91`:
`PERM_TRANSFER` 0x2000, `PERM_MODIFY` 0x4000, `PERM_COPY` 0x8000,
`PERM_EXPORT` 0x10000 (FS OpenSim export), `PERM_MOVE` 0x80000,
`PERM_ALL` 0x7fffffff, `PERM_ITEM_UNRESTRICTED` = M|C|T.

The R1 corpora name these as two checked properties for every corrupt
legacy input (`indra/llinventory/tests/fixtures/README.md:149-157`):

- *invariants*: owner ⊆ base, group ⊆ owner, everyone ⊆ owner, next ⊆
  base, everyone without MODIFY, and a no-transfer base that is not group
  owned leaves group and everyone without COPY;
- *no widening*: for each of the five masks no bit beyond what the same
  input without the corrupt line gives, and group ownership never newly set.

Port rule 4 (README `:608-615`) makes no-widening mandatory for the port.

**Item reader fix-ups** (both the legacy reader and `fromLLSD`):

- an inventory type of `IT_NONE`, or one that does not match the asset
  type, is replaced by `defaultForAssetType(mType)`
  (`llinventory.cpp:977-983` legacy, `:1324-1330` LLSD);
- `mPermissions.initMasks(mInventoryType)` always runs at the end
  (`:985`, `:1332`);
- the legacy `sale_info` block's `perm_mask` becomes the next-owner mask,
  with two fix-ups: `PERM_NONE` takes the *owner* mask, and a mask without
  COPY gains TRANSFER (`:827-843`; the same in `fromLLSD`, `:1218-1231`).

**Names**: `correctInventoryName` (`llinventory.cpp:357`) and
`correctInventoryDescription` (`:516`) replace non-standard ASCII and `|`
and trim and truncate the name to `DB_INV_ITEM_NAME_STR_LEN` = 63 bytes
(`llinventory.cpp:357-363`, `llmessage/lldbstrings.h:41`; the give corpus
relies on this, see `give/README.md` "Runtime edges"). The legacy
name/description readers replace `|` and non-standard bytes too
(`:918-958`).

### CRCs

The "CRC" is a 32-bit wrapping sum, not a CRC (comment at
`llinventory.cpp:490`).

| function | file:line | sums |
|---|---|---|
| `LLUUID::getCRC32` | `indra/llcommon/lluuid.cpp:1073-1083` | the four little-endian `U32` words of the id (`<FS>` rewrite of the old pointer cast, equivalent on little-endian hosts) |
| `LLPermissions::getCRC32` | `llpermissions.cpp:129-137` | creator + owner + last owner + group CRCs + base + owner + everyone + group masks. **The next-owner mask and the group-owned flag are not covered.** |
| `LLSaleInfo::getCRC32` | `llsaleinfo.cpp:74-79` | `(U32)price + type * 0x07073096` |
| `LLInventoryItem::getCRC32` | `llinventory.cpp:488-513` | id + parent + permissions + asset + type + inventory type + flags + sale + `(U32)creation date` + **thumbnail** |
| `LLViewerInventoryItem::getCRC32` | `newview/llviewerinventory.cpp:2647-2650` | forwards to the base |

Name and description are not covered. The thumbnail and the last owner are
covered but neither travels in a message `ItemData` block, so a block packed
from an item with a last owner fails its own CRC on unpack; R1 records 321
of 322 `unpack.fresh` verdicts as 0, and no caller checks the verdict
(R1 README `:622-626`).

### Asset-id obfuscation (`shadow_id`)

`MAGIC_ID` is `3c115e51-04f4-523c-9fa6-98aff1034730`
(`llinventory.cpp:73`). An item whose base mask lacks any of M, C, T and
whose asset id is not null writes `shadow_id` = asset XOR `MAGIC_ID`
through `LLXORCipher` (`llmessage/llxorcipher.h:37`) instead of `asset_id`,
in the legacy writer (`llinventory.cpp:1022-1040`) and in `asLLSD`
(`:1087-1100`, with the comment `*TODO: get rid of this. Phoenix
2008-01-30`). Both readers accept either key and XOR `shadow_id` back
(`:845-850`, `:1236-1241`). `exportLegacyStream(…, include_asset_key =
false)` writes a null `asset_id`. The crate has a module for it
(`core-rust:crates/vkstorm-inventory/src/shadow.rs`).

### Serialization forms

| form | write | read | notes |
|---|---|---|---|
| Legacy text, item | `exportLegacyStream` `llinventory.cpp:990-1056` | `importLegacyStream` `:766-985` | `\tinv_item\t0\n\t{` … `\t}`; nested `permissions 0` and `sale_info 0` blocks; `metadata\t<llsd>…</llsd>|` for thumbnail and script runtime |
| Legacy text, object | `:330` | `:212-328` | `inv_object` (task inventory folders) |
| Legacy text, category | `:1646` | `:1520-1644` | `inv_category` |
| Legacy text, permissions | `llpermissions.cpp:737` | `:579-735` | 16,384-byte line buffer, own reader |
| Legacy text, sale info | `llsaleinfo.cpp:81-94` | `:139-205` | returns `has_perm_mask`/`perm_mask` |
| Item LLSD | `asLLSD()` `llinventory.cpp:1059`, `asLLSD(LLSD&)` `:1066-1117` | `fromLLSD(sd, is_new)` `:1119-1336` | keys `item_id parent_id permissions thumbnail favorite script asset_id/shadow_id/linked_id type inv_type flags sale_info name desc created_at`; `type` and `inv_type` as names (an unnamed inventory type as an Integer, `:1102-1110`); `flags` via `ll_sd_from_U32` |
| Category LLSD, dialect 1 | `LLInventoryCategory::asLLSD` `:1383-1403` | — | id under `item_id`, `type` |
| Category LLSD, dialect 2 | `asAISCreateCatLLSD` `:1405-1427` | — | id under `category_id`, `type_default` |
| Category LLSD, dialect 3 | — | `fromLLSD` `:1438-1504` | reads `category_id` only (so dialect 1's `item_id` is dropped), `type` and `type_default` |
| Category LLSD, dialect 4 (cache) | `exportLLSD` `:1670-1686` | `importLLSDMap` `:1688`, `importLLSD(label, value)` `:1699-1748` | id under `cat_id`, `preferred_type` as a name; the viewer subclass adds `owner_id` and `version` (`llviewerinventory.cpp:853-877`) |
| Permissions LLSD | `ll_create_sd_from_permissions`/`ll_fill_sd_from_permissions` `llpermissions.cpp:1163-1181` | `importLLSD` `:787-848`, `ll_permissions_from_sd` `:1183` | `is_owner_group` is written but never read (R1 `c7.perm.derived_group`) |
| Sale LLSD | `asLLSD` `llsaleinfo.cpp:96-113`, `ll_create_sd_from_sale_info` `:328` | `fromLLSD` `:115-137`, `ll_sale_info_from_sd` `:338` | `perm_mask` in `sale_info` is the legacy next-owner carrier |
| Helpers | `ll_create_sd_from_inventory_item/category`, `ll_create_category_from_sd` (`llinventory.h:301-303`) | | header comment: "Not up to date (no favorites, nor thumbnails), for testing purposes" (`llinventory.h:292-300`) |

The cache file and AIS use dialects 4 and 2; the login skeleton uses
`folder_id` (R2 README "Notes for the port"); GenericMessage
`BulkUpdateInventory` folder records are read through `category_id`.

### Legacy reader quirks

The legacy item reader (`llinventory.cpp:766-985`) and the permissions
reader (`llpermissions.cpp:579-735`) are `std::istream::getline` into a
fixed buffer (`MAX_STRING` = `STD_STRING_BUF_SIZE` = 255,
`llcommon/lldefs.h:147,152`; 16,384 for permissions) followed by `sscanf`.
What the recorded corpora pin (R1 C6, R4a set 1):

- **Width**: `" %254s %254s"` (`:787-790`; the object and category readers
  at `:228`, `:1538`); a 254-byte token is read whole,
  a longer one is split (`c6.token.{253,254,255}`).
- **Line cap**: `getline(buffer, 255)` sets failbit on a line of 255 or more
  characters, which ends the read loop silently (`while(success &&
  input_stream.good())`); R4a records 254 parsing and 255 stopping, both at
  the top level of a task inventory file and inside an item
  (`task/README.md:419-427`).
- **Names**: `" %254s%254[\t]%254[^|]"` (`:923`, `:942`); an empty name puts
  `|` in the value, which is then cleared (`:926-930`, comment "IW: sscanf
  chokes"); the writer ends name and description with `|`
  (`:1052-1053`), and a `metadata` line also ends with `|` (`:1014-1016`),
  which is the "orphan `|`" the corpus records.
- **Metadata**: parsed only when the value starts with `<llsd>`
  (`:862`); the writer's comment (`:1002-1003`) notes a 255-character
  limit that the reader enforces by the line cap. `c6.meta.notprefix` and
  `c6.meta.bareMap` pin the prefix test.
- **Per-line parsing (bug 29, Vulkanstorm fix)**: each line is parsed on
  its own; a blank line, a keyword without a value or an unparsable value
  counts as absent (`:782-808`, the same in the object and category
  readers at `:225`, `:1533`; comments "Each line is parsed on its own",
  "a blank line counts as absent"). Block headers (`permissions`,
  `sale_info`) and `metadata` keep their handling.
- **Mask exception**: in the permissions reader an unparsable mask line
  sets that mask to `PERM_NONE`, and `creator_mask`'s `fixFairUse()` runs
  only after a parsed mask (`is_legacy_mask_keyword`,
  `llpermissions.cpp:572-577`).
- **Initialised targets (bug 38)**: `S32 date = 0` (`llinventory.cpp:962`)
  and the permissions reader's `mask`/`uuid_str` (`llpermissions.cpp:589-594`).
- **Overflow**: `%d`/`%x` keep the low 32 bits on MSVC and glibc
  (`flags 123456789` → `23456789`, `creation_date 2147483648` →
  -2147483648, `sale_price 3000000000` → -1294967296 clamped to 0);
  undefined by the C standard, recorded as plain values (R1 runtime edge 5).
- **UUID leniency**: `LLUUID::set` accepts other characters in the hyphen
  slots, mixed case and a 35-character form (C6 `c6.uuid.*`, C7
  `c7.uuidstr.*`); the crate reuses `vkstorm_llsd::text::uuid_set`.
- **`asset_id`/`shadow_id` order**: the later line wins.
- **Dead Firestorm helpers**: `splitCacheDescOrName` and `splitCacheLine`
  (`llinventory.cpp:687-764`, `<FS:ND>`) are defined but called nowhere
  (`grep -rn splitCacheLine indra --include=*.cpp --include=*.h` finds only
  the definitions). Inference: remnants of a faster cache reader from the
  text-cache era; the port can drop them.

Other legacy-stream users outside `llinventory` share these readers:
`LLWearable::importStream`/`exportStream` call
`LLPermissions::importLegacyStream` and `LLSaleInfo::importLegacyStream`
(`indra/llappearance/llwearable.cpp:111,117,290,315`), `LLNotecard`
(`llnotecard.cpp:115,248`), and the task inventory file loader
(`newview/llviewerobject.cpp:3661,3667`). Porting the readers therefore
changes wearable asset parsing too (§7), which no R-corpus records
directly. Inference: R1's C6 permission and sale-block cases cover the
code path, not the wearable file shape.

### Message blocks

| function | file:line | blocks/fields |
|---|---|---|
| `LLInventoryItem::packMessage` | `llinventory.cpp:618-635` | `ItemID FolderID` + permissions (`LLPermissions::packMessage` `llpermissions.cpp:525`) + `AssetID Type(S8) InvType(S8) Flags` + sale (`LLSaleInfo::packMessage` `llsaleinfo.cpp:224`) + `Name Description CreationDate(S32) CRC` |
| `LLViewerInventoryItem::packMessage` | `newview/llviewerinventory.cpp:576-593` | the same with `TransactionID` in place of `AssetID`; used by `UpdateInventoryItem` and `UpdateTaskInventory` senders |
| `LLInventoryItem::unpackMessage` | `llinventory.cpp:638-683` | reads the block, `initMasks(inv_type)`, `unpackMultiMessage` for sale, `replaceNonstandardASCII` on name and description, returns `local_crc == remote_crc`; does not touch last owner, thumbnail, favorite or runtime |
| `LLInventoryCategory::packMessage`/`unpackMessage` | `llinventory.cpp:1429, 1506` | folder blocks; the viewer's `LLViewerInventoryCategory::packMessage` (`llviewerinventory.cpp:675-682`) is what senders use |
| `LLAggregatePermissions::packMessage`/`unpackMessage` | `llpermissions.cpp:1055, 1060` | one `U8` per right |
| `LLSaleInfo::unpackMessage`/`unpackMultiMessage` | `llsaleinfo.cpp:242, 252` | `ObjectProperties` and item blocks |
| `LLPermissions::packMessage()`/`unpackMessage(LLSD)` | `llpermissions.cpp:506, 539` | hyphenated LLSD form with no caller (R1 C3) |

The message layer itself is Rust since rung three (the template builder and
reader); R1's C9 corpus drives it through the real `LLMessageSystem` and
`scripts/messages/message_template.msg`.

### Firestorm additions in `llinventory`

`grep -c '<FS' indra/llinventory/*.{cpp,h}`: `llinventory.cpp` 3 (the dead
helpers, FIRE-36028 OpenSim permissions in `fromLLSD`, `:1133-1140`),
`llpermissions.cpp` 5 and `.h` 4 (`sIsInOpenSim`, `allowOpenSimExportBy`,
`mask_to_string(…, isOpenSim)` that hides the export X outside OpenSim,
`:1090-1160`), `lltransactionflags.cpp` 5, `llfoldertype.*` 4,
`llpermissionsflags.h` 1, `llinventorysettings.cpp` 1. FIRE-36028 matters to
the port: in OpenSim `fromLLSD` does not reset the permissions before
reading (R1 `fromllsd.opensim`, crate feature `opensim`).

---

## Inventory: viewer layer (newview)

### Viewer subclasses and why the containers stay C++

`LLViewerInventoryItem : LLInventoryItem, boost::signals2::trackable`
(`newview/llviewerinventory.h:52`) adds the public `mIsComplete`,
`mTransactionID`, `mIsRunning`, `mIsFaulted` (`:168-172`).
`LLViewerInventoryCategory : LLInventoryCategory` (`:184`) adds the
protected `mOwnerID`, `mVersion` (`VERSION_UNKNOWN` = -1), `mDescendentCount`
(`DESCENDENT_COUNT_UNKNOWN` = -1), `mFetching` (`FETCH_NONE/NORMAL/
RECURSIVE/FAILED`) and `mDescendentsRequested` (an `LLFrameTimer`,
`:255-259`), with `friend class LLInventoryModel` (`:251`).

The subclasses read and write the base classes' protected fields directly,
not through accessors. Counts of whole-word uses in
`newview/llviewerinventory.cpp` (`grep -cw <field>`): `mUUID` 14,
`mParentUUID` 7, `mName` 7, `mType` 6, `mAssetUUID` 6, `mPermissions` 5,
`mThumbnailUUID` 4, `mInventoryType` 2, `mPreferredType` 2,
`mCreationDate`, `mDescription`, `mSaleInfo`, `mFlags` 1 each. Examples:
`updateServer` reads `mPermissions.getOwner()` (`:432, 503, 521, 537`),
`packMessage` packs `mPermissions`, `mFlags`, `mSaleInfo`, `mDescription`
(`:580-589`), the link getters resolve `mAssetUUID` through `gInventory`
(`:2573-2582, 2704-2719`). The model holds the objects by `LLPointer` in
maps and child vectors (below), the UI holds them by raw pointer and
`LLPointer` across 145 files (§8), and `trackable` ties boost signal
lifetimes to the item. This is why the crate doc comment states that "the
model containers stay C++" and the crate "works on flat records and views
of those objects" (`core-rust:crates/vkstorm-inventory/src/lib.rs:16-20`).

### `LLInventoryModel` containers

`newview/llinventorymodel.h`:

| member | line | type |
|---|---|---|
| `mCategoryMap`, `mItemMap` | 217-218 | `std::map<LLUUID, LLPointer<LLViewer…>>` |
| `mParentChildCategoryTree`, `mParentChildItemTree` | 222-223 | `std::map<LLUUID, cat_array_t*>` / `item_array_t*` (heap-allocated child vectors) |
| `mBacklinkMMap` | 227-228 | `std::multimap<target, link>` (broken links supported) |
| `mModifyMask`, `mChangedItemIDs`, `mAddedItemIDs` and the `…Backlog` copies | 643-651 | pending notification batch; the backlog is used while `notifyObservers` runs |
| `mPossiblyBrockenLinks`, `mLinksRebuildList` | 652-654 | broken link repair |
| `mTransactionId` | 657 | `[SL:KB]` Catznip UI-notifications patch |
| `mObservers` | 671-672 | `std::set<LLInventoryObserver*>` (pointer order) |
| `mHttpRequestFG/BG`, options, headers, policy | 701-705 | legacy HTTP transport |
| `mLastItem` | 402 | one-entry lookup cache |
| locked-folder maps | 398-416 | `<FS:Ansariel>` FIRE-29342 protected folders |

Key methods (`newview/llinventorymodel.cpp`): `getObject/getItem/getCategory`
`:686-733`, `getDirectDescendentsOf` `:748-779`, `collectDescendentsIf`
`:1377`, `updateItem` `:1595-1801`, `updateCategory` `:1826`,
`moveObject` `:1902`, `changeItemParent`/`changeCategoryParent`
`:1948/1989`, `onItemUpdated`/`onCategoryUpdated` `:2055/2096`,
`onDescendentsPurgedFromServer` `:2125`, `onObjectDeletedFromServer`
`:2206`, `deleteObject` `:2237`, `idleNotifyObservers` `:2353`,
`notifyObservers` `:2382`, `addChangedMask` `:2428`,
`fetchDescendentsOf` `:2515`, `cache` `:2572`, `addCategory` `:2622`,
`addItem` `:2683`, `accountForUpdate` `:2795-2862`, `loadSkeleton`
`:2913-3268`, `buildParentChildMap` `:3270-3576`,
`createCommonSystemCategories` `:3650`, `loadFromFile` `:3699-3815`,
`saveToFile` `:3817-3892`, `registerCallbacks` `:3894`, the UDP handlers
`:3930-4447`, `removeItem/Category/Object` `:4489-4582`,
`checkTrashOverflow` `:4584`, `validate` `:4985-5514`, the FS UDP-fetch
handlers `processFetchInventoryReply`/`processInventoryDescendents`
`:5536/5543`.

### Cache

- Name: `<cache dir>/<owner id>.inv.llsd` on the production grid,
  `<owner id>.<grid>.inv.llsd` elsewhere (`llinventorymodel.cpp:107-108`,
  `getInvCacheAddres` `:2544`, with `<FS:Ansariel>` OpenSim and
  illegal-character fixes `:2550, 2562`), gzipped as `.gz`.
- Format (`saveToFile` `:3817-3892`, `loadFromFile` `:3699-3815`): a
  4-byte big-endian version (`sCurrentInvCacheVersion` = 5, `:97`;
  `htonl`/`ntohl`), then binary LLSD `{categories: [exportLLSD…], items:
  [asLLSD…]}`. A version mismatch or parse failure marks the cache
  obsolete.
- Save (`cache()` `:2572-2620`): write `<name>` to a temporary file, gzip
  into `<name>.gz`, remove the temporary; a failed save keeps the old `.gz`
  (Vulkanstorm fix a92777dc18, "Do not replace the inventory cache after a
  failed save"; R2 C5).
- Load at login: `loadSkeleton(options, owner_id)` `:2913` is called first
  for the library, then for the agent (`newview/llstartup.cpp:2688-2707`,
  `STATE_INVENTORY_SKEL`); it gunzips, loads, reconciles cached folder
  versions with the skeleton's, and discards items whose folder version
  differs. `<FS:Zi>` `<agent>_DELETE_INV_GZ` marker files purge both caches
  (`:2918-2950`; bug 27 made the library purge happen before it loads,
  7f270e38d0). A corrupt `.gz` is discarded and removed (bug 20, 2d9238ed01;
  the plain-file follow-up dabe25973f).
- Logout save: the inventory part of `LLAppViewer::disconnectViewer` (R2
  freezes a copy in `tests/llinventorymodel_parity_generic.cpp`).

### Login sequence (inventory part)

`newview/llstartup.cpp`: `STATE_INVENTORY_SEND` (`:2625`, library root and
owner from `inventory-lib-root`/`inventory-lib-owner`, `:2654-2666`),
`STATE_INVENTORY_SKEL` (`:2682`, the two `loadSkeleton` calls),
`STATE_INVENTORY_SEND2` (`:2714`: `buildParentChildMap`, the
`InventoryUnusable` check, `LLInventoryModelBackgroundFetch::start()`,
`createCommonSystemCategories`), `STATE_INVENTORY_CALLBACKS` (`:2851`: the
COF id, the COF marked `FETCH_RECURSIVE`), and `findLostItems()` in
`STATE_STARTED` (`:3148`). R3b freezes all of these as copies.

### Fetch: background fetch, AIS and legacy

`LLInventoryModelBackgroundFetch` (`newview/llinventorymodelbackgroundfetch.cpp`,
LL singleton): queues `addRequestAtFront/Back` `:261/274`, `start`
`:287`, `scheduleFolderFetch`/`scheduleItemFetch` `:379/406`,
`fetchFolderAndLinks` `:430`, `fetchCOF` `:459`, `findLostItems` `:501`,
the idle entry `backgroundFetch` `:541`, and two schedulers:

| path | entry | limits | transport |
|---|---|---|---|
| AIS3 | `bulkFetchViaAis()` `:904`, per entry `:1015` | in flight `clamp(PoolSizeAIS - 1, 1, 50)` (`:916`); dispatch budget 1 s during login, 6 ms after, read through `LLTimer::getTotalSeconds()` at three points; `BatchSizeAIS3` subset batches | `AISAPI` commands on the coprocedure pool "AIS" |
| Legacy capabilities | `bulkFetch()` `:1220` | batch 10 (`:1236`), 12 outstanding (`:1237`) plus 4 (hard-coded, R3b README), `MAX_FETCH_RETRIES` 10 (`:182`, `<FS:ND>`) | `FetchInventoryDescendents2`/`FetchLibDescendents2` POSTs through `LLInventoryModel::requestPost` and `BGFolderHttpHandler` (`:1480-1838`) |
| UDP | `FetchInventoryDescendents` / `FetchInventory` | — | `#ifdef OPENSIM` for the adaptive loop; single UDP fallbacks also on Second Life without caps (R3b s13) |

`AISAPI` (`newview/llaisapi.cpp`): `isAvailable` `:64` (true on Second
Life whatever `FSUseAis3Api` says, `:68`; an `<FS:Ansariel>` debug setting
elsewhere), the command functions `CreateInventory` `:104` … `FetchOrphans`
`:758`, `EnqueueAISCommand` `:791` with `MAX_SIMULTANEOUS_COROUTINES` 2048
and a postponed queue drained by `onIdle` `:818`, the coroutine
`InvokeAISCommandCoro` `:856`, depth clamp `MAX_FOLDER_DEPTH_REQUEST` 50
(`:60`), URL length cap 2000 (`:656`). `AISUpdate` (`:1050-1864`) parses a
reply (`parseMeta`, `parseContent`, `parseItem`, `parseLink`,
`parseCategory`, `parseEmbedded*`, `:1122-1641`) and applies it in
`doUpdate()` (`:1643`), yielding through `checkTimeout()` (`:1096`) when the
per-update 8 ms task timer or the per-frame 10 ms shared batch timer
(`llaisapi.h:138-139`) is spent, and flushing observers every 50 changes
(`MAX_UPDATE_BACKLOG`, `:1693`). FS markers: FIRE-33337 rename workaround
(`:1375`), FIRE-33455 versions only rise (`:1678, 1803, 1822`), FIRE-34169
`#RLV` subfolder creation (`:1013`), `COPYINVENTORY` restored (`:987`).

### Observers, callbacks, notifications

`LLInventoryObserver` (`newview/llinventoryobserver.h:42`) receives a
`changed(U32 mask)` with bits `LABEL 1, INTERNAL 2, ADD 4, REMOVE 8,
STRUCTURE 16, CALLING_CARD 32, GESTURE 64, REBUILD 128, SORT 256, CREATE
512, UPDATE_CREATE 1024, UPDATE_FAVORITE 2048` (`:50-64`). Subclasses:
`LLInventoryFetchObserver` and its item, descendents and combo variants
(`:76-165`), `LLInventoryAddItemByAssetObserver` `:169`,
`LLInventoryAddedObserver` `:196`, `LLInventoryCategoryAddedObserver`
`:213`, `LLInventoryCompletionObserver` `:236`,
`LLInventoryCategoriesObserver` `:258`. `notifyObservers()` delivers the
union mask and the changed/added id sets once per call; observers are called
in `std::set` pointer order.

UDP completion callbacks go through `LLInventoryCallbackManager
gInventoryCallbacks` (`llviewerinventory.cpp:1064` `registerCB`, `:1201`):
a 32-bit `CallbackID` travels in `CreateInventoryItem`,
`CopyInventoryItem` and friends (`:1289, 1371, 1509`) and comes back in
`UpdateCreateInventoryItem`/`BulkUpdateInventory`; since bug 24
(96b7a77115, 30c17e45e8) each item block fires its own id. AIS uses
`AISAPI::completion_t` (a `std::function<void(const LLUUID&)>`).

### Threading and coroutines

The model is main-thread only (R2 README: "Survey 2 risk 10 (thread
misuse) has no case: the model is single-threaded"). Asynchrony is
cooperative: AIS commands are coroutines on `LLCoros` run by
`LLCoprocedureManager` pool "AIS", which suspend in `HttpCoroutineAdapter`
and in `llcoro::suspendUntilNextFrame` inside `AISUpdate::checkTimeout`;
`LLCoros::checkStop()` after that yield is the shutdown point. The legacy
path uses `LLCore::HttpHandler` callbacks pumped by
`handleResponses()` only inside `bulkFetch()` (R3b finding 2).
`createNewCategoryCoro` (`llinventorymodel.cpp:1209`) and the task
inventory capability fetch (`llviewerobject.cpp:3121-3260`) are coroutines
too. The consequence the corpora pin: a reply parsed before a yield can be
applied after other main-thread code changed the model (R3a set 13,
findings 3 and 4).

### UDP inventory messages

`registerCallbacks` (`llinventorymodel.cpp:3894`) installs handlers for
`UpdateCreateInventoryItem` `:3930` (via `messageUpdateCore` `:3962`),
`RemoveInventoryItem` `:4057`, `RemoveInventoryFolder` `:4098`,
`RemoveInventoryObjects` `:4116`, `SaveAssetIntoInventory` `:4135`
(RLVa hook `:4169`), `BulkUpdateInventory` `:4183` (RLVa `#RLV/~` rename
guard `:4231-4244`), `MoveInventoryItem` `:4390`, and, `<FS:Ansariel>
[UDP-Msg]`, `InventoryDescendents` and `FetchInventoryReply` `:5536-5620`.
The GenericMessage/LargeGenericMessage form of `BulkUpdateInventory` is
dispatched from `newview/llviewergenericmessage.cpp`. On Second Life with
AIS available every move (`MoveInventoryItem`, `MoveInventoryFolder`),
`CreateInventoryItem`, `CopyInventoryItem`, `ChangeInventoryItemFlags` and
all inbound inventory messages remain UDP (R3b "Rule PRE_D2_SL_FALLBACKS").

### Give and offer flows

- Give: `LLGiveInventory` (`newview/llgiveinventory.cpp`):
  `isInventoryGiveAcceptable` `:125`, `isInventoryGroupGiveAcceptable`
  `:157`, `doGiveInventoryItem` `:191`, `doGiveInventoryCategory` `:228`,
  the no-copy confirmation handlers `:374, 504`, `commitGiveInventoryItem`
  `:428` and `commitGiveInventoryCategory` `:561`; an
  `ImprovedInstantMessage` with a binary bucket (17 bytes per entry, capped
  at `1200 - strlen(name)`). 26 RLVa references (`@share`, hidden names).
- Offer: `LLIMProcessing::processNewMessage`
  (`newview/llimprocessing.cpp`, dialogs `IM_INVENTORY_OFFERED` 4,
  `IM_TASK_INVENTORY_OFFERED` 9, `IM_GROUP_NOTICE` 32,
  `IM_GROUP_NOTICE_REQUESTED` 37; `:826, 858`) builds an `LLOfferInfo`
  (`newview/llviewermessage.cpp`: `asLLSD` `:1698`, `fromLLSD` `:1717`,
  `sendReceiveResponse` `:1722`, `send_decline_response` `:1803`,
  `handleRespond` `:1831`, `inventory_offer_callback` `:1853`,
  `inventory_task_offer_callback` `:2136`, `forceResponse` `:8860`),
  throttled by `check_offer_throttle` `:1297`, opened by
  `open_inventory_offer` `:1403`. Offline offers arrive through the
  ReadOfflineMsgs capability coroutine. Firestorm and RLVa density is high:
  `llviewermessage.cpp` has 145 `<FS` and 137 RLVa markers,
  `llimprocessing.cpp` 60 and 69 (`grep -c`). FS settings in the offer
  policy: `FSUseLegacyInventoryAcceptMessages`,
  `FSDismissGroupNoticeAttachmentsToTrash`; RLVa: give-to-`#RLV`
  (`RlvGiveToRLVTaskOffer`, `RlvGiveToRLVAgentOffer`,
  `newview/rlvinventory.h:126-176`).
- Drag and drop: `LLToolDragAndDrop::handleGiveDragAndDrop` and the
  `dad3d*` functions (`newview/lltooldraganddrop.cpp`, 3,586 lines),
  `willObjectAcceptInventory`, `move_inv_category_world_to_agent`.

### Task (object contents) inventory

On `LLViewerObject` (`newview/llviewerobject.cpp`), state
`mInventory`, `mInventorySerialNum`, `mExpectedInventorySerialNum` (S16),
`mInvRequestState` (`INVENTORY_REQUEST_STOPPED/PENDING/WAIT/XFER`,
initialised `:306`), `mInvRequestXFerId`, `mInventoryDirty`,
`mPendingInventoryItemsIDs`, `mInventoryCallbacks`. Functions:
`deleteInventoryItem` `:2826`, `doUpdateInventory` `:2845` (the owner-guess
for local updates), `saveScript` `:2942`, `moveInventory` `:2993`,
`requestInventory` `:3081`, `fetchInventoryFromServer` `:3121` (UDP
`RequestTaskInventory` or the `RequestTaskInventory` capability coroutine,
`:3161-3260`), `processTaskInv` `:3437` (ReplyTaskInventory, then an xfer
of the task inventory file), `processTaskInvFile` `:3562`,
`loadTaskInvFile` `:3630` (legacy text, `push_front`, at most
`MAX_INV_FILE_READ_FAILS` = 25 unknown tokens, `:156`; an `inv_object` is
renamed `Contents`), `loadTaskInvLLSD` `:3699` (the capability reply,
`unpackMessage(LLSD)` per entry, `:3722`). FS: 39 `<FS` and 18 RLVa markers
in the file; `<FS:ND>` guards `AT_NONE` objects that `loadTaskInvFile` can
create (`:3982`). Vulkanstorm fixes: bugs 5, 6, 32, 33 and `146df7efa6`
(§5.5).

### Library

The Linden Library is a second inventory owned by
`ba2a564a-f0f1-4b82-9c61-b7520bfcd09f` (`ALEXANDRIA_LINDEN_ID` on Second
Life; `<FS:Beq>` reads the owner from the login on OpenSim,
`llinventorymodel.cpp:2945`, `llinventorymodelbackgroundfetch.cpp:1311`).
It shares the model maps, has its own cache file and skeleton, and is
fetched over `LibraryAPIv3`/`FetchLibDescendents2`. R1 commits a real
Library cache (490 categories, 5,585 items) as C10; R2 and R3b reuse it.

### RLVa inventory hooks

`RlvInventory` (`newview/rlvinventory.h:37`, an `LLInventoryObserver`) owns
the `#RLV` shared root (`RLV_ROOT_FOLDER`, `rlvdefines.h:80`) and the
`#RLV/~` put-inventory prefix (`:87`); `rlvlocks.*` lock attachments and
wearables. Hooks into the data path: the `BulkUpdateInventory` rename guard
(above), `SaveAssetIntoInventory` (`rlv.saved_asset` journal line in R2),
collect functors (`llinventorymodel.cpp:1376-1464`), give/offer `@share`
and hidden names, `willObjectAcceptInventory` with locked attachments and
`@unsit`/`@sittp` (R4b `will_accept_sweep`).

### Firestorm and other additions on the data path

| addition | where | port relevance |
|---|---|---|
| FIRE-29342 protected/locked folders | `llinventorymodel.h:398-416`, `.cpp:465, 482, 4623, 4924` | change*Parent refuses moves; R2 C3 `change*Parent with the FS locked folders` |
| ReplaceWornItemsOnly | `llinventorymodel.h:49-52, 393-410`, `.cpp:74, 4722` | wear helpers |
| `DELETE_INV_GZ` cache purge | `llinventorymodel.cpp:2918` | R2 C2 `marker_*` |
| `[UDP-Msg]` fetch replies | `llinventorymodel.h:752-755`, `.cpp:3922, 5534` | R2 msgs `udp_fetch_replies` |
| OpenSim suitcase, validation logging | `llinventorymodel.cpp:641, 5002-5475` (FIRE-31674, FIRE-31634) | not recorded (`OPENSIM` off) |
| FIRE-21247 trash warning, FIRE-36458 gesture link | `:4462, 4526` | UI policy |
| FIRE-21376 legacy fetch on OpenSim | `llinventorymodelbackgroundfetch.cpp:1248-1829` | OpenSim profile scenarios `o..` |
| FT_FIRESTORM/PHOENIX/RLV virtual folders | `llfoldertype.h:98-102` | type table |
| `fsfloaterpartialinventory.*` | UI | none |
| Catznip transaction id on notifications `[SL:KB]` | `llinventorymodel.h:656-658` | R2 `batch tid=` |

`grep -c '<FS'` per file: `llinventorymodel.cpp` 31, `llviewerinventory.cpp`
25, `llinventorymodelbackgroundfetch.cpp` 8, `llaisapi.cpp` 7,
`llinventoryobserver.cpp` 3, `llgiveinventory.cpp` 7,
`llinventoryfunctions.cpp` 23, `llinventorybridge.cpp` 112,
`llviewerobject.cpp` 39, `llinventorypanel.cpp` 33. No `vks`/`VKS` marker
exists in the production inventory sources (`grep -rni vks` over them
finds nothing); Vulkanstorm changes there are plain commits (§5.5).

---

## Inventory: oracle phase as built

### 5.1 The recorders

The rung is called "vkstorm-rust rung seven" throughout the fixtures. Each
recorder freezes the decision code it covers as verbatim C++ copies (or
compiles the live file with stubs), drives it from a deterministic corpus,
writes fixtures in dump mode (a dump never exits 0) and replays them in
normal mode. The six replay tests are in CI's focused test step
(`.github/workflows/build.yaml:223-225`).

| recorder | covers (C++) | replay test (CMake) | code under test | corpora (fixture dir) | copy check |
|---|---|---|---|---|---|
| **R1** data model | `indra/llinventory` records, `llassettype`, the viewer subclass functions that touch the records | `INTEGRATION_TEST_llinventory_parity` (`indra/llinventory/CMakeLists.txt:81-106`); tool `llinventory_tool` (`:108-113`) with `dump`, check and `bench` modes | live classes against frozen copies `llinventory/tests/llinventory_oracle.{h,cpp}` (1,695 + 4,973 lines), in process and against the files | C1–C10, `llinventory/tests/fixtures/` (19 files, 8.9 MB) | `check_r1_copies.py` (post-build) |
| **R2** model and cache | `llinventorymodel.cpp`, `llviewerinventory.cpp`, `llinventoryobserver.cpp`, `llviewerfoldertype.cpp` compiled live; UDP handlers; GenericMessage handlers and `disconnectViewer` inventory part as copies | `INTEGRATION_TEST_llinventorymodel_parity` (`newview/tests/inventory/r2.cmake`) | real model on stubs that journal every outward call (`llinventorymodel_parity_stubs.*`) | C1–C5 + real library, `fixtures/inventory/cache/`; set 8, `fixtures/inventory/msgs/` | `check_r2_copies.py` (inside the test) |
| **R3a** AIS update | `llaisapi.cpp` compiled verbatim with renamed `LLTimer`, `LLCoros`, `LLCoreHttpUtil`, `LLCoprocedureManager`; viewer-object members as copies; `accountForUpdate` copy | `INTEGRATION_TEST_llaisupdate_parity` (`r3-update.cmake`) | recording skeleton model (`llaisupdate_parity_stubs.*`) | 18 sets, 140 cases, `fixtures/inventory/ais/update/` | `check_r3a_copies.py` + `check_r3a_fingerprints.txt` + `check_r3a_fence.txt` |
| **R3b** fetch wire | real `llaisapi.cpp`, `llinventorymodelbackgroundfetch.cpp` (one class renamed each), model, observers, coroutines, coprocedures, llcorehttp | `INTEGRATION_TEST_llinvfetch_parity` (`r3-fetch.cmake`) | against `tests/vksinventory_mock_server.py` (992 lines) | 50 scenarios, `fixtures/inventory/ais/fetch/` | `check_r3b_copies.py` (startup and COF copies) |
| **R4a** task inventory | `llviewerobject.cpp` task-inventory regions and `LLViewerInventoryItem` members as copies | `INTEGRATION_TEST_llinventory_task_oracle` (`r4-task.cmake`) | through `TaskInventoryBoundary` (`llinventory_task_oracle_boundary.h`) | sets 1–4, `fixtures/inventory/task/` | `check_r4a_copies.py` (test 8) |
| **R4b** offers and giving | 49 compiled regions in `llinventory_offer_oracle_copies.{h,cpp}`; text guards `…_guards.h` | `INTEGRATION_TEST_llinventory_offer_oracle` (`r4-offer.cmake`) | through `r4b_boundary::Decisions` (`…_boundary.h`), fakes in `…_fakes.*` | `fixtures/inventory/offer/`, `fixtures/inventory/give/` | `check_r4b_copies.py` (last test) |

### 5.2 What each recorder records

**R1** (`indra/llinventory/tests/fixtures/README.md`): every form byte for
byte and every read field by field with the stream position and state.

| corpus | content | size |
|---|---|---|
| C1 | type tables over -129..256 and `INT_MIN/MAX`; `inventory_and_asset_types_match` over (-1..255)²; 138 reverse lookups; `getTypeNames`; `fromInventoryFlags`; `LLCategory` (top byte 0..6); transaction flags; `mask_to_string` | 1,280 cases |
| C2 | name/description normalisers over byte values, lengths, whitespace, UTF-8, `|` | 1,234 |
| C3 | permissions: `fix` over mask edges, `initMasks`, `accumulate`, `setOwnerAndGroup`, `deedToGroup`, `set*Bits`, queries, CRC, legacy and LLSD round trips, `LLAggregatePermissions`; random stream `c3r` 200,000 cases; exhaustive sweeps `c3x.fix.alpha5/4` (2^25 and 2^20 × 2) and `c3x.initmasks5` (2,048,000) as SHA-256 digests in chunks of 2^20 | 3,328 + 399 samples |
| C4 | sale info: types × prices, CRC, forms, `accumulate` incl. overflow, `fromLLSD` matrix; `c4r` 100,000 | 394 + 299 |
| C5 | items (every asset type, inventory type, masks × assets incl. `MAGIC`, metadata, flags, sale, names, dates, links) with ~20 derived fields each; 972-case type matrix; `c5r` 20,000; categories in all four LLSD dialects; `LLInventoryObject` | 280 + 972 + 19 + 151 |
| C6 | malformed legacy text through the item, object, category, permissions, sale readers and 22 notecards; corrupt-input invariant and no-widening checks | 293 |
| C7 | `LLInventoryItem::fromLLSD`: 20 keys × 48 LLSD values × `is_new`, plus OpenSim, asset-key subsets, thumbnails, script, `perm_mask` forms, UUID strings, permission keys, object reuse | 2,129 |
| C8 | `LLInventoryCategory::fromLLSD`/`importLLSD(Map)` and the viewer subclass: 13 keys × 48 values | 656 |
| C9 | message blocks through the real `LLMessageSystem`: `UpdateInventoryItem`, `UpdateTaskInventory`, `BulkUpdateInventory`, raw `ItemData` with CRCs, `UpdateInventoryFolder`, aggregate permissions (256 bytes), `ObjectProperties` category and sale | 832 |
| C10 | the public Linden Library cache: every category and item decoded, re-encoded byte-identically, fields | 6,075 |

Determinism: patterned UUIDs `U(tag)`, xorshift64* with stated seeds and
draw orders, `RT(win=…;linux=…;crate=…)` for the one C-runtime-dependent
value class (LLSD String→Integer of `"0x10"`: 16 on MSVC, 0 on libstdc++,
16 for the crate). (Superseded by the owner decision of 2026-10-28: the crate
gives the per-platform value too, and `crate=` is no longer read; see
`indra/llinventory/tests/fixtures/README.md`, *Port rules* 1.)

**R2** (`fixtures/inventory/cache/README.md`, `msgs/README.md`):

| corpus | content |
|---|---|
| C1 `c1_cache_files.txt`, `c1/` | `cache()` output for `standard`, `types`, `incomplete`, `history` and three random worlds: uncompressed bytes, notation, decode, re-encode, directory listing |
| C1 `c1_realistic.txt` | 8,000 folders, 100,000 items (57.8 MB cache): size, SHA-256, decode digests per 10,000 records, login from the cache |
| C2 `c2_load.txt`, `c2/<case>/` | 64 login load-and-discard cases: skeletons, cache directory before/after, `loadSkeleton` journal, `buildParentChildMap`, `validate`, system categories, logout save |
| C3 `c3_mutations.txt`, `c3_random.txt` | 26 API sequences and 40 random sequences × 40 mutations: every journal line (messages, batches, callbacks, AIS calls, fetches) and changed model lines |
| C4 `c4_validation*.txt` | `validate()` over 40 constructed states; QA-mode draws per C++ library (`mt19937` on MSVC, `minstd_rand0` on libstdc++) |
| C5 `c5_write_safety*.txt` | cache save under failures (temp file a directory, missing temp dir; Linux: `RLIMIT_FSIZE` mid-write) |
| real | `library_real.txt`: the Library cache as a skeleton beside the standard agent inventory |
| set 8 `msgs/set8_handlers.txt` | the UDP handlers: `UpdateCreateInventoryItem`, `BulkUpdateInventory` (UDP and GenericMessage/LargeGenericMessage, event queue), remove messages, `SaveAssetIntoInventory`, `MoveInventoryItem`, `InventoryDescendents`/`FetchInventoryReply`, `create_and_copy_with_ais` |

**R3a** (`ais/update/README.md`): op lists of `AISUpdate` over 140 cases
(389 steps, 153 command-function calls, 11 hooks, two runs each: `never`,
timers never expire, mandatory and byte-exact; `sched`, an injected clock,
relaxable). Each op is a model call, a query (`q.getCategory`,
`q.getItem`, `q.getVersion`, `q.findCategoryUUIDForType`, …), a yield, a
message, an HTTP request, a notification or a callback. Sets 1–11 follow
"survey 3 §8.1"; 12 random; 13–18 added after audits V0/V1 (interleaving,
folder links, partial maps, depth cut, loose typing, the 16 command
functions); limits at limit−1/limit/limit+1 (`MAX_UPDATE_BACKLOG` 50, 8 ms,
10 ms, depth 50, 2,048 coroutines). The large case s10a (2,000 folders,
20,000 items, 45,411 ops) is committed as digests.

**R3b** (`ais/fetch/README.md`): request-by-request wire records of 50
scenarios against a mock AIS and legacy-capability server: request heads
and bodies, statuses, the scheduler state after each change, observer
batches, messages, callbacks, model dumps. Four recording modes (SEQ,
PHASED, LOCKSTEP, CONC) make concurrency deterministic. Scenario table at
`ais/fetch/README.md:302-337`; the request count is the first line of every
scenario ("a wrong port multiplies server load", `:11-13`).

**R4a** (`task/README.md`): set 1 task inventory files (named cases,
`scrub.txt`, 3,000 seeded mutated files as digests per 97); set 2
`RequestTaskInventory` capability replies (named, 2,000 seeded); set 3
serial and request-state traces (every sequence of 1–4 events over two
alphabets and four configurations: 66, 1,090, 18,018, 298,114 traces);
set 4 the UDP datagrams the viewer sends, captured on a loopback receiver,
and `createInventoryItem`'s POST bodies.

**R4b** (`offer/README.md`, `give/README.md`): offers (responses, 129
incoming bucket encodings and 6,000 seeded, offline offers, `LLOfferInfo`
LLSD, forced-response indexing, throttle), the offer policy cross product
(589,824 cases, 1,804 outcome classes), the response table (43,008 cases),
present-object policy (512), 51 named cases; gives (72 item cases, 38 folder
cases, predicates 6,912 + rule cases, `will_accept` 2,562 + 102,400 sweep,
`world_to_agent` 1,361, share 13, drag-and-drop 297, seeded pack/folder/
predicate sweeps). The test also packs 3,000 seeded messages through live
`pack_instant_message` and its frozen copy.

### 5.3 How they are replayed

| test | replay rule | failure report |
|---|---|---|
| `llinventory_parity` | live vs frozen in process; structured corpora line by line, random streams by chunked digest and sample, sweeps by chunked digest, C10 field by field; fixture tree exact (test 5) | `<corpus>: case <id> field <field>.<column>: expected … actual …`; `c3r cases [12000, 13000)` |
| `llinventorymodel_parity` | each corpus regenerated and compared line by line; set 8 also checks expectations in code; closing test fails on a corpus not run, an unknown file, or the copy check | `case <id> line <n> field <name>` |
| `llaisupdate_parity` | three tut tests: `never` op lists (mandatory, binary LLSD compare), `sched` yields (relaxable), drift guard | `case <id>: never op <i> (<op>)/<path>: expected … actual …` |
| `llinvfetch_parity` | every scenario in `index.txt` order in one process; manifest digests; the `# measured` lines are not compared | per line; large scenarios per 500-line chunk |
| `llinventory_task_oracle` | text line by line, binary inputs byte for byte, sweeps by SHA-256 whole and per 97 units; set 1 random sweep as its own test (6) | file, line, case/trace, expected/actual |
| `llinventory_offer_oracle` | full fixtures aligned by case id; sweeps by digest, chunk digest, sample and outcome classes; fixture directories exact | case, key, expected, actual |

Every recorder writes with a `VKS_*_DUMP=<dir>` variable, refuses partial
dumps without a developer flag, and has a `regenerate.py` in
`core-rust:tools/inventory-fixtures/{model,cache,ais-update,ais-fetch,task,offer}/`
that dumps twice into fresh directories, requires identical output, then
replaces the viewer and crate trees together (`msgs` is written by the cache
regenerator; `ais-update` also has `gen_cases.py`, the normative case
generator). Recording platforms: Windows MSVC 19.44 (committed files),
replayed on Linux GCC 15.2 and 14.3; CI replays on windows-2022 and
ubuntu-24.04.

### 5.4 Copy-check scripts

All six share one normalisation (backslash splices joined first, comments
and `#if 0` removed, whitespace collapsed outside literals, literals
verbatim), require each region to match exactly once, check the enclosing
preprocessor conditionals (so `#if 1 <new> #else <old> #endif` fails), and
run an identifier fence so that a new live line touching a fenced member
fails until reviewed.

| script | lines | regions | fence |
|---|---:|---|---|
| `indra/llinventory/tests/check_r1_copies.py` | 375 | 12 headers and 11 sources of `llinventory`/`llassettype` copied whole; 6 `llviewerinventory.cpp` regions (cache labels `:91-92`, `packMessage` `:576-593`, `getCRC32` `:2647-2650`, category `packMessage` `:675-682`, `setVersion` `:740-743`, `exportLLSD`/`importLLSD` `:853-877`), each twice (over frozen and over live base classes) | `mTransactionID mOwnerID mVersion INV_OWNER_ID INV_VERSION getCRC32 packMessage exportLLSD importLLSD setVersion VERSION_UNKNOWN` in `llviewerinventory.{h,cpp}` |
| `newview/tests/check_r2_copies.py` | 182 | GenericMessage handlers (`llviewergenericmessage.cpp`), `disconnectViewer` inventory part | — |
| `newview/tests/check_r3a_copies.py` | 486 | viewer-object members, `accountForUpdate`; harness lines `// VKS-HARNESS` restricted to recording forms | fingerprints of 20+ emulated functions; fence on `mIsComplete … mDescendentsRequested`; no `#define` naming fenced items |
| `newview/tests/check_r3b_copies.py` | 296 | `llstartup.cpp` inventory states, `llappearancemgr.cpp` COF helpers | `mCOFID`, stage-1 class, startup calls, login keys |
| `newview/tests/check_r4a_copies.py` | 360 | `llviewerobject.cpp` and `llviewerinventory.cpp` regions | `mInventory*`, `mInvRequest*`, task methods (empty allowlist for `llviewerobject.*`) |
| `newview/tests/check_r4b_copies.py` | 419 | 49 compiled regions + text guards (in source order) | every offer/give identifier in `indra/newview` |

Partial runs: when a port step replaces a copied source on purpose,
`check_r1_copies.py --only indra/newview --allow-partial` keeps checking the
viewer subclass copies, and the frozen copies are checked once against the
copy commit with `--at`. **Gap:** the docstring says `--at 040ec69cfe`
(`check_r1_copies.py:52`), the README says `--at 60184e5054` (README
`:74`), and neither commit exists in this clone (`git cat-file -t` fails;
the oracle branch was squash-merged as `30c5cbb947`, PR #175). All 24 copy
blobs named in the oracle files do exist (`git cat-file -t` on each).
Inference: `--at` needs either the archived history or a blob-based mode.

### 5.5 The approved fixes the corpora record

The fixtures record the C++ *after* a set of approved fixes, so the port
reproduces fixed behaviour, not upstream bugs. From the READMEs and
`git log` on the inventory sources:

| fix | effect | commit on this line | recorder |
|---|---|---|---|
| LL #5358 | per-task 8 ms + shared 10 ms AIS budget | 1e8d13c11c | R3a/R3b |
| FIRE-33455 | AIS versions only rise; count-only accounting; null check | 4aebbdc8c6 | R3a |
| cache save check | failed save keeps the old cache | a92777dc18 | R2 C5 |
| bug 1 | legacy export writes `not` for an unknown sale type (was a null `char*`: MSVC crash, libstdc++ failed stream) | 9c2bde0909 | R1 |
| bug 4 | `LLCategory` stays inside its tables | e7bd28b09a | R1 |
| bug 5 | capability reply without `contents` releases the task request | a1781cfcd9 | R4a |
| bug 6 | `requestInventory` on cached contents keeps the outstanding request | f3e0c60b88 | R4a |
| bug 15 | AIS timers armed after start | 1b78df805d | R3a |
| bug 19 | refuse folder cycles; stop descendant walks on loops | f2958cbefc | R2 |
| bug 20 (+dabe25973f) | discard a cache that fails to gunzip | 2d9238ed01 | R2 C2 |
| bug 21 | keep the login's root when another root exists | 99edc1225b | R2 C2/C4 |
| bug 23 | don't list a new item `addItem` refused | 1175561fd0 | R2, R3a s15h |
| bug 24 | each UDP item block fires its own callback | 96b7a77115, 30c17e45e8 | R2 set 8 |
| bug 27 | purge the library cache before it loads | 7f270e38d0 | R2 C2 |
| bug 28 | Inference: "Unpack each InventoryDescendents block into a fresh object" (8fc23a4cf4) | 8fc23a4cf4 | R1/R2 |
| bug 29 | each legacy line parsed on its own | 67f35c2e27 | R1 C6, R4a |
| mask exception | unparsable mask reads as no permissions | 162622bc06 | R1 |
| bug 32 | `LLXferManager::requestFile` no longer throws on 1–3 char names | 910b816857 | R4a |
| bug 33 (+146df7efa6) | no null-region dereference in task senders | 07eddc2de5 | R4a |
| bug 38 | sscanf targets initialised | fecf9c037e | R1 |
| bug 51 | `checkTimeout()` rechecks the shared budget after resuming | 7f41835809 | R3a |
| bug 52 | reverted by PR #191 (parent version bumped again before an AIS update) | 85991ba914, revert d92fecd2e5 | R2 C3 |
| bug 53 | `group_owned` junk no longer widens (fixed by bug 29's rule) | — | R1 |

The README hashes for these (e.g. `913990ebe3`, `60184e5054`,
`472ecf270d`, `a6d6a04641`) are on branches not present in this clone;
the table's commits are their counterparts on this line, matched by
subject. Inference for bug 28: the R1 README says a reused object across
blocks is visible at `llinventorymodel.cpp:5523`, but every handler now
allocates a fresh object per block (`:3981, 4286, 5573`), which matches
8fc23a4cf4; the R1 corpus still records `LLInventoryItem::unpackMessage`
into a reused object (`c9.udp.multi`), which is a property of the base
function, not of the handler.

### 5.6 Port rules

From `indra/llinventory/tests/fixtures/README.md:585-618` ("Port rules
(required behaviour; coordinator decisions F5, updated after the bug
fixes)"):

1. **LLSD coercions.** The viewer keeps the C++ `LLSD` coercions: "the C++
   walker extracts each key with them (survey 1 §6)". The viewer test keeps
   the per-platform `"0x10"` values (16 MSVC, 0 libstdc++); the crate's own
   mapping over `vkstorm_llsd::Value` gives 16 and a crate test reads the
   `crate=` value.
   *Note (owner decision of 2026-10-28):* superseded. The crate's mapping
   now gives the same per-platform values (MSVC runtime in an MSVC build,
   libstdc++ otherwise), and the crate test reads `win=` or `linux=` for its
   platform; `crate=` is no longer read.
2. **Unknown sale type** in the legacy export writes `not`, stream good
   (bug 1).
3. **Malformed legacy lines** follow bug 29 (absent, field keeps its
   value), with the mask exception (`PERM_NONE`; `fixFairUse` only after a
   parsed `creator_mask`). The `fix()` invariants and no-widening checks
   stay mandatory for every corrupt input.
4. **No widening**: `111111` for the four corrupt-input cases.
5. **`LLCategory`** stays in its tables (bug 4).

Rules stated elsewhere:

- **R3a split (W3)**: the port keeps the C++ applier `doUpdate()` with its
  `checkTimeout()` call sites; the parse moves to Rust
  (`vkstorm-inventory` `ais/update.rs`) as an ordered op list C++ applies.
  Check 1 (`never`) stays byte-exact; check 2 (`sched`) may be replaced by a
  documented check of the same calls with the port's yields
  (`ais/update/README.md:174-182`). The two reads that happen at apply time
  (copy back of counts, the version rule) must stay at apply time
  (`:356-363`). Deltas must be signed (`:505-508`). The 403 alert flag is
  explicit state in a Rust replay (`:487-493`).
- **R3b seams (K5, K10, W3, W4)**: an injected clock read at the same
  three points of `bulkFetchViaAis`, and a stop check at the same point as
  `LLCoros::checkStop()` (`ais/fetch/README.md:445-454`).
- **Rule PRE_D2_SL_FALLBACKS** (decision 2026-10-25): the Second Life
  legacy-capability and UDP fallbacks stay; the six scenarios s10a, s12,
  s13, s14, c10, c11 are permanent; `VKS_INVFETCH_PRE_D2_GATES` stays ON
  (`:405-443`).
- **R4a (W5)**: the executor that carries out Rust decisions is one source
  file compiled into both the viewer and the test (`task/README.md:128-136`).
- **R4b (W6/W7)**: a second `Decisions` implementation calls Rust over FFI
  and runs the action list with the C++ executor; the fixtures must replay
  unchanged (`offer/README.md:62-70`).
- **R2 C4**: a port must state which `std::default_random_engine` it
  reproduces for `QAModeFakeSystemFolderIssues`
  (`cache/README.md:193-198`).
- **Recorded defects stay** until a fix is approved like the others (R3a
  finding 9; every README's "recorded as it is" list).

### 5.7 References to the lost surveys, reconstructed

The fixtures cite four inventory surveys (numbered 1–4; "survey 8.2" in R3b
is shorthand for survey 3 §8.2). They are not in the repository. Full list
from `grep -rn -i 'survey [0-9]' indra/llinventory/tests indra/newview/tests`.
What each section must have said, from the citing context:

| citation | where cited | reconstruction | confidence |
|---|---|---|---|
| survey 1 §1.7 | R1 README `:630` | Legacy text quirks of the item format: the orphan `|` after metadata, `permissions 0` written with a space, the `%254s` widths and line cap, the empty-name `|` hack | Inference from the examples; exact section text not recoverable |
| survey 1 §4.3 | R1 README `:630` | LLSD form quirks: `is_owner_group` never read, `item_id` vs `category_id` across the category dialects, the favorite branch of `LLInventoryCategory::importLLSD` falling through to `return false` (`llinventory.cpp:1731-1748`) | Inference |
| survey 1 §6 | R1 README `:578` | The boundary design: C++ walks the LLSD map and extracts each key with the C++ `LLSD` coercions, handing flat values to Rust | Stated by the citing sentence; detail not recoverable |
| survey 1 §7 | R1 README `:8` | The specification of corpora C1–C10 | Fully reconstructable: the README's "The corpora" section is that specification as executed |
| survey 2 §2.2 | R2 README `:285` | Cache file format; it wrote the folder key as `folder_id`, while the cache uses `cat_id` (the README corrects it) | Partly |
| survey 2 §2.4, §2.5 | `llinventorymodel_parity_c2.h:623` | Login load and discard rules of `loadSkeleton` | Partly (C2's 64 cases enumerate them) |
| survey 2 §8.2 | `llinventorymodel_parity_test.cpp:11` | R2's corpus plan (C1–C5) | Reconstructable from the R2 README |
| survey 2 Q3, Q4, Q7, Q8, Q9, Q10, Q13 | R2 README `:207-248`, msgs README | Open questions: Q3 `BulkUpdateInventory` resets folder version; Q4 `deleteObject` notifies once even when told not to; Q7 stale plain file beside a corrupt `.gz`; Q8 incomplete root reload reads as complete; Q9 clear-cache marker and the library (fixed as bug 27); Q10 unknown asset types invalidate their folder every login; Q13 link retarget leaves a stale backlink | Questions recoverable; numbering of the others (Q1, Q2, Q5, Q6, Q11, Q12) not |
| survey 2 risk 10 | R2 README `:278` | Thread misuse of the model | Title only |
| survey 3 layer A / §8.1 sets 1–11 | R3a README `:3, 405-420` | AIS response decision corpus plan | Reconstructable (the set table) |
| survey 3 layer B / §8.2 scenarios 1–12 | R3b README `:3, 302-316` | Fetch wire scenarios: 1 login cold, 2 login warm, 3 large, 4 forbidden, 5 transient 5xx, 6 outage, 7 forced, 8 items, 9 writes, 10 legacy caps, 11 UDP, 12 region change | Reconstructable |
| survey 3 §2.8 | `check_r3b_copies.py:82` | `start()` after login is a no-op once the walk started | Partly |
| survey 3 §4 | `llinvfetch_parity_scenarios.cpp:1032` | something "ignored" by the scheduler | Not recoverable |
| survey 3 §6 bugs 3, 12, 14 | R3a README `:391, 557, 564` | bug 3: `LL_ERRS("unpack failed")` after `unpackMessage`, unreachable from LLSD; bug 12: a new category whose parent arrives later in the same update is not linked; bug 14: `REMOVEITEM` 410 bumps the parent version | Partly |
| "survey bug" 7, 8, 9, 15 | R3b README `:368, 386, 397, 401` | 7 legacy 403 split sends library folders to `FetchInventoryDescendents2`; 8 legacy forced set kept for the session; 9 legacy walk on SL does not descend; 15 outage leaves version 0 | Recoverable from findings |
| survey 3 §10 risks | R3b README `:339-360` | 18 risks, listed with their scenarios | Reconstructable |
| survey 4 §1.6 | offer README `:273` | Calling-card offers (`OfferCallingCard`), out of scope | Title only |
| survey 4 §5, §5.8 | `llinventory_task_oracle_test.cpp:1441` | Task inventory; §5.8: a task file is removed only by a successful `loadTaskInvFile` | Partly |
| survey 4 §7 sets 4–8 | offer, give, msgs READMEs | Corpus plan: set 4 offers' IM part and gives, sets 5–6 offers, set 7 give predicates/drag-and-drop, set 8 UDP handlers | Reconstructable |
| survey 4 §9 risks | `llinventory_offer_oracle.cpp:1711`, offer README `:164` | Offer/give risks, each with a named case in `policy_named.txt` (51 cases) | Partly (the case names) |
| survey 1 §1, §4 other subsections; survey 2 §1, §3–§7; survey 3 §1, §3, §5, §7, §9; survey 4 §2–§4, §6, §8 | — | Never cited | Not recoverable |

Other uncited plan identifiers that appear in the fixtures and cannot be
expanded from the repository: coordinator decisions F1 (the approved fix
set), F3, F5 (port rules), F8 (offline offers); work items K5, K10, W3, W4
(fetch/AIS port), W5 (task inventory port), W6/W7 (offer/give port); audits
V0, V1 with finding codes (V13–V15, T2/T3/T17/T18, F5, P15, P21, P22, D7,
D13, D15, U1, U2, X1, X3, N1–N17, S1–S6); step D2 (dropped). Inference: W1
and W2 were the data model and model/cache ports (R1, R2), by elimination.

---

## Inventory: port plan inputs

### 6.1 Crate modules and the C++ each replaces

`core-rust:crates/vkstorm-inventory/src` (no code yet; each module is a doc
comment naming its C++). The C++ column is what the module must reproduce;
the fixture column is where its parity comes from.

| module | C++ functions replaced | file:line | parity |
|---|---|---|---|
| `types::asset` | `LLAssetType::lookup` (both directions), `lookupHumanReadable`, `getDesc`, `lookupCanLink`, `lookupIsAssetFetchByIDAllowed`, `lookupIsAssetIDKnowable`, `lookupIsLinkType`, `getTypeNames`, the entry table | `llcommon/llassettype.cpp:74-265` | R1 C1 `at`, `c1.name.*` |
| `types::inventory` | `LLInventoryType::lookup`, `lookupHumanReadable`, `defaultForAssetType`, `cannotRestrictPermissions`, `showInWorldPermissions`, `inventory_and_asset_types_match` | `llinventory/llinventorytype.cpp` | C1 `it`, `c1.match.*` |
| `types::folder` | `LLFolderType::lookup`, `lookupIsProtectedType`, `lookupIsAutomaticType`, `lookupIsSingletonType`, `lookupIsEnsembleType`, `folderTypeToAssetType`, `assetTypeToFolderType` | `llinventory/llfoldertype.cpp` | C1 `ft` |
| `types::sale` | `LLSaleInfo::lookup` both directions | `llsaleinfo.cpp:263-289` | C1 `sale` |
| `flags` | `LLInventoryItemFlags` constants, `getInventorySubType`/`setInventorySubType` (`llinventory.h:174`), `LLSettingsType::fromInventoryFlags`, `getIconName`, `getDefaultName`; `lltransactionflags.cpp` (`pack/unpack_transaction_flags`, `build_transfer_message_*`) | `llinventorydefines.h`, `llinventorysettings.cpp`, `lltransactionflags.cpp` | C1 |
| `text` | `LLInventoryObject::correctInventoryName`, `LLInventoryItem::correctInventoryDescription`, the `replaceNonstandardASCII`/`replaceChar('|')` sequence, `rename`, `setDescription` | `llinventory.cpp:179, 357, 516-530` | C2 |
| `category_obj` | `LLCategory::init/getU32/lookup/getDescription`, its `ObjectProperties` packing | `llcategory.cpp` | C1, C9 `c9.category.*` |
| `perms` | `LLPermissions::init`, `initMasks` (both), `fix`, `fixFairUse`, `fixOwnership`, `accumulate`, `setOwnerAndGroup`, `setLastOwner`, `yesReallySetOwner`, `deedToGroup`, `set*Bits`, `allowOperationBy` and helpers, `getOwnership`, `getSafeOwner`, `getCRC32`, `set`, `==`, `mask_to_string` (both; `opensim` feature for the export column) | `llpermissions.cpp:45-506, 850-880, 1090-1160` | C3 (structured, `c3r`, sweeps) |
| `aggregate` | `LLAggregatePermissions` (`getValue`, `getU8`, `isEmpty`, `aggregate` both, `aggregateBit`, `aggregateIndex`, `perm2PermIndex`, pack/unpack as `U8`) | `llpermissions.cpp:902-1080` | C3 aggregate, C9 `c9.agg*` |
| `sale` | `LLSaleInfo` constructor, `isForSale`, `getCRC32`, `setSalePrice` (clamp), `accumulate` (wrapping), `==` | `llsaleinfo.cpp:56-79, 207-212, 291-312` | C4 |
| `object` | `LLInventoryObject` record, `copyObject`, `getLinkedUUID`, `getIsLinkType`, `getActualType` | `llinventory.cpp:79-210` | C5 `c5o.*` |
| `item` | `LLInventoryItem` record, constructors, `copyItem`, `getLinkedUUID`, `getCRC32`, `accumulatePermissionSlamBits`, `setPermissions`, subtype helpers, the `packMessage`/`unpackMessage` field order and fix-ups | `llinventory.cpp:376-683` | C5, C9 |
| `category` | `LLInventoryCategory` record, constructors, `copyCategory`, preferred type | `llinventory.cpp:1341-1381` | C5 |
| `shadow` | the `MAGIC_ID` XOR (`LLXORCipher` over 16 bytes) and the `PERM_ITEM_UNRESTRICTED`/null rule | `llinventory.cpp:73, 845, 1022-1037, 1087-1100, 1236`; `llmessage/llxorcipher.cpp` | C5 `c5.mask.*` with MAGIC/NOT_MAGIC |
| `llsd::coerce` | `LLSD::asInteger/asString/asUUID/asBoolean/asReal` as the readers use them, over `vkstorm_llsd::Value` | `llcommon/llsd.cpp` | C7, C8 (with the `RT(...;crate=16)` values) |
| `llsd::item` | `LLInventoryItem::asLLSD` (both), `fromLLSD`, `ll_create_sd_from_inventory_item` | `llinventory.cpp:1059-1336` | C5 `llsd*`, `fromllsd*`, C7, C10 items |
| `llsd::category` | `asLLSD`, `asAISCreateCatLLSD`, `fromLLSD`, `exportLLSD`, `importLLSDMap`, `importLLSD`, `ll_create_sd_from_inventory_category`, `ll_create_category_from_sd`; viewer subclass `exportLLSD`/`importLLSD` keys (`owner_id`, `version`) | `llinventory.cpp:1383-1504, 1670-1770`; `llviewerinventory.cpp:853-877` | C5 categories, C8, C10 categories |
| `llsd::perms` | `LLPermissions::importLLSD`, `ll_create_sd_from_permissions`, `ll_fill_sd_from_permissions`, `ll_permissions_from_sd` | `llpermissions.cpp:776-848, 1163-1188` | C3, C7 `c7.perm.*` |
| `llsd::sale` | `LLSaleInfo::asLLSD` (both), `fromLLSD` (with `perm_mask` forms), `operator LLSD`, `ll_create_sd_from_sale_info`, `ll_sale_info_from_sd` | `llsaleinfo.cpp:96-137, 320-344` | C4, C7 `c7.permmask.*` |
| `legacy::reader` | `std::istream::getline(buffer, N)` with failbit/eofbit on overlong lines and EOF, the get position afterwards | (all legacy readers) | C6 `*.stream` columns (`state pos rest`) |
| `legacy::scanf` | the `sscanf` conversions `%254s`, `%254[\t]`, `%254[^|]`, `%x`, `%d` with MSVC/glibc overflow results | (all legacy readers) | C6, runtime edge 5 |
| `legacy::item` | `LLInventoryItem::importLegacyStream`/`exportLegacyStream` incl. the metadata line, shadow id, `perm_mask` fix-ups, type reset | `llinventory.cpp:766-1056` | C5 `legacy*`, C6 `I`, R4a set 1 (through the task loader) |
| `legacy::object` | `LLInventoryObject::importLegacyStream`/`exportLegacyStream` | `llinventory.cpp:212-342` | C5 `legacy.object`, C6 `O` |
| `legacy::category` | `LLInventoryCategory::importLegacyStream`/`exportLegacyStream` | `llinventory.cpp:1520-1668` | C5, C6 `C` |
| `legacy::perms` | `LLPermissions::importLegacyStream`/`exportLegacyStream` (16,384-byte lines, mask exception) | `llpermissions.cpp:572-775` | C3 legacy, C6 `P` |
| `legacy::sale` | `LLSaleInfo::importLegacyStream`/`exportLegacyStream` (`not` for unknown types) | `llsaleinfo.cpp:81-94, 139-205` | C4 legacy, C6 `S` |

Note (owner decision of 2026-10-28): `llsd::coerce` gives the per-platform
`RT(...)` value (`win=` with the MSVC runtime, `linux=` with libstdc++), not
the `crate=16` value named in the table; `crate=` is no longer read.

Not in the module list but covered by R1: `LLNotecard::importStream`/
`exportStream` (C6 `N`, 22 notecards; `llnotecard.cpp`), `LLLandmark`
(not recorded). Inference: notecards are a thin wrapper over
`legacy::item` and can stay C++ or join `legacy`; this is an open decision.

Modules the later steps will need (named in fixture READMEs, absent from
the crate): `ais/update.rs` (R3a, W3), a fetch scheduler and request builder
(R3b, K5/K10/W4), the cache codec and login reconciliation (R2), task
inventory decisions (R4a, W5), offer/give decisions (R4b, W6/W7).

### 6.2 Suggested dependency order

Within the R1 crate (each step testable against its corpus alone):

1. `types::*`, `flags` (C1) — no dependencies.
2. `text`, `shadow`, `category_obj` (C2, part of C5, C1/C9).
3. `perms`, `aggregate`, `sale` (C3, C4, including the exhaustive sweeps).
4. `object`, `item`, `category` records and CRCs (C5 `crc`, `derived`).
5. `llsd::coerce`, then `llsd::{perms,sale,item,category}` (C7, C8, C10).
6. `legacy::{reader,scanf}`, then `legacy::{perms,sale,object,category,item}`
   (C6, C5 `legacy*`).
7. Message field order (C9) last: the message system is Rust already, but
   the C++ `packMessage`/`unpackMessage` call it field by field.

Across the rung, by recorder:

1. R1 data model (above), C ABI, C++ wrappers; `check_r1_copies.py
   --only indra/newview --allow-partial`.
2. R2 cache codec (`loadFromFile`/`saveToFile` bytes) and, Inference,
   `validate()`; the model containers stay C++.
3. R3a AIS parse to op list (W3), C++ `doUpdate()` applier unchanged.
4. R3b scheduler and request building (K5, K10, W4) with the injected clock
   and stop check.
5. R4a task inventory decisions (W5), one shared executor.
6. R4b offer/give decisions (W6/W7) through the existing `Decisions` seam.

Inference: R3a before R3b, because the scheduler's completion handling
consumes AIS results; R4a and R4b are independent of R3 and of each other.

### 6.3 C ABI shape

What the existing sources commit to:

- The crate is `#![forbid(unsafe_code)]`; the C ABI is a `vkstorm-ffi`
  feature (`lib.rs:27-28`; `core-rust:crates/vkstorm-ffi/src` has no
  inventory module yet: `assets.rs audio.rs http.rs image.rs j2c.rs
  lib.rs llsd.rs messages.rs volume.rs`). The doc comment still says "the
  viewer's `vkstorm-ffi` crate", which predates the move of the FFI to
  vkstorm-core-rust (`doc/rust_port.md:36-41`).
- Type values are raw integer newtypes, never closed enums (`lib.rs:22-24`):
  the ABI passes `S8`/`S32`/`U8` as received.
- Records are flat; the C++ objects keep their fields (`lib.rs:16-20`).

Options for the record crossing (open decision, Inference on costs):

| shape | how | fits | concern |
|---|---|---|---|
| Flat records by value | `VksInvItem { uuid[16] ×7, masks ×5, i8 type, i8 inv_type, u32 flags, … , name ptr/len, desc ptr/len, runtime ptr/len }` copied in and out per call | rules (`fix`, `accumulate`, CRC, normalisers), the readers' "previous object" semantics (C6/C7 start from `prev_item()`, so the reader needs the whole record in and out) | string ownership across the boundary (Rust-owned buffers freed by a `vks_*_free`, or C++-provided output buffers) |
| Object views (callbacks) | Rust reads/writes fields through a vtable onto the C++ object | avoids copies on the hot cache-load path (100k items) | more `extern "C"` traffic, harder to keep panic-safe |
| Op lists | Rust returns an ordered list of model calls (R3a) or actions (R4a/R4b) that a C++ executor applies | AIS, task, offer decisions | the op vocabulary must match the recorded ops |

LLSD crossing: port rule 1 says the viewer's C++ walker extracts each key
with the C++ coercions, so for `fromLLSD` the C++ wrapper iterates the
`LLSD` map and passes key/value pairs (already coerced) to Rust in map
order, and for `asLLSD` Rust returns the field list and C++ builds the
`LLSD`. Inference: the alternative, passing binary LLSD bytes to
`vkstorm-llsd`, would change the `"0x10"` value on Linux and break rule 1.

Legacy streams: the C++ API takes `std::istream&` (file streams for task
inventory, string streams for notecards and wearables). The fixtures record
`state pos rest` after every read. Inference: the wrapper reads the
remaining bytes (or a bounded window), the crate returns the bytes it
consumed and the stream bits, and the wrapper sets the get position and
`setstate`; a non-seekable stream would need a line-at-a-time callback
instead.

### 6.4 What must stay C++ in this rung

- The record classes and their field storage (`LLInventoryObject`,
  `LLInventoryItem`, `LLInventoryCategory`, `LLViewerInventoryItem`,
  `LLViewerInventoryCategory`), `LLRefCount`/`LLPointer`, `virtual`
  link-following getters, `boost::signals2::trackable`.
- `LLInventoryModel`'s maps, child vectors, backlinks, observers,
  `notifyObservers` and the observer classes; `gInventoryCallbacks`.
- The coroutine plumbing (`LLCoros`, `LLCoprocedureManager`,
  `HttpCoroutineAdapter`), the AIS applier `doUpdate()` and its yield
  points (rule above), the legacy HTTP handlers.
- `LLMessageSystem` call sites (message codec already Rust), the xfer
  manager, `LLViewerObject`'s task-inventory state (R4a "Outputs ... which
  stays C++").
- UI, notifications, RLVa and Firestorm policy hooks that sit around the
  decisions (R4b keeps antispam and Omnifilter in C++ by text guard).

### 6.5 Open decisions

1. Record crossing: flat records vs views (6.3); string ownership.
2. Legacy stream crossing: seekable window vs line callback.
3. Whether `LLNotecard`, `LLLandmark` and `LLWearable`'s use of the
   permission/sale readers move with `legacy` or stay C++ callers.
4. Which `std::default_random_engine` `validate()`'s QA mode reproduces
   (R2 C4).
5. The R3a finding 9 count mismatch (a refused item still counted): fix
   before or after the port.
6. Whether `check_r1_copies.py --at` is made blob-based, since its commit is
   not in this clone (5.4).
7. The rung number: fixtures say "rung seven"; `doc/rust_port.md:13-15`
   lists textures between audio (rung six) and inventory, which would make
   inventory rung eight. Inference: textures were folded into rung five's
   asset pipeline.
8. Whether the crate gets its own replay tests (`tests/*.rs`) before the
   viewer wrappers land: the fixtures are in the crate, no test reads them
   yet.

---

## Primitives, characters, appearance

### `indra/llprimitive` (18,151 lines)

| component | file:line | content |
|---|---|---|
| `LLPrimitive : LLXform` | `llprimitive/llprimitive.h:434`, `.cpp` 2,438 lines | pcode, material, volume, texture entry list; `packTEField` `:1062`, `packTEMessage` `:1215, 1301`, `unpackTEMessage` `:1486, 1495` (the `TextureEntry` binary format: per-field face bitmasks) |
| Extra parameters (`LLNetworkData`) | `llprimitive.h:105-434` | `LLLightParams` `:143`, `LLReflectionProbeParams` `:191`, `LLFlexibleObjectData` `:274`, `LLSculptParams` `:314`, `LLLightImageParams` `:335`, `LLExtendedMeshParams` `:359`, `LLRenderMaterialParams` `:381`; each `pack`/`unpack` over `LLDataPacker` and LLSD |
| `LLTextureEntry` | `lltextureentry.h:71`, `.cpp` 800 lines | per-face texture id, color, scale/offset/rotation, bump/shiny/fullbright, media flags, glow, material id, GLTF override; `asLLSD` `:206-240`, `fromLLSD` `:241` |
| `LLPrimTextureList` | `llprimtexturelist.cpp` 450 lines | the face array |
| `LLVolumeMessage` | `llvolumemessage.h:36`, `.cpp` 557 lines | quantised volume params in object updates (profile/path) |
| Materials | `llmaterial.*` (legacy normal/specular), `llmaterialtable.cpp` 863 lines (physics material table), `llgltfmaterial.h:50`, `.cpp` 956 lines (`fromJSON` `:185`, `asJSON` `:202`, `setFromModel` over tinygltf `:220`, `applyOverride` `:612`, `applyOverrideLLSD` `:756`) |
| `LLMediaEntry` | `llmediaentry.cpp` 595 lines | media-on-a-prim LLSD |
| Mesh model | `llmodel.h:46` `LLMeshSkinInfo`, `:83` `LLModel : LLVolume`, `llmodel.cpp` 2,502 lines; `llmodelloader.cpp`, `lldaeloader.cpp` 2,744 lines (COLLADA importer) |
| Tests | `llprimitive/tests/`: `llprimitive_test.cpp`, `llgltfmaterial_test.cpp`, `llmediaentry_test.cpp`, `llmeshunpack_parity_test.cpp`, `fixtures/` |

Already Rust (`doc/rust_port.md`): the parametric prim mesher (rung "the
parametric prim mesher", `:261`), mesh LOD unpacking, skin info and convex
decomposition parsing (`LLVolume::unpackVolumeFaces`,
`LLMeshSkinInfo::fromZippedData`, `LLModel::Decomposition::fromZippedData`,
rung five, `:1213-1300`), with `INTEGRATION_TEST_llmeshunpack_parity`.
The importer (`fromLLSD`, `lldaeloader`) and `cacheOptimize` stay C++ by
that rung's judgement.

Port assessment. The `TextureEntry` binary codec, the extra-parameter
packers and `LLVolumeMessage` are pure data with message-system callers and
are natural first pieces of the object-store rung. `LLGLTFMaterial` JSON is
tinygltf-based and its override application feeds rendering. Inference:
`LLPrimitive` itself (an `LLXform` subclass that `LLViewerObject` inherits)
must keep its storage in C++ for the same reason as the inventory records.

### `indra/llcharacter` (16,083 lines)

| component | file:line | content |
|---|---|---|
| `LLCharacter` | `llcharacter/llcharacter.h:55` | abstract character (joints, visual params, motion controller); `LLAvatarAppearance` derives from it |
| `LLJoint` | `lljoint.h:90`, `.cpp` 1,032 lines | skeleton node, transforms, attachment/position overrides (`LLVector3OverrideMap` `:59`) |
| Motions | `llmotioncontroller.h:53, 82` (`LLMotionRegistry`, `LLMotionController`, 1,157 lines), `llkeyframemotion.h:58` (2,616 lines), `llkeyframewalkmotion.cpp`, `llheadrotmotion.cpp`, `lljointsolverrp3.cpp`, `llpose.cpp` | animation playback and blending |
| Animation asset | `LLKeyframeMotion::deserialize` `llkeyframemotion.cpp:1231`, `serialize` `:2048`; `LLKeyframeDataCache` `llkeyframemotion.h:442` | the `.anim` binary format over `LLDataPacker` |
| BVH import | `LLBVHLoader` `llbvhloader.h:225`, `.cpp` 1,500 lines | upload-time conversion |
| Gestures | `LLMultiGesture` `llmultigesture.h:40`, steps `:128-223`, `llgesture.cpp` | gesture asset format |
| Visual params | `LLVisualParam` `llvisualparam.h:105` | XML-defined shape params |
| Tests | `lljoint_test.cpp` only |

Port assessment. `deserialize`/`serialize` of keyframe motions and
gestures are asset parsers with no rendering dependency and could follow
the inventory rung as small units (they are asset formats, Inference: a
natural add-on to rung five's asset work). The motion controller runs per
frame on avatars and is coupled to `LLVOAvatar`, the joints and the
renderer's skinning; it belongs with the object store or the renderer.

### `indra/llappearance` (13,893 lines)

| component | file:line | content |
|---|---|---|
| `LLAvatarAppearance : LLCharacter` | `llappearance/llavatarappearance.h:52`, `.cpp` 2,195 lines | skeleton and mesh setup from `avatar_skeleton.xml`/`avatar_lad.xml`, wearable data, baked textures |
| `LLWearable` | `llwearable.h:43`, `.cpp` 793 lines | wearable asset: legacy text with **`LLPermissions`/`LLSaleInfo` legacy blocks** (`exportStream` `:99-117`, `importStream` `:211-315`), visual params, textures; `<FS:Ansariel> [Legacy Bake]` (`llwearable.h:35`) |
| `LLWearableData`, `LLWearableType` | `llwearabledata.h:36`, `llwearabletype.h:36` | per-type wearable stacks; type table |
| Baking | `lltexlayer.h:54-295` (`LLTexLayerInterface`, `LLTexLayer`, `LLTexLayerSet`, `LLTexLayerSetBuffer`), `.cpp` 2,010 lines, `lltexlayerparams.cpp` | local compositing (legacy bake; server-side baking is the norm on Second Life) |
| Meshes and morphs | `llpolymesh.h:64-156`, `llpolymorph.cpp` 846 lines, `llpolyskeletaldistortion.cpp`, `lldriverparam.cpp` 657 lines | avatar base mesh deformation |

Coupling to inventory: wearables are inventory items (`AT_CLOTHING`,
`AT_BODYPART`, subtype in the flags), outfits are folders of links, and
`LLWearable::importStream` reuses the permission and sale legacy readers
the R1 crate ports (§3). Port assessment: the wearable asset codec is data
and could move with or just after `legacy`; `LLTexLayer` is GL rendering
and stays C++ until the renderer rung.

---

## Object store (next rung)

### Components

| component | file:line | role |
|---|---|---|
| `LLViewerObject : LLPrimitive, LLRefCount, LLGLUpdate` | `newview/llviewerobject.h:125-128`, `.cpp` 8,352 lines | every in-world object; `processUpdateMessage` `:1252-2586` (about 1,335 lines: full, compressed and terse update decoding through `LLDataPackerBinaryBuffer`, `unpackParentID` `:747`, `extractSpatialExtents` `:1237`, extra params via `unpackParameterEntry` `:1644, 1981, 6860`), `setTE*` `:5547-6145`, `setParameterEntry` `:6957`; also the task inventory (§4) |
| `LLViewerObjectList` | `newview/llviewerobjectlist.cpp` 2,533 lines | the object map; `processUpdateCore` `:242`, `processObjectUpdateFromCache` `:333`, `processObjectUpdate` `:442`, `processCompressedObjectUpdate` `:755`, `processCachedObjectUpdate` `:762`, `update` `:921`, `killObject` `:1511`, `createObject(Viewer)` `:2135, 2183` |
| Message entry points | `newview/llviewermessage.cpp:4562-4621` | `process_object_update`, `process_compressed_object_update`, `process_cached_object_update`, `process_terse_object_update_improved` |
| `LLVOVolume` | `newview/llvovolume.cpp` 7,313 lines | prims and meshes; `processUpdateMessage` `:355`, `setVolume` `:1229` |
| `LLVOAvatar` (data side) | `newview/llvoavatar.cpp` 13,389 lines | `processUpdateMessage` `:3004`, `parseAppearanceMessage` `:10756`, `processAvatarAppearance` `:10933`; most of the file is per-frame update and rendering (`idleUpdate*` `:3116-4683`, complexity `:12347`) |
| `LLVOAvatarSelf`, `LLControlAvatar` | 3,802 and 726 lines | own avatar, animated objects |
| `LLVOCache` | `newview/llvocache.cpp` 2,039 lines | per-region object cache on disk (CRC-keyed cache entries) |
| Others | `llvotree.cpp`, `llvograss.cpp`, `llvopartgroup.cpp` | non-prim object kinds |

### Coupling and port assessment

- `LLViewerObject` inherits `LLPrimitive` (storage), `LLRefCount`
  (`LLPointer` everywhere) and `LLGLUpdate`; it owns its `LLDrawable`
  (renderer) and is referenced by the selection manager, the UI, scripts'
  dialogs and the task inventory. Its storage cannot leave C++ before the
  renderer rung, for the same reasons as the inventory records.
- The decoding inside `processUpdateMessage` is the clear data part: field
  extraction from the full/terse/compressed formats, the `TextureEntry`
  codec, the extra-parameter blocks, `LLVolumeMessage`, the cache entry
  format of `LLVOCache`. Inference: the inventory pattern applies — R-style
  recorders on the decoders (bytes in, field records out) and on the
  `LLViewerObjectList` decisions (create, update, kill, orphan handling,
  cache hits), with the object containers staying C++.
- The task inventory state machine already has its oracle (R4a) and moves
  with the inventory rung (W5); the object-store rung inherits
  `LLViewerObject` with that part done.
- `LLVOAvatar`'s appearance message parsing and the `llappearance`
  wearable/visual-param data are the data side of avatars; the motion and
  rendering side is per-frame and renderer-coupled.
- No recorder exists yet for the object store (`ls indra/newview/tests`
  shows inventory, texture and HTTP parity tests only). Real object update
  streams need a live simulator, the same gap R4a states for task inventory
  files.

---

## Coupling

### Include sites

Command (Git Bash, `indra/`), counting files that include each header,
excluding `tests/` directories:

```sh
for h in llinventory.h llpermissions.h …; do
  n=$(grep -rl --include=*.cpp --include=*.h "#include \"$h\"" newview | grep -v '/tests/' | wc -l)
  a=$(grep -rl --include=*.cpp --include=*.h "#include \"$h\"" . | grep -v '/tests/' | wc -l)
  echo "$h $n $a"; done
```

| header | files in `newview` | files in `indra` |
|---|---:|---:|
| `llinventory.h` | 47 | 51 |
| `llassettype.h` | 17 | 31 |
| `llinventorydefines.h` | 28 | 33 |
| `llpermissions.h` | 15 | 20 |
| `llinventorytype.h` | 11 | 18 |
| `llpermissionsflags.h` | 9 | 11 |
| `llfoldertype.h` | 7 | 9 |
| `lllandmark.h` | 6 | 7 |
| `llsaleinfo.h` | 5 | 9 |
| `llcategory.h` | 4 | 5 |
| `llinventorysettings.h` | 3 | 5 |
| `llnotecard.h` | 3 | 4 |
| `lltransactionflags.h` | 1 | 3 |
| `llparcel.h` (not inventory) | 77 | 78 |
| `llinventorymodel.h` | 107 | 107 |
| `llinventoryfunctions.h` | 87 | 87 |
| `llviewerinventory.h` | 75 | 75 |
| `llinventoryobserver.h` | 47 | 47 |
| `llinventorymodelbackgroundfetch.h` | 27 | 27 |
| `llaisapi.h` | 9 | 9 |
| `llgiveinventory.h` | 8 | 8 |

Direct includes understate reach: `llviewerinventory.h` includes
`llinventory.h`, so the base classes reach every file that includes the
viewer header.

### `gInventory` uses

Command (Git Bash, `indra/newview`):

```sh
grep -rlw gInventory --include=*.cpp --include=*.h . | grep -v '^./tests/' | wc -l          # 145 files
grep -rlw gInventory --include=*.cpp --include=*.h . | grep -v '^./tests/' | xargs grep -ow gInventory | wc -l   # 2,244 uses
```

145 of the 1,691 C++ files directly in `newview` reference `gInventory`,
2,244 times. Top files (`grep -cw`): `llinventoryfunctions.cpp` 171,
`llappearancemgr.cpp` 141, `llinventorybridge.cpp` 108,
`llinventorygallery.cpp` 106, `llinventorymodel.cpp` 94,
`llviewerinventory.cpp` 70, `llinventorygallerymenu.cpp` 64,
`llinventorymodelbackgroundfetch.cpp` 57, `llviewermessage.cpp` 54,
`rlvinventory.cpp` 52, `lltooldraganddrop.cpp` 49,
`llagentwearables.cpp` 44, `fslslbridge.cpp` 38, `aoengine.cpp` 37,
`llfavoritesbar.cpp` 36. Firestorm subsystems (`fslslbridge`, `aoengine`)
and RLVa are direct model clients.

### Base-class construction outside the model

`grep -rn 'new LLInventoryItem\b\|new LLInventoryItem(\|new LLInventoryObject\|new LLInventoryCategory\|LLPointer<LLInventoryItem>' --include=*.cpp indra/newview indra/llmessage indra/llui | grep -v tests | wc -l`
gives 36 sites, and 101 `newview` source files mention `LLInventoryItem`.
Task inventory lists (`LLInventoryObject::object_list_t`) hold base-class
objects that never enter the model.

---

## Risks and open questions

1. **Field storage stays C++, behaviour moves.** Every record rule must be
   reachable on a C++ object through flat copies or views; a rule that the
   viewer subclass reimplements on the protected fields (e.g.
   `LLViewerInventoryItem::packMessage`, `llviewerinventory.cpp:576-593`)
   is a second copy that R1 fences but that the crate does not replace
   unless the wrapper changes too.
2. **Per-platform LLSD coercion** (port rule 1): the viewer must keep the
   C++ coercions, so the `"0x10"` difference between MSVC and libstdc++
   remains a deliberate platform difference after the port.
3. **Legacy stream semantics** are part of the contract (`state pos rest`
   after every read), including failbit on 255-character lines; the stream
   crossing (6.3) is unproven.
4. **Wearables, notecards and task files** share the legacy readers;
   wearable streams are not in any corpus.
5. **The lost surveys**: sections cited by the fixtures are only partly
   reconstructable (5.7); plan identifiers (W/K/F/V codes) cannot be
   expanded from this repository.
6. **Unresolvable recording commits**: the README commits
   (`60184e5054`, `040ec69cfe`, `472ecf270d`, `a6d6a04641`, `8d18dd15dd`,
   bug-fix hashes) are absent from this clone; `check_r1_copies.py --at`
   cannot run here (5.4). The blobs are present.
7. **Stale line references in READMEs**: the R1 README cites
   `llinventorymodel.cpp:5523` for an object reused across blocks; that
   reuse no longer exists in the handlers (5.5). The R4a README's
   `llviewerobject.cpp` line ranges are as of `6f23ef84f0`; the copy check,
   not the numbers, is authoritative.
8. **Mock fidelity**: AIS reply shapes at the depth limit and the 403/410
   bodies are not known from the viewer (R3a/R3b "Mock fidelity"); the
   corpora pin what the C++ does with the mock's shapes, not what servers
   send.
9. **Concurrency** is recorded deterministically only through the LOCKSTEP
   and PHASED modes; the CONC scenarios compare multisets. A port that
   changes dispatch order within a frame can pass CONC and still differ in
   production ordering. Inference.
10. **Recorded defects** the port must reproduce until separately approved:
    R3a findings 2–9 (lower version applied when the count is unknown, the
    snapshot-then-yield window, stale item write-back, 410 bumping the
    parent, error bodies applied, loose typing, refused item counted), R3b
    findings 1–10 (empty legacy split POST, stranded legacy replies, drained
    queue, COF wait, no legacy descent, Lost And Found refetch, category
    PATCH keys, forced set, version 0 after outage, whole-subtree refetch),
    R2 recorded behaviours (Q3, Q4, Q8, Q10, Q13), R4a quirks (serial never
    sent, stuck pending, S16 wrap, first-match delete), R4b behaviours
    (muted recipient unmuted on give, no-copy delete on Yes, share gives one
    folder, sticky accept flag, forced MUTE adds no mute).
11. **OpenSim** paths are mostly unrecorded (`OPENSIM` off in Release):
    suitcase validation, the UDP adaptive fetch loop, null-parent rules.
    The `o..` fetch scenarios cover the legacy capability paths on another
    grid only.
12. **Crate skeleton only**: 31 doc-comment files, no tests; the fixtures
    are in place in the crate but nothing reads them. The `ais/update.rs`
    module the R3a README names does not exist yet.
13. **Rung numbering and order** between `doc/rust_port.md` (textures before
    inventory) and the fixtures ("rung seven") disagree (6.5 item 7).
14. **Undefined behaviour still present but unexercised**: unchecked casts
    of `LLInventoryObject` to `LLViewerInventoryItem` in `moveInventory`
    and `getInventoryItemByAsset` (R4a runtime edges), the uninitialised
    legacy type in task offer buckets (R4b runtime edges), `LLCategory`
    tables now bounded (bug 4). A Rust port removes these by construction;
    the recorders mark where the C++ value is not comparable.
