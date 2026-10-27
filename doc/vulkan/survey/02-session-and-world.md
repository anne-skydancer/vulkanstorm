# 02 — Vulkan session and world data

This chapter covers the CPU client model that feeds the Vulkan renderer
on `vkstorm-vulkan` at `51d1f729a6`. The model is the Vulkan session
(`VSNativeSession`), the scene store (`VSWorldScene`), and the avatar, terrain,
environment, region/parcel, material, texture, sound, media and task-inventory
owners around them.

**Headline status.** The Vulkan client is much further along than the
in-tree design notes say. It logs in, joins a circuit, receives capabilities,
keeps neighbour regions, and stores prims, meshes, sculpts, flexis, trees,
grass, avatars (with appearance, animation and server- or local-bake textures),
animesh, glTF-scene objects, terrain (legacy and PBR), particles, object
sounds and object media. Its main gaps are a hard 16384-object cap shared across
all regions, no object cache (`ObjectUpdateCached` always becomes a full
re-request), no prim hover text, no avatar impostors, a flat-colour water
quad, a separate mesh downloader with no disk cache, and CPU skinning that
re-uploads rigged meshes on every pose change. Several hot paths scan the whole
object map for each update.

Nothing in this chapter was built or run. Every status below comes from reading
the source and git history.

---

## Scope and size

Worktree: `codex/vulkan-survey` at `51d1f729a6` (merge of PR #225), compared with
`origin/vkstorm-release` where noted. All paths are relative to the worktree root;
`indra/newview/` is abbreviated to nothing when a bare `vs*` file name is given.

Commands used (run from `indra/newview` unless noted):

| Measurement | Command | Result |
|---|---|---|
| Vulkan file count / lines | `ls vs*.cpp vs*.h \| wc -l`; `cat vs*.cpp vs*.h \| wc -l` | 280 files, 63 353 lines |
| Guard lines outside `vs*` files | `grep -rn VS_NATIVE_VULKAN --include=*.cpp --include=*.h indra \| grep -v '/vs[a-z0-9]*\.\(cpp\|h\):' \| wc -l` (from repo root) | 1299 lines in 233 files |
| `vs*` files on release | `git ls-tree --name-only origin/vkstorm-release indra/newview/ \| grep -c indra/newview/vs` | 0 |
| Chapter-relevant files | `wc -l` over the 32 files listed below | 24 904 lines |
| Commits touching core files | `git log --oneline -- indra/newview/<f> \| wc -l` | session 58, scene 52, image provider 29, avatar 15, appearance 5, mesh assets 4 |
| Python fixtures | `ls scripts/tests/test_native_*.py \| wc -l` | 257 (`scripts/tests` holds 506 entries) |

Files covered here (lines): `vsnativesession.cpp` 2953, `vsnativecircuits.cpp` 437,
`vscapabilities.cpp` 156, `vsworldscene.cpp` 6745 (+ `vsworldscene.h` 346),
`vsworldavatar.cpp` 2399, `vsnativeappearance.cpp` 1732, `vsavatarmotions.cpp` 466,
`vsavatarnametagpolicy.h` 904, `vsworldassets.cpp` 326, `vsworldgltf.cpp` 1309,
`vsmaterial.cpp` 89, `vsmaterialassets.cpp` 202, `vsterraingeometry.cpp` 589,
`vsterrainassets.cpp` 199, `vsterrainshader.h` 632, `vsterrainpacket.h` 86,
`vsregionterrain.cpp` 60, `vsenvironmentstate.cpp` 87, `vsskygeometry.h` 102,
`vsregioncontext.cpp` 368, `vsregionlimits.cpp` 408, `vsparcelpresentation.cpp` 489,
`vsestatecovenant.cpp` 195, `vsuiimageprovider.cpp` 941, `vsworldsound.cpp` 216,
`vsobjectmedia.cpp` 324, `vstaskinventory.cpp` 558, `vsparticleeffects.cpp` 600,
`vsspatialindex.cpp` 149, `vsobjectindex.h` 74, `vsvegetation.cpp` 677,
`vsflexiblepath.cpp` 432.

Build gating: the Vulkan code compiles only when `USE_DILIGENTCORE` is set, which
defines `VS_NATIVE_VULKAN=1` (`indra/newview/CMakeLists.txt:2766-2767`). The replay
harnesses (`vsnativereplay.cpp`, `vsr*replay.cpp`, `vsvkdiag.cpp`) compile only under
`VS_VULKAN_DIAGNOSTICS` (`CMakeLists.txt:38-42`, `2390-2405`), which requires
`RelWithDebInfo`. At run time the Vulkan path is chosen by `RenderBackend == "Vulkan"`
(`llappviewer.cpp:1071-1074` → `initNativeVulkanLogin`, `llappviewer.cpp:1590`).

---

## Session lifecycle

### Ownership and entry points

| Item | Evidence |
|---|---|
| Owner | `LLAppViewer::mNativeSession` (shared_ptr), created in `initNativeVulkanLogin` (`llappviewer.cpp:1595`), shut down at `llappviewer.cpp:1845-1847`. A static weak pointer `sOwner` gives `VSNativeSession::active()` (`vsnativesession.cpp:327-349`). |
| Declared non-ownership | "Deliberately does not own LLWorld, LLViewerRegion or scene resources" (`vsnativesession.h:41`). The scene belongs to `VSVulkanContext` (`vsvulkancontext.cpp:308`) and is reached through `gViewerWindow->nativeContext()->world()`. |
| Login hand-off | `process_login_success_response` calls `VSNativeSession::acceptLogin` when a session is active (`llstartup.cpp:4810-4812`). |
| Start | `idle_startup` returns at `STATE_WORLD_INIT` for Vulkan and calls `begin()` (`llstartup.cpp:2069-2077`). `LLAppViewer` also calls `begin()` when the state is `STATE_WORLD_INIT` (`llappviewer.cpp:1762`). |
| Per-frame | `mNativeSession->tick()` (`llappviewer.cpp:1763`), followed by audio, AFK and media-focus pumps (`llappviewer.cpp:1765-1785`). |

### Phases and startup sub-states

`enum class Phase { Login, Connecting, Connected, Logout, Disconnected }`
(`vsnativesession.h:46`). There is a second, private
`enum class Startup { Idle, Inventory, Misc, Precache, Wearables, Complete }`
(`vsnativesession.h:246`) that mirrors the reference `STATE_INVENTORY_CALLBACKS`
→ `STATE_STARTED` sequence.

| Step | Function | What happens |
|---|---|---|
| Accept login | `acceptLogin` `vsnativesession.cpp:512-578` | Validates agent/session/host/circuit/seed URL and region size. Fills `gAgentID`, `gAgentSessionID`, `gAgentUsername`, maturity, buddy list, contact sets and benefits. Sets `gAgent` origin. No `LLAgent::init` scene bootstrap. |
| Begin | `begin` `:579-615` | Installs handlers (`install`, `:407-502`). Initialises the asset blacklist. Creates the first `RegionCircuit`. Calls `world().activateRegion` and `world().initializeSelf`. Sends `UseCircuitCode` with a weak-owner delivery callback (`:307-316`), launches `seedCoro`, and sets `STATE_WORLD_WAIT`. |
| Circuit ACK | `circuitResult` `:616-627` | Sends `CompleteAgentMovement` and `AgentWearablesRequest`. |
| Seed capabilities | `seedCoro` `:628-668` → `capabilities` `:669-702` | POSTs `vs_native_capability_names()`. Makes up to 3 attempts with a 15 s timeout. Admits only `http(s)` URLs, through `webURL` `:321-324`. Starts `LLEventPoll` and launches `simulatorFeaturesCoro` (`:721-758`, 2 attempts). |
| Connected | `connected` `:1998-2116` | Runs once handshake, movement, seed and ACK have all arrived. Creates `LLMaterialMgr`, particles, `LLEnvironment`, `LLViewerCamera`, `VSNativeAppearance`, inventory background fetch, experiences, Vulkan tools, voice authorisation, chat panel, mute list and money balance. |
| Startup advance | `advanceStartup` `:2193-2285` | Inventory → Misc (environment settings, audio, gestures, pathfinding) → Precache (`PrecachingDelay`, needs a self avatar) → Wearables (10 s clothing wait, `ClothingLoading` notice on timeout) → `STATE_STARTED`. |
| Logout | `requestLogout` `:2658-2676` → `logoutFramePresented` `:2677` → `finishLogout` `:2684` | Waits until the logout frame has been presented before tearing down. |
| Disconnect | `disconnect` `:2693-2702` | Sends `LogoutRequest`, resets, then shows an `ErrorMessage` whose callback calls `reset_login` only if the generation is unchanged. |
| Reset | `reset` `:2703-2884` | Retires more than 40 shared singletons and account services: tracker, world map, tools, voice, material manager, mesh uploads, particles, sounds, selection, appearance, gestures, AO, neighbour circuits (`AgentQuitCopy`), scene, parcel media, environment, covenants and others. Increments `mGeneration` and `mRegionEpoch`. |

`regionReady()` is defined as `Connected && !mChangingRegion && mSeedReady &&
mHandshake && mCircuitAck` (`vsnativesession.h:151`).

### Account guards, handles and generations

The session protects asynchronous deliveries with generation tokens:

| Guard | Evidence | Meaning |
|---|---|---|
| `mGeneration` (account) | `vsnativesession.h` member; incremented in `reset` (`vsnativesession.cpp:2790`) | Retires every callback from a previous login. |
| `mRegionEpoch` | incremented in `changeRegion` (`:1307`) and `reset` (`:2790`) | Retires capability and seed completions from a previous current region. |
| `accountGuard(bool allowConnecting)` | `:2117-2126` | Weak owner + generation + phase check. |
| `AccountHandle` / `InventoryHandle` | `vsnativesession.h:74-79`, `vsnativesession.cpp:2127-2144` | The inventory handle also compares the item's asset ID and `gInventory.objectRevision`. |
| `interactionGuard(FaceHandle)` | `:2145-2152` | Account guard AND scene face guard. |
| Per-circuit `incarnation` | `RegionCircuit` `vsnativesession.h:267`; `vsnativecircuits.cpp:152-200` | Neighbour circuits carry their own generation (`circuitGeneration`, `vsnativecircuits.cpp:146`). |

Coroutines (`seedCoro`, `simulatorFeaturesCoro`, mesh fetch, bake) post their
results back to the `"mainloop"` `LL::WorkQueue` and check generation and epoch
again there (for example `vsnativesession.cpp:646-660`, `:735-745`). Session state
is touched only on the main thread. **Status: Implemented.**

### Message admission and handler registration

`install` sets a message-system-wide admission predicate,
`msg.setMessageAdmission(... admit ...)` (`vsnativesession.cpp:410-414`). The policy
is in `admit` (`:1346-1373`):

| Category | Rule (line) |
|---|---|
| Transport (`StartPingCheck`, `CompletePingCheck`, `PacketAck`) | From any known circuit while not `Disconnected` (`:1350`) |
| World set (12 names, `:194-198`: object updates, `KillObject`, `LayerData`, `AvatarAppearance`, `AvatarAnimation`, `ObjectAnimation`, `ViewerEffect`, `ReplyTaskInventory`, `GenericStreamingMessage`) | Current host, or a neighbour that is acknowledged and handshaken, during `Connecting`/`Connected` (`:1363-1364`) |
| `LayerData` | Any *known* host, without requiring the neighbour handshake (`:1355`) |
| Region and estate (`RegionInfo`, `EstateOwnerMessage`, `TelehubInfo`, `LandStatReply`, `SimConsoleResponse`) | Current host and `regionReady()` (`:1352`) |
| Parcel replies | Connected, not changing region, from an acknowledged and handshaken circuit (`:1356-1358`) |
| Session set (116 names, `:199-305`) | Current host only, during `Connecting`/`Connected`/`Logout` (`:1368-1369`) |
| Anything else | Rejected; the first 8 rejections are logged (`:1370-1372`) |

Handler routing: `install` has 58 `setHandlerFunc` call sites (`:407-502`). One
loops the 12 world messages to `receiveMessage` (`:416`). One loops 33
region/session names to `receiveMessage` (`:419-421`). The rest bind shared viewer
handlers directly: sounds (`:428-431`), `SimStats`, `MeanCollisionAlert` (`:433`,
fixed by the recent collision-alert commit), script dialogs, groups, directory,
avatar properties and others (`:432-500`). Instant messages are installed by
`vs_native_im_install` (`:501`, `vsnativeim.cpp:123`).

Event-queue (LLSD) messages are handled in `deliver` (`:1719-1986`).
`RegionHandshake`, `EnableSimulator`, `TeleportFinish`, `CrossedRegion`,
`DisableSimulator` and about 25 other names are dispatched there.

**Finding (static evidence, likely bug).** `ParcelAccessListReply`,
`ParcelDwellReply`, `ParcelObjectOwnersReply` and `EstateCovenantReply` are
admitted (`:1356`), and `receive` has a branch for them (`:1433-1441`). However,
`install` never routes those names to `receiveMessage`. The shared registrations
for them live in `register_viewer_callbacks` (`llstartup.cpp:3885-3888`, `:3978`,
`:3985`), which runs at `STATE_SEED_CAP_GRANTED` (`llstartup.cpp:2332`). The Vulkan
path returns earlier, at `STATE_WORLD_INIT` (`llstartup.cpp:2071-2077`).
Inference: in a Vulkan session these four UDP replies are admitted but have no
handler, and the `receive` branch is reachable only by direct callers. The
branch was added in `4a4b951e67` without a matching routing entry
(`git show 4a4b951e67 -- indra/newview/vsnativesession.cpp`).

**Finding (dead branches).** `SimStats`, `AlertMessage` and `AgentAlertMessage`
are first routed to `receiveMessage` (`:419-421`) and then re-bound to the shared
`process_sim_stats`, `process_alert_message` and `process_agent_alert_message`
(`:432`, `:439-440`). The later binding wins, so the Vulkan `deliver` branches for
the UDP forms are unreachable. Inference: the shared handlers are probably what
was intended, but the duplicated code is misleading. Likewise `PayPriceReply` is
handled at `:1377` and then listed again in the condition at `:1399`.

### Capabilities (`vscapabilities.cpp`)

- `vs_build_capability_names` (`vscapabilities.cpp:7-143`) is the catalogue
  transposed from `LLViewerRegionImpl`. The OpenGL viewer now delegates to it as
  well (`llviewerregion.cpp:3460`).
- `vs_native_capability_names` (`:144-156`) appends 61 names (62 with `FlickrConnect`) that the Vulkan
  session needs whatever the HTTP-inventory policy, plus `FlickrConnect`
  (`:152-153`). The list is de-duplicated with a `std::set`. This is the
  FlickrConnect fix from `bcdfc2af48`, and it is syntactically correct: the
  preceding element `"FindExperienceByName",` ends with a comma.
- The session admits only the names it requested and only valid web URLs
  (`vsnativesession.cpp:674-679`). Neighbour seeds use the same list
  (`vsnativecircuits.cpp:255`).

**Status: Implemented.** The catalogue is shared with OpenGL, so any edit to
`vs_build_capability_names` changes both renderers (see Risks).

### Region crossing, teleport and neighbours

- `changeRegion` (`vsnativesession.cpp:1276-1334`) is used for both crossing and
  teleport. It validates the destination, reuses or creates a circuit, calls
  `world().activateRegion`, moves the old event poll into the old circuit,
  increments `mRegionEpoch`, and resends `UseCircuitCode` and the seed request.
  A teleport retires every other circuit (`:1308-1314`).
- Neighbours: `EnableSimulator` → `enableNeighbour` (`vsnativecircuits.cpp:152`,
  at most 65 circuits, `:165`). Each neighbour has its own seed coroutine
  (`:246`), simulator features (`:350`), and retirement through `AgentQuitCopy`
  (`:185-199`). `FSDisableNeighbourRegionConnections` is honoured
  (`vsnativesession.cpp:1736`).
- `tick` retires dead neighbour circuits and disconnects when the current
  circuit dies (`vsnativesession.cpp:2556-2581`).

**Status: Implemented.**

### Divergent mechanics inside the session

- The session pumps `LockMessageChecker` itself, at most 100 messages per frame
  (`vsnativesession.cpp:2574-2578`). Inference: under heavy traffic this cap
  could make the inbound queue fall behind.
- Parcel properties are polled with `ParcelPropertiesRequest` whenever the agent
  crosses a 4 m cell, at most once per second (`requestParcel`, `:760-776`;
  trigger at `:2600-2606`). Inference: this differs from the reference viewer,
  which relies mostly on simulator-pushed agent-parcel updates.
- `AgentUpdate` is sent at 10 Hz, or at once when control flags change
  (`:2612-2617`).

---

## Scene and object store

### Structure

`VSWorldScene` (`vsworldscene.h:36-346`) is a pImpl over `struct Impl`
(`vsworldscene.cpp:252-1835`). One root scene holds the current region. Neighbour
regions are child scenes in `std::map<LLHost, std::unique_ptr<VSWorldScene>>
mNeighbours` (`vsworldscene.h:344`), capped at 64 (`vsworldscene.cpp:1981`). All
regions share one `Budgets` object (`vsworldscene.cpp:243-251`):

| Budget | Value (`vsworldscene.cpp:250`) | Scope |
|---|---|---|
| `objects` | `maxObjects` = 16384 (`:108`) | **All regions combined** |
| `avatars` | 256 | All regions; also `avatars.size()>=256` per region (`:755`) |
| `objectRequests` | 4096 | All regions; per region `pending.size()>=1024` (`:1207`) |
| `primitiveBytes` | 128 MiB | Prim volumes, metadata, flex, animations, spatial index, parcel bitmaps |
| `terrainBytes` | 64 MiB | Terrain patches, sky mesh scratch, paint map, name tags |

Each `Impl::Object` (`:354-490`) holds the decoded update: pcode, transform,
motion, TEs, volume params, `LLVolume`, mesh asset, glTF owner, flex state,
particles, sound, media, render materials, control-avatar owner, task-inventory
serial, and the cached pick and spatial state.

### Identity: handles and generations

- `ObjectHandle {host, id, local, region (=incarnation), generation, kind, account,
  accountGeneration}` (`vsworldscene.h:50-56`). `FaceHandle` adds a face index and
  the object `revision` (`:60`).
- `current(ObjectHandle)` re-resolves by host and local ID and compares every
  field (`vsworldscene.cpp:2036-2041`). `guard()` returns a closure over a
  `weak_ptr<Impl>` that also checks `retired`, incarnation and account
  (`:2994-3026`).
- An object keeps its `generation` while its UUID, pcode and owner kind stay the
  same (`:1312`). Its `revision` advances on every full update (`:1313`).
- A region swap during crossing keeps the `Impl` objects alive and swaps the
  pointers (`activateRegion`, `:1988-2011`), so existing guards stay valid.

Naming nit: `ObjectHandle::region` holds the region *incarnation*, not the
region handle.

### Packet admission (`VSWorldScene::receive`, `vsworldscene.cpp:3252-3737`)

| Message | Handling | Status |
|---|---|---|
| `ObjectUpdate` (full) | Validates sizes, finiteness and scale, NameValue (≤4096), MediaURL, ExtraParams, PSBlock, TextureAnim. Decodes volume params with `LLVolumeMessage`, TEs with `LLPrimitive::unpackTEMessage`. Caps faces at 45 and objects at `maxObjects` (`:3584-3734`). | Implemented |
| `ObjectUpdateCompressed` | `Impl::compressed` (`:1425-1576`). Checks string termination before `unpackString` (`:1442-1446`). | Implemented |
| `ImprovedTerseObjectUpdate` | Decodes the quantised transform in place (`:3549-3581`). Unknown ID → `request(id)`. **A terse update that carries a TextureEntry triggers a full re-request instead of being parsed** (`:3579`). | Partial |
| `ObjectUpdateCached` | **Always** `request(id)`, which becomes `RequestMultipleObjects` with `CACHE_MISS_TYPE_TOTAL` (`:3538`, `:3738-3757`). No object cache exists: `grep -l "LLVOCache\|vocache" vs*` returns nothing. | Stub (re-request only) |
| `KillObject` | `eraseObject` per ID (`:3520-3527`). Children are marked `parentRetired` but not erased (`:1236`). | Partial (see divergence) |
| `GenericStreamingMessage` (GLTF overrides) | Notation LLSD ≤64 KiB, ≤45 sides. Overrides are held pending (≤4096) until the object arrives (`:3261-3285`). | Implemented |
| `ViewerEffect` | Beam, point, sphere, spiral and edit go to the shared `LLHUDManager::processViewerEffectBlock` (`:3311`). Look-at and point-at drive `VSWorldAvatar::attention`. Reset-skeleton is accepted only for the sender's own avatar (`:3316-3337`). | Implemented |
| `AvatarAppearance` / `AvatarAnimation` / `ObjectAnimation` | Routed to avatar or control-avatar owners. `ObjectAnimation` for a not-yet-seen object is held ≤60 s (`:3437-3505`). | Implemented |
| `LayerData` | `'L'`/`'M'` → terrain, `'7'`/`'9'` → wind. Other types, including cloud layers, are ignored (`:3507-3518`). | Implemented |
| `ReplyTaskInventory` | Linear scan of all objects for the matching UUID (`:3425-3436`), then `VSTaskInventory::receive` (`vsnativesession.cpp:1421-1426`). | Implemented |

**Hover text is absent.** The compressed path parses the `Text` and `Color`
fields and discards them (`vsworldscene.cpp:1461`). The full path never reads
`"Text"`. The only `LLHUDText` user in Vulkan code is `vsbeaconpresentation.cpp:100`.
Floating prim text (`llSetText`) is therefore not represented in the Vulkan
model.

**Lights.** Only a boolean `light` is retained (`:1507`, `:3679`, `:2321`). It is
used for render cost (`:1764`) and debug beacons (`:6338`). No local-light list
is published from the scene. Inference: local lights depend on the renderer
consuming the light parameters held in `VSObjectEditParameters`, and this
chapter found no such consumer.

### Replacement, retirement and indices

- `replace` (`vsworldscene.cpp:1272-1424`) reserves metadata, slot and primitive
  bytes. On refusal it calls `request(id)` and drops the update (`:1290-1305`).
  Derendered and blacklisted objects are erased (`:1306-1311`). When the identity
  is the same, it carries over mesh, published faces, media, glTF, flex, timing,
  task-inventory and control-avatar state (`:1312-1397`).
- `VSObjectIndex` (`vsobjectindex.h`) keeps `identities` (UUID→local) and
  `children` (parent→child, ordered) maps. These serve `object(LLUUID)`
  (`vsworldscene.cpp:2042-2055`), `linkedFamily` (`:2056-2109`) and
  `directChildren` (`:5537`).
- `VSSpatialIndex` (`vsspatialindex.cpp`, a Boost.Geometry R-tree whose allocator
  is charged to `primitiveBytes`) is used **only** for selection candidates
  (`vsworldscene.cpp:1775-1820`, `:6545-6631`). Per-frame packet building does not
  use it.

**Performance finding (static).** Several per-update paths ignore the indices and
scan the whole `objects` map:

| Path | Line | Cost per call |
|---|---|---|
| `replace`: find an earlier local ID with the same UUID | `:1276-1284` | O(N), even though `objectIndex.identities` exists |
| `eraseObject`: mark children `parentRetired` | `:1236` | O(N) |
| `dirtyChildren` (every terse update `:3577` and every `replace` `:1418`) | `:1822-1832` | At least one full O(N) pass, up to 256 passes |
| `ReplyTaskInventory` lookup | `:3430` | O(N) |
| `terrain()`: mark grass dirty in this and every neighbour region | `:893-905` | O(total objects) per LayerData packet |

Inference: with thousands of objects and hundreds of terse updates per second,
these scans grow as O(updates × objects). No measurement exists in this tree.

### Per-frame packet production

`packets()` (`vsworldscene.cpp:3815-4982`) pumps mesh assets (`:3819`) and
publishes ground (`:3820`). It then walks `mImpl->objects` several times
(`:3841`, `:3908`, `:4022`, `:4113`, `:4221`) using a distance test against the far
clip (`:3933-3935`). It does no frustum or octree culling on the CPU (none found).
`linksCurrent` (`:483-494`) walks the parent chain with a fresh `std::set` for
each object. Prim LOD uses `RenderVolumeLODFactor` with a fixed PI/3 field of
view: "Vulkan presentation currently uses the reference default PI/3 FOV"
(`:123`). Inference: zooming the camera does not change prim LOD as it does in
OpenGL.

**Status of the scene store: Implemented, with Partial areas** (object cache,
terse TEs, hover text, lights, global object cap).

---

## Objects and prims

| Feature | Vulkan implementation | Status |
|---|---|---|
| Parametric prims | `new LLVolume(params, 2.f)` (`vsworldscene.cpp:3728`, `:1546`); LOD via `meshDetail` / `vs_volume_lod_detail` (`:109-125`, `vsvolumelod.h`) | Implemented |
| Sculpties | `volume->sculpt(...)` from the resolved sculpt image raw (`:4506`) | Implemented |
| Mesh | `VSWorldAssets` (`vsworldassets.cpp`), requested per LOD in `packets()` (`:3937-3946`, `:4481`) | Implemented, separate downloader (see below) |
| Flexi | `VSFlexibleSection` / `vsflexiblepath.cpp` ("shared CPU flexible-object solver, used by both peer renderers") | Implemented |
| Trees / grass | `VSTreeGeometry : private LLVOTree::TreeSpeciesData`, `vs_grass_mesh` (`vsvegetation.h`); LOD `vs_tree_lod` (`vsvegetationlod.h`); grass follows terrain height | Implemented |
| Texture animation | `LLViewerTextureAnim` decoded (`:213`, `:3700-3711`) | Implemented (data) |
| Particles | `VSParticleEffects` drives the shared `LLViewerPartSim` / `LLViewerPartSourceScript` (`vsparticleeffects.cpp:73`, `:89`, `:219`) | Implemented |
| Animesh | `controlAvatar` (`VSWorldAvatar`, `controlAvatar=true`) per object, `controlAnimations` (`:419-421`, `:3437-3460`) | Implemented |
| glTF scene objects (`LL_SCULPT_TYPE_GLTF`) | `VSWorldGLTF` on the shared `LL::GLTF::Asset` (`vsworldgltf.cpp`, `vsworldscene.cpp:3218-3226`, `:4422-4429`); CPU animation (`vsworldgltf.cpp:351-360`) | Implemented (experimental feature upstream as well) |
| Reflection probes | Flag only (`:236`, `:1504`) | Partial (data) |
| Hover text | Discarded (`:1461`) | **Absent** |
| Object cache | None | **Absent** |

### Mesh assets (`vsworldassets.cpp`)

- The fetch uses `GetMesh2`, then `GetMesh`, then `ViewerAsset` (`:247-249`), with
  at most 4 concurrent requests (`:259`) and 1024 entries (`:225`). The CPU cache
  is 128 MiB with LRU eviction (`:23`, `:154`, `:305`).
- **Each (mesh ID, LOD) key is a separate HTTP request for the byte range
  `0-16777215`** (`:222`, `:271`). The decoder then keeps only the requested LOD.
  Inference: for an object that moves through several LODs, the same asset can be
  downloaded up to 4 times.
- There is **no disk cache**. `gMeshRepo` is initialised under Vulkan only for uploads
  and cost formulas (`llappviewer.cpp:1665`, `:1745`; `vsworldscene.cpp:6159`). It
  is not used to fetch render meshes.
- Decoding runs on the **main loop** inside the completion
  (`vsworldassets.cpp:277-311`; the comment says "Decode and publish only when the
  mainloop executes this delivery"). The zlib inflate and `LLVolume` construction
  are therefore on the frame thread.
- Physics sections are not decoded (comment at `:274`).

**Status: Partial.** It works on the evidence, but it diverges from
`LLMeshRepository` in caching, LOD fallback and threading.

---

## Avatars

`VSWorldAvatar` (`vsworldavatar.h`, `vsworldavatar.cpp` 2399 lines) is a Vulkan
avatar owner. Its `Impl` derives from the shared appearance library,
`struct VSWorldAvatar::Impl : LLWearableData, LLAvatarAppearance`
(`vsworldavatar.cpp:159`), and uses `NativeJoint : LLAvatarJoint`,
`NativeJointMesh : LLAvatarJointMesh` and `NativeLayers : LLTexLayerSet`
(`:99-158`). It does **not** create an `LLVOAvatar`.

| Area | Evidence | Status |
|---|---|---|
| Creation and caps | `Impl::avatar` (`vsworldscene.cpp:752-763`): 256 per region and a shared 256 budget | Implemented (capped) |
| Appearance message | `VSWorldAvatar::appearance` (`vsworldavatar.cpp:1415`) | Implemented |
| Animations | `animations(LLMessageSystem&)` (`:1460`). Motion registration copied out of `llvoavatar.cpp` into `vsavatarmotions.cpp:358-405`, including `LLPhysicsMotionController`, `LLEditingMotion`, `LLTargetingMotion` and the `LLPelvisFixMotion` class. Default motions at `:407-411`. | Implemented |
| Look-at / point-at | `attention` (`:1288`), `vs_attention_motion` (`:964`) | Implemented |
| Server-side bakes | `VSNativeAppearance::bake` POSTs `UpdateAvatarAppearance` (`vsnativeappearance.cpp:279-300`), at most 3 attempts | Implemented |
| Local bakes (no `UpdateAvatarAppearance`, e.g. OpenSim) | `legacyBake` (`:401-490`) composites on the CPU (`NativeCompositeImage`, `NativeLayers`) and uploads through `UploadBakedTexture` or asset storage | Implemented |
| Baked texture lookup for magic IDs | `vs_baked_texture_for_magic_id`, `vsbakedtexturepolicy.h` (used at `vsworldscene.cpp:654-659`) | Implemented |
| Body LOD | `vsavatarlod.h` ("Shared system-avatar body LOD policy from LLVOAvatar::updateJointLODs"), also used by OpenGL (`llvoavatar.cpp:36`) | Implemented |
| Rigged meshes | CPU skinning: `skinRiggedFace` (`vsworldavatar.cpp:1895-1956`), called from `appearanceRiggedVertices` (`vsworldscene.cpp:3797-3813`). Re-skinned and re-uploaded when `poseGeneration` changes (`:4784-4798`). | Implemented, **CPU-bound** |
| Pose change detection | Signature over every joint matrix, every visual-parameter weight and cloth state (`vsworldavatar.cpp:~975-1007`) | Implemented |
| Attachments | Parent chain to an avatar (`skeletonOwner` `vsworldscene.cpp:506-523`, `hudObject` `:524-541`); `attachments()` (`:2600`); `attachmentTransform` (`vsworldavatar.cpp:1890`) | Implemented |
| Attach / detach / wear | `VSNativeAppearance::attach/detach/wear/remove/wearCategory` (`vsnativeappearance.h`) | Implemented |
| Visibility policy | `complex`, `slow`, `visuallyMuted`, `overallAppearance` (`vsworldavatar.cpp:398-448`) mirror `LLVOAvatar::isTooComplex` / `getOverallAppearance`. Friends-only hiding was restored by `e77e965d88` (`:440-442`). | Implemented |
| Auto-tune (ART) | `updatePerformance` (`:449-505`) over Vulkan owners, using measured GPU timings | Implemented |
| Jellydoll | `Visibility::Jellydoll` (`:83-88`, `:1215-1274`) | Implemented |
| **Impostors** | No `RenderAvatarMaxNonImpostors` handling (`grep -n MaxNonImpostors vs*` returns nothing). `FSImpostorAvatarExclude` is read only for exclusion rules. | **Absent** |
| Name tags | `VSAvatarNameTag`, policy in `vsavatarnametagpolicy.h` (904 lines, shared with OpenGL, `llvoavatar.cpp:3275`) | Implemented |
| Voice visualiser / lip sync | `LLVoiceVisualizer`, `mOohMorph`/`mAahMorph` (`vsworldavatar.cpp:200-206`), `appendVoice` (`:2268`) | Implemented |
| Avatar cloth | `VSAvatarCloth` (`:176-179`) | Implemented |
| Reset skeleton | `resetSkeleton` (`:1111`), with a wire broadcast from `VSNativeSession::resetAvatar` (`vsnativesession.cpp:350-378`) | Implemented |

**Divergence:** the reference renderer skins on the GPU with a joint-matrix
palette. The Vulkan path skins on the CPU and uploads a new mesh for every rigged
face whenever the pose signature changes, which for an animated avatar is about
every frame (`vsworldscene.cpp:4784-4798`). `vsworldrenderer.h` has no joint or
palette type (`grep -n "joint\|palette" vsworldrenderer.h` returns nothing).
Inference: in crowds, CPU time and upload bandwidth will scale with total rigged
vertex count. This is the largest predictable performance gap in this chapter.

**Status: Implemented in breadth. Partial for scale** (impostors absent, CPU
skinning, 256-avatar cap).

---

## Terrain

| Piece | Evidence | Status |
|---|---|---|
| Bitstream decoding | `VSTerrain::decode` (`vsterrainpacket.h`, bounded), then the shared `decompress_patch` / `init_patch_decompressor` from `patch_dct` (`vsworldscene.cpp:856-873`) | Implemented |
| Patch store | `std::map<U64, Ground>` with `heights[1024]` per patch, at most 4096 patches, charged to `terrainBytes` (`:764-772`, `:876-879`) | Implemented |
| Geometry | `VSTerrainGeometry` "Shared CPU terrain strip/LOD topology from the reference viewer" (`vsterraingeometry.h`), including north/east stitching | Implemented |
| Neighbour edges | Dirtying across regions through the `Budgets::regions` weak registry (`vsworldscene.cpp:893-919`) | Implemented |
| Legacy texture terrain | `VSTerrainAssets`, ramp image `terrain:alpha-ramp`, `vs_request_world_image(id,128)` (`vsterrainassets.cpp:88`) | Implemented |
| PBR terrain | `LLFetchedGLTFMaterial` per layer, honouring `RenderTerrainPBREnabled`/`Force`/`Detail`/`PlanarSampleCount`/`TriplanarBlendFactor` (`vsterrainassets.cpp:94-138`) | Implemented |
| Local paint map | `createTerrainPaintMap` is a CPU transposition of the reference GL bake (`vsworldscene.cpp:3118-3179`) | Implemented |
| Region terrain UI values | `vs_region_terrain` (`vsregionterrain.cpp`), shared with OpenGL region UI | Implemented |
| Wind | `LayerData '7'/'9'` → `windX/windY` (`:787-820`), `windVelocity` (`:2766`) | Implemented |

Height sampling for avatars and grass uses `groundAt` (`:665-751`). **Status:
Implemented.** Shading parity belongs to the rendering chapter.

---

## Environment, sky and water

- **Environment** reuses the shared `LLEnvironment` (EEP) singleton. The session
  creates it (`VSNativeSession::initializeEnvironment`, `vsnativesession.cpp:1987-1997`),
  loads per-account settings in `Startup::Misc` (`:2212`), updates it each tick
  (`:2650-2651`), calls `onRegionChange` on crossing (`:2022`), and deletes it on
  reset.
- **Shader values:** `vs_environment_state` (`vsenvironmentstate.cpp:39-87`) calls
  the shared `LLEnvironment::updateGLVariablesForSettings` into a `thread_local`
  `LLShaderUniforms` array and copies 23 `vec4` slots (sun, moon, ambient, haze,
  clouds, water fog, gamma, droplet and ice). It **throws** `std::runtime_error`
  when a uniform is missing or any value is non-finite (`:20`, `:32`, `:84-85`). It
  is called from the frame path at `vsvulkancontext.cpp:580-581`.
- **Sky:** a dome mesh from `vs_sky_strips` sized by `WLSkyDetail` and the dome
  radius (`vsworldscene.cpp:1079-1111`), with rainbow and halo images from the sky
  settings (`:1128-1129`).
- **Water:** **one flat quad per region** at `waterHeight`, constant colour
  `{.08,.28,.38,.65}`, alpha-sorted (`vsworldscene.cpp:1171-1178`). It is reset
  when `RegionHandshake` changes the water height (`:3241-3242`). The renderer has
  no water-specific code (`grep -in water vsworldrenderer.cpp` returns nothing).
  There is no void water beyond region edges and no reflection or refraction.

**Status:** Environment values Implemented; sky Implemented (dome); water
**Partial (placeholder)**.

---

## Regions, parcels and land

There is no `vsland*` file (`ls vsland*` fails). Land functionality is spread
across the region and parcel owners below.

| Owner | Role | Status |
|---|---|---|
| `VSNativeSession::RegionSnapshot` / `RegionCircuit` (`vsnativesession.h:114-139`, `:267-302`) | Per-circuit name, ID, owner, flags, access, water, metadata (billable factor, protocols, CPU class, SKU), object capacity, coarse locations, parcel overlay | Implemented |
| `VSRegionContext` (`vsregioncontext.h`, `.cpp` 368) | Value object that shared UI uses in place of `LLViewerRegion*`; `legacy(LLViewerRegion*)` vs `native(const LLHost&)` (`vsregioncontext.h:66-70`) | Implemented |
| `VSRegionLimits` (`vsregionlimits.cpp`) | "Shared CPU simulator region limits, transposed from LLWorld"; used by OpenGL `llsurface.cpp` and `llviewerobject.cpp` as well | Implemented |
| Region info / estate | `applyRegionInfo` (`vsnativecircuits.cpp:293`); `EstateOwnerMessage` → shared `LLFloaterRegionInfo` (`vsnativesession.cpp:1393-1398`) | Implemented |
| Parcel overlay | `receiveParcelOverlay` (`vsnativecircuits.cpp:88`); exactly 1024-byte chunks (`vsnativesession.cpp:1448-1456`) | Implemented |
| Parcel properties | Shared `LLViewerParcelMgr::processParcelProperties` running in a Vulkan CPU mode (`mNativeCpu`, `llviewerparcelmgr.cpp:150`; Vulkan bitmap `:2038-2050`); the agent parcel is published back to the session (`:2120`, `:2182`) | Implemented |
| Parcel access, dwell, object owners, covenant | Admitted but probably without a handler; see Session finding | **Probably broken (Inference)** |
| Parcel borders and feedback | `VSParcelPresentation` (`vsparcelpresentation.cpp`), geometry from `llglsandbox.cpp` (`vsparcelgeometry.h`) | Implemented |
| Parcel permissions | `VSParcelPermissions::from(regionFlags, parcelFlags, seeAvatars)` (`vsnativesession.h:180-181`) | Implemented |
| Covenant | `vs_receive_covenant` (`vsestatecovenant.cpp`) | Implemented, reachability doubtful (see above) |
| Parcel audio / media | `requestParcelPlayback`, `parcelMedia`, `parcelMediaCommand` (`vsnativesession.cpp:777-1037`) over the shared `LLViewerMediaImpl` | Implemented |
| Coarse locations | UDP and EQ paths into `VSCoarseSnapshot` (`vsnativesession.cpp:1457-1473`; `vsnativecircuits.cpp:113`) | Implemented |

`gViewerWindow`-level shared land UIs are covered in the UI chapter.

---

## Materials and glTF

| Piece | Evidence | Status |
|---|---|---|
| Material snapshot model | `VSMaterial::State`, `legacy(LLTextureEntry, LLMaterial*)`, `pbr(LLGLTFMaterial, overrides)` (`vsmaterial.h`) | Implemented |
| Legacy (Blinn-Phong) materials | `VSMaterialAssets` fetches `RenderMaterials` per region with the shared `LLMaterial` decoder (`vsmaterialassets.cpp:1`, `:80`, `:155`); `put` for edits (`:127`) | Implemented |
| PBR materials | `gGLTFMaterialList.getMaterial(id)` per face (`vsworldscene.cpp:1226-1229`); `LLFetchedGLTFMaterial` (`:272`) | Implemented |
| GLTF overrides | `GenericStreamingMessage` → `applyMaterialOverrides` (`:1211-1232`, `:3261-3285`) | Implemented |
| glTF scenes | `VSWorldGLTF` (`vsworldgltf.cpp`); image preparation on the `General` queue (`:489-491`, `:665-666`); local file loading through `VSWorldScene::loadGLTF` (`vsworldscene.cpp:2110`) | Implemented |
| Material editor previews | `previewMaterialID`, `setLegacyMaterialPreview` (`:2697`, `:6638`) | Implemented |

Open question: the session also creates the shared `LLMaterialMgr`
(`vsnativesession.cpp:2029`) and retires it on reset (`:2730`;
`llmaterialmgr.cpp:1066-1076`). Two owners may therefore fetch `RenderMaterials`:
the shared manager for editor puts, and `VSMaterialAssets` for rendering.
Inference: possible duplicate requests. This was not verified.

---

## Textures and images

All world and UI images go through `VSUIImageProvider` (`vsuiimageprovider.cpp`).

| Stage | Evidence | Notes |
|---|---|---|
| Request | `vs_request_world_image(id, dimension, exact)` (`:707-728`) records per-frame peak demand (`frameTarget`/`previousTarget`) | Demand comes from the scene (`vsworldscene.cpp:4335`, `:4734-4750`), terrain, particles and glTF |
| Fetch | Shared `LLTextureFetch::createRequest`, with the URL from `ViewerAsset` or `GetTexture` (`vsuiimageprovider.cpp:804-815`, `createRequest` at `:806`); at most 3 attempts with backoff | Reuses the shared fetch worker, decode thread and `LLTextureCache` (evidence key `"cache_policy" = "shared_LLTextureFetch"`, `:396`) |
| Discard selection | `worldImageDimension` and `RenderMaxTextureResolution` (512–4096), `TextureLoadFullRes`, one-level hysteresis (`:786-803`) | Mirrors the reference virtual-size behaviour |
| Upload preparation | `beginPreparation` posts a mip chain or optional BC3 build to the `"General"` work queue and returns to `"mainloop"` (`:158-190`; `vstextureupload.h`) | Off the main thread |
| Residency and eviction | At most 16384 entries (`:471`, `:663`, `:710`, `:733`); a 64 MiB mask budget (`:149`); `evict` is a linear LRU scan that skips images used within the last second or still referenced by packets (`:193-221`) | Implemented; O(N) eviction |
| Memory policy | `textureMemoryPolicy().bias > 1.9` gives coarser targets under pressure (`:277-283`, `:790`); `vstexturememory.h` transposes `LLViewerTexture::updateClass` | Implemented |
| Region change | A region-epoch change deletes **every in-flight fetch** and resets entries that are not ready (`:261-273`) | Divergence (see below) |
| Avatar bakes | Magic baked IDs resolved through `bakedTexture` (`vsworldscene.cpp:645-651`) | Implemented |
| Map tiles | `requestMapImage` with `FTT_MAP_TILE` (`:653-705`) | Implemented |

**Status: Implemented.** It diverges in eviction cost and in cancelling fetches on
crossing.

---

## Other: sounds, media, task inventory, spatial queries

| Subsystem | Evidence | Status |
|---|---|---|
| Object loop sounds | `VSWorldSound` creates shared `LLAudioSourceVO` instances with a state callback instead of an `LLViewerObject` (`vsworldsound.cpp:69-84`); `llaudiosourcevo.cpp` is +109/−49 against release (`git diff --numstat origin/vkstorm-release...HEAD`) | Implemented |
| `SoundTrigger`, `PreloadSound`, `AttachedSound`, `AttachedSoundGainChange` | Shared handlers with `#if VS_NATIVE_VULKAN` branches into `worldSound()` (`llviewermessage.cpp:5228-5234` etc.) | Implemented |
| Collision alerts | `MeanCollisionAlert` → shared handler (`vsnativesession.cpp:433`) | Implemented (recent fix) |
| Object media (MOAP) | `VSObjectMedia` uses the `ObjectMedia` capability and shared media policy and plugins (`vsobjectmedia.cpp`); `syncMediaData` / `sendMediaDataUpdate` (`vsworldscene.cpp:2932-2951`) | Implemented |
| Parcel media | See Regions | Implemented |
| Task inventory | `VSTaskInventory` uses the shared item deserialisers over the `RequestTaskInventory` capability or legacy xfer (`vstaskinventory.cpp:1`, `:14`); request via `VSWorldScene::requestTaskInventory` (`vsworldscene.cpp:3037`) | Implemented |
| Object physics and costs | `VSObjectPhysics`, `VSObjectCosts`, created lazily (`vsnativesession.cpp:2167-2178`) | Implemented |
| Picking | `VSWorldScene::pick` / `pickHUD` (`vsworldscene.cpp:4983-5299`) | Implemented (covered in the interaction chapter) |
| Spatial index | R-tree for selection only (see Scene) | Partial (not used for rendering) |

---

## Threading and ownership summary

| Work | Thread | Evidence |
|---|---|---|
| Session state, admission, `receive`, `deliver` | Main | Handlers run from `LockMessageChecker` in `tick` (`vsnativesession.cpp:2573-2577`) |
| Seed, features, bake and mesh HTTP | `LLCoros` coroutines; results posted to `"mainloop"` | `vsnativesession.cpp:646`, `:735`; `vsworldassets.cpp:280` |
| Circuit ACK callback | Message system → posted to `"mainloop"` | `vsnativesession.cpp:307-316` |
| Scene and avatar mutation, packet build, CPU skinning | Main | No mutex in `vsworldscene.cpp` or `vsworldavatar.cpp` (`grep -ln "std::mutex\|std::thread\|LLMutex" vs*` lists only `vsmodelpreview.cpp`, `vsuifontcache.cpp`, `vsvkdiag.cpp` and three headers) |
| Mesh decode | Main | `vsworldassets.cpp:276-311` |
| Texture fetch and decode | Shared `LLTextureFetch` and decode threads | `vsuiimageprovider.cpp:804` |
| Image upload preparation (mips/BC3) | `"General"` work queue | `vsuiimageprovider.cpp:165-170`; `vsworldgltf.cpp:489`, `:665` |

Ownership: `LLAppViewer` owns the session. `VSVulkanContext` owns the renderer and
scene. The scene `Impl` is a `shared_ptr`, so guards and asynchronous work hold
`weak_ptr<Impl>`. The session owns `unique_ptr`s for appearance, task inventory,
physics, costs, attention, sound and particles, and a `shared_ptr` for selection
(`vsnativesession.h:258-265`). Shared singletons (`LLEnvironment`,
`LLMaterialMgr`, `LLViewerCamera`, `LLViewerPartSim`, `gAudiop`, `gInventory`) are
created on connect and retired in `reset`.

---

## Reuse vs replacement of shared code

| Shared viewer owner | Vulkan approach | Evidence |
|---|---|---|
| `LLWorld`, `LLViewerRegion` | **Replaced** by `RegionCircuit`, `VSRegionContext` and `VSRegionLimits`. `LLWorld` limits moved into `vsregionlimits.cpp` and are used by OpenGL too (`llworld.cpp` +42/−418 vs release) | `vsnativesession.h:41`; `git diff --stat origin/vkstorm-release...HEAD -- indra/newview/llworld.cpp` |
| `LLViewerObjectList`, `LLViewerObject`, `LLVOVolume`, `LLDrawable` | **Replaced** by `VSWorldScene::Impl::Object`. Shared CPU decoders are reused: `LLVolumeMessage`, `LLPrimitive::unpackTEMessage`, `LLVolume`, `LLPartSysData`, `LLDataPackerBinaryBuffer`. `gObjectList.isDerendered` is still consulted (`vsworldscene.cpp:1306`). | `vsworldscene.cpp:3712-3734` |
| `LLVOAvatar` | **Replaced** by `VSWorldAvatar`, which reuses `LLAvatarAppearance`, `LLWearableData`, `LLTexLayerSet`, `LLMotionController` motions and `LLPolyMesh`. Policies (motions, LOD, name tags, voice) were *extracted* from `llvoavatar.cpp` into `vs*` headers that both renderers include (`llvoavatar.cpp:30-36`, `:120`, `:3275`; `llvoavatar.cpp` +140/−1592 vs release). | `vsworldavatar.cpp:159` |
| `LLVOTree`, `LLVOGrass` | **Extracted** into `vsvegetation*`, used by both (`llvotree.cpp:28-31`; `llvotree.cpp` +40/−663) | `vsvegetation.h` |
| `LLSurface`, `LLSurfacePatch` | **Replaced** by the `Ground` map and `VSTerrainGeometry`; `patch_dct` reused | `vsworldscene.cpp:856-907` |
| `LLMeshRepository` | **Replaced** for render fetch (`VSWorldAssets`); still used for uploads and cost formulas | `vsworldassets.cpp`, `vsworldscene.cpp:6159` |
| `LLViewerTextureList`, `LLViewerFetchedTexture` | **Replaced** by `VSUIImageProvider`; `LLTextureFetch` and `LLTextureCache` reused | `vsuiimageprovider.cpp:804` |
| `LLEnvironment`, settings | **Reused** directly | `vsenvironmentstate.cpp` |
| `LLViewerParcelMgr` | **Reused**, with a Vulkan CPU mode (`mNativeCpu`) | `llviewerparcelmgr.cpp:150`, `:294`, `:2038` |
| `LLViewerPartSim` | **Reused** | `vsparticleeffects.cpp:89` |
| `LLAudioEngine`, `LLAudioSourceVO` | **Reused**, with `LLAudioSourceVO` extended for object-less state | `vsworldsound.cpp:76` |
| `LLGLTFMaterialList`, `LL::GLTF::Asset`, `LLMaterial` | **Reused** | `vsworldscene.cpp:1227`; `vsworldgltf.cpp` includes `gltf/asset.h` |
| `LLAgent`, `LLAgentCamera` | **Reused** as CPU owners; the session drives `updateNativeCamera` (`vsnativesession.cpp:2598`) | — |
| Capability catalogue | **Extracted** to `vscapabilities.cpp`, used by both | `llviewerregion.cpp:3460` |

Pattern: when an OpenGL class mixes CPU policy with GL ownership, the line pulls
the policy into a `vs*` file and makes the GL class call it. Pure CPU services are
reused as they are. The extraction changes OpenGL code on this line: the net
deltas against release in shared files are `llvoavatar.cpp` +140/−1592,
`llvotree.cpp` +40/−663, `llviewerobject.cpp` +108/−354, `llvovolume.cpp` +20/−279,
`llworld.cpp` +42/−418 (from `git diff --numstat origin/vkstorm-release...HEAD`).

---

## Divergences from the OpenGL viewer

| # | Divergence | Evidence | Intended? |
|---|---|---|---|
| D1 | Global object cap of 16384 across all regions; refused objects are re-requested at most 3 times, then dropped | `vsworldscene.cpp:108`, `:250`, `:1290-1305`, `:3746-3748` | Intended as a safety bound (comment-free). Inference: too low for dense SL regions with 4–8 neighbours. |
| D2 | No object cache; every `ObjectUpdateCached` becomes a full request | `:3538`, `:3753` | Probably interim. The parity doc lists "object cache equivalence" as remaining (`doc/vulkan/world-feature-parity.md:43`). |
| D3 | Terse updates with TEs trigger a full re-request | `:3579` | Probably a shortcut. |
| D4 | `KillObject` does not cascade to children; children keep `parentRetired` until killed | `:1233-1253` | Inference: intended, since the simulator kills children explicitly. Retired children stay in the cap. |
| D5 | Prim hover text not stored | `:1461` | Gap. |
| D6 | No avatar impostors | grep evidence above | Gap. |
| D7 | CPU skinning with a mesh re-upload per pose change | `vsworldscene.cpp:4784-4798`, `vsworldavatar.cpp:1895` | Inference: interim, pending GPU skinning in the renderer. |
| D8 | Separate mesh fetcher: whole-asset range per LOD, no disk cache, main-thread decode | `vsworldassets.cpp:222-311` | Partly intended (comments explain range admission); the duplication looks interim. |
| D9 | Water is a flat tinted quad | `vsworldscene.cpp:1171-1178` | Placeholder. |
| D10 | Prim LOD ignores the actual camera FOV | `:123` | Comment states it is current behaviour. |
| D11 | All in-flight texture fetches cancelled on region-epoch change | `vsuiimageprovider.cpp:262-273` | Intended (authorization changes with the capability host). Inference: crossings may cause reload stalls. |
| D12 | Parcel properties polled per 4 m cell | `vsnativesession.cpp:2603-2608` | Unclear. |
| D13 | At most 100 inbound messages per frame | `:2575` | Unclear. |
| D14 | Avatars capped at 256 (global) | `vsworldscene.cpp:250`, `:755` | Safety bound. |
| D15 | Hard `throw` on non-finite environment values in the frame path | `vsenvironmentstate.cpp:84-85` | Intended fail-fast; impact depends on the caller's exception handling (rendering chapter). |

---

## Risks and open questions

1. **Parcel reply routing (likely bug).** `ParcelAccessListReply`,
   `ParcelDwellReply`, `ParcelObjectOwnersReply` and `EstateCovenantReply` are
   admitted, but no handler is registered in Vulkan sessions
   (`vsnativesession.cpp:1356`, `:1433-1441`; `llstartup.cpp:2071-2077` vs `:2332`).
   This should be confirmed with a Vulkan login. If confirmed, the land access
   list, dwell, object owners and covenant views stay empty.
2. **O(N) per-update scans** in `replace`, `eraseObject`, `dirtyChildren`, the
   `ReplyTaskInventory` lookup and terrain grass dirtying (table in *Scene*). They
   need a profile under a busy region. The existing `objectIndex.children` /
   `identities` could replace most of them.
3. **CPU skinning and per-frame rigged uploads** (D7) are the most likely limit on
   crowd frame rate.
4. **The 16384-object global cap** (D1) together with **no object cache** (D2)
   means more network traffic and possibly missing objects in dense areas with
   neighbours.
5. **Mesh downloader** (D8): up to four full-range downloads per mesh and no disk
   cache, so meshes are re-fetched after every login. `gMeshRepo` is initialised
   anyway, which duplicates infrastructure.
6. **Shared-code coupling.** The capability catalogue, avatar motions, LOD
   policies, vegetation and region limits are now shared `vs*` code compiled into
   the OpenGL viewer. Any Vulkan fix to them changes OpenGL behaviour. The
   FlickrConnect episode (`116a410fc8` → `bcdfc2af48`) is an example that was
   caught by `test_native_capability_catalogue.py`.
7. **Two `begin()` call sites** (`llstartup.cpp:2074` and `llappviewer.cpp:1762`).
   Inference: harmless on success because `begin()` advances the state to
   `STATE_WORLD_WAIT` (`vsnativesession.cpp:613`). Whether a failed `begin()` that
   leaves `STATE_WORLD_INIT` set is re-entered each frame was not traced.
8. **Dead or duplicate routing** for `SimStats`, `AlertMessage`,
   `AgentAlertMessage` and `PayPriceReply` (Session section). Low risk, but it
   misleads readers about which code runs.
9. **`LayerData` admission** accepts any known host without the neighbour
   handshake that other world messages need (`vsnativesession.cpp:1355` vs
   `:1363-1364`). Inference: probably harmless, but inconsistent.
10. **Possible duplicate `RenderMaterials` fetches** by `LLMaterialMgr` and
    `VSMaterialAssets` (Materials section). Not verified.
11. **Local lights and reflection probes** are flags only in the scene model. No
    path to the renderer was found.
12. **Stale design docs.** `doc/vulkan/world-feature-parity.md:48` still says
    avatars and appearance are "Not admitted" and that mesh, sculpt, flexible
    objects and attachments are "explicitly omitted" (`:79-80`). The code
    contradicts both (Objects and Avatars sections).
13. **Test nature.** The 257 `test_native_*.py` fixtures mostly cut method bodies
    out of the sources and compile them against stubs (for example
    `scripts/tests/test_native_avatar_reset.py:14-18`). They check logic in
    isolation, not the integrated session against a simulator. The
    `VS_VULKAN_DIAGNOSTICS` replay harnesses (`vsnativereplay.cpp`) exercise the
    integrated path against a loopback fixture, but they are not in the default
    build. Whether any CI job runs them was not determined here.
